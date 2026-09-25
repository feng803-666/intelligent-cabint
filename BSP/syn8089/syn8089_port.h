#ifndef SYN8089_PORT_H
#define SYN8089_PORT_H

#include "syn8089.h"

/* 平台层：串口接收必须在发送及主循环工作期间持续缓存。 */
SYN8089_Status SYN8089_Port_Init(void);
SYN8089_Status SYN8089_Port_Transmit(const uint8_t *data, uint16_t size);
/* 非阻塞：有字节返回 1，无字节返回 0，串口/队列错误返回 -1。 */
int SYN8089_Port_ReadByte(uint8_t *data);
void SYN8089_Port_ClearRx(void);
void SYN8089_Port_SetReset(uint8_t high);
uint8_t SYN8089_Port_Busy(void);
uint32_t SYN8089_Port_Tick(void);
void SYN8089_Port_Delay(uint32_t ms);
/* 由 USART3_IRQHandler 调用，仅处理本驱动独占的 USART3 接收。 */
void SYN8089_Port_IRQHandler(void);

#endif
