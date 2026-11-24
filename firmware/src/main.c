#include "bsp.h"
#include "app.h"
#include "device.h"
#include "usb_link.h"
#include "invariant.h"

#ifdef USB2BOX_DEBUG
#include "stm32f4xx.h"

static const char *volatile assert_file;
static volatile uint32_t assert_line;

_Noreturn void usb2_assert_failed(const char *file, uint32_t line)
{
    if (!assert_line) {
        assert_file = file;
        assert_line = line;
        app_stop();
        usb_link_shutdown();
    }
    __disable_irq();
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) __BKPT(0);
    for (;;) __WFI();
}
#endif

static device dev;

int main(void)
{
    const bsp_clock_status clock = bsp_init();
    app_init();
    device_init(&dev, app_command, app_stop);
    if (clock == BSP_CLOCK_HSE_PLL) usb_link_start(&dev);
    for (;;) {
        if (bsp_handle_clock_failure()) usb_link_shutdown();
        const uint32_t now = bsp_millis();
        device_tick(&dev, now);
        app_poll(now);
        usb_link_poll(now);
        __asm__ volatile ("wfi");
    }
}
