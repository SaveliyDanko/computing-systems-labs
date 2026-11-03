#include "i2c_bus.h"
#include "board.h"
#include "gpio_driver.h"

typedef enum { WAIT_FREE, START_W, ADDRESS_W, REGISTER_BYTE, VALUE_BYTE,
               WRITE_FINISHED, START_R, ADDRESS_R, RECEIVE_BYTE, STOP_FINISHED,
               ERROR_STOP, CLEAR_LOW, CLEAR_HIGH, CLEAR_STOP_LOW,
               CLEAR_STOP_HIGH, CLEAR_STOP_RELEASE } Phase;
static struct {
    I2cBusResult result;
    Phase phase;
    uint32_t started, recovery_started, phase_started;
    uint8_t address, reg, value, received, pulses;
    bool read;
    I2cBusStats stats;
} bus;

static bool configure_bus_pins(IoMode mode)
{
    const IoConfig released = {IO_INPUT, IO_NO_PULL, 0, false, 0};
    const IoConfig pins = {mode, IO_NO_PULL, 2, true,
                          mode == IO_ALTERNATE ? BOARD_I2C_AF : 0};
    const uint16_t mask = (uint16_t)((1U << BOARD_I2C_SCL_PIN) | (1U << BOARD_I2C_SDA_PIN));
    /* Stop peripheral drive before this call. Release explicitly for the GPIO
     * driver's AF/output handoff protection; preload both OD outputs high. */
    return io_configure(BOARD_I2C_PORT, mask, &released, 0) == IO_OK &&
           io_configure(BOARD_I2C_PORT, mask, &pins, mask) == IO_OK;
}

static void reset_peripheral(void)
{
    RCC->APB1RSTR |= RCC_APB1RSTR_I2C1RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C1RST;
    I2C1->CR1 = 0;
    I2C1->CR2 = BOARD_I2C_PCLK_HZ / 1000000U;
    I2C1->OAR1 = 1U << 14; /* RM0090: bit 14 must be kept at one. */
    I2C1->OAR2 = 0;
    I2C1->CCR = (BOARD_I2C_PCLK_HZ + 2U * BOARD_I2C_BAUD - 1U) / (2U * BOARD_I2C_BAUD);
    I2C1->TRISE = BOARD_I2C_PCLK_HZ / 1000000U + 1U;
    I2C1->FLTR = 0;
    I2C1->CR1 = I2C_CR1_PE;
}

bool i2c_bus_init(void)
{
    /* PD7 enables the PCA9517 bridge to the on-board I2C devices. The keyboard
     * expander's own nRESET is tied high; PD7 is not its reset input. */
    const IoConfig enable = {IO_OUTPUT, IO_NO_PULL, 0, false, 0};
    if (!io_clock_enable(BOARD_I2C_ENABLE_PORT) ||
        io_configure(BOARD_I2C_ENABLE_PORT, (uint16_t)(1U << BOARD_I2C_ENABLE_PIN),
                     &enable, (uint16_t)(1U << BOARD_I2C_ENABLE_PIN)) != IO_OK) return false;
    if (!io_clock_enable(BOARD_I2C_PORT) || !configure_bus_pins(IO_ALTERNATE)) return false;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    (void)RCC->APB1ENR;
    NVIC_DisableIRQ(I2C1_EV_IRQn);
    NVIC_DisableIRQ(I2C1_ER_IRQn);
    reset_peripheral();
    bus.result = I2C_BUS_IDLE;
    bus.stats = (I2cBusStats){0};
    return true;
}

static bool start(uint8_t address, uint8_t reg, uint8_t value, bool read, uint32_t now)
{
    if (bus.result == I2C_BUS_PENDING || address > 0x7FU) return false;
    bus.address = address;
    bus.reg = reg;
    bus.value = value;
    bus.read = read;
    bus.received = 0;
    bus.started = now;
    bus.phase = WAIT_FREE;
    bus.result = I2C_BUS_PENDING;
    return true;
}

