// main/liang_audio.c —— 见 liang_audio.h。
// 配音是 16 kHz / 16 bit / 单声道原始 PCM，随固件嵌入 Flash（EMBED_FILES），
// 播放时分块直接从 Flash 映射区写入 I2S，不占用大块内部 RAM。
// 音效（咔哒、扫描嘀嘀、印章落地）在运行时合成。
#include "liang_audio.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stddef.h>
#include <string.h>

static const char *TAG = "liang_audio";

#define SAMPLE_RATE   16000
#define CHUNK_SAMPLES 256
#define VOLUME        82

typedef enum {
    CMD_TICK = 1,
    CMD_ANNOUNCE,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    uint8_t stage;
    uint8_t tariff;
    uint32_t token;
    uint32_t gen;
} audio_cmd_t;

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
} clip_t;

#define CLIP_SYMBOLS(n) \
    extern const uint8_t stage##n##_peak_start[] asm("_binary_stage" #n "_peak_pcm_start"); \
    extern const uint8_t stage##n##_peak_end[]   asm("_binary_stage" #n "_peak_pcm_end"); \
    extern const uint8_t stage##n##_idle_start[] asm("_binary_stage" #n "_idle_pcm_start"); \
    extern const uint8_t stage##n##_idle_end[]   asm("_binary_stage" #n "_idle_pcm_end");
CLIP_SYMBOLS(0)
CLIP_SYMBOLS(1)
CLIP_SYMBOLS(2)
CLIP_SYMBOLS(3)
CLIP_SYMBOLS(4)
CLIP_SYMBOLS(5)

