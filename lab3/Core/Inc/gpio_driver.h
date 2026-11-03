#ifndef GPIO_DRIVER_H
#define GPIO_DRIVER_H
#include "stm32f4xx.h"
#include <stdbool.h>

typedef enum { IO_INPUT, IO_OUTPUT, IO_ALTERNATE, IO_ANALOG } IoMode;
typedef enum { IO_NO_PULL, IO_PULL_UP, IO_PULL_DOWN } IoPull;
typedef struct {
    IoMode mode;
    IoPull pull;
    uint8_t speed;       /* 0..3, RM0090 OSPEEDR encoding */
    bool open_drain;
    uint8_t alternate;   /* 0..15 */
} IoConfig;
typedef enum { IO_OK, IO_INVALID, IO_LOCKED, IO_HANDOFF_REQUIRED } IoResult;

bool io_clock_enable(GPIO_TypeDef *port);
/* initial_bits contains physical levels, aligned to mask.
 * All validation precedes any write. Driven AF handoffs require the caller
 * to quiesce the peripheral and explicitly release the pin first.
 * Call from one cooperative context; this is not an ISR-safe RMW API. */
IoResult io_configure(GPIO_TypeDef *port, uint16_t mask,
                      const IoConfig *config, uint16_t initial_bits);
bool io_get_config(GPIO_TypeDef *port, unsigned pin, IoConfig *config);
uint16_t io_read(GPIO_TypeDef *port, uint16_t mask);
uint16_t io_read_latch(GPIO_TypeDef *port, uint16_t mask);
void io_write(GPIO_TypeDef *port, uint16_t mask, uint16_t bits);
bool io_read_pin(GPIO_TypeDef *port, unsigned pin);
void io_write_pin(GPIO_TypeDef *port, unsigned pin, bool high);
#endif
