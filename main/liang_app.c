// main/liang_app.c —— 滑动变祖器：上下键拖动「梁系强度」滑杆，肖像随阶段进化；
// 确定键触发「正脸识别鉴定」，按当前北京时间判定高峰/空闲时段，
// 盖章当前阶段名称，并以同一阶段的台词播报。
//
// 按键：
//   上 / 下   按下即 ±1，长按连续调节（越按越快）
//   确定 单击 正脸鉴定（扫描 → 盖章 + 震屏 → 配音）
//   确定 长按 一键登基（滑到梁祖后鉴定）；已在梁祖时为「退位」，滑回小难梁
//
// 线程：除 liang_app_create/handle_key（调用方持锁）外，所有 UI 更新都在
// LVGL 定时器/动画回调里执行，即运行在 LVGL 任务内。
#include "liang_app.h"

#include "liang_assets.h"
#include "liang_audio.h"
#include "liang_clock.h"
#include "liang_logic.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>

static const char *TAG = "liang_app";

// ---------------------------------------------------------------------------
// 布局（240×320，四角 30 px 圆角遮罩）
// ---------------------------------------------------------------------------
#define FRAME_X      16
#define FRAME_Y      40
#define FRAME_SIZE   (LIANG_PORTRAIT_SIZE + 8)
#define IMG_INSET    4
#define BRACKET_LEN  14
#define NAME_Y       250
#define SCALE_X      16
#define SCALE_Y      282
#define SCALE_W      208
#define SCALE_H      20
#define SCALE_PAD    8          // 刻度在 scale 对象内的左右留白 → 屏幕 x 24..216
#define SCALE_BASE   13         // 基线在 scale 对象内的 y
#define KNOB_D       12
#define HINT_Y       302
#define STAMP_W      156
#define STAMP_H      60
#define STAMP_Y      62

// ---------------------------------------------------------------------------
// 时序
// ---------------------------------------------------------------------------
#define SEQ_PERIOD_MS      25
#define SCAN_SWEEP_MS      (LIANG_AUDIO_SCAN_MS / 2)
#define STAMP_HOLD_MS      1500   // 配音结束后印章停留
#define CROSSFADE_MS       200
#define REPEAT_FIRST_MS    90
#define REPEAT_FAST_MS     45
#define RAMP_STEP_MS       55
#define DIM_AFTER_MS       60000
#define SLEEP_AFTER_MS     120000
#define BATTERY_POLL_MS    30000

static const char *const STAGE_NAMES[LIANG_STAGE_COUNT] = {
    "小难梁", "牢梁", "梁子", "梁圣", "梁神", "梁祖",
};

// 阶段配色锚点（取自参考录屏，深色阶段的文字改为浅色以保证真机可读）。
static const uint32_t BG[LIANG_STAGE_COUNT] = {
    0xE3E4DF, 0xBEBEBA, 0x959593, 0x50504E, 0x282828, 0x161616,
};
static const uint32_t INK_DARK[LIANG_STAGE_COUNT] = {
    0x131311, 0x1B1811, 0x1F1C17, 0x1F1C17, 0x1F1C17, 0x1F1C17,
};
static const uint32_t INK_LIGHT[LIANG_STAGE_COUNT] = {
    0xEAE6DC, 0xEAE6DC, 0xEAE6DC, 0xEAE6DC, 0xEDE3C8, 0xE6C677,
};
static const uint32_t ACCENT[LIANG_STAGE_COUNT] = {
    0x9E2235, 0xA1263A, 0x8E2A3B, 0xC0504A, 0xC9A45C, 0xE0BC6A,
};
#define SEAL_PEAK   0xD7263D   // 高峰：A 股红（涨）
#define SEAL_IDLE   0x1E9E5A   // 空闲：A 股绿（跌）
#define STAMP_PAPER 0xF6EFE2

// 每阶段的特效强度：震屏幅度、闪屏不透明度、印章边框。
static const uint8_t SHAKE_PX[LIANG_STAGE_COUNT]  = { 2, 3, 5, 6, 8, 11 };
static const uint8_t FLASH_OPA[LIANG_STAGE_COUNT] = { 0, 0, 0, 90, 170, 230 };
static const uint8_t SEAL_BORDER[LIANG_STAGE_COUNT] = { 2, 3, 3, 4, 4, 5 };

