#ifndef SOUND_H
#define SOUND_H
#include <stdbool.h>
#include <stdint.h>
bool sound_init(void);

bool sound_set_frequency(uint32_t frequency_millihz);
void sound_stop(void);

uint32_t sound_period_ticks(uint32_t frequency_millihz);
#endif
