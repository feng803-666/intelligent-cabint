#ifndef INTERRUPT_H
#define INTERRUPT_H

/* 编码器引脚初始化，由 Encoder_Init 调用。 */
void Interrupt_EncoderInit(void);
/* 仅从 1 ms SysTick 中断调用，不要再从主循环调用。 */
void Interrupt_Tick1ms(void);

#endif
