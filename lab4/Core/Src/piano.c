#include "piano.h"

/* Rounded from 440000 * 2^((MIDI_note - 69) / 12), in millihertz.
 * Keeping the table integral avoids floating-point work on the target. */
static const uint32_t note_frequencies[PIANO_OCTAVE_COUNT][PIANO_NOTE_COUNT] = {
    { 16352u, 18354u, 20602u, 21827u, 24500u, 27500u, 30868u }, /* C0..B0 */
    { 32703u, 36708u, 41203u, 43654u, 48999u, 55000u, 61735u }, /* C1..B1 */
    { 65406u, 73416u, 82407u, 87307u, 97999u, 110000u, 123471u }, /* C2..B2 */
    { 130813u, 146832u, 164814u, 174614u, 195998u, 220000u, 246942u }, /* C3..B3 */
    { 261626u, 293665u, 329628u, 349228u, 391995u, 440000u, 493883u }, /* C4..B4 */
    { 523251u, 587330u, 659255u, 698456u, 783991u, 880000u, 987767u }, /* C5..B5 */
    { 1046502u, 1174659u, 1318510u, 1396913u, 1567982u, 1760000u, 1975533u }, /* C6..B6 */
    { 2093005u, 2349318u, 2637020u, 2793826u, 3135963u, 3520000u, 3951066u }, /* C7..B7 */
    { 4186009u, 4698636u, 5274041u, 5587652u, 6271927u, 7040000u, 7902133u } /* C8..B8 */
};

static const char *const note_names[PIANO_NOTE_COUNT] = {
    "do", "re", "mi", "fa", "sol", "la", "si"
};

static const char *const octave_names[PIANO_OCTAVE_COUNT] = {
    "subcontra", "contra", "great", "small", "first",
    "second", "third", "fourth", "fifth"
};

static void stop_playback(piano_t *piano)
{
    piano->playing = false;
    piano->scale = false;
    piano->active_note = PIANO_NO_NOTE;
}

void piano_stop(piano_t *piano)
{
    stop_playback(piano);
    piano->suppress_lf = false;
}

static void start_playback(piano_t *piano, uint8_t note, bool scale,
                           uint32_t now_ms)
{
    piano->active_octave = piano->selected_octave;
    piano->active_duration_ms = piano->selected_duration_ms;
    piano->note_started_at_ms = now_ms;
    piano->active_note = note;
    piano->playing = true;
    piano->scale = scale;
}

void piano_init(piano_t *piano)
{
    *piano = (piano_t) {
        .selected_duration_ms = PIANO_DEFAULT_DURATION_MS,
        .active_duration_ms = 0u,
        .note_started_at_ms = 0u,
        .selected_octave = PIANO_DEFAULT_OCTAVE,
        .active_octave = 0u,
        .active_note = PIANO_NO_NOTE,
        .playing = false,
        .scale = false,
        .suppress_lf = false
    };
}

piano_event_t piano_input(piano_t *piano, uint8_t byte, uint32_t now_ms)
{
    piano_update(piano, now_ms);

    if (byte == '\n' && piano->suppress_lf) {
        piano->suppress_lf = false;
        return PIANO_EVENT_IGNORED;
    }
    /* Suppression applies only to an LF immediately following a CR. */
    piano->suppress_lf = byte == '\r';

    if (byte >= '1' && byte <= '7') {
        start_playback(piano, (uint8_t)(byte - '1'), false, now_ms);
        return PIANO_EVENT_NOTE;
    }

    switch (byte) {
    case '+':
        if (piano->selected_octave == PIANO_OCTAVE_MAX) {
            return PIANO_EVENT_LIMIT;
        }
        ++piano->selected_octave;
        return PIANO_EVENT_OCTAVE;
    case '-':
        if (piano->selected_octave == PIANO_OCTAVE_MIN) {
            return PIANO_EVENT_LIMIT;
        }
        --piano->selected_octave;
        return PIANO_EVENT_OCTAVE;
    case 'A':
        if (piano->selected_duration_ms == PIANO_DURATION_MAX_MS) {
            return PIANO_EVENT_LIMIT;
        }
        piano->selected_duration_ms += PIANO_DURATION_STEP_MS;
        return PIANO_EVENT_DURATION;
    case 'a':
        if (piano->selected_duration_ms == PIANO_DURATION_MIN_MS) {
            return PIANO_EVENT_LIMIT;
        }
        piano->selected_duration_ms -= PIANO_DURATION_STEP_MS;
        return PIANO_EVENT_DURATION;
    case '\r':
    case '\n':
        start_playback(piano, 0u, true, now_ms);
        return PIANO_EVENT_SCALE;
    default:
        return PIANO_EVENT_INVALID;
    }
}

void piano_update(piano_t *piano, uint32_t now_ms)
{
    if (!piano->playing) {
        return;
    }

    const uint32_t elapsed_ms = now_ms - piano->note_started_at_ms;
    if (elapsed_ms < piano->active_duration_ms) {
        return;
    }
    if (!piano->scale) {
        stop_playback(piano);
        return;
    }

    const uint32_t notes_elapsed = elapsed_ms / piano->active_duration_ms;
    const uint32_t notes_remaining = PIANO_NOTE_COUNT - piano->active_note;
    if (notes_elapsed >= notes_remaining) {
        stop_playback(piano);
        return;
    }

    piano->active_note = (uint8_t)(piano->active_note + notes_elapsed);
    piano->note_started_at_ms += notes_elapsed * piano->active_duration_ms;
}

uint32_t piano_frequency_millihz(const piano_t *piano)
{
    return piano->playing
        ? piano_note_frequency_millihz(piano->active_octave, piano->active_note)
        : 0u;
}

uint8_t piano_selected_octave(const piano_t *piano)
{
    return piano->selected_octave;
}

uint32_t piano_duration_ms(const piano_t *piano)
{
    return piano->selected_duration_ms;
}

bool piano_is_playing(const piano_t *piano)
{
    return piano->playing;
}

bool piano_is_scale(const piano_t *piano)
{
    return piano->playing && piano->scale;
}

uint8_t piano_current_note(const piano_t *piano)
{
    return piano->playing ? piano->active_note : PIANO_NO_NOTE;
}

uint8_t piano_playback_octave(const piano_t *piano)
{
    return piano->playing ? piano->active_octave : 0u;
}

uint32_t piano_playback_duration_ms(const piano_t *piano)
{
    return piano->playing ? piano->active_duration_ms : 0u;
}

uint32_t piano_note_frequency_millihz(uint8_t octave, uint8_t note)
{
    if (octave < PIANO_OCTAVE_MIN || octave > PIANO_OCTAVE_MAX
        || note >= PIANO_NOTE_COUNT) {
        return 0u;
    }
    return note_frequencies[octave - PIANO_OCTAVE_MIN][note];
}

const char *piano_note_name(uint8_t note)
{
    return note < PIANO_NOTE_COUNT ? note_names[note] : "?";
}

const char *piano_octave_name(uint8_t octave)
{
    if (octave < PIANO_OCTAVE_MIN || octave > PIANO_OCTAVE_MAX) {
        return "?";
    }
    return octave_names[octave - PIANO_OCTAVE_MIN];
}
