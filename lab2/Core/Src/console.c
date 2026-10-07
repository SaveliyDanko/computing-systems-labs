#include "console.h"
#include "command.h"
#include "serial.h"
#include <stdio.h>
#include <string.h>
_Static_assert(SERIAL_TX_CAPACITY >= CONSOLE_REPLY_RESERVE, "Console needs room for a complete reply");
static void reply(const char *text) { (void)serial_write(text, strlen(text)); }
void console_init(Console *c) { *c = (Console){.rx_epoch = serial_rx_epoch()}; }
static void execute(Console *c, Traffic *t)
{
    c->line[c->length] = 0;
    Command command = command_parse(c->line);
    switch (command.type) {
    case CMD_STATUS: {
        char response[CONSOLE_REPLY_RESERVE - 1];
        int size = snprintf(response, sizeof(response), "\r\n%s, mode %u, timeout %lu, %c\r\n",
            traffic_name(t), (unsigned)t->mode, (unsigned long)(t->red_ms / 1000U),
            serial_interrupts() ? 'I' : 'P');
        if (size > 0 && (size_t)size < sizeof(response)) (void)serial_write(response, (size_t)size);
        break;
    }
    case CMD_MODE: (void)traffic_set_mode(t, (uint8_t)command.value); reply("\r\nOK\r\n"); break;
    case CMD_TIMEOUT: (void)traffic_set_timeout(t, command.value); reply("\r\nOK\r\n"); break;
    case CMD_INTERRUPTS:
        serial_request_mode(command.value != 0);
        c->switching = true;
        break;
    default: reply("\r\nERROR: unsupported command or invalid argument\r\n"); break;
    }
}
void console_update(Console *c, Traffic *t)
{
    uint32_t epoch = serial_rx_epoch();
    if (epoch != c->rx_epoch) {
        c->rx_epoch = epoch;
        c->discard = true;
        c->length = 0;
    }
    if (c->switching) {
        if (serial_switch_pending() || serial_tx_free() < CONSOLE_REPLY_RESERVE) return;
        reply("\r\nOK\r\n");
        c->switching = false;
    }
    for (unsigned i = 0; i < 16 && serial_tx_free() >= CONSOLE_REPLY_RESERVE; ++i) {
        uint8_t byte;
        if (!serial_read(&byte)) return;
        /* RX may have faulted in an ISR since the start of this batch. */
        epoch = serial_rx_epoch();
        if (epoch != c->rx_epoch) {
            c->rx_epoch = epoch;
            c->discard = true;
            c->length = 0;
        }
        (void)serial_write(&byte, 1);
        if (byte == '\n' && c->after_cr) { c->after_cr = false; continue; }
        c->after_cr = byte == '\r';
        if (byte == '\r' || byte == '\n') {
            if (c->discard) reply("\r\nERROR: input lost or line too long; repeat command\r\n");
            else if (c->length) execute(c, t);
            c->discard = false;
            c->length = 0;
            if (c->switching) return;
        } else if (byte == 8 || byte == 127) {
            if (!c->discard && c->length) --c->length;
        } else if (byte < 32 || byte > 126) {
            c->discard = true;
        } else if (!c->discard) {
            if (c->length + 1U < CONSOLE_LINE_CAPACITY) c->line[c->length++] = (char)byte;
            else c->discard = true;
        }
    }
}
