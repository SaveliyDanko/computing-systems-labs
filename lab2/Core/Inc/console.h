#ifndef CONSOLE_H
#define CONSOLE_H
#include "traffic.h"
#include <stddef.h>
enum { CONSOLE_LINE_CAPACITY = 64, CONSOLE_REPLY_RESERVE = 96 };
typedef struct {
    char line[CONSOLE_LINE_CAPACITY];
    size_t length;
    uint32_t rx_epoch;
    bool discard, after_cr, switching;
} Console;
void console_init(Console *c);
void console_update(Console *c, Traffic *t);
#endif
