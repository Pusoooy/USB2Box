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

static uint32_t input_clock;
bool pwm_capture_start(uint32_t counter_hz)
{
    input_clock = counter_hz;
    return true;
}
bool pwm_capture_read(uint32_t *period, uint32_t *high, uint32_t *counter_hz)
{
    if (!input_clock || !running[0]) return false;
    *period = 1000;
    *high = 250;
    *counter_hz = input_clock;
    return true;
}
void pwm_capture_stop(void) { input_clock = 0; }
