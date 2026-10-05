// main/liang_audio.h —— 滑动变祖器的音频工作任务：刻度咔哒、鉴定扫描音、印章落地音与配音。
// 所有接口非阻塞、可在任意任务调用（按键回调除外，按键回调只入队）。
#pragma once

#include "esp_err.h"
#include "liang_logic.h"
#include <stdbool.h>
#include <stdint.h>

// 鉴定音效时间线（毫秒，从 liang_audio_announce() 开始计）。UI 动画按同一时间线对齐。
#define LIANG_AUDIO_SCAN_MS   720   // 扫描嘀嘀声
#define LIANG_AUDIO_THUD_MS   220   // 印章落地
#define LIANG_AUDIO_VOICE_AT  (LIANG_AUDIO_SCAN_MS + LIANG_AUDIO_THUD_MS)

// 播放结束回调，在音频任务中调用；只能做非阻塞的轻量操作（例如设置标志）。
typedef void (*liang_audio_done_cb_t)(uint32_t token, void *user);

// 初始化 codec 并启动工作任务。codec 不可用时返回错误，应用应继续以静音模式运行。
esp_err_t liang_audio_start(liang_audio_done_cb_t done_cb, void *user);

// 播放一次刻度咔哒声；正在播报时忽略。
void liang_audio_tick(int stage);

// 开始鉴定：扫描音 → 落地音 → 对应阶段与峰谷的配音。会打断正在播放的内容。
// 返回本次播报的 token，完成或被打断时以该 token 调用 done_cb。
uint32_t liang_audio_announce(int stage, liang_tariff_t tariff);

// 打断当前播放（异步）。
void liang_audio_cancel(void);

// 当前是否有播报在进行（含扫描与落地音）。
bool liang_audio_busy(void);

// 配音时长（毫秒），用于 UI 决定印章停留时间。
uint32_t liang_audio_voice_ms(int stage, liang_tariff_t tariff);
