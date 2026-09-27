#ifndef ESP12F_PORT_H
#define ESP12F_PORT_H

#include "esp12f.h"

ESP12F_Status ESP12F_Port_Init(void);
ESP12F_Status ESP12F_Port_Transmit(const uint8_t *data, uint16_t size);
/* 1=读到一个字节，0=暂时无数据，-1=UART 错误或缓存溢出。 */
int ESP12F_Port_ReadByte(uint8_t *byte);
/* 仅在模块启动/复位窗口后使用，清除启动乱码及 UART 错误。 */
void ESP12F_Port_ClearRx(void);
void ESP12F_Port_IRQHandler(void);
uint32_t ESP12F_Port_Tick(void);
/* 弱函数，可由应用覆盖，必须让系统时基继续运行，不得重入网络 API。 */
void ESP12F_Port_Idle(void);

#endif
