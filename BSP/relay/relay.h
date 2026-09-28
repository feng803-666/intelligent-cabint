#ifndef RELAY_H
#define RELAY_H

#include <main.h>
#include <gpio.h>
#include <stdint.h>

#ifndef RELAY_GPIO_Port
#define RELAY_GPIO_Port GPIOB
#endif

#ifndef RELAY_Pin
#define RELAY_Pin GPIO_PIN_12
#endif

uint8_t Relay_Off(void);
uint8_t Relay_On(void);

#endif