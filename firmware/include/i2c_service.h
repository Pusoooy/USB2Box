#ifndef USB2BOX_I2C_SERVICE_H
#define USB2BOX_I2C_SERVICE_H

#include "jobs.h"
#include "resources.h"

enum { I2C_SLAVE_BYTES = 64 };
typedef struct {
    uint8_t bytes[I2C_SLAVE_BYTES];
    uint8_t count;
    uint32_t received, errors;
} i2c_slave_event;

uint16_t i2c_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written, uint32_t now);
uint16_t i2c_job_command(uint8_t opcode, uint32_t id, uint8_t *out, uint16_t *written);
void i2c_poll(uint32_t now);
void i2c_stop_all(resources *pool);

bool i2c_driver_init(unsigned bus, uint32_t speed, bool slave, uint8_t address);
void i2c_driver_deinit(unsigned bus);
bool i2c_driver_start(unsigned bus, uint8_t address, bool read, uint8_t *buffer,
                      uint8_t length, bool first, bool last);
uint16_t i2c_driver_result(unsigned bus);
void i2c_driver_slave_tx(unsigned bus, const uint8_t *data, uint8_t length);
void i2c_driver_slave_event(unsigned bus, i2c_slave_event *out);

#endif
