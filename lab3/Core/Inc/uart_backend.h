#ifndef UART_BACKEND_H
#define UART_BACKEND_H
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx.h"
bool uart_backend_init(void);
bool uart_backend_mode(bool interrupts);
/* -1: corrupt input discarded; 0: no byte; 1: one byte. */
int uart_backend_receive(uint8_t *byte);
bool uart_backend_transmit(uint8_t *byte, bool interrupts);
bool uart_backend_idle(void);
bool uart_pins_init(void);
void serial_received(uint8_t byte);
void serial_receive_error(void);
void serial_transmitted(void);
void serial_transmit_error(void);
#endif
