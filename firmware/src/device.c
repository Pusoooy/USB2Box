#include "device.h"

uint16_t wire_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

uint32_t wire_u32(const uint8_t *p)
{
    return wire_u16(p) | ((uint32_t)wire_u16(p + 2) << 16);
}

void wire_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

void wire_put32(uint8_t *p, uint32_t value)
{
    wire_put16(p, (uint16_t)value);
    wire_put16(p + 2, (uint16_t)(value >> 16));
}

static void reset_rx(device *dev)
{
    dev->header_used = 0;
    dev->payload_used = 0;
    dev->assembling = false;
    dev->assembled = 0;
}

void device_init(device *dev, device_command command, device_stop stop)
{
    dev->command = command;
    dev->stop = stop;
    dev->active = false;
    dev->lease_id = 0;
    dev->requests = dev->invalid_headers = dev->errors = dev->lease_expired = dev->resets = 0;
    device_reset(dev);
}

static void stop_session(device *dev)
{
    dev->active = false;
    if (dev->stop) {
        dev->stop();
    }
}

void device_reset(device *dev)
{
    ++dev->resets;
    stop_session(dev);
    reset_rx(dev);
    dev->tx_size = 0;
    dev->tx_sent = 0;
    dev->last_request = 0;
}

void device_tick(device *dev, uint32_t now)
{
    if (dev->active && now - dev->lease_at >= DEVICE_LEASE_MS) {
        ++dev->lease_expired;
        stop_session(dev);
        reset_rx(dev);
    }
    if ((dev->assembling || dev->header_used) &&
        now - dev->partial_at >= DEVICE_FRAGMENT_MS) {
        reset_rx(dev);
    }
}

static uint16_t system_command(device *dev, uint16_t length, uint8_t *out,
                               uint16_t *written, uint32_t now)
{
    switch (dev->message.opcode) {
    case SYS_HELLO:
    case SYS_CAPABILITIES:
        if (length) return STATUS_VALUE;
        wire_put16(out, DEVICE_FRAGMENT);
        wire_put16(out + 2, DEVICE_MESSAGE);
        wire_put32(out + 4, DEVICE_LEASE_MS);
        *written = 8;
        return STATUS_OK;
    case SYS_VERSION:
        if (length) return STATUS_VALUE;
        out[0] = 1;
        out[1] = 0;
        *written = 2;
        return STATUS_OK;
    case SYS_DEVICE_INFO:
        if (length) return STATUS_VALUE;
        for (unsigned i = 0; i < 7; ++i) out[i] = (uint8_t)"USB2Box"[i];
        *written = 7;
        return STATUS_OK;
    case SYS_OPEN:
        if (length) return STATUS_VALUE;
        if (dev->active) return STATUS_BUSY;
        if (++dev->lease_id == 0) ++dev->lease_id;
        dev->active = true;
        dev->lease_at = now;
        wire_put32(out, dev->lease_id);
        *written = 4;
        return STATUS_OK;
    case SYS_HEARTBEAT:
    case SYS_CLOSE:
        if (!dev->active || length != 4 || wire_u32(dev->input) != dev->lease_id)
            return STATUS_SESSION;
        if (dev->message.opcode == SYS_CLOSE) stop_session(dev);
        else dev->lease_at = now;
        return STATUS_OK;
    case SYS_STOP:
        if (length) return STATUS_VALUE;
        stop_session(dev);
        return STATUS_OK;
    case SYS_CANCEL:
    case SYS_COLLECT:
        if (!dev->active) return STATUS_SESSION;
        break;
    default:
        break;
    }
    return dev->command ? dev->command(0, dev->message.opcode, dev->input,
                                       length, out, written, now) : STATUS_OPCODE;
}

static void enqueue_reply(device *dev, const frame_header *request,
                          uint16_t status, uint16_t written)
{
    frame_header reply = {
        .type = 2, .service = request->service, .opcode = request->opcode,
        .flags = FLAG_FIRST | FLAG_LAST, .request_id = request->request_id,
    };
    if (status != STATUS_OK || written > DEVICE_FRAGMENT) {
        if (status == STATUS_OK) status = STATUS_FULL;
        ++dev->errors;
        reply.flags |= FLAG_ERROR;
        uint8_t *error = dev->tx + FRAME_HEADER_SIZE;
        const uint8_t resource = status == STATUS_BUSY && written == 1 ? error[0] : 0xff;
        error[0] = (uint8_t)(status >> 12);
        error[1] = resource;
        error[2] = status == STATUS_BUSY || status == STATUS_FULL || status == STATUS_TIMEOUT;
        error[3] = 0;
        wire_put32(error + 4, dev->partial_at);
        wire_put32(error + 8, 0);
        written = 12;
    }
    reply.status = status;
    reply.length = written;
    reply.total = written;
    frame_encode(dev->tx, &reply);
    dev->tx_size = FRAME_HEADER_SIZE + written;
    dev->tx_sent = 0;
}

