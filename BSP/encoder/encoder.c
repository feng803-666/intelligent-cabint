#include "encoder.h"
#include "interrupt.h"
#include "main.h"
#include <limits.h>

static volatile uint8_t initialized;
static volatile int32_t pending_delta;
static volatile uint32_t pending_press, pending_release;
static volatile uint8_t button_pressed;
static uint8_t ab_candidate, ab_samples, ab_previous, synchronized;
static uint8_t button_samples;
static int8_t quarter_steps;

void Encoder_Init(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    initialized = 0U;
    Interrupt_EncoderInit();
    pending_delta = 0;
    pending_press = pending_release = 0U;
    button_pressed = button_samples = 0U;
    ab_candidate = ab_previous = 3U;
    ab_samples = synchronized = 0U;
    quarter_steps = 0;
    initialized = 1U;
    __set_PRIMASK(primask);
}

static void Encoder_Decode(uint8_t ab)
{
    /* 相邻合法状态记 +/-1；触点来回抖动产生的正反跳变相互抵消。 */
    static const int8_t transition[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };
    /* 从双路断开状态开始同步，避免上电停在半途误计数。 */
    if (!synchronized)
    {
        ab_previous = ab;
        quarter_steps = 0;
        synchronized = (ab == 3U);
        return;
    }
    if (ab == ab_previous) return;
    /* 两路同时变化说明漏采样/干扰；丢弃该周期，回到 11 后重新同步。 */
    if ((ab ^ ab_previous) == 3U)
    {
        quarter_steps = 0;
        synchronized = (ab == 3U);
        ab_previous = ab;
        return;
    }
    quarter_steps += transition[(ab_previous << 2U) | ab];
    ab_previous = ab;
    if (ab == 3U)
    {
        int32_t step = 0;
        if (quarter_steps == 4) step = ENCODER_DIRECTION;
        if (quarter_steps == -4) step = -ENCODER_DIRECTION;
        /* 完整走过四个相位才发布事件，不发布半步和回弹。 */
        if ((step > 0 && pending_delta < INT32_MAX) ||
            (step < 0 && pending_delta > INT32_MIN))
            pending_delta += step;
        quarter_steps = 0;
    }
}

void Encoder_Sample1ms(uint8_t ab, uint8_t pressed)
{
    if (!initialized) return;
    ab &= 3U;
    pressed = (pressed != 0U);
    if (ab != ab_candidate)
    {
        ab_candidate = ab;
        ab_samples = 1U;
    }
    else if (ab_samples < ENCODER_AB_STABLE_SAMPLES)
        ++ab_samples;
    if (ab_samples >= ENCODER_AB_STABLE_SAMPLES)
        Encoder_Decode(ab);

    /* 按下和松开都要求连续稳定，抖动期间重新计时，长按不连发。 */
    if (pressed == button_pressed)
        button_samples = 0U;
    else if (++button_samples >= ENCODER_BUTTON_STABLE_SAMPLES)
    {
        button_samples = 0U;
        button_pressed = pressed;
        if (pressed)
        {
            if (pending_press < UINT32_MAX) ++pending_press;
        }
        else if (pending_release < UINT32_MAX) ++pending_release;
    }
}

int32_t Encoder_GetDelta(void)
{
    /* 读取和清零作为整体执行，避免新中断事件被清零覆盖。 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    int32_t value = pending_delta;
    pending_delta = 0;
    __set_PRIMASK(primask);
    return value;
}

uint32_t Encoder_GetPressCount(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t value = pending_press;
    pending_press = 0U;
    __set_PRIMASK(primask);
    return value;
}

uint32_t Encoder_GetReleaseCount(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t value = pending_release;
    pending_release = 0U;
    __set_PRIMASK(primask);
    return value;
}

uint8_t Encoder_IsPressed(void)
{
    return button_pressed;
}
