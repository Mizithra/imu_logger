# IMU Datalogger (Avelo Technical)

A motion-triggered IMU datalogger built with Zephyr RTOS. An external virtual GPIO interrupt wakes the application, which processes simulated IMU batches through a motion classifier. A blackbox algorithim determines if motion is detected.  While motion is detected, batches are streamed over a ZBus to a simulated BLE interface and a simulated flash interface. The application records lifecycle events and returns to sleep after five minutes of inactivity.

The project uses `native_sim` to validate application logic and message flow without requiring physical peripherals.

## Threads

| Thread | Priority | Responsibility |
|---|---:|---|
| `datalogger` | 4 | Waits for wakeup, processes IMU batches, manages the inactivity timer, and publishes lifecycle events. |
| `imu` | 5 | Simulates IMU acquisition, FIFO batching, and watermark notifications; publishes batches on `imu_zbus`. |
| `ble` | 6 | Simulates streaming batches containing motion. |
| `flash` | 6 | Logs lifecycle events and stores IMU batches associated with detected motion. |
| `Simulation threads` | 7 | Generate sensor activity and simulate external wakeup events. |

`main()` initializes the modules and starts the application threads. Lower numerical priority values indicate higher priority.

## Architecture

```mermaid

flowchart TD
    GPIO([External GPIO Interrupt])
    DL(Datalogger Thread)
    IMU(IMU Thread / Driver)
    IQ[(imu_zbus)]
    MQ[(motion_zbus)]
    BLE(BLE Thread)
    FLASH(Flash Thread)

    GPIO --> DL
    DL -->|Start / Stop| IMU
    IMU -->|Publish batches| IQ
    IQ -->|Subscribe| DL
    DL -->|Classified batches / events| MQ
    MQ --> BLE
    MQ --> FLASH

    classDef bus fill:#dcecff,stroke:#2878b5,color:#17365d
    class IQ,MQ bus

```

Two zbus channels separate sensor acquisition from application-level events:

- **`imu_zbus`** carries raw IMU batches to the datalogger.
- **`motion_zbus`** carries classified batches and lifecycle events to BLE and flash consumers.

The datalogger owns the activity lifecycle and is the only module that classifies motion. BLE and flash consume published messages rather than reading shared application state.

## Activity lifecycle

1. **Sleep:** The datalogger blocks on a wakeup semaphore. This represents sleeping in `native_sim`; it is not a real CPU power state.
2. **Wakeup:** A simulated falling-edge GPIO interrupt signals the datalogger, which starts the IMU and publishes a wakeup event.
3. **Watermark Interrupt:** Wait for the IMU to fill it's buffer until it hits it's watermark and publishes the FIFO data.
4. **Active monitoring:** IMU batches are classified and published with their motion verdict. Motion batches are sent to BLE and stored by flash.
5. **No motion:** The first no-motion result starts a one-shot five-minute timer. Subsequent no-motion batches do not restart it.
6. **Motion resumes:** The timer is stopped and its pending timeout notification is cleared.
7. **Sleep transition:** When the timer expires, the datalogger checks for queued IMU batches before completing the transition. It stops the IMU and publishes a sleep event.

The timer callback only signals the datalogger thread. The thread performs the state transition, keeping application logic out of the callback.

## Logging

Flash logging distinguishes lifecycle events from IMU data.

| Record | When it is generated |
|---|---|
| `WAKEUP` | The application wakes from its simulated sleep state. |
| `MOTION_DETECTED` | The classifier transitions from no motion to motion. |
| `NO_MOTION_DETECTED` | The classifier transitions from motion to no motion. |
| `SLEEP` | The application enters its simulated sleep state. |
| `IMU batch` | A batch is received while motion is detected. |

Motion and no-motion events are logged on state changes, rather than for every batch. IMU batches

## AI Usage
I used AI (Claude and ChatGPT) as a development assistant throughout the assessment to help review the requirements, explore Zephyr RTOS design options, troubleshoot build issues, and improve the documentation.

