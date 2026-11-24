#ifndef USB2BOX_PWM_SERVICE_H
#define USB2BOX_PWM_SERVICE_H

#include "resources.h"

enum { PWM_GROUPS = 2, PWM_CHANNELS = 2 };

typedef struct {
    uint32_t prescaler, reload, compare, frequency_millihz, duty_ppm;
    uint32_t requested_duty;
    bool invert;
} pwm_period;

bool pwm_calculate(uint32_t clock, unsigned width, uint32_t frequency,
                   uint32_t duty, pwm_period *period);
uint16_t pwm_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written, uint32_t now);
void pwm_poll(uint32_t now);
void pwm_stop_all(resources *pool);

uint32_t pwm_driver_clock(void);
void pwm_driver_start(unsigned group, unsigned channel, const pwm_period *period, uint32_t pulses);
void pwm_driver_stop(unsigned group, unsigned channel);
bool pwm_driver_running(unsigned group, unsigned channel);
bool pwm_driver_update(unsigned group, const pwm_period periods[PWM_CHANNELS]);
bool pwm_driver_update_pending(unsigned group);

bool pwm_capture_start(uint32_t counter_hz);
bool pwm_capture_read(uint32_t *period, uint32_t *high, uint32_t *counter_hz);
void pwm_capture_stop(void);

#endif
