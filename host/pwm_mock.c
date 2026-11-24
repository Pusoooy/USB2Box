#include "pwm_service.h"
#include "mock_clock.h"

static struct {
    pwm_period current[PWM_CHANNELS], next[PWM_CHANNELS];
    uint32_t phase, pulses[PWM_CHANNELS];
    uint8_t active, waiting_for_period;
    bool pending;
} outputs[PWM_GROUPS];

static uint32_t last_ms;
static uint64_t adc_cycles;
static bool adc_enabled;

uint32_t pwm_driver_clock(void) { return 84000000; }

void pwm_driver_start(unsigned group, unsigned channel, const pwm_period *period, uint32_t pulses)
{
    const uint8_t bit = (uint8_t)(1u << channel);
    if (!outputs[group].active) outputs[group].phase = 0;
    else if (pulses) outputs[group].waiting_for_period |= bit;
    outputs[group].current[channel] = *period;
    outputs[group].pulses[channel] = pulses;
    outputs[group].active |= bit;
}

void pwm_driver_stop(unsigned group, unsigned channel)
{
    const uint8_t bit = (uint8_t)(1u << channel);
    outputs[group].active &= (uint8_t)~bit;
    outputs[group].waiting_for_period &= (uint8_t)~bit;
    if (!outputs[group].active) outputs[group].pending = false;
}

bool pwm_driver_running(unsigned group, unsigned channel)
{
    return (outputs[group].active & (1u << channel)) != 0;
}

bool pwm_driver_update(unsigned group, const pwm_period periods[PWM_CHANNELS])
{
    if (!outputs[group].active) return false;
    for (unsigned channel = 0; channel < PWM_CHANNELS; ++channel)
        outputs[group].next[channel] = periods[channel];
    outputs[group].pending = true;
    return true;
}

bool pwm_driver_update_pending(unsigned group) { return outputs[group].pending; }

bool pwm_driver_adc_trigger(bool enable)
{
    if (enable && !outputs[1].active) return false;
    adc_enabled = enable;
    if (enable) adc_cycles = 0;
    return true;
}

uint64_t pwm_mock_adc_cycles(void) { return adc_cycles; }

void pwm_mock_tick(uint32_t now)
{
    const uint32_t delta = now - last_ms;
    last_ms = now;
    for (unsigned group = 0; group < PWM_GROUPS; ++group) {
        const uint8_t active = outputs[group].active;
        if (!active) continue;
        const unsigned first = (active & 1u) ? 0 : 1;
        const uint64_t total = outputs[group].phase +
                               (uint64_t)delta * outputs[group].current[first].frequency_millihz;
        const uint64_t cycles = total / 1000000u;
        outputs[group].phase = (uint32_t)(total % 1000000u);
        if (!cycles) continue;
        if (group == 1 && adc_enabled) adc_cycles += cycles;
        if (outputs[group].pending) {
            for (unsigned channel = 0; channel < PWM_CHANNELS; ++channel)
                outputs[group].current[channel] = outputs[group].next[channel];
            outputs[group].pending = false;
        }
        for (unsigned channel = 0; channel < PWM_CHANNELS; ++channel) {
            const uint8_t bit = (uint8_t)(1u << channel);
            if (!(active & bit) || !outputs[group].pulses[channel]) continue;
            const uint64_t counted = cycles - ((outputs[group].waiting_for_period & bit) != 0);
            if (counted >= outputs[group].pulses[channel]) pwm_driver_stop(group, channel);
            else outputs[group].pulses[channel] -= (uint32_t)counted;
        }
        outputs[group].waiting_for_period = 0;
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
    const unsigned source = outputs[0].active ? 0 : 1;
    const uint8_t active = outputs[source].active;
    if (!input_clock || !active) return false;
    const pwm_period *signal = &outputs[source].current[(active & 1u) ? 0 : 1];
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
