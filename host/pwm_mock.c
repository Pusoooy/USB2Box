#include "pwm_service.h"
#include "mock_clock.h"

static struct {
    pwm_period current, next;
    uint32_t phase, pulses;
    bool running, pending;
} outputs[2];

static uint32_t last_ms;

uint32_t pwm_driver_clock(void) { return 84000000; }
void pwm_driver_start(unsigned group, const pwm_period *period, uint32_t pulses)
{
    outputs[group].current = *period;
    outputs[group].phase = 0;
    outputs[group].pulses = pulses;
    outputs[group].running = true;
    outputs[group].pending = false;
}
void pwm_driver_stop(unsigned group)
{
    outputs[group].running = outputs[group].pending = false;
}

bool pwm_driver_running(unsigned group) { return outputs[group].running; }

bool pwm_driver_update(unsigned group, const pwm_period *period)
{
    if (!outputs[group].running) return false;
    outputs[group].next = *period;
    outputs[group].pending = true;
    return true;
}

bool pwm_driver_update_pending(unsigned group) { return outputs[group].pending; }

void pwm_mock_tick(uint32_t now)
{
    const uint32_t delta = now - last_ms;
    last_ms = now;
    for (unsigned group = 0; group < 2; ++group) {
        if (!outputs[group].running) continue;
        const uint64_t total = outputs[group].phase +
                               (uint64_t)delta * outputs[group].current.frequency_millihz;
        const uint64_t cycles = total / 1000000u;
        outputs[group].phase = (uint32_t)(total % 1000000u);
        if (cycles && outputs[group].pending) {
            outputs[group].current = outputs[group].next;
            outputs[group].pending = false;
        }
        if (!outputs[group].pulses) continue;
        if (cycles >= outputs[group].pulses) pwm_driver_stop(group);
        else outputs[group].pulses -= (uint32_t)cycles;
    }
}

static uint32_t input_clock;
bool pwm_capture_start(uint32_t counter_hz)
{
    input_clock = counter_hz;
    return true;
}
bool pwm_capture_read(uint32_t *period, uint32_t *high, uint32_t *counter_hz)
{
    const unsigned source = outputs[0].running ? 0 : 1;
    if (!input_clock || !outputs[source].running) return false;
    const pwm_period *signal = &outputs[source].current;
    const uint64_t ticks = ((uint64_t)input_clock * 1000 + signal->frequency_millihz / 2) /
                           signal->frequency_millihz;
    if (!ticks || ticks > UINT16_MAX) return false;
    *period = (uint32_t)ticks;
    const uint32_t ppm = signal->invert ? 1000000 - signal->duty_ppm : signal->duty_ppm;
    *high = (uint32_t)((ticks * ppm + 500000) / 1000000);
    *counter_hz = input_clock;
    return true;
}
void pwm_capture_stop(void) { input_clock = 0; }
