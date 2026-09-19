#!/usr/bin/env python3
"""把 C1 右眼 PNG 转成 S3 大板 128×160 RGB565 小端 .bin。

默认读 image/ 根目录。星座套件用 --src image/zodiac/scorpio 等，
不要直接扫 image/zodiac（会混三套）。

圆屏能看到整段 GC9107 GRAM。不要把虹膜放大到 160 高（会顶边、显大），
也不要 115 贴顶（会整只眼睛偏上）。正方形虹膜缩小后贴进 128×160，并略向下。
左眼由固件水平镜像，这里只出一套右眼资产。

新素材：主眼 RGBA 透明 + 圆外光晕；表情/眨眼多为不透明黑底。
上板前先合成黑底，再按内容框裁切。
"""
from __future__ import annotations

import argparse
import os
import shutil
import struct
import sys

try:
    from PIL import Image
except ImportError:
    print("Need Pillow: pip install pillow", file=sys.stderr)
    sys.exit(1)

W, H = 128, 160
SQUARE = 128
# 相对屏宽留一圈黑边；160 铺满会比 0.99 寸圆玻璃显大
EYE_SIZE = 108
# 相对 160 行几何中心再往下；真机仍偏上约 15%
SHIFT_Y = 20

NAMES = (
    "eye_master",
    "eye_happy",
    "eye_angry",
    "eye_sad",
    "eye_joy",
    "eye_blink_30",
    "eye_blink_70",
    "eye_blink_closed",
)


def flatten_black(im: Image.Image) -> Image.Image:
    """透明/白底主眼压到不透明黑底，去掉圆外光晕对裁切的干扰。"""
    if im.mode == "RGBA":
        bg = Image.new("RGBA", im.size, (0, 0, 0, 255))
        return Image.alpha_composite(bg, im).convert("RGB")
    rgb = im.convert("RGB")
    w, h = rgb.size
    corners = [rgb.getpixel((1, 1)), rgb.getpixel((w - 2, 1)),
               rgb.getpixel((1, h - 2)), rgb.getpixel((w - 2, h - 2))]
    if all(r + g + b > 600 for r, g, b in corners):
        px = rgb.load()
        for y in range(h):
            for x in range(w):
                r, g, b = px[x, y]
                if r > 232 and g > 232 and b > 232:
                    px[x, y] = (0, 0, 0)
    return rgb


def square_crop(im: Image.Image) -> Image.Image:
    w, h = im.size
    side = min(w, h)
    left = (w - side) // 2
    top = (h - side) // 2
    return im.crop((left, top, left + side, top + side))


def content_bbox(im: Image.Image, threshold: int = 28):
    im = im.convert("RGB")
    w, h = im.size
    px = im.load()
    minx, miny, maxx, maxy = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if r + g + b > threshold:
                if x < minx:
                    minx = x
                if y < miny:
                    miny = y
                if x > maxx:
                    maxx = x
                if y > maxy:
                    maxy = y
    if maxx < 0:
        return None
    return minx, miny, maxx + 1, maxy + 1


def fill_eye(im: Image.Image, pad_frac: float = 0.01, threshold: int = 28) -> Image.Image:
    im = square_crop(flatten_black(im))
    box = content_bbox(im, threshold=threshold)
    if box is None:
        return im.resize((SQUARE, SQUARE), Image.Resampling.LANCZOS)

    x0, y0, x1, y1 = box
    cw, ch = x1 - x0, y1 - y0
    side = max(cw, ch)
    pad = max(1, int(side * pad_frac))
    side = side + pad * 2
    cx = (x0 + x1) // 2
    cy = (y0 + y1) // 2
    half = side // 2
    left = cx - half
    top = cy - half
    right = left + side
    bottom = top + side
    iw, ih = im.size
    if left < 0:
        right -= left
        left = 0
    if top < 0:
        bottom -= top
        top = 0
    if right > iw:
        left -= right - iw
        right = iw
    if bottom > ih:
        top -= bottom - ih
        bottom = ih
    left = max(0, left)
    top = max(0, top)
    right = min(iw, right)
    bottom = min(ih, bottom)
    side2 = min(right - left, bottom - top)
    im = im.crop((left, top, left + side2, top + side2))
    return im.resize((SQUARE, SQUARE), Image.Resampling.LANCZOS)


