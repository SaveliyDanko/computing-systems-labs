#include "stm32f4xx_hal.h"
#include "clock.h"
#include "sound.h"
#include "serial.h"
#include "console.h"

piano_t piano;
Console console;
volatile uint32_t lab3_fault;

static void fatal(uint32_t code)
{
    lab3_fault = code;
    for (;;) {                                                                     }
}

int main(void)
{
    SystemCoreClockUpdate();
    if (HAL_Init() != HAL_OK) fatal(1);
    if (!clock_init()) fatal(2);
    if (!sound_init()) fatal(3);
    piano_init(&piano);
    if (!serial_init(true)) fatal(4);
    console_init(&console);
    static const char greeting[] =
        "\r\nLab3 piano: 120 MHz, 57600 8N1\r\n"
        "Octave first (5/9), duration 1.0 s\r\n"
        "1..7: do re mi fa sol la si; +/-: octave; A/a: duration; Enter: scale\r\n";
    (void)serial_write(greeting, sizeof(greeting) - 1U);
    for (;;) {
        serial_service();
        uint32_t now = HAL_GetTick();
        piano_update(&piano, now);
        console_update(&console, &piano, now);
        if (!sound_set_frequency(piano_frequency_millihz(&piano))) fatal(5);
    }
}

void SysTick_Handler(void) { HAL_IncTick(); }
void HardFault_Handler(void) { fatal(0xF1); }
void MemManage_Handler(void) { fatal(0xF2); }
void BusFault_Handler(void) { fatal(0xF3); }
void UsageFault_Handler(void) { fatal(0xF4); }
