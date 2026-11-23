#include "pwm_service.h"

static bool running[2];
uint32_t pwm_driver_clock(void) { return 84000000; }
void pwm_driver_start(unsigned group, const pwm_period *period, uint32_t pulses)
{
    (void)period;
    (void)pulses;
    running[group] = true;
}
void pwm_driver_stop(unsigned group) { running[group] = false; }
bool pwm_driver_running(unsigned group) { return running[group]; }
