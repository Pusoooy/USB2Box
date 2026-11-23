#include "frame.h"

static uint32_t read_le(const uint8_t *bytes, unsigned width)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i) {
        value |= (uint32_t)bytes[i] << (8u * i);
    }
    return value;
}

static void write_le(uint8_t *bytes, uint32_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i) {
        bytes[i] = (uint8_t)(value >> (8u * i));
    }
}

bool frame_decode(frame_header *header, const uint8_t *wire, size_t available,
                  uint32_t max_fragment, uint32_t max_total)
{
    if (available < FRAME_HEADER_SIZE || read_le(wire, 4) != 0x58533255 ||
        wire[4] != 1 || wire[6] != FRAME_HEADER_SIZE ||
        wire[7] < 1 || wire[7] > 5 || read_le(wire + 14, 2) != 0) {
        return false;
    }
    const frame_header parsed = {
        .minor = wire[5], .type = wire[7], .service = wire[10], .opcode = wire[11],
        .flags = read_le(wire + 8, 2), .status = read_le(wire + 12, 2),
        .request_id = read_le(wire + 16, 4), .length = read_le(wire + 20, 4),
        .total = read_le(wire + 24, 4), .offset = read_le(wire + 28, 4),
        .stream_id = read_le(wire + 32, 2), .sequence = read_le(wire + 34, 2)
    };
    if (parsed.length > max_fragment || parsed.total > max_total ||
        parsed.offset > parsed.total || parsed.length > parsed.total - parsed.offset ||
        (parsed.type <= 2 && parsed.request_id == 0)) {
        return false;
    }
    *header = parsed;
    return true;
}

void frame_encode(uint8_t wire[static FRAME_HEADER_SIZE], const frame_header *header)
{
    write_le(wire, 0x58533255, 4);
    wire[4] = 1;
    wire[5] = header->minor;
    wire[6] = FRAME_HEADER_SIZE;
    wire[7] = header->type;
    write_le(wire + 8, header->flags, 2);
    wire[10] = header->service;
    wire[11] = header->opcode;
    write_le(wire + 12, header->status, 2);
    write_le(wire + 14, 0, 2);
    write_le(wire + 16, header->request_id, 4);
    write_le(wire + 20, header->length, 4);
    write_le(wire + 24, header->total, 4);
    write_le(wire + 28, header->offset, 4);
    write_le(wire + 32, header->stream_id, 2);
    write_le(wire + 34, header->sequence, 2);
}
