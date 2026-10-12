#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#define IMU_BATCH_SIZE 10
#define IMU_FIFO_DEPTH (IMU_BATCH_SIZE * 2)

struct imu_sample {
	uint8_t accel_x;
	uint8_t accel_y;
	uint8_t accel_z;
};

struct imu_data {
	struct imu_sample samples[IMU_BATCH_SIZE];
	uint8_t count;
};

int imu_init(void);
int imu_start(void);
int imu_stop(void);
bool imu_is_started(void);
int imu_fifo_read(struct imu_sample *out, size_t max);

ZBUS_CHAN_DECLARE(imu_zbus);
void imu_thread(void *p1, void *p2, void *p3);

/* Called by the native_sim sensor stub. */
int imu_sim_push_sample(const struct imu_sample *sample);
