#include "keypad.h"
#include "i2c_bus.h"

#define PCA9538_INPUT_REG 0u
#define PCA9538_OUTPUT_REG 1u
#define PCA9538_POLARITY_REG 2u
#define PCA9538_CONFIG_REG 3u
#define KEYPAD_ROW_COUNT 4u
#define KEYPAD_COLUMN_COUNT 3u
#define KEYPAD_ROW_SETTLE_MS 1u
#define KEYPAD_TRANSACTION_GUARD_MS (I2C_BUS_TIMEOUT_MS + I2C_BUS_RECOVERY_MAX_MS + 5u)

void keypad_filter_init(KeypadFilter *filter, uint32_t now_ms)
{
    *filter = (KeypadFilter) {
        .press_since_ms = now_ms,
        .release_since_ms = now_ms,
        .key_mask = 0u,
        .blocked = true,
        .press_tracking = false,
        .release_tracking = false,
        .delivered = false
    };
}

void keypad_filter_rearm(KeypadFilter *filter, uint32_t now_ms)
{
    keypad_filter_init(filter, now_ms);
}

uint8_t keypad_filter_update(KeypadFilter *filter, uint16_t mask, bool valid,
                             uint32_t now_ms)
{
    if (!valid || mask > KEYPAD_KEY_MASK ||
        (mask != 0u && (mask & (uint16_t)(mask - 1u)) != 0u)) {

        keypad_filter_rearm(filter, now_ms);
        return 0u;
    }

    if (mask == 0u) {
        filter->press_tracking = false;
        if (!filter->release_tracking) {
            filter->release_tracking = true;
            filter->release_since_ms = now_ms;
        }
        if (now_ms - filter->release_since_ms >= KEYPAD_DEBOUNCE_MS) {
            filter->blocked = false;
            filter->key_mask = 0u;
            filter->delivered = false;
        }
        return 0u;
    }

    filter->release_tracking = false;
    if (filter->blocked) {
        return 0u;
    }
    if (filter->key_mask != 0u && mask != filter->key_mask) {
        keypad_filter_rearm(filter, now_ms);
        return 0u;
    }
    filter->key_mask = mask;
    if (filter->delivered) {
        return 0u;
    }
    if (!filter->press_tracking) {
        filter->press_tracking = true;
        filter->press_since_ms = now_ms;
    }
    if (now_ms - filter->press_since_ms < KEYPAD_DEBOUNCE_MS) {
        return 0u;
    }

    filter->delivered = true;
    filter->press_tracking = false;
    uint8_t code = 1u;
    while ((mask >>= 1u) != 0u) {
        ++code;
    }
    return code;
}

bool keypad_filter_is_blocked(const KeypadFilter *filter)
{
    return filter->blocked;
}

static void transition(Keypad *keypad, KeypadScanState state, uint32_t now_ms)
{
    keypad->state = state;
    keypad->state_since_ms = now_ms;
}

static void scan_error(Keypad *keypad, uint32_t now_ms)
{
    ++keypad->stats.errors;
    keypad->frame_mask = 0u;
    keypad->last_mask = 0u;
    keypad->last_valid = false;
    keypad->row = 0u;
    (void)keypad_filter_update(&keypad->filter, 0u, false, now_ms);
    transition(keypad, KEYPAD_SCAN_RETRY, now_ms);
}

static void start_write(Keypad *keypad, uint8_t reg, uint8_t value,
                        KeypadScanState wait_state, uint32_t now_ms)
{
    if (i2c_bus_write_reg(KEYPAD_I2C_ADDRESS, reg, value, now_ms)) {
        transition(keypad, wait_state, now_ms);
    } else if (now_ms - keypad->state_since_ms >= I2C_BUS_TIMEOUT_MS) {

        scan_error(keypad, now_ms);
    }
}

static int transaction_complete(Keypad *keypad, uint32_t now_ms)
{
    const I2cBusResult result = i2c_bus_result();
    if (result == I2C_BUS_OK) {
        return 1;
    }
    if (result == I2C_BUS_ERROR || result == I2C_BUS_IDLE ||
        now_ms - keypad->state_since_ms >= KEYPAD_TRANSACTION_GUARD_MS) {
        scan_error(keypad, now_ms);
        return -1;
    }
    return 0;
}

bool keypad_init(Keypad *keypad, uint32_t now_ms)
{
    *keypad = (Keypad) {
        .state_since_ms = now_ms,
        .state = KEYPAD_SCAN_SAFE_START
    };
    keypad_filter_init(&keypad->filter, now_ms);
    keypad->bus_initialized = i2c_bus_init();
    if (!keypad->bus_initialized) {
        scan_error(keypad, now_ms);
    }
    return keypad->bus_initialized;
}

