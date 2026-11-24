#include "adc_service.h"
#include "stream.h"
#include "uart_service.h"
#include <assert.h>
#include <stdio.h>

static void setup(resources *pool)
{
    uint8_t out[DEVICE_FRAGMENT];
    uint16_t result;
    const uint8_t adc[] = {12, 1, 10, 2};
    const uint8_t capture[] = {0xa0, 0x86, 1, 0, 10, 0, 0, 0, 0, 0};
    const uint8_t credit[] = {3, 0, 0, 0, 1, 0};
    assert(adc_command(pool, 1, adc, sizeof adc, out, &result) == STATUS_OK);
    assert(adc_command(pool, 3, capture, sizeof capture, out, &result) == STATUS_OK);
    assert(adc_command(pool, ADC_TAGGED_CREDIT, credit, sizeof credit, out, &result) == STATUS_OK);
    adc_poll();
    for (uint8_t port = 0; port < 2; ++port) {
        const uint8_t cfg[] = {port, 0, 0xc2, 1, 0, 0, 1};
        const uint8_t start[] = {port, (uint8_t)(4 + port), 0, 0, 0};
        const uint8_t grant[] = {port, 1, 0};
        assert(uart_command(pool, 1, cfg, sizeof cfg, out, &result) == STATUS_OK);
        assert(uart_command(pool, 2, &port, 1, out, &result) == STATUS_OK);
        assert(uart_command(pool, UART_STREAM_CONFIG, start, sizeof start, out, &result) == STATUS_OK);
        assert(uart_command(pool, UART_STREAM_CREDIT, grant, sizeof grant, out, &result) == STATUS_OK);
    }
    for (uint8_t port = 0; port < 2; ++port) {
        const uint8_t data[] = {port, (uint8_t)(13 + port)};
        assert(uart_command(pool, 4, data, sizeof data, out, &result) == STATUS_OK);
    }
    uart_poll();
}

int main(void)
{
    resources pool;
    resources_init(&pool);
    setup(&pool);
    stream_mux mux = {0};
    mux.sequence[0] = UINT16_MAX;
    for (unsigned stream = 1; stream <= STREAM_COUNT; ++stream) {
        uint16_t length;
        frame_header header;
        const uint8_t *data = stream_peek(&mux, true, 11, &length);
        assert(length && frame_decode(&header, data, length, DEVICE_FRAGMENT, DEVICE_FRAGMENT));
        assert(header.stream_id == stream && header.type == 4);
        const uint8_t *payload = data + FRAME_HEADER_SIZE;
        if (stream == STREAM_ADC) {
            assert(header.sequence == UINT16_MAX && mux.sequence[0] == 0);
            assert(header.service == SERVICE_ADC && header.opcode == ADC_TAGGED_CREDIT);
            assert(wire_u32(payload) == 3 && wire_u16(payload + 4 + 12) == 10);
        } else {
            assert(header.service == SERVICE_UART && header.opcode == UART_STREAM_CREDIT);
            assert(wire_u32(payload) == stream + 2 && wire_u32(payload + 4) == 0);
            assert(wire_u32(payload + 8) == 11 && payload[UART_STREAM_META] == 16 - stream);
        }
        stream_consume(&mux, 7);
        /* Ending a lease cannot leave a prefix of a frame in the endpoint. */
        uint16_t remaining;
        assert(stream_peek(&mux, false, 12, &remaining) == data + 7);
        assert(remaining == length - 7);
        stream_consume(&mux, remaining);
    }
    uint16_t length;
    stream_peek(&mux, true, 13, &length);
    assert(!length);
    uart_stop_all(&pool);
    adc_stop_all(&pool);
    puts("round-robin ADC/UART multiplexing, wrap and partial frame draining passed");
}
