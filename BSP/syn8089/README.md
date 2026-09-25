# SYN8089 贴片语音模块驱动

源码、中文注释、字符串均为 UTF-8。CMake 显式指定 GCC 输入和执行字符集为 UTF-8。

## 接线与初始化

| 模块引脚 | 本板连接/配置 |
| --- | --- |
| RXD | PB10 / USART3_TX |
| TXD | PB11 / USART3_RX |
| RESET | PA11 / SYN_RES，低有效 |
| R/B | PA12 / SYN_R_B，高忙、低空闲 |
| BAUD0、BAUD1 | 悬空，内部上拉，对应 115200 bps |
| AO | 悬空；使用板载功放时喇叭接 SP+、SP- |
| VCC、GND | 模块 5 V 供电，与 MCU 共地 |

先执行 `HAL_Init()`、时钟、`MX_GPIO_Init()`、`MX_USART3_UART_Init()`，再调用
`SYN8089_Init()`。现有 GPIO 和串口配置已经匹配，无需修改 `.ioc`。
RESET 拉低 1100 ms，释放后最多等待 2000 ms 内出现启动回传 `0x4A`。
初始化失败会返回错误，不无限等待，也不会让主程序进入 Error_Handler。

驱动独占 USART3。平台层在初始化时启用 USART3 中断，以 32 字节环形队列缓存回传，
`stm32f1xx_it.c` 的用户代码区负责转发中断；不要同时使用 HAL 的 USART3 接收、
DMA、IT 发送或另一套 USART3 中断处理。USART1 仍用于 printf 调试。
CubeMX 中保持 USART3 NVIC 选项关闭，由驱动启用，避免生成第二份中断入口。

## 对外 API

接口定义和返回值见 `syn8089.h`。仅在主循环/任务中串行调用，不支持重入或中断调用。

| API | 行为 |
| --- | --- |
| `SYN8089_Init / Reset` | 初始化接收、硬件复位、等待启动成功 |
| `SYN8089_SpeakUTF8(text)` | 发送 1~2000 字节 UTF-8 文本，等待接收成功，随后异步播报 |
| `SYN8089_IsBusy(&busy)` | 处理回传并读取 R/B，不阻塞等待播报 |
| `SYN8089_WaitIdle(timeout_ms)` | 阻塞等待结束；超时不自动停止播报 |
| `SYN8089_Stop / Pause / Resume` | 停止、暂停、恢复，等待命令接收成功 |
| `SYN8089_SetVolume(volume)` | 音量 0~10，0 为静音，等待配置完成 |
| `SYN8089_SetSpeed(speed)` | 语速 1~30，等待配置完成 |

`SpeakUTF8` 忙时返回 `SYN8089_ERROR_BUSY`；如需打断，先停止并等待空闲。
收到播放接收应答后，即使 R/B 尚未拉高，也保持忙状态直至收到 `0x4F`；
暂停也保持忙状态。接收溢出、UART 错误、命令拒收、应答超时都会要求重新复位同步，
避免迟到应答被错误归给后续命令。`WaitIdle` 的播报等待超时不会使驱动失效。

文本帧：`FD + (文本字节数+2，大端两字节) + 01 + 05 + UTF-8文本`，
不发送 NUL、不附加校验字节。普通命令：`FD 00 01 命令字`。
完整帧一次发送，帧间至少等待 31 ms；发送最多等待 500 ms，命令应答最多等待 500 ms。
参数帧使用 `06 01`，以 `0x4F` 确认设置完成后才允许下一次调用。
有效 UTF-8 不代表所有 Unicode 字符都有对应发音，实际字库以模块为准。

```c
SYN8089_Status status = SYN8089_Init();
if (status == SYN8089_OK)
    status = SYN8089_SpeakUTF8("[g1]你好，欢迎使用智能柜。");
if (status == SYN8089_OK)
    status = SYN8089_WaitIdle(30000U);
```

控制标记也是文本字节数的一部分，半角 `[g1]` 指定中文，`[g2]` 指定英文。
音量、语速、语种等控制标记会由模块保存并全局生效，断电不丢失。
生产应用仅在需要修改时配置；不要在循环中重复写入。

