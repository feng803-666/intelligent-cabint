#include "duoji_app.h"
#include <stdint.h>


//调用Duoji_off,duoji_state为0，调用Duoji_On,duoji_state为1
volatile uint8_t duoji_state;