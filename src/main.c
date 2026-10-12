#include <zephyr/kernel.h>

#include "datalogger.h"
#include "ble_sim.h"
#include "flash_sim.h"
#include "imu_sim.h"
#include "hardware_sim.h"

#define APP_STACK_SIZE 1024
#define PRIO_DATALOGGER 4
#define PRIO_IMU 5
#define PRIO_BLE 6
#define PRIO_FLASH 6
#define PRIO_SIM 7

// Create thread stacks
static K_THREAD_STACK_DEFINE(datalogger_stack, APP_STACK_SIZE);
static K_THREAD_STACK_DEFINE(imu_stack, APP_STACK_SIZE);
static K_THREAD_STACK_DEFINE(ble_stack, APP_STACK_SIZE);
static K_THREAD_STACK_DEFINE(flash_stack, APP_STACK_SIZE);
static K_THREAD_STACK_DEFINE(sim_imu_stack, APP_STACK_SIZE);
static K_THREAD_STACK_DEFINE(sim_wakeup_stack, APP_STACK_SIZE);

static struct k_thread datalogger_thread_data;
static struct k_thread imu_thread_data;
static struct k_thread ble_thread_data;
static struct k_thread flash_thread_data;
static struct k_thread sim_imu_thread_data;
static struct k_thread sim_wakeup_thread_data;

// Create thread and name it
static void spawn(struct k_thread *thread, k_thread_stack_t *stack,
		  size_t stack_size, k_thread_entry_t entry, int priority,
		  const char *name)
{
	k_tid_t tid = k_thread_create(thread, stack, stack_size, entry,
				      NULL, NULL, NULL, priority, 0, K_NO_WAIT);

	k_thread_name_set(tid, name);
}

// Initialize peripherals and launch threads
int main(void)
{
	int ret;

	printk("*** Booting Zephyr OS on native_sim ***\n");

	// Check peirpherals can be initialized without errors
	ret = datalogger_init();
	if (ret < 0) {
		printk("datalogger_init failed (%d)\n", ret);
		return ret;
	}

	ret = sim_init();
	if (ret < 0) {
		printk("sim_init failed (%d)\n", ret);
		return ret;
	}

	ret = imu_init();
	if (ret < 0) {
		printk("imu_init failed (%d)\n", ret);
		return ret;
	}

	ret = flash_sim_init();
	if (ret < 0) {
		printk("flash_sim_init failed (%d)\n", ret);
		return ret;
	}

	// Create each thread for each peripheral
	spawn(&datalogger_thread_data, datalogger_stack,
	      K_THREAD_STACK_SIZEOF(datalogger_stack),
	      datalogger_thread, PRIO_DATALOGGER, "datalogger");
	spawn(&imu_thread_data, imu_stack, K_THREAD_STACK_SIZEOF(imu_stack),
	      imu_thread, PRIO_IMU, "imu");
	spawn(&ble_thread_data, ble_stack, K_THREAD_STACK_SIZEOF(ble_stack),
	      ble_thread, PRIO_BLE, "ble");
	spawn(&flash_thread_data, flash_stack, K_THREAD_STACK_SIZEOF(flash_stack),
	      flash_thread, PRIO_FLASH, "flash");
	spawn(&sim_imu_thread_data, sim_imu_stack,
	      K_THREAD_STACK_SIZEOF(sim_imu_stack),
	      sim_imu_thread, PRIO_SIM, "sim_imu");
	spawn(&sim_wakeup_thread_data, sim_wakeup_stack,
	      K_THREAD_STACK_SIZEOF(sim_wakeup_stack),
	      sim_wakeup_thread, PRIO_SIM, "sim_wakeup");

	return 0;
}
