#include "keypad.h"
#include "button.h"
#include "i2c_bus.h"
#include "clock.h"
#include "sound.h"
#include "serial.h"
#include "app.h"
#include "stm32f4xx_hal.h"

volatile uint32_t test_checks, test_failure, test_done;
App test_app;
Keypad test_keypad;
#define CHECK(x) do { ++test_checks; if (!(x)) { test_failure = __LINE__; test_done = 1; return; } } while (0)

uint32_t test_i2c_errors(void) { return i2c_bus_stats().errors; }
uint32_t test_i2c_nacks(void) { return i2c_bus_stats().nacks; }
uint32_t test_i2c_timeouts(void) { return i2c_bus_stats().timeouts; }
bool test_boot(void)
{
    SystemCoreClock = 16000000U;
    return HAL_Init() == HAL_OK && clock_init() && sound_init() &&
           serial_init(true) && app_init(&test_app, 0);
}
bool test_step(uint32_t now) { return app_update(&test_app, now); }
uint32_t test_mode(void) { return app_mode(&test_app); }
uint32_t test_mode_changes(void) { return test_app.mode_changes; }
uint32_t test_events(void) { return test_app.key_events; }
uint32_t test_last_key(void) { return test_app.last_key; }
uint32_t test_frequency(void) { return piano_frequency_millihz(&test_app.piano); }
uint32_t test_octave(void) { return piano_selected_octave(&test_app.piano); }
uint32_t test_duration(void) { return piano_duration_ms(&test_app.piano); }
uint32_t test_gamma(void) { return piano_is_scale(&test_app.piano); }
uint32_t test_note(void) { return piano_current_note(&test_app.piano); }
uint32_t test_app_errors(void) { return keypad_stats(&test_app.keypad).errors; }
bool test_keypad_boot(uint32_t now) { return keypad_init(&test_keypad, now); }
uint32_t test_keypad_step(uint32_t now) { return keypad_update(&test_keypad, now); }
uint32_t test_keypad_events(void) { return keypad_stats(&test_keypad).events; }
uint32_t test_keypad_errors(void) { return keypad_stats(&test_keypad).errors; }
uint32_t test_keypad_frames(void) { return keypad_stats(&test_keypad).frames; }
uint32_t test_keypad_mask(void) { return keypad_raw_mask(&test_keypad); }
uint32_t test_keypad_valid(void) { return keypad_scan_valid(&test_keypad); }
uint32_t test_keypad_blocked(void) { return keypad_is_blocked(&test_keypad); }

