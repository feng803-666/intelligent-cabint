#include "duoji.h"

void Duoji_Off(void)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1500);  
}

void Duoji_On(void)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1000);  
}