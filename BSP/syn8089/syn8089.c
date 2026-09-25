#include "syn8089.h"
#include "syn8089_port.h"
#include <stddef.h>
#include <string.h>

#define SYN8089_ACK_TIMEOUT_MS 500U
#define SYN8089_FRAME_GAP_MS   31U

/* 使用静态缓冲区，避免在 STM32F103 的栈上分配约 2 KB。 */
static uint8_t frame[SYN8089_MAX_TEXT_BYTES + 5U];
static uint8_t initialized;
static uint8_t pending;
static uint8_t frame_sent;
static uint32_t last_tx_tick;

static SYN8089_Status Fail(SYN8089_Status status)
{
    /* 不确定的应答不能归给下一条命令，要求复位后重新同步。 */
    initialized = 0U;
    return status;
}

static SYN8089_Status ProcessReplies(void)
{
    uint8_t byte;
    int result;
    while ((result = SYN8089_Port_ReadByte(&byte)) > 0)
    {
        if (byte == 0x4FU) pending = 0U;
        if (byte == 0x45U) return Fail(SYN8089_ERROR_REJECTED);
        if (byte == 0x4AU) return Fail(SYN8089_ERROR_NOT_INIT);
    }
    return (result < 0) ? Fail(SYN8089_ERROR_UART) : SYN8089_OK;
}

static SYN8089_Status WaitReply(uint8_t expected, uint32_t timeout_ms)
{
    uint32_t start = SYN8089_Port_Tick();
    for (;;)
    {
        uint8_t byte;
        int result = SYN8089_Port_ReadByte(&byte);
        if (result < 0) return Fail(SYN8089_ERROR_UART);
        if (result > 0)
        {
            if (byte == expected) return SYN8089_OK;
            if ((byte == 0x45U) && (expected != 0x4AU))
                return Fail(SYN8089_ERROR_REJECTED);
            if ((byte == 0x4AU) && (expected != 0x4AU))
                return Fail(SYN8089_ERROR_NOT_INIT);
            if (byte == 0x4FU) pending = 0U;
        }
        if ((uint32_t)(SYN8089_Port_Tick() - start) >= timeout_ms)
            return Fail(SYN8089_ERROR_TIMEOUT);
        /* 有数据时连续取出，避免人为增加连续应答的处理间隔。 */
        if (result == 0) SYN8089_Port_Delay(1U);
    }
}

static SYN8089_Status SendFrame(uint8_t command, uint8_t encoding,
                               const char *text, uint16_t length)
{
    SYN8089_Status status;
    uint16_t payload = (uint16_t)(1U + ((text != NULL) ? length + 1U : 0U));
    if (!initialized) return SYN8089_ERROR_NOT_INIT;
    if (frame_sent)
    {
        uint32_t elapsed = SYN8089_Port_Tick() - last_tx_tick;
        if (elapsed < SYN8089_FRAME_GAP_MS)
            SYN8089_Port_Delay(SYN8089_FRAME_GAP_MS - elapsed);
    }
    status = ProcessReplies();
    if (status != SYN8089_OK) return status;
    frame[0] = 0xFDU;
    frame[1] = (uint8_t)(payload >> 8);
    frame[2] = (uint8_t)payload;
    frame[3] = command;
    if (text != NULL)
    {
        frame[4] = encoding;
        memcpy(&frame[5], text, length);
    }
    /* 一次发送完整帧，保证帧内不会因分段调用产生大于 30 ms 的间隔。 */
    status = SYN8089_Port_Transmit(frame, (uint16_t)(payload + 3U));
    last_tx_tick = SYN8089_Port_Tick();
    frame_sent = 1U;
    if (status != SYN8089_OK) return Fail(status);
    if ((command == 0x01U) || (command == 0x06U)) pending = 1U;
    /* 参数帧以 0x4F 为最终成功依据，兼容中间附带 0x41 的回传。 */
    return WaitReply((command == 0x06U) ? 0x4FU : 0x41U,
                     SYN8089_ACK_TIMEOUT_MS);
}

SYN8089_Status SYN8089_Init(void)
{
    return SYN8089_Reset();
}

SYN8089_Status SYN8089_Reset(void)
{
    SYN8089_Status status;
    initialized = 0U;
    pending = 0U;
    frame_sent = 0U;
    status = SYN8089_Port_Init();
    if (status != SYN8089_OK) return status;
    SYN8089_Port_SetReset(0U);
    SYN8089_Port_Delay(1100U);
    SYN8089_Port_ClearRx();
    SYN8089_Port_SetReset(1U);
    /* 启动前可能带有杂字节，只要遇到 0x4A 就表示初始化完成。 */
    status = WaitReply(0x4AU, 2000U);
    if (status == SYN8089_OK) initialized = 1U;
    return status;
}

