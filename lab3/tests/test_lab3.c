#include "piano.h"
#include "sound.h"
#include "clock.h"
#include "console.h"
#include "serial.h"
#include "stm32f4xx_hal.h"
#include <string.h>

volatile uint32_t test_checks, test_failure, test_done;
piano_t test_piano;
static Console test_console;
#define CHECK(x) do { ++test_checks; if (!(x)) { test_failure = __LINE__; test_done = 1; return; } } while (0)

bool test_boot(void)
{
    SystemCoreClock = 16000000U;
    if (HAL_Init() != HAL_OK || !clock_init() || !sound_init()) return false;
    piano_init(&test_piano);
    if (!serial_init(true)) return false;
    console_init(&test_console);
    return true;
}

void test_step(uint32_t now)
{
    serial_service();
    piano_update(&test_piano, now);
    console_update(&test_console, &test_piano, now);
    (void)sound_set_frequency(piano_frequency_millihz(&test_piano));
}

uint32_t test_octave(void) { return piano_selected_octave(&test_piano); }
uint32_t test_frequency(void) { return piano_frequency_millihz(&test_piano); }
uint32_t test_duration(void) { return piano_duration_ms(&test_piano); }
uint32_t test_active(void) { return piano_is_playing(&test_piano); }
uint32_t test_gamma(void) { return piano_is_scale(&test_piano); }
uint32_t test_note(void) { return piano_current_note(&test_piano); }
uint32_t test_rx_errors(void) { return serial_stats().rx_errors; }
uint32_t test_overflows(void) { return serial_stats().rx_overflows; }

