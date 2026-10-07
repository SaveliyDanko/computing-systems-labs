#include "stm32f4xx_hal.h"
#include "board.h"
#include "led.h"
#include "button.h"
#include "traffic.h"
#include "serial.h"
#include "console.h"

/* These symbols can be inspected in CubeIDE's Expressions view. */
Traffic traffic;
Button button;
volatile uint32_t lab2_fault;
static LedBank leds;
Console console;

static void fatal(uint32_t code)
{
    lab2_fault = code;
    for (;;) { /* Configuration failure only; normal processes never wait. */ }
}

static void SystemClock_Config(void)
{
    /* HSI 16 MHz is sufficient for GPIO; does not depend on board crystal.
     * HAL_RCC_ClockConfig recalibrates the 1 ms SysTick time base. */
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    /* OpenOCD may temporarily use PLL during reset-init/Flash programming. */
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) fatal(1);
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK) fatal(2);
    osc.PLL.PLLState = RCC_PLL_OFF;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) fatal(6);
}

int main(void)
{
    SystemCoreClockUpdate();
    if (HAL_Init() != HAL_OK) fatal(3);
    SystemClock_Config();
    if (!led_init(&leds, BOARD_LED_PORT, BOARD_LED_MASK, 0)) fatal(4);
    const IoConfig input = {IO_INPUT, IO_NO_PULL, 0, false, 0};
    if (!io_clock_enable(BOARD_BUTTON_PORT) ||
        io_configure(BOARD_BUTTON_PORT, (uint16_t)(1U << BOARD_BUTTON_PIN), &input, 0) != IO_OK)
        fatal(5);
    uint32_t now = HAL_GetTick();
    /* External pull-up R11: a closed button reads zero. */
    button_init(&button, !io_read_pin(BOARD_BUTTON_PORT, BOARD_BUTTON_PIN), now, BOARD_DEBOUNCE_MS);
    traffic_init(&traffic, now);
    if (!serial_init(true)) fatal(7);
    console_init(&console);
    static const char greeting[] = "\r\nLab2 traffic: 57600 8N1, mode 1, timeout 12, I\r\n";
    (void)serial_write(greeting, sizeof(greeting) - 1U);
    for (;;) {
        serial_service();
        console_update(&console, &traffic);
        now = HAL_GetTick();
        bool press = button_update(&button, !io_read_pin(BOARD_BUTTON_PORT, BOARD_BUTTON_PIN), now);
        traffic_update(&traffic, press, now);
        uint16_t bits = 0;
        switch (traffic_light(&traffic, now)) {
        case LIGHT_RED: bits = BOARD_RED; break;
        case LIGHT_GREEN: bits = BOARD_GREEN; break;
        case LIGHT_YELLOW: bits = BOARD_YELLOW; break;
        default: break;
        }
        led_write(&leds, bits);
    }
}

void SysTick_Handler(void) { HAL_IncTick(); }
void HardFault_Handler(void) { fatal(0xF1); }
void MemManage_Handler(void) { fatal(0xF2); }
void BusFault_Handler(void) { fatal(0xF3); }
void UsageFault_Handler(void) { fatal(0xF4); }
