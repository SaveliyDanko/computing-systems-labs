#ifndef BOARD_H
#define BOARD_H
#include "stm32f4xx.h"
/* SDK1.1M.pdf: PA5 -> ADC/TIM.CH1 -> SA2.7 -> amplifier -> BA1. */
#define BOARD_SOUND_PORT GPIOA
#define BOARD_SOUND_PIN 5U
#define BOARD_SOUND_AF 1U
#define BOARD_SYSCLK_HZ 120000000U
#define BOARD_TIMER_HZ 60000000U
#define BOARD_UART_BAUD 57600U
#endif
