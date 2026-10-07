#ifndef COMMAND_H
#define COMMAND_H
#include <stdint.h>
typedef enum { CMD_ERROR, CMD_STATUS, CMD_MODE, CMD_TIMEOUT, CMD_INTERRUPTS } CommandType;
typedef struct { CommandType type; uint32_t value; } Command;
Command command_parse(const char *line);
#endif
