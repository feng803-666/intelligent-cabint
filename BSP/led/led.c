#include "led.h"
#include "tim.h"

/* LED1 使用的定时器 PWM 通道，必须与 CubeMX 配置保持一致。 */
#define LED1_PWM_CHANNEL   TIM_CHANNEL_3
/* 呼吸亮度计算的内部最大值，数值越大，亮度变化分辨率越高。 */
#define LED1_LEVEL_MAX     1000U
/* 允许设置的最短呼吸周期，单位：毫秒。 */
#define LED1_BREATH_MIN_MS 200U

static uint32_t s_led1_breath_start_tick;
static uint32_t s_led1_breath_period_ms = LED1_BREATH_DEFAULT_PERIOD_MS;
static uint8_t s_led1_initialized;
static uint8_t s_led1_breathing;

static void LED1_ApplyLevel(uint16_t level)
{
  uint32_t pwm_counts;
  uint32_t corrected_level;
  uint32_t compare;

  if (level > LED1_LEVEL_MAX)
  {
    level = LED1_LEVEL_MAX;
  }

  pwm_counts = __HAL_TIM_GET_AUTORELOAD(&htim3) + 1U;
  corrected_level = ((uint32_t)level * (uint32_t)level) / LED1_LEVEL_MAX;
  compare = pwm_counts - ((corrected_level * pwm_counts) / LED1_LEVEL_MAX);
  __HAL_TIM_SET_COMPARE(&htim3, LED1_PWM_CHANNEL, compare);
}

static void LED2_Write(GPIO_PinState state)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, state);

  gpio.Pin = LED2_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED2_GPIO_Port, &gpio);
}

HAL_StatusTypeDef LED_Init(void)
{
  LED1_ApplyLevel(0U);
  if (HAL_TIM_PWM_Start(&htim3, LED1_PWM_CHANNEL) != HAL_OK)
  {
    return HAL_ERROR;
  }

  s_led1_initialized = 1U;
  s_led1_breathing = 0U;
  return HAL_OK;
}

void LED1_SetBrightness(uint8_t percent)
{
  uint16_t level;

  if (s_led1_initialized == 0U)
  {
    return;
  }
  if (percent > 100U)
  {
    percent = 100U;
  }

  s_led1_breathing = 0U;
  level = (uint16_t)(((uint32_t)percent * LED1_LEVEL_MAX) / 100U);
  LED1_ApplyLevel(level);
}

void LED1_On(void)
{
  LED1_SetBrightness(100U);
}

void LED1_Off(void)
{
  LED1_SetBrightness(0U);
}

void LED1_BreathStart(uint32_t period_ms)
{
  if (s_led1_initialized == 0U)
  {
    return;
  }
  if (period_ms < LED1_BREATH_MIN_MS)
  {
    period_ms = LED1_BREATH_MIN_MS;
  }

  s_led1_breath_period_ms = period_ms;
  s_led1_breath_start_tick = HAL_GetTick();
  s_led1_breathing = 1U;
  LED1_ApplyLevel(0U);
}

void LED1_BreathStop(void)
{
  s_led1_breathing = 0U;
  if (s_led1_initialized != 0U)
  {
    LED1_ApplyLevel(0U);
  }
}

void LED1_Task(void)
{
  uint32_t elapsed;
  uint32_t phase;
  uint32_t half_period;
  uint32_t level;

  if ((s_led1_initialized == 0U) || (s_led1_breathing == 0U))
  {
    return;
  }

  elapsed = HAL_GetTick() - s_led1_breath_start_tick;
  phase = elapsed % s_led1_breath_period_ms;
  half_period = s_led1_breath_period_ms / 2U;
  if (phase < half_period)
  {
    level = (phase * LED1_LEVEL_MAX) / half_period;
  }
  else
  {
    level = ((s_led1_breath_period_ms - phase) * LED1_LEVEL_MAX) /
            (s_led1_breath_period_ms - half_period);
  }
  LED1_ApplyLevel((uint16_t)level);
}

void LED2_On(void)
{
  LED2_Write(GPIO_PIN_RESET);
}

void LED2_Off(void)
{
  LED2_Write(GPIO_PIN_SET);
}
