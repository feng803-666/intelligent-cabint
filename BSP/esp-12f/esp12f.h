#ifndef ESP12F_H
#define ESP12F_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    ESP12F_OK = 0,
    ESP12F_ERROR_PARAM,
    ESP12F_ERROR_NOT_INIT,
    ESP12F_ERROR_UART,
    ESP12F_ERROR_TIMEOUT,
    ESP12F_ERROR_AT,
    ESP12F_ERROR_DISCONNECTED,
    ESP12F_ERROR_OVERFLOW,
    ESP12F_ERROR_PROTOCOL,
    ESP12F_ERROR_MQTT_REJECTED,
    ESP12F_ERROR_CLOUD_REJECTED
} ESP12F_Status;

typedef struct
{
    const char *host;
    uint16_t port;
    const char *client_id;
    const char *username;
    const char *password;
    uint16_t keep_alive_seconds; /* 20..600 */
} ESP12F_MQTTConfig;

/* topic/payload 不以 NUL 结尾，只在回调期间有效。回调不能调用驱动 API。 */
typedef void (*ESP12F_MessageHandler)(const uint8_t *topic, uint16_t topic_size,
                                    const uint8_t *payload, uint16_t payload_size);

/* 单设备、无堆分配；除 IRQ 外的 API 只能在主循环中串行调用。
 * ESP8266 必须运行 AT 固件，115200/8N1，普通 TCP、单连接、主动接收。
 * Init/Join/Connect/Subscribe/Publish 是有界阻塞操作，等待期间调用 Port_Idle。
 * 故障后调用 Init 重新开始；Init 会软件复位 ESP8266。
 */
ESP12F_Status ESP12F_Init(void);
ESP12F_Status ESP12F_JoinWiFi(const char *ssid, const char *password);
ESP12F_Status ESP12F_MQTTConnect(const ESP12F_MQTTConfig *config,
                               ESP12F_MessageHandler handler);
/* MQTT 3.1.1，clean session；订阅和发布仅支持 QoS 0，无 retain。
 * Publish OK 仅表示 TCP 发送完成，平台确认由 OneNET_Report 完成。
 * 单个 MQTT 报文最多 768 字节（包含协议头）。
 */
ESP12F_Status ESP12F_MQTTSubscribe(const char *topic);
ESP12F_Status ESP12F_MQTTPublish(const char *topic, const void *payload, uint16_t size);
/* 主循环频繁调用（建议间隔 <20 ms）；处理接收、心跳和掉线。 */
ESP12F_Status ESP12F_Poll(void);
uint8_t ESP12F_IsConnected(void);
uint8_t ESP12F_LastConnAck(void); /* MQTT CONNACK 拒绝码，0 表示无拒绝 */
/* +CWJAP 失败码：0=无；1=超时；2=密码错误；3=找不到 AP；4=连接失败。 */
uint8_t ESP12F_LastWiFiError(void);
const char *ESP12F_StatusString(ESP12F_Status status);

#ifdef __cplusplus
}
#endif
#endif
