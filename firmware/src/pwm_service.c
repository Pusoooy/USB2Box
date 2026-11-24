#include "pwm_service.h"

typedef struct {
    pwm_period period;
    uint32_t deadline;
    bool configured, pulsed;
} pwm_output;

typedef struct {
    pwm_output outputs[PWM_CHANNELS];
    pwm_period next[PWM_CHANNELS];
    bool updating;
} pwm_group;

static pwm_group groups[PWM_GROUPS];
static bool capture, adc_clock_claimed;

static const uint8_t output_pin[PWM_GROUPS][PWM_CHANNELS] = {
    {RESOURCE_PIN('A', 0), RESOURCE_PIN('A', 1)},
    {RESOURCE_PIN('C', 6), RESOURCE_PIN('C', 7)},
};

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
    period->requested_duty = duty;
    return true;
}

static bool group_running(unsigned group)
{
    return pwm_driver_running(group, 0) || pwm_driver_running(group, 1);
}

static void stop_output(resources *pool, unsigned group, unsigned channel)
{
    pwm_output *output = &groups[group].outputs[channel];
    if (!output->configured) return;
    pwm_driver_stop(group, channel);
    output->configured = output->pulsed = false;
    const uint8_t owner = (uint8_t)(5 + group);
    resource_release_item(pool, owner, output_pin[group][channel]);
    if (!groups[group].outputs[1 - channel].configured) {
        groups[group].updating = false;
        resource_release(pool, owner);
    }
}

void pwm_stop_all(resources *pool)
{
    for (unsigned group = 0; group < PWM_GROUPS; ++group)
        for (unsigned channel = 0; channel < PWM_CHANNELS; ++channel)
            stop_output(pool, group, channel);
    if (capture) pwm_capture_stop();
    capture = false;
    resource_release(pool, 9);
}

void pwm_poll(uint32_t now)
{
    for (unsigned group = 0; group < PWM_GROUPS; ++group) {
        for (unsigned channel = 0; channel < PWM_CHANNELS; ++channel) {
            const pwm_output *output = &groups[group].outputs[channel];
            if (output->configured && pwm_driver_running(group, channel) &&
                now - output->deadline < 0x80000000u)
                pwm_driver_stop(group, channel);
        }
    }
}

static void refresh_period(unsigned group)
{
    pwm_group *state = &groups[group];
    if (!state->updating || (group_running(group) && pwm_driver_update_pending(group))) return;
    for (unsigned channel = 0; channel < PWM_CHANNELS; ++channel)
        if (state->outputs[channel].configured)
            state->outputs[channel].period = state->next[channel];
    state->updating = false;
}

bool pwm_claim_adc_clock(uint32_t *ticks)
{
    const unsigned group = 1;
    refresh_period(group);
    if (adc_clock_claimed || groups[group].updating || !group_running(group)) return false;
    const unsigned channel = pwm_driver_running(group, 0) ? 0 : 1;
    if (!pwm_driver_running(group, channel)) return false;
    const pwm_period *period = &groups[group].outputs[channel].period;
    *ticks = (period->prescaler + 1u) * (period->reload + 1u);
    adc_clock_claimed = true;
    return true;
}

bool pwm_adc_clock_running(void)
{
    return group_running(1);
}

void pwm_release_adc_clock(void)
{
    pwm_driver_adc_trigger(false);
    adc_clock_claimed = false;
}

static bool suitable_for_count(const pwm_period *period, uint32_t clock)
{
    const uint64_t divider = period->prescaler + 1u;
    const uint64_t min_ticks = (clock + 99999u) / 100000u;
    return period->compare * divider >= min_ticks &&
           (period->reload + 1u - period->compare) * divider >= min_ticks;
}

static uint16_t reserve_output(resources *pool, unsigned group, unsigned channel)
{
    resource_plan plan = {0};
    resource_add(&plan, output_pin[group][channel]);
    resource_add(&plan, RESOURCE_TIMER(group == 0 ? 5 : 3));
    uint8_t conflict;
    return resource_reserve(pool, &plan, (uint8_t)(5 + group), &conflict);
}

