#ifndef LED_H
#define LED_H
#include "gpio_driver.h"
/* Universal same-port indicator bank. The application defines color patterns;
 * this driver knows only pin masks and physical output levels. */
typedef struct { GPIO_TypeDef *port; uint16_t mask; } LedBank;
bool led_init(LedBank *led, GPIO_TypeDef *port, uint16_t mask, uint16_t off_bits);
void led_write(const LedBank *led, uint16_t physical_bits);
#endif