typedef enum {
    MODE_IDLE = 0,
    MODE_SCANNING,
    MODE_STAMPED,
    MODE_RAMP,
} app_mode_t;

typedef struct {
    uint32_t bg, ink, muted, accent;
} theme_t;

static struct {
    lv_obj_t *scr;
    lv_obj_t *title, *counter, *battery, *battery_fill;
    lv_obj_t *frame, *img_back, *img_front, *scanline;
    lv_obj_t *stamp, *stamp_text, *tag, *tag_text;
    lv_obj_t *name, *state_lbl, *phase_lbl;
    lv_obj_t *scale, *knob;
    lv_obj_t *hint;
    lv_obj_t *flash;
} ui;

static theme_t s_theme;
static int s_level;
static int s_shown_stage = -1;
static app_mode_t s_mode;
static bool s_audio_ok;

// 鉴定状态
static uint32_t s_seq_t0;
static uint32_t s_voice_ms;
static int s_fx_stage;
static lv_timer_t *s_seq_timer;
static lv_timer_t *s_shake_timer;
static int s_shake_left;

// 连续调节 / 一键登基
static lv_timer_t *s_repeat_timer;
static bsp_btn_t s_repeat_btn;
static int s_repeat_count;
static lv_timer_t *s_ramp_timer;
static int s_ramp_dir;

// 息屏
static uint32_t s_last_input;
static uint8_t s_backlight = 100;
static bool s_swallow;   // 唤醒屏幕的那次按键不执行动作

static const uint16_t BTN_MV[BSP_BTN_COUNT][2] = BSP_BTN_MV_TABLE;

// ---------------------------------------------------------------------------
// 主题
// ---------------------------------------------------------------------------
static int luminance(uint32_t rgb) {
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return (r * 299 + g * 587 + b * 114) / 1000;
}

static theme_t theme_for(int level) {
    theme_t t;
    t.bg = liang_level_color(BG, level);
    t.ink = luminance(t.bg) >= 0x70 ? liang_level_color(INK_DARK, level)
                                    : liang_level_color(INK_LIGHT, level);
    t.accent = liang_level_color(ACCENT, level);
    t.muted = liang_lerp_rgb(t.ink, t.bg, 3, 5);
    return t;
}

static void apply_theme(void) {
    s_theme = theme_for(s_level);
    lv_color_t ink = lv_color_hex(s_theme.ink);
    lv_color_t muted = lv_color_hex(s_theme.muted);
    lv_obj_set_style_bg_color(ui.scr, lv_color_hex(s_theme.bg), 0);
    lv_obj_set_style_text_color(ui.title, ink, 0);
    lv_obj_set_style_text_color(ui.counter, ink, 0);
    lv_obj_set_style_text_color(ui.name, ink, 0);
    lv_obj_set_style_text_color(ui.state_lbl, muted, 0);
    lv_obj_set_style_text_color(ui.phase_lbl, muted, 0);
    lv_obj_set_style_text_color(ui.hint, muted, 0);
    lv_obj_set_style_border_color(ui.battery, ink, 0);
    lv_obj_set_style_bg_color(ui.battery_fill, ink, 0);
    lv_obj_set_style_border_color(ui.knob, lv_color_hex(s_theme.accent), 0);
    lv_obj_set_style_bg_color(ui.knob, lv_color_hex(s_theme.bg), 0);
    lv_obj_set_style_bg_color(ui.scanline, lv_color_hex(s_theme.accent), 0);
    lv_obj_set_style_bg_grad_color(ui.scanline, lv_color_hex(s_theme.accent), 0);
    lv_obj_set_style_border_color(ui.scanline, lv_color_hex(0xFFFFFF), 0);
    lv_obj_invalidate(ui.frame);
    lv_obj_invalidate(ui.scale);
}

// ---------------------------------------------------------------------------
// 自绘：取景框四角与刻度尺
// ---------------------------------------------------------------------------
static void fill_rect(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t color, lv_opa_t opa) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    d.bg_opa = opa;
    d.radius = 0;
    lv_area_t a = { x1, y1, x2, y2 };
    lv_draw_rect(layer, &d, &a);
}

