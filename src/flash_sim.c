#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "flash_sim.h"
#include "datalogger.h"

#define FLASH_RECORD_CAPACITY 256

static struct flash_record records[FLASH_RECORD_CAPACITY];
static uint16_t record_count;
K_MUTEX_DEFINE(records_mutex);

ZBUS_MSG_SUBSCRIBER_DEFINE(flash_sub);
ZBUS_CHAN_ADD_OBS(motion_zbus, flash_sub, 3);

// Convert Flash Event ENUM into Text String
static const char *event_name(enum flash_event event)
{
	switch (event) {
	case FLASH_EVT_WAKEUP:
		return "WAKEUP";
	case FLASH_EVT_MOTION_DETECTED:
		return "MOTION_DETECTED";
	case FLASH_EVT_NO_MOTION_DETECTED:
		return "NO_MOTION_DETECTED";
	case FLASH_EVT_SLEEP:
		return "SLEEP";
	default:
		return "UNKNOWN";
	}
}

int flash_sim_init(void)
{
	record_count = 0;
	return 0;
}

// Store record in flash
static int store_record(struct flash_record *record)
{
	int err = 0;

	k_mutex_lock(&records_mutex, K_FOREVER);
	if (record_count >= FLASH_RECORD_CAPACITY) {
		err = -ENOSPC;
	} else {
		record->timestamp_ms = k_uptime_get_32();
		records[record_count++] = *record;
	}
	k_mutex_unlock(&records_mutex);

	if (err) {
		printk("[Flash] Log full; record dropped\n");
	}
	return err;
}

// Simulate logging to flash
int flash_sim_log_event(enum flash_event event)
{
	struct flash_record record = {
		.type = FLASH_RECORD_EVENT,
		.event = event,
	};
	int err;

	if ((unsigned int)event > FLASH_EVT_SLEEP) {
		return -EINVAL;
	}

	err = store_record(&record);
	if (!err) {
		printk("[Flash][Event] %s\n", event_name(event));
	}

	return err;
}

int flash_sim_append(const struct imu_data *data)
{
	struct flash_record record = {
		.type = FLASH_RECORD_IMU,
	};

	if (data == NULL) {
		return -EINVAL;
	}

	record.data = *data;
	if (store_record(&record) == 0) {
		printk("[Flash][IMU] Stored batch (%u samples)\n", data->count);
		return 0;
	}

	return -ENOSPC;
}

int flash_sim_read(uint16_t index, struct flash_record *record)
{
	if (record == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&records_mutex, K_FOREVER);
	if (index >= record_count) {
		k_mutex_unlock(&records_mutex);
		return -ENOENT;
	}

	*record = records[index];
	k_mutex_unlock(&records_mutex);
	return 0;
}

void flash_thread(void *p1, void *p2, void *p3)
{
	const struct zbus_channel *chan;

	// Wait for motion data zbus message
	while (1) {
		struct motion_msg msg;
		int err = zbus_sub_wait_msg(&flash_sub, &chan, &msg, K_FOREVER);

		if (err) {
			printk("[Flash] Message wait failed (%d)\n", err);
			continue;
		}

		// Depending on message recieved,
		switch (msg.type) {
		case MOTION_MSG_WAKE:
			flash_sim_log_event(FLASH_EVT_WAKEUP);
			break;
		case MOTION_MSG_MOTION_DETECTED:
			flash_sim_log_event(FLASH_EVT_MOTION_DETECTED);
			break;
		case MOTION_MSG_NO_MOTION_DETECTED:
			flash_sim_log_event(FLASH_EVT_NO_MOTION_DETECTED);
			break;
		case MOTION_MSG_BATCH:
			if (msg.motion) {
				flash_sim_append(&msg.data);
			}
			break;
		case MOTION_MSG_SLEEP:
			flash_sim_log_event(FLASH_EVT_SLEEP);
			break;
		default:
			break;
		}
	}
}
