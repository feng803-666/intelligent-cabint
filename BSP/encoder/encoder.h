#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/* 相位连续两次采样相同才接受；按键连续稳定 20 次才确认。 */
#define ENCODER_AB_STABLE_SAMPLES 2U
#define ENCODER_BUTTON_STABLE_SAMPLES 20U
/* 正方向：AB=11->01->00->10->11；若实际方向相反，改为 -1。 */
#define ENCODER_DIRECTION 1

/* GPIO 初始化后调用，清空事件；上电按住时，稳定 20 ms 后报一次按下。 */
void Encoder_Init(void);
/* 取出并清空累计净位移；完整相位周期计 +/-1，可累计多个周期。 */
int32_t Encoder_GetDelta(void);
/* 取出并清空按下/松开次数；长按不会重复产生按下事件。 */
uint32_t Encoder_GetPressCount(void);
uint32_t Encoder_GetReleaseCount(void);
/* 返回消抖后的按住状态：1=按住，0=松开。 */
uint8_t Encoder_IsPressed(void);
/* 中断层内部接口：每 1 ms 调用一次，ab 的 bit1=A、bit0=B。 */
void Encoder_Sample1ms(uint8_t ab, uint8_t pressed);

#endif
