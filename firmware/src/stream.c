#include "stream.h"
#include "adc_service.h"
#include "uart_service.h"

static void choose_frame(stream_mux *mux, uint32_t now)
{
    uint8_t *body = mux->frame + FRAME_HEADER_SIZE;
    const unsigned start = mux->next;
    for (unsigned step = 0; step < STREAM_COUNT; ++step) {
        const unsigned index = (start + step) % STREAM_COUNT;
        const unsigned stream_id = index + 1;
        uint8_t opcode = UART_STREAM_CREDIT;
        const uint16_t payload = stream_id == STREAM_ADC ? adc_stream_payload(body, &opcode) :
                                 uart_stream_payload(stream_id - STREAM_UART0, body, now);
        if (!payload) continue;

        frame_header header = {
            .type = 4, .service = stream_id == STREAM_ADC ? SERVICE_ADC : SERVICE_UART,
            .opcode = opcode, .flags = FLAG_FIRST | FLAG_LAST, .length = payload,
            .total = payload, .stream_id = (uint16_t)stream_id, .sequence = mux->sequence[index]++,
        };
        frame_encode(mux->frame, &header);
        mux->length = FRAME_HEADER_SIZE + payload;
        mux->next = (uint8_t)((index + 1) % STREAM_COUNT);
        return;
    }
}

const uint8_t *stream_peek(stream_mux *mux, bool session_active, uint32_t now,
                           uint16_t *length)
{
    if (!session_active && mux->offset == 0) mux->length = 0;
    if (!mux->length && session_active) choose_frame(mux, now);
    *length = mux->length - mux->offset;
    return mux->frame + mux->offset;
}

void stream_consume(stream_mux *mux, uint16_t length)
{
    if (length >= mux->length - mux->offset) mux->length = mux->offset = 0;
    else mux->offset += length;
}

void stream_reset(stream_mux *mux)
{
    mux->length = mux->offset = mux->next = 0;
    for (unsigned i = 0; i < STREAM_COUNT; ++i) mux->sequence[i] = 0;
}
