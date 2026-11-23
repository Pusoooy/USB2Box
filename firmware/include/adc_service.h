#ifndef USB2BOX_ADC_SERVICE_H
#define USB2BOX_ADC_SERVICE_H

#include "resources.h"

enum { ADC_RANKS = 16, ADC_BLOCK = 128, ADC_CACHE = 4096 };
typedef struct {
    uint8_t resolution, ranks, channels[ADC_RANKS], times[ADC_RANKS];
    bool watchdog;
    uint8_t watchdog_channel;
    uint16_t low, high;
} adc_config;

uint16_t adc_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written);
void adc_poll(void);
void adc_stop_all(resources *pool);
uint16_t adc_stream_payload(uint8_t *out);

uint32_t adc_driver_start(const adc_config *config, uint32_t rate, uint16_t samples);
void adc_driver_stop(void);
bool adc_driver_triggered(void);
uint16_t adc_driver_read(uint16_t *samples, uint32_t *overflow);

#endif