## 应用集成状态与使用步骤

实板上电播报测试已由用户确认成功。main.c 中的测试初始化、播报、调试变量、
printf 日志和完成检测均已移除；驱动、串口接收中断及构建配置保留。
目前上电不会自动调用 SYN8089_Init，也不会自动播报。

需要使用语音的源文件先包含 `syn8089.h`。在 main.c 的 `USER CODE BEGIN 2`
区域调用一次 `SYN8089_Init()`，此时 GPIO 和 USART3 已初始化完成。
初始化成功后，在开门、关门、按键等业务事件发生时调用 `SYN8089_SpeakUTF8()`。

```c
/* 初始化：每次 MCU 上电调用一次，不能放进 while (1) 里反复执行。 */
SYN8089_Status status = SYN8089_Init();
if (status != SYN8089_OK)
{
    /* 初始化失败，记录错误；后续可使用 SYN8089_Reset() 重试。 */
}

/* 业务事件：例如检测到柜门刚刚打开时执行一次。 */
status = SYN8089_SpeakUTF8("柜门已打开，请取出物品。");
```

返回值 `status` 是本次调用的结果：`SYN8089_OK` 表示调用成功。
对 SpeakUTF8 而言，这只代表模块接收成功，此时声音可能还在播放。
若返回 `SYN8089_ERROR_BUSY`，本句没有被接收进播放队列；需要应用稍后重试。
驱动本身不排队，也不自动重复播报。

连续播放两句可以这样写：

```c
SYN8089_Status status = SYN8089_SpeakUTF8("柜门已打开。");
if (status == SYN8089_OK)
    status = SYN8089_WaitIdle(10000U); /* 最多等 10 秒，提前结束就立即返回 */
if (status == SYN8089_OK)
    status = SYN8089_SpeakUTF8("请取出物品，并关闭柜门。");
```

WaitIdle 会让当前主循环等待，期间 LED_Task 等主循环函数不会运行。
如果希望主循环继续处理按键、屏幕和呼吸灯，改用 IsBusy 每次检查一下：

```c
uint8_t busy;
SYN8089_Status status = SYN8089_IsBusy(&busy);
if (status == SYN8089_OK)
{
    if (busy == 0U)
    {
        /* 模块空闲；只有存在待播报事件时才发送下一句。 */
    }
}
```

`&busy` 是把变量的位置交给函数，让函数把结果写进去：0 表示空闲，1 表示忙。
必须先检查 status 成功，才可以使用 busy 的结果。
不要把 SpeakUTF8 无条件放进 while (1)，否则每次播完又会重播；
也不要只判断“柜门一直开着”，应判断“柜门刚从关闭变成打开”。

音量可用 `SYN8089_SetVolume(1)`，语速可用 `SYN8089_SetSpeed(5)`，
均应在初始化成功且空闲时调用，并检查返回值。模块保存这些设置，普通播报无需重复设置。
Pause 暂时停住，Resume 从暂停处继续；Stop 结束当前句，再次播放需重新调用 SpeakUTF8。
需要插播时，按 Stop → WaitIdle → SpeakUTF8 顺序执行，每一步成功才继续。

## 验证

在 `project` 目录执行：

```powershell
cmake --build --preset Debug
gcc -std=c11 -Wall -Wextra -Werror -finput-charset=UTF-8 -fexec-charset=UTF-8 -I BSP/syn8089 tests/test_syn8089.c BSP/syn8089/syn8089.c -o build/test_syn8089.exe
./build/test_syn8089.exe
```

模拟测试覆盖中文帧字节、2000 字节边界、非法 UTF-8、忙保护、暂停/恢复/停止、
紧邻 ACK/完成回传、参数设置、启动杂字节、超时及故障恢复、时基回绕。
用户已确认实板播报测试成功；模拟测试不代替引脚波形、电气性能等测量。

依据：[SYN8089 用户手册](https://www.doc.voicetx.com/home/SYN8089)、
[串口命令说明](https://public.voicetx.com/zh/home/uart)、
[文本控制标记](https://public.voicetx.com/zh/home/mark_ce)。
