#include "pwm_service.h"
#include "stm32f4xx_hal.h"

static TIM_TypeDef *const timers[] = {TIM5, TIM3};
static GPIO_TypeDef *const ports[] = {GPIOA, GPIOC};
static const IRQn_Type irq[] = {TIM5_IRQn, TIM3_IRQn};
static volatile uint32_t remaining[2];
static volatile bool running[2];

uint32_t pwm_driver_clock(void)
{
    const uint32_t apb1 = HAL_RCC_GetPCLK1Freq();
    return apb1 == HAL_RCC_GetHCLKFreq() ? apb1 : apb1 * 2u;
}

void pwm_driver_stop(unsigned group)
{
    if (!running[group]) return;
    TIM_TypeDef *timer = timers[group];
    timer->DIER = 0;
    /* Keep the channel driving low without modifying shared GPIO in the IRQ. */
    timer->CCMR1 = TIM_CCMR1_OC1M_2; /* Force inactive */
    timer->CCER = TIM_CCER_CC1E;     /* Non-inverted low output */
    timer->CR1 = 0;
    running[group] = false;
}

void pwm_driver_start(unsigned group, const pwm_period *period, uint32_t pulses)
{
    if (group == 0) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_TIM5_CLK_ENABLE();
    } else {
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_TIM3_CLK_ENABLE();
    }
    TIM_TypeDef *timer = timers[group];
    timer->CR1 = 0;
    timer->DIER = 0;
    timer->CCER = 0;
    timer->PSC = period->prescaler;
    timer->ARR = period->reload;
    timer->CCR1 = period->compare;
    timer->CCMR1 = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1PE;
    timer->CR1 = TIM_CR1_ARPE;
    timer->EGR = TIM_EGR_UG;
    timer->SR = 0;
    timer->CCER = TIM_CCER_CC1E | (period->invert ? TIM_CCER_CC1P : 0);
    GPIO_InitTypeDef pin = {
        .Pin = group == 0 ? GPIO_PIN_0 : GPIO_PIN_6,
        .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_HIGH, .Alternate = 2,
    };
    HAL_GPIO_Init(ports[group], &pin);
    remaining[group] = pulses;
    running[group] = true;
    HAL_NVIC_ClearPendingIRQ(irq[group]);
    HAL_NVIC_SetPriority(irq[group], 5, 0);
    HAL_NVIC_EnableIRQ(irq[group]);
    if (pulses) timer->DIER = TIM_DIER_CC1IE;
    timer->CR1 |= TIM_CR1_CEN;
}

bool pwm_driver_running(unsigned group) { return running[group]; }

bool pwm_driver_update(unsigned group, const pwm_period *period)
{
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    const bool active = running[group];
    if (active) {
        TIM_TypeDef *timer = timers[group];
        /* Defer update events during the writes. PSC, ARR and CCR1
         * take effect together at the next natural counter overflow. */
        SET_BIT(timer->CR1, TIM_CR1_UDIS);
        timer->PSC = period->prescaler;
        timer->ARR = period->reload;
        timer->CCR1 = period->compare;
        timer->SR = ~TIM_SR_UIF;
        CLEAR_BIT(timer->CR1, TIM_CR1_UDIS);
    }
    __set_PRIMASK(mask);
    return active;
}

bool pwm_driver_update_pending(unsigned group)
{
    return !(timers[group]->SR & TIM_SR_UIF);
}

static void compare_irq(unsigned group)
{
    TIM_TypeDef *timer = timers[group];
    if (!(timer->SR & TIM_SR_CC1IF)) return;
    timer->SR = ~TIM_SR_CC1IF;
    if (running[group] && remaining[group] && --remaining[group] == 0)
        pwm_driver_stop(group);
}

void TIM5_IRQHandler(void) { compare_irq(0); }
void TIM3_IRQHandler(void) { compare_irq(1); }

static uint32_t capture_clock;
static bool capture_seen;

bool pwm_capture_start(uint32_t counter_hz)
{
    const uint32_t clock = pwm_driver_clock();
    if (counter_hz > clock) return false;
    const uint32_t divider = (clock + counter_hz - 1) / counter_hz;
    if (divider > 65536) return false;
    capture_clock = clock / divider;
    capture_seen = false;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();
    GPIO_InitTypeDef input = {
        .Pin = GPIO_PIN_6, .Mode = GPIO_MODE_AF_PP, .Alternate = GPIO_AF2_TIM4,
    };
    HAL_GPIO_Init(GPIOB, &input);
    TIM4->CR1 = 0;
    TIM4->DIER = 0;
    TIM4->CCER = 0;
    TIM4->PSC = divider - 1;
    TIM4->ARR = UINT16_MAX;
    TIM4->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_1; /* Direct and indirect TI1. */
    TIM4->SMCR = TIM_SMCR_TS_0 | TIM_SMCR_TS_2 | TIM_SMCR_SMS_2;
    TIM4->EGR = TIM_EGR_UG;
    TIM4->SR = 0;
    TIM4->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC2P;
    TIM4->CR1 = TIM_CR1_URS | TIM_CR1_CEN;
    return true;
}

bool pwm_capture_read(uint32_t *period, uint32_t *high, uint32_t *counter_hz)
{
    const uint32_t flags = TIM4->SR;
    if (!(flags & TIM_SR_CC1IF)) return false;
    const uint32_t measured_period = TIM4->CCR1;
    const uint32_t measured_high = TIM4->CCR2;
    TIM4->SR = ~(TIM_SR_UIF | TIM_SR_CC1OF | TIM_SR_CC2OF);
    const bool valid = capture_seen && !(flags & TIM_SR_UIF) &&
                       measured_period != 0 && measured_high <= measured_period;
    capture_seen = true;
    if (!valid) return false;
    *period = measured_period;
    *high = measured_high;
    *counter_hz = capture_clock;
    return true;
}

void pwm_capture_stop(void)
{
    TIM4->CR1 = 0;
    TIM4->CCER = 0;
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6);
}
