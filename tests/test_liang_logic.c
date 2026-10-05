// tests/test_liang_logic.c —— 滑动变祖器纯逻辑的主机测试。
#include "liang_logic.h"

#include <stdio.h>
#include <stdlib.h>

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

static liang_civil_time_t at(int y, int mo, int d, int wday, int h, int mi) {
    liang_civil_time_t t = { y, mo, d, wday, h, mi };
    return t;
}

static void test_levels_and_stages(void) {
    CHECK(liang_clamp_level(-3) == 0);
    CHECK(liang_clamp_level(31) == LIANG_LEVEL_MAX);
    CHECK(liang_stage_of(0) == 0);
    CHECK(liang_stage_of(5) == 0);
    CHECK(liang_stage_of(6) == 1);
    CHECK(liang_stage_of(7) == 1);    // 视频 07/30 = 牢梁
    CHECK(liang_stage_of(13) == 2);   // 13/30 = 梁子
    CHECK(liang_stage_of(18) == 3);   // 18/30 = 梁圣
    CHECK(liang_stage_of(23) == 3);   // 23/30 = 梁圣
    CHECK(liang_stage_of(24) == 4);
    CHECK(liang_stage_of(28) == 4);   // 28/30 = 梁神
    CHECK(liang_stage_of(29) == 4);
    CHECK(liang_stage_of(30) == 5);   // 30/30 = 梁祖
    CHECK(liang_stage_of(99) == 5);
}

static void test_tariff(void) {
    // 2026-10-09 周五，普通工作日。
    liang_civil_time_t t = at(2026, 10, 9, 5, 8, 59);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_IDLE);
    t = at(2026, 10, 9, 5, 9, 0);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_PEAK);
    t = at(2026, 10, 9, 5, 11, 59);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_PEAK);
    t = at(2026, 10, 9, 5, 12, 0);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_IDLE);
    t = at(2026, 10, 9, 5, 13, 59);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_IDLE);
    t = at(2026, 10, 9, 5, 14, 0);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_PEAK);
    t = at(2026, 10, 9, 5, 17, 59);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_PEAK);
    t = at(2026, 10, 9, 5, 18, 0);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_IDLE);

    // 国庆假期内的周一（2026-10-05）全天空闲。
    t = at(2026, 10, 5, 1, 10, 0);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_IDLE);
    // 节后第一个工作日（2026-10-08 周四）恢复高峰。
    t = at(2026, 10, 8, 4, 10, 0);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_PEAK);
    // 周末调休补班日（2026-10-10 周六）按规则仍属于周末空闲。
    t = at(2026, 10, 10, 6, 10, 0);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_IDLE);
    // 普通周日。
    t = at(2026, 10, 11, 0, 15, 0);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_IDLE);
    // 春节区间首尾。
    CHECK(liang_is_statutory_holiday(2026, 2, 15));
    CHECK(liang_is_statutory_holiday(2026, 2, 23));
    CHECK(!liang_is_statutory_holiday(2026, 2, 24));
    CHECK(!liang_is_statutory_holiday(2026, 2, 14));
    // 未收录年份只按星期与时段判断。
    t = at(2027, 1, 4, 1, 10, 0);
    CHECK(liang_tariff_at(&t) == LIANG_TARIFF_PEAK);
    CHECK(liang_tariff_at(NULL) == LIANG_TARIFF_IDLE);
}

static void test_colors(void) {
    CHECK(liang_lerp_rgb(0x000000, 0xFFFFFF, 0, 6) == 0x000000);
    CHECK(liang_lerp_rgb(0x000000, 0xFFFFFF, 6, 6) == 0xFFFFFF);
    CHECK(liang_lerp_rgb(0x000000, 0xFFFFFF, 3, 6) == 0x808080);
    CHECK(liang_lerp_rgb(0xFF0000, 0x0000FF, 1, 2) == 0x7F0080);
    CHECK(liang_lerp_rgb(0xFFFFFF, 0x000000, 1, 2) == 0x7F7F7F);
    const uint32_t anchors[LIANG_STAGE_COUNT] = {
        0xEEEEEE, 0xCCCCCC, 0xAAAAAA, 0x888888, 0x444444, 0x111111,
    };
    CHECK(liang_level_color(anchors, 0) == 0xEEEEEE);
    CHECK(liang_level_color(anchors, 6) == 0xCCCCCC);
    CHECK(liang_level_color(anchors, 3) == 0xDDDDDD);
    CHECK(liang_level_color(anchors, 30) == 0x111111);
    CHECK(liang_level_color(anchors, 27) == liang_lerp_rgb(0x444444, 0x111111, 3, 6));
}

static void test_scale(void) {
    CHECK(liang_scale_x(0, 24, 192) == 24);
    CHECK(liang_scale_x(30, 24, 192) == 216);
    CHECK(liang_scale_x(15, 24, 192) == 120);
    CHECK(liang_scale_x(-1, 24, 192) == 24);
    for (int i = 1; i <= LIANG_LEVEL_MAX; i++) {
        CHECK(liang_scale_x(i, 24, 192) > liang_scale_x(i - 1, 24, 192));
    }
}

int main(void) {
    test_levels_and_stages();
    test_tariff();
    test_colors();
    test_scale();
    if (s_failures) {
        fprintf(stderr, "test_liang_logic: %d failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    printf("test_liang_logic: PASS\n");
    return EXIT_SUCCESS;
}
