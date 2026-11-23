#include "bsp.h"
#include "app.h"
#include "device.h"
#include "usb_link.h"

static device dev;

int main(void)
{
    const bsp_clock_status clock = bsp_init();
    app_init();
    device_init(&dev, app_command, app_stop);
    if (clock == BSP_CLOCK_HSE_PLL) usb_link_start(&dev);
    for (;;) {
        const uint32_t now = bsp_millis();
        device_tick(&dev, now);
        app_poll(now);
        usb_link_poll(now);
        __asm__ volatile ("wfi");
    }
}
