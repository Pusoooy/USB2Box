#include "pwm_service.h"

typedef struct {
    pwm_period period;
    uint32_t deadline;
    bool configured;
} pwm_state;

static pwm_state groups[2];

bool pwm_calculate(uint32_t clock, unsigned width, uint32_t frequency,
                   uint32_t duty, pwm_period *period)
{
    if (!clock || !frequency || frequency > clock || frequency > 1000000 ||
        duty > 1000000 || (width != 16 && width != 32)) return false;
    /* Reserve one CCR value above ARR for exact 100% duty. */
    const uint64_t max_period = width == 16 ? UINT16_MAX : UINT32_MAX;
    const uint64_t ticks = ((uint64_t)clock + frequency / 2u) / frequency;
    const uint64_t divider = (ticks + max_period - 1u) / max_period;
    if (divider > 65536) return false;
    const uint64_t cycle = (ticks + divider / 2u) / divider;
    const uint64_t compare = (cycle * duty + 500000u) / 1000000u;
    period->prescaler = (uint32_t)(divider - 1);
    period->reload = (uint32_t)(cycle - 1);
    period->compare = (uint32_t)compare;
    period->frequency_millihz = (uint32_t)(((uint64_t)clock * 1000u + cycle * divider / 2u) /
                                           (cycle * divider));
    period->duty_ppm = (uint32_t)((compare * 1000000u + cycle / 2u) / cycle);
    return true;
}

static void stop_group(resources *pool, unsigned group)
{
    if (!groups[group].configured) return;
    pwm_driver_stop(group);
    groups[group].configured = false;
    resource_release(pool, (uint8_t)(5 + group));
}

void pwm_stop_all(resources *pool)
{
    for (unsigned group = 0; group < 2; ++group) stop_group(pool, group);
}

void pwm_poll(uint32_t now)
{
    for (unsigned group = 0; group < 2; ++group)
        if (groups[group].configured && pwm_driver_running(group) &&
            now - groups[group].deadline < 0x80000000u) pwm_driver_stop(group);
}

uint16_t pwm_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written, uint32_t now)
{
    if (!length || input[0] > 1) return STATUS_VALUE;
    const unsigned group = input[0];
    pwm_state *state = &groups[group];
    switch (opcode) {
    case 1:
    case 3: {
        if (length != 10 || input[9] > 1) return STATUS_VALUE;
        if (pwm_driver_running(group)) return STATUS_BUSY;
        pwm_period period;
        if (!pwm_calculate(pwm_driver_clock(), group == 0 ? 32 : 16,
                           wire_u32(input + 1), wire_u32(input + 5), &period)) return STATUS_VALUE;
        resource_plan plan = {0};
        resource_add(&plan, group == 0 ? RESOURCE_PIN('A', 0) : RESOURCE_PIN('C', 6));
        resource_add(&plan, RESOURCE_TIMER(group == 0 ? 5 : 3));
        uint8_t conflict;
        const uint16_t status = resource_reserve(pool, &plan, (uint8_t)(5 + group), &conflict);
        if (status != STATUS_OK) return status;
        period.invert = input[9] != 0;
        state->period = period;
        state->configured = true;
        break;
    }
    case 2: {
        if (length != 9 || !state->configured) return STATUS_VALUE;
        if (pwm_driver_running(group)) return STATUS_BUSY;
        const uint32_t duration = wire_u32(input + 1), pulses = wire_u32(input + 5);
        if (!duration || duration > 60000) return STATUS_VALUE;
        if (pulses) {
            const uint64_t divider = state->period.prescaler + 1;
            const uint64_t min_ticks = pwm_driver_clock() / 100000u;
            if (state->period.compare * divider < min_ticks ||
                (state->period.reload + 1u - state->period.compare) * divider < min_ticks)
                return STATUS_VALUE;
        }
        state->deadline = now + duration;
        pwm_driver_start(group, &state->period, pulses);
        break;
    }
    case 4:
        if (length != 1) return STATUS_VALUE;
        stop_group(pool, group);
        return STATUS_OK;
    case 5:
        if (length != 1 || !state->configured) return STATUS_VALUE;
        break;
    default:
        return STATUS_OPCODE;
    }
    wire_put32(out, state->period.frequency_millihz);
    wire_put32(out + 4, state->period.duty_ppm);
    wire_put32(out + 8, state->period.prescaler);
    wire_put32(out + 12, state->period.reload);
    out[16] = pwm_driver_running(group);
    *written = 17;
    return STATUS_OK;
}
