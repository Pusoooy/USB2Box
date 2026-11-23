#ifndef USB2BOX_CLOCK_POLICY_H
#define USB2BOX_CLOCK_POLICY_H

#include "bsp.h"

#define BSP_MHZ 1000000u
#define BSP_HSE_SUPPORTED(hz) \
    ((hz) >= 4u * BSP_MHZ && (hz) <= 26u * BSP_MHZ && (hz) % BSP_MHZ == 0u)

enum { BSP_PLL_VCO_MHZ = 336, BSP_PLL_P = 4, BSP_PLL_Q = 7 };

bsp_clock_status clock_select(uint32_t hse_hz);
bool clock_start_pll(uint32_t pll_m, uint32_t pll_n);
bool clock_restore_hsi(void);

#endif
