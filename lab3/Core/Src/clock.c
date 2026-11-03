#include "clock.h"
#include "stm32f4xx_hal.h"

bool clock_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return false;
    /* Return to HSI first: OpenOCD can leave a different PLL configuration. */
    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK) return false;
    /* 16 / 8 * 120 / 2 = 120 MHz; PLLQ=5 also yields 48 MHz. */
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 120;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 5;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return false;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    /* APB1 divider > 1: TIM2 runs at 2 * PCLK1, not HCLK. */
    RCC->DCKCFGR &= ~RCC_DCKCFGR_TIMPRE;
    return HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) == HAL_OK;
}
