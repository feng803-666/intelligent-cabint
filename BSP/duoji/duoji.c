#include "duoji.h"
#include <stdint.h>

uint8_t Duoji_Off(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1500);
    
    return 0;  
}

uint8_t Duoji_On(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1000);
    
    return 1;
}