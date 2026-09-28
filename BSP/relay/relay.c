#include "relay.h"
#include "main.h"
#include "stm32f1xx_hal_gpio.h"
#include <stdint.h>

uint8_t Relay_Off(void)
{
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, 0);

    return 0;
}

uint8_t Relay_On(void)
{
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, 1);

    return 1;
}