SYN8089_Status SYN8089_IsBusy(uint8_t *busy)
{
    SYN8089_Status status;
    if (busy == NULL) return SYN8089_ERROR_PARAM;
    if (!initialized) return SYN8089_ERROR_NOT_INIT;
    status = ProcessReplies();
    if (status == SYN8089_OK)
        *busy = (uint8_t)(pending || SYN8089_Port_Busy());
    return status;
}

SYN8089_Status SYN8089_WaitIdle(uint32_t timeout_ms)
{
    uint32_t start = SYN8089_Port_Tick();
    for (;;)
    {
        uint8_t busy;
        SYN8089_Status status = SYN8089_IsBusy(&busy);
        if (status != SYN8089_OK) return status;
        if (!busy) return SYN8089_OK;
        if ((uint32_t)(SYN8089_Port_Tick() - start) >= timeout_ms)
            return SYN8089_ERROR_TIMEOUT;
        SYN8089_Port_Delay(1U);
    }
}

/* 严格验证 UTF-8，拒绝过长编码、代理项、截断和超出 Unicode 范围的值。 */
static uint8_t ValidUTF8(const uint8_t *text, uint16_t length)
{
    uint16_t i = 0U;
    while (i < length)
    {
        uint8_t lead = text[i++];
        uint8_t count;
        uint32_t value;
        uint32_t minimum;
        if (lead < 0x80U) continue;
        if ((lead >= 0xC2U) && (lead <= 0xDFU))
        { count = 1U; value = lead & 0x1FU; minimum = 0x80U; }
        else if ((lead >= 0xE0U) && (lead <= 0xEFU))
        { count = 2U; value = lead & 0x0FU; minimum = 0x800U; }
        else if ((lead >= 0xF0U) && (lead <= 0xF4U))
        { count = 3U; value = lead & 0x07U; minimum = 0x10000U; }
        else return 0U;
        if ((uint16_t)(length - i) < count) return 0U;
        while (count-- != 0U)
        {
            uint8_t next = text[i++];
            if ((next & 0xC0U) != 0x80U) return 0U;
            value = (value << 6) | (next & 0x3FU);
        }
        if ((value < minimum) || (value > 0x10FFFFU) ||
            ((value >= 0xD800U) && (value <= 0xDFFFU))) return 0U;
    }
    return 1U;
}

static SYN8089_Status CheckIdle(void)
{
    uint8_t busy;
    SYN8089_Status status = SYN8089_IsBusy(&busy);
    if (status != SYN8089_OK) return status;
    return busy ? SYN8089_ERROR_BUSY : SYN8089_OK;
}

SYN8089_Status SYN8089_SpeakUTF8(const char *text)
{
    uint16_t length = 0U;
    SYN8089_Status status;
    if (text == NULL) return SYN8089_ERROR_PARAM;
    while ((length <= SYN8089_MAX_TEXT_BYTES) && (text[length] != '\0')) ++length;
    if ((length == 0U) || (length > SYN8089_MAX_TEXT_BYTES) ||
        !ValidUTF8((const uint8_t *)text, length)) return SYN8089_ERROR_PARAM;
    status = CheckIdle();
    if (status != SYN8089_OK) return status;
    return SendFrame(0x01U, 0x05U, text, length);
}

SYN8089_Status SYN8089_Stop(void)
{
    SYN8089_Status status = SendFrame(0x02U, 0U, NULL, 0U);
    if (status == SYN8089_OK) pending = 0U;
    return status;
}

SYN8089_Status SYN8089_Pause(void)
{
    return SendFrame(0x03U, 0U, NULL, 0U);
}

SYN8089_Status SYN8089_Resume(void)
{
    return SendFrame(0x04U, 0U, NULL, 0U);
}

static SYN8089_Status SetParameter(char key, uint8_t value)
{
    char text[6];
    uint16_t length = 0U;
    SYN8089_Status status = CheckIdle();
    if (status != SYN8089_OK) return status;
    text[length++] = '[';
    text[length++] = key;
    if (value >= 10U) text[length++] = (char)('0' + value / 10U);
    text[length++] = (char)('0' + value % 10U);
    text[length++] = ']';
    /* 参数标记均为 ASCII，采用手册规定的 0x06 / 0x01 参数帧。 */
    status = SendFrame(0x06U, 0x01U, text, length);
    if (status == SYN8089_OK) pending = 0U;
    return status;
}

SYN8089_Status SYN8089_SetVolume(uint8_t volume)
{
    if (volume > 10U) return SYN8089_ERROR_PARAM;
    return SetParameter('v', volume);
}

SYN8089_Status SYN8089_SetSpeed(uint8_t speed)
{
    if ((speed < 1U) || (speed > 30U)) return SYN8089_ERROR_PARAM;
    return SetParameter('s', speed);
}
