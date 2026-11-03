#include "gpio_driver.h"
#include "uart_backend.h"
bool uart_pins_init(void)
{
    const IoConfig pins = {IO_ALTERNATE, IO_PULL_UP, 2, false, 7};
    if (!io_clock_enable(GPIOA) ||
        io_configure(GPIOA, (uint16_t)((1U << 9) | (1U << 10)), &pins, 0) != IO_OK)
        return false;
    /* PC6/PC7 share these board nets: keep USART6 disconnected. */
    const IoConfig released = {IO_INPUT, IO_NO_PULL, 0, false, 0};
    return io_clock_enable(GPIOC) &&
        io_configure(GPIOC, (uint16_t)((1U << 6) | (1U << 7)), &released, 0) == IO_OK;
}
