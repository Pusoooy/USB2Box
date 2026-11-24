#include "adc_service.h"

static adc_config config;
static adc_plan capture;
static uint16_t cache[ADC_CACHE];
static uint64_t total_samples, target;
static uint32_t consumed, dropped, start_ms, stream_tag;
static uint16_t pre_trigger, post_trigger, credits;
static uint8_t stream_mode;
static bool configured, running, awaiting_trigger, stream_finished, threshold_seen;

/* ADC clock cycles from the STM32F401 regular conversion settings. */
static const uint16_t sample_cycles[] = {3, 15, 28, 56, 84, 112, 144, 480};

bool adc_plan_capture(const adc_config *cfg, uint32_t hz, uint32_t timer_hz,
                      uint32_t adc_hz, adc_plan *plan)
{
    if (!timer_hz || !adc_hz || !cfg->ranks || cfg->ranks > ADC_RANKS ||
        (hz && hz < cfg->ranks)) return false;
    const uint64_t scaled = (uint64_t)timer_hz * cfg->ranks;
    const uint64_t ticks = hz ? (scaled + hz - 1) / hz : 0;
    if (ticks > UINT32_MAX) return false;
    uint32_t sample_total = 0;
    for (unsigned rank = 0; rank < cfg->ranks; ++rank) {
        if (cfg->times[rank] >= sizeof sample_cycles / sizeof sample_cycles[0]) return false;
        sample_total += sample_cycles[cfg->times[rank]];
    }
    uint8_t bits = cfg->resolution ? cfg->resolution : 12;
    while (bits >= 6 && ticks &&
           (uint64_t)(sample_total + (bits + 2u) * cfg->ranks) * timer_hz > ticks * adc_hz) {
        if (cfg->resolution) return false;
        bits -= 2;
    }
    if (bits != 6 && bits != 8 && bits != 10 && bits != 12) return false;

    plan->resolution = bits;
    plan->timer_ticks = (uint32_t)ticks;
    plan->aggregate_rate = ticks ? (uint32_t)(scaled / ticks) : 1;
    plan->scan_ns = (uint32_t)((ticks * 1000000000u + timer_hz / 2u) / timer_hz);
    uint32_t elapsed = 0;
    for (unsigned rank = 0; rank < cfg->ranks; ++rank) {
        const uint32_t sample = sample_cycles[cfg->times[rank]];
        plan->rank_ns[rank] = (uint32_t)((uint64_t)(elapsed + sample) * 1000000000u / adc_hz);
        elapsed += sample + bits;
    }
    return true;
}

static void stop(void)
{
    if (running) adc_driver_stop();
    running = awaiting_trigger = false;
}

void adc_stop_all(resources *pool)
{
    stop();
    configured = false;
    credits = 0;
    stream_tag = 0;
    total_samples = consumed = 0;
    capture = (adc_plan){0};
    resource_release(pool, 8);
}

static void trigger(void)
{
    const uint32_t produced = (uint32_t)total_samples;
    if (produced - consumed > pre_trigger) consumed = produced - pre_trigger;
    target = total_samples + post_trigger;
    awaiting_trigger = false;
}

void adc_poll(void)
{
    if (!running) return;
    if (config.watchdog && adc_driver_triggered()) {
        threshold_seen = true;
        if (awaiting_trigger) trigger();
    }
    uint16_t block[ADC_BLOCK];
    uint32_t lost = 0;
    const uint16_t count = adc_driver_read(block, &lost);
    if (lost) {
        dropped += lost;
        stop();
        return;
    }
    for (unsigned i = 0; i < count; ++i) {
        if ((uint32_t)total_samples - consumed == ADC_CACHE) {
            if (!awaiting_trigger) {
                dropped += count - i;
                stop();
                return;
            }
            ++consumed;
        }
        const uint32_t index = (uint32_t)total_samples++;
        cache[index % ADC_CACHE] = block[i];
        if (!awaiting_trigger && target && total_samples == target) {
            stop();
            return;
        }
    }
}

static const uint8_t channel_pin[] = {
    RESOURCE_PIN('A', 0), RESOURCE_PIN('A', 1), RESOURCE_PIN('A', 2), RESOURCE_PIN('A', 3),
    RESOURCE_PIN('A', 4), RESOURCE_PIN('A', 5), RESOURCE_PIN('A', 6), RESOURCE_PIN('A', 7),
    RESOURCE_PIN('B', 0), RESOURCE_PIN('B', 1), RESOURCE_PIN('C', 0), RESOURCE_PIN('C', 1),
    RESOURCE_PIN('C', 2), RESOURCE_PIN('C', 3), RESOURCE_PIN('C', 4), RESOURCE_PIN('C', 5),
};

