#ifndef BOARD_H
#define BOARD_H
#include "stm32f4xx.h"
/* SDK1.1M.pdf pages 2/3: GLC=PD13, YARC=PD14, YCRA=PD15. */
#define BOARD_LED_PORT GPIOD
#define BOARD_LED_MASK ((uint16_t)((1U << 13) | (1U << 14) | (1U << 15)))
#define BOARD_GREEN ((uint16_t)(1U << 13))
#define BOARD_RED ((uint16_t)(1U << 15))
#define BOARD_YELLOW ((uint16_t)(1U << 14))
#define BOARD_BUTTON_PORT GPIOC
#define BOARD_BUTTON_PIN 15U
#define BOARD_DEBOUNCE_MS 30U
#define BOARD_UART_BAUD 57600U
#endif
