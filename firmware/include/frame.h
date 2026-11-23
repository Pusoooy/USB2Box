#ifndef USB2BOX_FRAME_H
#define USB2BOX_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { FRAME_HEADER_SIZE = 36 };

typedef struct {
    uint8_t minor, type, service, opcode;
    uint16_t flags, status, stream_id, sequence;
    uint32_t request_id, length, total, offset;
} frame_header;

/* The caller provides a full header and the negotiated logical-message limit. */
bool frame_decode(frame_header *header, const uint8_t *wire, size_t available,
                  uint32_t max_fragment, uint32_t max_total);
void frame_encode(uint8_t wire[static FRAME_HEADER_SIZE], const frame_header *header);

#endif
