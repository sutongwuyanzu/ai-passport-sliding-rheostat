// main/liang_logic.c —— 见 liang_logic.h。
#include "liang_logic.h"

#include <stddef.h>

int liang_clamp_level(int level) {
    if (level < 0) return 0;
    if (level > LIANG_LEVEL_MAX) return LIANG_LEVEL_MAX;
    return level;
}

int liang_stage_of(int level) {
    int stage = liang_clamp_level(level) / LIANG_LEVELS_PER_STAGE;
    return stage < LIANG_STAGE_COUNT ? stage : LIANG_STAGE_COUNT - 1;
}

typedef struct {
    uint16_t year;
    uint8_t m0, d0;   // 放假首日
    uint8_t m1, d1;   // 放假末日（含）
} holiday_range_t;

// 《国务院办公厅关于2026年部分节假日安排的通知》（国办发明电〔2025〕7号，2025-11-04）。
// https://www.gov.cn/zhengce/zhengceku/202511/content_7047091.htm
// 2027 年安排截至 2026-10-05 尚未发布；公布后在此追加。
static const holiday_range_t HOLIDAYS[] = {
    { 2026,  1,  1,  1,  3 },   // 元旦
    { 2026,  2, 15,  2, 23 },   // 春节
    { 2026,  4,  4,  4,  6 },   // 清明节
    { 2026,  5,  1,  5,  5 },   // 劳动节
    { 2026,  6, 19,  6, 21 },   // 端午节
    { 2026,  9, 25,  9, 27 },   // 中秋节
    { 2026, 10,  1, 10,  7 },   // 国庆节
};

bool liang_is_statutory_holiday(int year, int month, int day) {
    const int key = month * 100 + day;
    for (size_t i = 0; i < sizeof(HOLIDAYS) / sizeof(HOLIDAYS[0]); i++) {
        const holiday_range_t *h = &HOLIDAYS[i];
        if (h->year != year) continue;
        if (key >= h->m0 * 100 + h->d0 && key <= h->m1 * 100 + h->d1) return true;
    }
    return false;
}

liang_tariff_t liang_tariff_at(const liang_civil_time_t *t) {
    if (!t) return LIANG_TARIFF_IDLE;
    if (t->wday == 0 || t->wday == 6) return LIANG_TARIFF_IDLE;
    if (liang_is_statutory_holiday(t->year, t->month, t->day)) return LIANG_TARIFF_IDLE;
    const int minutes = t->hour * 60 + t->minute;
    const bool morning = minutes >= 9 * 60 && minutes < 12 * 60;
    const bool afternoon = minutes >= 14 * 60 && minutes < 18 * 60;
    return (morning || afternoon) ? LIANG_TARIFF_PEAK : LIANG_TARIFF_IDLE;
}

static int lerp_channel(int a, int b, int num, int den) {
    // 带符号四舍五入，保证端点精确命中。
    int diff = (b - a) * num;
    int half = den / 2;
    return a + (diff >= 0 ? (diff + half) / den : -((-diff + half) / den));
}

uint32_t liang_lerp_rgb(uint32_t a, uint32_t b, int num, int den) {
    if (den <= 0) return a;
    if (num <= 0) return a;
    if (num >= den) return b;
    uint32_t out = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        int ca = (int)((a >> shift) & 0xFF);
        int cb = (int)((b >> shift) & 0xFF);
        out |= (uint32_t)lerp_channel(ca, cb, num, den) << shift;
    }
    return out;
}

uint32_t liang_level_color(const uint32_t anchors[LIANG_STAGE_COUNT], int level) {
    level = liang_clamp_level(level);
    int stage = liang_stage_of(level);
    if (stage >= LIANG_STAGE_COUNT - 1) return anchors[LIANG_STAGE_COUNT - 1];
    int offset = level - stage * LIANG_LEVELS_PER_STAGE;
    return liang_lerp_rgb(anchors[stage], anchors[stage + 1], offset, LIANG_LEVELS_PER_STAGE);
}

int liang_scale_x(int level, int x0, int width) {
    level = liang_clamp_level(level);
    return x0 + (level * width + LIANG_LEVEL_MAX / 2) / LIANG_LEVEL_MAX;
}