I reviewed the suggestions and made the final design decisions based on my understanding of the requirements and the constraints of the simulated environment.

AI assistance was particularly useful for reviewing edge cases and creating mock functions. I treated its suggestions as input to evaluate rather than assuming generated code or explanations were automatically correct.

As I am more familliar with FreeRtos, I also leverage AI for clarifying syntax, and what the equivalent Zephyr functions are compared to FreeRtos functions.

# Criteria

## Receives a wakeup signal from an external INT GPIO.
Simulated using Zephyr's GPIO API and a virtual GPIO device.

The interrupt-driven approach avoids continuously polling the wakeup input, reducing unnecessary CPU activity.

**Assumption**: native_sim emulates the GPIO event but does not reproduce a physical interrupt controller or CPU sleep state.

## Upon wakeup, initializes/starts the IMU and consumes data from its FIFO via a watermark interrupt.
A simulated IMU module provides initialization, start/stop controls, FIFO batches, and watermark notifications.

**Assumption**: The simulated FIFO and watermark reproduce the intended data flow, not the timing or hardware behavior of a physical sensor.

## Passes all IMU FIFO data through a black-box algorithm to evaluate whether motion activity is detected.
A black-box classifier interface evaluates every received IMU batch.

**Assumption**: The current classifier is a deterministic mock that reports motion for the first ten batches after wakeup, then reports no motion. It demonstrates the application flow but is not a real motion-detection algorithm.
## Continuously streams incoming IMU FIFO data live over BLE ONLY when the black-box algorithm reports active motion.
A simulated BLE consumer subscribes to motion_zbus.

**Assumption**: The BLE interface validates application-level message flow. Actual Bluetooth connections, GATT characteristics, MTU negotiation, packet fragmentation, and radio behavior are outside the simulation.
## Stores IMU data to an external flash device ONLY when the black-box algorithm reports active motion.
A simulated flash consumer subscribes to motion_zbus.

**Assumption**: The current storage implementation is a fixed-capacity, RAM-backed mock. It demonstrates record handling but does not provide persistent storage across resets or model physical flash erase behavior, endurance, or power-loss recovery.
## Logs distinct system events to the same flash device (specifically: wakeup time, activity detected time, inactivity detected time, and sleep time).
The datalogger publishes explicit lifecycle events to motion_zbus, which the flash consumer records separately from IMU samples.

## Streams historical stored IMU data from flash to a central device upon receiving a read/offload request over BLE.


## Monitors activity state: if no motion activity is detected for 5 continuous minutes, logs inactivity and sleep events, stops the IMU, and returns the MCU to low-power sleep.
The datalogger owns the activity lifecycle and uses a one-shot Zephyr timer.

**Assumption**: The timer measures five minutes from the first no-motion classification, rather than five minutes since the last motion-positive batch.

**Simulation limitation**: Waiting on a semaphore emulates an idle application thread. It does not put the simulated CPU into a real low-power state.
## Must be designed with CR2032 coin cell power operational constraints in mind.
The architecture is designed to minimize unnecessary processing, radio activity, and sensor operation, with hardware deep sleep as the intended deployment behavior.

The principal power-saving choices are:
- Use an external interrupt as the wakeup mechanism rather than continuously polling.
- Batch IMU samples through the FIFO and watermark mechanism.
- Gate live BLE streaming and IMU storage on the classifier's motion verdict.  Stops burning cycles on data that doesn't need to be saved/transmitted
- Stop the IMU when the inactivity timeout completes.
- Block on semaphores and synchronization primitives when work is unavailable.
- Keep the simulated peripheral implementations simple so they do not obscure the intended power-management architecture.

Assumption: The current native_sim implementation cannot demonstrate actual CR2032 battery life, current consumption, peak-current behavior, or wakeup latency. These require measurements on the target hardware. The IMU, BLE radio, and flash device must be selected and configured to meet the battery's average- and peak-current constraints
