#include "button.h"
#include "traffic.h"
#include "gpio_driver.h"
#include "led.h"
#include "board.h"

volatile uint32_t test_done, test_failure, test_checks;

volatile uint32_t guard_high, guard_driven;
#define CHECK(x) do { ++test_checks; if (!(x)) {test_failure = __LINE__; test_done = 1; for (;;) {}} } while (0)

static void debounce_tests(void)
{
    Button b;
    button_init(&b, false, 0, 30);
    CHECK(!button_update(&b, true, 1));
    CHECK(!button_update(&b, false, 4));
    CHECK(!button_update(&b, true, 8));
    CHECK(!button_update(&b, true, 37));
    CHECK(button_update(&b, true, 38));
    CHECK(b.pressed);
    CHECK(!button_update(&b, true, 10000));
    CHECK(!button_update(&b, false, 10001));
    CHECK(!button_update(&b, true, 10004));
    CHECK(!button_update(&b, false, 10008));
    CHECK(!button_update(&b, false, 10038));
    CHECK(!b.pressed);
    CHECK(!button_update(&b, true, 10040));
    CHECK(button_update(&b, true, 10070));
    button_init(&b, false, UINT32_MAX - 20, 30);
    CHECK(!button_update(&b, true, UINT32_MAX - 10));
    CHECK(!button_update(&b, true, 18));
    CHECK(button_update(&b, true, 19));
    button_init(&b, true, 0, 30);
    CHECK(!button_update(&b, true, 1000));
}

static void traffic_tests(void)
{
    Traffic t;
    traffic_init(&t, 0);
    CHECK(traffic_light(&t, 0) == LIGHT_RED);
    traffic_update(&t, false, 11999); CHECK(t.state == TRAFFIC_RED);
    traffic_update(&t, false, 12000); CHECK(t.state == TRAFFIC_GREEN);
    traffic_update(&t, true, 13000); CHECK(!t.request);
    traffic_update(&t, false, 15000); CHECK(t.state == TRAFFIC_BLINK);
    CHECK(traffic_light(&t, 15000) == LIGHT_GREEN);
    CHECK(traffic_light(&t, 15499) == LIGHT_GREEN);
    CHECK(traffic_light(&t, 15500) == LIGHT_OFF);
    CHECK(traffic_light(&t, 16000) == LIGHT_GREEN);
    traffic_update(&t, false, 18000); CHECK(t.state == TRAFFIC_YELLOW);
    traffic_update(&t, false, 19000); CHECK(t.state == TRAFFIC_RED);
    traffic_update(&t, false, 30999); CHECK(t.state == TRAFFIC_RED);
    traffic_update(&t, false, 31000); CHECK(t.state == TRAFFIC_GREEN);

    traffic_init(&t, 0);
    traffic_update(&t, true, 1000); CHECK(t.request);
    traffic_update(&t, true, 2000);
    traffic_update(&t, false, 2999); CHECK(t.state == TRAFFIC_RED);
    traffic_update(&t, false, 3000); CHECK(t.state == TRAFFIC_GREEN && !t.request);
    traffic_update(&t, false, 6000);
    traffic_update(&t, false, 9000);
    traffic_update(&t, false, 10000);
    traffic_update(&t, false, 13000); CHECK(t.state == TRAFFIC_RED && !t.request);
    traffic_update(&t, false, 22000); CHECK(t.state == TRAFFIC_GREEN);
    traffic_init(&t, 0);
    traffic_update(&t, true, 3000); CHECK(t.state == TRAFFIC_GREEN);
    traffic_init(&t, 0);
    traffic_update(&t, true, 8000); CHECK(t.state == TRAFFIC_GREEN && !t.request);

    for (unsigned phase = 0; phase < 2; ++phase) {
        traffic_init(&t, 0);
        traffic_update(&t, false, 12000);
        traffic_update(&t, false, 15000);
        traffic_update(&t, phase == 0, 16000); CHECK(t.state == TRAFFIC_BLINK);
        traffic_update(&t, false, 18000);
        traffic_update(&t, phase == 1, 18500); CHECK(t.state == TRAFFIC_YELLOW && t.request);
        traffic_update(&t, false, 19000); CHECK(t.state == TRAFFIC_RED && t.request);
        traffic_update(&t, false, 21999); CHECK(t.state == TRAFFIC_RED);
        traffic_update(&t, false, 22000); CHECK(t.state == TRAFFIC_GREEN && !t.request);
    }
    traffic_init(&t, UINT32_MAX - 999);
    traffic_update(&t, true, 1999); CHECK(t.state == TRAFFIC_RED);
    traffic_update(&t, false, 2000); CHECK(t.state == TRAFFIC_GREEN);

    Button b;
    traffic_init(&t, 0); button_init(&b, false, 0, 30);
    for (uint32_t now = 0; now <= 10000; ++now) {
        bool raw = now >= 900 && (now >= 910 || (now & 1U));
        traffic_update(&t, button_update(&b, raw, now), now);
        if (now == 2999) CHECK(t.state == TRAFFIC_RED);
        if (now == 3000) CHECK(t.state == TRAFFIC_GREEN);
    }
    CHECK(t.state == TRAFFIC_RED && !t.request);
}

