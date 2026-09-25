#include "at24c02c.h"
#include "at24c02c_port.h"
#include <stddef.h>

static uint8_t initialized;

static AT24C02C_Status WaitReady(void)
{
    /* 最长写周期 5 ms，预留余量；仅对无应答重试，物理总线故障直接返回。 */
    for (uint8_t retry = 0; retry <= 10U; ++retry)
    {
        AT24C02C_Status result = AT24C02C_Port_IsReady(AT24C02C_ADDRESS);
        if (result != AT24C02C_ERROR_I2C) return result;
        if (retry < 10U) AT24C02C_Port_DelayMs(1U);
    }
    return AT24C02C_ERROR_TIMEOUT;
}

AT24C02C_Status AT24C02C_Init(void)
{
    initialized = 0U;
    AT24C02C_Status result = AT24C02C_Port_Init();
    if (result == AT24C02C_OK) result = WaitReady();
    if (result == AT24C02C_OK) initialized = 1U;
    return result;
}

AT24C02C_Status AT24C02C_IsReady(void)
{
    if (!initialized) return AT24C02C_ERROR_NOT_INIT;
    return AT24C02C_Port_IsReady(AT24C02C_ADDRESS);
}

static uint8_t ValidRange(uint16_t address, const void *data, uint16_t size)
{
    return data != NULL && size > 0U && address < AT24C02C_CAPACITY &&
           size <= AT24C02C_CAPACITY - address;
}

AT24C02C_Status AT24C02C_Read(uint16_t address, uint8_t *data, uint16_t size)
{
    if (!ValidRange(address, data, size)) return AT24C02C_ERROR_PARAM;
    if (!initialized) return AT24C02C_ERROR_NOT_INIT;
    return AT24C02C_Port_Read(AT24C02C_ADDRESS, (uint8_t)address, data, size);
}

AT24C02C_Status AT24C02C_Write(uint16_t address, const uint8_t *data, uint16_t size)
{
    if (!ValidRange(address, data, size)) return AT24C02C_ERROR_PARAM;
    if (!initialized) return AT24C02C_ERROR_NOT_INIT;
    while (size > 0U)
    {
        uint16_t count = AT24C02C_PAGE_SIZE - address % AT24C02C_PAGE_SIZE;
        if (count > size) count = size;
        AT24C02C_Status result = AT24C02C_Port_WritePage(
            AT24C02C_ADDRESS, (uint8_t)address, data, count);
        if (result != AT24C02C_OK) return result;
        result = WaitReady();
        if (result != AT24C02C_OK) return result;
        address += count;
        data += count;
        size -= count;
    }
    return AT24C02C_OK;
}

AT24C02C_Status AT24C02C_ReadByte(uint16_t address, uint8_t *data)
{
    return AT24C02C_Read(address, data, 1U);
}

AT24C02C_Status AT24C02C_WriteByte(uint16_t address, uint8_t data)
{
    return AT24C02C_Write(address, &data, 1U);
}
