#include "adc_service.h"
#include "mock_clock.h"

static uint64_t value;
static uint8_t resolution, rank_count, channel[ADC_RANKS];
static uint16_t remaining;
static bool active, finite, threshold, watchdog;
static uint8_t watchdog_channel;
static uint16_t lo, hi;
static uint32_t mock_now, started_at, rate;
static bool timed, timer_triggered;

void adc_mock_tick(uint32_t now)
{
    mock_now = now;
    timed = true;
}

void adc_driver_clocks(uint32_t *timer_hz, uint32_t *adc_hz)
{
    *timer_hz = 84000000;
    *adc_hz = 21000000;
}

void adc_driver_reference(uint16_t *code, uint16_t *millivolts)
{
    *code = 1500;
    *millivolts = 3300;
}

bool adc_driver_start(const adc_config *config, const adc_plan *plan, uint16_t samples)
{
    value = 0;
    watchdog = config->watchdog;
    watchdog_channel = config->watchdog_channel;
    lo = config->low;
    hi = config->high;
    threshold = false;
    resolution = config->resolution;
    rank_count = config->ranks;
    for (unsigned i = 0; i < rank_count; ++i) channel[i] = config->channels[i];
    remaining = samples;
    finite = samples != 0;
    timer_triggered = plan->timer_ticks != 0;
    rate = plan->aggregate_rate;
    started_at = mock_now;
    active = true;
    return true;
}
void adc_driver_stop(void) { active = false; }
uint16_t adc_driver_read(uint16_t *samples, uint32_t *overflow)
{
    (void)overflow;
    if (!active) return 0;
    uint16_t count = finite && remaining < ADC_BLOCK ? remaining : ADC_BLOCK;
    if (timed && timer_triggered) {
        const uint64_t available = (uint64_t)(mock_now - started_at) * rate / 1000u - value;
        if (available < count) count = (uint16_t)available;
    }
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t input = channel[value % rank_count];
        samples[i] = input == 17 ? (uint16_t)(1500u >> (12 - resolution)) :
                     (uint16_t)((value * 17u + input * 31u) % (1u << resolution));
        if (watchdog && channel[value % rank_count] == watchdog_channel &&
            (samples[i] < lo || samples[i] > hi)) threshold = true;
        ++value;
    }
    if (finite) remaining -= count;
    return count;
}

bool adc_driver_triggered(void) { return threshold; }
