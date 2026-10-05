// main/liang_assets.h —— 滑动变祖器的嵌入素材：6 张阶段肖像与中文字体子集。
#pragma once

#include "lvgl.h"
#include <stdbool.h>

#define LIANG_PORTRAIT_SIZE 200

LV_FONT_DECLARE(liang_font_title_18)
LV_FONT_DECLARE(liang_font_num_18)
LV_FONT_DECLARE(liang_font_name_28)
LV_FONT_DECLARE(liang_font_stamp_34)
LV_FONT_DECLARE(liang_font_small_12)

// 校验嵌入数据尺寸并填好图片描述符。数据异常时返回 false（对应肖像不显示）。
bool liang_assets_init(void);

// 阶段 0..5 的肖像。
const lv_image_dsc_t *liang_portrait(int stage);
