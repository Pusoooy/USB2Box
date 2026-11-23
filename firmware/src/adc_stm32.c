#include "adc_service.h"
#include "stm32f4xx_hal.h"

static uint16_t dma_samples[2 * ADC_BLOCK];
static ADC_HandleTypeDef adc;
static DMA_HandleTypeDef adc_dma;
static volatile uint32_t ready, errors;
static uint32_t read_at;
static uint16_t finite;
static bool active;
static volatile bool threshold;

static const uint32_t sample_time[] = {
    ADC_SAMPLETIME_3CYCLES, ADC_SAMPLETIME_15CYCLES, ADC_SAMPLETIME_28CYCLES,
    ADC_SAMPLETIME_56CYCLES, ADC_SAMPLETIME_84CYCLES, ADC_SAMPLETIME_112CYCLES,
    ADC_SAMPLETIME_144CYCLES, ADC_SAMPLETIME_480CYCLES,
};
static const uint16_t sample_cycles[] = {3, 15, 28, 56, 84, 112, 144, 480};

static bool configure_adc(const adc_config *cfg, bool timer)
{
    const uint32_t resolutions[] = {ADC_RESOLUTION_6B, ADC_RESOLUTION_8B,
                                    ADC_RESOLUTION_10B, ADC_RESOLUTION_12B};
    adc = (ADC_HandleTypeDef){
        .Instance = ADC1,
        .Init = {
            .ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4,
            .Resolution = resolutions[(cfg->resolution - 6) / 2],
            .ScanConvMode = cfg->ranks > 1,
            .ExternalTrigConvEdge = timer ? ADC_EXTERNALTRIGCONVEDGE_RISING :
                                           ADC_EXTERNALTRIGCONVEDGE_NONE,
            .ExternalTrigConv = timer ? ADC_EXTERNALTRIGCONV_T2_TRGO : ADC_SOFTWARE_START,
            .DataAlign = ADC_DATAALIGN_RIGHT, .NbrOfConversion = cfg->ranks,
            .DMAContinuousRequests = ENABLE, .EOCSelection = ADC_EOC_SEQ_CONV,
        },
    };
    if (HAL_ADC_Init(&adc) != HAL_OK) return false;
    for (unsigned rank = 0; rank < cfg->ranks; ++rank) {
        const unsigned channel = cfg->channels[rank];
        ADC_ChannelConfTypeDef entry = {
            .Channel = channel == 16 ? ADC_CHANNEL_TEMPSENSOR : channel,
            .Rank = rank + 1, .SamplingTime = sample_time[cfg->times[rank]],
        };
        if (HAL_ADC_ConfigChannel(&adc, &entry) != HAL_OK) return false;
        if (channel < 16) {
            GPIO_TypeDef *port = channel < 8 ? GPIOA : channel < 10 ? GPIOB : GPIOC;
            const unsigned pin = channel < 8 ? channel : channel < 10 ? channel - 8 : channel - 10;
            GPIO_InitTypeDef mode = {.Pin = 1u << pin, .Mode = GPIO_MODE_ANALOG};
            HAL_GPIO_Init(port, &mode);
        }
    }
    if (!cfg->watchdog) return true;
    ADC_AnalogWDGConfTypeDef watchdog = {
        .WatchdogMode = ADC_ANALOGWATCHDOG_SINGLE_REG,
        .Channel = cfg->watchdog_channel == 16 ? ADC_CHANNEL_TEMPSENSOR : cfg->watchdog_channel,
        .ITMode = ENABLE, .HighThreshold = cfg->high, .LowThreshold = cfg->low,
    };
    return HAL_ADC_AnalogWDGConfig(&adc, &watchdog) == HAL_OK;
}

