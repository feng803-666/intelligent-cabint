# SHT30 温湿度驱动

## 文件与职责

| 文件 | 用途 |
| --- | --- |
| `sht30.h` | 对外 API、返回码、测量数据结构、七位地址配置 |
| `sht30.c` | 测量命令、CRC 校验、温湿度换算，与 MCU 无关 |
| `sht30_port.h` | 同步 I2C、复位、延时的平台接口约定 |
| `sht30_port.c` | 当前板子的 STM32 HAL 适配 |

工程已编译两个 `.c` 文件，并添加本目录到头文件搜索路径。
`main.c` 中已移除 SHT30 初始化测试、定时采样、调试变量和 OLED 测试显示；应用需要时自行调用 API。

## 当前硬件配置

- I2C1：PB6=SCL、PB7=SDA，外接 4.7 kΩ 上拉。
- nRESET：PA4，低电平有效，GPIO 配置为输出，空闲时保持高电平。
- ADDR 接地，七位地址为 `0x44`；ALERT 未使用；R 接地。
- 应用先完成 `HAL_Init()`、系统时钟、`MX_GPIO_Init()`、`MX_I2C1_Init()`，再初始化传感器。
- 驱动不配置 MCU 时钟或外设，也不依赖 OLED、串口和主循环。

## API

所有 API 返回 `SHT30_Status`。接口为阻塞式、单设备接口，不可从中断调用。
在 RTOS 中，同一设备或共享 I2C 总线的访问，需要调用方围绕整个 API 调用加锁；单独保护每次 I2C 传输不足以防止测量命令被交错。

| 函数 | 使用方法与行为 |
| --- | --- |
| `SHT30_Init(void)` | 完成 MCU 外设初始化后调用。硬件复位、检查设备应答，然后读取状态并校验 CRC。成功后可测量。复位后内部加热器默认关闭。初始化成功表示通信和状态 CRC 正常，并不校验测量精度。 |
| `SHT30_Reset(void)` | 故障恢复时调用。nRESET 拉低至少 1 ms，释放后等待至少 2 ms，再检查应答。不会重建 MCU 的 I2C 外设，也不会清除物理总线故障。若还需要状态 CRC 检查，直接调用 `SHT30_Init()`。 |
| `SHT30_Read(SHT30_Data *data)` | 发送高重复性、无时钟拉伸的单次测量命令，等待至少 20 ms，再接收并校验两组 CRC。成功时返回摄氏温度和相对湿度 `%RH`；失败时整个输出保持原值。指针不能为 `NULL`。 |
| `SHT30_ReadStatus(uint16_t *status)` | 读取状态寄存器并校验 CRC。成功时更新输出，失败时保持原值。指针不能为 `NULL`。此操作不会清除状态标志。 |

读取周期由应用决定，例如每秒调用一次 `SHT30_Read()`。不需要每次测量前重新初始化。
核心驱动不缓存初始化状态，因此应用必须检查初始化结果；初始化失败后应先处理故障并重试。

### 返回码

| 返回值 | 数值 | 含义 |
| --- | --- | --- |
| `SHT30_OK` | 0 | 成功 |
| `SHT30_ERROR_PARAM` | 1 | 输出指针为空 |
| `SHT30_ERROR_I2C` | 2 | I2C 失败，例如设备无应答；HAL 适配层返回的一般错误 |
| `SHT30_ERROR_TIMEOUT` | 3 | 底层返回超时 |
| `SHT30_ERROR_BUSY` | 4 | 底层返回忙 |
| `SHT30_ERROR_CRC` | 5 | 接收数据校验失败，不能使用本次数据 |

当前 HAL 适配每次读写的超时参数为 100 ms，设备应答探测最多尝试两次。
一次 API 可能包含多次传输与延时，因此 100 ms 不是整个 API 的总超时。

### 最小使用示例

以下是业务层示例，不会自动运行，按需放入应用模块：

```c
#include "sht30.h"

static SHT30_Data environment;
static SHT30_Status sensor_result;

/* 由应用在 GPIO、I2C 和时基初始化完成后调用一次。 */
void Environment_Init(void)
{
    sensor_result = SHT30_Init();
}

/* 由主循环或任务每秒调用，失败时下一周期尝试恢复。 */
void Environment_Update(void)
{
    if (sensor_result != SHT30_OK)
    {
        sensor_result = SHT30_Init();
        if (sensor_result != SHT30_OK)
        {
            return;
        }
    }

    sensor_result = SHT30_Read(&environment);
    if (sensor_result == SHT30_OK)
    {
        /* environment.temperature_c：摄氏度
         * environment.humidity_rh：相对湿度百分数，例如 43.0 表示 43%RH。
         * 在这里交给显示、控制或通信模块使用。
         */
    }
    /* 失败时 environment 保留上次值，应通过 sensor_result 标识其非新数据。 */
}
```

状态读取示例：

```c
uint16_t status;
if (SHT30_ReadStatus(&status) == SHT30_OK)
{
    uint8_t heater_on = (status & SHT30_STATUS_HEATER_ON) != 0U;
    uint8_t reset_seen = (status & SHT30_STATUS_RESET_DETECTED) != 0U;
    /* heater_on=1 表示加热器开启；reset_seen=1 表示发生过复位，
     * 初始化刚完成时出现复位标志属于正常情况。
     */
    (void)heater_on;
    (void)reset_seen;
}
```

## 移植方法

### 更换 STM32 引脚、总线或地址

1. 在 CubeMX 中配置新的 I2C 和复位输出，并在应用中完成初始化。
2. 修改 `sht30_port.c` 顶部的配置宏，或在构建系统中定义：
   `SHT30_I2C_HANDLE`、`SHT30_RESET_GPIO_PORT`、`SHT30_RESET_GPIO_PIN`、`SHT30_TIMEOUT_MS`。
   新 I2C 句柄必须由适配文件包含的头文件声明。
3. ADDR 接高电平时，将 `SHT30_ADDRESS` 统一定义为 `0x45U`；接地时默认 `0x44U`。
   不要填 `0x88`/`0x8A`，地址左移由 HAL 适配层处理。

### 更换 MCU、SDK 或使用软件 I2C

保留 `sht30.c`、`sht30.h`、`sht30_port.h`，替换 `sht30_port.c` 中的五个函数即可：

- `SHT30_Port_Write`：向七位地址发送全部字节，完成后返回。
- `SHT30_Port_Read`：读取指定数量字节，完成后返回。
- `SHT30_Port_IsReady`：检查设备是否应答。
- `SHT30_Port_SetReset`：参数 1 拉低 nRESET，参数 0 拉高 nRESET。
- `SHT30_Port_DelayMs`：延时至少指定毫秒数。

平台层需把底层错误映射到 `SHT30_Status`，为通信设置有限超时。
接口中的缓冲区可能来自栈，不能让异步传输在函数返回后继续访问它。
本实现要求可控制 nRESET；没有连接复位脚的平台需要另行实现可靠的复位流程，不能把复位函数简单留空。
当前 API 没有设备句柄，不支持在运行时选择多个 SHT30；多设备应用需进一步改为实例接口。

## 验证

在工程目录执行固件构建：

```powershell
cmake --build --preset Debug
```

协议依据：[Sensirion SHT3x-DIS 数据手册](https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf)。
