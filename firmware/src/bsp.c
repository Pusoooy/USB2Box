#include "bsp.h"
#include "clock_policy.h"
#include "stm32f4xx_hal.h"

_Static_assert(HSE_VALUE == 0 || BSP_HSE_SUPPORTED(HSE_VALUE),
               "HSE_HZ must be 0 or a supported 4-26 MHz crystal frequency");
_Static_assert(OTG_FS_IRQn == 67, "Check the OTG FS slot in startup.S");

enum { USB_IRQ_PRIORITY = 6 };
static volatile bsp_clock_status clock_status;
static volatile bool hse_failed;

static HAL_StatusTypeDef select_bus_clocks(uint32_t source, uint32_t apb1_div)
{
    RCC_ClkInitTypeDef bus = {
        .ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2,
        .SYSCLKSource = source,
        .AHBCLKDivider = RCC_SYSCLK_DIV1,
        .APB1CLKDivider = apb1_div,
        .APB2CLKDivider = RCC_HCLK_DIV1,
    };
    /* Conservative latency for both the 84 MHz transition and HSI fallback. */
    return HAL_RCC_ClockConfig(&bus, FLASH_LATENCY_5);
}

bool clock_start_pll(uint32_t pll_m, uint32_t pll_n)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitTypeDef oscillator = {
        .OscillatorType = RCC_OSCILLATORTYPE_HSE,
        .HSEState = RCC_HSE_ON,
        .PLL = {
            .PLLState = RCC_PLL_ON,
            .PLLSource = RCC_PLLSOURCE_HSE,
            .PLLM = pll_m,
            .PLLN = pll_n,
            .PLLP = RCC_PLLP_DIV4,
            .PLLQ = BSP_PLL_Q,
        },
    };
    return HAL_RCC_OscConfig(&oscillator) == HAL_OK &&
           select_bus_clocks(RCC_SYSCLKSOURCE_PLLCLK, RCC_HCLK_DIV2) == HAL_OK &&
           SystemCoreClock == 84u * BSP_MHZ;
}

bool clock_restore_hsi(void)
{
    if (select_bus_clocks(RCC_SYSCLKSOURCE_HSI, RCC_HCLK_DIV1) != HAL_OK) {
        return false;
    }
    RCC_OscInitTypeDef oscillator = {
        .OscillatorType = RCC_OSCILLATORTYPE_HSE,
        .HSEState = RCC_HSE_OFF,
        .PLL.PLLState = RCC_PLL_NONE,
    };
    /* HAL can disable HSE before PLL. Turn PLL off first so fallback is safe. */
    RCC_OscInitTypeDef pll_off = { .PLL.PLLState = RCC_PLL_OFF };
    return HAL_RCC_OscConfig(&pll_off) == HAL_OK &&
           HAL_RCC_OscConfig(&oscillator) == HAL_OK;
}

bsp_clock_status bsp_init(void)
{
    HAL_Init();
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    HAL_NVIC_SetPriority(OTG_FS_IRQn, USB_IRQ_PRIORITY, 0);

    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef usb_pins = {
        .Pin = GPIO_PIN_11 | GPIO_PIN_12,
        .Mode = GPIO_MODE_ANALOG,
        .Pull = GPIO_NOPULL,
    };
    HAL_GPIO_Init(GPIOA, &usb_pins);

    clock_status = clock_select(HSE_VALUE);
    if (clock_status == BSP_CLOCK_HSE_PLL) {
        __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
        HAL_RCC_EnableCSS();
    }
    return clock_status;
}

bool bsp_usb_enable_pins(void)
{
    if (clock_status != BSP_CLOCK_HSE_PLL) {
        return false;
    }
    GPIO_InitTypeDef usb_pins = {
        .Pin = GPIO_PIN_11 | GPIO_PIN_12,
        .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = GPIO_AF10_OTG_FS,
    };
    HAL_GPIO_Init(GPIOA, &usb_pins);
    return true;
}

uint32_t bsp_millis(void)
{
    return HAL_GetTick();
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void NMI_Handler(void)
{
    HAL_RCC_NMI_IRQHandler();
}

void HAL_RCC_CSSCallback(void)
{
    hse_failed = true;
}

bool bsp_handle_clock_failure(void)
{
    if (!hse_failed) return false;
    hse_failed = false;
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    clock_status = BSP_CLOCK_FALLBACK_HSI;
    SystemCoreClockUpdate();
    HAL_InitTick(TICK_INT_PRIORITY);
    return true;
}
