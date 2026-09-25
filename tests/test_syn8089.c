/* 主机模拟测试：验证协议、边界、连续回传及故障恢复，不依赖 STM32 HAL。 */
#include "syn8089.h"
#include "syn8089_port.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t tick, reset_at, previous_tx;
static uint8_t queue[64], head, tail, pin_busy, rx_fault;
static uint8_t boot_reply = 1U, ack_reply = 1U, reject_reply, finish_reply;
static uint8_t parameter_ack = 1U;
static uint8_t tx[2005];
static uint16_t tx_length;
static unsigned transmissions, tx_since_reset;
static SYN8089_Status tx_result = SYN8089_OK;

static void Push(uint8_t byte) { assert(head < sizeof(queue)); queue[head++] = byte; }
SYN8089_Status SYN8089_Port_Init(void) { return SYN8089_OK; }
void SYN8089_Port_ClearRx(void) { head = tail = rx_fault = 0U; }
int SYN8089_Port_ReadByte(uint8_t *byte)
{
    if (rx_fault) return -1;
    if (tail == head) { head = tail = 0U; return 0; }
    *byte = queue[tail++];
    return 1;
}
void SYN8089_Port_SetReset(uint8_t high)
{
    if (!high) { reset_at = tick; tx_since_reset = 0U; }
    else
    {
        assert((uint32_t)(tick - reset_at) >= 1100U);
        if (boot_reply) { Push(0x00); Push(0x45); Push(0xF0); Push(0x4A); }
    }
}
uint8_t SYN8089_Port_Busy(void) { return pin_busy; }
uint32_t SYN8089_Port_Tick(void) { return tick; }
void SYN8089_Port_Delay(uint32_t ms) { tick += ms; }
void SYN8089_Port_IRQHandler(void) { }
SYN8089_Status SYN8089_Port_Transmit(const uint8_t *data, uint16_t size)
{
    if (tx_since_reset) assert((uint32_t)(tick - previous_tx) > 30U);
    previous_tx = tick;
    ++tx_since_reset;
    ++transmissions;
    assert(size <= sizeof(tx));
    memcpy(tx, data, size);
    tx_length = size;
    assert(tx[0] == 0xFD && (((unsigned)tx[1] << 8) | tx[2]) == size - 3U);
    if (tx_result != SYN8089_OK) return tx_result;
    if (reject_reply) { Push(0x45); return SYN8089_OK; }
    if (ack_reply && ((tx[3] != 0x06) || parameter_ack)) Push(0x41);
    if ((tx[3] == 0x06) || finish_reply) Push(0x4F);
    return SYN8089_OK;
}

