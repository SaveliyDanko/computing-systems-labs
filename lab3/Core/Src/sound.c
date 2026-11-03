#include "sound.h"
#include "board.h"
#include "gpio_driver.h"

static uint32_t current_frequency;

uint32_t sound_period_ticks(uint32_t frequency_millihz)
{
    /* Covers all seven natural notes in all nine octaves, with a small margin. */
    if (frequency_millihz < 16000U || frequency_millihz > 8000000U) return 0;
    return (uint32_t)(((uint64_t)BOARD_TIMER_HZ * 1000U + frequency_millihz / 2U) /
                      frequency_millihz);
}

bool sound_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    RCC->APB1RSTR |= RCC_APB1RSTR_TIM2RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_TIM2RST;
    TIM2->CR1 = TIM_CR1_ARPE;
    TIM2->PSC = 0;
    TIM2->ARR = 1;
    TIM2->CCR1 = 0;
    TIM2->CCMR1 = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1PE;
    TIM2->CCER = TIM_CCER_CC1E;
    TIM2->DIER = 0; /* Waveform generation is entirely in hardware. */
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->CNT = 0;
    current_frequency = 0;
    const IoConfig pin = {IO_ALTERNATE, IO_NO_PULL, 2, false, BOARD_SOUND_AF};
    return io_clock_enable(BOARD_SOUND_PORT) &&
           io_configure(BOARD_SOUND_PORT, (uint16_t)(1U << BOARD_SOUND_PIN), &pin, 0) == IO_OK;
}

void sound_stop(void)
{
    /* Keep CH1 enabled in PWM1 with CCR=0: PA5 is actively held low. */
    TIM2->CCR1 = 0;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 &= ~TIM_CR1_CEN;
    TIM2->CNT = 0;
    TIM2->SR = 0;
    current_frequency = 0;
}

bool sound_set_frequency(uint32_t frequency_millihz)
{
    if (!frequency_millihz) {
        if (current_frequency) sound_stop();
        return true;
    }
    uint32_t ticks = sound_period_ticks(frequency_millihz);
    if (!ticks) return false;
    if (frequency_millihz == current_frequency) return true;
    sound_stop();
    TIM2->ARR = ticks - 1U;
    TIM2->CCR1 = ticks / 2U;
    TIM2->EGR = TIM_EGR_UG; /* Load both preload registers before starting. */
    TIM2->SR = 0;
    TIM2->CNT = 0;
    TIM2->CR1 |= TIM_CR1_CEN;
    current_frequency = frequency_millihz;
    return true;
}
