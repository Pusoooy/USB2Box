#include "uart_service.h"
#include "stm32f4xx_hal.h"

typedef struct {
    UART_HandleTypeDef handle;
    DMA_HandleTypeDef rx, tx;
    volatile uint32_t wraps, errors;
} uart_hardware;

static uart_hardware hardware[2];
static const IRQn_Type rx_irq[] = {DMA2_Stream2_IRQn, DMA1_Stream5_IRQn};
static const IRQn_Type tx_irq[] = {DMA2_Stream7_IRQn, DMA1_Stream6_IRQn};
static const IRQn_Type uart_irq[] = {USART1_IRQn, USART2_IRQn};

static void irq_configure(IRQn_Type irq)
{
    HAL_NVIC_ClearPendingIRQ(irq);
    HAL_NVIC_SetPriority(irq, 7, 0);
    HAL_NVIC_EnableIRQ(irq);
}

bool uart_driver_configure(unsigned port, uint32_t baud, uint8_t parity, uint8_t stop_bits)
{
    uart_hardware *hw = &hardware[port];
    *hw = (uart_hardware){0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    if (port == 0) {
        __HAL_RCC_USART1_CLK_ENABLE();
        hw->handle.Instance = USART1;
        hw->rx.Instance = DMA2_Stream2;
        hw->tx.Instance = DMA2_Stream7;
    } else {
        __HAL_RCC_USART2_CLK_ENABLE();
        hw->handle.Instance = USART2;
        hw->rx.Instance = DMA1_Stream5;
        hw->tx.Instance = DMA1_Stream6;
    }
    hw->rx.Init = (DMA_InitTypeDef){
        .Channel = DMA_CHANNEL_4, .Direction = DMA_PERIPH_TO_MEMORY,
        .MemInc = DMA_MINC_ENABLE, .Mode = DMA_CIRCULAR, .Priority = DMA_PRIORITY_HIGH,
    };
    hw->tx.Init = hw->rx.Init;
    hw->tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hw->tx.Init.Mode = DMA_NORMAL;
    hw->tx.Init.Priority = DMA_PRIORITY_MEDIUM;
    __HAL_LINKDMA(&hw->handle, hdmarx, hw->rx);
    __HAL_LINKDMA(&hw->handle, hdmatx, hw->tx);
    if (HAL_DMA_Init(&hw->rx) != HAL_OK || HAL_DMA_Init(&hw->tx) != HAL_OK)
        return false;

    hw->handle.Init = (UART_InitTypeDef){
        .BaudRate = baud, .WordLength = parity ? UART_WORDLENGTH_9B : UART_WORDLENGTH_8B,
        .StopBits = stop_bits == 2 ? UART_STOPBITS_2 : UART_STOPBITS_1,
        .Parity = parity == 1 ? UART_PARITY_EVEN : parity == 2 ? UART_PARITY_ODD : UART_PARITY_NONE,
        .Mode = UART_MODE_TX_RX, .OverSampling = UART_OVERSAMPLING_16,
    };
    if (HAL_UART_Init(&hw->handle) != HAL_OK) return false;
    GPIO_InitTypeDef gpio = {
        .Pin = port == 0 ? GPIO_PIN_6 | GPIO_PIN_7 : GPIO_PIN_2 | GPIO_PIN_3,
        .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP, .Speed = GPIO_SPEED_FREQ_HIGH,
        .Alternate = port == 0 ? GPIO_AF7_USART1 : GPIO_AF7_USART2,
    };
    HAL_GPIO_Init(port == 0 ? GPIOB : GPIOA, &gpio);
    irq_configure(rx_irq[port]);
    irq_configure(tx_irq[port]);
    irq_configure(uart_irq[port]);
    return true;
}

bool uart_driver_start(unsigned port, uint8_t *buffer, uint16_t length)
{
    uart_hardware *hw = &hardware[port];
    return HAL_UART_Receive_DMA(&hw->handle, buffer, length) == HAL_OK;
}

void uart_driver_stop(unsigned port)
{
    uart_hardware *hw = &hardware[port];
    HAL_NVIC_DisableIRQ(rx_irq[port]);
    HAL_NVIC_DisableIRQ(tx_irq[port]);
    HAL_NVIC_DisableIRQ(uart_irq[port]);
    HAL_UART_DMAStop(&hw->handle);
    HAL_UART_DeInit(&hw->handle);
    HAL_DMA_DeInit(&hw->rx);
    HAL_DMA_DeInit(&hw->tx);
    HAL_GPIO_DeInit(port == 0 ? GPIOB : GPIOA,
                    port == 0 ? GPIO_PIN_6 | GPIO_PIN_7 : GPIO_PIN_2 | GPIO_PIN_3);
}

uint32_t uart_driver_rx_count(unsigned port)
{
    uart_hardware *hw = &hardware[port];
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t remaining = __HAL_DMA_GET_COUNTER(&hw->rx);
    uint32_t count = hw->wraps;
    if (__HAL_DMA_GET_FLAG(&hw->rx, __HAL_DMA_GET_TC_FLAG_INDEX(&hw->rx))) {
        count += UART_RX_SIZE;
        remaining = __HAL_DMA_GET_COUNTER(&hw->rx);
        if (remaining == 0) remaining = UART_RX_SIZE;
    }
    __set_PRIMASK(primask);
    return count + UART_RX_SIZE - remaining;
}

uint32_t uart_driver_errors(unsigned port) { return hardware[port].errors; }
bool uart_driver_tx_idle(unsigned port) { return hardware[port].handle.gState == HAL_UART_STATE_READY; }
bool uart_driver_send(unsigned port, uint8_t *bytes, uint16_t len)
{
    return HAL_UART_Transmit_DMA(&hardware[port].handle, bytes, len) == HAL_OK;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    hardware[handle->Instance == USART1 ? 0 : 1].wraps += UART_RX_SIZE;
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    hardware[handle->Instance == USART1 ? 0 : 1].errors++;
}
void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&hardware[0].handle); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&hardware[1].handle); }
void DMA2_Stream2_IRQHandler(void) { HAL_DMA_IRQHandler(&hardware[0].rx); }
void DMA2_Stream7_IRQHandler(void) { HAL_DMA_IRQHandler(&hardware[0].tx); }
void DMA1_Stream5_IRQHandler(void) { HAL_DMA_IRQHandler(&hardware[1].rx); }
void DMA1_Stream6_IRQHandler(void) { HAL_DMA_IRQHandler(&hardware[1].tx); }
