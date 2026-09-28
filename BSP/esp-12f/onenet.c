#include "onenet.h"
#include "esp12f_port.h"
#include "onenet_credentials.h"
#include <stdio.h>
#include <string.h>

#define POST_TOPIC "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post"
#define REPLY_TOPIC POST_TOPIC "/reply"
#define SET_TOPIC "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set"
#define SET_REPLY_TOPIC SET_TOPIC "_reply"
#define REPLY_TIMEOUT_MS 10000U
#define CONTROL_QUEUE_SIZE 4U

static uint32_t message_id;
static char pending_id[11];
static uint8_t pending, reply_received;
static uint16_t reply_code;
static const char *stage = "ESP";
static OneNET_ControlHandler control_handler;
typedef struct
{
    char id[14]; /* OneJSON 数字字符串 ID 最长 13 位。 */
    uint8_t mask, lock, relay;
    uint16_t code;
} Control;
static Control controls[CONTROL_QUEUE_SIZE];
static unsigned control_head, control_count;

/* 有界 JSON 读取，只提取顶层 id/code，不能把 msg 或嵌套对象中的 code 当应答。 */
static void White(const uint8_t **p, const uint8_t *end)
{
    while ((*p < end) && ((**p == ' ') || (**p == '\t') || (**p == '\r') || (**p == '\n'))) ++*p;
}

static uint8_t String(const uint8_t **p, const uint8_t *end,
                      const uint8_t **start, uint16_t *size)
{
    if ((*p == end) || (*(*p)++ != '"')) return 0U;
    *start = *p;
    while (*p < end)
    {
        uint8_t c = *(*p)++;
        if (c == '"') { *size = (uint16_t)(*p - *start - 1); return 1U; }
        if (c < 32U) return 0U;
        if (c == '\\')
        {
            if (*p == end) return 0U;
            c = *(*p)++;
            if (c == 'u')
            {
                for (unsigned i = 0U; i < 4U; ++i)
                {
                    if (*p == end) return 0U;
                    c = *(*p)++;
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                          (c >= 'A' && c <= 'F'))) return 0U;
                }
            }
            else if ((c == 0U) || (strchr("\"\\/bfnrt", (int)c) == NULL)) return 0U;
        }
    }
    return 0U;
}

static uint8_t SkipValue(const uint8_t **p, const uint8_t *end, unsigned depth)
{
    const uint8_t *start;
    uint16_t size;
    if ((depth > 8U) || (*p == end)) return 0U;
    if (**p == '"') return String(p, end, &start, &size);
    if ((**p == '{') || (**p == '['))
    {
        uint8_t object = **p == '{';
        uint8_t close = object ? '}' : ']';
        ++*p;
        White(p, end);
        if ((*p < end) && (**p == close)) { ++*p; return 1U; }
        for (;;)
        {
            if (object)
            {
                if (!String(p, end, &start, &size)) return 0U;
                White(p, end);
                if ((*p == end) || (*(*p)++ != ':')) return 0U;
                White(p, end);
            }
            if (!SkipValue(p, end, depth + 1U)) return 0U;
            White(p, end);
            if (*p == end) return 0U;
            if (**p == close) { ++*p; return 1U; }
            if (*(*p)++ != ',') return 0U;
            White(p, end);
        }
    }
    start = *p;
    while ((*p < end) && (strchr(",]} \t\r\n", (int)**p) == NULL) && (**p != 0U)) ++*p;
    size = (uint16_t)(*p - start);
    if ((size == 4U && (memcmp(start, "true", 4U) == 0 || memcmp(start, "null", 4U) == 0)) ||
        (size == 5U && memcmp(start, "false", 5U) == 0)) return 1U;
    /* JSON number syntax for unknown fields. */
    const uint8_t *n = start;
    if ((n < *p) && (*n == '-')) ++n;
    if (n == *p) return 0U;
    if (*n == '0') ++n;
    else
    {
        if (*n < '1' || *n > '9') return 0U;
        while (n < *p && *n >= '0' && *n <= '9') ++n;
    }
    if (n < *p && *n == '.')
    {
        ++n;
        if (n == *p || *n < '0' || *n > '9') return 0U;
        while (n < *p && *n >= '0' && *n <= '9') ++n;
    }
    if (n < *p && (*n == 'e' || *n == 'E'))
    {
        ++n;
        if (n < *p && (*n == '+' || *n == '-')) ++n;
        if (n == *p || *n < '0' || *n > '9') return 0U;
        while (n < *p && *n >= '0' && *n <= '9') ++n;
    }
    return n == *p;
}

