/* Host simulation: real ESP12F + OneNET code, a deterministic UART/AT broker. */
#include "esp12f.h"
#include "esp12f_port.h"
#include "onenet.h"
#include "onenet_credentials.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t tick;
static uint8_t queue[16384];
static size_t head, tail;
static unsigned read_budget = 3U, wire_length, transmissions, pings, publications;
static uint8_t uart_fault, connack_code, suback_bad_id, suback_reject, no_ping, no_send_ok;
static unsigned reply_mode;
static uint8_t wifi_reject;
static const char *at_error, *at_silent;
static char last_payload[800];

#define TEST_TOPIC "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post"
enum { REPLY_OK, REPLY_REJECT, REPLY_WRONG_ID, REPLY_MISSING, REPLY_NESTED_ONLY,
       REPLY_DUPLICATE, REPLY_REORDERED, REPLY_WRONG_TOPIC, REPLY_TRAILING,
       REPLY_ESCAPED, REPLY_EMBEDDED_NUL };

static void Push(const void *data, size_t size)
{
    if (head == tail) head = tail = 0U;
    assert(head + size <= sizeof(queue));
    memcpy(queue + head, data, size);
    head += size;
}
static void Text(const char *text) { Push(text, strlen(text)); }

static void IPD(const uint8_t *data, size_t size)
{
    char header[32];
    (void)snprintf(header, sizeof(header), "\r\n+IPD,%u:", (unsigned)size);
    Text(header);
    Push(data, size);
}

static void Incoming(uint8_t fixed, const uint8_t *data, size_t size)
{
    uint8_t packet[1024];
    size_t n = size, pos = 1U;
    packet[0] = fixed;
    do
    {
        uint8_t digit = (uint8_t)(n % 128U);
        n /= 128U;
        if (n) digit |= 0x80U;
        packet[pos++] = digit;
    } while (n);
    memcpy(packet + pos, data, size);
    /* MQTT fixed header / remaining length / body cross separate IPD frames. */
    IPD(packet, 1U);
    IPD(packet + 1U, 1U);
    IPD(packet + 2U, size + pos - 2U);
}

static void CloudReply(const char *topic, const char *json)
{
    uint8_t body[900];
    size_t len = strlen(topic), json_size = strlen(json);
    body[0] = (uint8_t)(len >> 8);
    body[1] = (uint8_t)len;
    memcpy(body + 2U, topic, len);
    memcpy(body + 2U + len, json, json_size);
    Incoming(0x30U, body, 2U + len + json_size);
}

static void ReplyToPublish(const char *id)
{
    char json[500];
    if (reply_mode == REPLY_MISSING) return;
    if (reply_mode == REPLY_NESTED_ONLY)
        (void)snprintf(json, sizeof(json), "{\"id\":\"%s\",\"data\":{\"code\":200},\"msg\":\"code:200\"}", id);
    else if (reply_mode == REPLY_DUPLICATE)
        (void)snprintf(json, sizeof(json), "{\"id\":\"%s\",\"code\":200,\"code\":400}", id);
    else if (reply_mode == REPLY_REORDERED)
        (void)snprintf(json, sizeof(json), " { \"data\": [true,false,null,-1.2e+3,{\"code\":500}], \"code\":200, \"id\":\"%s\" } \n", id);
    else if (reply_mode == REPLY_TRAILING)
        (void)snprintf(json, sizeof(json), "{\"id\":\"%s\",\"code\":200}garbage", id);
    else if (reply_mode == REPLY_ESCAPED)
        (void)snprintf(json, sizeof(json), "{\"msg\":\"\\\"code\\\":500 CLOSED\\r\\n +IPD,4: \\u4f60\",\"code\":200,\"id\":\"%s\"}", id);
    else
        (void)snprintf(json, sizeof(json), "{\"id\":\"%s\",\"code\":%u,\"msg\":\"ok\"}",
                       reply_mode == REPLY_WRONG_ID ? "999999" : id,
                       reply_mode == REPLY_REJECT ? 400U : 200U);
    if (reply_mode == REPLY_EMBEDDED_NUL)
    {
        uint8_t body[500];
        const char *topic = TEST_TOPIC "/reply";
        size_t t = strlen(topic), j = strlen(json);
        body[0] = 0U; body[1] = (uint8_t)t;
        memcpy(body + 2U, topic, t);
        memcpy(body + 2U + t, json, j);
        body[2U + t + j] = 0U;
        Incoming(0x30U, body, 3U + t + j);
    }
    else CloudReply(reply_mode == REPLY_WRONG_TOPIC ? "wrong/topic" : TEST_TOPIC "/reply", json);
}

