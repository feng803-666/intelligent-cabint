#include "esp12f.h"
#include "esp12f_port.h"
#include <stdio.h>
#include <string.h>

#define PACKET_CAPACITY 768U
#define AT_OK       (1U << 0)
#define AT_ERROR    (1U << 1)
#define AT_PROMPT   (1U << 2)
#define AT_SENT     (1U << 3)
#define AT_MASK     0x1FU
#define MQTT_CONNACK (1U << 5)
#define MQTT_SUBACK  (1U << 6)
#define RESPONSE_MS 10000U

static struct
{
    uint8_t initialized, tcp, mqtt, ping_pending, connack_code, suback_code;
    uint8_t wifi_error;
    uint16_t events, packet_id, subscribe_id, keep_alive;
    ESP12F_Status fault;
    ESP12F_MessageHandler handler;
    uint32_t last_tx, ping_at;
    char line[256];
    uint16_t line_size;
    uint8_t drop_line;
    uint32_t ipd_remaining;
    uint8_t rx[PACKET_CAPACITY], tx[PACKET_CAPACITY];
    uint16_t rx_size, rx_header;
    uint32_t rx_remaining, rx_multiplier;
} esp;

static ESP12F_Status Fail(ESP12F_Status status)
{
    esp.fault = status;
    esp.mqtt = esp.tcp = 0U;
    return status;
}

static void MQTTDispatch(void)
{
    const uint8_t *body = esp.rx + esp.rx_header;
    uint32_t size = esp.rx_remaining;
    switch (esp.rx[0])
    {
        case 0x20U:
            if ((size != 2U) || (body[0] != 0U) || (body[1] > 5U))
                { (void)Fail(ESP12F_ERROR_PROTOCOL); break; }
            esp.connack_code = body[1];
            esp.events |= MQTT_CONNACK;
            break;
        case 0x90U:
            if ((size != 3U) || ((body[2] != 0U) && (body[2] != 0x80U)))
                { (void)Fail(ESP12F_ERROR_PROTOCOL); break; }
            if ((((uint16_t)body[0] << 8) | body[1]) == esp.subscribe_id)
            {
                esp.suback_code = body[2];
                esp.events |= MQTT_SUBACK;
            }
            break;
        case 0xD0U:
            if (size != 0U) (void)Fail(ESP12F_ERROR_PROTOCOL);
            else esp.ping_pending = 0U;
            break;
        default:
            /* 订阅请求的最高 QoS 为 0；只接受 QoS 0 PUBLISH（含 retain）。 */
            if ((esp.rx[0] & 0xFEU) == 0x30U)
            {
                uint16_t topic_size;
                if (size < 2U) { (void)Fail(ESP12F_ERROR_PROTOCOL); break; }
                topic_size = (uint16_t)(((uint16_t)body[0] << 8) | body[1]);
                if ((topic_size == 0U) || ((uint32_t)topic_size + 2U > size))
                    { (void)Fail(ESP12F_ERROR_PROTOCOL); break; }
                if (esp.handler != NULL)
                    esp.handler(body + 2U, topic_size, body + 2U + topic_size,
                                (uint16_t)(size - 2U - topic_size));
            }
            else (void)Fail(ESP12F_ERROR_PROTOCOL);
            break;
    }
}

static void MQTTByte(uint8_t byte)
{
    if (esp.rx_size >= sizeof(esp.rx)) { (void)Fail(ESP12F_ERROR_OVERFLOW); return; }
    esp.rx[esp.rx_size++] = byte;
    if (esp.rx_size == 1U)
    {
        esp.rx_remaining = 0U;
        esp.rx_multiplier = 1U;
        esp.rx_header = 0U;
        return;
    }
    if (esp.rx_header == 0U)
    {
        esp.rx_remaining += (uint32_t)(byte & 0x7FU) * esp.rx_multiplier;
        if ((byte & 0x80U) != 0U)
        {
            if (esp.rx_size >= 5U) { (void)Fail(ESP12F_ERROR_PROTOCOL); return; }
            esp.rx_multiplier *= 128U;
            return;
        }
        esp.rx_header = esp.rx_size;
        if (esp.rx_remaining > sizeof(esp.rx) - esp.rx_header)
            { (void)Fail(ESP12F_ERROR_OVERFLOW); return; }
    }
    if (esp.rx_size == esp.rx_header + esp.rx_remaining)
    {
        MQTTDispatch();
        esp.rx_size = 0U;
    }
}

