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

/**
 * Pure key filter; fields are public only to allow static allocation.
 * Bit n in a raw mask represents physical button SB(n + 1).
 */
typedef struct {
    uint32_t press_since_ms;
    uint32_t release_since_ms;
    uint16_t key_mask;
    bool blocked;
    bool press_tracking;
    bool release_tracking;
    bool delivered;
} KeypadFilter;

/** Start blocked: even a key held at boot cannot become a new press. */
void keypad_filter_init(KeypadFilter *filter, uint32_t now_ms);

/** Discard pending presses and require a confirmed release before arming. */
void keypad_filter_rearm(KeypadFilter *filter, uint32_t now_ms);

/**
 * Supply one complete 12-key snapshot. Returns SB code 1..12 exactly once
 * after the same sole key has remained pressed for 30 ms; otherwise zero.
 * Holding a key never repeats. A chord, a change to a different key before
 * a confirmed release, an invalid mask, or valid=false blocks all presses
 * until valid all-released snapshots have persisted for 30 ms.
 * An ordinary release also requires 30 ms before another press is accepted.
 * All timing uses unsigned subtraction and supports uint32_t tick rollover.
 */
uint8_t keypad_filter_update(KeypadFilter *filter, uint16_t mask, bool valid,
                             uint32_t now_ms);
/** Whether boot/error/chord/mode-switch inhibition remains latched. */
bool keypad_filter_is_blocked(const KeypadFilter *filter);

typedef struct {
    uint32_t frames;  /**< Successfully scanned complete four-row frames. */
    uint32_t errors;  /**< Failed frames/initialization attempts. */
    uint32_t events;  /**< Debounced press events returned to the caller. */
} KeypadStats;

/* Scanner implementation state; applications should use the API below. */
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

/** Nonblocking scanner plus filter, suitable for static/stack allocation. */
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

/**
 * Initialize the MCU I2C bus and schedule expander initialization.
 * Returns false only if MCU bus initialization fails; later update calls
 * still retry. Slave absence/failure is detected asynchronously in update.
 */
bool keypad_init(Keypad *keypad, uint32_t now_ms);

/**
 * Service I2C and advance at most one nonblocking scanner step.
 * Call frequently from the main loop; this owns i2c_bus_service(now_ms).
 * Returns one debounced SB code 1..12, or zero. Scanning errors discard the
 * incomplete frame, block the filter, and schedule a retry after 50 ms.
 * PCA9538 P0..P3 are rows, P4..P6 are pulled-up columns, P7 is unused.
 * Only the selected row drives low; all other pins remain inputs.
 * Full-frame bit mapping is row + 4 * column (physical SB numbering).
 */
uint8_t keypad_update(Keypad *keypad, uint32_t now_ms);

/** Suppress held/pending keys until all keys are released for 30 ms. */
void keypad_rearm(Keypad *keypad, uint32_t now_ms);

KeypadStats keypad_stats(const Keypad *keypad);
uint16_t keypad_raw_mask(const Keypad *keypad);
bool keypad_scan_valid(const Keypad *keypad);
bool keypad_is_blocked(const Keypad *keypad);

#ifdef __cplusplus
}
#endif

#endif /* KEYPAD_H */