static const uint8_t *ReadString(const uint8_t *p, const uint8_t *end, const char *expected)
{
    assert((size_t)(end - p) >= 2U);
    size_t size = ((unsigned)p[0] << 8) | p[1];
    p += 2U;
    assert((size_t)(end - p) >= size);
    assert(size == strlen(expected) && memcmp(p, expected, size) == 0);
    return p + size;
}

ESP12F_Status ESP12F_Port_Init(void)
{
    head = tail = 0U;
    uart_fault = 1U; /* ESP8266 ROM startup at 74880 baud: expected framing error. */
    wire_length = 0U;
    return ESP12F_OK;
}

void ESP12F_Port_ClearRx(void)
{
    head = tail = 0U;
    uart_fault = 0U;
}

int ESP12F_Port_ReadByte(uint8_t *byte)
{
    if (uart_fault) return -1;
    if (head == tail || read_budget == 0U) return 0;
    --read_budget;
    *byte = queue[tail++];
    return 1;
}
uint32_t ESP12F_Port_Tick(void) { return tick; }
void ESP12F_Port_Idle(void) { ++tick; read_budget = 3U; }
void ESP12F_Port_IRQHandler(void) { }

ESP12F_Status ESP12F_Port_Transmit(const uint8_t *data, uint16_t size)
{
    ++transmissions;
    if (wire_length)
    {
        assert(size == wire_length);
        wire_length = 0U;
        const uint8_t *p = data + 1U, *end = data + size;
        size_t remaining = 0U, multiplier = 1U;
        uint8_t digit;
        do
        {
            assert(p < end);
            digit = *p++;
            remaining += (digit & 127U) * multiplier;
            multiplier *= 128U;
        } while (digit & 128U);
        assert(remaining == (size_t)(end - p));
        switch (data[0])
        {
            case 0x10U:
            {
                static const uint8_t expected[] = {0,4,'M','Q','T','T',4,0xC2,0,60};
                assert(remaining >= sizeof(expected));
                assert(memcmp(p, expected, sizeof(expected)) == 0);
                p += sizeof(expected);
                p = ReadString(p, end, ONENET_DEVICE_NAME);
                p = ReadString(p, end, ONENET_PRODUCT_ID);
                p = ReadString(p, end, ONENET_TOKEN);
                assert(p == end);
                uint8_t ack[] = {0, connack_code};
                Incoming(0x20U, ack, sizeof(ack));
                break;
            }
            case 0x82U:
            {
                assert(remaining >= 3U);
                uint8_t ack[] = {p[0], p[1], suback_reject ? 0x80U : 0U};
                if (suback_bad_id) ++ack[1];
                p = ReadString(p + 2U, end, TEST_TOPIC "/reply");
                assert(end - p == 1 && *p == 0U);
                Incoming(0x90U, ack, sizeof(ack));
                break;
            }
            case 0x30U:
            {
                ++publications;
                assert(remaining >= 2U);
                size_t topic_size = ((unsigned)p[0] << 8) | p[1];
                assert(topic_size + 2U <= remaining);
                uint8_t onenet_topic = topic_size == strlen(TEST_TOPIC) &&
                    memcmp(p + 2U, TEST_TOPIC, topic_size) == 0;
                p += 2U + topic_size;
                size_t payload_size = (size_t)(end - p);
                assert(payload_size < sizeof(last_payload));
                memcpy(last_payload, p, payload_size);
                last_payload[payload_size] = '\0';
                if (onenet_topic)
                {
                    char id[11];
                    assert(sscanf(last_payload, "{\"id\":\"%10[0-9]", id) == 1);
                    ReplyToPublish(id);
                }
                break;
            }
            case 0xC0U:
            {
                assert(remaining == 0U);
                ++pings;
                if (!no_ping) { const uint8_t pong[] = {0xD0,0}; IPD(pong, sizeof(pong)); }
                break;
            }
            default: assert(!"Unexpected outgoing MQTT packet");
        }
        /* Broker reply can arrive BEFORE SEND OK. Never discard it. */
        if (!no_send_ok) Text("\r\nSEND OK\r\n");
        return ESP12F_OK;
    }
    char command[300];
    assert(size < sizeof(command));
    memcpy(command, data, size);
    command[size] = '\0';
    if (at_error && strcmp(command, at_error) == 0) { Text("\r\nERROR\r\n"); return ESP12F_OK; }
    if (at_silent && strcmp(command, at_silent) == 0) return ESP12F_OK;
    if (strcmp(command, "AT+RST\r\n") == 0)
    {
        uart_fault = 1U; /* Boot-ROM garbage must not poison the AT session. */
        Text("\r\nOK\r\nready\r\n");
    }
    else if (strncmp(command, "AT+CWJAP=", 9U) == 0)
    {
        assert(strcmp(command, "AT+CWJAP=\"test\\,ssid\",\"quote\\\"back\\\\slash\"\r\n") == 0);
        if (wifi_reject) Text("\r\n+CWJAP:3\r\nFAIL\r\n");
        else Text("\r\nWIFI CONNECTED\r\nWIFI GOT IP\r\nOK\r\n");
    }
    else if (strncmp(command, "AT+CIPSTART=", 12U) == 0)
    {
        assert(strcmp(command, "AT+CIPSTART=\"TCP\",\"mqtts.heclouds.com\",1883\r\n") == 0);
        Text("\r\nCONNECT\r\nOK\r\n");
    }
    else if (sscanf(command, "AT+CIPSEND=%u", &wire_length) == 1) Text("\r\nOK\r\n> ");
    else Text("\r\nOK\r\n");
    return ESP12F_OK;
}