void test_entry(void)
{
    piano_t p;
    piano_init(&p);
    CHECK(piano_selected_octave(&p) == 5 && piano_duration_ms(&p) == 1000);
    CHECK(!piano_is_playing(&p) && !piano_is_scale(&p));
    CHECK(piano_frequency_millihz(&p) == 0 && piano_current_note(&p) == PIANO_NO_NOTE);
    CHECK(piano_playback_octave(&p) == 0 && piano_playback_duration_ms(&p) == 0);
    CHECK(piano_note_frequency_millihz(5, 5) == 440000);
    CHECK(piano_note_frequency_millihz(1, 0) == 16352);
    CHECK(piano_note_frequency_millihz(9, 6) == 7902133);
    CHECK(!piano_note_frequency_millihz(0, 0));
    CHECK(!piano_note_frequency_millihz(10, 0));
    CHECK(!piano_note_frequency_millihz(5, 7));
    CHECK(!strcmp(piano_note_name(7), "?"));
    CHECK(!strcmp(piano_octave_name(0), "?"));
    for (uint8_t octave = 1; octave <= 9; ++octave) {
        for (uint8_t note = 0; note < 7; ++note) {
            uint32_t frequency = piano_note_frequency_millihz(octave, note);
            CHECK(frequency > 0);
            if (note) CHECK(frequency > piano_note_frequency_millihz(octave, note - 1));
            if (octave < 9) {
                uint32_t next = piano_note_frequency_millihz(octave + 1, note);
                CHECK(next + 1 >= frequency * 2 && next <= frequency * 2 + 1);
            }
        }
    }
    for (uint8_t note = 0; note < 7; ++note) {
        CHECK(piano_input(&p, (uint8_t)('1' + note), 100) == PIANO_EVENT_NOTE);
        CHECK(piano_current_note(&p) == note && !piano_is_scale(&p));
        CHECK(piano_frequency_millihz(&p) == piano_note_frequency_millihz(5, note));
        piano_update(&p, 1099);
        CHECK(piano_is_playing(&p));
        piano_update(&p, 1100);
        CHECK(!piano_is_playing(&p) && !piano_frequency_millihz(&p));
    }
    piano_init(&p);
    for (unsigned i = 0; i < 4; ++i) CHECK(piano_input(&p, '-', 0) == PIANO_EVENT_OCTAVE);
    CHECK(piano_selected_octave(&p) == 1);
    CHECK(piano_input(&p, '-', 0) == PIANO_EVENT_LIMIT && piano_selected_octave(&p) == 1);
    for (unsigned i = 0; i < 8; ++i) CHECK(piano_input(&p, '+', 0) == PIANO_EVENT_OCTAVE);
    CHECK(piano_selected_octave(&p) == 9);
    CHECK(piano_input(&p, '+', 0) == PIANO_EVENT_LIMIT && piano_selected_octave(&p) == 9);
    piano_init(&p);
    for (unsigned i = 0; i < 9; ++i) CHECK(piano_input(&p, 'a', 0) == PIANO_EVENT_DURATION);
    CHECK(piano_duration_ms(&p) == 100);
    CHECK(piano_input(&p, 'a', 0) == PIANO_EVENT_LIMIT && piano_duration_ms(&p) == 100);
    for (unsigned i = 0; i < 49; ++i) CHECK(piano_input(&p, 'A', 0) == PIANO_EVENT_DURATION);
    CHECK(piano_duration_ms(&p) == 5000);
    CHECK(piano_input(&p, 'A', 0) == PIANO_EVENT_LIMIT && piano_duration_ms(&p) == 5000);

    piano_init(&p);
    CHECK(piano_input(&p, '6', 100) == PIANO_EVENT_NOTE);
    CHECK(piano_input(&p, '+', 200) == PIANO_EVENT_OCTAVE);
    CHECK(piano_input(&p, 'A', 300) == PIANO_EVENT_DURATION);
    CHECK(piano_selected_octave(&p) == 6 && piano_duration_ms(&p) == 1100);
    CHECK(piano_frequency_millihz(&p) == 440000);
    CHECK(piano_playback_octave(&p) == 5 && piano_playback_duration_ms(&p) == 1000);
    piano_update(&p, 1099);
    CHECK(piano_is_playing(&p));
    piano_update(&p, 1100);
    CHECK(!piano_is_playing(&p));
    CHECK(piano_input(&p, '6', 1100) == PIANO_EVENT_NOTE);
    CHECK(piano_frequency_millihz(&p) == 880000 && piano_playback_duration_ms(&p) == 1100);

    piano_init(&p);
    CHECK(piano_input(&p, '\r', 100) == PIANO_EVENT_SCALE);
    CHECK(piano_input(&p, '\n', 200) == PIANO_EVENT_IGNORED);
    CHECK(piano_input(&p, '+', 300) == PIANO_EVENT_OCTAVE);
    CHECK(piano_input(&p, 'A', 400) == PIANO_EVENT_DURATION);
    for (uint8_t note = 0; note < 7; ++note) {
        piano_update(&p, note == 0 ? 400U : 100U + note * 1000U);
        CHECK(piano_is_scale(&p) && piano_current_note(&p) == note);
        CHECK(piano_frequency_millihz(&p) == piano_note_frequency_millihz(5, note));
        CHECK(piano_playback_duration_ms(&p) == 1000 && piano_playback_octave(&p) == 5);
    }
    piano_update(&p, 7099);
    CHECK(piano_is_scale(&p) && piano_is_playing(&p));
    piano_update(&p, 7100);
    CHECK(!piano_is_scale(&p) && !piano_is_playing(&p));
    CHECK(piano_input(&p, '\n', 8000) == PIANO_EVENT_SCALE);
    CHECK(piano_playback_duration_ms(&p) == 1100 && piano_playback_octave(&p) == 6);
    piano_update(&p, 11301);
    CHECK(piano_current_note(&p) == 3);
    piano_update(&p, 12399);
    CHECK(piano_current_note(&p) == 3);
    piano_update(&p, 12400);
    CHECK(piano_current_note(&p) == 4);
    CHECK(piano_input(&p, '3', 12401) == PIANO_EVENT_NOTE);
    CHECK(!piano_is_scale(&p) && piano_current_note(&p) == 2);
    CHECK(piano_input(&p, '\r', 12500) == PIANO_EVENT_SCALE && piano_current_note(&p) == 0);
    CHECK(piano_input(&p, 'x', 12501) == PIANO_EVENT_INVALID);
    CHECK(piano_input(&p, '\n', 12502) == PIANO_EVENT_SCALE);

    piano_init(&p);
    CHECK(piano_input(&p, '1', UINT32_MAX - 499U) == PIANO_EVENT_NOTE);
    piano_update(&p, 499);
    CHECK(piano_is_playing(&p));
    piano_update(&p, 500);
    CHECK(!piano_is_playing(&p));
    CHECK(piano_input(&p, '\n', UINT32_MAX - 499U) == PIANO_EVENT_SCALE);
    piano_update(&p, 2500);
    CHECK(piano_current_note(&p) == 3 && piano_is_scale(&p));
    piano_update(&p, 6500);
    CHECK(!piano_is_playing(&p) && piano_frequency_millihz(&p) == 0);

    CHECK(sound_init());
    CHECK((GPIOA->MODER & (3U << 10)) == (2U << 10));
    CHECK(((GPIOA->AFR[0] >> 20) & 15U) == 1);
    CHECK(TIM2->PSC == 0 && TIM2->DIER == 0);
    CHECK((TIM2->CCMR1 & TIM_CCMR1_OC1M) == (6U << 4));
    CHECK((TIM2->CCMR1 & TIM_CCMR1_OC1PE) && (TIM2->CR1 & TIM_CR1_ARPE));
    CHECK((TIM2->CCER & TIM_CCER_CC1E) && !(TIM2->CR1 & TIM_CR1_CEN));
    CHECK(sound_period_ticks(440000) == 136364);
    CHECK(sound_period_ticks(16352) == 3669276);
    CHECK(!sound_period_ticks(0) && !sound_period_ticks(UINT32_MAX));
    CHECK(sound_set_frequency(440000));
    CHECK(TIM2->ARR == 136363 && TIM2->CCR1 == 68182 && (TIM2->CR1 & TIM_CR1_CEN));
    CHECK(sound_set_frequency(16352));
    CHECK(TIM2->ARR > 65535 && TIM2->ARR == 3669275);
    uint32_t previous = TIM2->ARR;
    CHECK(!sound_set_frequency(UINT32_MAX) && TIM2->ARR == previous);
    sound_stop();
    CHECK(TIM2->CCR1 == 0 && !(TIM2->CR1 & TIM_CR1_CEN) && TIM2->CNT == 0);
    CHECK(sound_set_frequency(880000));
    CHECK(sound_set_frequency(0));
    CHECK(TIM2->CCR1 == 0 && !(TIM2->CR1 & TIM_CR1_CEN));
    test_done = 1;
}
