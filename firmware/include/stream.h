#ifndef USB2BOX_STREAM_H
#define USB2BOX_STREAM_H

#include "device.h"

enum { STREAM_ADC = 1, STREAM_UART0, STREAM_UART1, STREAM_COUNT = STREAM_UART1 };

typedef struct {
    uint8_t frame[FRAME_HEADER_SIZE + DEVICE_FRAGMENT];
    uint16_t length, offset, sequence[STREAM_COUNT];
    uint8_t next;
} stream_mux;

const uint8_t *stream_peek(stream_mux *mux, bool session_active, uint32_t now,
                           uint16_t *length);
void stream_consume(stream_mux *mux, uint16_t length);
void stream_reset(stream_mux *mux);

#endif