static void LineReceived(void)
{
    esp.line[esp.line_size] = '\0';
    if ((esp.line_size == 8U) && (memcmp(esp.line, "+CWJAP:", 7U) == 0) &&
        (esp.line[7] >= '1') && (esp.line[7] <= '4'))
        esp.wifi_error = (uint8_t)(esp.line[7] - '0');
    if (strcmp(esp.line, "OK") == 0) esp.events |= AT_OK;
    else if ((strcmp(esp.line, "ERROR") == 0) || (strcmp(esp.line, "FAIL") == 0) ||
             (strcmp(esp.line, "SEND FAIL") == 0) ||
             (strncmp(esp.line, "busy", 4U) == 0)) esp.events |= AT_ERROR;
    else if (strcmp(esp.line, "SEND OK") == 0) esp.events |= AT_SENT;
    else if (strcmp(esp.line, "ready") == 0)
    {
        if (esp.initialized) (void)Fail(ESP12F_ERROR_DISCONNECTED);
    }
    else if ((strcmp(esp.line, "CLOSED") == 0) ||
             (strcmp(esp.line, "WIFI DISCONNECT") == 0))
    {
        if (esp.tcp || esp.mqtt) (void)Fail(ESP12F_ERROR_DISCONNECTED);
    }
    esp.line_size = 0U;
}

static void ReceiveByte(uint8_t byte)
{
    if (esp.ipd_remaining != 0U)
    {
        --esp.ipd_remaining;
        MQTTByte(byte);
        return;
    }
    if (esp.drop_line)
    {
        if (byte == '\n') { esp.drop_line = 0U; esp.line_size = 0U; }
        return;
    }
    if ((byte == ':') && (esp.line_size >= 6U) &&
        (memcmp(esp.line, "+IPD,", 5U) == 0))
    {
        uint32_t length = 0U;
        for (uint16_t i = 5U; i < esp.line_size; ++i)
        {
            if ((esp.line[i] < '0') || (esp.line[i] > '9') || (length > 6553U))
                { (void)Fail(ESP12F_ERROR_PROTOCOL); return; }
            length = length * 10U + (uint32_t)(esp.line[i] - '0');
        }
        if (length > 65535U) { (void)Fail(ESP12F_ERROR_PROTOCOL); return; }
        esp.ipd_remaining = length;
        esp.line_size = 0U;
    }
    else if (byte == '\n') LineReceived();
    else if (byte == '\r') { /* CRLF 文本；二进制走上面的长度分支。 */ }
    else if ((byte == '>') && (esp.line_size == 0U)) esp.events |= AT_PROMPT;
    else if ((byte == ' ') && (esp.line_size == 0U)) { /* 提示符后的空格。 */ }
    else if (esp.line_size < sizeof(esp.line) - 1U) esp.line[esp.line_size++] = (char)byte;
    else { esp.drop_line = 1U; esp.line_size = 0U; }
}

static ESP12F_Status Receive(void)
{
    uint8_t byte;
    int result;
    if (esp.fault != ESP12F_OK) return esp.fault;
    while ((result = ESP12F_Port_ReadByte(&byte)) > 0)
    {
        ReceiveByte(byte);
        if (esp.fault != ESP12F_OK) return esp.fault;
    }
    return result < 0 ? Fail(ESP12F_ERROR_UART) : ESP12F_OK;
}

