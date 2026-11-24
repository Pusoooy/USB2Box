#include "pwm_service.h"
#include "stm32f4xx_hal.h"

static TIM_TypeDef *const timers[PWM_GROUPS] = {TIM5, TIM3};
static GPIO_TypeDef *const ports[PWM_GROUPS] = {GPIOA, GPIOC};
static const IRQn_Type irq[PWM_GROUPS] = {TIM5_IRQn, TIM3_IRQn};
static const uint32_t pins[PWM_GROUPS][PWM_CHANNELS] = {
    {GPIO_PIN_0, GPIO_PIN_1}, {GPIO_PIN_6, GPIO_PIN_7},
};

static volatile uint32_t remaining[PWM_GROUPS][PWM_CHANNELS];
static volatile uint8_t active_channels[PWM_GROUPS], waiting_for_period[PWM_GROUPS];
static volatile bool pending_update[PWM_GROUPS];

uint32_t pwm_driver_clock(void)
{
    const uint32_t apb1 = HAL_RCC_GetPCLK1Freq();
    return apb1 == HAL_RCC_GetHCLKFreq() ? apb1 : apb1 * 2u;
}

static void set_compare(TIM_TypeDef *timer, unsigned channel, uint32_t value)
{
    if (channel == 0) timer->CCR1 = value;
    else timer->CCR2 = value;
}

static void set_mode(TIM_TypeDef *timer, unsigned channel, uint32_t mode)
{
    const unsigned shift = channel * 8u;
    MODIFY_REG(timer->CCMR1, (TIM_CCMR1_OC1M | TIM_CCMR1_OC1PE | TIM_CCMR1_CC1S) << shift,
               mode << shift);
}

static void enable_output(TIM_TypeDef *timer, unsigned channel, bool invert)
{
    const unsigned shift = channel * 4u;
    MODIFY_REG(timer->CCER, (TIM_CCER_CC1P | TIM_CCER_CC1E) << shift,
               (TIM_CCER_CC1E | (invert ? TIM_CCER_CC1P : 0u)) << shift);
}

void pwm_driver_stop(unsigned group, unsigned channel)
{
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    const uint8_t bit = (uint8_t)(1u << channel);
    if (active_channels[group] & bit) {
        TIM_TypeDef *timer = timers[group];
        CLEAR_BIT(timer->DIER, TIM_DIER_CC1IE << channel);
        waiting_for_period[group] &= (uint8_t)~bit;
        if (!waiting_for_period[group]) CLEAR_BIT(timer->DIER, TIM_DIER_UIE);
        /* Keep the electrical level low with the current polarity. */
        const bool invert = (timer->CCER & (TIM_CCER_CC1P << (channel * 4u))) != 0;
        set_mode(timer, channel, TIM_CCMR1_OC1M_2 | (invert ? TIM_CCMR1_OC1M_0 : 0u));
        active_channels[group] &= (uint8_t)~bit;
        if (!active_channels[group]) {
            timer->CR1 = 0;
            timer->DIER = 0;
            pending_update[group] = false;
        }
    }
    __set_PRIMASK(mask);
}

