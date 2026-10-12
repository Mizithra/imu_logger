#pragma once

#include <stdint.h>
#include "imu_sim.h"

enum flash_event {
	FLASH_EVT_WAKEUP,
	FLASH_EVT_MOTION_DETECTED,
	FLASH_EVT_NO_MOTION_DETECTED,
	FLASH_EVT_SLEEP,
};

enum flash_record_type {
	FLASH_RECORD_EVENT,
	FLASH_RECORD_IMU,
};

struct flash_record {
	uint32_t timestamp_ms;
	enum flash_record_type type;
	enum flash_event event;
	struct imu_data data;
};

int flash_sim_init(void);
int flash_sim_append(const struct imu_data *data);
int flash_sim_log_event(enum flash_event event);
int flash_sim_read(uint16_t index, struct flash_record *record);
void flash_thread(void *p1, void *p2, void *p3);
