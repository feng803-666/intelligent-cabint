#include "encoder.h"
#include <assert.h>
#include <stdio.h>

uint32_t test_primask;
void Interrupt_EncoderInit(void) {}

static void samples(uint8_t ab, uint8_t key, unsigned count)
{
    while (count--) Encoder_Sample1ms(ab, key);
}

static void state(uint8_t ab)
{
    samples(ab, 0U, ENCODER_AB_STABLE_SAMPLES);
}

static void reset(void)
{
    Encoder_Init();
    state(3);
}

static void positive(void)
{
    state(1); state(0); state(2); state(3);
}

static void negative(void)
{
    state(2); state(0); state(1); state(3);
}

int main(void)
{
    /* 未初始化的 SysTick 不产生任何事件。 */
    positive();
    samples(3, 1, 100);
    assert(Encoder_GetDelta() == 0 && Encoder_GetPressCount() == 0);

    reset();
    positive();
    assert(Encoder_GetDelta() == ENCODER_DIRECTION);
    assert(Encoder_GetDelta() == 0);
    negative();
    assert(Encoder_GetDelta() == -ENCODER_DIRECTION);

    /* 模拟 OLED 忙碌期间积累多次滚动，读取应得到净位移。 */
    for (unsigned i = 0; i < 100; ++i) positive();
    for (unsigned i = 0; i < 23; ++i) negative();
    assert(Encoder_GetDelta() == 77 * ENCODER_DIRECTION);

    /* 每个边沿都回弹一次，仍只能产生一个完整步。 */
    const uint8_t cycle[] = {3, 1, 0, 2, 3};
    for (unsigned i = 1; i < sizeof(cycle); ++i)
    {
        state(cycle[i]); state(cycle[i - 1]); state(cycle[i]);
    }
    assert(Encoder_GetDelta() == ENCODER_DIRECTION);

    /* 单采样毛刺、走到一半回头，都不产生整步。 */
    samples(1, 0, 1); state(3);
    state(1); state(0); state(1); state(3);
    assert(Encoder_GetDelta() == 0);

    /* 非法双位跳变丢弃当前周期，之后正常整步能够恢复。 */
    state(1); state(2); state(3);
    assert(Encoder_GetDelta() == 0);
    positive();
    assert(Encoder_GetDelta() == ENCODER_DIRECTION);

    /* 半途上电先寻找同步点，不报告不完整的第一步。 */
    Encoder_Init();
    state(0); state(2); state(3);
    assert(Encoder_GetDelta() == 0);
    negative();
    assert(Encoder_GetDelta() == -ENCODER_DIRECTION);

    /* 按下抖动：未稳定 20 ms 不报告；稳定后只报告一次。 */
    reset();
    for (unsigned i = 0; i < 20; ++i)
    {
        samples(3, 1, 1); samples(3, 0, 1);
    }
    samples(3, 1, 19);
    assert(!Encoder_IsPressed() && Encoder_GetPressCount() == 0);
    samples(3, 1, 1);
    assert(Encoder_IsPressed() && Encoder_GetPressCount() == 1);
    samples(3, 1, 1000);
    assert(Encoder_GetPressCount() == 0);

    /* 按住时仍可滚动，两个状态机互不阻塞。 */
    samples(1, 1, 2); samples(0, 1, 2);
    samples(2, 1, 2); samples(3, 1, 2);
    assert(Encoder_GetDelta() == ENCODER_DIRECTION && Encoder_IsPressed());

    samples(3, 0, 19); samples(3, 1, 1); samples(3, 0, 19);
    assert(Encoder_IsPressed() && Encoder_GetReleaseCount() == 0);
    samples(3, 0, 1);
    assert(!Encoder_IsPressed() && Encoder_GetReleaseCount() == 1);
    assert(Encoder_GetReleaseCount() == 0);
    for (unsigned i = 0; i < 3; ++i)
    {
        samples(3, 1, 20); samples(3, 0, 20);
    }
    assert(Encoder_GetPressCount() == 3 && Encoder_GetReleaseCount() == 3);

    /* 已有的中断屏蔽状态在 API 返回后保持不变。 */
    test_primask = 1;
    Encoder_Init();
    assert(test_primask == 1);
    (void)Encoder_GetDelta();
    (void)Encoder_GetPressCount();
    (void)Encoder_GetReleaseCount();
    assert(test_primask == 1);
    test_primask = 0;
    (void)Encoder_GetDelta();
    assert(test_primask == 0);
    samples(3, 1, 20);
    assert(Encoder_GetPressCount() == 1);
    puts("Encoder tests passed");
    return 0;
}
