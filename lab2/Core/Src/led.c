#include "led.h"
bool led_init(LedBank *led, GPIO_TypeDef *port, uint16_t mask, uint16_t off_bits)
{
    const IoConfig config = {IO_OUTPUT, IO_NO_PULL, 0, false, 0};
    if (!led || !io_clock_enable(port)) return false;
    if (io_configure(port, mask, &config, off_bits) != IO_OK) return false;
    *led = (LedBank){port, mask};
    return true;
}
void led_write(const LedBank *led, uint16_t bits)
{
    io_write(led->port, led->mask, bits);
}
