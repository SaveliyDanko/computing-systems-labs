#ifndef TRAFFIC_H
#define TRAFFIC_H
#include <stdbool.h>
#include <stdint.h>
enum { GREEN_MS = 3000, RED_MS = 12000, BLINK_MS = 3000,
       BLINK_HALF_MS = 500, YELLOW_MS = 1000, MAX_TIMEOUT_SECONDS = 86400 };
typedef enum { TRAFFIC_RED, TRAFFIC_GREEN, TRAFFIC_BLINK, TRAFFIC_YELLOW } TrafficState;
typedef enum { LIGHT_OFF, LIGHT_RED, LIGHT_GREEN, LIGHT_YELLOW } Light;
typedef struct {
    TrafficState state;
    uint32_t since, red_ms;
    bool request;
    uint8_t mode;
} Traffic;
void traffic_init(Traffic *t, uint32_t now);
void traffic_update(Traffic *t, bool press_event, uint32_t now);
Light traffic_light(const Traffic *t, uint32_t now);
bool traffic_set_timeout(Traffic *t, uint32_t seconds);
bool traffic_set_mode(Traffic *t, uint8_t mode);
const char *traffic_name(const Traffic *t);
#endif
