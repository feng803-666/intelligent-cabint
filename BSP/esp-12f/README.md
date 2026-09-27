# ESP-12F / OneNET 温湿度测试

ESP-12F 使用 ESP8266 AT 固件，通过 USART2 发送普通 TCP AT 指令，STM32 负责
MQTT 3.1.1 编解码。无需模块支持 `AT+MQTT...` 命令。当前实现为单连接、
clean session、QoS 0、不保留消息；上报是否成功以 OneNET 的业务应答为准。

## 板级连接

| 项目 | 当前配置 |
| --- | --- |
| STM32 USART2 TX | PA2，接模块 RX |
| STM32 USART2 RX | PA3，接模块 TX |
| 串口参数 | 115200、8N1、无流控 |
| ESP8266 网络 | 2.4 GHz Wi-Fi，Station 模式 |
| SHT30 | 复用 `BSP/sht30`，I2C1，默认七位地址 0x44 |
| 调试输出 | USART1，115200、8N1 |

模块应正常供电、共地，EN/启动引脚按现有硬件配置进入 AT 固件。
驱动没有假定 ESP 复位 GPIO，使用 `AT+RST` 软件复位。模块必须以普通 AT
模式启动；自定义应用固件、自动进入透传的固件或不同波特率需先恢复配置。
初始化会忽略上电和软件复位窗口的启动日志，清除由 ROM 的 74880 波特率
造成的 UART 帧错误，再发送 AT 检查 115200 下是否就绪。

USART2 接收由 `ESP12F_Port_IRQHandler()` 独占，2048 字节环形缓存，
在 `stm32f1xx_it.c` 的 USART2 用户区接入；不能同时使用 HAL 的
`HAL_UART_Receive` / `Receive_IT` / `Receive_DMA`。
IRQ 只接收字节，主循环解析 `+IPD` 和 MQTT，支持串口分片、TCP 分包和粘包。
UART 错误、缓存溢出、超长 MQTT 包会返回错误并使连接失效。

## 本地配置与构建

保留同目录下用户已有的 UTF-8 文件（支持 UTF-8 BOM）：

`wifi.txt`：

```text
name:你的WiFi名称
password:你的WiFi密码
```

`onenet.txt`：

```text
产品id:你的产品ID
设备名称:你的设备名称
设备密钥:保留你现有的设备密钥
token:你生成的完整token
```

构建只读取产品 ID、设备名称、token、Wi-Fi 参数，不把设备密钥编入固件。
值可以不加引号，也可以用一对双引号包裹（兼容用户现有文件）。外层双引号会
去除，值内部的逗号、双引号、反斜杠会在 AT 命令中自动转义。
Wi-Fi 名称最多 32 字节，密码最多 64 字节。文本更新后 CMake 会重新生成
`build/<配置>/generated/onenet_credentials.h`；该文件及固件含连接凭据，
不要提交到 Git。现有 `.gitignore` 已忽略构建目录和这两个文本文件。
代码、测试与日志不输出密码或 token。

现有 token 的到期时间为 **2027-09-26 23:00:00（UTC+8）**。
这里直接使用用户提供的 token，不在 MCU 上重新计算或自动续期；到期后更新
`onenet.txt` 的 token 并重新构建、烧录。

在 `project` 目录运行：

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

固件输出为 `build/Debug/project.elf`。Release 对应使用 `Release` preset。
不要手动编辑生成的凭据头文件；非 CMake 工具链需要自行按
`onenet_credentials.h.in` 准备同名头文件并加入包含路径。

当前板使用 DAPLink（CMSIS-DAP），已配置 `.vscode/launch.json`，可用 Cortex-Debug
下载调试，或使用本机 OpenOCD：

```powershell
openocd -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg -c "adapter speed 1000" -c "program build/Debug/project.elf verify reset exit"
```

下载前关闭其他占用 DAPLink 的调试器。DAPLink 的虚拟串口还需将 USART1 TX
连接到探针 RX 并共地；只有 SWD 连接时可以调试，但收不到 USART1 日志。
调试器可观察 `telemetry_sample`（真实温湿度）、`telemetry_upload_count`
（平台确认成功次数）、`telemetry_last_status`（最近状态）。

