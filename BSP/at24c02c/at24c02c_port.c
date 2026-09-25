#include "at24c02c_port.h"
#include "main.h"

/* 当前板级连接：PB13=SCL、PB14=SDA；必须外接上拉电阻。
 * 开漏写 1 表示释放总线，读取 IDR 可直接获取从机应答，无须切换方向。
 * 软件时序每半周期至少 5 us，实际频率低于 100 kHz。
 */
#define SCL_PORT I2C2_SCL_GPIO_Port
#define SCL_PIN  I2C2_SCL_Pin
#define SDA_PORT I2C2_SDA_GPIO_Port
#define SDA_PIN  I2C2_SDA_Pin

static void Delay(void)
{
    /* Cortex-M3 DWT 计数器提供与编译优化无关的延时；不占用定时器。
     * 无符号减法兼容计数器回绕；中断只会延长时序。
     */
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = (SystemCoreClock / 1000000U + 1U) * 5U;
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) { }
}

static void SDA(GPIO_PinState level) { HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, level); }
static void SCL_Low(void) { HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_RESET); }

static AT24C02C_Status SCL_High(void)
{
    uint32_t start = HAL_GetTick();
    HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_SET);
    while (HAL_GPIO_ReadPin(SCL_PORT, SCL_PIN) == GPIO_PIN_RESET)
    {
        if ((uint32_t)(HAL_GetTick() - start) >= 2U) return AT24C02C_ERROR_TIMEOUT;
    }
    Delay();
    return AT24C02C_OK;
}

static AT24C02C_Status Start(void)
{
    SDA(GPIO_PIN_SET);
    Delay();
    AT24C02C_Status result = SCL_High();
    if (result != AT24C02C_OK) return result;
    if (HAL_GPIO_ReadPin(SDA_PORT, SDA_PIN) == GPIO_PIN_RESET)
        return AT24C02C_ERROR_BUSY;
    SDA(GPIO_PIN_RESET);
    Delay();
    SCL_Low();
    Delay();
    return AT24C02C_OK;
}

static AT24C02C_Status Stop(AT24C02C_Status result)
{
    SCL_Low();
    SDA(GPIO_PIN_RESET);
    Delay();
    AT24C02C_Status stop_result = SCL_High();
    SDA(GPIO_PIN_SET);
    Delay();
    /* 即使超时也释放两条线；保留最先发生的错误。 */
    return result == AT24C02C_OK ? stop_result : result;
}

static AT24C02C_Status Send(uint8_t value)
{
    for (uint8_t bit = 0; bit < 8U; ++bit)
    {
        SDA((value & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        Delay();
        AT24C02C_Status result = SCL_High();
        if (result != AT24C02C_OK) return result;
        SCL_Low();
        Delay();
        value <<= 1;
    }
    SDA(GPIO_PIN_SET);
    Delay();
    AT24C02C_Status result = SCL_High();
    if (result != AT24C02C_OK) return result;
    uint8_t nack = HAL_GPIO_ReadPin(SDA_PORT, SDA_PIN) == GPIO_PIN_SET;
    SCL_Low();
    Delay();
    return nack ? AT24C02C_ERROR_I2C : AT24C02C_OK;
}

static AT24C02C_Status Receive(uint8_t *data, uint8_t last)
{
    uint8_t value = 0U;
    SDA(GPIO_PIN_SET);
    for (uint8_t bit = 0; bit < 8U; ++bit)
    {
        Delay();
        AT24C02C_Status result = SCL_High();
        if (result != AT24C02C_OK) return result;
        value = (uint8_t)((value << 1) | (HAL_GPIO_ReadPin(SDA_PORT, SDA_PIN) == GPIO_PIN_SET));
        SCL_Low();
        Delay();
    }
    /* 最后一个字节发送 NACK，其余字节发送 ACK。 */
    SDA(last ? GPIO_PIN_SET : GPIO_PIN_RESET);
    Delay();
    AT24C02C_Status result = SCL_High();
    SCL_Low();
    Delay();
    SDA(GPIO_PIN_SET);
    if (result == AT24C02C_OK) *data = value;
    return result;
}

AT24C02C_Status AT24C02C_Port_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, SCL_PIN | SDA_PIN, GPIO_PIN_SET);
    gpio.Pin = SCL_PIN | SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    /* 不清零计数器，避免影响其他使用 DWT 的模块。 */
    uint32_t before = DWT->CYCCNT;
    for (volatile uint32_t i = 0U; i < 32U; ++i) { __NOP(); }
    if (DWT->CYCCNT == before) return AT24C02C_ERROR_TIMEOUT;
    AT24C02C_Status result = SCL_High();
    if (result != AT24C02C_OK) return result;
    return HAL_GPIO_ReadPin(SDA_PORT, SDA_PIN) == GPIO_PIN_SET ?
           AT24C02C_OK : AT24C02C_ERROR_BUSY;
}

AT24C02C_Status AT24C02C_Port_IsReady(uint8_t device)
{
    AT24C02C_Status result = Start();
    if (result == AT24C02C_OK) result = Send((uint8_t)(device << 1));
    return Stop(result);
}

AT24C02C_Status AT24C02C_Port_WritePage(uint8_t device, uint8_t address,
                                     const uint8_t *data, uint16_t size)
{
    AT24C02C_Status result = Start();
    if (result == AT24C02C_OK) result = Send((uint8_t)(device << 1));
    if (result == AT24C02C_OK) result = Send(address);
    for (uint16_t i = 0U; i < size && result == AT24C02C_OK; ++i) result = Send(data[i]);
    return Stop(result);
}

AT24C02C_Status AT24C02C_Port_Read(uint8_t device, uint8_t address,
                                uint8_t *data, uint16_t size)
{
    AT24C02C_Status result = Start();
    if (result == AT24C02C_OK) result = Send((uint8_t)(device << 1));
    if (result == AT24C02C_OK) result = Send(address);
    if (result == AT24C02C_OK) result = Start();
    if (result == AT24C02C_OK) result = Send((uint8_t)((device << 1) | 1U));
    for (uint16_t i = 0U; i < size && result == AT24C02C_OK; ++i)
        result = Receive(&data[i], i == size - 1U);
    return Stop(result);
}

void AT24C02C_Port_DelayMs(uint32_t milliseconds) { HAL_Delay(milliseconds); }
