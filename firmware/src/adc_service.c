#include "adc_service.h"

static adc_config config;
static uint16_t cache[ADC_CACHE];
static uint32_t produced, consumed, target, dropped, rate, stream_tag;
static uint16_t pre_trigger, post_trigger, credits;
static bool configured, running, awaiting_trigger, stream_finished, threshold_seen;

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
    produced = consumed = 0;
    resource_release(pool, 8);
}

static void trigger(void)
{
    if (produced - consumed > pre_trigger) consumed = produced - pre_trigger;
    target = produced + post_trigger;
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
        if (produced - consumed == ADC_CACHE) {
            if (!awaiting_trigger) {
                dropped += count - i;
                stop();
                return;
            }
            ++consumed;
        }
        cache[produced++ % ADC_CACHE] = block[i];
        if (!awaiting_trigger && target && produced == target) {
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
    adc_config next = {.resolution = input[0] ? input[0] : 12, .ranks = input[1]};
    if (next.resolution != 6 && next.resolution != 8 &&
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

static uint16_t start(uint32_t hz, uint16_t samples, uint16_t pre, uint16_t post)
{
    if (!configured || running) return STATUS_BUSY;
    if (hz > 100000 || (hz && hz < config.ranks) || samples > ADC_CACHE ||
        (pre || post ? samples || !post || (uint32_t)pre + post > ADC_CACHE : false))
        return STATUS_VALUE;
    if (!hz && (samples != 1 || config.ranks != 1 || pre || post)) return STATUS_VALUE;
    produced = consumed = dropped = 0;
    target = samples;
    pre_trigger = pre;
    post_trigger = post;
    awaiting_trigger = post != 0;
    credits = 0;
    stream_tag = 0;
    stream_finished = threshold_seen = false;
    rate = adc_driver_start(&config, hz, post ? 0 : samples);
    running = rate != 0;
    if (!running) awaiting_trigger = false;
    return running ? STATUS_OK : STATUS_IO;
}

static uint16_t read_samples(uint8_t *out, uint16_t limit)
{
    const uint32_t available = awaiting_trigger ? 0 : produced - consumed;
    if (limit > available) limit = (uint16_t)available;
    wire_put32(out, rate);
    wire_put32(out + 4, consumed);
    wire_put32(out + 8, dropped);
    wire_put16(out + 12, limit);
    out[14] = config.resolution;
    out[15] = (uint8_t)((running ? 1 : 0) | (awaiting_trigger ? 2 : 0) |
                        (available > limit ? 4 : 0) | (threshold_seen ? 8 : 0));
    for (unsigned i = 0; i < limit; ++i)
        wire_put16(out + 16 + 2 * i, cache[consumed++ % ADC_CACHE]);
    return (uint16_t)(16 + 2 * limit);
}

uint16_t adc_stream_payload(uint8_t *out, uint8_t *opcode)
{
    if (!credits || awaiting_trigger || stream_finished ||
        (produced == consumed && running)) return 0;
    --credits;
    const uint16_t prefix = stream_tag ? 4 : 0;
    if (stream_tag) wire_put32(out, stream_tag);
    *opcode = stream_tag ? ADC_TAGGED_CREDIT : ADC_STREAM_CREDIT;
    const uint16_t length = read_samples(out + prefix, (DEVICE_FRAGMENT - prefix - 16) / 2);
    if (!running && produced == consumed) stream_finished = true;
    return prefix + length;
}

uint16_t adc_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written)
{
    switch (opcode) {
    case 1:
        return configure(pool, input, length);
    case 2:
        return length == 0 ? start(0, 1, 0, 0) : STATUS_VALUE;
    case 3:
        return length == 10 ? start(wire_u32(input), wire_u16(input + 4),
                                    wire_u16(input + 6), wire_u16(input + 8)) : STATUS_VALUE;
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
        if (low > high || high >= (1u << config.resolution)) return STATUS_VALUE;
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
    case ADC_TAGGED_CREDIT: {
        if (length != 6 || !configured || !wire_u32(input)) return STATUS_VALUE;
        const uint32_t tag = wire_u32(input);
        const uint16_t count = wire_u16(input + 4);
        const uint16_t pending = tag == stream_tag ? credits : 0;
        if (count > 16u - pending) return STATUS_VALUE;
        stream_tag = tag;
        credits = pending + count;
        return STATUS_OK;
    }
    default:
        return STATUS_OPCODE;
    }
}
