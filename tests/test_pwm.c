#include "pwm_service.h"
#include "../host/mock_clock.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    resources pool;
    resources_init(&pool);
    pwm_period period;
    assert(pwm_calculate(84000000, 16, 1, 1000000, &period));
    assert(period.compare == period.reload + 1 && period.reload < UINT16_MAX);
    assert(pwm_calculate(84000000, 32, 1000, 250000, &period));
    assert(period.prescaler == 0 && period.reload == 83999 && period.compare == 21000);
    assert(pwm_calculate(84000000, 16, 1234, 0, &period) && period.compare == 0);
    assert(!pwm_calculate(84000000, 16, 1000, 1000001, &period));

    uint8_t out[DEVICE_FRAGMENT], req[10] = {0};
    uint16_t written;
    pwm_mock_tick(UINT32_MAX - 10);
    for (unsigned i = 0; i < 2; ++i) {
        req[0] = i;
        wire_put32(req + 1, 1000 + i * 999);
        wire_put32(req + 5, 250000);
        assert(pwm_command(&pool, 1, req, 10, out, &written, 0) == STATUS_OK);
        assert(written == 17 && wire_u32(out) > (999 + i * 999) * 1000);
        wire_put32(req + 1, 20 + i);
        wire_put32(req + 5, i == 0 ? 10 : 0);
        assert(pwm_command(&pool, 2, req, 9, out, &written, UINT32_MAX - 10) == STATUS_OK);
    }
    wire_put32(req, 1000000);
    assert(pwm_command(&pool, 6, req, 4, out, &written, 0) == STATUS_OK);
    assert(pwm_command(&pool, 7, NULL, 0, out, &written, 0) == STATUS_OK);
    assert(written == 12 && wire_u32(out) == 1000 && wire_u32(out + 4) == 250);
    assert(pwm_command(&pool, 8, NULL, 0, out, &written, 0) == STATUS_OK);
    req[0] = 0;
    wire_put32(req + 1, 1500);
    wire_put32(req + 5, 450000);
    assert(pwm_command(&pool, 3, req, 10, out, &written, 1) == STATUS_OK);
    assert(wire_u32(out) == 1500000 && out[16] == 3);
    assert(pwm_command(&pool, 3, req, 10, out, &written, 1) == STATUS_BUSY);
    assert(pwm_command(&pool, 5, req, 1, out, &written, 1) == STATUS_OK);
    assert(wire_u32(out) == 1000000 && out[16] == 3);
    pwm_mock_tick(UINT32_MAX - 9);
    assert(pwm_command(&pool, 5, req, 1, out, &written, 2) == STATUS_OK);
    assert(wire_u32(out) == 1500000 && wire_u32(out + 4) == 450000 && out[16] == 1);
    req[9] = 1;
    assert(pwm_command(&pool, 3, req, 10, out, &written, 2) == STATUS_VALUE);
    req[9] = 0;
    wire_put32(req + 5, 0);
    assert(pwm_command(&pool, 3, req, 10, out, &written, 2) == STATUS_VALUE);
    pwm_mock_tick(UINT32_MAX - 3);
    assert(!pwm_driver_running(0)); /* One old cycle and nine updated cycles. */
    assert(pwm_command(&pool, 2, (uint8_t[]){0, 20, 0, 0, 0, 0, 0, 0, 0},
                      9, out, &written, UINT32_MAX - 10) == STATUS_OK);
    pwm_poll(9);
    assert(!pwm_driver_running(0) && pwm_driver_running(1));
    pwm_stop_all(&pool);
    assert(!pwm_driver_running(1) && pool.owner[RESOURCE_TIMER(3)] == 0);
    puts("PWM independent frequency plans and deadline safe stop passed");
}
