#include "adc_service.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    resources pool;
    resources_init(&pool);
    uint8_t out[DEVICE_FRAGMENT], config[] = {12, 2, 10, 2, 11, 2};
    uint16_t written;
    assert(adc_command(&pool, 1, config, sizeof config, out, &written) == STATUS_OK);
    uint8_t start[10] = {0};
    wire_put32(start, 100000);
    wire_put16(start + 4, 257);
    assert(adc_command(&pool, 3, start, sizeof start, out, &written) == STATUS_OK);
    for (unsigned i = 0; i < 4; ++i) adc_poll();
    uint8_t credit[2] = {2, 0};
    assert(adc_command(&pool, 7, credit, sizeof credit, out, &written) == STATUS_OK);
    assert(adc_stream_payload(out) == 512 && wire_u16(out + 12) == 248);
    assert(wire_u16(out + 16) == 310 && wire_u16(out + 18) == 358);
    assert(adc_stream_payload(out) == 34 && wire_u32(out + 4) == 248 && out[15] == 0);
    assert(adc_stream_payload(out) == 0);

    wire_put16(start + 4, 0);
    wire_put16(start + 6, 10);
    wire_put16(start + 8, 20);
    assert(adc_command(&pool, 3, start, sizeof start, out, &written) == STATUS_OK);
    adc_poll();
    adc_poll();
    assert(adc_command(&pool, 5, NULL, 0, out, &written) == STATUS_OK);
    adc_poll();
    uint8_t limit[] = {128, 0};
    assert(adc_command(&pool, 6, limit, sizeof limit, out, &written) == STATUS_OK);
    assert(wire_u16(out + 12) == 30 && wire_u32(out + 4) == 246);

    wire_put16(start + 6, 0);
    wire_put16(start + 8, 0);
    assert(adc_command(&pool, 3, start, sizeof start, out, &written) == STATUS_OK);
    for (unsigned i = 0; i < ADC_CACHE / ADC_BLOCK + 1; ++i) adc_poll();
    assert(adc_command(&pool, 6, limit, sizeof limit, out, &written) == STATUS_OK);
    assert(wire_u32(out + 8) == ADC_BLOCK && out[15] == 0);
    adc_stop_all(&pool);
    assert(!pool.owner[RESOURCE_STREAM(2, 4)]);
    puts("ADC scan, finite count, pre/post trigger, credit and overrun stop passed");
}