static void frame_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    const uint32_t col = s_theme.ink;
    const int L = BRACKET_LEN, T = 2;
    // 左上
    fill_rect(layer, c.x1, c.y1, c.x1 + L - 1, c.y1 + T - 1, col, LV_OPA_COVER);
    fill_rect(layer, c.x1, c.y1, c.x1 + T - 1, c.y1 + L - 1, col, LV_OPA_COVER);
    // 右上
    fill_rect(layer, c.x2 - L + 1, c.y1, c.x2, c.y1 + T - 1, col, LV_OPA_COVER);
    fill_rect(layer, c.x2 - T + 1, c.y1, c.x2, c.y1 + L - 1, col, LV_OPA_COVER);
    // 左下
    fill_rect(layer, c.x1, c.y2 - T + 1, c.x1 + L - 1, c.y2, col, LV_OPA_COVER);
    fill_rect(layer, c.x1, c.y2 - L + 1, c.x1 + T - 1, c.y2, col, LV_OPA_COVER);
    // 右下
    fill_rect(layer, c.x2 - L + 1, c.y2 - T + 1, c.x2, c.y2, col, LV_OPA_COVER);
    fill_rect(layer, c.x2 - T + 1, c.y2 - L + 1, c.x2, c.y2, col, LV_OPA_COVER);
}

static void scale_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    const int x0 = c.x1 + SCALE_PAD;
    const int w = SCALE_W - 2 * SCALE_PAD;
    const int base = c.y1 + SCALE_BASE;
    const int knob_x = liang_scale_x(s_level, x0, w);
    // 基线：未达部分细灰线，已达部分粗强调线。
    fill_rect(layer, x0, base, x0 + w, base, s_theme.muted, LV_OPA_COVER);
    fill_rect(layer, x0, base - 1, knob_x, base, s_theme.accent, LV_OPA_COVER);
    for (int i = 0; i <= LIANG_LEVEL_MAX; i++) {
        const int x = liang_scale_x(i, x0, w);
        const bool major = i % LIANG_LEVELS_PER_STAGE == 0;
        const int h = major ? 9 : 5;
        const bool reached = i <= s_level;
        fill_rect(layer, x, base - h, x + (major ? 1 : 0), base - 1,
                  reached ? s_theme.accent : s_theme.muted, LV_OPA_COVER);
    }
}

// ---------------------------------------------------------------------------
// 滑杆、肖像与文字
// ---------------------------------------------------------------------------
static void knob_x_anim(void *obj, int32_t v) {
    lv_obj_set_x(obj, v);
}

static int knob_target_x(void) {
    const int x0 = SCALE_X + SCALE_PAD;
    return liang_scale_x(s_level, x0, SCALE_W - 2 * SCALE_PAD) - KNOB_D / 2;
}

static void image_opa_anim(void *obj, int32_t v) {
    lv_obj_set_style_image_opa(obj, (lv_opa_t)v, 0);
}

static void crossfade_done(lv_anim_t *a) {
    (void)a;
    // 淡入完成后底图换成新图，前景复位为透明，下次切换再淡入。
    lv_image_set_src(ui.img_back, liang_portrait(s_shown_stage));
    lv_obj_set_style_image_opa(ui.img_front, LV_OPA_TRANSP, 0);
}

static void show_stage(int stage, bool animate) {
    if (stage == s_shown_stage) return;
    s_shown_stage = stage;
    lv_label_set_text(ui.name, STAGE_NAMES[stage]);
    lv_label_set_text_fmt(ui.phase_lbl, "%02d/%02d", stage + 1, LIANG_STAGE_COUNT);
    const lv_image_dsc_t *img = liang_portrait(stage);
    if (!img) return;
    lv_anim_delete(ui.img_front, image_opa_anim);
    if (!animate) {
        lv_image_set_src(ui.img_back, img);
        lv_obj_set_style_image_opa(ui.img_front, LV_OPA_TRANSP, 0);
        return;
    }
    // 只用 image_opa（逐像素混合），不用对象级 opa，避免 LVGL 申请整块图层缓冲。
    lv_image_set_src(ui.img_front, img);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui.img_front);
    lv_anim_set_exec_cb(&a, image_opa_anim);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, CROSSFADE_MS);
    lv_anim_set_completed_cb(&a, crossfade_done);
    lv_anim_start(&a);
}

