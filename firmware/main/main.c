/*
 * SWMD Phase 1 - ESP32-S3 CSI capture firmware (ESP-IDF v5.3.x)
 *
 * Connects to a 2.4 GHz AP, pings the gateway at 10 Hz to guarantee a
 * steady RX stream (CSI is computed only on RX), enables the CSI engine,
 * and prints one CSV line per frame to the default console:
 *
 *   CSI,<ts_us>,<rssi>,<rate>,<sig_mode>,<mcs>,<cwb>,<channel>,<len>,[i0 r0 i1 r1 ...]
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "ping/ping_sock.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"

#define WIFI_SSID         "csi-test"
#define WIFI_PASS         "12345678"
#define PING_INTERVAL_MS  100      /* 10 Hz CSI rate */
#define TAG               "SWMD"

static void csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf || info->len == 0) return;
    const wifi_pkt_rx_ctrl_t *rx = &info->rx_ctrl;

    printf("CSI,%lld,%d,%u,%u,%u,%u,%u,%u,[",
           (long long)esp_timer_get_time(),
           rx->rssi,
           (unsigned)rx->rate,
           (unsigned)rx->sig_mode,
           (unsigned)rx->mcs,
           (unsigned)rx->cwb,
           (unsigned)rx->channel,
           (unsigned)info->len);

    for (int i = 0; i < info->len; ++i) {
        printf("%d%c", info->buf[i], (i + 1 == info->len) ? ']' : ' ');
    }
    putchar('\n');
}

static void on_ping_success(esp_ping_handle_t hdl, void *args) { (void)hdl; (void)args; }
static void on_ping_timeout(esp_ping_handle_t hdl, void *args) { (void)hdl; (void)args; }

static void start_gateway_ping(uint32_t gw_ip)
{
    ip_addr_t target = { 0 };
    target.type = IPADDR_TYPE_V4;
    target.u_addr.ip4.addr = gw_ip;

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr  = target;
    cfg.count        = ESP_PING_COUNT_INFINITE;
    cfg.interval_ms  = PING_INTERVAL_MS;
    cfg.timeout_ms   = 1000;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end     = NULL,
        .cb_args         = NULL,
    };
    esp_ping_handle_t ping;
    ESP_ERROR_CHECK(esp_ping_new_session(&cfg, &cbs, &ping));
    ESP_ERROR_CHECK(esp_ping_start(ping));
}

static esp_err_t enable_csi(void)
{
    /* ESP-IDF v5.3 rejects HT40 CSI config with the channel filter disabled. */
    wifi_csi_config_t csi_cfg = {
        .lltf_en           = true,
        .htltf_en          = true,
        .stbc_htltf2_en    = false,
        .ltf_merge_en      = false,
        .channel_filter_en = true,
        .manu_scale        = false,
        .shift             = 0,
        .dump_ack_en       = false,
    };
    esp_err_t err = esp_wifi_set_csi_config(&csi_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_csi_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_csi_rx_cb(&csi_rx_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_csi_rx_cb failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_csi failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void net_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP " IPSTR ", gw " IPSTR,
                 IP2STR(&evt->ip_info.ip), IP2STR(&evt->ip_info.gw));
        start_gateway_ping(evt->ip_info.gw.addr);
        if (enable_csi() == ESP_OK) {
            ESP_LOGI(TAG, "CSI streaming started");
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &net_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &net_event_handler, NULL));

    wifi_config_t sta = { 0 };
    strncpy((char *)sta.sta.ssid,     WIFI_SSID, sizeof(sta.sta.ssid) - 1);
    strncpy((char *)sta.sta.password, WIFI_PASS, sizeof(sta.sta.password) - 1);
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}
