#ifndef USB2BOX_BSP_H
#define USB2BOX_BSP_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_CLOCK_HSI,
    BSP_CLOCK_HSE_PLL,
    BSP_CLOCK_FALLBACK_HSI,
    BSP_CLOCK_ERROR,
} bsp_clock_status;

bsp_clock_status bsp_init(void);
uint32_t bsp_millis(void);
bool bsp_handle_clock_failure(void);

/* Call after the USB device controller is configured and soft-disconnected.
 * The DCD owns the USB interrupt enable and connect sequence.
 */
bool bsp_usb_enable_pins(void);

#endif
