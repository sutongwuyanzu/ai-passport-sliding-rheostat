// main/liang_secrets.example.h —— Wi-Fi 对时凭证模板。
// 复制为 main/liang_secrets.h（已被 .gitignore 忽略，不要提交）并填写 2.4 GHz 网络。
// 不存在 liang_secrets.h 时固件跳过联网，只用编译时间 / NVS 存档时间估算。
#pragma once

#define LIANG_WIFI_SSID "your-2.4GHz-ssid"
#define LIANG_WIFI_PASS "your-password"
