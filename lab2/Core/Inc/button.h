#ifndef BUTTON_H
#define BUTTON_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t changed_at, debounce_ms;
    bool candidate, pressed;
} Button;
void button_init(Button *b, bool pressed, uint32_t now, uint32_t debounce_ms);
bool button_update(Button *b, bool raw_pressed, uint32_t now);
#endif
