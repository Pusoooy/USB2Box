#ifndef USB2BOX_APP_H
#define USB2BOX_APP_H

#include "device.h"

void app_init(void);
void app_stop(void);
void app_poll(uint32_t now);
uint16_t app_command(uint8_t service, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *output, uint16_t *written, uint32_t now);

#endif
