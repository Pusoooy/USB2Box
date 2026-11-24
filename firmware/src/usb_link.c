#include "bsp.h"
#include "stream.h"
#include "usb_link.h"
#include "tusb.h"
#include "device/dcd.h"

static device *link_device;
enum { BUS_RESET = 1, BUS_SUSPEND = 2 };
static uint8_t bus_events;
static uint8_t receive_buffer[64];
static uint16_t received, consumed;
static stream_mux streams;

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

static void send_stream(uint32_t now)
{
    uint16_t length;
    const uint8_t *frame = stream_peek(&streams, link_device->active, now, &length);
    if (!length) return;
    stream_consume(&streams, (uint16_t)tud_vendor_n_write(1, frame, length));
    tud_vendor_n_write_flush(1);
}

void usb_link_poll(uint32_t now)
{
    if (!link_device) return;
    tud_task_ext(0, false);
    const uint8_t events = __atomic_exchange_n(&bus_events, 0, __ATOMIC_RELAXED);
    if (events & BUS_RESET) {
        device_reset(link_device);
        received = consumed = 0;
        stream_reset(&streams);
    } else if (events & BUS_SUSPEND) {
        device_suspend(link_device);
        received = consumed = 0;
        if (tud_mounted()) tud_vendor_n_read_flush(0);
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
    send_stream(now);
}

void OTG_FS_IRQHandler(void)
{
    tud_int_handler(0);
}

void tud_event_hook_cb(uint8_t rhport, uint32_t event, bool in_isr)
{
    (void)rhport;
    (void)in_isr;
    if (event == DCD_EVENT_BUS_RESET || event == DCD_EVENT_UNPLUGGED)
        __atomic_or_fetch(&bus_events, BUS_RESET, __ATOMIC_RELAXED);
    else if (event == DCD_EVENT_SUSPEND)
        __atomic_or_fetch(&bus_events, BUS_SUSPEND, __ATOMIC_RELAXED);
}

uint32_t tusb_time_millis_api(void)
{
    return bsp_millis();
}
