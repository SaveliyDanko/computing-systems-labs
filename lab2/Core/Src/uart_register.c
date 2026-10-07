/* Part 2: no HAL/LL UART calls. CMSIS supplies registers and NVIC. */
#ifdef UART_REGISTER_BACKEND
#include "uart_backend.h"
#include "board.h"
static uint8_t *pending_tx;
static const uint32_t errors = USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE;
bool uart_backend_init(void)
{
    if (!uart_pins_init()) return false;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;
    USART1->CR1 = 0;
    USART1->CR2 = 0;
    USART1->CR3 = 0;
    /* APB2=HSI 16 MHz, OVER8=0: rounded PCLK/baud is the BRR encoding. */
    USART1->BRR = (16000000U + BOARD_UART_BAUD / 2U) / BOARD_UART_BAUD;
    USART1->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;
    pending_tx = 0;
    NVIC_DisableIRQ(USART1_IRQn);
    NVIC_SetPriority(USART1_IRQn, 1);
    return true;
}
bool uart_backend_mode(bool interrupts)
{
    NVIC_DisableIRQ(USART1_IRQn);
    USART1->CR1 &= ~(USART_CR1_RXNEIE | USART_CR1_PEIE | USART_CR1_TXEIE | USART_CR1_TCIE);
    USART1->CR3 &= ~USART_CR3_EIE;
    NVIC_ClearPendingIRQ(USART1_IRQn);
    if (interrupts) {
        USART1->CR1 |= USART_CR1_RXNEIE | USART_CR1_PEIE;
        USART1->CR3 |= USART_CR3_EIE;
        NVIC_EnableIRQ(USART1_IRQn);
    }
    return true;
}
int uart_backend_receive(uint8_t *byte)
{
    uint32_t status = USART1->SR;
    if (!(status & (USART_SR_RXNE | errors))) return 0;
    uint8_t data = (uint8_t)USART1->DR;
    if (status & errors) return -1;
    *byte = data;
    return 1;
}
bool uart_backend_transmit(uint8_t *byte, bool interrupts)
{
    if (interrupts) {
        if (pending_tx) return false;
        pending_tx = byte;
        USART1->CR1 |= USART_CR1_TXEIE;
        return true;
    }
    if (!(USART1->SR & USART_SR_TXE)) return false;
    USART1->DR = *byte;
    return true;
}
bool uart_backend_idle(void) { return (USART1->SR & USART_SR_TC) != 0; }
void USART1_IRQHandler(void)
{
    uint8_t byte;
    int status = uart_backend_receive(&byte);
    if (status > 0) serial_received(byte);
    else if (status < 0) serial_receive_error();
    uint32_t sr = USART1->SR, cr1 = USART1->CR1;
    if ((sr & USART_SR_TXE) && (cr1 & USART_CR1_TXEIE) && pending_tx) {
        USART1->DR = *pending_tx;
        pending_tx = 0;
        USART1->CR1 = (cr1 & ~USART_CR1_TXEIE) | USART_CR1_TCIE;
    } else if ((sr & USART_SR_TC) && (cr1 & USART_CR1_TCIE)) {
        USART1->CR1 &= ~USART_CR1_TCIE;
        serial_transmitted();
    }
}
#endif
