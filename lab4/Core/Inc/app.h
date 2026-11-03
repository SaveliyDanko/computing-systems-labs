#ifndef APP_H
#define APP_H
#include "piano.h"
#include "button.h"
#include "keypad.h"
#include <stdbool.h>
#include <stdint.h>

#define APP_REPORT_CAPACITY 16U
typedef enum { APP_MODE_TEST, APP_MODE_PIANO } AppMode;
typedef enum { REPORT_MODE, REPORT_KEY, REPORT_NOTE, REPORT_SETTINGS,
               REPORT_LIMIT, REPORT_OFFLINE, REPORT_ONLINE } AppReportKind;
typedef struct {
    AppReportKind kind;
    uint32_t duration, frequency;
    uint8_t key, octave, note;
} AppReport;
typedef struct {
    piano_t piano;
    Button button;
    Keypad keypad;
    AppMode mode;
    uint32_t key_events, mode_changes, dropped_reports, seen_bus_errors;
    uint8_t last_key, reported_scale_note, report_head, report_tail, report_count;
    bool bus_offline;
    AppReport reports[APP_REPORT_CAPACITY];
} App;

/* Call after clock_init, sound_init and serial_init(true). Starts in TEST.
 * UART is an output/diagnostic channel; input comes from the physical keys. */
bool app_init(App *app, uint32_t now_ms);
bool app_update(App *app, uint32_t now_ms);
AppMode app_mode(const App *app);
uint8_t app_key_character(uint8_t key);
#endif
