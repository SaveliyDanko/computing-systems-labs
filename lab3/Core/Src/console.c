#include "console.h"
#include "serial.h"
#include <stdio.h>
#include <string.h>

_Static_assert(SERIAL_TX_CAPACITY >= CONSOLE_REPLY_RESERVE, "TX must fit an echo and reply");

static void reply(const char *text) { (void)serial_write(text, strlen(text)); }

void console_init(Console *console)
{
    *console = (Console){serial_rx_epoch(), PIANO_NO_NOTE};
}

static void respond(piano_event_t event, const piano_t *piano, uint8_t byte)
{
    char text[CONSOLE_REPLY_RESERVE - 1U];
    int size;
    switch (event) {
    case PIANO_EVENT_NOTE: {
        uint32_t frequency = piano_frequency_millihz(piano);
        size = snprintf(text, sizeof(text), "\r\nNote %s, octave %s (%u/9), %lu.%03lu Hz, duration %lu.%lu s\r\n",
            piano_note_name(piano_current_note(piano)), piano_octave_name(piano_playback_octave(piano)),
            (unsigned)piano_playback_octave(piano), (unsigned long)(frequency / 1000U),
            (unsigned long)(frequency % 1000U), (unsigned long)(piano_playback_duration_ms(piano) / 1000U),
            (unsigned long)((piano_playback_duration_ms(piano) % 1000U) / 100U));
        break;
    }
    case PIANO_EVENT_OCTAVE:
        size = snprintf(text, sizeof(text), "\r\nOctave %s (%u/9), duration %lu.%lu s\r\n",
            piano_octave_name(piano_selected_octave(piano)), (unsigned)piano_selected_octave(piano),
            (unsigned long)(piano_duration_ms(piano) / 1000U),
            (unsigned long)((piano_duration_ms(piano) % 1000U) / 100U));
        break;
    case PIANO_EVENT_DURATION:
        size = snprintf(text, sizeof(text), "\r\nDuration %lu.%lu s, octave %s (%u/9)\r\n",
            (unsigned long)(piano_duration_ms(piano) / 1000U),
            (unsigned long)((piano_duration_ms(piano) % 1000U) / 100U),
            piano_octave_name(piano_selected_octave(piano)), (unsigned)piano_selected_octave(piano));
        break;
    case PIANO_EVENT_SCALE:
        size = snprintf(text, sizeof(text), "\r\nScale do re mi fa sol la si, octave %s (%u/9), duration %lu.%lu s per note\r\n",
            piano_octave_name(piano_playback_octave(piano)), (unsigned)piano_playback_octave(piano),
            (unsigned long)(piano_playback_duration_ms(piano) / 1000U),
            (unsigned long)((piano_playback_duration_ms(piano) % 1000U) / 100U));
        break;
    case PIANO_EVENT_LIMIT:
        reply("\r\nLIMIT: octave 1..9, duration 0.1..5.0 s\r\n");
        return;
    case PIANO_EVENT_INVALID:
        size = snprintf(text, sizeof(text), "\r\nERROR: неверный символ, код 0x%02X; use 1..7, +, -, A, a, Enter\r\n", (unsigned)byte);
        break;
    default: return;
    }
    if (size > 0 && (size_t)size < sizeof(text)) (void)serial_write(text, (size_t)size);
}

void console_update(Console *console, piano_t *piano, uint32_t now_ms)
{
    for (unsigned i = 0; i < 16U && serial_tx_free() >= CONSOLE_REPLY_RESERVE; ++i) {
        uint32_t epoch = serial_rx_epoch();
        if (epoch != console->rx_epoch) {
            console->rx_epoch = epoch;
            reply("\r\nERROR: input lost; repeat the last key\r\n");

            continue;
        }
        uint8_t byte;
        if (!serial_read(&byte)) break;
        epoch = serial_rx_epoch();
        if (epoch != console->rx_epoch) continue;
        (void)serial_write(&byte, 1);
        piano_event_t event = piano_input(piano, byte, now_ms);
        respond(event, piano, byte);
        if (event == PIANO_EVENT_SCALE) console->reported_scale_note = PIANO_NO_NOTE;
    }
    if (!piano_is_scale(piano)) console->reported_scale_note = PIANO_NO_NOTE;
    else if (console->reported_scale_note != piano_current_note(piano) &&
             serial_tx_free() >= CONSOLE_REPLY_RESERVE) {
        respond(PIANO_EVENT_NOTE, piano, 0);
        console->reported_scale_note = piano_current_note(piano);
    }
}