static void dispatch(device *dev, uint32_t now)
{
    ++dev->requests;
    uint16_t written = 0;
    uint16_t status = STATUS_SESSION;
    uint8_t *out = dev->tx + FRAME_HEADER_SIZE;
    if (dev->message.request_id == dev->last_request) {
        status = STATUS_DUPLICATE;
    } else {
        dev->last_request = dev->message.request_id;
        if (dev->message.service == 0) {
            status = system_command(dev, dev->assembled, out, &written, now);
        } else if (dev->message.service == SERVICE_DIAG && dev->message.opcode == 1 && !dev->assembled) {
            wire_put32(out, dev->requests);
            wire_put32(out + 4, dev->invalid_headers);
            wire_put32(out + 8, dev->errors);
            wire_put32(out + 12, dev->lease_expired);
            wire_put32(out + 16, dev->resets);
            written = 20;
            status = STATUS_OK;
        } else if (dev->active) {
            status = dev->command ? dev->command(dev->message.service, dev->message.opcode,
                     dev->input, dev->assembled, out, &written, now) : STATUS_OPCODE;
        }
    }
    enqueue_reply(dev, &dev->message, status, written);
}

static void finish_fragment(device *dev, uint32_t now)
{
    const frame_header *h = &dev->fragment;
    const bool first = (h->flags & FLAG_FIRST) != 0;
    const bool last = (h->flags & FLAG_LAST) != 0;
    const bool valid = h->type == 1 && h->minor == 0 && h->status == 0 &&
                       (h->flags & ~(FLAG_FIRST | FLAG_LAST)) == 0 &&
                       h->stream_id == 0 && (h->length || (first && last)) &&
                       last == (h->offset + h->length == h->total);
    if (!valid || (first && (h->offset != 0 || h->sequence != 0 || dev->assembling)) ||
        (!first && (!dev->assembling || h->offset != dev->assembled ||
         h->sequence != (uint16_t)(dev->message.sequence + 1u) ||
         h->request_id != dev->message.request_id || h->service != dev->message.service ||
         h->opcode != dev->message.opcode || h->total != dev->message.total))) {
        reset_rx(dev);
        if (h->type == 1) enqueue_reply(dev, h, STATUS_FRAME, 0);
        return;
    }
    if (first) dev->message = *h;
    dev->message.sequence = h->sequence;
    for (uint32_t i = 0; i < h->length; ++i) {
        dev->input[dev->assembled++] = dev->payload[i];
    }
    dev->header_used = 0;
    dev->payload_used = 0;
    dev->assembling = !last;
    if (last) {
        dispatch(dev, now);
        dev->assembled = 0;
    }
}

size_t device_feed(device *dev, const uint8_t *bytes, size_t length, uint32_t now)
{
    size_t offset = 0;
    device_tick(dev, now);
    while (offset < length && !dev->tx_size) {
        dev->partial_at = now;
        if (dev->header_used < FRAME_HEADER_SIZE) {
            dev->header[dev->header_used++] = bytes[offset++];
            if (dev->header_used != FRAME_HEADER_SIZE) continue;
            if (!frame_decode(&dev->fragment, dev->header, FRAME_HEADER_SIZE,
                              DEVICE_FRAGMENT, DEVICE_MESSAGE)) {
                ++dev->invalid_headers;
                dev->assembling = false;
                dev->assembled = 0;
                for (size_t i = 1; i < FRAME_HEADER_SIZE; ++i) {
                    dev->header[i - 1] = dev->header[i];
                }
                --dev->header_used;
                continue;
            }
        } else {
            dev->payload[dev->payload_used++] = bytes[offset++];
        }
        if (dev->payload_used == dev->fragment.length) finish_fragment(dev, now);
    }
    return offset;
}

const uint8_t *device_reply(const device *dev, size_t *length)
{
    *length = dev->tx_size - dev->tx_sent;
    return dev->tx + dev->tx_sent;
}

void device_consume_reply(device *dev, size_t length)
{
    if (length >= (size_t)(dev->tx_size - dev->tx_sent)) {
        dev->tx_size = 0;
        dev->tx_sent = 0;
    } else {
        dev->tx_sent += (uint16_t)length;
    }
}
