#!/usr/bin/env python3
"""把 image/ 下的 C1 右眼 PNG 转成 S3 大板 128×160 RGB565 小端 .bin。

圆屏能看到整段 GC9107 GRAM。不要把虹膜放大到 160 高（会顶边、显大），
也不要 115 贴顶（会整只眼睛偏上）。正方形虹膜缩小后贴进 128×160，并略向下。
左眼由固件水平镜像，这里只出一套右眼资产。
"""
from __future__ import annotations

import os
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
    im = square_crop(im.convert("RGB"))
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


def main() -> None:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    src_dir = os.path.join(root, "image")
    out_dir = os.path.join(root, "main", "boards", "aipet", "esp32-s3-usb-cam", "assets")
    os.makedirs(out_dir, exist_ok=True)

    for name in NAMES:
        src = os.path.join(src_dir, f"{name}.png")
        if not os.path.isfile(src):
            print(f"missing {src}", file=sys.stderr)
            sys.exit(1)
        im = to_panel(fill_eye(Image.open(src)))
        write_rgb565(im, os.path.join(out_dir, f"{name}.bin"))

    write_black(os.path.join(out_dir, "eye_closed.bin"))


if __name__ == "__main__":
    main()
