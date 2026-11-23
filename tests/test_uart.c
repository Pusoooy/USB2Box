#include "uart_service.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    resources pool;
    resources_init(&pool);
    uint8_t out[DEVICE_FRAGMENT], request[514] = {0};
    uint16_t written = 0;
    for (unsigned port = 0; port < 2; ++port) {
        request[0] = port;
        wire_put32(request + 1, 115200);
        request[5] = 0;
        request[6] = 1;
        assert(uart_command(&pool, 1, request, 7, out, &written) == STATUS_OK);
        assert(uart_command(&pool, 2, request, 1, out, &written) == STATUS_OK);
    }
    uint32_t seed = 314159;
    for (unsigned iteration = 0; iteration < 40; ++iteration) {
        uint8_t expected[2][513];
        for (unsigned port = 0; port < 2; ++port) {
            request[0] = port;
            for (unsigned i = 0; i < 513; ++i) {
                seed = seed * 1664525u + 1013904223u;
                request[i + 1] = expected[port][i] = (uint8_t)(seed >> 24);
            }
            assert(uart_command(&pool, 4, request, sizeof request, out, &written) == STATUS_OK);
        }
        uart_poll();
        uart_poll();
        uart_poll();
        for (unsigned port = 0; port < 2; ++port) {
            request[0] = port;
            wire_put16(request + 1, 512);
            assert(uart_command(&pool, 3, request, 3, out, &written) == STATUS_OK);
            assert(written == 512);
            for (unsigned i = 0; i < written; ++i) assert(out[i] == expected[1 - port][i]);
            assert(uart_command(&pool, 3, request, 3, out, &written) == STATUS_OK);
            assert(written == 1 && out[0] == expected[1 - port][512]);
        }
    }
    request[0] = 0;
    for (unsigned i = 0; i < 10; ++i) {
        assert(uart_command(&pool, 4, request, sizeof request, out, &written) == STATUS_OK);
        uart_poll();
        uart_poll();
        uart_poll();
    }
    request[0] = 1;
    assert(uart_command(&pool, 6, request, 1, out, &written) == STATUS_OK);
    assert(wire_u32(out) == 5130 - UART_RX_SIZE);
    uart_stop_all(&pool);
    assert(pool.owner[RESOURCE_STREAM(2, 2)] == 0);
    puts("UART dual cross-loopback, wrap and overflow passed");
}
