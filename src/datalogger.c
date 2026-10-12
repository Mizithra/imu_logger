#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/zbus/zbus.h>

#include "datalogger.h"
#include "imu_sim.h"

#define BUTTON_NODE DT_ALIAS(sw0)
#define INACTIVITY_TIMEOUT K_MINUTES(5)
#define SIM_MOTION_BATCHES 10

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static struct gpio_callback button_cb_data;
static K_SEM_DEFINE(wakeup_sem, 0, 1);
static K_SEM_DEFINE(inactivity_sem, 0, 1);
static uint32_t sim_batch_count;

ZBUS_MSG_SUBSCRIBER_DEFINE(logger_sub);
ZBUS_CHAN_ADD_OBS(imu_zbus, logger_sub, 1);

ZBUS_CHAN_DEFINE(
	motion_zbus,
	struct motion_msg,
	NULL,
	NULL,
	ZBUS_OBSERVERS_EMPTY,
	ZBUS_MSG_INIT(0)
);

static void inactivity_timer_callback(struct k_timer *timer)
{
	k_sem_give(&inactivity_sem);
}

K_TIMER_DEFINE(inactivity_timer, inactivity_timer_callback, NULL);

static void button_pressed_callback(const struct device *dev,
				    struct gpio_callback *cb, uint32_t pins)
{
	k_sem_give(&wakeup_sem);
}

// black box algorithim
static bool detect_motion(const struct imu_data *data)
{
	/* Replace with the assessment's black-box classifier. */
	return sim_batch_count++ < SIM_MOTION_BATCHES;
}

// Publish message to 
static int publish_message(enum motion_msg_type type, bool motion,
			   const struct imu_data *data)
{
	struct motion_msg msg = {
		.type = type,
		.motion = motion,
	};

	if (data != NULL) {
		msg.data = *data;
	}

	return zbus_chan_pub(&motion_zbus, &msg, K_MSEC(100));
}

static void publish_event(enum motion_msg_type type)
{
	int err = publish_message(type, false, NULL);

	if (err) {
		printk("[Datalogger] Event publish failed (%d)\n", err);
	}
}

int datalogger_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_pressed_callback,
			   BIT(button.pin));

	ret = gpio_add_callback(button.port, &button_cb_data);
	if (ret < 0) {
		return ret;
	}

	return gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_FALLING);
}

static bool process_batch(struct imu_data *batch, bool *inactivity_pending,
			  bool *have_motion_state, bool *last_motion)
{
	bool motion;
	int err;

	if (batch->count > IMU_BATCH_SIZE) {
		printk("[Datalogger] Invalid sample count %u; clamping\n",
		       batch->count);
		batch->count = IMU_BATCH_SIZE;
	}

	motion = detect_motion(batch);

	if (!*have_motion_state || motion != *last_motion) {
		publish_event(motion ? MOTION_MSG_MOTION_DETECTED :
				      MOTION_MSG_NO_MOTION_DETECTED);
		*have_motion_state = true;
		*last_motion = motion;
	}

	err = publish_message(MOTION_MSG_BATCH, motion, batch);
	if (err) {
		printk("[Datalogger] Batch publish failed (%d)\n", err);
	}

	if (motion) {
		k_timer_stop(&inactivity_timer);
		k_sem_reset(&inactivity_sem);
		*inactivity_pending = false;
	} else if (!*inactivity_pending) {
		k_sem_reset(&inactivity_sem);
		k_timer_start(&inactivity_timer, INACTIVITY_TIMEOUT, K_NO_WAIT);
		*inactivity_pending = true;
	}

	return motion;
}


static bool inactivity_timeout_reached(bool *inactivity_pending,
				       bool *have_motion_state,
				       bool *last_motion)
{
	const struct zbus_channel *chan;
	struct imu_data batch;
	bool motion_seen = false;

	if (k_sem_take(&inactivity_sem, K_NO_WAIT) != 0) {
		return false;
	}

	return *inactivity_pending && !motion_seen;
}

static void run_active(void)
{
	bool inactivity_pending = false;
	bool have_motion_state = false;
	bool last_motion = false;
	const struct zbus_channel *chan;

	sim_batch_count = 0;
	k_timer_stop(&inactivity_timer);
	k_sem_reset(&inactivity_sem);

	if (imu_start() < 0) {
		printk("[Datalogger] Failed to start IMU\n");
		return;
	}

	while (1) {
		struct imu_data batch;
		int err = zbus_sub_wait_msg(&logger_sub, &chan, &batch,
					    K_MSEC(50));

		if (err == 0) {
			process_batch(&batch, &inactivity_pending,
						&have_motion_state, &last_motion);
						
		} else if (err != -ENOMSG && err != -EAGAIN) {
			printk("[Datalogger] IMU message wait failed (%d)\n", err);
		}

		if (inactivity_timeout_reached(&inactivity_pending,
					       &have_motion_state,
					       &last_motion)) {
			//Return and enter_sleep
			return;
		}
	}
}

static void enter_sleep(void)
{
	const struct zbus_channel *chan;
	struct imu_data stale;

	k_timer_stop(&inactivity_timer);
	imu_stop();
	publish_event(MOTION_MSG_SLEEP);

	while (zbus_sub_wait_msg(&logger_sub, &chan, &stale, K_NO_WAIT) == 0) {
	}

	k_sem_reset(&wakeup_sem);
}

void datalogger_thread(void *p1, void *p2, void *p3)
{

	while (1) {
		printk("[Datalogger] Sleeping; waiting for wakeup\n");
		k_sem_take(&wakeup_sem, K_FOREVER);

		printk("[Datalogger] Wakeup received\n");
		publish_event(MOTION_MSG_WAKE);
		run_active();
		enter_sleep();
	}
}
