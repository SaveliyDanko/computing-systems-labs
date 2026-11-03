#ifndef CLOCK_H
#define CLOCK_H
#include <stdbool.h>
/* HAL_Init must have configured the time base before this call. */
bool clock_init(void);
#endif
