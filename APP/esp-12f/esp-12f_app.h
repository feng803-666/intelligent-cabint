#ifndef ESP12F_APP_H
#define ESP12F_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* 应用层：SHT30 采样、OneNET 上报、重连、OLED 显示及串口日志。
 * 调用前完成 HAL 时基、GPIO、I2C1、USART1/2、OLED 和 LED 初始化。
 * 在主循环启动前调用一次；初始化传感器和应用状态，首次 Task 发起联网。
 */
void ESP12F_App_Init(void);

/* 主循环频繁调用：在线时每 5 秒采样上报，网络失败后等待 10 秒重连。
 * 网络操作沿用 BSP 的有界阻塞接口；等待期间维护呼吸灯。
 * 仅在主循环串行调用，不可从 ISR 或 ESP12F 接收/等待回调中重入。
 */
void ESP12F_App_Task(void);

#ifdef __cplusplus
}
#endif

#endif
