#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "ble_sim.h"
#include "datalogger.h"

ZBUS_MSG_SUBSCRIBER_DEFINE(ble_sub);
ZBUS_CHAN_ADD_OBS(motion_zbus, ble_sub, 2);

void ble_thread(void *p1, void *p2, void *p3)
{
	const struct zbus_channel *chan;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		struct motion_msg msg;
		int err = zbus_sub_wait_msg(&ble_sub, &chan, &msg, K_FOREVER);

		if (err) {
			printk("[BLE] Message wait failed (%d)\n", err);
			continue;
		}

		if (msg.type != MOTION_MSG_BATCH || !msg.motion) {
			continue;
		}

		if (msg.data.count > IMU_BATCH_SIZE) {
			msg.data.count = IMU_BATCH_SIZE;
		}

		/* Replace with the BLE notify/write call on hardware. */
		printk("[BLE] Streaming %u IMU samples\n", msg.data.count);
	}
}
