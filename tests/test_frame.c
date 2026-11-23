#include "frame.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t reference[] = {
        0x55, 0x32, 0x53, 0x58, 1, 0, 36, 2, 5, 0, 4, 7, 1, 0x30, 0, 0,
        0x78, 0x56, 0x34, 0x12, 3, 0, 0, 0, 8, 0, 0, 0, 5, 0, 0, 0, 2, 0, 0xff, 0xff
    };
    uint8_t buffer[FRAME_HEADER_SIZE + 1];
    uint8_t *wire = buffer + 1;
    frame_header header;
    assert(frame_decode(&header, reference, sizeof reference, 64, 256));
    assert(header.request_id == 0x12345678 && header.sequence == UINT16_MAX);
    assert(header.length == 3 && header.total == 8 && header.offset == 5);
    frame_encode(wire, &header);
    assert(memcmp(wire, reference, sizeof reference) == 0);
    for (size_t i = 0; i < sizeof reference; ++i) {
        assert(!frame_decode(&header, wire, i, 64, 256));
    }
    assert(!frame_decode(&header, wire, sizeof reference, 2, 256));
    assert(!frame_decode(&header, wire, sizeof reference, 64, 7));
    header.total = UINT32_MAX;
    header.offset = UINT32_MAX - 1;
    frame_encode(wire, &header);
    assert(!frame_decode(&header, wire, sizeof reference, UINT32_MAX, UINT32_MAX));

    /* Reproducible mutations, including unaligned input and short reads. */
    uint32_t seed = 1;
    for (unsigned i = 0; i < 10000; ++i) {
        memcpy(wire, reference, sizeof reference);
        seed = seed * 1664525u + 1013904223u;
        wire[(seed >> 8) % sizeof reference] ^= (uint8_t)(seed >> 16);
        if (frame_decode(&header, wire, sizeof reference, 64, 256)) {
            assert(header.offset <= header.total);
            assert(header.length <= header.total - header.offset);
            uint8_t encoded[FRAME_HEADER_SIZE];
            frame_encode(encoded, &header);
            assert(memcmp(encoded, wire, sizeof encoded) == 0);
        }
    }
    puts("frame vectors and 10000 deterministic mutations passed");
}
