#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#include "hardware_sim.h"
#include "imu_sim.h"

#define BUTTON_NODE DT_ALIAS(sw0)
#define SIM_SAMPLE_PERIOD_MS 100

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);

int sim_init(void)
{
	return gpio_emul_input_set(button.port, button.pin, 1);
}

void sim_wakeup_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_sleep(K_SECONDS(1));

	while (1) {
		printk("[SIM] Triggering wakeup\n");
		gpio_emul_input_set(button.port, button.pin, 0);
		k_sleep(K_MSEC(100));
		gpio_emul_input_set(button.port, button.pin, 1);
		k_sleep(K_SECONDS(10));
	}
}

void sim_imu_thread(void *p1, void *p2, void *p3)
{
	uint8_t next_x = 0;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		struct imu_sample sample;

		k_sleep(K_MSEC(SIM_SAMPLE_PERIOD_MS));
		if (!imu_is_started()) {
			continue;
		}

		sample.accel_x = next_x++;
		sample.accel_y = 1;
		sample.accel_z = 2;

		if (imu_sim_push_sample(&sample) != 0) {
			printk("[SIM] IMU FIFO full; sample dropped\n");
		}
	}
}