void test_entry(void)
{
    KeypadFilter filter;
    Button button;
    keypad_filter_init(&filter, 0);
    CHECK(keypad_filter_is_blocked(&filter));
    CHECK(!keypad_filter_update(&filter, 0, true, 0));
    CHECK(!keypad_filter_update(&filter, 0, true, 29));
    CHECK(keypad_filter_is_blocked(&filter));
    CHECK(!keypad_filter_update(&filter, 0, true, 30));
    CHECK(!keypad_filter_is_blocked(&filter));
    for (unsigned code = 1; code <= 12; ++code) {
        uint16_t mask = (uint16_t)(1U << (code - 1U));
        uint32_t start = 100U + code * 1000U;
        CHECK(!keypad_filter_update(&filter, mask, true, start));
        CHECK(!keypad_filter_update(&filter, mask, true, start + 29U));
        CHECK(keypad_filter_update(&filter, mask, true, start + 30U) == code);
        CHECK(!keypad_filter_update(&filter, mask, true, start + 500U));
        CHECK(!keypad_filter_update(&filter, 0, true, start + 501U));
        CHECK(!keypad_filter_update(&filter, 0, true, start + 530U));
        CHECK(!keypad_filter_update(&filter, 0, true, start + 531U));
        CHECK(!keypad_filter_is_blocked(&filter));
    }
    /* Chords cannot turn into a press by releasing only one of their keys. */
    keypad_filter_rearm(&filter, 0);
    CHECK(!keypad_filter_update(&filter, 0, true, 0));
    CHECK(!keypad_filter_update(&filter, 0, true, 30));
    CHECK(!keypad_filter_update(&filter, 1, true, 100));
    CHECK(!keypad_filter_update(&filter, 3, true, 129));
    CHECK(keypad_filter_is_blocked(&filter));
    CHECK(!keypad_filter_update(&filter, 1, true, 200));
    CHECK(!keypad_filter_update(&filter, 1, true, 1000));
    CHECK(!keypad_filter_update(&filter, 0, true, 1100));
    CHECK(!keypad_filter_update(&filter, 0, true, 1129));
    CHECK(keypad_filter_is_blocked(&filter));
    CHECK(!keypad_filter_update(&filter, 0, true, 1130));
    CHECK(!keypad_filter_is_blocked(&filter));
    CHECK(!keypad_filter_update(&filter, 2, true, 1200));
    CHECK(keypad_filter_update(&filter, 2, true, 1230) == 2);
    CHECK(!keypad_filter_update(&filter, 3, true, 1231));
    CHECK(!keypad_filter_update(&filter, 2, true, 1500));

    /* Stable release is required after bounce, key changes and invalid scans. */
    keypad_filter_rearm(&filter, 0);
    CHECK(!keypad_filter_update(&filter, 0, true, 0));
    CHECK(!keypad_filter_update(&filter, 0, true, 30));
    CHECK(!keypad_filter_update(&filter, 1, true, 100));
    CHECK(!keypad_filter_update(&filter, 0, true, 110));
    CHECK(!keypad_filter_update(&filter, 1, true, 120));
    CHECK(keypad_filter_update(&filter, 1, true, 150) == 1);
    CHECK(!keypad_filter_update(&filter, 0, true, 200));
    CHECK(!keypad_filter_update(&filter, 0, true, 230));
    CHECK(!keypad_filter_update(&filter, 1, true, 300));
    CHECK(!keypad_filter_update(&filter, 2, true, 301));
    CHECK(!keypad_filter_update(&filter, 2, true, 400));
    CHECK(keypad_filter_is_blocked(&filter));
    CHECK(!keypad_filter_update(&filter, 0, true, 500));
    CHECK(!keypad_filter_update(&filter, 0, true, 530));
    CHECK(!keypad_filter_update(&filter, 4, true, 600));
    CHECK(!keypad_filter_update(&filter, 4, false, 629));
    CHECK(!keypad_filter_update(&filter, 4, true, 1000));
    CHECK(keypad_filter_is_blocked(&filter));
    CHECK(!keypad_filter_update(&filter, 0, true, 1100));
    CHECK(!keypad_filter_update(&filter, 0, true, 1130));
    CHECK(!keypad_filter_update(&filter, 0x1000, true, 1200));
    CHECK(keypad_filter_is_blocked(&filter));
    CHECK(!keypad_filter_update(&filter, 0, true, 1300));
    CHECK(!keypad_filter_update(&filter, 0, true, 1330));
    CHECK(!keypad_filter_update(&filter, 4, true, 1400));
    CHECK(keypad_filter_update(&filter, 4, true, 1430) == 3);

    keypad_filter_init(&filter, 0);
    CHECK(!keypad_filter_update(&filter, 1, true, 0));
    CHECK(!keypad_filter_update(&filter, 1, true, 10000));
    CHECK(keypad_filter_is_blocked(&filter));
    CHECK(!keypad_filter_update(&filter, 0, true, 10001));
    CHECK(!keypad_filter_update(&filter, 0, true, 10031));
    CHECK(!keypad_filter_update(&filter, 1, true, 11000));
    CHECK(keypad_filter_update(&filter, 1, true, 11030) == 1);
    keypad_filter_rearm(&filter, 11031);
    CHECK(!keypad_filter_update(&filter, 1, true, 12000));
    CHECK(keypad_filter_is_blocked(&filter));

    keypad_filter_init(&filter, UINT32_MAX - 14U);
    CHECK(!keypad_filter_update(&filter, 0, true, UINT32_MAX - 14U));
    CHECK(!keypad_filter_update(&filter, 0, true, 14));
    CHECK(keypad_filter_is_blocked(&filter));
    CHECK(!keypad_filter_update(&filter, 0, true, 15));
    CHECK(!keypad_filter_is_blocked(&filter));
    CHECK(!keypad_filter_update(&filter, 0x800, true, 20));
    CHECK(!keypad_filter_update(&filter, 0x800, true, 49));
    CHECK(keypad_filter_update(&filter, 0x800, true, 50) == 12);

    button_init(&button, false, UINT32_MAX - 14U, 30);
    CHECK(!button_update(&button, true, UINT32_MAX - 14U));
    CHECK(!button_update(&button, true, 14));
    CHECK(button_update(&button, true, 15));
    CHECK(!button_update(&button, true, 1000));
    CHECK(!button_update(&button, false, 1001));
    CHECK(!button_update(&button, false, 1031));
    CHECK(!button_update(&button, true, 1032));
    CHECK(button_update(&button, true, 1062));
    static const uint8_t mapping[] = {'1', '2', '3', '4', '5', '6', '7', '+', '-', 'A', 'a', '\r'};
    for (uint8_t key = 1; key <= 12; ++key)
        CHECK(app_key_character(key) == mapping[key - 1U]);
    CHECK(!app_key_character(0) && !app_key_character(13));
    test_done = 1;
}
