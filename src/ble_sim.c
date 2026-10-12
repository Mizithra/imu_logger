#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "ble_sim.h"
#include "datalogger.h"

ZBUS_MSG_SUBSCRIBER_DEFINE(ble_sub);
ZBUS_CHAN_ADD_OBS(motion_zbus, ble_sub, 2);

// Callback for receiving payload request.
void rx_callback()
{
	// payload message identified
	//Notify thread to handle the historical data payload
}

// Send samples out of BLE
void tx_samples(struct motion_msg msg)
{
	// Create and send out IMU data over BLE
	printk("[BLE] Streaming %u IMU samples\n", msg.data.count);

}
void ble_thread(void *p1, void *p2, void *p3)
{
	const struct zbus_channel *chan;

	// Wait for motion data zbus message
	while (1) {
		struct motion_msg msg;
		int err = zbus_sub_wait_msg(&ble_sub, &chan, &msg, K_FOREVER);

		if (err) {
			printk("[BLE] Message wait failed (%d)\n", err);
			continue;
		}

		// If not a Batch message, skip.  These are the Events for the Flash to log
		// Note: This could be handled by having a different queue or bus for the flash events
		//       but for this assessment I have re-used the same bus to save time.
		if (msg.type != MOTION_MSG_BATCH || !msg.motion) {
			continue;
		}

		if (msg.data.count > IMU_BATCH_SIZE) {
			msg.data.count = IMU_BATCH_SIZE;
		}

		/* Replace with the BLE notify/write call on hardware. */
		// Publish samples over BLE
		tx_samples(msg);
	}
}

