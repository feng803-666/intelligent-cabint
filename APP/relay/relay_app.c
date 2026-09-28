#include "relay_app.h"


//调用Relay_Off,Relay_state为0，调用Relay_On,Relay_state为1
volatile uint8_t relay_state;