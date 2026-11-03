#include "button.h"
void button_init(Button *b, bool pressed, uint32_t now, uint32_t debounce_ms)
{
    *b = (Button){.changed_at = now, .debounce_ms = debounce_ms,
                  .candidate = pressed, .pressed = pressed};
}
bool button_update(Button *b, bool raw, uint32_t now)
{
    if (raw != b->candidate) {
        b->candidate = raw;
        b->changed_at = now;
    }
    if (b->candidate != b->pressed && (uint32_t)(now - b->changed_at) >= b->debounce_ms) {
        b->pressed = b->candidate;
        return b->pressed;
    }
    return false;
}
