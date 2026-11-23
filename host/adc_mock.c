#include "adc_service.h"

static uint32_t value;
static uint8_t resolution, rank_count, channel[ADC_RANKS];
static uint16_t remaining;
static bool active, finite;

uint32_t adc_driver_start(const adc_config *config, uint32_t rate, uint16_t samples)
{
    value = 0;
    resolution = config->resolution;
    rank_count = config->ranks;
    for (unsigned i = 0; i < rank_count; ++i) channel[i] = config->channels[i];
    remaining = samples;
    finite = samples != 0;
    active = true;
    return rate ? rate : 1;
}
void adc_driver_stop(void) { active = false; }
uint16_t adc_driver_read(uint16_t *samples, uint32_t *overflow)
{
    (void)overflow;
    if (!active) return 0;
    const uint16_t count = finite && remaining < ADC_BLOCK ? remaining : ADC_BLOCK;
    for (unsigned i = 0; i < count; ++i) {
        samples[i] = (uint16_t)((value * 17u + channel[value % rank_count] * 31u) %
                                (1u << resolution));
        ++value;
    }
    if (finite) remaining -= count;
    return count;
}