static void gpio_tests(void)
{
    const uint16_t pin = 1U << 2;
    IoConfig c = {IO_OUTPUT, IO_NO_PULL, 0, false, 0}, got;
    CHECK(io_clock_enable(GPIOD));
    CHECK((RCC->AHB1ENR & (1U << 3)) != 0);
    CHECK(!io_clock_enable((GPIO_TypeDef *)0));
    GPIOD->MODER = 3;
    GPIOD->ODR = 1;
    guard_high = pin;
    CHECK(io_configure(GPIOD, pin, &c, pin) == IO_OK);
    guard_driven = pin;
    CHECK(io_get_config(GPIOD, 2, &got));
    CHECK(got.mode == IO_OUTPUT && got.speed == 0 && !got.open_drain);
    CHECK((GPIOD->MODER & 3U) == 3);
    CHECK(io_read_latch(GPIOD, pin | 1U) == (pin | 1U));
    c.speed = 2;
    CHECK(io_configure(GPIOD, pin, &c, pin) == IO_OK);
    CHECK(io_get_config(GPIOD, 2, &got) && got.speed == 2);
    guard_high = guard_driven = 0;
    io_write_pin(GPIOD, 2, false); CHECK(io_read_latch(GPIOD, pin | 1U) == 1);
    io_write(GPIOD, 0x000C, 8); CHECK(io_read_latch(GPIOD, 15) == 9);
    GPIOD->IDR = pin;
    CHECK(io_read_pin(GPIOD, 2)); CHECK(!io_read_pin(GPIOD, 3));
    CHECK(io_read(GPIOD, 15) == pin);
    uint32_t old_moder = GPIOD->MODER, old_odr = GPIOD->ODR;
    c.speed = 4;
    CHECK(io_configure(GPIOD, pin, &c, 0) == IO_INVALID);
    CHECK(GPIOD->MODER == old_moder && GPIOD->ODR == old_odr);
    c.speed = 0;
    GPIOD->LCKR = 0x10000 | pin;
    CHECK(io_configure(GPIOD, pin, &c, pin) == IO_LOCKED);
    CHECK(GPIOD->MODER == old_moder && GPIOD->ODR == old_odr);
    GPIOD->LCKR = 0;
    c.mode = IO_INPUT; c.pull = IO_PULL_UP;
    CHECK(io_configure(GPIOD, pin, &c, 0) == IO_OK);
    CHECK(io_get_config(GPIOD, 2, &got) && got.mode == IO_INPUT && got.pull == IO_PULL_UP);
    c.mode = IO_ALTERNATE; c.alternate = 7;
    CHECK(io_configure(GPIOD, pin, &c, 0) == IO_OK);
    CHECK(io_get_config(GPIOD, 2, &got) && got.alternate == 7 && got.mode == IO_ALTERNATE);
    c.mode = IO_OUTPUT;
    CHECK(io_configure(GPIOD, pin, &c, 0) == IO_HANDOFF_REQUIRED);
    c.mode = IO_ANALOG;
    CHECK(io_configure(GPIOD, pin, &c, 0) == IO_OK);
    c.mode = IO_OUTPUT;
    CHECK(io_configure(GPIOD, pin, &c, pin) == IO_OK);
    CHECK(!io_get_config(GPIOD, 16, &got));
    CHECK(io_configure(GPIOD, 0, &c, 0) == IO_INVALID);
    LedBank leds;
    CHECK(led_init(&leds, GPIOD, BOARD_LED_MASK, 0));
    led_write(&leds, BOARD_RED); CHECK(io_read_latch(GPIOD, BOARD_LED_MASK) == (1U << 15));
    led_write(&leds, BOARD_YELLOW); CHECK(io_read_latch(GPIOD, BOARD_LED_MASK) == (1U << 14));
    led_write(&leds, BOARD_GREEN); CHECK(io_read_latch(GPIOD, BOARD_LED_MASK) == (1U << 13));
}

void test_entry(void)
{
    debounce_tests(); traffic_tests(); gpio_tests();
    test_done = 1;
    for (;;) {}
}
