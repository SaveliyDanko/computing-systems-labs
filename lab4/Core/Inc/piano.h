#ifndef PIANO_H
#define PIANO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIANO_NOTE_COUNT 7u
#define PIANO_NO_NOTE PIANO_NOTE_COUNT
#define PIANO_OCTAVE_MIN 1u
#define PIANO_OCTAVE_MAX 9u
#define PIANO_OCTAVE_COUNT 9u
#define PIANO_DEFAULT_OCTAVE 5u
#define PIANO_DURATION_MIN_MS 100u
#define PIANO_DURATION_MAX_MS 5000u
#define PIANO_DURATION_STEP_MS 100u
#define PIANO_DEFAULT_DURATION_MS 1000u

/** Result of the most recent input byte. State changes precede the result. */
typedef enum {
    PIANO_EVENT_IGNORED = 0,
    PIANO_EVENT_NOTE,
    PIANO_EVENT_OCTAVE,
    PIANO_EVENT_DURATION,
    PIANO_EVENT_SCALE,
    PIANO_EVENT_INVALID,
    PIANO_EVENT_LIMIT
} piano_event_t;

/**
 * Application state, exposed only to permit static/stack allocation.
 * Use the functions below to inspect or change it.
 * Octaves are numbered 1..9: subcontra (C0) through fifth (C8).
 * Notes are indexed 0..6: do, re, mi, fa, sol, la, si.
 */
typedef struct {
    uint32_t selected_duration_ms;
    uint32_t active_duration_ms;
    uint32_t note_started_at_ms;
    uint8_t selected_octave;
    uint8_t active_octave;
    uint8_t active_note;
    bool playing;
    bool scale;
    bool suppress_lf;
} piano_t;

/** Initialize a silent piano in the first octave (C4), duration 1000 ms. */
void piano_init(piano_t *piano);
/* Stop playback while preserving octave/duration settings. */
void piano_stop(piano_t *piano);

/**
 * Process one UART byte at the supplied unsigned millisecond tick.
 * 1..7 start a note, +/- select an octave, A/a adjust duration by 100 ms,
 * and CR or LF starts a seven-note ascending scale. An LF immediately
 * following CR is ignored. Other bytes return INVALID.
 * A new note/scale interrupts playback. Octave/duration changes affect
 * subsequent launches; active notes and scales keep their initial settings.
 * This function also advances any playback to now_ms before processing input.
 */
piano_event_t piano_input(piano_t *piano, uint8_t byte, uint32_t now_ms);

/**
 * Advance/finish playback without blocking. Call regularly, including when
 * UART input is absent. Unsigned subtraction handles UINT32_MAX rollover;
 * fewer than 2^32 milliseconds may elapse between updates of an active note.
 * Late updates skip elapsed notes and retain the original scale timeline.
 */
void piano_update(piano_t *piano, uint32_t now_ms);

/** Current output frequency in millihertz; zero means silence. */
uint32_t piano_frequency_millihz(const piano_t *piano);

/** Settings for the next note/scale launch. */
uint8_t piano_selected_octave(const piano_t *piano);
uint32_t piano_duration_ms(const piano_t *piano);

/** Playback status. current_note returns PIANO_NO_NOTE while silent. */
bool piano_is_playing(const piano_t *piano);
bool piano_is_scale(const piano_t *piano);
uint8_t piano_current_note(const piano_t *piano);

/** Snapshot used by current playback, or zero when silent. */
uint8_t piano_playback_octave(const piano_t *piano);
uint32_t piano_playback_duration_ms(const piano_t *piano);

/**
 * Equal-tempered natural-note table (A4 = 440000 mHz), rounded to 1 mHz.
 * Invalid octave/note values return zero. Names use ASCII for UART output;
 * invalid values return "?". These helpers do not depend on piano state.
 */
uint32_t piano_note_frequency_millihz(uint8_t octave, uint8_t note);
const char *piano_note_name(uint8_t note);
const char *piano_octave_name(uint8_t octave);

#ifdef __cplusplus
}
#endif

#endif /* PIANO_H */
