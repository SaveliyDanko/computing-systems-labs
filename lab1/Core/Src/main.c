#include "stm32f4xx_hal.h"
#include "board.h"
#include "led.h"
#include "button.h"
#include "traffic.h"

Traffic traffic;
Button button;
volatile uint32_t lab1_fault;
static LedBank leds;

static void fatal(uint32_t code)
{
    lab1_fault = code;
    for (;;) {}
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
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
    button_init(&button, !io_read_pin(BOARD_BUTTON_PORT, BOARD_BUTTON_PIN), now, BOARD_DEBOUNCE_MS);
    traffic_init(&traffic, now);
    for (;;) {
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
