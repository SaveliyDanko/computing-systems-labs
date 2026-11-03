#ifndef SERIAL_H
#define SERIAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef SERIAL_RX_CAPACITY
#define SERIAL_RX_CAPACITY 128U
#endif
#ifndef SERIAL_TX_CAPACITY
#define SERIAL_TX_CAPACITY 512U
#endif
typedef struct { uint32_t rx_overflows, rx_errors, tx_errors; } SerialStats;
bool serial_init(bool interrupts);
/* Main only. Enqueue all bytes or none; never wait for the wire. */
bool serial_write(const void *data, size_t size);
bool serial_read(uint8_t *byte);
size_t serial_tx_free(void);
void serial_service(void);
void serial_request_mode(bool interrupts);
bool serial_interrupts(void);
bool serial_switch_pending(void);
bool serial_idle(void);
SerialStats serial_stats(void);
uint32_t serial_rx_epoch(void);
#endif
