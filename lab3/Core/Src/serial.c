#include "serial.h"
#include "uart_backend.h"
_Static_assert(SERIAL_RX_CAPACITY > 0 && SERIAL_RX_CAPACITY <= 65535, "RX capacity");
_Static_assert(SERIAL_TX_CAPACITY > 0 && SERIAL_TX_CAPACITY <= 65535, "TX capacity");
static uint8_t rx[SERIAL_RX_CAPACITY], tx[SERIAL_TX_CAPACITY], tx_byte;
static volatile uint16_t rx_head, rx_tail, rx_count, tx_head, tx_tail, tx_count;
static volatile bool irq_mode, target_mode, busy;
static volatile uint32_t rx_epoch;
static volatile SerialStats stats;
static uint32_t lock(void) { uint32_t p = __get_PRIMASK(); __disable_irq(); return p; }
static void unlock(uint32_t p) { __set_PRIMASK(p); }
bool serial_init(bool interrupts)
{
    rx_head = rx_tail = rx_count = tx_head = tx_tail = tx_count = 0;
    busy = false;
    rx_epoch = 0;
    stats = (SerialStats){0};
    irq_mode = target_mode = interrupts;
    return uart_backend_init() && uart_backend_mode(interrupts);
}
bool serial_write(const void *data, size_t size)
{
    if (!data && size) return false;
    /* TX indices/count are owned by main; ISR only clears busy.
     * Copy with interrupts enabled so a large write cannot stall RX. */
    if (size > SERIAL_TX_CAPACITY - tx_count) return false;
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        tx[tx_head] = bytes[i];
        tx_head = (uint16_t)((tx_head + 1U) % SERIAL_TX_CAPACITY);
    }
    tx_count += (uint16_t)size;
    return true;
}
bool serial_read(uint8_t *byte)
{
    if (!byte) return false;
    uint32_t p = lock();
    if (!rx_count) { unlock(p); return false; }
    *byte = rx[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % SERIAL_RX_CAPACITY);
    --rx_count;
    unlock(p);
    return true;
}
size_t serial_tx_free(void)
{
    uint32_t p = lock();
    size_t result = SERIAL_TX_CAPACITY - tx_count;
    unlock(p);
    return result;
}
void serial_received(uint8_t byte)
{
    if (rx_count == SERIAL_RX_CAPACITY) {
        ++stats.rx_overflows;
        ++rx_epoch;
        rx_count = 0;
        rx_tail = rx_head;
        return;
    }
    rx[rx_head] = byte;
    rx_head = (uint16_t)((rx_head + 1U) % SERIAL_RX_CAPACITY);
    ++rx_count;
}
void serial_receive_error(void)
{
    ++stats.rx_errors;
    ++rx_epoch;
    rx_count = 0;
    rx_tail = rx_head;
}
void serial_transmit_error(void) { ++stats.tx_errors; }
void serial_transmitted(void) { busy = false; }
bool serial_idle(void) { return !tx_count && !busy && uart_backend_idle(); }
void serial_request_mode(bool interrupts) { target_mode = interrupts; }
bool serial_interrupts(void) { return irq_mode; }
bool serial_switch_pending(void) { return irq_mode != target_mode; }
uint32_t serial_rx_epoch(void) { return rx_epoch; }
SerialStats serial_stats(void)
{
    uint32_t p = lock();
    SerialStats result = stats;
    unlock(p);
    return result;
}
void serial_service(void)
{
    if (serial_switch_pending() && serial_idle()) {
        uint32_t p = lock();
        if (uart_backend_mode(target_mode)) irq_mode = target_mode;
        unlock(p);
    }
    if (!irq_mode) {
        uint8_t byte;
        int status = uart_backend_receive(&byte);
        if (status > 0) serial_received(byte);
        else if (status < 0) serial_receive_error();
    }
    if (!tx_count || busy) return;
    if (irq_mode) {
        uint32_t p = lock();
        tx_byte = tx[tx_tail];
        busy = true;
        if (uart_backend_transmit(&tx_byte, true)) {
            tx_tail = (uint16_t)((tx_tail + 1U) % SERIAL_TX_CAPACITY);
            --tx_count;
        } else busy = false;
        unlock(p);
    } else {
        /* SysTick remains enabled during bounded HAL polling transfers. */
        tx_byte = tx[tx_tail];
        if (uart_backend_transmit(&tx_byte, false)) {
            tx_tail = (uint16_t)((tx_tail + 1U) % SERIAL_TX_CAPACITY);
            --tx_count;
        }
    }
}