## 对外 API

| 接口 | 用途 |
| --- | --- |
| `ESP12F_Init()` | 初始化接收、等待启动、软件复位、配置 AT 模式 |
| `ESP12F_JoinWiFi(ssid, password)` | 连接 Wi-Fi，最长等待 30 秒 |
| `ESP12F_MQTTConnect(config, handler)` | TCP 连接和 MQTT CONNECT，检查 CONNACK |
| `ESP12F_MQTTSubscribe(topic)` | QoS 0 订阅，检查匹配包 ID 的 SUBACK |
| `ESP12F_MQTTPublish(topic, data, length)` | QoS 0 发布；OK 仅表示发送完成 |
| `ESP12F_Poll()` | 处理接收、断线和 MQTT 心跳 |
| `ESP12F_IsConnected()` | 最近一次已处理的 MQTT 连接状态 |
| `ESP12F_LastConnAck()` | 最近的 CONNACK 拒绝码 |
| `ESP12F_LastWiFiError()` | 最近的 CWJAP 错误码：1 超时、2 密码错误、3 未找到 AP、4 连接失败 |
| `ESP12F_StatusString(status)` | 可用于日志的状态字符串 |
| `OneNET_Connect()` | 根据本地配置初始化、联网、登录、订阅上报应答 |
| `OneNET_Poll()` | 转调 MQTT 接收和心跳处理 |
| `OneNET_Report(temp, humi)` | 两位小数上报，等待对应消息 ID 的业务应答 |
| `OneNET_LastReplyCode()` | 本次上报业务响应码，0 表示未收到有效应答 |
| `OneNET_Stage()` | 定位连接失败阶段：ESP/WIFI/MQTT/SUBSCRIBE/ONLINE |

所有 API 在主循环串行调用，不能从 ISR、接收回调或其他线程重入。
阻塞等待期间调用可覆盖的 `ESP12F_Port_Idle()`，本工程在其中维护呼吸灯。
模块初始化约需数秒，联网失败时连接调用可能持续数十秒；这是主循环测试实现，
不保证其他主循环业务的实时响应。常态下频繁调用 Poll，建议间隔小于 20 ms。
单个 MQTT 接收包上限 768 字节；发送缓冲同为 768 字节，预留 5 字节头空间，
可用 body 最多 763 字节。使用静态缓冲，不动态分配内存。

## APP 应用层与 main.c 调用

联网、采样、上报、重连、OLED 状态显示、日志和调试状态统一位于
`APP/esp-12f/esp-12f_app.c`，接口见同目录 `esp-12f_app.h`。
`ESP12F_Port_Idle()` 的应用覆盖也在此文件内，网络等待时继续维护呼吸灯。
采样周期和重连间隔在应用源文件顶部配置。

`main.c` 保留外设及编码器、OLED、语音、LED 初始化，再调用应用接口：

```c
#include "esp-12f_app.h"

/* 完成 HAL、外设、OLED 和 LED 初始化后，主循环前调用一次。 */
ESP12F_App_Init();

while (1)
{
    LED1_Task();
    ESP12F_App_Task();
}
```

`ESP12F_App_Init()` 初始化 SHT30 和应用状态，实际联网在首次 Task 中进行。
`ESP12F_App_Task()` 必须持续调用，不能只在 5 秒定时点调用；它也负责网络轮询。
以下为应用层保留的测试行为：

1. 保留编码器、OLED、SYN8089 欢迎语和呼吸灯，初始化 SHT30。
2. 连接 Wi-Fi，然后登录 `mqtts.heclouds.com:1883`，使用设备名称作为 Client ID、
   产品 ID 作为 username、完整 token 作为 password。此端口使用普通 TCP，无 TLS。
3. 订阅 `$sys/<产品ID>/<设备名称>/thing/property/post/reply`。
4. 每 5 秒读取 SHT30，发布到 `$sys/<产品ID>/<设备名称>/thing/property/post`：

```json
{"id":"1","version":"1.0","params":{"temp":{"value":25.13},"humi":{"value":56.38}}}
```

