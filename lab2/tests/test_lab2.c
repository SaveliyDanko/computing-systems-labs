#include "traffic.h"
#include "button.h"
#include "command.h"
#include "console.h"
#include "serial.h"
#include "stm32f4xx_hal.h"
#include <string.h>
volatile uint32_t test_checks, test_failure, test_done;
Traffic test_traffic;
Console test_console;
#define CHECK(x) do { ++test_checks; if (!(x)) { test_failure = __LINE__; test_done = 1; return; } } while (0)
bool test_boot(bool interrupts)
{
    SystemCoreClock = 16000000U;
    traffic_init(&test_traffic, 0);
    if (!serial_init(interrupts)) return false;
    console_init(&test_console);
    return true;
}
void test_step(uint32_t now)
{
    serial_service();
    console_update(&test_console, &test_traffic);
    traffic_update(&test_traffic, false, now);
}
uint32_t test_red_ms(void) { return test_traffic.red_ms; }
uint32_t test_mode(void) { return test_traffic.mode; }
uint32_t test_overflows(void) { return serial_stats().rx_overflows; }
uint32_t test_rx_errors(void) { return serial_stats().rx_errors; }
uint32_t test_tx_errors(void) { return serial_stats().tx_errors; }
uint32_t test_fill_tx(uint32_t count)
{
    static uint8_t bytes[SERIAL_TX_CAPACITY];
    for (uint32_t i = 0; i < count && i < SERIAL_TX_CAPACITY; ++i) bytes[i] = (uint8_t)('a' + i % 26);
    return serial_write(bytes, count);
}
void test_entry(void)
{
    Traffic t;
    Button b;
    traffic_init(&t, UINT32_MAX - 1000U);
    CHECK(t.mode == 1 && t.red_ms == 12000 && !t.request);
    traffic_update(&t, true, UINT32_MAX - 500U);
    CHECK(t.request && t.state == TRAFFIC_RED);
    traffic_update(&t, false, 1998U);
    CHECK(t.state == TRAFFIC_RED);
    traffic_update(&t, false, 1999U);
    CHECK(t.state == TRAFFIC_GREEN && !t.request);
    CHECK(!traffic_set_timeout(&t, 0));
    CHECK(!traffic_set_timeout(&t, 86401));
    CHECK(traffic_set_timeout(&t, 86400) && t.red_ms == 86400000U);
    CHECK(!traffic_set_mode(&t, 0) && !traffic_set_mode(&t, 3));
    traffic_init(&t, 0);
    traffic_update(&t, true, 2000);
    CHECK(t.request);
    CHECK(traffic_set_mode(&t, 2) && !t.request);
    traffic_update(&t, true, 3000);
    CHECK(t.state == TRAFFIC_RED && !t.request);
    CHECK(traffic_set_timeout(&t, 4));
    traffic_update(&t, false, 3999);
    CHECK(t.state == TRAFFIC_RED);
    traffic_update(&t, false, 4000);
    CHECK(t.state == TRAFFIC_GREEN);
    traffic_update(&t, true, 7000);
    CHECK(t.state == TRAFFIC_BLINK && !t.request);
    CHECK(!strcmp(traffic_name(&t), "blinking green"));
    CHECK(traffic_light(&t, 7499) == LIGHT_GREEN);
    CHECK(traffic_light(&t, 7500) == LIGHT_OFF);
    CHECK(traffic_set_mode(&t, 1));
    traffic_update(&t, true, 10000);
    CHECK(t.state == TRAFFIC_YELLOW && t.request);
    traffic_update(&t, false, 11000);
    CHECK(t.state == TRAFFIC_RED && t.request);
    traffic_update(&t, false, 12000);
    CHECK(t.state == TRAFFIC_GREEN && !t.request);
    traffic_init(&t, 1000);
    CHECK(traffic_set_timeout(&t, 1));
    traffic_update(&t, true, 1249);
    CHECK(t.state == TRAFFIC_RED);
    traffic_update(&t, false, 1250);
    CHECK(t.state == TRAFFIC_GREEN);
    const char *valid[] = {"?", "set mode 1", "set mode 2", "set timeout 1",
        "set timeout 86400", "set interrupts on", "set interrupts off"};
    for (unsigned i = 0; i < sizeof(valid)/sizeof(valid[0]); ++i) CHECK(command_parse(valid[i]).type != CMD_ERROR);
    const char *invalid[] = {"", "set mode 3", "set timeout", "set timeout ",
        "set timeout 0", "set timeout -1", "set timeout +1", "set timeout 86401",
        "set timeout 4294967296", "set timeout 9999999999999999999999999",
        "set timeout 1foo", "set interrupts ON", "?junk", "set mode 1 junk", "set timeout 1 "};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) CHECK(command_parse(invalid[i]).type == CMD_ERROR);
    CHECK(command_parse(0).type == CMD_ERROR);
    CHECK(command_parse("set timeout 00012").value == 12);
    button_init(&b, true, 0, 30);
    CHECK(!button_update(&b, true, 100));
    CHECK(!button_update(&b, false, 101));
    CHECK(!button_update(&b, false, 131));
    CHECK(!button_update(&b, true, 132));
    CHECK(!button_update(&b, true, 161));
    CHECK(button_update(&b, true, 162));
    CHECK(!button_update(&b, true, 200));
    test_done = 1;
}
