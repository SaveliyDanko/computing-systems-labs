#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEYPAD_KEY_COUNT 12u
#define KEYPAD_KEY_MASK 0x0FFFu
#define KEYPAD_DEBOUNCE_MS 30u
#define KEYPAD_RETRY_MS 50u
#define KEYPAD_I2C_ADDRESS 0x71u

typedef struct {
    uint32_t press_since_ms;
    uint32_t release_since_ms;
    uint16_t key_mask;
    bool blocked;
    bool press_tracking;
    bool release_tracking;
    bool delivered;
} KeypadFilter;

void keypad_filter_init(KeypadFilter *filter, uint32_t now_ms);

void keypad_filter_rearm(KeypadFilter *filter, uint32_t now_ms);

uint8_t keypad_filter_update(KeypadFilter *filter, uint16_t mask, bool valid,
                             uint32_t now_ms);

bool keypad_filter_is_blocked(const KeypadFilter *filter);

typedef struct {
    uint32_t frames;
    uint32_t errors;
    uint32_t events;
} KeypadStats;

typedef enum {
    KEYPAD_SCAN_SAFE_START,
    KEYPAD_SCAN_SAFE_WAIT,
    KEYPAD_SCAN_OUTPUT_START,
    KEYPAD_SCAN_OUTPUT_WAIT,
    KEYPAD_SCAN_POLARITY_START,
    KEYPAD_SCAN_POLARITY_WAIT,
    KEYPAD_SCAN_ROW_START,
    KEYPAD_SCAN_ROW_WAIT,
    KEYPAD_SCAN_SETTLE,
    KEYPAD_SCAN_READ_START,
    KEYPAD_SCAN_READ_WAIT,
    KEYPAD_SCAN_RETRY
} KeypadScanState;

typedef struct {
    KeypadFilter filter;
    KeypadStats stats;
    uint32_t state_since_ms;
    uint16_t frame_mask;
    uint16_t last_mask;
    KeypadScanState state;
    uint8_t row;
    bool last_valid;
    bool bus_initialized;
} Keypad;

bool keypad_init(Keypad *keypad, uint32_t now_ms);

uint8_t keypad_update(Keypad *keypad, uint32_t now_ms);

void keypad_rearm(Keypad *keypad, uint32_t now_ms);

KeypadStats keypad_stats(const Keypad *keypad);
uint16_t keypad_raw_mask(const Keypad *keypad);
bool keypad_scan_valid(const Keypad *keypad);
bool keypad_is_blocked(const Keypad *keypad);

#ifdef __cplusplus
}
#endif

#endif
