/* 主机测试：模拟 EEPROM，检查所有合法区间的分页、边界及错误传播。 */
#include "at24c02c.h"
#include "at24c02c_port.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t memory[256];
static unsigned writes, probes, delays;
static unsigned nack_count;
static AT24C02C_Status bus_result = AT24C02C_OK;
static AT24C02C_Status write_result = AT24C02C_OK;

AT24C02C_Status AT24C02C_Port_Init(void) { return bus_result; }
AT24C02C_Status AT24C02C_Port_IsReady(uint8_t device)
{
    assert(device == 0x50);
    ++probes;
    if (bus_result != AT24C02C_OK) return bus_result;
    if (nack_count) { --nack_count; return AT24C02C_ERROR_I2C; }
    return AT24C02C_OK;
}
void AT24C02C_Port_DelayMs(uint32_t ms) { assert(ms == 1); ++delays; }
AT24C02C_Status AT24C02C_Port_WritePage(uint8_t device, uint8_t address,
                                     const uint8_t *data, uint16_t size)
{
    assert(device == 0x50 && size > 0 && size <= 8);
    assert(address / 8 == (address + size - 1) / 8);
    ++writes;
    if (write_result != AT24C02C_OK) return write_result;
    memcpy(memory + address, data, size);
    return AT24C02C_OK;
}
AT24C02C_Status AT24C02C_Port_Read(uint8_t device, uint8_t address,
                                uint8_t *data, uint16_t size)
{
    assert(device == 0x50 && address + size <= 256);
    if (bus_result != AT24C02C_OK) return bus_result;
    memcpy(data, memory + address, size);
    return AT24C02C_OK;
}

int main(void)
{
    uint8_t tx[256], rx[256];
    for (unsigned i = 0; i < 256; ++i) tx[i] = (uint8_t)(i ^ 0x5A);
    assert(AT24C02C_ReadByte(0, rx) == AT24C02C_ERROR_NOT_INIT);
    assert(AT24C02C_WriteByte(0, 1) == AT24C02C_ERROR_NOT_INIT);
    assert(AT24C02C_IsReady() == AT24C02C_ERROR_NOT_INIT);
    assert(AT24C02C_Init() == AT24C02C_OK);
    for (unsigned address = 0; address < 256; ++address)
    {
        for (unsigned size = 1; size <= 256 - address; ++size)
        {
            memset(memory, 0xCC, sizeof(memory));
            writes = 0;
            assert(AT24C02C_Write(address, tx, size) == AT24C02C_OK);
            assert(writes == ((address % 8 + size + 7) / 8));
            assert(AT24C02C_Read(address, rx, size) == AT24C02C_OK);
            assert(memcmp(tx, rx, size) == 0);
            for (unsigned i = 0; i < address; ++i) assert(memory[i] == 0xCC);
            for (unsigned i = address + size; i < 256; ++i) assert(memory[i] == 0xCC);
        }
    }
    writes = probes = 0;
    assert(AT24C02C_Write(255, tx, 2) == AT24C02C_ERROR_PARAM);
    assert(AT24C02C_Write(0, tx, 0) == AT24C02C_ERROR_PARAM);
    assert(AT24C02C_Write(0, NULL, 1) == AT24C02C_ERROR_PARAM);
    assert(AT24C02C_Write(0, tx, 65535) == AT24C02C_ERROR_PARAM);
    assert(AT24C02C_Read(256, rx, 1) == AT24C02C_ERROR_PARAM);
    assert(AT24C02C_Read(65535, rx, 1) == AT24C02C_ERROR_PARAM);
    assert(AT24C02C_Read(0, NULL, 1) == AT24C02C_ERROR_PARAM);
    assert(AT24C02C_Read(0, rx, 0) == AT24C02C_ERROR_PARAM);
    assert(writes == 0 && probes == 0);
    nack_count = 3; probes = delays = 0;
    assert(AT24C02C_WriteByte(255, 0xA5) == AT24C02C_OK);
    assert(probes == 4 && delays == 3);
    assert(AT24C02C_ReadByte(255, rx) == AT24C02C_OK && rx[0] == 0xA5);
    nack_count = 20; probes = delays = writes = 0;
    assert(AT24C02C_Write(0, tx, 16) == AT24C02C_ERROR_TIMEOUT);
    assert(probes == 11 && delays == 10 && writes == 1);
    nack_count = 0;
    write_result = AT24C02C_ERROR_I2C; probes = 0;
    assert(AT24C02C_WriteByte(0, 0) == AT24C02C_ERROR_I2C && probes == 0);
    write_result = AT24C02C_OK;
    bus_result = AT24C02C_ERROR_BUSY;
    assert(AT24C02C_ReadByte(0, rx) == AT24C02C_ERROR_BUSY);
    assert(AT24C02C_WriteByte(0, 0) == AT24C02C_ERROR_BUSY);
    assert(AT24C02C_Init() == AT24C02C_ERROR_BUSY);
    assert(AT24C02C_ReadByte(0, rx) == AT24C02C_ERROR_NOT_INIT);
    bus_result = AT24C02C_OK;
    assert(AT24C02C_Init() == AT24C02C_OK);
    puts("PASS: 32896 ranges, page boundaries, invalid parameters, ACK polling, errors");
    return 0;
}
