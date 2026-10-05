// main/liang_assets.c —— 见 liang_assets.h。
// 肖像是 200×200 小端 RGB565 原始像素（tools/liang_assets.py 生成），
// 通过 EMBED_FILES 留在 Flash，LVGL 直接从映射区读取，不复制到内部 RAM。
#include "liang_assets.h"

#include "esp_log.h"
#include <stddef.h>
#include <stdint.h>

static const char *TAG = "liang_assets";

#define PORTRAIT_BYTES (LIANG_PORTRAIT_SIZE * LIANG_PORTRAIT_SIZE * 2)

#define PORTRAIT_SYMBOLS(n) \
    extern const uint8_t portrait##n##_start[] asm("_binary_stage_" #n "_rgb565_start"); \
    extern const uint8_t portrait##n##_end[]   asm("_binary_stage_" #n "_rgb565_end");
PORTRAIT_SYMBOLS(0)
PORTRAIT_SYMBOLS(1)
PORTRAIT_SYMBOLS(2)
PORTRAIT_SYMBOLS(3)
PORTRAIT_SYMBOLS(4)
PORTRAIT_SYMBOLS(5)

static const uint8_t *const STARTS[6] = {
    portrait0_start, portrait1_start, portrait2_start,
    portrait3_start, portrait4_start, portrait5_start,
};
static const uint8_t *const ENDS[6] = {
    portrait0_end, portrait1_end, portrait2_end,
    portrait3_end, portrait4_end, portrait5_end,
};

static lv_image_dsc_t s_portraits[6];

bool liang_assets_init(void) {
    bool ok = true;
    for (int i = 0; i < 6; i++) {
        size_t size = (size_t)(ENDS[i] - STARTS[i]);
        // EMBED_FILES 会在末尾补一个 NUL，允许多 1 字节。
        if (size < PORTRAIT_BYTES) {
            ESP_LOGE(TAG, "肖像 %d 数据只有 %u 字节，应为 %u", i, (unsigned)size, PORTRAIT_BYTES);
            ok = false;
            continue;
        }
        s_portraits[i] = (lv_image_dsc_t){
            .header = {
                .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_RGB565,
                .w = LIANG_PORTRAIT_SIZE,
                .h = LIANG_PORTRAIT_SIZE,
                .stride = LIANG_PORTRAIT_SIZE * 2,
            },
            .data_size = PORTRAIT_BYTES,
            .data = STARTS[i],
        };
    }
    return ok;
}

const lv_image_dsc_t *liang_portrait(int stage) {
    if (stage < 0) stage = 0;
    if (stage > 5) stage = 5;
    return s_portraits[stage].data ? &s_portraits[stage] : NULL;
}