static uint16_t configure(resources *pool, const uint8_t *input, uint16_t length)
{
    if (length < 4 || !input[1] || input[1] > 8 || length != 2u + 2u * input[1])
        return STATUS_VALUE;
    if (configured) return STATUS_BUSY;
    adc_config next = {.resolution = input[0], .ranks = input[1]};
    if (next.resolution != 0 && next.resolution != 6 && next.resolution != 8 &&
        next.resolution != 10 && next.resolution != 12) return STATUS_VALUE;

    resource_plan plan = {0};
    resource_add(&plan, RESOURCE_ADC);
    resource_add(&plan, RESOURCE_TIMER(2));
    if (!resource_dma_add(&plan, pool, 8, "ADC1", 2, 4, 0)) return STATUS_BUSY;
    bool temp = false, vbat = false;
    for (unsigned i = 0; i < next.ranks; ++i) {
        const uint8_t channel = input[2 + 2 * i], time = input[3 + 2 * i];
        if (channel > 18 || time > 7 || (channel >= 16 && time < 7)) return STATUS_VALUE;
        if (channel < 16 && !resource_add(&plan, channel_pin[channel])) return STATUS_VALUE;
        temp |= channel == 16;
        vbat |= channel == 18;
        next.channels[i] = channel;
        next.times[i] = time;
    }
    if (temp && vbat) return STATUS_VALUE;
    uint8_t conflict;
    const uint16_t status = resource_reserve(pool, &plan, 8, &conflict);
    if (status != STATUS_OK) return status;
    config = next;
    configured = true;
    return STATUS_OK;
}

static uint16_t start(uint32_t hz, uint16_t samples, uint16_t pre, uint16_t post, uint32_t now)
{
    if (!configured || running) return STATUS_BUSY;
    if (hz > 100000 || (hz && hz < config.ranks) || samples > ADC_CACHE ||
        (pre || post ? samples || !post || (uint32_t)pre + post > ADC_CACHE : false))
        return STATUS_VALUE;
    if (!hz && (samples != 1 || config.ranks != 1 || pre || post)) return STATUS_VALUE;
    adc_plan plan = {0};
    uint32_t timer, converter;
    adc_driver_clocks(&timer, &converter);
    if (!adc_plan_capture(&config, hz, timer, converter, &plan)) return STATUS_VALUE;
    adc_config active = config;
    active.resolution = plan.resolution;
    if (!config.resolution && active.watchdog) {
        active.low >>= 12 - plan.resolution;
        active.high >>= 12 - plan.resolution;
    }
    if (!adc_driver_start(&active, &plan, post ? 0 : samples)) return STATUS_IO;
    capture = plan;
    start_ms = now;
    total_samples = consumed = dropped = 0;
    target = samples;
    pre_trigger = pre;
    post_trigger = post;
    awaiting_trigger = post != 0;
    credits = 0;
    stream_tag = 0;
    stream_mode = 0;
    stream_finished = threshold_seen = false;
    running = true;
    return STATUS_OK;
}

static uint16_t read_samples(uint8_t *out, uint16_t limit)
{
    const uint32_t available = awaiting_trigger ? 0 : (uint32_t)total_samples - consumed;
    if (limit > available) limit = (uint16_t)available;
    wire_put32(out, capture.aggregate_rate);
    wire_put32(out + 4, consumed);
    wire_put32(out + 8, dropped);
    wire_put16(out + 12, limit);
    out[14] = capture.resolution ? capture.resolution : (config.resolution ? config.resolution : 12);
    out[15] = (uint8_t)((running ? 1 : 0) | (awaiting_trigger ? 2 : 0) |
                        (available > limit ? 4 : 0) | (threshold_seen ? 8 : 0));
    for (unsigned i = 0; i < limit; ++i)
        wire_put16(out + 16 + 2 * i, cache[consumed++ % ADC_CACHE]);
    return (uint16_t)(16 + 2 * limit);
}

