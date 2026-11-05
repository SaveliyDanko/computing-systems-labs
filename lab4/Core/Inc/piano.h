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

typedef enum {
    PIANO_EVENT_IGNORED = 0,
    PIANO_EVENT_NOTE,
    PIANO_EVENT_OCTAVE,
    PIANO_EVENT_DURATION,
    PIANO_EVENT_SCALE,
    PIANO_EVENT_INVALID,
    PIANO_EVENT_LIMIT
} piano_event_t;

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

void piano_init(piano_t *piano);

void piano_stop(piano_t *piano);

piano_event_t piano_input(piano_t *piano, uint8_t byte, uint32_t now_ms);

void piano_update(piano_t *piano, uint32_t now_ms);

uint32_t piano_frequency_millihz(const piano_t *piano);

uint8_t piano_selected_octave(const piano_t *piano);
uint32_t piano_duration_ms(const piano_t *piano);

bool piano_is_playing(const piano_t *piano);
bool piano_is_scale(const piano_t *piano);
uint8_t piano_current_note(const piano_t *piano);

uint8_t piano_playback_octave(const piano_t *piano);
uint32_t piano_playback_duration_ms(const piano_t *piano);

uint32_t piano_note_frequency_millihz(uint8_t octave, uint8_t note);
const char *piano_note_name(uint8_t note);
const char *piano_octave_name(uint8_t octave);

#ifdef __cplusplus
}
#endif

#endif
