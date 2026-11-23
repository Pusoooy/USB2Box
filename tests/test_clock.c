#include "clock_policy.h"

#include <assert.h>
#include <stdio.h>

static unsigned pll_calls, restore_calls;
static uint32_t pll_m, pll_n;
static bool pll_ready, hsi_ready;

bool clock_start_pll(uint32_t m, uint32_t n)
{
    ++pll_calls;
    pll_m = m;
    pll_n = n;
    return pll_ready;
}

bool clock_restore_hsi(void)
{
    ++restore_calls;
    return hsi_ready;
}

int main(void)
{
    assert(clock_select(0) == BSP_CLOCK_HSI);
    assert(clock_select(3000000) == BSP_CLOCK_ERROR);
    assert(clock_select(27000000) == BSP_CLOCK_ERROR);
    assert(clock_select(25000001) == BSP_CLOCK_ERROR);
    assert(pll_calls == 0 && restore_calls == 0);

    pll_ready = true;
    for (uint32_t mhz = 4; mhz <= 26; ++mhz) {
        assert(clock_select(mhz * BSP_MHZ) == BSP_CLOCK_HSE_PLL);
        assert(pll_m >= 2 && pll_m <= 63);
        assert(mhz / pll_m == (mhz % 2 == 0 ? 2u : 1u));
        const uint32_t vco = mhz / pll_m * pll_n;
        assert(vco / BSP_PLL_P == 84 && vco / BSP_PLL_Q == 48);
    }
    assert(pll_calls == 23 && restore_calls == 0);

    pll_ready = false;
    hsi_ready = true;
    assert(clock_select(8000000) == BSP_CLOCK_FALLBACK_HSI);
    hsi_ready = false;
    assert(clock_select(25000000) == BSP_CLOCK_ERROR);
    assert(restore_calls == 2);
    puts("clock profiles and simulated PLL start / HSI recovery passed");
}
