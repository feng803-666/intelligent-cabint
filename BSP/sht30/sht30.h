#ifndef SHT30_H
#define SHT30_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 七位地址：ADDR 接地用 0x44，接电源用 0x45，不能填左移后的地址。
 * 可通过编译宏覆盖；同一次构建中所有源文件必须使用相同配置。
 */
#ifndef SHT30_ADDRESS
#define SHT30_ADDRESS 0x44U
#endif
#if (SHT30_ADDRESS != 0x44U) && (SHT30_ADDRESS != 0x45U)
#error "SHT30_ADDRESS must be 0x44 or 0x45"
#endif

#define SHT30_STATUS_HEATER_ON (1U << 13)
#define SHT30_STATUS_RESET_DETECTED (1U << 4)

typedef enum
{
    SHT30_OK = 0,
    SHT30_ERROR_PARAM,
    SHT30_ERROR_I2C,
    SHT30_ERROR_TIMEOUT,
    SHT30_ERROR_BUSY,
    SHT30_ERROR_CRC
} SHT30_Status;

typedef struct
{
    float temperature_c; /* 温度，单位：摄氏度 */
    float humidity_rh;   /* 相对湿度，单位：%RH */
} SHT30_Data;

/* 所有接口均为阻塞接口，只能在主循环/任务中串行调用，不能在中断中调用。
 * 调用前须完成平台时基、复位 GPIO、I2C 初始化，并保持供电稳定。
 * 当前接口支持单个板级设备；同一设备及共享总线须由调用方互斥。
 * 硬件差异集中在 sht30_port.c，核心驱动不依赖 STM32 HAL。
 */

/* 硬件复位、检查应答并读取状态寄存器验证 CRC；默认关闭加热器。 */
SHT30_Status SHT30_Init(void);
/* nRESET 拉低 1 ms 后释放，等待 2 ms 并检查应答，可用于故障恢复。
 * 不重新配置 MCU 外设；不读取状态 CRC（需要完整检查时调用 Init）。
 */
SHT30_Status SHT30_Reset(void);
/* 高重复性、无时钟拉伸的单次测量，转换等待 20 ms。
 * data 不可为空；仅成功时更新数据，失败时保持原值。
 */
SHT30_Status SHT30_Read(SHT30_Data *data);
/* 读取原始状态寄存器并校验 CRC，仅成功时更新输出。
 * bit13=加热器开启，bit4=发生过复位，bit1=命令错误，bit0=写校验错误。
 */
SHT30_Status SHT30_ReadStatus(uint16_t *status);

#ifdef __cplusplus
}
#endif

#endif