int main(void)
{
    uint8_t busy = 7U;
    unsigned before;
    char long_text[2002];
    const uint8_t expected[] = {0xFD, 0x00, 0x08, 0x01, 0x05,
                               0xE4, 0xBD, 0xA0, 0xE5, 0xA5, 0xBD};
    assert(SYN8089_IsBusy(&busy) == SYN8089_ERROR_NOT_INIT && busy == 7U);
    assert(SYN8089_Init() == SYN8089_OK);
    assert(SYN8089_SpeakUTF8("你好") == SYN8089_OK);
    assert(tx_length == sizeof(expected) && memcmp(tx, expected, sizeof(expected)) == 0);
    /* 已接收但 R/B 还未拉高时，不能被误判为空闲。 */
    assert(SYN8089_IsBusy(&busy) == SYN8089_OK && busy == 1U);
    before = transmissions;
    assert(SYN8089_SpeakUTF8("下一句") == SYN8089_ERROR_BUSY);
    assert(SYN8089_SetVolume(5) == SYN8089_ERROR_BUSY);
    assert(transmissions == before);
    assert(SYN8089_WaitIdle(0) == SYN8089_ERROR_TIMEOUT);
    assert(SYN8089_Pause() == SYN8089_OK && tx[3] == 0x03 && tx_length == 4);
    assert(SYN8089_WaitIdle(10) == SYN8089_ERROR_TIMEOUT);
    assert(SYN8089_Resume() == SYN8089_OK && tx[3] == 0x04);
    Push(0x4F);
    pin_busy = 1U;
    assert(SYN8089_IsBusy(&busy) == SYN8089_OK && busy == 1U);
    pin_busy = 0U;
    assert(SYN8089_WaitIdle(0) == SYN8089_OK);
    /* ACK 后紧跟完成回传，不能被接收清空操作丢弃。 */
    finish_reply = 1U;
    assert(SYN8089_SpeakUTF8("测试") == SYN8089_OK);
    assert(SYN8089_WaitIdle(0) == SYN8089_OK);
    finish_reply = 0U;
    assert(SYN8089_SetVolume(10) == SYN8089_OK);
    assert(tx[3] == 0x06 && tx[4] == 0x01 && tx_length == 10);
    assert(memcmp(tx + 5, "[v10]", 5) == 0);
    parameter_ack = 0U;
    assert(SYN8089_SetSpeed(30) == SYN8089_OK);
    assert(memcmp(tx + 5, "[s30]", 5) == 0);
    assert(SYN8089_WaitIdle(0) == SYN8089_OK);

    before = transmissions;
    assert(SYN8089_SpeakUTF8(NULL) == SYN8089_ERROR_PARAM);
    assert(SYN8089_SpeakUTF8("") == SYN8089_ERROR_PARAM);
    assert(SYN8089_SpeakUTF8("\xE4\xBD") == SYN8089_ERROR_PARAM);
    assert(SYN8089_SpeakUTF8("\xC0\xAF") == SYN8089_ERROR_PARAM);
    assert(SYN8089_SpeakUTF8("\xED\xA0\x80") == SYN8089_ERROR_PARAM);
    assert(SYN8089_SpeakUTF8("\xF4\x90\x80\x80") == SYN8089_ERROR_PARAM);
    assert(SYN8089_SpeakUTF8("\xE4\x41\x80") == SYN8089_ERROR_PARAM);
    assert(SYN8089_SetVolume(11) == SYN8089_ERROR_PARAM);
    assert(SYN8089_SetSpeed(0) == SYN8089_ERROR_PARAM);
    assert(SYN8089_SetSpeed(31) == SYN8089_ERROR_PARAM);
    assert(SYN8089_IsBusy(NULL) == SYN8089_ERROR_PARAM);
    memset(long_text, 'a', 2001);
    long_text[2001] = '\0';
    assert(SYN8089_SpeakUTF8(long_text) == SYN8089_ERROR_PARAM);
    assert(transmissions == before);
    long_text[2000] = '\0';
    assert(SYN8089_SpeakUTF8(long_text) == SYN8089_OK);
    assert(tx_length == 2005 && tx[1] == 0x07 && tx[2] == 0xD2);
    assert(SYN8089_Stop() == SYN8089_OK && tx[3] == 0x02);
    assert(SYN8089_WaitIdle(0) == SYN8089_OK);

    /* 超时后迟到的 ACK 不能误认为下一条指令的 ACK。 */
    ack_reply = 0U;
    assert(SYN8089_SpeakUTF8("超时") == SYN8089_ERROR_TIMEOUT);
    Push(0x41);
    assert(SYN8089_SpeakUTF8("拒绝发送") == SYN8089_ERROR_NOT_INIT);
    ack_reply = 1U;
    assert(SYN8089_Reset() == SYN8089_OK);
    reject_reply = 1U;
    assert(SYN8089_SpeakUTF8("拒收") == SYN8089_ERROR_REJECTED);
    reject_reply = 0U;
    assert(SYN8089_Reset() == SYN8089_OK);
    tx_result = SYN8089_ERROR_UART;
    assert(SYN8089_Stop() == SYN8089_ERROR_UART);
    tx_result = SYN8089_OK;
    assert(SYN8089_Reset() == SYN8089_OK);
    rx_fault = 1U;
    busy = 7U;
    assert(SYN8089_IsBusy(&busy) == SYN8089_ERROR_UART && busy == 7U);
    assert(SYN8089_Reset() == SYN8089_OK);
    Push(0x4A);
    assert(SYN8089_IsBusy(&busy) == SYN8089_ERROR_NOT_INIT);
    boot_reply = 0U;
    assert(SYN8089_Reset() == SYN8089_ERROR_TIMEOUT);
    boot_reply = 1U;
    assert(SYN8089_Init() == SYN8089_OK);
    /* 毫秒计数回绕不能破坏帧间隔或等待超时。 */
    tick = UINT32_MAX - 10U;
    assert(SYN8089_SpeakUTF8("回绕") == SYN8089_OK);
    assert(SYN8089_WaitIdle(20) == SYN8089_ERROR_TIMEOUT);
    assert(SYN8089_Stop() == SYN8089_OK);
    puts("SYN8089 protocol tests passed.");
    return 0;
}
