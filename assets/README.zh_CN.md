<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 滑动变祖器应用素材

| 路径 | 格式 | 来源与接入方式 |
| --- | --- | --- |
| `images/liang/src/stage_{0..5}.png` | 354 × 354 PNG | 六个阶段肖像（小难梁 → 梁祖），从应用作者提供的参考录屏中裁出；每个阶段取稳定帧段中最清晰的一帧，未做时间平均。 |
| `images/liang/stage_{0..5}.png` / `.rgb565` | 200 × 200 PNG 预览 / 小端 RGB565 原始像素（80 000 字节） | 由 `python3 tools/liang_assets.py` 生成（Lanczos 缩小 + 轻度锐化）。`.rgb565` 通过 `EMBED_FILES` 嵌入，由 `main/liang_assets.c` 包装成 `lv_image_dsc_t`。 |
| `fonts/liang_font_*.c` | LVGL 9 位图字体，4 bpp，未压缩 | 思源宋体 Noto Serif SC Bold/Black（SIL OFL 1.1）子集，用 `lv_font_conv` 1.5.2 经 `python3 tools/liang_assets.py --fonts --otf-dir <目录> --lv-font-conv <路径>` 生成；每个子集只含该脚本 `FONTS` 中列出的字形。 |
| `music/liang/stage{0..5}_{peak,idle}.pcm` | 16 kHz、16 bit、单声道、无头 s16le | 按 `music/liang/voices.json` 中的阶段音色描述与台词生成；每段台词明确说出当前阶段名，裁掉首尾静音并归一响度。通过 `EMBED_FILES` 嵌入，播放时从 Flash 分块读取。 |
