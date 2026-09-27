#include "relay.h"
#include "main.h"
#include "stm32f1xx_hal_gpio.h"

void Relay_Off(void)
{
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, 0);
}

void Relay_On(void)
{
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, 1);
}