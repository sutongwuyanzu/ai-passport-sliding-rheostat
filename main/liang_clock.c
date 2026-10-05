// main/liang_clock.c —— 见 liang_clock.h。
#include "liang_clock.h"

#include "liang_audio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#if __has_include("liang_secrets.h")
#include "liang_secrets.h"
#endif
#ifndef LIANG_WIFI_SSID
#define LIANG_WIFI_SSID ""
#define LIANG_WIFI_PASS ""
#endif

static const char *TAG = "liang_clock";

#define NVS_NS            "liang"
#define NVS_KEY_EPOCH     "epoch"
#define NVS_KEY_LEVEL     "level"
#define SAVE_EPOCH_EVERY_S  (10 * 60)
#define SYNC_TIMEOUT_MS     20000
#define SYNC_RETRY_S        (15 * 60)
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAILED_BIT     BIT1
#define WIFI_MAX_RETRY      5

static bool s_nvs_ok;
static volatile liang_clock_source_t s_source = LIANG_CLOCK_BUILD;
static int s_pending_level = -1;
static EventGroupHandle_t s_wifi_events;
static int s_wifi_retry;
static bool s_wifi_ready;

static esp_err_t nvs_get_epoch(int64_t *out) {
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (e != ESP_OK) return e;
    e = nvs_get_i64(h, NVS_KEY_EPOCH, out);
    nvs_close(h);
    return e;
}

static void nvs_put_i32(const char *key, int32_t v) {
    if (!s_nvs_ok) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_i32(h, key, v) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static void save_epoch(void) {
    if (!s_nvs_ok) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_i64(h, NVS_KEY_EPOCH, (int64_t)time(NULL)) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static void restore_estimate(void) {
    int64_t epoch = LIANG_BUILD_EPOCH;
    s_source = LIANG_CLOCK_BUILD;
    int64_t saved = 0;
    if (s_nvs_ok && nvs_get_epoch(&saved) == ESP_OK && saved > epoch) {
        epoch = saved;
        s_source = LIANG_CLOCK_SAVED;
    }
    struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "估算时间来源=%s epoch=%lld",
             s_source == LIANG_CLOCK_SAVED ? "NVS 存档" : "编译时间", (long long)epoch);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry++ < WIFI_MAX_RETRY) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_prepare(void) {
    if (s_wifi_ready) return ESP_OK;
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK) return e;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    e = esp_wifi_init(&cfg);
    if (e != ESP_OK) return e;
    e = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL);
    if (e == ESP_OK) {
        e = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL);
    }
    if (e != ESP_OK) return e;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, LIANG_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, LIANG_WIFI_PASS, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = strlen(LIANG_WIFI_PASS) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    e = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (e != ESP_OK) return e;

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "ntp.tencent.com", "cn.pool.ntp.org"));
    sntp.start = false;
    e = esp_netif_sntp_init(&sntp);
    if (e != ESP_OK) return e;
    s_wifi_ready = true;
    return ESP_OK;
}

static bool try_sync_once(void) {
    if (wifi_prepare() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi 初始化失败，放弃联网对时");
        return false;
    }
    s_wifi_retry = 0;
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
    if (esp_wifi_start() != ESP_OK) return false;
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(SYNC_TIMEOUT_MS));
    bool ok = false;
    if (bits & WIFI_CONNECTED_BIT) {
        esp_netif_sntp_start();
        ok = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SYNC_TIMEOUT_MS)) == ESP_OK;
    }
    ESP_LOGI(TAG, "联网对时%s（Wi-Fi %s）", ok ? "成功" : "失败",
             (bits & WIFI_CONNECTED_BIT) ? "已连接" : "未连接");
    // 对时后关闭射频：省电，并把无线占用的时间片让给画面与音频。
    esp_wifi_disconnect();
    esp_wifi_stop();
    return ok;
}

static void clock_task(void *arg) {
    (void)arg;
    const bool has_wifi = strlen(LIANG_WIFI_SSID) > 0;
    int64_t next_sync = 0;
    int64_t last_epoch_save = time(NULL);
    for (;;) {
        int64_t now_s = (int64_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
        // 联网对时与写 Flash 都避开播报，避免 I2S 供给被 cache 停顿打断（硬件指南 8.1）。
        if (has_wifi && s_source != LIANG_CLOCK_NTP && now_s >= next_sync && !liang_audio_busy()) {
            if (try_sync_once()) {
                s_source = LIANG_CLOCK_NTP;
                save_epoch();
                last_epoch_save = time(NULL);
            } else {
                next_sync = now_s + SYNC_RETRY_S;
            }
        }
        if (!liang_audio_busy()) {
            // 原子取走待存值：UI 在两步之间写入的新值不会被清掉。
            int level = __atomic_exchange_n(&s_pending_level, -1, __ATOMIC_SEQ_CST);
            if (level >= 0) nvs_put_i32(NVS_KEY_LEVEL, level);
            if (time(NULL) - last_epoch_save >= SAVE_EPOCH_EVERY_S) {
                save_epoch();
                last_epoch_save = time(NULL);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t liang_clock_start(void) {
    setenv("TZ", "CST-8", 1);
    tzset();
    esp_err_t e = nvs_flash_init();
    s_nvs_ok = e == ESP_OK;
    if (!s_nvs_ok) {
        // 不自动擦除：保留设备上可能存在的其他数据，只是放弃持久化。
        ESP_LOGE(TAG, "NVS 初始化失败: %s；本次不保存时间与强度", esp_err_to_name(e));
    }
    restore_estimate();
    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) return ESP_ERR_NO_MEM;
    if (xTaskCreate(clock_task, "liang_clock", 4096, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (strlen(LIANG_WIFI_SSID) == 0) ESP_LOGW(TAG, "未配置 Wi-Fi 凭证，使用估算时间");
    return ESP_OK;
}

liang_clock_source_t liang_clock_source(void) {
    return s_source;
}

void liang_clock_now(liang_civil_time_t *out) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    out->year = tm.tm_year + 1900;
    out->month = tm.tm_mon + 1;
    out->day = tm.tm_mday;
    out->wday = tm.tm_wday;
    out->hour = tm.tm_hour;
    out->minute = tm.tm_min;
}

int liang_clock_load_level(void) {
    if (!s_nvs_ok) return -1;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return -1;
    int32_t v = -1;
    esp_err_t e = nvs_get_i32(h, NVS_KEY_LEVEL, &v);
    nvs_close(h);
    return e == ESP_OK ? (int)v : -1;
}

void liang_clock_save_level_later(int level) {
    __atomic_store_n(&s_pending_level, level, __ATOMIC_SEQ_CST);
}
