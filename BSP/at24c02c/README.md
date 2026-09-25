# AT24C02C 软件 I2C 驱动

参考 SHT30 分层：`at24c02c.h/.c` 提供不依赖 HAL 的核心 API，
`at24c02c_port.h/.c` 实现 STM32F1 GPIO 软件 I2C。CMake 已加入两个源文件。

## 接线与初始化

- A0、A1、A2 接地，七位地址固定为 `0x50`（线上写 `0xA0`、读 `0xA1`）。
- WP 下拉到地，允许写入；PB13=SCL，PB14=SDA。
- SCL、SDA 均需外接上拉至与 MCU 兼容的电源，常用 4.7 kΩ；本驱动不启用内部上拉。
- 先运行 `HAL_Init()`、`SystemClock_Config()`、`MX_GPIO_Init()`，电源稳定后调用 `AT24C02C_Init()`。
- Init 将 PB13/PB14 设为开漏，并开启 Cortex-M3 DWT 周期计数器。不要在运行期间关闭 DWT。
- 不使用硬件 I2C2，无须创建 `hi2c2`；与 SHT30 的 I2C1 独立。

## API

所有接口返回 `AT24C02C_Status`，阻塞执行，仅供主循环或任务串行调用。
禁止在中断、关闭中断或系统时基停止时调用；RTOS 下在整个 API 外保护总线。

| 接口 | 作用 |
| --- | --- |
| `AT24C02C_Init()` | 初始化平台、检查总线空闲、等待设备应答；失败后读写不可用，可重新初始化 |
| `AT24C02C_IsReady()` | 单次应答探测，无应答立即返回 I2C 错误 |
| `AT24C02C_Read(address, data, size)` | 随机地址起始的连续读取，重复 START，最后一字节 NACK |
| `AT24C02C_Write(address, data, size)` | 自动按 8 字节页边界拆分，并在每页之后轮询写完成 |
| `AT24C02C_ReadByte(address, data)` | 读取一个字节 |
| `AT24C02C_WriteByte(address, data)` | 写入一个字节并等待完成 |

地址类型为 `uint16_t`，合法范围 `0x00~0xFF`；长度 `1~256`，
`address + size` 不得超过 256。空指针、零长度或越界均返回参数错误，不会访问总线。
Read 失败时可能已经修改部分缓冲区，Write 失败时可能已写入部分 EEPROM，均不保证原子性。
写成功表示器件重新应答，不能检测 WP 误接高电平；需要数据保证时须读回比较。

| 返回码 | 值 | 含义 |
| --- | --- | --- |
| `AT24C02C_OK` | 0 | 成功 |
| `AT24C02C_ERROR_PARAM` | 1 | 参数非法 |
| `AT24C02C_ERROR_I2C` | 2 | 地址或数据未应答 |
| `AT24C02C_ERROR_TIMEOUT` | 3 | SCL 拉高等待超时、DWT 未运行，或写完成/初始化应答轮询超时 |
| `AT24C02C_ERROR_BUSY` | 4 | START 前 SDA 被拉低 |
| `AT24C02C_ERROR_NOT_INIT` | 5 | 尚未成功初始化 |

软件半周期至少 5 us，GPIO 开销使实际速率低于 100 kHz。
每次 SCL 拉高等待上限为 2 ms；写完成最多探测 11 次，间隔至少 1 ms。
这不是整个 API 的超时上限。总线故障时释放引脚并返回，不自动发送恢复时钟；
排除接线/供电/上拉问题后重新初始化。当前只支持单主机，不支持仲裁。

```c
#include "at24c02c.h"

/* 在平台初始化完成、供电稳定后执行，检查每一步的返回值。 */
uint8_t tx[] = {0x12, 0x34, 0x56, 0x78};
uint8_t rx[sizeof(tx)];
AT24C02C_Status status = AT24C02C_Init();
if (status == AT24C02C_OK)
    status = AT24C02C_Write(0x20, tx, sizeof(tx));
if (status == AT24C02C_OK)
    status = AT24C02C_Read(0x20, rx, sizeof(rx));
/* status 为 OK 时再使用 rx；需要写校验时逐字节比较 tx/rx。 */
```

## 应用集成状态

main.c 中的 AT24C02C 上电测试、调试变量和 OLED 测试显示已移除。
应用需要时按上述示例自行调用初始化及读写 API，上电不再自动读写 EEPROM。

## 移植与构建

更换 MCU 时保留核心文件，实现 port 头文件中的同步接口即可。
更换引脚时需同时调整 port 中引脚宏、GPIO 时钟和初始化端口，不能只改 pin。
时钟改变后须保证 `SystemCoreClock` 正确，平台需要运行中的 HAL 毫秒时基。

在 `project` 目录运行 `cmake --build --preset Debug`。
构建通过不能替代实板验证；应用集成后检查 API 返回码及读回数据，必要时用逻辑分析仪检查 ACK 及时序。

主机模拟测试覆盖全部 32896 个合法读写区间、页边界、非法参数、ACK 轮询及错误传播。
使用本机 GCC（非 arm-none-eabi-gcc）在 `project` 目录执行：

```powershell
gcc -std=c11 -Wall -Wextra -Werror -I BSP/at24c02c tests/test_at24c02c.c BSP/at24c02c/at24c02c.c -o build/test_at24c02c.exe
./build/test_at24c02c.exe
```

此测试替换了平台接口，不验证 GPIO 电气行为或实际软件 I2C 波形。

协议依据：[Microchip AT24C01C/AT24C02C 数据手册](https://ww1.microchip.com/downloads/en/DeviceDoc/AT24C01C-AT24C02C-I2C-Compatible-Two-Wire-Serial-EEPROM-1Kbit-2Kbit-20006111A.pdf)。
