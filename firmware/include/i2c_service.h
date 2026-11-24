#ifndef USB2BOX_I2C_SERVICE_H
#define USB2BOX_I2C_SERVICE_H

#include "jobs.h"
#include "resources.h"

enum {
    I2C_SLAVE_BYTES = 64, I2C_EVENT_CAPACITY = 8,
    I2C_EVENT_RX = 1, I2C_EVENT_TX, I2C_EVENT_ERROR,
    I2C_LINE_SCL = 1, I2C_LINE_SDA = 2,
};

uint16_t i2c_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written, uint32_t now);
uint16_t i2c_job_command(uint8_t opcode, uint32_t id, uint8_t *out, uint16_t *written);
void i2c_poll(uint32_t now);
void i2c_stop_all(resources *pool);
/* Single producer per bus: the slave's event and error IRQs share a priority. */
void i2c_record_slave_event(unsigned bus, uint8_t kind, const uint8_t *data,
                             uint8_t count, uint32_t now);

bool i2c_driver_init(unsigned bus, uint32_t speed, bool slave, uint8_t address);
void i2c_driver_deinit(unsigned bus);
bool i2c_driver_start(unsigned bus, uint8_t address, bool read, uint8_t *buffer,
                      uint8_t length, bool first, bool last);
uint16_t i2c_driver_result(unsigned bus);
void i2c_driver_slave_tx(unsigned bus, const uint8_t *data, uint8_t length);
void i2c_driver_service(unsigned bus);
void i2c_driver_recovery_begin(unsigned bus);
void i2c_driver_recovery_drive(unsigned bus, bool release_scl, bool release_sda);
uint8_t i2c_driver_lines(unsigned bus);

#endif
