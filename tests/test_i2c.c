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
