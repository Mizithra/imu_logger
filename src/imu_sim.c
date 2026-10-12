#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "imu_sim.h"

ZBUS_CHAN_DEFINE(
	imu_zbus,
	struct imu_data,
	NULL,
	NULL,
	ZBUS_OBSERVERS_EMPTY,
	ZBUS_MSG_INIT(0)
);

K_MSGQ_DEFINE(imu_fifo, sizeof(struct imu_sample), IMU_FIFO_DEPTH, 1);

static K_SEM_DEFINE(watermark_sem, 0, 1);
static atomic_t started;

static void watermark_interrupt(void)
{
	k_sem_give(&watermark_sem);
}

int imu_init(void)
{
	printk("[IMU] Initialized\n");
	return 0;
}

int imu_start(void)
{
	k_msgq_purge(&imu_fifo);
	k_sem_reset(&watermark_sem);
	atomic_set(&started, 1);
	printk("[IMU] Started\n");
	return 0;
}

int imu_stop(void)
{
	atomic_set(&started, 0);
	k_msgq_purge(&imu_fifo);
	k_sem_reset(&watermark_sem);
	printk("[IMU] Stopped\n");
	return 0;
}

bool imu_is_started(void)
{
	return atomic_get(&started) != 0;
}

int imu_fifo_read(struct imu_sample *out, size_t max)
{
	size_t count = 0;

	while (count < max &&
	       k_msgq_get(&imu_fifo, &out[count], K_NO_WAIT) == 0) {
		count++;
	}

	return count;
}

int imu_sim_push_sample(const struct imu_sample *sample)
{
	if (sample == NULL) {
		return -EINVAL;
	}

	if (k_msgq_put(&imu_fifo, sample, K_NO_WAIT) != 0) {
		return -ENOSPC;
	}

	if (k_msgq_num_used_get(&imu_fifo) >= IMU_BATCH_SIZE) {
		watermark_interrupt();
	}

	return 0;
}

void imu_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		k_sem_take(&watermark_sem, K_FOREVER);

		while (k_msgq_num_used_get(&imu_fifo) >= IMU_BATCH_SIZE) {
			struct imu_data batch = {0};
			int count = imu_fifo_read(batch.samples, IMU_BATCH_SIZE);
			int err;

			if (count <= 0) {
				break;
			}

			batch.count = count;
			err = zbus_chan_pub(&imu_zbus, &batch, K_MSEC(100));
			if (err) {
				printk("[IMU] Batch publish failed (%d)\n", err);
			}
		}
	}
}
