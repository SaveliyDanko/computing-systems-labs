#include "command.h"
#include "traffic.h"
#include <string.h>
Command command_parse(const char *line)
{
    if (!line) return (Command){CMD_ERROR, 0};
    if (!strcmp(line, "?")) return (Command){CMD_STATUS, 0};
    if (!strcmp(line, "set mode 1")) return (Command){CMD_MODE, 1};
    if (!strcmp(line, "set mode 2")) return (Command){CMD_MODE, 2};
    if (!strcmp(line, "set interrupts on")) return (Command){CMD_INTERRUPTS, 1};
    if (!strcmp(line, "set interrupts off")) return (Command){CMD_INTERRUPTS, 0};
    if (!strncmp(line, "set timeout ", 12)) {
        const char *p = line + 12;
        uint32_t value = 0;
        if (!*p) return (Command){CMD_ERROR, 0};
        for (; *p; ++p) {
            if (*p < '0' || *p > '9') return (Command){CMD_ERROR, 0};
            unsigned digit = (unsigned)(*p - '0');
            if (value > (MAX_TIMEOUT_SECONDS - digit) / 10U) return (Command){CMD_ERROR, 0};
            value = value * 10U + digit;
        }
        if (value) return (Command){CMD_TIMEOUT, value};
    }
    return (Command){CMD_ERROR, 0};
}
