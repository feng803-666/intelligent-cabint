#ifndef AT24C02C_PORT_H
#define AT24C02C_PORT_H

#include "at24c02c.h"

/* 内部平台接口，地址为七位地址，所有传输同步完成。
 * 核心保证参数合法；WritePage 不可跨页；Read 必须使用重复起始。
 */
AT24C02C_Status AT24C02C_Port_Init(void);
AT24C02C_Status AT24C02C_Port_IsReady(uint8_t device);
AT24C02C_Status AT24C02C_Port_WritePage(uint8_t device, uint8_t address,
                                     const uint8_t *data, uint16_t size);
AT24C02C_Status AT24C02C_Port_Read(uint8_t device, uint8_t address,
                                uint8_t *data, uint16_t size);
void AT24C02C_Port_DelayMs(uint32_t milliseconds);
#endif
