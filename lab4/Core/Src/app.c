#include "app.h"
#include "board.h"
#include "gpio_driver.h"
#include "serial.h"
#include "sound.h"
#include <stdio.h>

#define REPORT_RESERVE 192U
_Static_assert(SERIAL_TX_CAPACITY >= REPORT_RESERVE, "TX must fit a complete report");
_Static_assert(APP_REPORT_CAPACITY > 0 && APP_REPORT_CAPACITY <= 255U, "Report queue size");

AppMode app_mode(const App *app) { return app->mode; }

uint8_t app_key_character(uint8_t key)
{
    static const uint8_t mapping[KEYPAD_KEY_COUNT] = {
        '1', '2', '3', '4', '5', '6', '7', '+', '-', 'A', 'a', '\r'
    };
    return key >= 1U && key <= KEYPAD_KEY_COUNT ? mapping[key - 1U] : 0;
}

static void notify(App *app, AppReportKind kind, uint8_t key)
{
    if (app->report_count == APP_REPORT_CAPACITY) { ++app->dropped_reports; return; }
    AppReport *r = &app->reports[app->report_head];
    *r = (AppReport){.kind = kind, .key = key};
    if (kind == REPORT_NOTE) {
        r->note = piano_current_note(&app->piano);
        r->octave = piano_playback_octave(&app->piano);
        r->duration = piano_playback_duration_ms(&app->piano);
        r->frequency = piano_frequency_millihz(&app->piano);
    } else {
        r->octave = piano_selected_octave(&app->piano);
        r->duration = piano_duration_ms(&app->piano);
    }
    app->report_head = (uint8_t)((app->report_head + 1U) % APP_REPORT_CAPACITY);
    ++app->report_count;
}

static void send_report(App *app)
{
    if (!app->report_count || serial_tx_free() < REPORT_RESERVE) return;
    const AppReport *r = &app->reports[app->report_tail];
    char text[REPORT_RESERVE];
    int size;
    switch (r->kind) {
    case REPORT_MODE:
        size = snprintf(text, sizeof(text), "\r\nMode: %s\r\n",
                        r->key == APP_MODE_TEST ? "keyboard test" : "piano");
        break;
    case REPORT_KEY:
        size = snprintf(text, sizeof(text), "\r\nKey %u\r\n", (unsigned)r->key);
        break;
    case REPORT_NOTE:
        size = snprintf(text, sizeof(text), "\r\nNote %s, octave %s (%u/9), %lu.%03lu Hz, duration %lu.%lu s\r\n",
            piano_note_name(r->note), piano_octave_name(r->octave), (unsigned)r->octave,
            (unsigned long)(r->frequency / 1000U), (unsigned long)(r->frequency % 1000U),
            (unsigned long)(r->duration / 1000U), (unsigned long)((r->duration % 1000U) / 100U));
        break;
    case REPORT_SETTINGS:
        size = snprintf(text, sizeof(text), "\r\nOctave %s (%u/9), duration %lu.%lu s\r\n",
            piano_octave_name(r->octave), (unsigned)r->octave,
            (unsigned long)(r->duration / 1000U), (unsigned long)((r->duration % 1000U) / 100U));
        break;
    case REPORT_LIMIT:
        size = snprintf(text, sizeof(text), "\r\nLIMIT: octave %u/9, duration %lu.%lu s\r\n",
            (unsigned)r->octave, (unsigned long)(r->duration / 1000U),
            (unsigned long)((r->duration % 1000U) / 100U));
        break;
    case REPORT_OFFLINE:
        size = snprintf(text, sizeof(text), "\r\nKeyboard I2C error; retrying\r\n");
        break;
    default:
        size = snprintf(text, sizeof(text), "\r\nKeyboard online; release all keys\r\n");
        break;
    }
    if (size > 0 && (size_t)size < sizeof(text) && serial_write(text, (size_t)size)) {
        app->report_tail = (uint8_t)((app->report_tail + 1U) % APP_REPORT_CAPACITY);
        --app->report_count;
    }
}

bool app_init(App *app, uint32_t now_ms)
{
    *app = (App){.mode = APP_MODE_TEST, .reported_scale_note = PIANO_NO_NOTE};
    piano_init(&app->piano);
    const IoConfig button_pin = {IO_INPUT, IO_NO_PULL, 0, false, 0};
    if (!io_clock_enable(BOARD_BUTTON_PORT) ||
        io_configure(BOARD_BUTTON_PORT, (uint16_t)(1U << BOARD_BUTTON_PIN), &button_pin, 0) != IO_OK)
        return false;
    button_init(&app->button, !io_read_pin(BOARD_BUTTON_PORT, BOARD_BUTTON_PIN),
                now_ms, BOARD_DEBOUNCE_MS);
    if (!keypad_init(&app->keypad, now_ms)) return false;
    notify(app, REPORT_MODE, APP_MODE_TEST);
    return true;
}

bool app_update(App *app, uint32_t now_ms)
{
    serial_service();

    for (unsigned i = 0; i < 16U; ++i) {
        uint8_t byte;
        if (!serial_read(&byte)) break;
    }
    bool switched = button_update(&app->button,
                    !io_read_pin(BOARD_BUTTON_PORT, BOARD_BUTTON_PIN), now_ms);
    if (switched) {
        app->mode = app->mode == APP_MODE_TEST ? APP_MODE_PIANO : APP_MODE_TEST;
        ++app->mode_changes;
        piano_stop(&app->piano);
        app->reported_scale_note = PIANO_NO_NOTE;
        keypad_rearm(&app->keypad, now_ms);
        notify(app, REPORT_MODE, (uint8_t)app->mode);
    }
    piano_update(&app->piano, now_ms);
    uint8_t key = keypad_update(&app->keypad, now_ms);
    if (key && !switched) {
        ++app->key_events;
        app->last_key = key;
        if (app->mode == APP_MODE_TEST) notify(app, REPORT_KEY, key);
        else {
            piano_event_t event = piano_input(&app->piano, app_key_character(key), now_ms);
            if (event == PIANO_EVENT_NOTE) notify(app, REPORT_NOTE, key);
            else if (event == PIANO_EVENT_OCTAVE || event == PIANO_EVENT_DURATION)
                notify(app, REPORT_SETTINGS, key);
            else if (event == PIANO_EVENT_LIMIT) notify(app, REPORT_LIMIT, key);
            else if (event == PIANO_EVENT_SCALE) app->reported_scale_note = PIANO_NO_NOTE;
        }
    }
    if (!piano_is_scale(&app->piano)) app->reported_scale_note = PIANO_NO_NOTE;
    else if (app->reported_scale_note != piano_current_note(&app->piano)) {
        notify(app, REPORT_NOTE, 12);
        app->reported_scale_note = piano_current_note(&app->piano);
    }
    KeypadStats stats = keypad_stats(&app->keypad);
    if (stats.errors != app->seen_bus_errors) {
        app->seen_bus_errors = stats.errors;
        if (!app->bus_offline) notify(app, REPORT_OFFLINE, 0);
        app->bus_offline = true;
    } else if (app->bus_offline && keypad_scan_valid(&app->keypad)) {
        app->bus_offline = false;
        notify(app, REPORT_ONLINE, 0);
    }
    bool sound_ok = sound_set_frequency(piano_frequency_millihz(&app->piano));
    send_report(app);
    return sound_ok;
}
