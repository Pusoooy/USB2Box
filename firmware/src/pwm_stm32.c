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
