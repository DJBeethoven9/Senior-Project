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

  int8_t* buf = info->buf;
  int     len = min((int)(info->len / 2), CSI_SUBCARRIERS);

  portENTER_CRITICAL_ISR(&csiMux);
  for (int i = 0; i < len; i++) {
    float imag = buf[2 * i];
    float real = buf[2 * i + 1];
    csiAmplitude[i] = sqrtf(real * real + imag * imag);
    csiPhase[i]     = atan2f(imag, real) * 180.0f / M_PI;
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
    .channel_filter_en = false,
    .manu_scale        = false,
    .shift             = 0,
  };
  esp_wifi_set_csi_config(&csiConfig);
  esp_wifi_set_csi_rx_cb(csiCallback, NULL);
  esp_wifi_set_csi(true);
  Serial.println("CSI enabled!");
}