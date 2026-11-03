/* Part 1: HAL polling and HAL interrupt adapters, selected at runtime. */
#ifndef UART_REGISTER_BACKEND
#include "stm32f4xx_hal.h"
#include "uart_backend.h"
#include "board.h"
static UART_HandleTypeDef uart;
static uint8_t rx_byte;
static bool irq_enabled;
static const uint32_t errors = USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE;
bool uart_backend_init(void)
{
    if (!uart_pins_init()) return false;
    __HAL_RCC_USART1_CLK_ENABLE();
    uart.Instance = USART1;
    uart.Init.BaudRate = BOARD_UART_BAUD;
    uart.Init.WordLength = UART_WORDLENGTH_8B;
    uart.Init.StopBits = UART_STOPBITS_1;
    uart.Init.Parity = UART_PARITY_NONE;
    uart.Init.Mode = UART_MODE_TX_RX;
    uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    return HAL_UART_Init(&uart) == HAL_OK;
}
int uart_backend_receive(uint8_t *byte)
{
    if (USART1->SR & errors) {
        __HAL_UART_CLEAR_OREFLAG(&uart);
        return -1;
    }
    return HAL_UART_Receive(&uart, byte, 1, 0) == HAL_OK ? 1 : 0;
}
bool uart_backend_mode(bool interrupts)
{
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    if (HAL_UART_AbortReceive(&uart) != HAL_OK) return false;
    irq_enabled = interrupts;
    /* Drain a pending byte before arming asynchronous receive. */
    uint8_t byte;
    int status = uart_backend_receive(&byte);
    if (status > 0) serial_received(byte);
    else if (status < 0) serial_receive_error();
    HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
    if (interrupts) {
        if (HAL_UART_Receive_IT(&uart, &rx_byte, 1) != HAL_OK) return false;
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
    return true;
}
bool uart_backend_transmit(uint8_t *byte, bool interrupts)
{
    if (interrupts) return HAL_UART_Transmit_IT(&uart, byte, 1) == HAL_OK;
    if (!__HAL_UART_GET_FLAG(&uart, UART_FLAG_TXE)) return false;
    HAL_StatusTypeDef result = HAL_UART_Transmit(&uart, byte, 1, 2);
    if (result == HAL_TIMEOUT) {
        /* TXE was set: the byte was written; do not duplicate it on TC timeout. */
        serial_transmit_error();
        return true;
    }
    return result == HAL_OK;
}
bool uart_backend_idle(void) { return __HAL_UART_GET_FLAG(&uart, UART_FLAG_TC) != RESET; }
void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&uart); }
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    if (handle != &uart) return;
    if (handle->ErrorCode == HAL_UART_ERROR_NONE) {
        serial_received(rx_byte);
        if (irq_enabled) (void)HAL_UART_Receive_IT(handle, &rx_byte, 1);
    }
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle)
{
    if (handle == &uart) serial_transmitted();
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    if (handle != &uart) return;
    serial_receive_error();
    (void)HAL_UART_AbortReceive(handle);
    __HAL_UART_CLEAR_OREFLAG(handle);
    if (irq_enabled) (void)HAL_UART_Receive_IT(handle, &rx_byte, 1);
}
#endif
