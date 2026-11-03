#ifndef SOUND_H
#define SOUND_H
#include <stdbool.h>
#include <stdint.h>
bool sound_init(void);
/* Frequency in millihertz; zero means silence. Invalid input leaves sound intact. */
bool sound_set_frequency(uint32_t frequency_millihz);
void sound_stop(void);
/* Pure calculation: round(timer_clock * 1000 / frequency), 0 on invalid input. */
uint32_t sound_period_ticks(uint32_t frequency_millihz);
#endif
