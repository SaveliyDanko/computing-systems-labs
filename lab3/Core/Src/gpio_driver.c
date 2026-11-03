#include "gpio_driver.h"

static int port_index(GPIO_TypeDef *p)
{
    GPIO_TypeDef *const ports[] = {GPIOA, GPIOB, GPIOC, GPIOD, GPIOE,
                                   GPIOF, GPIOG, GPIOH, GPIOI, GPIOJ, GPIOK};
    for (unsigned i = 0; i < sizeof ports / sizeof ports[0]; ++i)
        if (ports[i] == p) return (int)i;
    return -1;
}

bool io_clock_enable(GPIO_TypeDef *p)
{
    int i = port_index(p);
    if (i < 0) return false;
    RCC->AHB1ENR |= 1UL << (unsigned)i;
    (void)RCC->AHB1ENR; /* peripheral clock enable propagation */
    return true;
}

void io_write(GPIO_TypeDef *p, uint16_t mask, uint16_t bits)
{
    p->BSRR = ((uint32_t)(mask & (uint16_t)~bits) << 16) | (bits & mask);
}
uint16_t io_read(GPIO_TypeDef *p, uint16_t mask) { return (uint16_t)p->IDR & mask; }
uint16_t io_read_latch(GPIO_TypeDef *p, uint16_t mask) { return (uint16_t)p->ODR & mask; }
bool io_read_pin(GPIO_TypeDef *p, unsigned pin)
{
    return pin < 16 && io_read(p, (uint16_t)(1U << pin)) != 0;
}
void io_write_pin(GPIO_TypeDef *p, unsigned pin, bool high)
{
    if (pin < 16) io_write(p, (uint16_t)(1U << pin), high ? (uint16_t)(1U << pin) : 0);
}
bool io_get_config(GPIO_TypeDef *p, unsigned pin, IoConfig *c)
{
    if (port_index(p) < 0 || pin > 15 || !c) return false;
    c->mode = (IoMode)((p->MODER >> (pin * 2)) & 3U);
    c->pull = (IoPull)((p->PUPDR >> (pin * 2)) & 3U);
    c->speed = (uint8_t)((p->OSPEEDR >> (pin * 2)) & 3U);
    c->open_drain = ((p->OTYPER >> pin) & 1U) != 0;
    c->alternate = (uint8_t)((p->AFR[pin / 8] >> ((pin % 8) * 4)) & 15U);
    return true;
}
IoResult io_configure(GPIO_TypeDef *p, uint16_t mask, const IoConfig *c, uint16_t initial)
{
    if (port_index(p) < 0 || !mask || !c || (unsigned)c->mode > 3 ||
        (unsigned)c->pull > 2 || c->speed > 3 || c->alternate > 15) return IO_INVALID;
    if ((p->LCKR & (1UL << 16)) && (p->LCKR & mask)) return IO_LOCKED;
    uint32_t field_mask = 0, modes = 0, pulls = 0, speeds = 0;
    uint32_t af_mask[2] = {0, 0}, af_bits[2] = {0, 0};
    uint32_t release_mask = 0;
    for (unsigned pin = 0; pin < 16; ++pin) {
        if (!(mask & (1U << pin))) continue;
        IoConfig old;
        (void)io_get_config(p, pin, &old);
        /* AF output is controlled by a peripheral, not ODR. A generic GPIO
         * driver cannot promise a glitch-free takeover of an active AF. */
        if (old.mode == IO_ALTERNATE && c->mode != IO_INPUT && c->mode != IO_ANALOG &&
            (c->mode != IO_ALTERNATE || old.alternate != c->alternate ||
             old.speed != c->speed || old.pull != c->pull || old.open_drain != c->open_drain))
            return IO_HANDOFF_REQUIRED;
        if (old.mode == IO_OUTPUT && c->mode == IO_ALTERNATE)
            return IO_HANDOFF_REQUIRED;
        unsigned shift = pin * 2;
        field_mask |= 3UL << shift;
        modes |= (uint32_t)c->mode << shift;
        pulls |= (uint32_t)c->pull << shift;
        speeds |= (uint32_t)c->speed << shift;
        af_mask[pin / 8] |= 15UL << ((pin % 8) * 4);
        af_bits[pin / 8] |= (uint32_t)c->alternate << ((pin % 8) * 4);
        if (c->mode == IO_INPUT || c->mode == IO_ANALOG) release_mask |= 3UL << shift;
    }
    /* Release outputs BEFORE changing their electrical parameters on exit.
     * Output->output never goes through input/high impedance. */
    if (release_mask) p->MODER = (p->MODER & ~release_mask) | (modes & release_mask);
    /* When changing a driven open-drain output to push-pull, first enable
     * push-pull at the old latch level. Do not first release an OD low output
     * by loading a high latch, which could leave a floating interval. */
    if (c->mode == IO_OUTPUT && !c->open_drain) {
        uint32_t driven = 0;
        for (unsigned pin = 0; pin < 16; ++pin)
            if ((mask & (1U << pin)) && ((p->MODER >> (pin * 2)) & 3U) == IO_OUTPUT)
                driven |= 1UL << pin;
        if (driven) p->OTYPER &= ~driven;
    }
    /* Preload latch BEFORE enabling output; update multiple signals atomically. */
    if (c->mode == IO_OUTPUT) io_write(p, mask, initial);
    p->OSPEEDR = (p->OSPEEDR & ~field_mask) | speeds;
    p->PUPDR = (p->PUPDR & ~field_mask) | pulls;
    p->OTYPER = (p->OTYPER & ~(uint32_t)mask) | (c->open_drain ? mask : 0);
    for (unsigned i = 0; i < 2; ++i)
        if (af_mask[i]) p->AFR[i] = (p->AFR[i] & ~af_mask[i]) | af_bits[i];
    p->MODER = (p->MODER & ~field_mask) | modes;
    return IO_OK;
}