#define CLIP_PAIR(n) \
    { { stage##n##_peak_start, stage##n##_peak_end }, { stage##n##_idle_start, stage##n##_idle_end } }
static const clip_t CLIPS[LIANG_STAGE_COUNT][2] = {
    CLIP_PAIR(0), CLIP_PAIR(1), CLIP_PAIR(2), CLIP_PAIR(3), CLIP_PAIR(4), CLIP_PAIR(5),
};

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static liang_audio_done_cb_t s_done_cb;
static void *s_done_user;
static volatile uint32_t s_gen;        // 每次 announce/cancel 自增；播放中发现变化即中止
static volatile bool s_busy;
static uint32_t s_next_token;
static int16_t s_buf[CHUNK_SAMPLES];

// 64 点正弦表（Q15），用相位累加合成音效，避免 libm 浮点开销。
static const int16_t SINE64[64] = {
         0,   3212,   6393,   9512,  12539,  15446,  18204,  20787,
     23170,  25329,  27245,  28898,  30273,  31356,  32137,  32609,
     32767,  32609,  32137,  31356,  30273,  28898,  27245,  25329,
     23170,  20787,  18204,  15446,  12539,   9512,   6393,   3212,
         0,  -3212,  -6393,  -9512, -12539, -15446, -18204, -20787,
    -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609,
    -32767, -32609, -32137, -31356, -30273, -28898, -27245, -25329,
    -23170, -20787, -18204, -15446, -12539,  -9512,  -6393,  -3212,
};

static inline int16_t sine_at(uint32_t phase) {
    return SINE64[phase >> 26];
}

static inline uint32_t phase_step(uint32_t hz) {
    return (uint32_t)(((uint64_t)hz << 32) / SAMPLE_RATE);
}

static bool still_current(uint32_t gen) {
    return gen == s_gen;
}

static bool write_chunk(const int16_t *pcm, size_t samples) {
    return bsp_audio_write(pcm, samples * sizeof(int16_t)) == ESP_OK;
}

static bool play_silence(uint32_t ms, uint32_t gen) {
    memset(s_buf, 0, sizeof(s_buf));
    uint32_t total = SAMPLE_RATE * ms / 1000;
    while (total > 0) {
        if (!still_current(gen)) return false;
        uint32_t n = total < CHUNK_SAMPLES ? total : CHUNK_SAMPLES;
        if (!write_chunk(s_buf, n)) return false;
        total -= n;
    }
    return true;
}

// 正弦音，频率从 hz0 线性滑到 hz1，起止 5 ms 淡入淡出，amp 为 Q15 幅度。
static bool play_tone(uint32_t hz0, uint32_t hz1, uint32_t ms, int amp, uint32_t gen) {
    const uint32_t total = SAMPLE_RATE * ms / 1000;
    const uint32_t fade = SAMPLE_RATE * 5 / 1000;
    uint32_t phase = 0;
    uint32_t i = 0;
    while (i < total) {
        if (!still_current(gen)) return false;
        uint32_t n = (total - i) < CHUNK_SAMPLES ? (total - i) : CHUNK_SAMPLES;
        for (uint32_t k = 0; k < n; k++, i++) {
            uint32_t hz = hz0 + (uint32_t)(((int64_t)hz1 - hz0) * (int64_t)i / (int64_t)total);
            phase += phase_step(hz);
            int32_t env = amp;
            if (i < fade) env = env * (int32_t)i / (int32_t)fade;
            else if (total - i < fade) env = env * (int32_t)(total - i) / (int32_t)fade;
            s_buf[k] = (int16_t)(((int32_t)sine_at(phase) * env) >> 15);
        }
        if (!write_chunk(s_buf, n)) return false;
    }
    return true;
}

// 印章落地：低频下滑正弦 + 开头一小段噪声，指数衰减。stage 越高越低沉越响。
static bool play_thud(int stage, uint32_t gen) {
    const uint32_t total = SAMPLE_RATE * LIANG_AUDIO_THUD_MS / 1000;
    const uint32_t hz0 = 150 - (uint32_t)stage * 12;
    const uint32_t hz1 = 45;
    const int32_t amp0 = 14000 + stage * 2600;
    uint32_t phase = 0;
    uint32_t noise = 0x1234567u;
    int32_t env = amp0;
    uint32_t i = 0;
    while (i < total) {
        if (!still_current(gen)) return false;
        uint32_t n = (total - i) < CHUNK_SAMPLES ? (total - i) : CHUNK_SAMPLES;
        for (uint32_t k = 0; k < n; k++, i++) {
            uint32_t hz = hz0 - (uint32_t)((hz0 - hz1) * i / total);
            phase += phase_step(hz);
            int32_t s = ((int32_t)sine_at(phase) * env) >> 15;
            if (i < SAMPLE_RATE * 18 / 1000) {
                noise = noise * 1664525u + 1013904223u;
                s += ((int32_t)(noise >> 17) - 16384) * env / 65536;
            }
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            s_buf[k] = (int16_t)s;
            env -= env / 600;   // 约 37 ms 时间常数
        }
        if (!write_chunk(s_buf, n)) return false;
    }
    return true;
}

// 扫描音：三声逐级升高的短促嘀，合计 LIANG_AUDIO_SCAN_MS。
static bool play_scan(uint32_t gen) {
    static const uint16_t SCAN_HZ[3] = { 1320, 1760, 2350 };
    for (int i = 0; i < 3; i++) {
        if (!play_tone(SCAN_HZ[i], SCAN_HZ[i], 70, 7000, gen)) return false;
        if (!play_silence(i < 2 ? 170 : 170 + (LIANG_AUDIO_SCAN_MS - 3 * 240), gen)) return false;
    }
    return true;
}

static bool play_clip(const clip_t *clip, uint32_t gen) {
    const int16_t *pcm = (const int16_t *)clip->start;
    size_t total = (size_t)(clip->end - clip->start) / sizeof(int16_t);
    size_t done = 0;
    while (done < total) {
        if (!still_current(gen)) return false;
        size_t n = (total - done) < CHUNK_SAMPLES ? (total - done) : CHUNK_SAMPLES;
        // Flash 映射区可直接作为 I2S 写入源：驱动会拷贝到 DMA 缓冲。
        if (bsp_audio_write(pcm + done, n * sizeof(int16_t)) != ESP_OK) return false;
        done += n;
    }
    return true;
}

static void run_announce(const audio_cmd_t *cmd) {
    s_busy = true;
    const clip_t *clip = &CLIPS[cmd->stage][cmd->tariff];
    bool ok = play_scan(cmd->gen)
           && play_thud(cmd->stage, cmd->gen)
           && play_clip(clip, cmd->gen);
    if (ok) play_silence(60, cmd->gen);
    if (still_current(cmd->gen)) s_busy = false;
    ESP_LOGI(TAG, "播报 token=%lu %s", (unsigned long)cmd->token, ok ? "完成" : "被打断");
    if (s_done_cb) s_done_cb(cmd->token, s_done_user);
}

static void audio_task(void *arg) {
    (void)arg;
    audio_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (!still_current(cmd.gen)) continue;   // 排队期间已被新命令取代
        if (cmd.type == CMD_TICK) {
            if (s_busy) continue;
            uint32_t hz = 2400 - (uint32_t)cmd.stage * 220;
            play_tone(hz, hz * 3 / 4, 14, 5200, cmd.gen);
        } else if (cmd.type == CMD_ANNOUNCE) {
            run_announce(&cmd);
        }
    }
}

esp_err_t liang_audio_start(liang_audio_done_cb_t done_cb, void *user) {
    if (s_task) return ESP_OK;
    s_done_cb = done_cb;
    s_done_user = user;
    esp_err_t e = bsp_audio_init();
    if (e == ESP_OK) e = bsp_audio_set_format(SAMPLE_RATE, 16, 1);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "音频初始化失败: %s（以静音模式运行）", esp_err_to_name(e));
        return e;
    }
    bsp_audio_set_volume(VOLUME);
    s_queue = xQueueCreate(4, sizeof(audio_cmd_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    // 优先级高于 LVGL 渲染任务，保证 I2S 供给不断档（见硬件指南 8.1）。
    if (xTaskCreate(audio_task, "liang_audio", 3072, NULL, 6, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void liang_audio_tick(int stage) {
    if (!s_queue || s_busy) return;
    audio_cmd_t cmd = { .type = CMD_TICK, .stage = (uint8_t)stage, .gen = s_gen };
    (void)xQueueSend(s_queue, &cmd, 0);
}

uint32_t liang_audio_announce(int stage, liang_tariff_t tariff) {
    uint32_t token = ++s_next_token;
    if (!s_queue) return token;
    if (stage < 0) stage = 0;
    if (stage >= LIANG_STAGE_COUNT) stage = LIANG_STAGE_COUNT - 1;
    s_busy = true;
    audio_cmd_t cmd = {
        .type = CMD_ANNOUNCE,
        .stage = (uint8_t)stage,
        .tariff = (uint8_t)tariff,
        .token = token,
        .gen = ++s_gen,
    };
    if (xQueueSend(s_queue, &cmd, 0) != pdTRUE) {
        xQueueReset(s_queue);
        (void)xQueueSend(s_queue, &cmd, 0);
    }
    return token;
}

void liang_audio_cancel(void) {
    s_gen++;
    s_busy = false;
}

bool liang_audio_busy(void) {
    return s_busy;
}

uint32_t liang_audio_voice_ms(int stage, liang_tariff_t tariff) {
    if (stage < 0 || stage >= LIANG_STAGE_COUNT) return 0;
    const clip_t *clip = &CLIPS[stage][tariff == LIANG_TARIFF_PEAK ? 0 : 1];
    size_t samples = (size_t)(clip->end - clip->start) / sizeof(int16_t);
    return (uint32_t)(samples * 1000 / SAMPLE_RATE);
}
