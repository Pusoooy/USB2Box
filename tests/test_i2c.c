#include "i2c_service.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    resources pool;
    resources_init(&pool);
    uint8_t out[DEVICE_FRAGMENT], config[5] = {0};
    uint16_t written;
    wire_put32(config + 1, 10000);
    for (unsigned master = 0; master < 2; ++master) {
        const uint8_t slave = (uint8_t)(1 - master);
        for (unsigned bus = 0; bus < 2; ++bus) {
            config[0] = bus;
            assert(i2c_command(&pool, 1, config, 5, out, &written, 0) == STATUS_OK);
        }
        uint8_t slave_config[] = {slave, 1, 42};
        assert(i2c_command(&pool, 2, slave_config, 3, out, &written, 0) == STATUS_OK);
        uint8_t bytes[] = {slave, 0xb0, 0xb1};
        assert(i2c_command(&pool, 4, bytes, 3, out, &written, 0) == STATUS_OK);
        uint8_t transfer[] = {master, 1, 0, 0, 0, 20, 0, 0, 0, 42, 2, 0, 1, 0xa0, 1, 2};
        assert(i2c_command(&pool, 3, transfer, sizeof transfer, out, &written, 0) == STATUS_OK);
        for (unsigned now = 0; now < 4; ++now) i2c_poll(now);
        assert(i2c_job_command(SYS_COLLECT, 1, out, &written) == STATUS_OK);
        assert(written == 6 && wire_u16(out) == STATUS_OK && out[4] == 0xb0 && out[5] == 0xb1);
        assert(i2c_command(&pool, 5, bytes, 1, out, &written, 4) == STATUS_OK);
        assert(written == 9 && wire_u32(out) == 1 && out[8] == 0xa0);
        assert(i2c_command(&pool, 7, bytes, 1, out, &written, 4) == STATUS_OK);
        assert(written == 22 && wire_u32(out) == 1 && out[20] == I2C_EVENT_TX && out[21] == 2);
        for (unsigned event = 0; event < I2C_EVENT_CAPACITY + 2; ++event) {
            const uint8_t payload[] = {(uint8_t)event};
            i2c_record_slave_event(slave, I2C_EVENT_RX, payload, 1, event + 10);
        }
        for (unsigned event = 0; event < I2C_EVENT_CAPACITY; ++event) {
            assert(i2c_command(&pool, 7, bytes, 1, out, &written, 4) == STATUS_OK);
            assert(written == 23 && wire_u32(out) == event + 2 && wire_u32(out + 4) == event + 10);
            assert(wire_u32(out + 8) == 2 && wire_u32(out + 12) == I2C_EVENT_CAPACITY + 3);
            assert(out[20] == I2C_EVENT_RX && out[21] == 1 && out[22] == event);
        }
        assert(i2c_command(&pool, 7, bytes, 1, out, &written, 4) == STATUS_OK);
        assert(written == 22 && out[20] == 0 && wire_u32(out + 8) == 2);
        i2c_record_slave_event(slave, I2C_EVENT_ERROR, NULL, 0, 42);
        assert(i2c_command(&pool, 7, bytes, 1, out, &written, 4) == STATUS_OK);
        assert(wire_u32(out) == I2C_EVENT_CAPACITY + 4 && wire_u32(out + 16) == 1);
        assert(out[20] == I2C_EVENT_ERROR);
        transfer[9] = 119;
        assert(i2c_command(&pool, 3, transfer, sizeof transfer, out, &written, 5) == STATUS_OK);
        i2c_poll(6);
        i2c_poll(25);
        assert(i2c_job_command(SYS_COLLECT, 1, out, &written) == STATUS_OK);
        assert(wire_u16(out) == STATUS_TIMEOUT);
        i2c_stop_all(&pool);
    }
    puts("I2C dual master/slave combined transactions, events and timeout passed");
}
