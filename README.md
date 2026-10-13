# IMU Datalogger (Zephyr, native_sim)

A motion-triggered IMU datalogger built on Zephyr RTOS and run on `native_sim`. Peripherals are mocked: the GPIO is emulated, and the IMU, BLE and flash are stubs. The goal is to show application structure and message flow, not hardware behavior.

## Build / run
```
west build -b native_sim .
west build -t run        # or ./build/zephyr/zephyr.exe
```

## How it works
1. `sim_wakeup_thread` pulses the emulated `sw0` GPIO low (falling edge) 10 s after boot, then every 6 minutes.
2. The GPIO callback gives a semaphore. The `datalogger` thread wakes, publishes a `WAKE` event and starts the IMU.
3. `sim_imu_thread` pushes one sample every 100 ms into a 20-entry message queue (the mock FIFO) while the IMU is started. When the queue holds 10 samples (`IMU_BATCH_SIZE`), it gives a semaphore (the mock watermark interrupt).
4. The `imu` thread reads a 10-sample batch from the FIFO and publishes it on `imu_zbus`.
5. The datalogger classifies each batch with `detect_motion()`. This is a mock that reports motion for the first 10 batches after each wakeup and no motion after that.
   - On a change of classification (and on the first batch), it publishes `MOTION_DETECTED` or `NO_MOTION_DETECTED`.
   - Motion batches are published on `motion_zbus` as `BATCH`. No-motion batches are dropped here, so BLE and flash never see them.
6. The first no-motion batch starts a one-shot 5-minute timer. Any motion batch cancels it.
7. When the timer fires, the datalogger stops the IMU, publishes `SLEEP`, drains stale batches and blocks on the wakeup semaphore again.

```mermaid
flowchart TD
    GPIO([Emulated GPIO]) --> DL(datalogger)
    DL -->|start / stop| IMU(imu)
    IMU -->|batches| IQ[(imu_zbus)]
    IQ --> DL
    DL -->|events + motion batches| MQ[(motion_zbus)]
    MQ --> BLE(ble)
    MQ --> FLASH(flash)
```

## Threads
| Thread | Prio | Role |
|---|---:|---|
| `datalogger` | 4 | Lifecycle, classification, inactivity timer, event publishing |
| `imu` | 5 | Drains FIFO on watermark, publishes batches |
| `ble` | 6 | Prints "streaming" for each motion batch |
| `flash` | 6 | Stores events and motion batches in a RAM log |
| `sim_imu`, `sim_wakeup` | 7 | Generate samples and wakeup pulses |

Each thread has a 1 KB stack. Lower numbers mean higher priority.

## Flash log
- RAM-backed array of 256 records, protected by a mutex.
- Record types are events (`WAKEUP`, `MOTION_DETECTED`, `NO_MOTION_DETECTED`, `SLEEP`) and IMU batches. Every record carries a `k_uptime_get_32()` timestamp. The timestamp is stored but not printed.
- When the log is full, new records are dropped with a message. Events and batches share the same space.

## BLE
- Live streaming is a stub (`tx_samples`) that prints the batch size.
- `rx_callback()` submits a work item that walks the flash log and prints each record. **Nothing calls `rx_callback()` in this simulation, so the offload path is implemented but never exercised.**

## Power design (intent)
- GPIO interrupt wakeup instead of polling.
- IMU FIFO batching, so consumers are woken once per 10 samples.
- Classification gates BLE and flash at the source.
- The IMU is stopped on inactivity.
- Threads block on semaphores and zbus when they have no work.

## Known limitations
- **Inactivity vs. sleep:** there is no separate inactivity event. `NO_MOTION_DETECTED` is logged at the first no-motion batch, and `SLEEP` is logged when the 5-minute timer fires.
- **Datalogger polling:** while active, the datalogger waits on zbus with a 50 ms timeout and checks the timeout semaphore each pass. It is not fully blocking.
- **Sleep is not real:** `CONFIG_PM` is on but no power states are used. Sleep is a blocked semaphore. Hardware would use deep sleep with GPIO wake and an RTC for timestamps.
- **BLE:** streaming is unconditional (no connection or subscription check), and offload has no MTU pacing, flow control or disconnect handling.
- **Flash:** the real device would need page buffering, a ring log, erase handling and power-loss recovery.
- **Classifier:** `detect_motion()` is a mock.
- **CR2032:** battery life, peak current and wake latency can't be shown on `native_sim` and need measurement on target hardware.
- **Unused config:** `MAIN_SLEEP_MS` is defined but not used, and the Zephyr logging module is not used (`printk` only).
- **Stack sizes** need measuring on target (e.g. `CONFIG_THREAD_ANALYZER`).

## Sample output (abbreviated)
```
[IMU] Initialized
[Datalogger] Sleeping; waiting for wakeup
[SIM] Triggering wakeup
[Datalogger] Wakeup received
[IMU] Started
[Flash][Event] WAKEUP
[BLE] Streaming 10 IMU samples
[Flash][Event] MOTION_DETECTED
[Flash][IMU] Stored batch (10 samples)
... (10 motion batches)
[Flash][Event] NO_MOTION_DETECTED
[IMU] Stopped
[Datalogger] Sleeping; waiting for wakeup
[Flash][Event] SLEEP
[SIM] Triggering wakeup
```

## AI usage
I used Claude and ChatGPT to review requirements, explore Zephyr options, troubleshoot builds, mock peripherals and improve documentation. I'm more familiar with FreeRTOS, so I also used them to map concepts to Zephyr equivalents.