#pragma once

/* native_sim-only wakeup and sensor input. */
int sim_init(void);
void sim_wakeup_thread(void *p1, void *p2, void *p3);
void sim_imu_thread(void *p1, void *p2, void *p3);