/* 完整验证后才入队，禁止无效 JSON 导致部分执行。 */
static uint8_t ParseParams(const uint8_t **p, const uint8_t *end, Control *request)
{
    const uint8_t *key, *value;
    uint16_t length;
    if (*p == end || *(*p)++ != '{') return 0U;
    White(p, end);
    if (*p < end && **p == '}') { ++*p; return 1U; }
    for (;;)
    {
        if (!String(p, end, &key, &length)) return 0U;
        uint8_t bit = length == 4U && memcmp(key, "lock", 4U) == 0 ? ONENET_SET_LOCK :
            length == 5U && memcmp(key, "relay", 5U) == 0 ? ONENET_SET_RELAY : 0U;
        White(p, end);
        if (*p == end || *(*p)++ != ':') return 0U;
        White(p, end);
        value = *p;
        if (!SkipValue(p, end, 0U)) return 0U;
        size_t size = (size_t)(*p - value);
        uint8_t state = 0U, valid = 1U;
        if ((size == 1U && *value == '1') || (size == 4U && memcmp(value, "true", 4U) == 0)) state = 1U;
        else if (!((size == 1U && *value == '0') || (size == 5U && memcmp(value, "false", 5U) == 0))) valid = 0U;
        if (!bit || !valid || (request->mask & bit)) request->code = 400U;
        request->mask |= bit;
        if (bit == ONENET_SET_LOCK) request->lock = state;
        if (bit == ONENET_SET_RELAY) request->relay = state;
        White(p, end);
        if (*p == end) return 0U;
        if (**p == '}') { ++*p; return 1U; }
        if (*(*p)++ != ',') return 0U;
        White(p, end);
    }
}

static void SetMessage(const uint8_t *payload, uint16_t size)
{
    const uint8_t *p = payload, *end = payload + size, *key, *value;
    uint16_t length;
    uint8_t found_id = 0U, found_params = 0U;
    Control request = { .code = 200U };
    White(&p, end);
    if (p == end || *p++ != '{') return;
    for (;;)
    {
        White(&p, end);
        if (!String(&p, end, &key, &length)) return;
        uint8_t is_id = length == 2U && memcmp(key, "id", 2U) == 0;
        uint8_t is_params = length == 6U && memcmp(key, "params", 6U) == 0;
        White(&p, end);
        if (p == end || *p++ != ':') return;
        White(&p, end);
        if (is_id)
        {
            if (found_id || !String(&p, end, &value, &length) ||
                length == 0U || length >= sizeof(request.id)) return;
            for (uint16_t i = 0U; i < length; ++i)
                if (value[i] < '0' || value[i] > '9') return;
            memcpy(request.id, value, length);
            found_id = 1U;
        }
        else if (is_params)
        {
            if (found_params) request.code = 400U;
            found_params = 1U;
            if (p < end && *p == '{')
            {
                if (!ParseParams(&p, end, &request)) return;
            }
            else
            {
                request.code = 400U;
                if (!SkipValue(&p, end, 0U)) return;
            }
        }
        else if (!SkipValue(&p, end, 0U)) return;
        White(&p, end);
        if (p == end) return;
        if (*p == '}') { ++p; break; }
        if (*p++ != ',') return;
    }
    White(&p, end);
    if (p != end || !found_id) return;
    if (!found_params || !request.mask) request.code = 400U;
    /* 队列满时丢弃，不执行未能保存应答的命令；云端将超时。 */
    if (control_count == CONTROL_QUEUE_SIZE) return;
    controls[(control_head + control_count) % CONTROL_QUEUE_SIZE] = request;
    ++control_count;
}

