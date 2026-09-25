#ifndef SYN8089_H
#define SYN8089_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYN8089_MAX_TEXT_BYTES 2000U

typedef enum
{
    SYN8089_OK = 0,
    SYN8089_ERROR_PARAM,
    SYN8089_ERROR_NOT_INIT,
    SYN8089_ERROR_BUSY,
    SYN8089_ERROR_UART,
    SYN8089_ERROR_TIMEOUT,
    SYN8089_ERROR_REJECTED
} SYN8089_Status;

/* 单模块驱动，独占 USART3；所有应用接口须在主循环/任务中串行调用，
 * 不可在中断中调用。调用前先初始化 HAL 时基、GPIO 和 USART3。
 * 固定接线：RESET=PA11，R/B=PA12，TX=PB10，RX=PB11。
 * BAUD0、BAUD1 悬空：115200、8 数据位、1 停止位、无校验。
 */

/* 初始化接收中断，复位模块并等待 0x4A；最长约 3.1 秒。 */
SYN8089_Status SYN8089_Init(void);
/* 故障恢复：RESET 拉低 1100 ms 后释放，最多等待启动应答 2000 ms。
 * 超时/通信异常后须重新 Init 或 Reset；复位不恢复芯片保存的音色参数。
 */
SYN8089_Status SYN8089_Reset(void);

/* 发送以 NUL 结尾的 UTF-8 文本，1~2000 字节（不含末尾 NUL）。
 * 忙时返回 BUSY，不打断已有播报；需打断时先 Stop。
 * 返回 OK 表示收到接收成功应答，不表示声音已播放完毕。
 * 参数含文本控制标记时，芯片会保存相应参数，避免无意义地反复配置。
 * 文本必须是有效 UTF-8；非法、截断或超长文本均拒绝发送。
 */
SYN8089_Status SYN8089_SpeakUTF8(const char *text);
SYN8089_Status SYN8089_Stop(void);
SYN8089_Status SYN8089_Pause(void);
SYN8089_Status SYN8089_Resume(void);

/* 读取忙状态并处理串口回传；忙状态包括等待开始播报和暂停。
 * 仅成功时更新 busy：1=忙，0=空闲。主循环可反复调用，不等待播报。
 */
SYN8089_Status SYN8089_IsBusy(uint8_t *busy);
/* 等待当前播报结束，timeout_ms=0 仅检查一次；超时不停止播报。
 * 必须有运行中的毫秒时基；暂停期间也会超时，需要 Resume 或 Stop。
 */
SYN8089_Status SYN8089_WaitIdle(uint32_t timeout_ms);

/* 设置音量 0~10、语速 1~30，等待参数配置完成后返回。
 * 参数由芯片断电保存；仅在用户需要改变设置时调用，忙时返回 BUSY。
 */
SYN8089_Status SYN8089_SetVolume(uint8_t volume);
SYN8089_Status SYN8089_SetSpeed(uint8_t speed);

#ifdef __cplusplus
}
#endif
#endif