bool i2c_bus_write_reg(uint8_t address, uint8_t reg, uint8_t value, uint32_t now_ms)
{ return start(address, reg, value, false, now_ms); }
bool i2c_bus_read_reg(uint8_t address, uint8_t reg, uint32_t now_ms)
{ return start(address, reg, 0, true, now_ms); }
I2cBusResult i2c_bus_result(void) { return bus.result; }
uint8_t i2c_bus_data(void) { return bus.received; }
I2cBusStats i2c_bus_stats(void) { return bus.stats; }

static void fail(uint32_t status, bool timeout, uint32_t now)
{
    ++bus.stats.errors;
    if (status & I2C_SR1_AF) ++bus.stats.nacks;
    if (timeout) ++bus.stats.timeouts;
    /* Let STOP actually reach the target before resetting the peripheral. */
    if (I2C1->SR2 & I2C_SR2_MSL) I2C1->CR1 |= I2C_CR1_STOP;
    bus.phase = ERROR_STOP;
    bus.phase_started = bus.recovery_started = now;
}

static void recovery_finished(void)
{
    I2C1->CR1 = 0;
    (void)configure_bus_pins(IO_ALTERNATE);
    reset_peripheral();
    bus.result = I2C_BUS_ERROR;
}

static void recover(uint32_t now)
{
    if ((uint32_t)(now - bus.recovery_started) >= I2C_BUS_RECOVERY_MAX_MS) {
        recovery_finished();
        return;
    }
    if (bus.phase == ERROR_STOP) {
        /* An interrupted one-byte reception can be stretching SCL on RXNE. */
        if (I2C1->SR1 & I2C_SR1_RXNE) (void)I2C1->DR;
        if (!(I2C1->CR1 & I2C_CR1_STOP) && !(I2C1->SR2 & I2C_SR2_BUSY)) {
            reset_peripheral();
            bus.result = I2C_BUS_ERROR;
        } else if ((uint32_t)(now - bus.phase_started) >= 2U) {
            I2C1->CR1 = 0;
            if (!configure_bus_pins(IO_OUTPUT)) { recovery_finished(); return; }
            io_write_pin(BOARD_I2C_PORT, BOARD_I2C_SCL_PIN, false);
            bus.pulses = 0;
            bus.phase = CLEAR_LOW;
            bus.phase_started = now;
        }
        return;
    }
    if ((uint32_t)(now - bus.phase_started) < 1U) return;
    switch (bus.phase) {
    case CLEAR_LOW:
        io_write_pin(BOARD_I2C_PORT, BOARD_I2C_SCL_PIN, true);
        bus.phase = CLEAR_HIGH;
        break;
    case CLEAR_HIGH:
        /* OD HIGH releases the line. If a device holds SCL low, do not count
         * a clock; the overall recovery deadline still bounds this state. */
        if (!io_read_pin(BOARD_I2C_PORT, BOARD_I2C_SCL_PIN)) return;
        if (++bus.pulses < 9U) {
            io_write_pin(BOARD_I2C_PORT, BOARD_I2C_SCL_PIN, false);
            bus.phase = CLEAR_LOW;
        } else {
            io_write_pin(BOARD_I2C_PORT, BOARD_I2C_SCL_PIN, false);
            io_write_pin(BOARD_I2C_PORT, BOARD_I2C_SDA_PIN, false);
            bus.phase = CLEAR_STOP_LOW;
        }
        break;
    case CLEAR_STOP_LOW:
        io_write_pin(BOARD_I2C_PORT, BOARD_I2C_SCL_PIN, true);
        bus.phase = CLEAR_STOP_HIGH;
        break;
    case CLEAR_STOP_HIGH:
        if (!io_read_pin(BOARD_I2C_PORT, BOARD_I2C_SCL_PIN)) return;
        io_write_pin(BOARD_I2C_PORT, BOARD_I2C_SDA_PIN, true);
        bus.phase = CLEAR_STOP_RELEASE;
        break;
    case CLEAR_STOP_RELEASE:
        recovery_finished();
        return;
    default: recovery_finished(); return;
    }
    bus.phase_started = now;
}

