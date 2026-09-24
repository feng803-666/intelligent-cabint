#include "sht30_port.h"
#include "main.h"
#include "i2c.h"

/* STM32 HAL 板级适配：换引脚或 I2C 时只修改此处，或通过编译宏覆盖。
 * GPIO 与 I2C 的初始化由应用负责，本文件不修改 CubeMX 生成的配置。
 */
#ifndef SHT30_I2C_HANDLE
#define SHT30_I2C_HANDLE hi2c1
#endif
#ifndef SHT30_RESET_GPIO_PORT
#define SHT30_RESET_GPIO_PORT nRESET_GPIO_Port
#endif
#ifndef SHT30_RESET_GPIO_PIN
#define SHT30_RESET_GPIO_PIN nRESET_Pin
#endif
#ifndef SHT30_TIMEOUT_MS
#define SHT30_TIMEOUT_MS 100U
#endif

static SHT30_Status SHT30_FromHAL(HAL_StatusTypeDef status)
{
    switch (status)
    {
        case HAL_OK:      return SHT30_OK;
        case HAL_TIMEOUT: return SHT30_ERROR_TIMEOUT;
        case HAL_BUSY:    return SHT30_ERROR_BUSY;
        default:          return SHT30_ERROR_I2C;
    }
}

SHT30_Status SHT30_Port_Write(uint8_t address, const uint8_t *data, uint16_t size)
{
    /* HAL 的发送参数未声明 const，但阻塞发送不会修改缓冲区。 */
    return SHT30_FromHAL(HAL_I2C_Master_Transmit(&SHT30_I2C_HANDLE,
                         (uint16_t)(address << 1), (uint8_t *)data,
                         size, SHT30_TIMEOUT_MS));
}

SHT30_Status SHT30_Port_Read(uint8_t address, uint8_t *data, uint16_t size)
{
    return SHT30_FromHAL(HAL_I2C_Master_Receive(&SHT30_I2C_HANDLE,
                         (uint16_t)(address << 1), data, size, SHT30_TIMEOUT_MS));
}

SHT30_Status SHT30_Port_IsReady(uint8_t address)
{
    return SHT30_FromHAL(HAL_I2C_IsDeviceReady(&SHT30_I2C_HANDLE,
                         (uint16_t)(address << 1), 2U, SHT30_TIMEOUT_MS));
}

void SHT30_Port_SetReset(uint8_t asserted)
{
    HAL_GPIO_WritePin(SHT30_RESET_GPIO_PORT, SHT30_RESET_GPIO_PIN,
                      asserted ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void SHT30_Port_DelayMs(uint32_t milliseconds)
{
    HAL_Delay(milliseconds);
}
