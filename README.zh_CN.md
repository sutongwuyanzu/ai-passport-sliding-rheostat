<p align="right">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

# 滑动变祖器

跑在 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 上的一块小固件。硬件是开源的 ESP32-C3 可穿戴设备。右侧三个键把强度从 0 调到 30，240x320 屏幕上的肖像跟着六个阶段变。短按 **OK** 做一次鉴定：高峰时段盖高峰的章、念高峰的那一句，空闲时段盖空闲的章、念空闲的那一句。长按 **OK** 会一路滑到 30 再鉴定。已经在 30 时，同样的长按会滑回 0。

> **非官方固件。** 与 FoloToy 无关。刷入会替换设备上现有的固件。还想留着原来的镜像，请先按下文备份。风险自负。

板级支持和基线工程来自 FoloToy 的 MIT 固件。本应用的界面、逻辑、肖像和配音都在这个仓库里，使用同一份 MIT 许可（[`LICENSE`](LICENSE)）。

<p align="center">
  <a href="https://x.com/cgnot996"><img src="https://img.shields.io/badge/X-铁柱AGI%20%40cgnot996-black?logo=x&logoColor=white" alt="X 铁柱AGI" /></a>
  <img src="https://img.shields.io/badge/微信公众号-铁柱AGI-07C160?logo=wechat&logoColor=white" alt="微信公众号 铁柱AGI" />
</p>

<p align="center">
  <a href="https://x.com/cgnot996">关注 X @cgnot996</a>
  ·
  微信搜一搜「铁柱AGI」，或扫下面的码关注公众号
</p>

<p align="center">
  <img src="docs/assets/wechat/mp-search-scan.png" alt="微信搜一搜 铁柱AGI，扫码关注公众号" width="420" />
</p>

## 它做什么

| | |
|---|---|
| 强度 | **UP** / **DOWN** 每次 1 格。按住连续调节，按得越久跳得越快 |
| 鉴定 | 短按 **OK**，肖像扫一遍，盖章，并播一句 |
| 登基 / 退位 | 长按 **OK** 滑到 30 再鉴定。已经在 30 时，长按 **OK** 滑回 0 再鉴定 |
| 峰谷 | 北京时间，周一到周五 09:00-12:00、14:00-18:00 为高峰。周末、其余钟点、以及表内的法定节假日为空闲 |
| 时钟 | 编译进 Wi-Fi 凭证后用 SNTP 对时。否则取编译时间和 NVS 存档时间里较晚的一个。印章时间前面有 `≈`，表示这是估算 |
| 电量 | CW2017 电量计。读不到时电量图标自己隐藏 |
| 屏幕 | 空闲 1 分钟变暗，2 分钟关背光。下一按只负责把屏幕唤醒 |

节假日表目前只收了国务院办公厅公布的 2026 年安排。还没写进表的年份，按普通工作日计算。周六、周日即使被公布为调休补班，也按空闲处理。

六个阶段：

| 强度 | 阶段 |
|---|---|
| 0-5 | 小难梁 |
| 6-11 | 牢梁 |
| 12-17 | 梁子 |
| 18-23 | 梁圣 |
| 24-29 | 梁神 |
| 30 | 梁祖 |

鉴定印章和配音都会使用当前阶段名；下方标签继续显示高峰/空闲与对应价格，六个阶段各有一套高峰和空闲台词。

## 硬件

| | |
|---|---|
| 芯片 | ESP32-C3，单核 160 MHz，约 400 KB SRAM，**无 PSRAM** |
| Flash | 8 MB 内置，DIO |
| 屏幕 | 240x320 ST7789P3，SPI，圆角 |
| 音频 | ES8311 编解码器，麦克风和扬声器走 I2S。本应用只播放烧在 flash 里的句子，不录音 |
| 按键 | 右侧 UP、DOWN、OK，共用 GPIO0 上的一组 ADC 分压 |
| 电池 | CW2017 电量计，500 mAh |
| USB | 芯片自带的 USB Serial/JTAG（macOS 为 `/dev/cu.usbmodem*`，Linux 为 `/dev/ttyACM*`） |

引脚、屏幕初始化参数和按键电压窗口来自 FoloToy 的 BSP（`components/bsp/include/bsp_pins.h`）。更细的硬件说明在 [`docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md`](docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)。

## 准备

- 一块 FoloToy AI Passport，以及一根能传数据的 USB 线。
- macOS 或 Linux。
- [ESP-IDF v5.5.3](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/get-started/index.html)，并安装 `esp32c3` 工具链。不支持其他版本。不要用 Muse 那份固件的 ESP-IDF 6.0.1 环境来编这个仓库。

安装工具链（只需一次）：

```sh
git clone -b v5.5.3 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.3
~/esp/esp-idf-v5.5.3/install.sh esp32c3
. ~/esp/esp-idf-v5.5.3/export.sh
```

每开一个新终端都要再执行一次 `export.sh`。`idf.py --version` 必须打印 `v5.5.3`。

拉下这份固件：

```sh
git clone https://github.com/RongleCat/ai-passport-sliding-rheostat.git
cd ai-passport-sliding-rheostat
```

## 编译

