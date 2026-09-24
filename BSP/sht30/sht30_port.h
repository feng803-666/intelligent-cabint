#ifndef SHT30_PORT_H
#define SHT30_PORT_H

#include "sht30.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 平台适配接口：核心驱动只依赖这些函数，不包含任何 MCU/HAL 头文件。
 * 地址统一传七位地址；平台层负责转换为底层库要求的格式。
 * 读写必须同步完成后返回，不能直接替换成异步 DMA/中断接口。
 * 同一总线的并发访问由应用在完整的公开 API 调用外部加锁。
 */
SHT30_Status SHT30_Port_Write(uint8_t address, const uint8_t *data, uint16_t size);
SHT30_Status SHT30_Port_Read(uint8_t address, uint8_t *data, uint16_t size);
SHT30_Status SHT30_Port_IsReady(uint8_t address);

/* asserted=1：拉低 nRESET；asserted=0：释放复位、输出高电平。 */
void SHT30_Port_SetReset(uint8_t asserted);
/* 延时不能短于指定毫秒数，调用时系统时基必须已运行。 */
void SHT30_Port_DelayMs(uint32_t milliseconds);

#ifdef __cplusplus
}
#endif

#endif