static uint16_t configure(resources *pool, unsigned group, unsigned channel, bool update,
                          uint32_t frequency, uint32_t duty, bool invert)
{
    pwm_group *state = &groups[group];
    pwm_output *output = &state->outputs[channel];
    pwm_output *sibling = &state->outputs[1 - channel];
    const bool running = pwm_driver_running(group, channel);
    const bool sibling_running = pwm_driver_running(group, 1 - channel);
    if (state->updating || (running && !update) || (update && !output->configured && sibling_running) ||
        (update && group == 1 && adc_clock_claimed))
        return STATUS_BUSY;
    const uint32_t clock = pwm_driver_clock();
    const unsigned width = group == 0 ? 32 : 16;
    pwm_period plans[PWM_CHANNELS] = {0};
    pwm_period *plan = &plans[channel];
    if (!pwm_calculate(clock, width, frequency, duty, plan)) return STATUS_VALUE;
    plan->invert = invert;
    if (running && (invert != output->period.invert ||
        (output->pulsed && !suitable_for_count(plan, clock)))) return STATUS_VALUE;

    if (sibling->configured) {
        if (!update) {
            if (plan->prescaler != sibling->period.prescaler || plan->reload != sibling->period.reload)
                return STATUS_BUSY;
        } else {
            pwm_period *other = &plans[1 - channel];
            if (!pwm_calculate(clock, width, frequency, sibling->period.requested_duty, other))
                return STATUS_VALUE;
            other->invert = sibling->period.invert;
            if (sibling_running && sibling->pulsed && !suitable_for_count(other, clock))
                return STATUS_VALUE;
        }
    }
    const uint16_t status = reserve_output(pool, group, channel);
    if (status != STATUS_OK) return status;
    output->configured = true;
    if (update && (running || sibling_running) && pwm_driver_update(group, plans)) {
        for (unsigned track = 0; track < PWM_CHANNELS; ++track) state->next[track] = plans[track];
        state->updating = true;
    } else {
        output->period = *plan;
        if (update && sibling->configured) sibling->period = plans[1 - channel];
    }
    return STATUS_OK;
}

static uint16_t capture_command(resources *pool, uint8_t opcode, const uint8_t *input,
                                   uint16_t length, uint8_t *out, uint16_t *written)
{
    if (opcode == 6) {
        if (length != 4 || capture || !wire_u32(input)) return STATUS_VALUE;
        resource_plan plan = {0};
        resource_add(&plan, RESOURCE_PIN('B', 6));
        resource_add(&plan, RESOURCE_TIMER(4));
        uint8_t conflict;
        const uint16_t status = resource_reserve(pool, &plan, 9, &conflict);
        if (status != STATUS_OK) return status;
        capture = pwm_capture_start(wire_u32(input));
        if (!capture) resource_release(pool, 9);
        return capture ? STATUS_OK : STATUS_VALUE;
    }
    if (length || !capture) return STATUS_VALUE;
    if (opcode == 7) {
        uint32_t period, high, clock;
        if (!pwm_capture_read(&period, &high, &clock)) return STATUS_BUSY;
        wire_put32(out, period);
        wire_put32(out + 4, high);
        wire_put32(out + 8, clock);
        *written = 12;
    } else {
        pwm_capture_stop();
        capture = false;
        resource_release(pool, 9);
    }
    return STATUS_OK;
}

uint16_t pwm_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written, uint32_t now)
{
    if (opcode >= 6 && opcode <= 8)
        return capture_command(pool, opcode, input, length, out, written);
    if (!length || input[0] >= PWM_GROUPS) return STATUS_VALUE;
    const unsigned group = input[0];
    unsigned channel = 0;
    ++input;
    --length;
    if (opcode >= 9 && opcode <= 13) {
        if (!length || input[0] == 0 || input[0] > PWM_CHANNELS) return STATUS_VALUE;
        channel = input[0] - 1;
        ++input;
        --length;
        opcode -= 8;
    }
    pwm_group *state = &groups[group];
    pwm_output *output = &state->outputs[channel];
    refresh_period(group);
    switch (opcode) {
    case 1:
    case 3: {
        if (length != 9 || input[8] > 1) return STATUS_VALUE;
        const uint16_t status = configure(pool, group, channel, opcode == 3, wire_u32(input),
                                          wire_u32(input + 4), input[8] != 0);
        if (status != STATUS_OK) return status;
        break;
    }
    case 2: {
        if (length != 8 || !output->configured) return STATUS_VALUE;
        if (pwm_driver_running(group, channel) || state->updating ||
            (group == 1 && adc_clock_claimed)) return STATUS_BUSY;
        const uint32_t duration = wire_u32(input), pulses = wire_u32(input + 4);
        if (!duration || duration > 60000) return STATUS_VALUE;
        if (pulses && !suitable_for_count(&output->period, pwm_driver_clock())) return STATUS_VALUE;
        output->deadline = now + duration;
        output->pulsed = pulses != 0;
        pwm_driver_start(group, channel, &output->period, pulses);
        break;
    }
    case 4:
        if (length) return STATUS_VALUE;
        stop_output(pool, group, channel);
        return STATUS_OK;
    case 5:
        if (length || !output->configured) return STATUS_VALUE;
        break;
    default:
        return STATUS_OPCODE;
    }
    refresh_period(group);
    const pwm_period *period = opcode == 3 && state->updating ? &state->next[channel] : &output->period;
    wire_put32(out, period->frequency_millihz);
    wire_put32(out + 4, period->duty_ppm);
    wire_put32(out + 8, period->prescaler);
    wire_put32(out + 12, period->reload);
    out[16] = pwm_driver_running(group, channel) ? (uint8_t)(1 | (state->updating ? 2 : 0)) : 0;
    *written = 17;
    return STATUS_OK;
}