uint32_t adc_driver_start(const adc_config *cfg, uint32_t rate_hz, uint16_t samples)
{
    const uint32_t apb1 = HAL_RCC_GetPCLK1Freq();
    const uint32_t timer_clock = apb1 == HAL_RCC_GetHCLKFreq() ? apb1 : apb1 * 2u;
    const uint32_t ticks = rate_hz ? (timer_clock * cfg->ranks + rate_hz - 1u) / rate_hz : 0;
    const uint32_t actual = rate_hz ? (timer_clock * cfg->ranks) / ticks : 1;
    unsigned cycles = 0;
    for (unsigned i = 0; i < cfg->ranks; ++i)
        cycles += sample_cycles[cfg->times[i]] + cfg->resolution + 2;
    if ((uint64_t)cycles * actual > (uint64_t)(HAL_RCC_GetPCLK2Freq() / 4u) * cfg->ranks)
        return 0;

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_ADC_FORCE_RESET();
    __HAL_RCC_ADC_RELEASE_RESET();
    if (!configure_adc(cfg, rate_hz != 0)) return 0;

    finite = samples && samples <= ADC_BLOCK ? samples : 0;
    adc_dma = (DMA_HandleTypeDef){
        .Instance = DMA2_Stream4,
        .Init = {
            .Channel = DMA_CHANNEL_0, .Direction = DMA_PERIPH_TO_MEMORY,
            .MemInc = DMA_MINC_ENABLE, .Priority = DMA_PRIORITY_HIGH,
            .Mode = finite ? DMA_NORMAL : DMA_CIRCULAR,
            .PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD,
            .MemDataAlignment = DMA_MDATAALIGN_HALFWORD,
        },
    };
    __HAL_LINKDMA(&adc, DMA_Handle, adc_dma);
    if (HAL_DMA_Init(&adc_dma) != HAL_OK) return 0;
    ready = read_at = errors = 0;
    threshold = false;
    TIM2->CR1 = 0;
    TIM2->CR2 = TIM_TRGO_UPDATE;
    TIM2->PSC = 0;
    TIM2->ARR = ticks ? ticks - 1 : 0;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    HAL_NVIC_SetPriority(DMA2_Stream4_IRQn, 7, 0);
    HAL_NVIC_SetPriority(ADC_IRQn, 7, 0);
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream4_IRQn);
    HAL_NVIC_ClearPendingIRQ(ADC_IRQn);
    HAL_NVIC_EnableIRQ(DMA2_Stream4_IRQn);
    HAL_NVIC_EnableIRQ(ADC_IRQn);
    if (HAL_ADC_Start_DMA(&adc, (uint32_t *)dma_samples, finite ? finite : 2 * ADC_BLOCK) != HAL_OK) {
        HAL_NVIC_DisableIRQ(DMA2_Stream4_IRQn);
        HAL_NVIC_DisableIRQ(ADC_IRQn);
        HAL_DMA_DeInit(&adc_dma);
        HAL_ADC_DeInit(&adc);
        return 0;
    }
    active = true;
    if (rate_hz) TIM2->CR1 = TIM_CR1_CEN;
    return actual;
}

void adc_driver_stop(void)
{
    if (!active) return;
    TIM2->CR1 = 0;
    HAL_NVIC_DisableIRQ(DMA2_Stream4_IRQn);
    HAL_NVIC_DisableIRQ(ADC_IRQn);
    HAL_ADC_Stop_DMA(&adc);
    HAL_ADC_DeInit(&adc);
    HAL_DMA_DeInit(&adc_dma);
    active = false;
}

uint16_t adc_driver_read(uint16_t *samples, uint32_t *overflow)
{
    const uint32_t end = ready;
    const uint32_t pending = end - read_at;
    if (errors || (!finite && pending >= 2 * ADC_BLOCK)) {
        *overflow = pending ? pending : 1;
        return 0;
    }
    if (!pending) return 0;
    for (unsigned i = 0; i < pending; ++i) samples[i] = dma_samples[(read_at + i) % (2u * ADC_BLOCK)];
    if (!finite && ready - read_at >= 2 * ADC_BLOCK) {
        *overflow = ready - read_at;
        return 0;
    }
    read_at = end;
    return (uint16_t)pending;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *handle)
{
    if (handle == &adc && !finite) ready += ADC_BLOCK;
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *handle)
{
    if (handle == &adc) ready += finite ? finite : ADC_BLOCK;
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *handle)
{
    if (handle == &adc) ++errors;
}
bool adc_driver_triggered(void) { return threshold; }

void HAL_ADC_LevelOutOfWindowCallback(ADC_HandleTypeDef *handle)
{
    if (handle == &adc) {
        threshold = true;
        __HAL_ADC_DISABLE_IT(&adc, ADC_IT_AWD);
    }
}

void DMA2_Stream4_IRQHandler(void) { HAL_DMA_IRQHandler(&adc_dma); }
void ADC_IRQHandler(void) { HAL_ADC_IRQHandler(&adc); }
