#include "interrupt.h"
#include "encoder.h"
#include "main.h"

void Interrupt_EncoderInit(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* 使用周期采样，关闭旧边沿中断，避免触点抖动反复抢占 CPU。 */
    HAL_NVIC_DisableIRQ(EXTI0_IRQn);
    HAL_NVIC_DisableIRQ(EXTI1_IRQn);
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &gpio);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0 | GPIO_PIN_1);
}

void Interrupt_Tick1ms(void)
{
    /* 同时读取 A/B；bit1=A(PA0)，bit0=B(PA1)，S(PB1)低电平为按下。
     * 中断中只采样和更新状态，禁止延时、打印和刷新 OLED。
     */
    uint32_t pins = GPIOA->IDR;
    uint8_t ab = (uint8_t)(((pins & GPIO_PIN_0) << 1U) |
                           ((pins & GPIO_PIN_1) >> 1U));
    uint8_t pressed = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_RESET);
    Encoder_Sample1ms(ab, pressed);
}
