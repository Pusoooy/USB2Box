#include "i2c_service.h"

typedef struct {
    bool slave;
    uint8_t address, tx[I2C_SLAVE_BYTES];
    uint16_t result;
} mock_bus;

static mock_bus mock[2];
static uint32_t bus_time;

bool i2c_driver_init(unsigned bus, uint32_t speed, bool slave, uint8_t address)
{
    (void)speed;
    mock[bus] = (mock_bus){.slave = slave, .address = address};
    return true;
}
void i2c_driver_deinit(unsigned bus) { mock[bus].slave = false; }
bool i2c_driver_start(unsigned bus, uint8_t address, bool read, uint8_t *buffer,
                      uint8_t length, bool first, bool last)
{
    (void)first;
    (void)last;
    mock_bus *peer = &mock[1 - bus];
    if (address == 119) {
        mock[bus].result = JOB_PENDING;
    } else if (!peer->slave || peer->address != address || length > I2C_SLAVE_BYTES) {
        mock[bus].result = STATUS_IO;
    } else {
        if (read) {
            for (unsigned i = 0; i < length; ++i) buffer[i] = peer->tx[i];
            i2c_record_slave_event(1 - bus, I2C_EVENT_TX, 0, length, ++bus_time);
        } else {
            i2c_record_slave_event(1 - bus, I2C_EVENT_RX, buffer, length, ++bus_time);
        }
        mock[bus].result = STATUS_OK;
    }
    return true;
}
uint16_t i2c_driver_result(unsigned bus) { return mock[bus].result; }
void i2c_driver_slave_tx(unsigned bus, const uint8_t *data, uint8_t length)
{
    for (unsigned i = 0; i < I2C_SLAVE_BYTES; ++i) mock[bus].tx[i] = i < length ? data[i] : 0xff;
}
void i2c_driver_service(unsigned bus) { (void)bus; }