void i2c_bus_service(uint32_t now_ms)
{
    if (bus.result != I2C_BUS_PENDING) return;
    if (bus.phase >= ERROR_STOP) { recover(now_ms); return; }
    uint32_t status = I2C1->SR1;
    /* ES0206 2.10.1: a lone BERR can be spurious; clear it and continue. */
    if ((status & I2C_SR1_BERR) &&
        !(status & (I2C_SR1_ARLO | I2C_SR1_AF | I2C_SR1_OVR | I2C_SR1_TIMEOUT))) {
        I2C1->SR1 &= ~I2C_SR1_BERR;
        ++bus.stats.errors;
        status &= ~I2C_SR1_BERR;
    }
    if (status & (I2C_SR1_ARLO | I2C_SR1_AF | I2C_SR1_OVR | I2C_SR1_TIMEOUT)) {
        fail(status, false, now_ms);
        return;
    }
    if ((uint32_t)(now_ms - bus.started) >= I2C_BUS_TIMEOUT_MS) {
        fail(status, true, now_ms);
        return;
    }
    switch (bus.phase) {
    case WAIT_FREE:
        if (!(I2C1->SR2 & I2C_SR2_BUSY)) {
            I2C1->CR1 |= I2C_CR1_START;
            bus.phase = START_W;
        }
        break;
    case START_W:
        if (status & I2C_SR1_SB) {
            I2C1->DR = (uint32_t)bus.address << 1;
            bus.phase = ADDRESS_W;
        }
        break;
    case ADDRESS_W:
        if (status & I2C_SR1_ADDR) {
            (void)I2C1->SR2; /* SR1 was read above; SR1 -> SR2 clears ADDR. */
            bus.phase = REGISTER_BYTE;
        }
        break;
    case REGISTER_BYTE:
        if (status & I2C_SR1_TXE) {
            I2C1->DR = bus.reg;
            bus.phase = bus.read ? WRITE_FINISHED : VALUE_BYTE;
        }
        break;
    case VALUE_BYTE:
        if (status & I2C_SR1_TXE) {
            I2C1->DR = bus.value;
            bus.phase = WRITE_FINISHED;
        }
        break;
    case WRITE_FINISHED:
        if (status & I2C_SR1_BTF) {
            if (bus.read) {
                I2C1->CR1 |= I2C_CR1_START;
                bus.phase = START_R;
            } else {
                I2C1->CR1 |= I2C_CR1_STOP;
                bus.phase = STOP_FINISHED;
            }
        }
        break;
    case START_R:
        if (status & I2C_SR1_SB) {
            I2C1->DR = ((uint32_t)bus.address << 1) | 1U;
            bus.phase = ADDRESS_R;
        }
        break;
    case ADDRESS_R:
        if (status & I2C_SR1_ADDR) {
            /* RM0090 one-byte master reception: clear ACK, clear ADDR, STOP.
             * Keep that short sequence indivisible from SysTick/UART IRQ. */
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            I2C1->CR1 &= ~I2C_CR1_ACK;
            (void)I2C1->SR2;
            I2C1->CR1 |= I2C_CR1_STOP;
            __set_PRIMASK(primask);
            bus.phase = RECEIVE_BYTE;
        }
        break;
    case RECEIVE_BYTE:
        if (status & I2C_SR1_RXNE) {
            bus.received = (uint8_t)I2C1->DR;
            bus.phase = STOP_FINISHED;
        }
        break;
    case STOP_FINISHED:
        if (!(I2C1->CR1 & I2C_CR1_STOP) && !(I2C1->SR2 & I2C_SR2_BUSY))
            bus.result = I2C_BUS_OK;
        break;
    default: break; /* Recovery phases are handled above. */
    }
}