Wi-Fi 是可选的，只用来在 2.4 GHz 网络上做 NTP 对时。复制模板，填你自己的网络。`main/liang_secrets.h` 已被 git 忽略。不要把 SSID 或密码提交进仓库，也不要写进 `sdkconfig.defaults` 或其他会跟踪的文件。

```sh
cp main/liang_secrets.example.h main/liang_secrets.h
```

没有这个文件时，固件跳过联网，时钟保持估算。密码会编进固件，编过密码的二进制不要外传。

```sh
. ~/esp/esp-idf-v5.5.3/export.sh
idf.py set-target esp32c3
idf.py build
```

新克隆下来的目录必须先 `idf.py set-target esp32c3`。之后的增量编译可以跳过。第一次编译会按 `components/bsp/idf_component.yml` 把 LVGL 等依赖下载到 `managed_components/`，这个目录被 git 忽略。不要改里面下载来的源码。`sdkconfig` 是生成文件，同样被忽略；仓库里跟踪的是 `sdkconfig.defaults`。

想按 CI 的方式编译并核对合并镜像：

```sh
./tools/validate.sh --firmware
```

## 刷入

先确认按键都松开。GPIO0 既是三个按键，也是芯片的启动脚，复位时按住会进入下载模式。

先开机。Passport 关机时不会出现在 USB 设备里。查端口：macOS 用 `ls /dev/cu.usb*`，Linux 用 `ls /dev/ttyACM*`。

第一次替换设备上的固件之前，先整片备份，备份文件不要放进 git：

```sh
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 read-flash 0 0x800000 ai-passport-backup.bin
```

按 `build/flash_args` 写入这三段：

```sh
cd build
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 -b 460800 \
  --before default-reset --after hard-reset \
  write-flash @flash_args
```

把端口换成你机器上的那个。`flash_args` 里已经是 `dio`、80 MHz、`--flash-size 8MB`，以及这些地址：bootloader `0x0`，分区表 `0x8000`，应用 `0x10000`。每一段都要看到 `Hash of data verified`。

NVS 在 `0x9000`，PHY 数据在 `0xf000`，这次写入不覆盖它们，所以已经保存的强度和时钟会留下来。这张分区表和 AI Passport 的基线一致。不要加 `--erase-all`，不要运行 `idf.py erase-flash`。那两步都会清掉 NVS。

ESP-IDF 5.5.3 上，`idf.py -p PORT flash` 写入的是同样的三段。Muse 那份说明里不让用 `idf.py flash`，是因为 ESP-IDF 6.0.1 会把 `SERIAL_TOOL` 拆开；那条只适用于 6.0.1，不适用于本仓库。整片擦除仍然不要做。

打开串口经常会让板子复位。看日志放在刷完之后：

```sh
. ~/esp/esp-idf-v5.5.3/export.sh
idf.py -p /dev/cu.usbmodem1101 monitor
```

用 `Ctrl-]` 退出。

把备份写回去：

```sh
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 write-flash 0 ai-passport-backup.bin
```

## 第一次使用

- **UP** / **DOWN** 每次调 1 格。按住会连续跳，跳过一小段以后变得更快。
- 短按 **OK** 鉴定当前阶段。
- 长按 **OK** 滑到 30 再鉴定。已经在 30 时，长按 **OK** 滑回 0 再鉴定。
- 空闲 1 分钟背光变暗，2 分钟关闭。下一按只把屏幕唤醒。
- 印章上是北京时间。前面有 `≈` 表示还没对上时。写好 `main/liang_secrets.h`，重新编译再刷一次。

## 注意事项

- 只在你信任的网络上填写 Wi-Fi。SSID 和密码会编译进固件，只留在 `main/liang_secrets.h` 里。
- 只支持 2.4 GHz。
- 没有 PSRAM。念出来的是 flash 里的采样，不是语音合成。
- 不要改 `partitions.csv`，也不要改上面三个刷写地址。换回别的固件，需要你自己事先保存的整片备份，从 `0x0` 整片写回。
- 如果这块板上次刷的是另一套分区，例如 Muse 移植（应用在 `0x20000`），需要先整片擦除一次，这个镜像才能启动。擦除会清掉 NVS。之后恢复分段写入。
- 刷机找不到端口时，先松开全部按键再试。仍然没有，按住按键、点一下复位、再松开，让芯片进入下载模式。
- 不要用 v5.5.3 以外的 ESP-IDF 来编这个仓库。同一块硬件上的 Muse 固件用的是 v6.0.1。两个环境不要共一个终端。

## 许可

基线固件和 BSP 归 FoloToy，使用 MIT 许可（[`LICENSE`](LICENSE)）。本仓库里的滑动变祖器应用使用同一许可。

| 路径 | 来源 | 许可 |
|---|---|---|
| `assets/fonts/liang_font_*.c` | 思源宋体 Noto Serif SC Bold / Black 子集 | SIL OFL 1.1 |
| `assets/images/liang/` | 为本应用准备的肖像 | 随本仓库以 MIT 分发 |
| `assets/music/liang/` | 为本应用准备的配音 | 随本仓库以 MIT 分发 |

编译时下载的依赖保留各自的许可。FoloToy 和 AI Passport 归 FoloToy 所有。本项目未获其认可或背书。
