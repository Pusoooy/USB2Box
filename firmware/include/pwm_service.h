#ifndef USB2BOX_PWM_SERVICE_H
#define USB2BOX_PWM_SERVICE_H

#include "resources.h"

typedef struct {
    uint32_t prescaler, reload, compare, frequency_millihz, duty_ppm;
    bool invert;
} pwm_period;

bool pwm_calculate(uint32_t clock, unsigned width, uint32_t frequency,
                   uint32_t duty, pwm_period *period);
uint16_t pwm_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written, uint32_t now);
void pwm_poll(uint32_t now);
void pwm_stop_all(resources *pool);

uint32_t pwm_driver_clock(void);
void pwm_driver_start(unsigned group, const pwm_period *period, uint32_t pulses);
void pwm_driver_stop(unsigned group);
bool pwm_driver_running(unsigned group);

#endif
