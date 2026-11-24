#include "spi_service.h"
#include <assert.h>
#include <stdio.h>

void spi_mock_fault(uint16_t count, uint16_t status);

int main(void)
{
    resources pool;
    resources_init(&pool);
    uint8_t out[DEVICE_FRAGMENT], request[9 + JOB_DATA] = {0};
    uint16_t written;
    uint32_t now = 1;
    for (unsigned mode = 0; mode < 4; ++mode) {
        uint8_t config[14] = {0, mode, mode % 2 ? 16 : 8, mode % 2, mode, 0};
        wire_put32(config + 6, 1000000);
        wire_put16(config + 10, 1000);
        wire_put16(config + 12, 1000);
        assert(spi_command(&pool, 1, config, sizeof config, out, &written, now) == STATUS_OK);
        assert(spi_command(&pool, 2, request, 1, out, &written, now) == STATUS_OK);
        for (unsigned segment = 0; segment < 4; ++segment) {
            wire_put32(request + 1, segment + 1);
            wire_put32(request + 5, 20);
            for (unsigned i = 0; i < JOB_DATA; ++i) request[9 + i] = (uint8_t)(segment + i);
            assert(spi_command(&pool, 3, request, sizeof request, out, &written, now) == STATUS_OK);
            assert(spi_command(&pool, 4, request, 1, out, &written, now) == STATUS_BUSY);
            spi_poll(++now);
            spi_poll(++now);
            assert(spi_command(&pool, 7, request, 5, out, &written, now) == STATUS_OK);
            assert(written == JOB_DATA + 4 && wire_u16(out) == STATUS_OK);
            for (unsigned i = 0; i < JOB_DATA; ++i) assert(out[4 + i] == request[9 + i]);
        }
        assert(spi_command(&pool, 4, request, 1, out, &written, now) == STATUS_BUSY);
        assert(spi_command(&pool, 4, request, 1, out, &written, ++now) == STATUS_OK);
        assert(spi_command(&pool, 2, request, 1, out, &written, now) == STATUS_OK);
        assert(spi_command(&pool, 3, request, sizeof request, out, &written, now) == STATUS_OK);
        assert(spi_command(&pool, 8, request, 5, out, &written, now) == STATUS_OK);
        assert(spi_command(&pool, 7, request, 5, out, &written, now) == STATUS_OK);
        assert(wire_u16(out) == STATUS_CANCELLED);
        const uint16_t faults[] = {STATUS_IO, JOB_PENDING, JOB_PENDING};
        const uint16_t expected[] = {STATUS_IO, STATUS_TIMEOUT, STATUS_CANCELLED};
        for (unsigned test = 0; test < 3; ++test) {
            assert(spi_command(&pool, 2, request, 1, out, &written, now) == STATUS_OK);
            wire_put32(request + 5, 20);
            spi_mock_fault(12, faults[test]);
            assert(spi_command(&pool, 3, request, sizeof request, out, &written, now) == STATUS_OK);
            spi_poll(++now);
            if (test == 2) {
                assert(spi_command(&pool, 8, request, 5, out, &written, now) == STATUS_OK);
            } else {
                now += test == 1 ? 20 : 1;
                spi_poll(now);
            }
            assert(spi_command(&pool, 7, request, 5, out, &written, now) == STATUS_OK);
            assert(written == 16 && wire_u16(out) == expected[test] && wire_u16(out + 2) == 12);
            for (unsigned i = 0; i < 12; ++i) assert(out[4 + i] == request[9 + i]);
        }
        assert(spi_command(&pool, 5, request, 1, out, &written, now) == STATUS_OK);
    }
    puts("SPI modes, held CS and partial DMA results on fault, timeout and cancellation passed");
}
