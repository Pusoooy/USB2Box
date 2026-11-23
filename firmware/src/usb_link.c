#include "bsp.h"
#include "adc_service.h"
#include "usb_link.h"
#include "tusb.h"
#include "device/dcd.h"

static device *link_device;
static volatile bool must_reset;
static uint8_t receive_buffer[64];
static uint16_t received, consumed;
static uint8_t stream[FRAME_HEADER_SIZE + DEVICE_FRAGMENT];
static uint16_t stream_length, stream_offset, stream_sequence;

bool usb_link_start(device *dev)
{
    if (!usb_identity_configured()) return false;
    if (!tud_init(0)) return false;
    tud_disconnect();
    if (!bsp_usb_enable_pins()) return false;
    link_device = dev;
    return tud_connect();
}

void usb_link_shutdown(void)
{
    if (!link_device) return;
    tud_disconnect();
    device_reset(link_device);
    link_device = 0;
}

static void send_stream(void)
{
    if (stream_offset == stream_length) {
        const uint16_t payload = adc_stream_payload(stream + FRAME_HEADER_SIZE);
        if (!payload) return;
        frame_header header = {
            .type = 4, .service = SERVICE_ADC, .opcode = 7, .flags = FLAG_FIRST | FLAG_LAST,
            .length = payload, .total = payload, .stream_id = 1,
            .sequence = stream_sequence++,
        };
        frame_encode(stream, &header);
        stream_length = (uint16_t)(FRAME_HEADER_SIZE + payload);
        stream_offset = 0;
    }
    stream_offset += (uint16_t)tud_vendor_n_write(1, stream + stream_offset,
                                               stream_length - stream_offset);
    tud_vendor_n_write_flush(1);
}

void usb_link_poll(uint32_t now)
{
    if (!link_device) return;
    tud_task_ext(0, false);
    if (__atomic_exchange_n(&must_reset, false, __ATOMIC_RELAXED)) {
        device_reset(link_device);
        received = consumed = 0;
        stream_length = stream_offset = stream_sequence = 0;
    }
    if (!tud_mounted() || tud_suspended()) return;
    if (consumed == received) {
        received = (uint16_t)tud_vendor_n_read(0, receive_buffer, sizeof receive_buffer);
        consumed = 0;
    }
    consumed += (uint16_t)device_feed(link_device, receive_buffer + consumed,
                                      received - consumed, now);
    size_t length;
    const uint8_t *reply = device_reply(link_device, &length);
    if (length) {
        uint32_t accepted = tud_vendor_n_write(0, reply, (uint32_t)length);
        tud_vendor_n_write_flush(0);
        device_consume_reply(link_device, accepted);
    }
    if (link_device->active) send_stream();
    else stream_length = stream_offset = stream_sequence = 0;
}

void OTG_FS_IRQHandler(void)
{
    tud_int_handler(0);
}

void tud_event_hook_cb(uint8_t rhport, uint32_t event, bool in_isr)
{
    (void)rhport;
    (void)in_isr;
    if (event == DCD_EVENT_BUS_RESET || event == DCD_EVENT_UNPLUGGED ||
        event == DCD_EVENT_SUSPEND) __atomic_store_n(&must_reset, true, __ATOMIC_RELAXED);
}

uint32_t tusb_time_millis_api(void)
{
    return bsp_millis();
}
