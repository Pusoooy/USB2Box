#include "spi_service.h"
#include "stm32f4xx_hal.h"

static SPI_HandleTypeDef spi;
static DMA_HandleTypeDef receive, transmit;
static GPIO_TypeDef *cs_gpio;
static uint16_t cs_pin;
static bool active_high, wide;
static volatile uint16_t transfer_status;
static uint16_t transfer_bytes;

void spi_driver_select(bool active)
{
    HAL_GPIO_WritePin(cs_gpio, cs_pin, active == active_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void set_irq(IRQn_Type irq)
{
    HAL_NVIC_ClearPendingIRQ(irq);
    HAL_NVIC_SetPriority(irq, 8, 0);
    HAL_NVIC_EnableIRQ(irq);
}

uint32_t spi_driver_init(const spi_config *config)
{
    unsigned divisor = 2;
    uint32_t baud = SPI_BAUDRATEPRESCALER_2;
    const uint32_t clock = HAL_RCC_GetPCLK2Freq();
    while (clock / divisor > config->hz && divisor < 256) {
        divisor *= 2;
        baud += SPI_CR1_BR_0;
    }
    if (clock / divisor > config->hz) return 0;

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    wide = config->bits == 16;
    receive = (DMA_HandleTypeDef){
        .Instance = DMA2_Stream0,
        .Init = {
            .Channel = DMA_CHANNEL_3, .Direction = DMA_PERIPH_TO_MEMORY,
            .MemInc = DMA_MINC_ENABLE, .Priority = DMA_PRIORITY_MEDIUM,
            .PeriphDataAlignment = wide ? DMA_PDATAALIGN_HALFWORD : DMA_PDATAALIGN_BYTE,
            .MemDataAlignment = wide ? DMA_MDATAALIGN_HALFWORD : DMA_MDATAALIGN_BYTE,
        },
    };
    transmit = receive;
    transmit.Instance = DMA2_Stream3;
    transmit.Init.Direction = DMA_MEMORY_TO_PERIPH;
    spi = (SPI_HandleTypeDef){
        .Instance = SPI1,
        .Init = {
            .Mode = SPI_MODE_MASTER, .Direction = SPI_DIRECTION_2LINES,
            .DataSize = wide ? SPI_DATASIZE_16BIT : SPI_DATASIZE_8BIT,
            .CLKPolarity = config->mode & 2 ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW,
            .CLKPhase = config->mode & 1 ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE,
            .NSS = SPI_NSS_SOFT, .BaudRatePrescaler = baud,
            .FirstBit = config->lsb ? SPI_FIRSTBIT_LSB : SPI_FIRSTBIT_MSB,
        },
    };
    __HAL_LINKDMA(&spi, hdmarx, receive);
    __HAL_LINKDMA(&spi, hdmatx, transmit);
    if (HAL_DMA_Init(&receive) != HAL_OK || HAL_DMA_Init(&transmit) != HAL_OK ||
        HAL_SPI_Init(&spi) != HAL_OK) return 0;

    GPIO_InitTypeDef pins = {
        .Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7, .Mode = GPIO_MODE_AF_PP,
        .Speed = GPIO_SPEED_FREQ_VERY_HIGH, .Alternate = GPIO_AF5_SPI1,
    };
    HAL_GPIO_Init(GPIOA, &pins);
    cs_gpio = config->cs ? GPIOC : GPIOB;
    cs_pin = config->cs ? (uint16_t)(1u << (config->cs - 1)) : GPIO_PIN_12;
    active_high = config->active_high != 0;
    spi_driver_select(false);
    pins = (GPIO_InitTypeDef){.Pin = cs_pin, .Mode = GPIO_MODE_OUTPUT_PP};
    HAL_GPIO_Init(cs_gpio, &pins);

    set_irq(DMA2_Stream0_IRQn);
    set_irq(DMA2_Stream3_IRQn);
    set_irq(SPI1_IRQn);
    return clock / divisor;
}

void spi_driver_deinit(void)
{
    HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_DisableIRQ(DMA2_Stream3_IRQn);
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
    HAL_SPI_DeInit(&spi);
    HAL_DMA_DeInit(&receive);
    HAL_DMA_DeInit(&transmit);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
}

bool spi_driver_start(uint8_t *tx, uint8_t *rx, uint16_t bytes)
{
    transfer_bytes = bytes;
    transfer_status = JOB_PENDING;
    return HAL_SPI_TransmitReceive_DMA(&spi, tx, rx, wide ? bytes / 2 : bytes) == HAL_OK;
}
uint16_t spi_driver_result(void) { return transfer_status; }

uint16_t spi_driver_received(void)
{
    const uint32_t remaining = __HAL_DMA_GET_COUNTER(&receive) * (wide ? 2u : 1u);
    __DMB();
    return remaining > transfer_bytes ? 0 : (uint16_t)(transfer_bytes - remaining);
}

void spi_driver_abort(void) { HAL_SPI_Abort(&spi); }

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *handle)
{
    if (handle == &spi) transfer_status = STATUS_OK;
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *handle)
{
    if (handle == &spi) transfer_status = STATUS_IO;
}
void DMA2_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&receive); }
void DMA2_Stream3_IRQHandler(void) { HAL_DMA_IRQHandler(&transmit); }
void SPI1_IRQHandler(void) { HAL_SPI_IRQHandler(&spi); }
