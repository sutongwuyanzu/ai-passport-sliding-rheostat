// main/main.c —— 滑动变祖器入口：初始化板级外设，启动音频/时钟任务，
// 把按键事件经队列转交到持有 LVGL 锁的输入任务。
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "liang_app.h"
#include "liang_assets.h"
#include "liang_audio.h"
#include "liang_clock.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "main";

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static volatile bool s_input_ready;

static void input_task(void *arg) {
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) != pdTRUE) continue;
        if (!bsp_lvgl_lock(500)) continue;
        liang_app_handle_key(input.btn, input.event);
        bsp_lvgl_unlock();
    }
}

// 按键回调运行在共享 esp_timer 任务中：只入队，立即返回。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!s_input_ready) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

static void log_heap(const char *when) {
    ESP_LOGI(TAG, "[%s] 空闲堆 %u 字节，最大连续块 %u 字节", when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void app_main(void) {
    ESP_LOGI(TAG, "滑动变祖器 启动");
    bsp_i2c_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    if (!liang_assets_init()) ESP_LOGE(TAG, "肖像素材不完整");

    const bool audio_ok = liang_audio_start(NULL, NULL) == ESP_OK;
    if (bsp_battery_init() != ESP_OK) ESP_LOGW(TAG, "电量计不可用，隐藏电量显示");
    if (liang_clock_start() != ESP_OK) ESP_LOGE(TAG, "时钟任务启动失败");

    if (bsp_lvgl_lock(1000)) {
        liang_app_create(audio_ok, liang_clock_load_level());
        bsp_lvgl_unlock();
    }
    bsp_display_backlight(100);

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue ||
        xTaskCreate(input_task, "liang_input", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "输入任务创建失败");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败");
        return;
    }
    s_input_ready = true;
    log_heap("就绪");
}
