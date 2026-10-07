#include "traffic.h"
void traffic_init(Traffic *t, uint32_t now)
{
    *t = (Traffic){.state = TRAFFIC_RED, .since = now, .red_ms = RED_MS, .mode = 1};
}
bool traffic_set_timeout(Traffic *t, uint32_t seconds)
{
    if (!seconds || seconds > MAX_TIMEOUT_SECONDS) return false;
    t->red_ms = seconds * 1000U;
    return true;
}
bool traffic_set_mode(Traffic *t, uint8_t mode)
{
    if (mode != 1 && mode != 2) return false;
    t->mode = mode;
    if (mode == 2) t->request = false;
    return true;
}
void traffic_update(Traffic *t, bool press, uint32_t now)
{
    /* Event belongs to the state visible when sampled, before transition. */
    if (press && t->mode == 1 && t->state != TRAFFIC_GREEN) t->request = true;
    uint32_t duration;
    switch (t->state) {
    case TRAFFIC_RED: duration = t->request ? t->red_ms / 4U : t->red_ms; break;
    case TRAFFIC_GREEN: duration = GREEN_MS; break;
    case TRAFFIC_BLINK: duration = BLINK_MS; break;
    default: duration = YELLOW_MS; break;
    }
    if ((uint32_t)(now - t->since) < duration) return;
    if (t->state == TRAFFIC_RED) t->request = false; /* consumed by this red */
    t->state = (TrafficState)(((unsigned)t->state + 1U) % 4U);
    t->since = now; /* no shortened visible phases after a debugger pause */
}
Light traffic_light(const Traffic *t, uint32_t now)
{
    switch (t->state) {
    case TRAFFIC_RED: return LIGHT_RED;
    case TRAFFIC_GREEN: return LIGHT_GREEN;
    case TRAFFIC_YELLOW: return LIGHT_YELLOW;
    case TRAFFIC_BLINK:
        return ((uint32_t)(now - t->since) / BLINK_HALF_MS) % 2U ? LIGHT_OFF : LIGHT_GREEN;
    default: return LIGHT_OFF;
    }
}
const char *traffic_name(const Traffic *t)
{
    switch (t->state) {
    case TRAFFIC_RED: return "red";
    case TRAFFIC_GREEN: return "green";
    case TRAFFIC_BLINK: return "blinking green";
    case TRAFFIC_YELLOW: return "yellow";
    default: return "unknown";
    }
}