static ESP12F_Status Pump(unsigned milliseconds)
{
    ESP12F_Status status = ESP12F_OK;
    for (unsigned i = 0U; i < milliseconds; ++i)
    {
        status = OneNET_Poll();
        if (status != ESP12F_OK) break;
        ESP12F_Port_Idle();
    }
    return status;
}

int main(void)
{
    assert(ESP12F_Poll() == ESP12F_ERROR_NOT_INIT);
    assert(OneNET_Report(25.0f, 50.0f) == ESP12F_ERROR_DISCONNECTED);
    assert(OneNET_Connect() == ESP12F_OK);
    assert(strcmp(OneNET_Stage(), "ONLINE") == 0);
    assert(OneNET_Report(25.125f, 56.375f) == ESP12F_OK);
    assert(strstr(last_payload, "\"temp\":{\"value\":25.13}") != NULL);
    assert(strstr(last_payload, "\"humi\":{\"value\":56.38}") != NULL);
    assert(OneNET_LastReplyCode() == 200U);
    assert(OneNET_Report(-0.01f, 0.0f) == ESP12F_OK);
    assert(strstr(last_payload, "\"value\":-0.01") != NULL);
    assert(OneNET_Report(-20.0f, 100.0f) == ESP12F_OK);
    unsigned before = transmissions;
    assert(OneNET_Report(NAN, 10.0f) == ESP12F_ERROR_PARAM);
    assert(OneNET_Report(10.0f, INFINITY) == ESP12F_ERROR_PARAM);
    assert(OneNET_Report(-20.01f, 10.0f) == ESP12F_ERROR_PARAM);
    assert(OneNET_Report(100.01f, 10.0f) == ESP12F_ERROR_PARAM);
    assert(OneNET_Report(10.0f, -0.01f) == ESP12F_ERROR_PARAM);
    assert(OneNET_Report(10.0f, 100.01f) == ESP12F_ERROR_PARAM);
    assert(ESP12F_MQTTPublish("test/#", "x", 1) == ESP12F_ERROR_PARAM);
    assert(ESP12F_MQTTPublish("test", NULL, 1) == ESP12F_ERROR_PARAM);
    char oversized[800]; memset(oversized, 'x', sizeof(oversized)); oversized[799] = 0;
    assert(ESP12F_MQTTPublish(oversized, "x", 1) == ESP12F_ERROR_PARAM);
    assert(transmissions == before);
    assert(OneNET_LastReplyCode() == 0U);

    reply_mode = REPLY_REJECT;
    assert(OneNET_Report(1,2) == ESP12F_ERROR_CLOUD_REJECTED && OneNET_LastReplyCode() == 400U);
    const unsigned ignored[] = {REPLY_WRONG_ID, REPLY_MISSING, REPLY_NESTED_ONLY,
        REPLY_DUPLICATE, REPLY_WRONG_TOPIC, REPLY_TRAILING, REPLY_EMBEDDED_NUL};
    for (unsigned i = 0U; i < sizeof(ignored)/sizeof(ignored[0]); ++i)
    {
        reply_mode = ignored[i];
        assert(OneNET_Report(1,2) == ESP12F_ERROR_TIMEOUT && OneNET_LastReplyCode() == 0U);
        assert(ESP12F_IsConnected());
    }
    reply_mode = REPLY_REORDERED;
    assert(OneNET_Report(1,2) == ESP12F_OK);
    /* A stale response from an earlier message cannot acknowledge the next one. */
    CloudReply(TEST_TOPIC "/reply", "{\"id\":\"1\",\"code\":400}");
    assert(OneNET_Report(1,2) == ESP12F_OK);
    reply_mode = REPLY_ESCAPED;
    assert(OneNET_Report(1,2) == ESP12F_OK && ESP12F_IsConnected());
    reply_mode = REPLY_OK;

    unsigned ping_before = pings;
    tick += 30001U;
    assert(Pump(100) == ESP12F_OK && pings == ping_before + 1U);
    no_ping = 1U;
    tick += 30001U;
    assert(Pump(100) == ESP12F_OK);
    tick += 10001U;
    assert(OneNET_Poll() == ESP12F_ERROR_TIMEOUT && !ESP12F_IsConnected());
    no_ping = 0U;
    assert(OneNET_Connect() == ESP12F_OK);
    Text("\r\nWIFI DISCONNECT\r\n");
    assert(Pump(100) == ESP12F_ERROR_DISCONNECTED);
    assert(OneNET_Connect() == ESP12F_OK);
    Text("\r\nCLOSED\r\n");
    assert(Pump(100) == ESP12F_ERROR_DISCONNECTED);
    assert(OneNET_Connect() == ESP12F_OK);
    Text("\r\nready\r\n");
    assert(Pump(100) == ESP12F_ERROR_DISCONNECTED);
    assert(OneNET_Connect() == ESP12F_OK);
    uart_fault = 1U;
    assert(OneNET_Poll() == ESP12F_ERROR_UART && !ESP12F_IsConnected());

    connack_code = 4U;
    assert(OneNET_Connect() == ESP12F_ERROR_MQTT_REJECTED && ESP12F_LastConnAck() == 4U);
    connack_code = 0U;
    suback_bad_id = 1U;
    assert(OneNET_Connect() == ESP12F_ERROR_TIMEOUT);
    suback_bad_id = 0U;
    suback_reject = 1U;
    assert(OneNET_Connect() == ESP12F_ERROR_MQTT_REJECTED);
    suback_reject = 0U;
    at_error = "AT+CWJAP=\"test\\,ssid\",\"quote\\\"back\\\\slash\"\r\n";
    assert(OneNET_Connect() == ESP12F_ERROR_AT && strcmp(OneNET_Stage(), "WIFI") == 0);
    at_error = NULL;
    wifi_reject = 1U;
    assert(OneNET_Connect() == ESP12F_ERROR_AT && ESP12F_LastWiFiError() == 3U);
    wifi_reject = 0U;
    at_silent = "AT\r\n";
    tick = UINT32_MAX - 1600U;
    assert(OneNET_Connect() == ESP12F_ERROR_TIMEOUT);
    at_silent = NULL;
    tick = UINT32_MAX - 1700U;
    assert(OneNET_Connect() == ESP12F_OK);
    assert(OneNET_Report(1,2) == ESP12F_OK);

    /* SEND OK missing must not be mistaken for confirmed transport success. */
    no_send_ok = 1U;
    assert(OneNET_Report(1,2) == ESP12F_ERROR_TIMEOUT);
    no_send_ok = 0U;
    assert(OneNET_Connect() == ESP12F_OK);
    const uint8_t too_large[] = {0x30, 0xFF, 0x7F};
    IPD(too_large, sizeof(too_large));
    assert(Pump(100) == ESP12F_ERROR_OVERFLOW);
    assert(OneNET_Connect() == ESP12F_OK);
    const uint8_t malformed[] = {0x30, 0x80, 0x80, 0x80, 0x80};
    IPD(malformed, sizeof(malformed));
    assert(Pump(100) == ESP12F_ERROR_PROTOCOL);
    assert(OneNET_Connect() == ESP12F_OK);
    /* Coalesced MQTT packets in one IPD, plus binary NUL bytes. */
    const uint8_t combined[] = {0xD0,0,0x30,5,0,1,'x',0,'z',0xD0,0};
    IPD(combined, sizeof(combined));
    assert(Pump(100) == ESP12F_OK);
    assert(OneNET_Report(1,2) == ESP12F_OK);
    printf("ESP12F/OneNET tests passed (%u publications, %u heartbeats).\n", publications, pings);
    return 0;
}
