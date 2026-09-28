#ifndef ONENET_H
#define ONENET_H

#include "esp12f.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 使用构建时从 wifi.txt / onenet.txt 生成的配置。
 * Connect 完成模块初始化、Wi-Fi、MQTT 登录及上报应答/属性下发订阅；失败可再次调用。
 * 单连接、主循环串行调用；占用 ESP12F 的消息回调。
 */
ESP12F_Status OneNET_Connect(void);
ESP12F_Status OneNET_Poll(void);
#define ONENET_SET_LOCK  1U
#define ONENET_SET_RELAY 2U
/* mask 指定本次要修改的属性；回调中禁止调用网络 API。 */
typedef void (*OneNET_ControlHandler)(uint8_t mask, uint8_t lock, uint8_t relay);
void OneNET_SetControlHandler(OneNET_ControlHandler handler);
/* 只接受当前物模型范围 temp=-20..100 C、humi=0..100 %RH。
 * 保留两位小数，等待匹配消息 ID 的平台响应，最长约 23 秒。
 * OK 表示平台 code=200；超时无法判断平台是否已接收。
 */
ESP12F_Status OneNET_Report(float temperature_c, float humidity_rh,
                           uint8_t lock, uint8_t relay);
/* 本地开关值为 0/1，上报为物模型 bool（JSON true/false）。
 * 无有效温湿度时也可以单独上报开关。
 */
ESP12F_Status OneNET_ReportSwitches(uint8_t lock, uint8_t relay);
uint16_t OneNET_LastReplyCode(void); /* 0=尚未收到对应的有效应答 */
const char *OneNET_Stage(void);      /* ESP / WIFI / MQTT / SUBSCRIBE / ONLINE */

#ifdef __cplusplus
}
#endif
#endif
