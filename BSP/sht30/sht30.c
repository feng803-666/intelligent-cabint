#include "sht30.h"
#include "sht30_port.h"
#include <stddef.h>

#define SHT30_CMD_MEASURE 0x2400U
#define SHT30_CMD_READ_STATUS 0xF32DU

/* 每两个数据字节独立校验：初值 0xFF，多项式 0x31，无反射和末尾异或。 */
static uint8_t SHT30_CRC(const uint8_t *data)
{
    uint8_t crc = 0xFFU;
    for (uint8_t i = 0; i < 2U; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; ++bit)
        {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U)
                                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static SHT30_Status SHT30_Command(uint16_t command)
{
    uint8_t bytes[2] = {(uint8_t)(command >> 8), (uint8_t)command};
    /* 命令高字节先发，无额外的寄存器地址阶段。 */
    return SHT30_Port_Write(SHT30_ADDRESS, bytes, 2U);
}

SHT30_Status SHT30_Reset(void)
{
    SHT30_Port_SetReset(1U);
    SHT30_Port_DelayMs(1U);
    SHT30_Port_SetReset(0U);
    /* 覆盖低电压下最长 1.5 ms 的上电时间。 */
    SHT30_Port_DelayMs(2U);
    return SHT30_Port_IsReady(SHT30_ADDRESS);
}

SHT30_Status SHT30_Init(void)
{
    uint16_t status;
    SHT30_Status result = SHT30_Reset();
    if (result != SHT30_OK)
    {
        return result;
    }
    /* 复位后加热器默认关闭，复位标志为 1 属于正常状态。 */
    return SHT30_ReadStatus(&status);
}

SHT30_Status SHT30_ReadStatus(uint16_t *status)
{
    uint8_t bytes[3];
    SHT30_Status result;
    if (status == NULL)
    {
        return SHT30_ERROR_PARAM;
    }
    result = SHT30_Command(SHT30_CMD_READ_STATUS);
    if (result != SHT30_OK)
    {
        return result;
    }
    result = SHT30_Port_Read(SHT30_ADDRESS, bytes, 3U);
    if (result != SHT30_OK)
    {
        return result;
    }
    if (SHT30_CRC(bytes) != bytes[2])
    {
        return SHT30_ERROR_CRC;
    }
    *status = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    return SHT30_OK;
}

SHT30_Status SHT30_Read(SHT30_Data *data)
{
    uint8_t bytes[6];
    uint16_t raw_temperature;
    uint16_t raw_humidity;
    SHT30_Status result;
    if (data == NULL)
    {
        return SHT30_ERROR_PARAM;
    }
    result = SHT30_Command(SHT30_CMD_MEASURE);
    if (result != SHT30_OK)
    {
        return result;
    }
    /* 不依赖 ALERT；无时钟拉伸模式须等转换完成后再读取。 */
    SHT30_Port_DelayMs(20U);
    result = SHT30_Port_Read(SHT30_ADDRESS, bytes, 6U);
    if (result != SHT30_OK)
    {
        return result;
    }
    if (SHT30_CRC(bytes) != bytes[2] || SHT30_CRC(&bytes[3]) != bytes[5])
    {
        return SHT30_ERROR_CRC;
    }
    raw_temperature = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    raw_humidity = (uint16_t)(((uint16_t)bytes[3] << 8) | bytes[4]);
    data->temperature_c = -45.0f + 175.0f * (float)raw_temperature / 65535.0f;
    data->humidity_rh = 100.0f * (float)raw_humidity / 65535.0f;
    return SHT30_OK;
}
