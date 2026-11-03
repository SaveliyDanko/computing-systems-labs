#ifndef CONSOLE_H
#define CONSOLE_H
#include "piano.h"
#include <stdint.h>
#define CONSOLE_REPLY_RESERVE 192U
typedef struct { uint32_t rx_epoch; uint8_t reported_scale_note; } Console;
void console_init(Console *console);
void console_update(Console *console, piano_t *piano, uint32_t now_ms);
#endif
