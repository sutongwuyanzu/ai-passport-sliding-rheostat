// main/liang_app.h —— 滑动变祖器应用：界面、按键交互与鉴定特效。
#pragma once

#include "bsp_button.h"
#include "esp_err.h"
#include <stdbool.h>

// 建立界面并载入屏幕。必须在 LVGL 任务内或持有 bsp_lvgl_lock() 时调用。
// audio_ok 为 false 时以静音模式运行；initial_level 为 -1 时从 0 开始。
void liang_app_create(bool audio_ok, int initial_level);

// 处理一个按键事件。必须在 LVGL 任务内或持有 bsp_lvgl_lock() 时调用。
void liang_app_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