static ESP12F_Status Wait(uint16_t event, uint32_t timeout)
{
    uint32_t start = ESP12F_Port_Tick();
    do
    {
        ESP12F_Status status = Receive();
        if (status != ESP12F_OK) return status;
        if ((esp.events & AT_ERROR) != 0U) return Fail(ESP12F_ERROR_AT);
        if ((esp.events & event) != 0U) return ESP12F_OK;
        ESP12F_Port_Idle();
    } while ((uint32_t)(ESP12F_Port_Tick() - start) < timeout);
    return Fail(ESP12F_ERROR_TIMEOUT);
}

static ESP12F_Status Command(const char *command, uint16_t event, uint32_t timeout)
{
    ESP12F_Status status = Receive();
    if (status != ESP12F_OK) return status;
    esp.events &= (uint16_t)~AT_MASK;
    status = ESP12F_Port_Transmit((const uint8_t *)command, (uint16_t)strlen(command));
    return status == ESP12F_OK ? Wait(event, timeout) : Fail(status);
}

static void Delay(uint32_t milliseconds)
{
    uint32_t start = ESP12F_Port_Tick();
    while ((uint32_t)(ESP12F_Port_Tick() - start) < milliseconds)
    {
        ESP12F_Port_Idle();
    }
}

ESP12F_Status ESP12F_Init(void)
{
    ESP12F_Status status;
    memset(&esp, 0, sizeof(esp));
    status = ESP12F_Port_Init();
    if (status != ESP12F_OK) return Fail(status);
    Delay(1500U); /* 首次上电等待 AT 固件启动。 */
    ESP12F_Port_ClearRx();
    status = Command("AT\r\n", AT_OK, 2000U);
    if (status != ESP12F_OK) return status;
    /* ESP8266 ROM 用 74880 波特率打印启动日志，会触发 115200 接收端 FE。
     * 复位窗口内不解析消息，等待后丢弃乱码/帧错误，再用 AT 验证固件就绪。
     */
    status = ESP12F_Port_Transmit((const uint8_t *)"AT+RST\r\n", 8U);
    if (status != ESP12F_OK) return Fail(status);
    Delay(2000U);
    ESP12F_Port_ClearRx();
    esp.line_size = esp.rx_size = 0U;
    esp.drop_line = 0U;
    esp.ipd_remaining = 0U;
    status = Command("AT\r\n", AT_OK, 2000U);
    if (status != ESP12F_OK) return status;
    status = Command("ATE0\r\n", AT_OK, 2000U);
    if (status != ESP12F_OK) return status;
    status = Command("AT+CWMODE=1\r\n", AT_OK, 2000U);
    if (status != ESP12F_OK) return status;
    status = Command("AT+CIPMODE=0\r\n", AT_OK, 2000U);
    if (status != ESP12F_OK) return status;
    status = Command("AT+CIPMUX=0\r\n", AT_OK, 2000U);
    if (status != ESP12F_OK) return status;
    status = Command("AT+CIPDINFO=0\r\n", AT_OK, 2000U);
    if (status != ESP12F_OK) return status;
    /* 软件复位后的标准 ESP8266 AT 默认为主动 +IPD 接收，兼容旧固件。 */
    esp.initialized = 1U;
    return ESP12F_OK;
}

static uint8_t EscapeAT(const char *input, char *output, uint16_t max_input)
{
    uint16_t count = 0U;
    if (input == NULL) return 0U;
    while (*input != '\0')
    {
        char c = *input++;
        if ((++count > max_input) || ((uint8_t)c < 32U) || (c == 127)) return 0U;
        if ((c == '\\') || (c == '"') || (c == ',')) *output++ = '\\';
        *output++ = c;
    }
    *output = '\0';
    return 1U;
}

