#ifndef BSP_LED_LED_H
#define BSP_LED_LED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* LED1 呼吸周期默认值，单位：毫秒。可按需要修改。 */
#define LED1_BREATH_DEFAULT_PERIOD_MS 5000U

/* LED2 GPIO 配置：低电平点亮。可根据实际硬件连接修改。 */
#define LED2_GPIO_Port GPIOA
#define LED2_Pin       GPIO_PIN_6

/**
 * @brief 初始化 LED 驱动并启动 LED1 的 TIM3_CH3 PWM 输出。
 * @return HAL_OK：初始化成功；HAL_ERROR：PWM 启动失败。
 * @note 调用本函数前必须先由 CubeMX 生成的 MX_TIM3_Init() 初始化 TIM3。
 */
HAL_StatusTypeDef LED_Init(void);

/**
 * @brief 设置 LED1 的固定亮度，并停止呼吸效果。
 * @param percent 亮度百分比，范围 0~100；超过 100 时按 100 处理。
 */
void LED1_SetBrightness(uint8_t percent);

/** @brief LED1 常亮，等效于将亮度设置为 100%。 */
void LED1_On(void);

/** @brief LED1 常灭，等效于将亮度设置为 0%。 */
void LED1_Off(void);

/**
 * @brief 启动 LED1 呼吸效果。
 * @param period_ms 一次“渐亮+渐灭”的完整周期，单位：毫秒；最小为 200 ms。
 */
void LED1_BreathStart(uint32_t period_ms);

/** @brief 停止 LED1 呼吸效果，并将 LED1 熄灭。 */
void LED1_BreathStop(void);

/**
 * @brief 更新 LED1 呼吸亮度。
 * @note 使用非阻塞方式实现，需要在主循环中持续调用。
 */
void LED1_Task(void);

/**
 * @brief LED2 常亮，PA6 输出低电平。
 * @note PA6 同时是 SPI1_MISO，调用后 PA6 将被切换为普通 GPIO 输出。
 */
void LED2_On(void);

/**
 * @brief LED2 常灭，PA6 输出高电平。
 * @note PA6 同时是 SPI1_MISO，调用后 PA6 将被切换为普通 GPIO 输出。
 */
void LED2_Off(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_LED_H */
