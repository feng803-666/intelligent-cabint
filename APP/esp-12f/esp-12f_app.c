#include "esp-12f_app.h"
#include "main.h"
#include "OLED.h"
#include "led.h"
#include "sht30.h"
#include "onenet.h"
#include "esp12f_port.h"
#include <stdio.h>

#define TELEMETRY_PERIOD_MS 5000U
#define NETWORK_RETRY_MS   10000U

static uint8_t network_online, sensor_ready;
static uint32_t network_attempt_at, sample_at;
/* DAPLink 调试观察项：无需串口也可确认真实采样和云端成功次数。 */
static volatile SHT30_Data telemetry_sample;
static volatile uint32_t telemetry_upload_count;
static volatile ESP12F_Status telemetry_last_status;

void ESP12F_App_Init(void)
{
  network_online = 0U;
  sample_at = 0U;
  telemetry_sample = (SHT30_Data){0};
  telemetry_upload_count = 0U;
  telemetry_last_status = ESP12F_ERROR_NOT_INIT;
  sensor_ready = SHT30_Init() == SHT30_OK;
  /* 首次 Task 立即连接；连接失败后才等待 NETWORK_RETRY_MS。 */
  network_attempt_at = HAL_GetTick() - NETWORK_RETRY_MS;
}

/* AT / MQTT 阻塞等待期间继续维护呼吸灯；禁止在此回调重入网络 API。 */
void ESP12F_Port_Idle(void)
{
  LED1_Task();
  HAL_Delay(1U);
}

void ESP12F_App_Task(void)
{
  ESP12F_Status status;
  SHT30_Status sensor_status;
  SHT30_Data data;
  uint32_t now = HAL_GetTick();

  if (!network_online)
  {
    if ((uint32_t)(now - network_attempt_at) < NETWORK_RETRY_MS) return;
    OLED_Clear();
    OLED_ShowString(0, 0, "OneNET connecting", OLED_6X8);
    OLED_Update();
    printf("[OneNET] connecting...\r\n");
    status = OneNET_Connect();
    telemetry_last_status = status;
    network_attempt_at = HAL_GetTick();
    printf("[OneNET] stage=%s result=%s connack=%u wifi=%u\r\n", OneNET_Stage(),
           ESP12F_StatusString(status), (unsigned)ESP12F_LastConnAck(),
           (unsigned)ESP12F_LastWiFiError());
    OLED_Clear();
    OLED_Printf(0, 0, OLED_6X8, "Net: %s", ESP12F_StatusString(status));
    OLED_Update();
    if (status != ESP12F_OK) return;
    network_online = 1U;
    sample_at = HAL_GetTick() - TELEMETRY_PERIOD_MS;
  }

  status = OneNET_Poll();
  if (status != ESP12F_OK)
  {
    telemetry_last_status = status;
    printf("[OneNET] link lost: %s\r\n", ESP12F_StatusString(status));
    network_online = 0U;
    network_attempt_at = HAL_GetTick();
    OLED_ClearArea(0, 0, 128, 8);
    OLED_ShowString(0, 0, "Net: reconnecting", OLED_6X8);
    OLED_Update();
    return;
  }
  if ((uint32_t)(HAL_GetTick() - sample_at) < TELEMETRY_PERIOD_MS) return;
  sample_at = HAL_GetTick();
  sensor_status = sensor_ready ? SHT30_OK : SHT30_Init();
  if (sensor_status == SHT30_OK) sensor_status = SHT30_Read(&data);
  sensor_ready = sensor_status == SHT30_OK;
  if (!sensor_ready)
  {
    printf("[SHT30] read failed=%u; sample skipped\r\n", (unsigned)sensor_status);
    OLED_ClearArea(0, 16, 128, 48);
    OLED_Printf(0, 16, OLED_6X8, "SHT30 error: %u", (unsigned)sensor_status);
    OLED_Update();
    return;
  }

  /* 不启用 newlib 的浮点 printf，避免占用额外 Flash。 */
  telemetry_sample = data;
  int32_t t = (int32_t)(data.temperature_c * 100.0f + (data.temperature_c < 0 ? -0.5f : 0.5f));
  uint32_t abs_t = (uint32_t)(t < 0 ? -t : t);
  uint32_t h = (uint32_t)(data.humidity_rh * 100.0f + 0.5f);
  OLED_Clear();
  OLED_ShowString(0, 0, "Net: online", OLED_6X8);
  OLED_Printf(0, 16, OLED_6X8, "T: %s%lu.%02lu C", t < 0 ? "-" : "",
              (unsigned long)(abs_t / 100U), (unsigned long)(abs_t % 100U));
  OLED_Printf(0, 32, OLED_6X8, "H: %lu.%02lu %%", (unsigned long)(h / 100U), (unsigned long)(h % 100U));
  OLED_Update();
  status = OneNET_Report(data.temperature_c, data.humidity_rh);
  telemetry_last_status = status;
  if (status == ESP12F_OK) ++telemetry_upload_count;
  printf("[OneNET] temp=%s%lu.%02lu humi=%lu.%02lu result=%s code=%u\r\n",
         t < 0 ? "-" : "", (unsigned long)(abs_t / 100U), (unsigned long)(abs_t % 100U),
         (unsigned long)(h / 100U), (unsigned long)(h % 100U),
         ESP12F_StatusString(status), (unsigned)OneNET_LastReplyCode());
  OLED_Printf(0, 48, OLED_6X8, "%s %u", ESP12F_StatusString(status), (unsigned)OneNET_LastReplyCode());
  OLED_Update();
  if (status != ESP12F_OK && status != ESP12F_ERROR_PARAM && status != ESP12F_ERROR_CLOUD_REJECTED)
  {
    network_online = 0U;
    network_attempt_at = HAL_GetTick();
  }
}

