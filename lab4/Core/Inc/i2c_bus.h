#ifndef I2C_BUS_H
#define I2C_BUS_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { I2C_BUS_IDLE, I2C_BUS_PENDING, I2C_BUS_OK, I2C_BUS_ERROR } I2cBusResult;
typedef struct { uint32_t errors, nacks, timeouts; } I2cBusStats;
#define I2C_BUS_TIMEOUT_MS 10U
#define I2C_BUS_RECOVERY_MAX_MS 30U

bool i2c_bus_init(void);
bool i2c_bus_write_reg(uint8_t address, uint8_t reg, uint8_t value, uint32_t now_ms);
bool i2c_bus_read_reg(uint8_t address, uint8_t reg, uint32_t now_ms);
void i2c_bus_service(uint32_t now_ms);
I2cBusResult i2c_bus_result(void);
uint8_t i2c_bus_data(void);
I2cBusStats i2c_bus_stats(void);
#endif
