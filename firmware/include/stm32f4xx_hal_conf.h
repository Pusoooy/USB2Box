#ifndef USB2BOX_HAL_CONF_H
#define USB2BOX_HAL_CONF_H

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED

#define HSI_VALUE 16000000u
#define LSE_STARTUP_TIMEOUT 5000u
#define HSE_STARTUP_TIMEOUT 100u
#define TICK_INT_PRIORITY 15u
#define INSTRUCTION_CACHE_ENABLE 1u
#define DATA_CACHE_ENABLE 1u
#define PREFETCH_ENABLE 0u
#define USE_RTOS 0u

#include "stm32f4xx_hal_rcc.h"
#include "stm32f4xx_hal_cortex.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_uart.h"
#include "stm32f4xx_hal_i2c.h"
#include "stm32f4xx_hal_spi.h"
#include "stm32f4xx_hal_pwr.h"

#define assert_param(expr) ((void)0u)

#endif
