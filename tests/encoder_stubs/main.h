#ifndef ENCODER_TEST_MAIN_H
#define ENCODER_TEST_MAIN_H
#include <stdint.h>
/* 主机测试替身：模拟 Cortex-M 中断屏蔽寄存器。 */
extern uint32_t test_primask;
static inline uint32_t __get_PRIMASK(void) { return test_primask; }
static inline void __disable_irq(void) { test_primask = 1U; }
static inline void __set_PRIMASK(uint32_t value) { test_primask = value; }
#endif
