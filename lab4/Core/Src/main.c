#include "stm32f4xx_hal.h"
#include "clock.h"
#include "sound.h"
#include "serial.h"
#include "app.h"

App app;
volatile uint32_t lab4_fault;

static void fatal(uint32_t code)
{
    lab4_fault = code;
    for (;;) {                                                                     }
}

int main(void)
{
    SystemCoreClockUpdate();
    if (HAL_Init() != HAL_OK) fatal(1);
    if (!clock_init()) fatal(2);
    if (!sound_init()) fatal(3);
    if (!serial_init(true)) fatal(4);
    if (!app_init(&app, HAL_GetTick())) fatal(5);
    static const char greeting[] =
        "\r\nLab4 matrix piano: 120 MHz, 57600 8N1\r\n"
        "PC15: test/piano; SB1..7: notes; SB8/9: octave +/-;\r\n"
        "SB10/11: duration +/-; SB12: scale\r\n";
    (void)serial_write(greeting, sizeof(greeting) - 1U);
    for (;;) {
        if (!app_update(&app, HAL_GetTick())) fatal(6);
    }
}

void SysTick_Handler(void) { HAL_IncTick(); }
void HardFault_Handler(void) { fatal(0xF1); }
void MemManage_Handler(void) { fatal(0xF2); }
void BusFault_Handler(void) { fatal(0xF3); }
void UsageFault_Handler(void) { fatal(0xF4); }
