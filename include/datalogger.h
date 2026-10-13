#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/zbus/zbus.h>
#include "imu_sim.h"

enum motion_msg_type {
	MOTION_MSG_WAKE,
	MOTION_MSG_MOTION_DETECTED,
	MOTION_MSG_NO_MOTION_DETECTED,
	MOTION_MSG_BATCH,
	MOTION_MSG_SLEEP,
};

struct motion_msg {
	enum motion_msg_type type;
	uint32_t timestamp_ms;
	bool motion;
	struct imu_data data;
};

ZBUS_CHAN_DECLARE(motion_zbus);

int datalogger_init(void);
void datalogger_thread(void *p1, void *p2, void *p3);