uint8_t keypad_update(Keypad *keypad, uint32_t now_ms)
{
    i2c_bus_service(now_ms);

    switch (keypad->state) {
    case KEYPAD_SCAN_SAFE_START:

        start_write(keypad, PCA9538_CONFIG_REG, 0xFFu,
                    KEYPAD_SCAN_SAFE_WAIT, now_ms);
        break;
    case KEYPAD_SCAN_SAFE_WAIT:
        if (transaction_complete(keypad, now_ms) > 0) {
            transition(keypad, KEYPAD_SCAN_OUTPUT_START, now_ms);
        }
        break;
    case KEYPAD_SCAN_OUTPUT_START:
        start_write(keypad, PCA9538_OUTPUT_REG, 0x00u,
                    KEYPAD_SCAN_OUTPUT_WAIT, now_ms);
        break;
    case KEYPAD_SCAN_OUTPUT_WAIT:
        if (transaction_complete(keypad, now_ms) > 0) {
            transition(keypad, KEYPAD_SCAN_POLARITY_START, now_ms);
        }
        break;
    case KEYPAD_SCAN_POLARITY_START:
        start_write(keypad, PCA9538_POLARITY_REG, 0x00u,
                    KEYPAD_SCAN_POLARITY_WAIT, now_ms);
        break;
    case KEYPAD_SCAN_POLARITY_WAIT:
        if (transaction_complete(keypad, now_ms) > 0) {
            transition(keypad, KEYPAD_SCAN_ROW_START, now_ms);
        }
        break;
    case KEYPAD_SCAN_ROW_START:

        start_write(keypad, PCA9538_CONFIG_REG,
                    (uint8_t)(0xFFu & ~(1u << keypad->row)),
                    KEYPAD_SCAN_ROW_WAIT, now_ms);
        break;
    case KEYPAD_SCAN_ROW_WAIT:
        if (transaction_complete(keypad, now_ms) > 0) {
            transition(keypad, KEYPAD_SCAN_SETTLE, now_ms);
        }
        break;
    case KEYPAD_SCAN_SETTLE:
        if (now_ms - keypad->state_since_ms >= KEYPAD_ROW_SETTLE_MS) {
            transition(keypad, KEYPAD_SCAN_READ_START, now_ms);
        }
        break;
    case KEYPAD_SCAN_READ_START:
        if (i2c_bus_read_reg(KEYPAD_I2C_ADDRESS, PCA9538_INPUT_REG, now_ms)) {
            transition(keypad, KEYPAD_SCAN_READ_WAIT, now_ms);
        } else if (now_ms - keypad->state_since_ms >= I2C_BUS_TIMEOUT_MS) {
            scan_error(keypad, now_ms);
        }
        break;
    case KEYPAD_SCAN_READ_WAIT:
        if (transaction_complete(keypad, now_ms) > 0) {
            const uint8_t inputs = i2c_bus_data();
            for (uint8_t column = 0u; column < KEYPAD_COLUMN_COUNT; ++column) {
                if ((inputs & (1u << (column + KEYPAD_ROW_COUNT))) == 0u) {
                    keypad->frame_mask |= (uint16_t)(1u <<
                        (keypad->row + KEYPAD_ROW_COUNT * column));
                }
            }
            ++keypad->row;
            if (keypad->row < KEYPAD_ROW_COUNT) {
                transition(keypad, KEYPAD_SCAN_ROW_START, now_ms);
                break;
            }

            ++keypad->stats.frames;
            keypad->last_mask = keypad->frame_mask;
            keypad->last_valid = true;
            keypad->frame_mask = 0u;
            keypad->row = 0u;
            transition(keypad, KEYPAD_SCAN_ROW_START, now_ms);
            const uint8_t event = keypad_filter_update(&keypad->filter,
                keypad->last_mask, true, now_ms);
            if (event != 0u) {
                ++keypad->stats.events;
            }
            return event;
        }
        break;
    case KEYPAD_SCAN_RETRY:
        if (now_ms - keypad->state_since_ms >= KEYPAD_RETRY_MS) {
            if (!keypad->bus_initialized) {
                keypad->bus_initialized = i2c_bus_init();
                if (!keypad->bus_initialized) {
                    scan_error(keypad, now_ms);
                    break;
                }
            }
            transition(keypad, KEYPAD_SCAN_SAFE_START, now_ms);
        }
        break;
    default:
        scan_error(keypad, now_ms);
        break;
    }
    return 0u;
}

void keypad_rearm(Keypad *keypad, uint32_t now_ms)
{
    keypad_filter_rearm(&keypad->filter, now_ms);
}

KeypadStats keypad_stats(const Keypad *keypad)
{
    return keypad->stats;
}

uint16_t keypad_raw_mask(const Keypad *keypad)
{
    return keypad->last_mask;
}

bool keypad_scan_valid(const Keypad *keypad)
{
    return keypad->last_valid;
}

bool keypad_is_blocked(const Keypad *keypad)
{
    return keypad_filter_is_blocked(&keypad->filter);
}
