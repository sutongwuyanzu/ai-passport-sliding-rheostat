// main/liang_clock.h —— 北京时间来源：Wi-Fi SNTP 对时，失败时退回到
// NVS 存档时间与编译时间中较晚的一个，并在开机期间持续走时。
// 同时负责把强度等级存入 NVS。
#pragma once

#include "esp_err.h"
#include "liang_logic.h"
#include <stdbool.h>

typedef enum {
    LIANG_CLOCK_BUILD = 0,   // 只有编译时间（估算）
    LIANG_CLOCK_SAVED,       // 来自 NVS 存档（估算，断电期间未计入）
    LIANG_CLOCK_NTP,         // 已联网对时
} liang_clock_source_t;

// 初始化 NVS、时区，恢复估算时间，并在后台尝试 Wi-Fi 对时（若编译了凭证）。
esp_err_t liang_clock_start(void);

liang_clock_source_t liang_clock_source(void);

// 当前北京时间。
void liang_clock_now(liang_civil_time_t *out);

// 强度等级的持久化。读取失败返回 -1。
int liang_clock_load_level(void);
// 延迟写入：记录待存值，由后台任务在空闲（无音频播放）时写 Flash。
void liang_clock_save_level_later(int level);