ESP12F_Status ESP12F_JoinWiFi(const char *ssid, const char *password)
{
    char escaped_ssid[65], escaped_password[129], command[224];
    if (!EscapeAT(ssid, escaped_ssid, 32U) || (ssid[0] == '\0') ||
        !EscapeAT(password, escaped_password, 64U)) return ESP12F_ERROR_PARAM;
    if (!esp.initialized) return ESP12F_ERROR_NOT_INIT;
    if (esp.tcp) return ESP12F_ERROR_PARAM;
    esp.wifi_error = 0U;
    (void)snprintf(command, sizeof(command), "AT+CWJAP=\"%s\",\"%s\"\r\n",
                   escaped_ssid, escaped_password);
    return Command(command, AT_OK, 30000U);
}

static uint8_t PutString(uint16_t *position, const char *string)
{
    size_t length;
    if (string == NULL) return 0U;
    length = strlen(string);
    if ((*position > sizeof(esp.tx) - 2U) ||
        (length > sizeof(esp.tx) - *position - 2U)) return 0U;
    esp.tx[(*position)++] = (uint8_t)(length >> 8);
    esp.tx[(*position)++] = (uint8_t)length;
    memcpy(esp.tx + *position, string, length);
    *position += (uint16_t)length;
    return 1U;
}

static ESP12F_Status SendPacket(uint8_t header, uint16_t body_end)
{
    char command[32];
    uint16_t remaining = body_end - 5U, header_size = 1U, total;
    ESP12F_Status status;
    esp.tx[0] = header;
    do
    {
        uint8_t digit = (uint8_t)(remaining % 128U);
        remaining /= 128U;
        if (remaining != 0U) digit |= 0x80U;
        esp.tx[header_size++] = digit;
    } while (remaining != 0U);
    total = body_end - 5U + header_size;
    memmove(esp.tx + header_size, esp.tx + 5U, body_end - 5U);
    (void)snprintf(command, sizeof(command), "AT+CIPSEND=%u\r\n", (unsigned)total);
    status = Command(command, AT_PROMPT, 3000U);
    if (status != ESP12F_OK) return status;
    esp.events &= (uint16_t)~AT_MASK;
    status = ESP12F_Port_Transmit(esp.tx, total);
    if (status != ESP12F_OK) return Fail(status);
    status = Wait(AT_SENT, RESPONSE_MS);
    if (status == ESP12F_OK) esp.last_tx = ESP12F_Port_Tick();
    return status;
}

ESP12F_Status ESP12F_MQTTConnect(const ESP12F_MQTTConfig *config,
                               ESP12F_MessageHandler handler)
{
    char host[193], command[240];
    uint16_t position = 15U;
    ESP12F_Status status;
    static const uint8_t connect_header[] = {0U, 4U, 'M', 'Q', 'T', 'T', 4U, 0xC2U};
    if ((config == NULL) || (config->port == 0U) ||
        (config->keep_alive_seconds < 20U) || (config->keep_alive_seconds > 600U) ||
        !EscapeAT(config->host, host, 96U) || (host[0] == '\0') ||
        (config->client_id == NULL) || (config->client_id[0] == '\0'))
        return ESP12F_ERROR_PARAM;
    if (!esp.initialized) return ESP12F_ERROR_NOT_INIT;
    if (esp.tcp) return ESP12F_ERROR_PARAM;
    memcpy(esp.tx + 5U, connect_header, sizeof(connect_header));
    esp.tx[13] = (uint8_t)(config->keep_alive_seconds >> 8);
    esp.tx[14] = (uint8_t)config->keep_alive_seconds;
    if (!PutString(&position, config->client_id) || !PutString(&position, config->username) ||
        !PutString(&position, config->password)) return ESP12F_ERROR_PARAM;
    (void)snprintf(command, sizeof(command), "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",
                   host, (unsigned)config->port);
    /* 在 CIPSTART 等待期间也能识别 CLOSED。 */
    esp.tcp = 1U;
    status = Command(command, AT_OK, 15000U);
    if (status != ESP12F_OK) return status;
    esp.handler = handler;
    esp.keep_alive = config->keep_alive_seconds;
    esp.events &= (uint16_t)~MQTT_CONNACK;
    status = SendPacket(0x10U, position);
    if (status != ESP12F_OK) return status;
    status = Wait(MQTT_CONNACK, RESPONSE_MS);
    if (status != ESP12F_OK) return status;
    if (esp.connack_code != 0U) return Fail(ESP12F_ERROR_MQTT_REJECTED);
    esp.mqtt = 1U;
    return ESP12F_OK;
}