void pwm_driver_start(unsigned group, unsigned channel, const pwm_period *period, uint32_t pulses)
{
    if (group == 0) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_TIM5_CLK_ENABLE();
    } else {
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_TIM3_CLK_ENABLE();
    }
    GPIO_InitTypeDef pin = {
        .Pin = pins[group][channel], .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_HIGH, .Alternate = 2,
    };
    HAL_GPIO_WritePin(ports[group], (uint16_t)pin.Pin, GPIO_PIN_RESET);
    HAL_GPIO_Init(ports[group], &pin);

    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    TIM_TypeDef *timer = timers[group];
    const uint8_t bit = (uint8_t)(1u << channel);
    const bool joining = active_channels[group] != 0;
    const uint32_t pwm_mode = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
    remaining[group][channel] = pulses;
    active_channels[group] |= bit;

    set_mode(timer, channel, TIM_CCMR1_OC1M_2);
    enable_output(timer, channel, false);
    if (joining) {
        SET_BIT(timer->CR1, TIM_CR1_UDIS);
        /* Install an inactive compare value, then preload the first complete cycle. */
        set_compare(timer, channel, period->invert ? timer->ARR + 1u : 0u);
        set_mode(timer, channel, pwm_mode | TIM_CCMR1_OC1PE);
        set_compare(timer, channel, period->compare);
        if (pulses) {
            waiting_for_period[group] |= bit;
            timer->SR = ~TIM_SR_UIF;
            SET_BIT(timer->DIER, TIM_DIER_UIE);
        }
    } else {
        timer->CR1 = TIM_CR1_ARPE;
        timer->DIER = 0;
        timer->PSC = period->prescaler;
        timer->ARR = period->reload;
        set_mode(timer, channel, pwm_mode | TIM_CCMR1_OC1PE);
        set_compare(timer, channel, period->compare);
        timer->EGR = TIM_EGR_UG;
        timer->SR = 0;
        if (pulses) SET_BIT(timer->DIER, TIM_DIER_CC1IE << channel);
        HAL_NVIC_ClearPendingIRQ(irq[group]);
        HAL_NVIC_SetPriority(irq[group], 5, 0);
        HAL_NVIC_EnableIRQ(irq[group]);
    }
    enable_output(timer, channel, period->invert);
    pin.Mode = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(ports[group], &pin);
    if (joining) CLEAR_BIT(timer->CR1, TIM_CR1_UDIS);
    else SET_BIT(timer->CR1, TIM_CR1_CEN);
    __set_PRIMASK(mask);
}

bool pwm_driver_running(unsigned group, unsigned channel)
{
    return (active_channels[group] & (1u << channel)) != 0;
}

bool pwm_driver_update(unsigned group, const pwm_period periods[PWM_CHANNELS])
{
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    const uint8_t active = active_channels[group];
    if (active) {
        TIM_TypeDef *timer = timers[group];
        const pwm_period *clock = &periods[(active & 1u) ? 0 : 1];
        /* Transfer the shared timebase and both compare preloads in one update. */
        SET_BIT(timer->CR1, TIM_CR1_UDIS);
        timer->PSC = clock->prescaler;
        timer->ARR = clock->reload;
        for (unsigned channel = 0; channel < PWM_CHANNELS; ++channel)
            if (active & (1u << channel)) set_compare(timer, channel, periods[channel].compare);
        timer->SR = ~TIM_SR_UIF;
        pending_update[group] = true;
        CLEAR_BIT(timer->CR1, TIM_CR1_UDIS);
    }
    __set_PRIMASK(mask);
    return active != 0;
}

bool pwm_driver_update_pending(unsigned group)
{
    if (pending_update[group] && (timers[group]->SR & TIM_SR_UIF))
        pending_update[group] = false;
    return pending_update[group];
}

static void compare_irq(unsigned group)
{
    TIM_TypeDef *timer = timers[group];
    if ((timer->DIER & TIM_DIER_UIE) && (timer->SR & TIM_SR_UIF)) {
        timer->SR = ~TIM_SR_UIF;
        pending_update[group] = false;
        /* A joining output starts counting its finite pulses after its first reload. */
        for (unsigned channel = 0; channel < PWM_CHANNELS; ++channel)
            if (waiting_for_period[group] & (1u << channel)) {
                timer->SR = ~(TIM_SR_CC1IF << channel);
                SET_BIT(timer->DIER, TIM_DIER_CC1IE << channel);
            }
        waiting_for_period[group] = 0;
        CLEAR_BIT(timer->DIER, TIM_DIER_UIE);
    }
    for (unsigned channel = 0; channel < PWM_CHANNELS; ++channel) {
        const uint32_t flag = TIM_SR_CC1IF << channel;
        if ((timer->DIER & (TIM_DIER_CC1IE << channel)) && (timer->SR & flag)) {
            timer->SR = ~flag;
            if (remaining[group][channel] && --remaining[group][channel] == 0)
                pwm_driver_stop(group, channel);
        }
    }
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
