#include "esp12f_port.h"
#include "usart.h"

#define RX_CAPACITY 2048U
static volatile uint8_t rx_data[RX_CAPACITY];
static volatile uint16_t rx_head, rx_tail;
static volatile uint8_t rx_error;

ESP12F_Status ESP12F_Port_Init(void)
{
    if ((huart2.Instance != USART2) || (huart2.Init.BaudRate != 115200U) ||
        (huart2.Init.WordLength != UART_WORDLENGTH_8B) ||
        (huart2.Init.StopBits != UART_STOPBITS_1) ||
        (huart2.Init.Parity != UART_PARITY_NONE) ||
        (huart2.Init.Mode != UART_MODE_TX_RX) ||
        (huart2.Init.HwFlowCtl != UART_HWCONTROL_NONE)) return ESP12F_ERROR_UART;

    HAL_NVIC_DisableIRQ(USART2_IRQn);
    __HAL_UART_DISABLE_IT(&huart2, UART_IT_RXNE);
    ESP12F_Port_ClearRx();
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn, 5U, 0U);
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    return ESP12F_OK;
}

void ESP12F_Port_IRQHandler(void)
{
    uint32_t flags = USART2->SR;
    if ((flags & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE |
                  USART_SR_NE | USART_SR_PE)) != 0U)
    {
        /* F1 依次读取 SR、DR 清除接收及错误标志；ISR 只入队。 */
        uint8_t byte = (uint8_t)USART2->DR;
        uint16_t next = (uint16_t)((rx_head + 1U) % RX_CAPACITY);
        if ((flags & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) != 0U)
            rx_error = 1U;
        else if (next == rx_tail)
            rx_error = 1U;
        else
        {
            rx_data[rx_head] = byte;
            rx_head = next;
        }
    }
}

int ESP12F_Port_ReadByte(uint8_t *byte)
{
    if (rx_error) return -1;
    if (rx_tail == rx_head) return 0;
    *byte = rx_data[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % RX_CAPACITY);
    return 1;
}

void ESP12F_Port_ClearRx(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    rx_head = rx_tail = 0U;
    rx_error = 0U;
    __set_PRIMASK(mask);
}

ESP12F_Status ESP12F_Port_Transmit(const uint8_t *data, uint16_t size)
{
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart2, data, size, 1000U);
    if (status == HAL_TIMEOUT) return ESP12F_ERROR_TIMEOUT;
    return status == HAL_OK ? ESP12F_OK : ESP12F_ERROR_UART;
}

uint32_t ESP12F_Port_Tick(void) { return HAL_GetTick(); }
__weak void ESP12F_Port_Idle(void) { HAL_Delay(1U); }
