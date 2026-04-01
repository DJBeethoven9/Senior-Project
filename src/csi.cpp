#include "csi.h"

float    csiAmplitude[CSI_SUBCARRIERS] = {0};
float    csiPhase[CSI_SUBCARRIERS]     = {0};
float    rssiHistory[HISTORY_SIZE]     = {0};
int      historyIndex   = 0;
int      currentRSSI    = 0;
uint32_t csiPacketCount = 0;

portMUX_TYPE csiMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR csiCallback(void* ctx, wifi_csi_info_t* info) {
  if (!info || !info->buf || info->len < 2) return;

  int offset = info->first_word_invalid ? 4 : 0;
  int available = info->len - offset;
  if (available < 2) return;

  int8_t* buf = info->buf + offset;
  int len = min(available / 2, CSI_SUBCARRIERS);

  portENTER_CRITICAL_ISR(&csiMux);
  for (int i = 0; i < len; i++) {
    float imag = buf[2 * i];
    float real = buf[2 * i + 1];
    csiAmplitude[i] = sqrtf(real * real + imag * imag);
    csiPhase[i]     = atan2f(imag, real) * 180.0f / M_PI;
  }

  for (int i = len; i < CSI_SUBCARRIERS; i++) {
    csiAmplitude[i] = 0.0f;
    csiPhase[i] = 0.0f;
  }

  currentRSSI               = info->rx_ctrl.rssi;
  rssiHistory[historyIndex] = currentRSSI;
  historyIndex              = (historyIndex + 1) % HISTORY_SIZE;
  csiPacketCount++;
  portEXIT_CRITICAL_ISR(&csiMux);
}

void csiSetup() {
  wifi_csi_config_t csiConfig = {
    .lltf_en           = true,
    .htltf_en          = true,
    .stbc_htltf2_en    = true,
    .ltf_merge_en      = true,
    .channel_filter_en = true,
    .manu_scale        = false,
    .shift             = 0,
  };

  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;

  esp_err_t err = esp_wifi_set_csi_config(&csiConfig);
  if (err != ESP_OK) {
    Serial.printf("CSI config failed: %d\n", err);
    return;
  }

  err = esp_wifi_set_promiscuous_filter(&filter);
  if (err != ESP_OK) {
    Serial.printf("Promiscuous filter failed: %d\n", err);
  }

  err = esp_wifi_set_promiscuous(true);
  if (err != ESP_OK) {
    Serial.printf("Promiscuous mode failed: %d\n", err);
  }

  err = esp_wifi_set_csi_rx_cb(csiCallback, NULL);
  if (err != ESP_OK) {
    Serial.printf("CSI callback failed: %d\n", err);
    return;
  }

  err = esp_wifi_set_csi(true);
  if (err != ESP_OK) {
    Serial.printf("CSI enable failed: %d\n", err);
    return;
  }

  Serial.println("CSI enabled in promiscuous mode");
}
