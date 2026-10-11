<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Sliding Rheostat

A small firmware for the [FoloToy AI Passport](https://github.com/FoloToy/ai-passport), an open ESP32-C3 wearable. Three buttons on the right edge move an intensity from 0 to 30. The portrait on the 240x320 screen follows six stages. A short press of **OK** runs an appraisal: a peak tariff stamps and speaks the peak identity, an idle tariff stamps and speaks the idle identity. A long press of **OK** slides all the way to level 30 and then appraises. At level 30, the same long press slides back to 0.

> **Unofficial.** Not affiliated with FoloToy. Flashing replaces the firmware already on the device. Back up first if you still want that image. Proceed at your own risk.

The board support and the baseline tree come from FoloToy's MIT firmware. This application's screens, logic, portraits, and voices are in the same repository, under the same MIT license ([`LICENSE`](LICENSE)). The appraisal stamp and each spoken line use the current stage name; peak/idle pricing remains in the tag below the stamp. The stage names and scripts are listed in [`README.zh_CN.md`](README.zh_CN.md).

<p align="center">
  <a href="https://x.com/cgnot996"><img src="https://img.shields.io/badge/X-%E9%93%81%E6%9F%B1AGI%20%40cgnot996-black?logo=x&logoColor=white" alt="X @cgnot996" /></a>
  <img src="https://img.shields.io/badge/%E5%BE%AE%E4%BF%A1%E5%85%AC%E4%BC%97%E5%8F%B7-%E9%93%81%E6%9F%B1AGI-07C160?logo=wechat&logoColor=white" alt="WeChat official account" />
</p>

<p align="center">
  <a href="https://x.com/cgnot996">Follow @cgnot996 on X</a>
  ·
  On WeChat, search the account name shown on the badge, or scan the code below
</p>

<p align="center">
  <img src="docs/assets/wechat/mp-search-scan.png" alt="WeChat official account QR code" width="420" />
</p>

## What it does

| | |
|---|---|
| Intensity | **UP** / **DOWN** step by 1. Hold to repeat; the repeat speeds up while the key stays down |
| Appraisal | Short **OK** scans the portrait, stamps it, and plays one line |
| Ramp | Long **OK** slides to level 30, then appraises. At level 30, long **OK** slides back to 0 and appraises |
| Tariff | Weekdays 09:00-12:00 and 14:00-18:00, Beijing time, are peak. Weekends, other hours, and listed public holidays are idle |
| Clock | Wi-Fi SNTP when you compile in credentials. Otherwise the later of the build time and the time saved in NVS. A leading `≈` on the stamp means the clock is estimated |
| Battery | State of charge from the CW2017 gauge. The indicator hides itself if the gauge is missing |
| Screen | Dims after one idle minute, backlight off after two. The next press only wakes the screen |

The holiday table currently covers the 2026 State Council schedule. A later year is treated as an ordinary weekday until that table is extended. Saturday and Sunday stay idle even when a published calendar marks them as makeup workdays.

## Hardware

| | |
|---|---|
| Chip | ESP32-C3, single core, 160 MHz, about 400 KB SRAM, **no PSRAM** |
| Flash | 8 MB embedded, DIO |
| Display | 240x320 ST7789P3, SPI, rounded corners |
| Audio | ES8311 codec, microphone and speaker over I2S. This app plays lines from flash; it does not record |
| Buttons | UP, DOWN, OK on the right edge, one ADC ladder on GPIO0 |
| Battery | CW2017 fuel gauge, 500 mAh |
| USB | The chip's own USB Serial/JTAG (`/dev/cu.usbmodem*` on macOS, `/dev/ttyACM*` on Linux) |

Pins, the panel init table, and the button windows come from FoloToy's BSP (`components/bsp/include/bsp_pins.h`). Deeper hardware notes are in [`docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`](docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md).

## Prepare

- A FoloToy AI Passport, and a USB cable that carries data.
- macOS or Linux.
- [ESP-IDF v5.5.3](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/get-started/index.html) with the `esp32c3` tools. Other versions are not supported. Do not build this tree with the ESP-IDF 6.0.1 environment used by the Muse port.

Install the toolchain once:

```sh
git clone -b v5.5.3 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.3
~/esp/esp-idf-v5.5.3/install.sh esp32c3
. ~/esp/esp-idf-v5.5.3/export.sh
```

Run `export.sh` again in every new shell. `idf.py --version` must print `v5.5.3`.

Clone this firmware:

```sh
git clone https://github.com/RongleCat/ai-passport-sliding-rheostat.git
cd ai-passport-sliding-rheostat
```

## Build

Wi-Fi is optional. It is only used to set the clock from NTP on a 2.4 GHz network. Copy the template and fill in your own network. `main/liang_secrets.h` is gitignored. Do not commit the SSID or the password, and do not put them in `sdkconfig.defaults` or any other tracked file.

```sh
cp main/liang_secrets.example.h main/liang_secrets.h
```

Without that file the firmware skips Wi-Fi and keeps an estimated clock. A password compiled into a binary is in the binary: do not share that build.

```sh
. ~/esp/esp-idf-v5.5.3/export.sh
idf.py set-target esp32c3
idf.py build
```

`idf.py set-target esp32c3` is required on a fresh checkout. Later incremental builds can skip it. The first build downloads LVGL and the other components listed in `components/bsp/idf_component.yml` into `managed_components/`, which git ignores. Do not edit those downloaded sources. `sdkconfig` is generated and gitignored; `sdkconfig.defaults` is the tracked baseline.

To build and check the merged image the way CI does:

```sh
./tools/validate.sh --firmware
```

## Flash

Release every button first. GPIO0 is the key ladder and the chip's boot strap. Holding a key at reset enters the download mode.

Turn the Passport on. Switched off, it does not show up on USB. Find the port with `ls /dev/cu.usb*` on macOS or `ls /dev/ttyACM*` on Linux.

Back up the image already on the device before the first replacement, and keep the file outside git:

```sh
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 read-flash 0 0x800000 ai-passport-backup.bin
```

Flash the three images listed in `build/flash_args`:

```sh
cd build
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 -b 460800 \
  --before default-reset --after hard-reset \
  write-flash @flash_args
```

Replace the port with yours. `flash_args` already says `dio`, 80 MHz, `--flash-size 8MB`, and these addresses: bootloader `0x0`, partition table `0x8000`, application `0x10000`. Each segment must print `Hash of data verified`.

NVS at `0x9000` and PHY data at `0xf000` are not in that write, so a saved level and a saved clock stay. This partition table matches the AI Passport baseline. Do not pass `--erase-all`. Do not run `idf.py erase-flash`. Either one wipes NVS.

On ESP-IDF 5.5.3, `idf.py -p PORT flash` writes the same three images. The Muse port warns against `idf.py flash` because ESP-IDF 6.0.1 splits `SERIAL_TOOL`; that warning belongs to 6.0.1, not to this tree. Still do not erase the chip.

Opening the serial port often resets the board. Read the log after flashing:

```sh
. ~/esp/esp-idf-v5.5.3/export.sh
idf.py -p /dev/cu.usbmodem1101 monitor
```

Leave the monitor with `Ctrl-]`.

To put the backup back:

```sh
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 write-flash 0 ai-passport-backup.bin
```

## First use

- **UP** / **DOWN** change the level by 1. Hold either key to keep stepping; after a short run the steps come faster.
- Short **OK** appraises the current stage.
- Long **OK** ramps to level 30 and appraises. At level 30, long **OK** ramps down to 0 and appraises.
- After one idle minute the backlight dims. After two it turns off. The next press only wakes the screen.
- The stamp shows Beijing time. A leading `≈` means the clock has not synced. Add `main/liang_secrets.h`, rebuild, and flash again.

| Level | Stage |
|---|---|
| 0-5 | 0 |
| 6-11 | 1 |
| 12-17 | 2 |
| 18-23 | 3 |
| 24-29 | 4 |
| 30 | 5 |

## Cautions

- Pair the clock only on a network you trust. The SSID and password are compiled in. Leave them in `main/liang_secrets.h` or nowhere.
- Wi-Fi is 2.4 GHz only.
- There is no PSRAM. The lines are samples in flash, not a speech synthesizer.
- Do not change `partitions.csv` or the three flash addresses. Restoring another firmware takes a full-chip backup you saved yourself, written back at `0x0`.
- A board that was last flashed with a different layout, such as the Muse port whose application sits at `0x20000`, needs one full erase before this image will boot. That erase deletes NVS. After that, go back to segmented writes.
- If flashing cannot see the port, release every button and try again. If it is still missing, hold a button, tap reset, then release, so the chip enters the download mode.
- Do not point this tree at an ESP-IDF other than v5.5.3. The Muse firmware on the same hardware uses v6.0.1. The two environments do not share a shell.

## License

The baseline firmware and the BSP are FoloToy's, under the MIT license ([`LICENSE`](LICENSE)). The sliding-rheostat application in this tree is released under the same license.

| Path | Source | License |
|---|---|---|
| `assets/fonts/liang_font_*.c` | Noto Serif SC Bold / Black subsets | SIL OFL 1.1 |
| `assets/images/liang/` | Portraits prepared for this application | Distributed with this repository under MIT |
| `assets/music/liang/` | Spoken lines prepared for this application | Distributed with this repository under MIT |

Dependencies fetched at build time keep their own licenses. FoloToy and AI Passport are FoloToy's. This project is not endorsed by FoloToy.