5. 只接受同一消息 ID 的顶层 `code`，`200` 才返回成功。
   业务应答最长等 10 秒，错误码通过 USART1 与 OLED 显示。
   MQTT 的 `SEND OK` 本身不视为平台成功。
6. 读取失败跳过本次数据，下一次重试 SHT30 初始化；超出已有物模型范围
   `temp=-20..100`、`humi=0..100` 的值不会上传，不会用虚构数值替代。
7. 连接/传输失败后等待 10 秒，再从模块复位开始重连；成功后立即采样上报。
   MQTT keep alive 为 60 秒，空闲 30 秒发送 PINGREQ，10 秒未收到 PINGRESP 则重连。

烧录并复位后，在 USART1 看到如下日志，并在平台设备属性中确认 `temp` / `humi`
持续更新，才完成实板端到端验收：

```text
[OneNET] stage=ONLINE result=OK connack=0 wifi=0
[OneNET] temp=25.13 humi=56.38 result=OK code=200
```

故障定位：`stage=ESP` 检查固件/波特率/接线；`WIFI` 检查热点；`MQTT`
检查域名访问及凭据；CONNACK 4/5 检查 token 和设备权限；`SUBSCRIBE`
检查 topic 权限；`CLOUD_REJECTED` 看业务 code 和物模型标识。
ESP-12F 不支持 5 GHz 热点，手机热点需设为 2.4 GHz；保持名称和密码不变，
本程序会自动重连。Wi-Fi 能力参考
[ESP8266EX 规格](https://documentation.espressif.com/0a-esp8266ex_datasheet_en.html)。
若 MCU 在 `CIPSEND` 数据阶段意外复位，模块可能仍等待剩余数据而不响应 AT，
应同时复位/重上电 ESP 模块后重试。

## 主机验证

测试使用 `tests/esp12f/onenet_credentials.h` 中的虚构凭据，不连接真实平台。

```powershell
gcc -std=c11 -Wall -Wextra -Werror -pedantic -I BSP/esp-12f -I tests/esp12f tests/test_esp12f.c BSP/esp-12f/esp12f.c BSP/esp-12f/onenet.c -o build/test_esp12f.exe
.\build\test_esp12f.exe
```

覆盖 MQTT CONNECT/SUBSCRIBE 字节、AT 参数转义、跨 `+IPD` 分片与粘包、
响应早于 SEND OK、数值边界、NaN/Inf、错误 ID/topic、JSON 嵌套与重复字段、
鉴权/订阅拒绝、串口错误、断线、心跳超时、时间回绕和超长包。
协议模拟和交叉编译不能替代实板 Wi-Fi、传感器和云平台验证。

## 本次实板验证（2026-09-27）

- DAPLink / CMSIS-DAP 已完成 STM32F103C8 固件下载与读回校验。
- 最终 Debug 固件：Flash 43656 B / 64 KiB，RAM 9600 B / 20 KiB
  （链接器统计，包含预留堆栈）。
- 将热点改为 2.4 GHz 后，COM8 连续收到真实 SHT30 上报的 `result=OK code=200`。
- 结束验证时，通过 SWD 读取 `telemetry_upload_count=72`、
  `telemetry_last_status=ESP12F_OK`、`reply_code=200`，最近样本约
  28.73°C / 34.17%RH。程序继续运行并每 5 秒上报。
- 串口实测记录：`build/Debug/esp12f_board_final.log`。
- 下载前的 64 KiB Flash 备份：`build/Debug/before_esp12f_20260927_214609.bin`。
- 验证结束已释放串口和 OpenOCD，可继续使用 VS Code / 串口工具。

协议参考：[OneNET 接入](https://iot.10086.cn/doc/aiot/fuse/detail/919)、
[OneJSON 属性上报](https://iot.10086.cn/doc/iot_platform/book/device-connect%26manager/thing-model/protocol/OneJSON/property%26event.html)、
[ESP8266 TCP/IP AT](https://docs.espressif.com/projects/esp-at/en/release-v2.1.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html)、
[ESP8266 启动日志](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html)。
