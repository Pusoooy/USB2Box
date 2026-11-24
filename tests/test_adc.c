#include "adc_service.h"
#include "pwm_service.h"
#include "../host/mock_clock.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    resources pool;
    resources_init(&pool);
    uint8_t out[DEVICE_FRAGMENT], config[] = {12, 2, 10, 2, 11, 2};
    uint16_t written;
    assert(adc_command(&pool, 1, config, sizeof config, out, &written, 1234) == STATUS_OK);
    uint8_t start[10] = {0};
    wire_put32(start, 100000);
    wire_put16(start + 4, 257);
    assert(adc_command(&pool, 3, start, sizeof start, out, &written, 1234) == STATUS_OK);
    for (unsigned i = 0; i < 4; ++i) adc_poll();
    uint8_t credit[2] = {2, 0};
    assert(adc_command(&pool, 7, credit, sizeof credit, out, &written, 1234) == STATUS_OK);
    uint8_t opcode;
    assert(adc_stream_payload(out, &opcode) == 512 && wire_u16(out + 12) == 248);
    assert(opcode == ADC_STREAM_CREDIT);
    assert(wire_u16(out + 16) == 310 && wire_u16(out + 18) == 358);
    assert(adc_stream_payload(out, &opcode) == 34 && wire_u32(out + 4) == 248 && out[15] == 0);
    assert(adc_stream_payload(out, &opcode) == 0);

    wire_put16(start + 4, 0);
    wire_put16(start + 6, 10);
    wire_put16(start + 8, 20);
    assert(adc_command(&pool, 3, start, sizeof start, out, &written, 1234) == STATUS_OK);
    adc_poll();
    adc_poll();
    assert(adc_command(&pool, 5, NULL, 0, out, &written, 1234) == STATUS_OK);
    adc_poll();
    uint8_t limit[] = {128, 0};
    assert(adc_command(&pool, 6, limit, sizeof limit, out, &written, 1234) == STATUS_OK);
    assert(wire_u16(out + 12) == 30 && wire_u32(out + 4) == 246);

    wire_put16(start + 6, 0);
    wire_put16(start + 8, 0);
    assert(adc_command(&pool, 3, start, sizeof start, out, &written, 1234) == STATUS_OK);
    for (unsigned i = 0; i < ADC_CACHE / ADC_BLOCK + 1; ++i) adc_poll();
    assert(adc_command(&pool, 6, limit, sizeof limit, out, &written, 1234) == STATUS_OK);
    assert(wire_u32(out + 8) == ADC_BLOCK && !(out[15] & 1));
    uint8_t threshold[] = {10, 0, 0, 1, 0};
    assert(adc_command(&pool, 8, threshold, sizeof threshold, out, &written, 1234) == STATUS_OK);
    wire_put16(start + 6, 4);
    wire_put16(start + 8, 6);
    assert(adc_command(&pool, 3, start, sizeof start, out, &written, 1234) == STATUS_OK);
    adc_poll();
    adc_poll();
    assert(adc_command(&pool, 6, limit, sizeof limit, out, &written, 1234) == STATUS_OK);
    assert(wire_u16(out + 12) == 10 && (out[15] & 8));
    adc_stop_all(&pool);
    assert(!pool.owner[RESOURCE_STREAM(2, 4)]);

    const adc_config auto_cfg = {.ranks = 2, .times = {6, 7}};
    adc_plan plan = {0};
    assert(adc_plan_capture(&auto_cfg, 65000, 84000000, 21000000, &plan));
    assert(plan.resolution == 8 && plan.timer_ticks == 2585 && plan.scan_ns == 30774);
    assert(plan.rank_ns[1] > plan.rank_ns[0]);
    const adc_config forced = {.resolution = 12, .ranks = 2, .times = {6, 7}};
    assert(!adc_plan_capture(&forced, 65000, 84000000, 21000000, &plan));
    const uint8_t timing[] = {0, 2, 10, 6, 11, 7};
    assert(adc_command(&pool, 1, timing, sizeof timing, out, &written, 1234) == STATUS_OK);
    wire_put32(start, 65000);
    wire_put16(start + 4, 4);
    wire_put16(start + 6, 0);
    wire_put16(start + 8, 0);
    assert(adc_command(&pool, 3, start, sizeof start, out, &written, 1234) == STATUS_OK);
    adc_poll();
    const uint8_t timed_grant[] = {0xef, 0xbe, 0xad, 0xde, 1, 0};
    assert(adc_command(&pool, ADC_TIMED_CREDIT, timed_grant, sizeof timed_grant, out, &written, 1234) == STATUS_OK);
    assert(adc_stream_payload(out, &opcode) == 28 + 16 + 8 && opcode == ADC_TIMED_CREDIT);
    assert(wire_u32(out) == 0xdeadbeefu && wire_u32(out + 4) == 1234037);
    assert(wire_u32(out + 12) == 30774 && out[16] == 2 && out[17] == 0);
    assert(out[18] == 10 && out[23] == 11 && out[28 + 14] == 8);
    assert(adc_command(&pool, ADC_REFERENCE_CAL, NULL, 0, out, &written, 1234) == STATUS_OK);
    assert(wire_u16(out) == 1500 && wire_u16(out + 2) == 3300);
    adc_stop_all(&pool);

    const uint32_t now = 1240;
    assert(adc_command(&pool, 1, config, sizeof config, out, &written, now) == STATUS_OK);
    const uint8_t synced[] = {8, 0, 0, 0, 0, 0};
    assert(adc_command(&pool, ADC_PWM_START, synced, sizeof synced, out, &written, now) == STATUS_BUSY);
    const uint8_t pwm[] = {1, 0xe8, 3, 0, 0, 0x90, 0xd0, 3, 0, 0};
    const uint8_t pwm_second_config[] = {1, 2, 0xe8, 3, 0, 0, 0x90, 0xd0, 3, 0, 0};
    const uint8_t pwm_start[] = {1, 100, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t pwm_second[] = {1, 2, 100, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t group = 1;
    pwm_mock_tick(now);
    assert(pwm_command(&pool, 1, pwm, sizeof pwm, out, &written, now) == STATUS_OK);
    assert(pwm_command(&pool, 9, pwm_second_config, sizeof pwm_second_config, out, &written, now) == STATUS_OK);
    assert(pwm_command(&pool, 2, pwm_start, sizeof pwm_start, out, &written, now) == STATUS_OK);
    assert(adc_command(&pool, ADC_PWM_START, synced, sizeof synced, out, &written, now) == STATUS_OK);
    assert(pwm_command(&pool, 3, pwm, sizeof pwm, out, &written, now) == STATUS_BUSY);
    assert(pwm_command(&pool, 10, pwm_second, sizeof pwm_second, out, &written, now) == STATUS_BUSY);
    pwm_mock_tick(now + 2);
    adc_poll();
    assert(adc_command(&pool, 6, limit, sizeof limit, out, &written, now + 2) == STATUS_OK);
    assert(wire_u32(out) == 2000 && wire_u16(out + 12) == 4 && out[15] == 1);
    assert(pwm_command(&pool, 4, &group, 1, out, &written, now + 2) == STATUS_OK);
    adc_poll();
    assert(adc_command(&pool, 6, limit, sizeof limit, out, &written, now + 2) == STATUS_OK);
    assert(wire_u16(out + 12) == 0 && out[15] == ADC_FLAG_TRIGGER_STOPPED);
    assert(pwm_command(&pool, 1, pwm, sizeof pwm, out, &written, now + 2) == STATUS_OK);
    assert(pwm_command(&pool, 2, pwm_start, sizeof pwm_start, out, &written, now + 2) == STATUS_OK);
    assert(adc_command(&pool, ADC_PWM_START, synced, sizeof synced, out, &written, now + 2) == STATUS_OK);
    pwm_mock_tick(now + 6);
    adc_poll();
    assert(adc_command(&pool, 6, limit, sizeof limit, out, &written, now + 6) == STATUS_OK);
    assert(wire_u16(out + 12) == 8 && out[15] == 0);
    assert(pwm_command(&pool, 3, pwm, sizeof pwm, out, &written, now + 6) == STATUS_OK);
    adc_stop_all(&pool);
    pwm_stop_all(&pool);
    puts("ADC scan timing, limits, finite capture, PWM synchronization and trigger stop passed");
}
