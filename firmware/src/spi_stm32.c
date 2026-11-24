#include "spi_service.h"
#include "stm32f4xx_hal.h"

static SPI_HandleTypeDef spi;
static DMA_HandleTypeDef receive, transmit;
static GPIO_TypeDef *cs_gpio;
static uint16_t cs_pin;
static bool active_high, wide;
static volatile uint16_t transfer_status;
static uint16_t transfer_bytes;
static volatile bool irq_transfer;
static const uint8_t *irq_tx;
static uint8_t *irq_rx;
static uint16_t irq_sent;
static volatile uint16_t irq_received;

enum { SPI_IRQ_MASK = SPI_CR2_ERRIE | SPI_CR2_RXNEIE | SPI_CR2_TXEIE };

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

bool spi_driver_enable_dma(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();
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
    if (HAL_DMA_Init(&receive) != HAL_OK || HAL_DMA_Init(&transmit) != HAL_OK) {
        HAL_DMA_DeInit(&receive);
        HAL_DMA_DeInit(&transmit);
        return false;
    }
    __HAL_LINKDMA(&spi, hdmarx, receive);
    __HAL_LINKDMA(&spi, hdmatx, transmit);
    set_irq(DMA2_Stream0_IRQn);
    set_irq(DMA2_Stream3_IRQn);
    return true;
}

void spi_driver_disable_dma(void)
{
    HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_DisableIRQ(DMA2_Stream3_IRQn);
    HAL_DMA_DeInit(&receive);
    HAL_DMA_DeInit(&transmit);
    spi.hdmarx = NULL;
    spi.hdmatx = NULL;
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
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    wide = config->bits == 16;
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
    if (HAL_SPI_Init(&spi) != HAL_OK) return 0;

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

    irq_transfer = false;
    set_irq(SPI1_IRQn);
    return clock / divisor;
}

void spi_driver_deinit(void)
{
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
    HAL_SPI_DeInit(&spi);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
}

static void read_irq_word(void)
{
    if (wide) wire_put16(irq_rx + irq_received, (uint16_t)SPI1->DR);
    else irq_rx[irq_received] = *(__IO uint8_t *)&SPI1->DR;
    __DMB();
    irq_received += wide ? 2u : 1u;
}

static void short_irq(void)
{
    const uint32_t flags = SPI1->SR;
    if (flags & (SPI_SR_MODF | SPI_SR_OVR | SPI_SR_FRE)) {
        CLEAR_BIT(SPI1->CR2, SPI_IRQ_MASK);
        transfer_status = STATUS_IO;
        return;
    }
    if ((flags & SPI_SR_RXNE) && irq_received < irq_sent) read_irq_word();
    if (irq_received == transfer_bytes) {
        CLEAR_BIT(SPI1->CR2, SPI_IRQ_MASK);
        return;
    }
    if (irq_received != irq_sent) return;
    if (!(flags & SPI_SR_TXE)) {
        SET_BIT(SPI1->CR2, SPI_CR2_TXEIE);
        return;
    }
    CLEAR_BIT(SPI1->CR2, SPI_CR2_TXEIE);
    if (wide) SPI1->DR = wire_u16(irq_tx + irq_sent);
    else *(__IO uint8_t *)&SPI1->DR = irq_tx[irq_sent];
    irq_sent += wide ? 2u : 1u;
}

bool spi_driver_start(const uint8_t *tx, uint8_t *rx, uint16_t bytes)
{
    USB2_ASSERT(bytes && bytes <= JOB_DATA && (!wide || bytes % 2u == 0));
    if (!bytes || bytes > JOB_DATA || (wide && bytes % 2u)) return false;
    transfer_bytes = bytes;
    transfer_status = JOB_PENDING;
    irq_transfer = bytes <= SPI_IRQ_MAX_BYTES;
    if (irq_transfer) {
        irq_tx = tx;
        irq_rx = rx;
        irq_sent = irq_received = 0;
        __HAL_SPI_ENABLE(&spi);
        __DMB();
        SET_BIT(SPI1->CR2, SPI_IRQ_MASK);
        return true;
    }
    if (!spi.hdmarx || !spi.hdmatx) return false;
    if (HAL_SPI_TransmitReceive_DMA(&spi, tx, rx, wide ? bytes / 2 : bytes) == HAL_OK) return true;
    spi_driver_abort();
    return false;
}

uint16_t spi_driver_result(void)
{
    if (irq_transfer && transfer_status == JOB_PENDING && irq_received == transfer_bytes &&
        !(SPI1->SR & SPI_SR_BSY)) {
        __DMB();
        return STATUS_OK;
    }
    return transfer_status;
}

static void reset_spi(void)
{
    __HAL_RCC_SPI1_FORCE_RESET();
    __HAL_RCC_SPI1_RELEASE_RESET();
    HAL_SPI_Init(&spi);
}

uint16_t spi_driver_abort(void)
{
    if (irq_transfer) {
        CLEAR_BIT(SPI1->CR2, SPI_IRQ_MASK);
        if ((SPI1->SR & SPI_SR_RXNE) && irq_received < irq_sent) read_irq_word();
        reset_spi();
        return irq_received;
    }
    if (HAL_SPI_Abort(&spi) != HAL_OK) reset_spi();
    const uint32_t remaining = __HAL_DMA_GET_COUNTER(&receive) * (wide ? 2u : 1u);
    __DMB();
    return remaining > transfer_bytes ? 0 : (uint16_t)(transfer_bytes - remaining);
}

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
void SPI1_IRQHandler(void)
{
    if (irq_transfer) {
        if (SPI1->CR2 & SPI_IRQ_MASK) short_irq();
    } else {
        HAL_SPI_IRQHandler(&spi);
    }
}