ESP12F_Status ESP12F_MQTTSubscribe(const char *topic)
{
    uint16_t position = 7U;
    ESP12F_Status status;
    if ((topic == NULL) || (topic[0] == '\0')) return ESP12F_ERROR_PARAM;
    if (!esp.mqtt) return ESP12F_ERROR_DISCONNECTED;
    if (!PutString(&position, topic) || (position >= sizeof(esp.tx))) return ESP12F_ERROR_PARAM;
    if (++esp.packet_id == 0U) ++esp.packet_id;
    esp.subscribe_id = esp.packet_id;
    esp.tx[5] = (uint8_t)(esp.packet_id >> 8);
    esp.tx[6] = (uint8_t)esp.packet_id;
    esp.tx[position++] = 0U;
    esp.events &= (uint16_t)~MQTT_SUBACK;
    status = SendPacket(0x82U, position);
    if (status != ESP12F_OK) return status;
    status = Wait(MQTT_SUBACK, RESPONSE_MS);
    if (status != ESP12F_OK) return status;
    return esp.suback_code == 0U ? ESP12F_OK : Fail(ESP12F_ERROR_MQTT_REJECTED);
}

ESP12F_Status ESP12F_MQTTPublish(const char *topic, const void *payload, uint16_t size)
{
    uint16_t position = 5U;
    if ((topic == NULL) || (topic[0] == '\0') || ((payload == NULL) && (size != 0U)) ||
        (strchr(topic, '#') != NULL) || (strchr(topic, '+') != NULL)) return ESP12F_ERROR_PARAM;
    if (!esp.mqtt) return ESP12F_ERROR_DISCONNECTED;
    if (!PutString(&position, topic) || (size > sizeof(esp.tx) - position)) return ESP12F_ERROR_PARAM;
    if (size != 0U) memcpy(esp.tx + position, payload, size);
    return SendPacket(0x30U, position + size);
}

ESP12F_Status ESP12F_Poll(void)
{
    uint32_t now;
    ESP12F_Status status;
    if (!esp.initialized) return ESP12F_ERROR_NOT_INIT;
    status = Receive();
    if (status != ESP12F_OK) return status;
    if (!esp.mqtt) return ESP12F_ERROR_DISCONNECTED;
    now = ESP12F_Port_Tick();
    if (esp.ping_pending && ((uint32_t)(now - esp.ping_at) >= RESPONSE_MS))
        return Fail(ESP12F_ERROR_TIMEOUT);
    if (!esp.ping_pending && ((uint32_t)(now - esp.last_tx) >= (uint32_t)esp.keep_alive * 500U))
    {
        esp.ping_pending = 1U;
        esp.ping_at = now;
        return SendPacket(0xC0U, 5U);
    }
    return ESP12F_OK;
}

uint8_t ESP12F_IsConnected(void) { return esp.mqtt; }
uint8_t ESP12F_LastConnAck(void) { return esp.connack_code; }
uint8_t ESP12F_LastWiFiError(void) { return esp.wifi_error; }

const char *ESP12F_StatusString(ESP12F_Status status)
{
    static const char *const names[] = {"OK", "PARAM", "NOT_INIT", "UART", "TIMEOUT", "AT",
        "DISCONNECTED", "OVERFLOW", "PROTOCOL", "MQTT_REJECTED", "CLOUD_REJECTED"};
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "UNKNOWN";
}
