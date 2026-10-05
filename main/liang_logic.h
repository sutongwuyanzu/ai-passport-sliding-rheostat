// main/liang_logic.h —— 滑动变祖器的纯逻辑：强度/阶段换算、峰谷时段判定、配色插值。
// 不依赖 ESP-IDF 与 LVGL，由 tests/test_liang_logic.c 在主机上覆盖。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LIANG_LEVEL_MAX      30
#define LIANG_STAGE_COUNT    6
#define LIANG_LEVELS_PER_STAGE 6

typedef enum {
    LIANG_TARIFF_PEAK = 0,   // 高峰时段 → 梁文峰
    LIANG_TARIFF_IDLE,       // 空闲时段 → 梁文谷
} liang_tariff_t;

// 本地（北京时间）民用时间。wday: 0=周日 … 6=周六，与 struct tm 一致。
typedef struct {
    int year;
    int month;   // 1..12
    int day;     // 1..31
    int wday;
    int hour;
    int minute;
} liang_civil_time_t;

// 把任意整数夹到 [0, LIANG_LEVEL_MAX]。
int liang_clamp_level(int level);

// 强度 → 阶段：0–5 小难梁，6–11 牢梁，12–17 梁子，18–23 梁圣，24–29 梁神，30 梁祖。
int liang_stage_of(int level);

// 法定节假日放假日（国务院办公厅公布的放假区间）。只收录已公布年份。
bool liang_is_statutory_holiday(int year, int month, int day);

// 峰谷判定：周一至周五（不含法定节假日）09:00–12:00、14:00–18:00 为高峰，
// 其余时段（含周末、法定节假日全天，以及周末调休补班日）为空闲。
liang_tariff_t liang_tariff_at(const liang_civil_time_t *t);

// 0xRRGGBB 线性插值，num/den ∈ [0,1]。
uint32_t liang_lerp_rgb(uint32_t a, uint32_t b, int num, int den);

// 某强度在阶段锚点之间插值后的颜色。anchors 长度为 LIANG_STAGE_COUNT，
// 锚点位于每个阶段的起始强度（0、6、12、18、24、30）。
uint32_t liang_level_color(const uint32_t anchors[LIANG_STAGE_COUNT], int level);

// 刻度尺：强度 level 在 [x0, x0+width] 上的像素 x（四舍五入）。
int liang_scale_x(int level, int x0, int width);
