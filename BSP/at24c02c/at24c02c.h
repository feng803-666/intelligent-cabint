#ifndef AT24C02C_H
#define AT24C02C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A0~A2 接地：七位地址 0x50，线上写/读地址为 0xA0/0xA1。 */
#define AT24C02C_ADDRESS 0x50U
#define AT24C02C_CAPACITY 256U
#define AT24C02C_PAGE_SIZE 8U

typedef enum
{
    AT24C02C_OK = 0,
    AT24C02C_ERROR_PARAM,
    AT24C02C_ERROR_I2C,      /* 从机未应答 */
    AT24C02C_ERROR_TIMEOUT,  /* 时钟线被拉低或写周期等待超时 */
    AT24C02C_ERROR_BUSY,     /* 数据线被占用 */
    AT24C02C_ERROR_NOT_INIT
} AT24C02C_Status;

/* 单设备阻塞接口，仅在主循环/任务中串行调用，禁止中断调用。
 * 先完成 HAL、系统时钟及 GPIO 初始化；共享总线须在完整 API 外加锁。
 */
AT24C02C_Status AT24C02C_Init(void);
AT24C02C_Status AT24C02C_IsReady(void);
/* 地址范围 0~255，长度 1~256，禁止越界；空指针/零长度均报参数错误。
 * 读取失败时缓冲区可能已有部分数据，调用方必须检查返回值。
 */
AT24C02C_Status AT24C02C_Read(uint16_t address, uint8_t *data, uint16_t size);
/* 自动按 8 字节页拆分，每页等待写入完成；失败可能已写入部分数据。
 * 成功表示传输及写周期完成，不等于读回校验通过。
 */
AT24C02C_Status AT24C02C_Write(uint16_t address, const uint8_t *data, uint16_t size);
AT24C02C_Status AT24C02C_ReadByte(uint16_t address, uint8_t *data);
AT24C02C_Status AT24C02C_WriteByte(uint16_t address, uint8_t data);

#ifdef __cplusplus
}
#endif
#endif