static void refresh_level(bool animate) {
    lv_label_set_text_fmt(ui.counter, "%02d/%02d", s_level, LIANG_LEVEL_MAX);
    apply_theme();
    show_stage(liang_stage_of(s_level), animate);
    lv_anim_delete(ui.knob, knob_x_anim);
    if (animate) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ui.knob);
        lv_anim_set_exec_cb(&a, knob_x_anim);
        lv_anim_set_values(&a, lv_obj_get_x(ui.knob), knob_target_x());
        lv_anim_set_duration(&a, 90);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    } else {
        lv_obj_set_x(ui.knob, knob_target_x());
    }
}

static void set_hint_default(void) {
    lv_label_set_text(ui.hint, "▲▼ 调浓度 · OK 鉴定");
}

// ---------------------------------------------------------------------------
// 鉴定特效
// ---------------------------------------------------------------------------
static void shake_timer_cb(lv_timer_t *t) {
    (void)t;
    if (s_shake_left <= 0) {
        lv_obj_set_pos(ui.frame, FRAME_X, FRAME_Y);
        lv_timer_delete(s_shake_timer);
        s_shake_timer = NULL;
        return;
    }
    const int amp = SHAKE_PX[s_fx_stage] * s_shake_left / 12 + 1;
    const int sx = (s_shake_left & 1) ? amp : -amp;
    const int sy = (s_shake_left & 2) ? amp / 2 : -amp / 2;
    lv_obj_set_pos(ui.frame, FRAME_X + sx, FRAME_Y + sy);
    s_shake_left--;
}

static void start_shake(void) {
    s_shake_left = s_fx_stage >= 4 ? 16 : 12;
    if (!s_shake_timer) s_shake_timer = lv_timer_create(shake_timer_cb, 30, NULL);
}

static void flash_opa_anim(void *obj, int32_t v) {
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)v, 0);
}

static void flash_done(lv_anim_t *a) {
    lv_obj_add_flag((lv_obj_t *)a->var, LV_OBJ_FLAG_HIDDEN);
}

static void start_flash(void) {
    const lv_opa_t opa = FLASH_OPA[s_fx_stage];
    if (!opa) return;
    // 梁圣白光，梁神/梁祖金光。
    lv_obj_set_style_bg_color(ui.flash, lv_color_hex(s_fx_stage >= 4 ? 0xFFE8A3 : 0xFFFFFF), 0);
    lv_obj_remove_flag(ui.flash, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui.flash);
    lv_anim_set_exec_cb(&a, flash_opa_anim);
    lv_anim_set_values(&a, opa, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, s_fx_stage >= 5 ? 520 : 360);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, flash_done);
    lv_anim_start(&a);
}

static void stamp_y_anim(void *obj, int32_t v) {
    lv_obj_set_y(obj, v);
}

static void stamp_fade_anim(void *obj, int32_t v) {
    (void)obj;
    lv_opa_t o = (lv_opa_t)v;
    lv_obj_set_style_bg_opa(ui.stamp, (lv_opa_t)(v * 235 / 255), 0);
    lv_obj_set_style_border_opa(ui.stamp, o, 0);
    lv_obj_set_style_text_opa(ui.stamp_text, o, 0);
    lv_obj_set_style_bg_opa(ui.tag, o, 0);
    lv_obj_set_style_text_opa(ui.tag_text, o, 0);
}

static void stamp_hide(lv_anim_t *a) {
    (void)a;
    lv_obj_add_flag(ui.stamp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.tag, LV_OBJ_FLAG_HIDDEN);
}