static void Message(const uint8_t *topic, uint16_t topic_size,
                    const uint8_t *payload, uint16_t payload_size)
{
    const uint8_t *p = payload, *end = payload + payload_size, *text;
    uint16_t length, code = 0U;
    uint8_t found_id = 0U, found_code = 0U;
    if (topic_size == sizeof(SET_TOPIC) - 1U && memcmp(topic, SET_TOPIC, topic_size) == 0)
    {
        SetMessage(payload, payload_size);
        return;
    }
    if (!pending || reply_received || topic_size != sizeof(REPLY_TOPIC) - 1U ||
        memcmp(topic, REPLY_TOPIC, topic_size) != 0) return;
    White(&p, end);
    if ((p == end) || (*p++ != '{')) return;
    for (;;)
    {
        White(&p, end);
        if (!String(&p, end, &text, &length)) return;
        uint8_t is_id = length == 2U && memcmp(text, "id", 2U) == 0;
        uint8_t is_code = length == 4U && memcmp(text, "code", 4U) == 0;
        White(&p, end);
        if ((p == end) || (*p++ != ':')) return;
        White(&p, end);
        if (is_id)
        {
            if (found_id || !String(&p, end, &text, &length) ||
                length != strlen(pending_id) || memcmp(text, pending_id, length) != 0) return;
            found_id = 1U;
        }
        else if (is_code)
        {
            if (found_code || p == end || *p < '1' || *p > '9') return;
            while (p < end && *p >= '0' && *p <= '9')
            {
                uint32_t value = (uint32_t)code * 10U + (uint32_t)(*p++ - '0');
                if (value > 65535U) return;
                code = (uint16_t)value;
            }
            found_code = 1U;
        }
        else if (!SkipValue(&p, end, 0U)) return;
        White(&p, end);
        if (p == end) return;
        if (*p == '}') { ++p; break; }
        if (*p++ != ',') return;
    }
    White(&p, end);
    if (p == end && found_id && found_code)
    {
        reply_code = code;
        reply_received = 1U;
    }
}

ESP12F_Status OneNET_Connect(void)
{
    static const ESP12F_MQTTConfig config = {"mqtts.heclouds.com", 1883U,
        ONENET_DEVICE_NAME, ONENET_PRODUCT_ID, ONENET_TOKEN, 60U};
    ESP12F_Status status;
    pending = reply_received = 0U;
    control_head = control_count = 0U;
    reply_code = 0U;
    stage = "ESP";
    status = ESP12F_Init();
    if (status != ESP12F_OK) return status;
    stage = "WIFI";
    status = ESP12F_JoinWiFi(ONENET_WIFI_SSID, ONENET_WIFI_PASSWORD);
    if (status != ESP12F_OK) return status;
    stage = "MQTT";
    status = ESP12F_MQTTConnect(&config, Message);
    if (status != ESP12F_OK) return status;
    stage = "SUBSCRIBE";
    status = ESP12F_MQTTSubscribe(REPLY_TOPIC);
    if (status != ESP12F_OK) return status;
    status = ESP12F_MQTTSubscribe(SET_TOPIC);
    if (status == ESP12F_OK) stage = "ONLINE";
    return status;
}

void OneNET_SetControlHandler(OneNET_ControlHandler handler) { control_handler = handler; }

ESP12F_Status OneNET_Poll(void)
{
    ESP12F_Status status = ESP12F_Poll();
    if (status != ESP12F_OK) return status;
    /* 限制每次处理数量；发送期间新收到的命令留到后续轮询。 */
    unsigned count = control_count;
    while (count--)
    {
        Control request = controls[control_head];
        char payload[96];
        control_head = (control_head + 1U) % CONTROL_QUEUE_SIZE;
        --control_count;
        if (request.code == 200U)
        {
            if (control_handler) control_handler(request.mask, request.lock, request.relay);
            else request.code = 500U;
        }
        int size = snprintf(payload, sizeof(payload), "{\"id\":\"%s\",\"code\":%u,\"msg\":\"%s\"}",
            request.id, (unsigned)request.code, request.code == 200U ? "success" : "invalid or unavailable");
        status = ESP12F_MQTTPublish(SET_REPLY_TOPIC, payload, (uint16_t)size);
        if (status != ESP12F_OK) return status;
    }
    return ESP12F_OK;
}

