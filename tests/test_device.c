#include "device.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned stopped;
static void stop(void) { ++stopped; }

static uint16_t echo(uint8_t service, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *output, uint16_t *written, uint32_t now)
{
    (void)now;
    if (service != 6 || opcode != 1 || length > DEVICE_FRAGMENT) return STATUS_OPCODE;
    memcpy(output, input, length);
    *written = length;
    return STATUS_OK;
}

static size_t send(device *dev, frame_header header, const void *payload, uint32_t now)
{
    uint8_t wire[FRAME_HEADER_SIZE + DEVICE_FRAGMENT];
    header.type = 1;
    frame_encode(wire, &header);
    if (header.length) memcpy(wire + FRAME_HEADER_SIZE, payload, header.length);
    for (size_t i = 0; i < FRAME_HEADER_SIZE + header.length; ++i)
        assert(device_feed(dev, wire + i, 1, now) == 1);
    size_t length;
    device_reply(dev, &length);
    return length;
}

static uint32_t reply(device *dev, uint16_t status, uint32_t request)
{
    size_t length;
    const uint8_t *wire = device_reply(dev, &length);
    frame_header h;
    assert(frame_decode(&h, wire, length, DEVICE_FRAGMENT, DEVICE_FRAGMENT));
    assert(h.type == 2 && h.status == status && h.request_id == request);
    const uint32_t value = h.length == 4 ? wire_u32(wire + FRAME_HEADER_SIZE) : 0;
    device_consume_reply(dev, 1);
    device_consume_reply(dev, length - 1);
    return value;
}

int main(void)
{
    device dev;
    device_init(&dev, echo, stop);
    assert(stopped == 1);
    const uint32_t now = UINT32_MAX - 500;
    frame_header h = {.opcode = SYS_OPEN, .request_id = UINT32_MAX,
                      .flags = FLAG_FIRST | FLAG_LAST };
    send(&dev, h, NULL, now);
    const uint32_t lease = reply(&dev, STATUS_OK, UINT32_MAX);
    assert(lease != 0);
    send(&dev, h, NULL, now);
    reply(&dev, STATUS_DUPLICATE, UINT32_MAX);
    h = (frame_header){.service = 6, .opcode = 1, .request_id = 1,
                       .flags = FLAG_FIRST, .length = 2, .total = 4};
    assert(send(&dev, h, "ab", now) == 0);
    h.flags = FLAG_LAST;
    h.offset = 2;
    h.sequence = 1;
    send(&dev, h, "cd", now);
    size_t length;
    const uint8_t *wire = device_reply(&dev, &length);
    assert(length == 40 && memcmp(wire + FRAME_HEADER_SIZE, "abcd", 4) == 0);
    reply(&dev, STATUS_OK, 1);
    assert(send(&dev, h, "cd", now));
    reply(&dev, STATUS_FRAME, 1);

    uint8_t heartbeat[4];
    wire_put32(heartbeat, lease);
    h = (frame_header){.opcode = SYS_HEARTBEAT, .request_id = 2,
                       .flags = FLAG_FIRST | FLAG_LAST, .length = 4, .total = 4};
    send(&dev, h, heartbeat, now + 1900);
    reply(&dev, STATUS_OK, 2);
    device_tick(&dev, now + 3899);
    assert(dev.active && stopped == 1);
    device_tick(&dev, now + 3900);
    assert(!dev.active && stopped == 2);
    h.service = 6;
    ++h.request_id;
    send(&dev, h, heartbeat, now + 3900);
    reply(&dev, STATUS_SESSION, 3);

    uint8_t joined[3 + 2 * FRAME_HEADER_SIZE] = {0x55, 0xff, 0x32};
    h = (frame_header){.type = 1, .opcode = SYS_VERSION,
                       .request_id = 4, .flags = FLAG_FIRST | FLAG_LAST};
    frame_encode(joined + 3, &h);
    ++h.request_id;
    frame_encode(joined + 3 + FRAME_HEADER_SIZE, &h);
    assert(device_feed(&dev, joined, sizeof joined, now + 4000) == 3 + FRAME_HEADER_SIZE);
    reply(&dev, STATUS_OK, 4);
    assert(device_feed(&dev, joined + 3 + FRAME_HEADER_SIZE, FRAME_HEADER_SIZE,
                       now + 4000) == FRAME_HEADER_SIZE);
    reply(&dev, STATUS_OK, 5);
    device_reset(&dev);
    assert(stopped == 3);
    puts("device Mock Bulk framing, request wrap, backpressure and lease passed");
}