static uint16_t timing_metadata(uint8_t *out, uint32_t tag)
{
    const uint32_t available = (uint32_t)total_samples - consumed;
    const uint64_t first = total_samples - available;
    const uint8_t rank = (uint8_t)(first % config.ranks);
    const uint64_t cycle = first / config.ranks + (capture.timer_ticks != 0);
    const uint64_t first_us = (uint64_t)start_ms * 1000u +
                              (cycle * capture.scan_ns + capture.rank_ns[rank]) / 1000u;
    wire_put32(out, tag);
    wire_put64(out + 4, first_us);
    wire_put32(out + 12, capture.scan_ns);
    out[16] = config.ranks;
    out[17] = rank;
    for (unsigned i = 0; i < config.ranks; ++i) {
        out[18 + i * 5] = config.channels[i];
        wire_put32(out + 19 + i * 5, capture.rank_ns[i]);
    }
    return (uint16_t)(18 + 5u * config.ranks);
}

uint16_t adc_stream_payload(uint8_t *out, uint8_t *opcode)
{
    if (!credits || awaiting_trigger || stream_finished ||
        ((uint32_t)total_samples == consumed && running)) return 0;
    --credits;
    uint16_t prefix = 0;
    if (stream_mode == ADC_TIMED_CREDIT) {
        prefix = timing_metadata(out, stream_tag);
    } else if (stream_tag) {
        wire_put32(out, stream_tag);
        prefix = 4;
    }
    *opcode = stream_tag ? stream_mode : ADC_STREAM_CREDIT;
    const uint16_t length = read_samples(out + prefix, (DEVICE_FRAGMENT - prefix - 16) / 2);
    if (!running && (uint32_t)total_samples == consumed) stream_finished = true;
    return prefix + length;
}

uint16_t adc_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written, uint32_t now)
{
    switch (opcode) {
    case 1:
        return configure(pool, input, length);
    case 2:
        return length == 0 ? start(0, 1, 0, 0, now) : STATUS_VALUE;
    case 3:
        return length == 10 ? start(wire_u32(input), wire_u16(input + 4),
                                    wire_u16(input + 6), wire_u16(input + 8), now) : STATUS_VALUE;
    case 4:
        if (length != 0) return STATUS_VALUE;
        adc_stop_all(pool);
        return STATUS_OK;
    case 5:
        if (length != 0 || !awaiting_trigger) return STATUS_VALUE;
        trigger();
        return STATUS_OK;
    case 6: {
        if (length != 2 || !configured) return STATUS_VALUE;
        const uint16_t count = wire_u16(input);
        if (count > (DEVICE_FRAGMENT - 16) / 2) return STATUS_VALUE;
        *written = read_samples(out, count);
        return STATUS_OK;
    }
    case ADC_STREAM_CREDIT:
        if (length != 2 || !configured || stream_tag || wire_u16(input) > 16u - credits)
            return STATUS_VALUE;
        credits += wire_u16(input);
        return STATUS_OK;
    case 8: {
        if (length != 5 || !configured || running) return STATUS_VALUE;
        const uint16_t low = wire_u16(input + 1), high = wire_u16(input + 3);
        if (low > high || high >= (1u << (config.resolution ? config.resolution : 12)))
            return STATUS_VALUE;
        for (unsigned rank = 0; rank < config.ranks; ++rank) {
            if (config.channels[rank] != input[0]) continue;
            config.watchdog = true;
            config.watchdog_channel = input[0];
            config.low = low;
            config.high = high;
            return STATUS_OK;
        }
        return STATUS_VALUE;
    }
    case ADC_TAGGED_CREDIT:
    case ADC_TIMED_CREDIT: {
        if (length != 6 || !configured || !capture.resolution || !wire_u32(input))
            return STATUS_VALUE;
        const uint32_t tag = wire_u32(input);
        const uint16_t count = wire_u16(input + 4);
        const uint16_t pending = tag == stream_tag && opcode == stream_mode ? credits : 0;
        if (count > 16u - pending) return STATUS_VALUE;
        stream_tag = tag;
        stream_mode = opcode;
        credits = pending + count;
        return STATUS_OK;
    }
    case ADC_TIMED_READ: {
        if (length != 2 || !configured || !capture.resolution) return STATUS_VALUE;
        const uint16_t prefix = timing_metadata(out, 0);
        const uint16_t count = wire_u16(input);
        if (count > (DEVICE_FRAGMENT - prefix - 16) / 2) return STATUS_VALUE;
        *written = prefix + read_samples(out + prefix, count);
        return STATUS_OK;
    }
    case ADC_REFERENCE_CAL: {
        if (length) return STATUS_VALUE;
        uint16_t code, millivolts;
        adc_driver_reference(&code, &millivolts);
        if (!code || code > 4095 || !millivolts) return STATUS_IO;
        wire_put16(out, code);
        wire_put16(out + 2, millivolts);
        *written = 4;
        return STATUS_OK;
    }
    default:
        return STATUS_OPCODE;
    }
}