static ESP12F_Status PublishReport(const char *payload, uint16_t size)
{
    ESP12F_Status status;
    uint32_t start;
    pending = 1U;
    reply_received = 0U;
    reply_code = 0U;
    status = ESP12F_MQTTPublish(POST_TOPIC, payload, size);
    start = ESP12F_Port_Tick();
    while (status == ESP12F_OK && !reply_received)
    {
        status = OneNET_Poll();
        if (status != ESP12F_OK || reply_received) break;
        if ((uint32_t)(ESP12F_Port_Tick() - start) >= REPLY_TIMEOUT_MS)
            { status = ESP12F_ERROR_TIMEOUT; break; }
        ESP12F_Port_Idle();
    }
    pending = 0U;
    if (status != ESP12F_OK) return status;
    return reply_code == 200U ? ESP12F_OK : ESP12F_ERROR_CLOUD_REJECTED;
}

static void NextID(void)
{
    if (++message_id == 0U) ++message_id;
    (void)snprintf(pending_id, sizeof(pending_id), "%lu", (unsigned long)message_id);
}

ESP12F_Status OneNET_Report(float temperature_c, float humidity_rh, uint8_t lock, uint8_t relay)
{
    char payload[224];
    int32_t temperature;
    uint32_t magnitude, humidity;
    int size;
    reply_code = 0U;
    /* 这种比较同时拒绝 NaN/Inf，避免不合法 JSON 和越界浮点转整数。 */
    if (!(temperature_c >= -20.0f && temperature_c <= 100.0f &&
          humidity_rh >= 0.0f && humidity_rh <= 100.0f) || lock > 1U || relay > 1U) return ESP12F_ERROR_PARAM;
    if (!ESP12F_IsConnected()) return ESP12F_ERROR_DISCONNECTED;
    temperature = (int32_t)(temperature_c * 100.0f + (temperature_c < 0.0f ? -0.5f : 0.5f));
    magnitude = (uint32_t)(temperature < 0 ? -temperature : temperature);
    humidity = (uint32_t)(humidity_rh * 100.0f + 0.5f);
    NextID();
    size = snprintf(payload, sizeof(payload),
        "{\"id\":\"%s\",\"version\":\"1.0\",\"params\":{\"temp\":{\"value\":%s%lu.%02lu},"
        "\"humi\":{\"value\":%lu.%02lu},\"lock\":{\"value\":%s},\"relay\":{\"value\":%s}}}", pending_id, temperature < 0 ? "-" : "",
        (unsigned long)(magnitude / 100U), (unsigned long)(magnitude % 100U),
        (unsigned long)(humidity / 100U), (unsigned long)(humidity % 100U),
        lock ? "true" : "false", relay ? "true" : "false");
    if (size < 0 || (unsigned)size >= sizeof(payload)) return ESP12F_ERROR_PARAM;
    return PublishReport(payload, (uint16_t)size);
}

ESP12F_Status OneNET_ReportSwitches(uint8_t lock, uint8_t relay)
{
    char payload[144];
    reply_code = 0U;
    if (lock > 1U || relay > 1U) return ESP12F_ERROR_PARAM;
    if (!ESP12F_IsConnected()) return ESP12F_ERROR_DISCONNECTED;
    NextID();
    int size = snprintf(payload, sizeof(payload),
        "{\"id\":\"%s\",\"version\":\"1.0\",\"params\":{\"lock\":{\"value\":%s},\"relay\":{\"value\":%s}}}",
        pending_id, lock ? "true" : "false", relay ? "true" : "false");
    if (size < 0 || (unsigned)size >= sizeof(payload)) return ESP12F_ERROR_PARAM;
    return PublishReport(payload, (uint16_t)size);
}

uint16_t OneNET_LastReplyCode(void) { return reply_code; }
const char *OneNET_Stage(void) { return stage; }