static void stop_effect(bool cancel_audio) {
    if (s_seq_timer) {
        lv_timer_delete(s_seq_timer);
        s_seq_timer = NULL;
    }
    if (s_shake_timer) {
        lv_timer_delete(s_shake_timer);
        s_shake_timer = NULL;
    }
    lv_anim_delete(ui.stamp, stamp_y_anim);
    lv_anim_delete(ui.stamp, stamp_fade_anim);
    lv_anim_delete(ui.flash, flash_opa_anim);
    lv_obj_add_flag(ui.flash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.scanline, LV_OBJ_FLAG_HIDDEN);
    stamp_hide(NULL);
    lv_obj_set_pos(ui.frame, FRAME_X, FRAME_Y);
    if (cancel_audio && s_audio_ok) liang_audio_cancel();
    if (s_mode == MODE_SCANNING || s_mode == MODE_STAMPED) s_mode = MODE_IDLE;
    set_hint_default();
}

static void land_stamp(void) {
    s_mode = MODE_STAMPED;
    lv_obj_add_flag(ui.scanline, LV_OBJ_FLAG_HIDDEN);
    stamp_fade_anim(NULL, LV_OPA_COVER);
    lv_obj_remove_flag(ui.stamp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.tag, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui.stamp);
    lv_anim_set_exec_cb(&a, stamp_y_anim);
    lv_anim_set_values(&a, -STAMP_H - 10, STAMP_Y);
    // 小难梁轻轻放下，其余阶段重重砸下并回弹。
    lv_anim_set_duration(&a, s_fx_stage == 0 ? 320 : 170);
    lv_anim_set_path_cb(&a, s_fx_stage == 0 ? lv_anim_path_ease_out : lv_anim_path_overshoot);
    lv_anim_start(&a);
    start_shake();
    start_flash();
    lv_label_set_text(ui.hint, "OK 再次鉴定 · ▲▼ 调浓度");
}

static void seq_timer_cb(lv_timer_t *t) {
    (void)t;
    const uint32_t el = lv_tick_elaps(s_seq_t0);
    if (s_mode == MODE_SCANNING) {
        if (el < LIANG_AUDIO_SCAN_MS) {
            // 扫描线自上而下扫两遍。
            const int travel = LIANG_PORTRAIT_SIZE - 18;
            const int y = IMG_INSET + (int)((el % SCAN_SWEEP_MS) * travel / SCAN_SWEEP_MS);
            lv_obj_set_y(ui.scanline, y);
        } else {
            land_stamp();
        }
        return;
    }
    if (s_mode == MODE_STAMPED) {
        // 小难梁的印章一直在发抖。
        if (s_fx_stage == 0) {
            const int wobble = ((el / 70) % 2) ? 2 : -2;
            lv_obj_set_x(ui.stamp, (FRAME_SIZE - STAMP_W) / 2 + wobble);
        }
        if (el >= LIANG_AUDIO_VOICE_AT + s_voice_ms + STAMP_HOLD_MS) {
            lv_timer_delete(s_seq_timer);
            s_seq_timer = NULL;
            s_mode = MODE_IDLE;
            set_hint_default();
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, ui.stamp);
            lv_anim_set_exec_cb(&a, stamp_fade_anim);
            lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
            lv_anim_set_duration(&a, 260);
            lv_anim_set_completed_cb(&a, stamp_hide);
            lv_anim_start(&a);
        }
    }
}

