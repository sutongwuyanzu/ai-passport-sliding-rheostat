#!/usr/bin/env python3
"""Build display assets for the 滑动变祖器 (sliding rheostat) app.

Portraits: assets/images/liang/src/stage_N.png (354x354 crops from the
reference recording) -> assets/images/liang/stage_N.png (preview) and
assets/images/liang/stage_N.rgb565 (raw little-endian RGB565, embedded as-is
into the firmware and wrapped in an lv_image_dsc_t at runtime).

Fonts: Noto Serif SC subsets generated with lv_font_conv into assets/fonts/.
Pass --fonts together with the OTF directory and lv_font_conv path.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
from pathlib import Path

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
IMG_DIR = ROOT / "assets" / "images" / "liang"
FONT_DIR = ROOT / "assets" / "fonts"
PORTRAIT_SIZE = 200
STAGES = 6

ASCII = "".join(chr(c) for c in range(0x20, 0x7F))

# Each subset lists exactly the glyphs its widgets render; keep in sync with
# the strings in main/liang_app.c (tests/test_liang_fonts.py checks this).
FONTS = [
    # name, weight, size, symbols
    ("liang_font_title_18", "Black", 18, "滑动变祖器"),
    ("liang_font_num_18", "Black", 18, "0123456789/ "),
    ("liang_font_name_28", "Black", 28, "小难梁牢子圣神祖"),
    ("liang_font_stamp_34", "Black", 34, "梁文峰谷"),
    (
        "liang_font_small_12",
        "Bold",
        12,
        ASCII + "当前状态阶段调浓度鉴定正脸识别中高峰空闲时原价五折一键登基退位再次"
        "▲▼·…≈：",
    ),
]


def build_portraits() -> None:
    for i in range(STAGES):
        src = Image.open(IMG_DIR / "src" / f"stage_{i}.png").convert("RGB")
        img = src.resize((PORTRAIT_SIZE, PORTRAIT_SIZE), Image.LANCZOS)
        img = img.filter(ImageFilter.UnsharpMask(radius=0.8, percent=60, threshold=2))
        img.save(IMG_DIR / f"stage_{i}.png")
        out = bytearray()
        for r, g, b in img.getdata():
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out += struct.pack("<H", v)
        (IMG_DIR / f"stage_{i}.rgb565").write_bytes(out)
        print(f"stage_{i}: {len(out)} bytes")


def build_fonts(otf_dir: Path, conv: str) -> None:
    for name, weight, size, symbols in FONTS:
        otf = otf_dir / f"NotoSerifSC-{weight}.otf"
        cmd = [
            conv, "--font", str(otf), "--symbols", symbols,
            "--size", str(size), "--bpp", "4", "--format", "lvgl", "--no-compress",
            "--lv-include", "lvgl.h",  # 1.5.x derives the symbol name from the output file
            "--output", str(FONT_DIR / f"{name}.c"),
        ]
        subprocess.run(cmd, check=True)
        print(f"{name}: {len(set(symbols))} glyphs")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fonts", action="store_true", help="also regenerate font subsets")
    parser.add_argument("--otf-dir", type=Path, help="directory with NotoSerifSC-{Bold,Black}.otf")
    parser.add_argument("--lv-font-conv", default="lv_font_conv", help="lv_font_conv executable (1.5.x)")
    args = parser.parse_args()
    build_portraits()
    if args.fonts:
        if not args.otf_dir:
            parser.error("--fonts requires --otf-dir")
        build_fonts(args.otf_dir, args.lv_font_conv)


if __name__ == "__main__":
    main()
