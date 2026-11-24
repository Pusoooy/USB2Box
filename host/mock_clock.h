#ifndef USB2BOX_MOCK_CLOCK_H
#define USB2BOX_MOCK_CLOCK_H

#include <stdint.h>

void pwm_mock_tick(uint32_t now);
uint64_t pwm_mock_adc_cycles(void);
void adc_mock_tick(uint32_t now);

#endif
