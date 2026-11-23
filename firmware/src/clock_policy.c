#include "clock_policy.h"

bsp_clock_status clock_select(uint32_t hse_hz)
{
    if (hse_hz == 0) {
        return BSP_CLOCK_HSI;
    }
    if (!BSP_HSE_SUPPORTED(hse_hz)) {
        return BSP_CLOCK_ERROR;
    }

    /* Prefer a 2 MHz PLL input to reduce jitter. Odd MHz crystals use 1 MHz. */
    const uint32_t pll_input = hse_hz % (2u * BSP_MHZ) == 0
                            ? 2u * BSP_MHZ : BSP_MHZ;
    const uint32_t pll_n = BSP_PLL_VCO_MHZ / (pll_input / BSP_MHZ);
    if (clock_start_pll(hse_hz / pll_input, pll_n)) {
        return BSP_CLOCK_HSE_PLL;
    }
    return clock_restore_hsi() ? BSP_CLOCK_FALLBACK_HSI : BSP_CLOCK_ERROR;
}