static void start_announce(void) {
    stop_effect(false);
    liang_civil_time_t now;
    liang_clock_now(&now);
    const liang_tariff_t tariff = liang_tariff_at(&now);
    const bool peak = tariff == LIANG_TARIFF_PEAK;
    s_fx_stage = liang_stage_of(s_level);
    s_voice_ms = liang_audio_voice_ms(s_fx_stage, tariff);
    if (s_audio_ok) liang_audio_announce(s_fx_stage, tariff);

    const uint32_t seal = peak ? SEAL_PEAK : SEAL_IDLE;
    // 盖章和肖像下方的阶段名保持一致；峰谷信息放在下方标签中。
    lv_label_set_text(ui.stamp_text, STAGE_NAMES[s_fx_stage]);
    lv_obj_set_style_text_color(ui.stamp_text, lv_color_hex(seal), 0);
    lv_obj_set_style_border_color(ui.stamp, lv_color_hex(seal), 0);
    lv_obj_set_style_border_width(ui.stamp, SEAL_BORDER[s_fx_stage], 0);
    lv_obj_set_x(ui.stamp, (FRAME_SIZE - STAMP_W) / 2);
    lv_obj_set_style_bg_color(ui.tag, lv_color_hex(seal), 0);
    const bool estimated = liang_clock_source() != LIANG_CLOCK_NTP;
    lv_label_set_text_fmt(ui.tag_text, "%s%02d:%02d %s",
                          estimated ? "≈" : "", now.hour, now.minute,
                          peak ? "▲ 高峰时段 · 原价" : "▼ 空闲时段 · 五折");
    ESP_LOGI(TAG, "鉴定：强度 %d 阶段 %s，%04d-%02d-%02d %02d:%02d（%s）→ %s",
             s_level, STAGE_NAMES[s_fx_stage], now.year, now.month, now.day,
             now.hour, now.minute, estimated ? "估算时间" : "已对时",
             STAGE_NAMES[s_fx_stage]);

    s_mode = MODE_SCANNING;
    s_seq_t0 = lv_tick_get();
    lv_obj_set_y(ui.scanline, IMG_INSET);
    lv_obj_remove_flag(ui.scanline, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui.hint, "正脸识别中…");
    s_seq_timer = lv_timer_create(seq_timer_cb, SEQ_PERIOD_MS, NULL);
}

// ---------------------------------------------------------------------------
// 调节
// ---------------------------------------------------------------------------
static bool step_level(int delta) {
    const int next = liang_clamp_level(s_level + delta);
    if (next == s_level) return false;
    s_level = next;
    refresh_level(true);
    if (s_audio_ok) liang_audio_tick(liang_stage_of(s_level));
    liang_clock_save_level_later(s_level);
    return true;
}

static void stop_repeat(void) {
    if (s_repeat_timer) {
        lv_timer_delete(s_repeat_timer);
        s_repeat_timer = NULL;
    }
}

static bool button_held(bsp_btn_t btn) {
    const int mv = bsp_button_read_mv();
    return mv >= 0 && mv >= BTN_MV[btn][0] && mv < BTN_MV[btn][1];
}

static void repeat_timer_cb(lv_timer_t *t) {
    if (!button_held(s_repeat_btn) || !step_level(s_repeat_btn == BSP_BTN_UP ? 1 : -1)) {
        stop_repeat();
        return;
    }
    if (++s_repeat_count == 6) lv_timer_set_period(t, REPEAT_FAST_MS);
}

static void stop_ramp(void) {
    if (s_ramp_timer) {
        lv_timer_delete(s_ramp_timer);
        s_ramp_timer = NULL;
    }
    if (s_mode == MODE_RAMP) {
        s_mode = MODE_IDLE;
        set_hint_default();
    }
}

static void ramp_timer_cb(lv_timer_t *t) {
    (void)t;
    if (!step_level(s_ramp_dir)) {
        stop_ramp();
        start_announce();
    }
}

static void start_ramp(void) {
    stop_effect(true);
    stop_repeat();
    s_ramp_dir = s_level >= LIANG_LEVEL_MAX ? -1 : 1;
    s_mode = MODE_RAMP;
    lv_label_set_text(ui.hint, s_ramp_dir > 0 ? "一键登基…" : "退位…");
    if (!s_ramp_timer) s_ramp_timer = lv_timer_create(ramp_timer_cb, RAMP_STEP_MS, NULL);
}

// ---------------------------------------------------------------------------
// 息屏与电量
// ---------------------------------------------------------------------------
static void set_backlight(uint8_t percent) {
    if (s_backlight == percent) return;
    s_backlight = percent;
    bsp_display_backlight(percent);
}

static void idle_timer_cb(lv_timer_t *t) {
    (void)t;
    if (s_mode != MODE_IDLE) {
        s_last_input = lv_tick_get();
        return;
    }
    const uint32_t idle = lv_tick_elaps(s_last_input);
    if (idle >= SLEEP_AFTER_MS) set_backlight(0);
    else if (idle >= DIM_AFTER_MS) set_backlight(15);
}

