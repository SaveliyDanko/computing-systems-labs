#include "traffic.h"
void traffic_init(Traffic *t, uint32_t now)
{
    *t = (Traffic){.state = TRAFFIC_RED, .since = now, .request = false};
}
void traffic_update(Traffic *t, bool press, uint32_t now)
{
    if (press && t->state != TRAFFIC_GREEN) t->request = true;
    uint32_t duration;
    switch (t->state) {
    case TRAFFIC_RED: duration = t->request ? SHORT_RED_MS : RED_MS; break;
    case TRAFFIC_GREEN: duration = GREEN_MS; break;
    case TRAFFIC_BLINK: duration = BLINK_MS; break;
    default: duration = YELLOW_MS; break;
    }
    if ((uint32_t)(now - t->since) < duration) return;
    if (t->state == TRAFFIC_RED) t->request = false;
    t->state = (TrafficState)(((unsigned)t->state + 1U) % 4U);
    t->since = now;
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