def to_panel(im: Image.Image) -> Image.Image:
    """缩小圆眼，水平居中，垂直在 GRAM 中心基础上再下移 SHIFT_Y。"""
    if im.size != (SQUARE, SQUARE):
        im = im.resize((SQUARE, SQUARE), Image.Resampling.LANCZOS)
    eye = im.resize((EYE_SIZE, EYE_SIZE), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (W, H), (0, 0, 0))
    x = (W - EYE_SIZE) // 2
    y = (H - EYE_SIZE) // 2 + SHIFT_Y
    y = max(0, min(H - EYE_SIZE, y))
    canvas.paste(eye, (x, y))
    return canvas


def write_rgb565(im: Image.Image, out_path: str) -> None:
    data = bytearray()
    for y in range(H):
        for x in range(W):
            r, g, b = im.getpixel((x, y))
            c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data += struct.pack("<H", c)
    with open(out_path, "wb") as f:
        f.write(data)
    print(f"{os.path.basename(out_path)}  {len(data)} bytes")


def write_black(out_path: str) -> None:
    write_rgb565(Image.new("RGB", (W, H), (0, 0, 0)), out_path)


def convert_one(src_dir: str, out_dir: str, preview: bool) -> None:
    os.makedirs(out_dir, exist_ok=True)
    for name in NAMES:
        src = os.path.join(src_dir, f"{name}.png")
        if not os.path.isfile(src):
            print(f"missing {src}", file=sys.stderr)
            sys.exit(1)
        im = to_panel(fill_eye(Image.open(src)))
        write_rgb565(im, os.path.join(out_dir, f"{name}.bin"))
        if preview:
            im.save(os.path.join(out_dir, f"{name}_128x160.png"))
    write_black(os.path.join(out_dir, "eye_closed.bin"))
    if preview:
        Image.new("RGB", (W, H), (0, 0, 0)).save(os.path.join(out_dir, "eye_closed_128x160.png"))


def main() -> None:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    embed_dir = os.path.join(root, "main", "boards", "aipet", "esp32-s3-usb-cam", "assets")
    parser = argparse.ArgumentParser(description="Convert C1 eye PNG to S3 RGB565 bins")
    parser.add_argument("--src", default=os.path.join(root, "image"),
                        help="单套 PNG 目录（不要指向 image/zodiac）")
    parser.add_argument("--out", default=None, help="输出目录，默认板级 assets/")
    parser.add_argument("--preview", action="store_true", help="同时写出 128x160 PNG")
    parser.add_argument("--install", action="store_true",
                        help="把输出复制到固件 embed 目录（先备份已有 .bin）")
    args = parser.parse_args()

    src_dir = os.path.abspath(args.src)
    out_dir = os.path.abspath(args.out) if args.out else embed_dir
    print(f"src={src_dir}")
    print(f"out={out_dir}")
    convert_one(src_dir, out_dir, args.preview)

    if args.install and os.path.normpath(out_dir) != os.path.normpath(embed_dir):
        os.makedirs(embed_dir, exist_ok=True)
        backup = os.path.join(embed_dir, "legacy_backup")
        os.makedirs(backup, exist_ok=True)
        for name in list(NAMES) + ["eye_closed"]:
            src_bin = os.path.join(embed_dir, f"{name}.bin")
            if os.path.isfile(src_bin):
                shutil.copy2(src_bin, os.path.join(backup, f"{name}.bin"))
            shutil.copy2(os.path.join(out_dir, f"{name}.bin"), src_bin)
        with open(os.path.join(embed_dir, "CURRENT_SKIN.txt"), "w", encoding="utf-8") as f:
            f.write(src_dir + "\n")
        print(f"installed into {embed_dir} (backup={backup})")


if __name__ == "__main__":
    main()
