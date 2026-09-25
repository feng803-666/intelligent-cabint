#include "syn8089_port.h"
#include "main.h"
#include "usart.h"

#define RX_CAPACITY 32U
static volatile uint8_t rx_data[RX_CAPACITY];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static volatile uint8_t rx_error;

SYN8089_Status SYN8089_Port_Init(void)
{
    if ((huart3.Instance != USART3) || (huart3.Init.BaudRate != 115200U) ||
        (huart3.Init.WordLength != UART_WORDLENGTH_8B) ||
        (huart3.Init.StopBits != UART_STOPBITS_1) ||
        (huart3.Init.Parity != UART_PARITY_NONE) ||
        (huart3.Init.Mode != UART_MODE_TX_RX) ||
        (huart3.Init.HwFlowCtl != UART_HWCONTROL_NONE)) return SYN8089_ERROR_UART;

    HAL_NVIC_DisableIRQ(USART3_IRQn);
    /* 独占接收侧，直接处理 RXNE；不能混用 HAL_UART_Receive[_IT/_DMA]。 */
    __HAL_UART_DISABLE_IT(&huart3, UART_IT_RXNE);
    SYN8089_Port_ClearRx();
    HAL_NVIC_ClearPendingIRQ(USART3_IRQn);
    HAL_NVIC_SetPriority(USART3_IRQn, 5U, 0U);
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    return SYN8089_OK;
}

void SYN8089_Port_IRQHandler(void)
{
    uint32_t flags = USART3->SR;
    if ((flags & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE |
                  USART_SR_NE | USART_SR_PE)) != 0U)
    {
        /* F1 按 SR、DR 顺序读取，清除 RXNE 及溢出等错误标志。 */
        uint8_t byte = (uint8_t)USART3->DR;
        uint8_t next = (uint8_t)((rx_head + 1U) % RX_CAPACITY);
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

int SYN8089_Port_ReadByte(uint8_t *data)
{
    if (rx_error) return -1;
    if (rx_tail == rx_head) return 0;
    *data = rx_data[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1U) % RX_CAPACITY);
    return 1;
}

void SYN8089_Port_ClearRx(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    rx_head = 0U;
    rx_tail = 0U;
    rx_error = 0U;
    __set_PRIMASK(mask);
}

SYN8089_Status SYN8089_Port_Transmit(const uint8_t *data, uint16_t size)
{
    /* 2005 字节在 115200 下约 175 ms，500 ms 留出足够余量。 */
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart3, data, size, 500U);
    if (status == HAL_TIMEOUT) return SYN8089_ERROR_TIMEOUT;
    return (status == HAL_OK) ? SYN8089_OK : SYN8089_ERROR_UART;
}

void SYN8089_Port_SetReset(uint8_t high)
{
    HAL_GPIO_WritePin(SYN_RES_GPIO_Port, SYN_RES_Pin,
                     high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t SYN8089_Port_Busy(void)
{
    return HAL_GPIO_ReadPin(SYN_R_B_GPIO_Port, SYN_R_B_Pin) == GPIO_PIN_SET;
}

uint32_t SYN8089_Port_Tick(void) { return HAL_GetTick(); }
void SYN8089_Port_Delay(uint32_t ms) { HAL_Delay(ms); }