static void battery_update(void) {
    const int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_obj_add_flag(ui.battery, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(ui.battery, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(ui.battery_fill, LV_MAX(1, 14 * soc / 100));
}

static void battery_timer_cb(lv_timer_t *t) {
    (void)t;
    battery_update();
}

// ---------------------------------------------------------------------------
// 构建界面
// ---------------------------------------------------------------------------
static lv_obj_t *plain_obj(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

void liang_app_create(bool audio_ok, int initial_level) {
    s_audio_ok = audio_ok;
    s_level = initial_level >= 0 ? liang_clamp_level(initial_level) : 0;

    ui.scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(ui.scr);
    lv_obj_set_style_bg_opa(ui.scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ui.scr, LV_OBJ_FLAG_SCROLLABLE);

    // 顶栏：标题、电量、强度计数
    ui.title = label(ui.scr, &liang_font_title_18);
    lv_label_set_text(ui.title, "滑动变祖器");
    lv_obj_set_pos(ui.title, 24, 14);

    ui.battery = plain_obj(ui.scr);
    lv_obj_set_size(ui.battery, 18, 9);
    lv_obj_set_pos(ui.battery, 142, 20);
    lv_obj_set_style_border_width(ui.battery, 1, 0);
    lv_obj_set_style_radius(ui.battery, 2, 0);
    ui.battery_fill = plain_obj(ui.battery);
    lv_obj_set_style_bg_opa(ui.battery_fill, LV_OPA_COVER, 0);
    lv_obj_set_size(ui.battery_fill, 14, 5);
    lv_obj_set_pos(ui.battery_fill, 2, 2);

    ui.counter = label(ui.scr, &liang_font_num_18);
    lv_obj_align(ui.counter, LV_ALIGN_TOP_RIGHT, -24, 15);

    // 肖像区：取景框（自绘四角）+ 双层图片做淡入切换 + 扫描线 + 印章
    ui.frame = plain_obj(ui.scr);
    lv_obj_set_size(ui.frame, FRAME_SIZE, FRAME_SIZE);
    lv_obj_set_pos(ui.frame, FRAME_X, FRAME_Y);
    lv_obj_add_event_cb(ui.frame, frame_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    ui.img_back = lv_image_create(ui.frame);
    lv_obj_set_pos(ui.img_back, IMG_INSET, IMG_INSET);
    lv_obj_set_size(ui.img_back, LIANG_PORTRAIT_SIZE, LIANG_PORTRAIT_SIZE);
    ui.img_front = lv_image_create(ui.frame);
    lv_obj_set_pos(ui.img_front, IMG_INSET, IMG_INSET);
    lv_obj_set_size(ui.img_front, LIANG_PORTRAIT_SIZE, LIANG_PORTRAIT_SIZE);
    lv_obj_set_style_image_opa(ui.img_front, LV_OPA_TRANSP, 0);

    // 扫描线：上方渐隐的光带 + 底部 2 px 亮线
    ui.scanline = plain_obj(ui.frame);
    lv_obj_set_size(ui.scanline, LIANG_PORTRAIT_SIZE, 18);
    lv_obj_set_x(ui.scanline, IMG_INSET);
    lv_obj_set_style_bg_opa(ui.scanline, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_main_opa(ui.scanline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(ui.scanline, 150, 0);
    lv_obj_set_style_bg_grad_dir(ui.scanline, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_side(ui.scanline, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(ui.scanline, 2, 0);
    lv_obj_set_style_border_opa(ui.scanline, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui.scanline, LV_OBJ_FLAG_HIDDEN);

    ui.stamp = plain_obj(ui.frame);
    lv_obj_set_size(ui.stamp, STAMP_W, STAMP_H);
    lv_obj_set_pos(ui.stamp, (FRAME_SIZE - STAMP_W) / 2, STAMP_Y);
    lv_obj_set_style_bg_color(ui.stamp, lv_color_hex(STAMP_PAPER), 0);
    lv_obj_set_style_radius(ui.stamp, 6, 0);
    // 阶段名与肖像下方共用同一套字形，避免盖章出现另一套名称。
    ui.stamp_text = label(ui.stamp, &liang_font_name_28);
    lv_obj_set_style_text_letter_space(ui.stamp_text, 2, 0);
    lv_obj_center(ui.stamp_text);
    lv_obj_add_flag(ui.stamp, LV_OBJ_FLAG_HIDDEN);

    ui.tag = plain_obj(ui.frame);
    lv_obj_set_size(ui.tag, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(ui.tag, 4, 0);
    lv_obj_set_style_pad_hor(ui.tag, 6, 0);
    lv_obj_set_style_pad_ver(ui.tag, 2, 0);
    lv_obj_align(ui.tag, LV_ALIGN_BOTTOM_MID, 0, -IMG_INSET - 8);
    ui.tag_text = label(ui.tag, &liang_font_small_12);
    lv_obj_set_style_text_color(ui.tag_text, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(ui.tag, LV_OBJ_FLAG_HIDDEN);

    // 阶段名与两侧小字
    ui.name = label(ui.scr, &liang_font_name_28);
    lv_obj_set_width(ui.name, 120);
    lv_obj_set_style_text_align(ui.name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui.name, 60, NAME_Y);
    ui.state_lbl = label(ui.scr, &liang_font_small_12);
    lv_label_set_text(ui.state_lbl, "当前状态");
    lv_obj_set_pos(ui.state_lbl, 22, NAME_Y + 10);
    ui.phase_lbl = label(ui.scr, &liang_font_small_12);
    lv_obj_set_width(ui.phase_lbl, 50);
    lv_obj_set_style_text_align(ui.phase_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(ui.phase_lbl, 168, NAME_Y + 10);

    // 刻度尺与滑块
    ui.scale = plain_obj(ui.scr);
    lv_obj_set_size(ui.scale, SCALE_W, SCALE_H);
    lv_obj_set_pos(ui.scale, SCALE_X, SCALE_Y);
    lv_obj_add_event_cb(ui.scale, scale_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    ui.knob = plain_obj(ui.scr);
    lv_obj_set_size(ui.knob, KNOB_D, KNOB_D);
    lv_obj_set_y(ui.knob, SCALE_Y + SCALE_BASE - KNOB_D / 2);
    lv_obj_set_style_radius(ui.knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ui.knob, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui.knob, 3, 0);

    ui.hint = label(ui.scr, &liang_font_small_12);
    lv_obj_set_width(ui.hint, 200);
    lv_obj_set_style_text_align(ui.hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui.hint, 20, HINT_Y);
    set_hint_default();

    // 全屏闪光层（盖在所有内容之上）
    ui.flash = plain_obj(ui.scr);
    lv_obj_set_size(ui.flash, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_style_bg_opa(ui.flash, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(ui.flash, LV_OBJ_FLAG_HIDDEN);

    refresh_level(false);
    battery_update();
    lv_timer_create(battery_timer_cb, BATTERY_POLL_MS, NULL);
    lv_timer_create(idle_timer_cb, 1000, NULL);
    s_last_input = lv_tick_get();
    lv_screen_load(ui.scr);
}

void liang_app_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    s_last_input = lv_tick_get();
    if (ev == BSP_BTN_PRESS) {
        s_swallow = false;
        if (s_backlight < 100) {
            // 先亮屏，这一次按键不触发动作。
            set_backlight(100);
            s_swallow = true;
            return;
        }
    } else if (s_swallow) {
        return;
    }

    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        const int dir = btn == BSP_BTN_UP ? 1 : -1;
        if (ev == BSP_BTN_PRESS) {
            if (s_mode == MODE_RAMP) stop_ramp();
            if (s_mode == MODE_SCANNING || s_mode == MODE_STAMPED) stop_effect(true);
            stop_repeat();
            step_level(dir);
        } else if (ev == BSP_BTN_LONG && s_mode == MODE_IDLE) {
            s_repeat_btn = btn;
            s_repeat_count = 0;
            if (!s_repeat_timer) s_repeat_timer = lv_timer_create(repeat_timer_cb, REPEAT_FIRST_MS, NULL);
        }
        return;
    }

    if (btn == BSP_BTN_OK) {
        if (ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE) {
            if (s_mode != MODE_RAMP) start_announce();
        } else if (ev == BSP_BTN_LONG && s_mode != MODE_RAMP) {
            start_ramp();
        }
    }
}
