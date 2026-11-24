#ifndef USB2BOX_ADC_SERVICE_H
#define USB2BOX_ADC_SERVICE_H

#include "resources.h"

enum {
    ADC_RANKS = 16, ADC_BLOCK = 128, ADC_CACHE = 4096,
    ADC_STREAM_CREDIT = 7, ADC_TAGGED_CREDIT = 9, ADC_TIMED_CREDIT = 10,
    ADC_TIMED_READ = 11, ADC_REFERENCE_CAL = 12,
};
typedef struct {
    uint8_t resolution, ranks, channels[ADC_RANKS], times[ADC_RANKS];
    bool watchdog;
    uint8_t watchdog_channel;
    uint16_t low, high;
} adc_config;

typedef struct {
    uint32_t timer_ticks, aggregate_rate, scan_ns, rank_ns[ADC_RANKS];
    uint8_t resolution;
} adc_plan;

bool adc_plan_capture(const adc_config *config, uint32_t rate, uint32_t timer_hz,
                       uint32_t adc_hz, adc_plan *plan);
uint16_t adc_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written, uint32_t now);
void adc_poll(void);
void adc_stop_all(resources *pool);
uint16_t adc_stream_payload(uint8_t *out, uint8_t *opcode);

void adc_driver_clocks(uint32_t *timer_hz, uint32_t *adc_hz);
void adc_driver_reference(uint16_t *code, uint16_t *millivolts);
bool adc_driver_start(const adc_config *config, const adc_plan *plan, uint16_t samples);
void adc_driver_stop(void);
bool adc_driver_triggered(void);
uint16_t adc_driver_read(uint16_t *samples, uint32_t *overflow);

#endif
