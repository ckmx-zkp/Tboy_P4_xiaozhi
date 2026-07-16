#!/usr/bin/env python3
"""Convert eye_pic source images to 240x240 RGB565 .bin for AI pet board embed.

By default auto-crops the glowing eye (non-black content) so it fills the round
GC9A01 — AI images often leave a large black ring around a small iris.

Usage:
  python scripts/convert_eye_pic.py
  python scripts/convert_eye_pic.py --src eye_pic --out main/boards/waveshare/esp32-p4-ai-pet/assets
  python scripts/convert_eye_pic.py --no-fill          # old behavior: letterbox only
  python scripts/convert_eye_pic.py --pad 0.02         # keep 2% black margin after crop
"""
from __future__ import annotations

import argparse
import os
import struct
import sys

try:
    from PIL import Image
except ImportError:
    print("Need Pillow: pip install pillow", file=sys.stderr)
    sys.exit(1)

W = H = 240


def square_crop(im: Image.Image) -> Image.Image:
    w, h = im.size
    side = min(w, h)
    left = (w - side) // 2
    top = (h - side) // 2
    return im.crop((left, top, left + side, top + side))


def content_bbox(im: Image.Image, threshold: int = 28) -> tuple[int, int, int, int] | None:
    """Axis-aligned bbox of pixels brighter than threshold (sum RGB)."""
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


def fill_eye(im: Image.Image, pad_frac: float = 0.02, threshold: int = 28) -> Image.Image:
    """
    Center-square the image, then crop to eye content and resize to WxH so the
    iris fills the round panel (only a tiny black margin remains).
    """
    im = square_crop(im.convert("RGB"))
    box = content_bbox(im, threshold=threshold)
    if box is None:
        return im.resize((W, H), Image.Resampling.LANCZOS)

    x0, y0, x1, y1 = box
    cw, ch = x1 - x0, y1 - y0
    # Square crop around content center (keeps circular eye circular)
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

    # If crop exceeds image, clamp and accept slightly off-center
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

    # Force square after clamp
    side2 = min(right - left, bottom - top)
    im = im.crop((left, top, left + side2, top + side2))
    return im.resize((W, H), Image.Resampling.LANCZOS)


def to_rgb565(
    path: str,
    out_path: str,
    *,
    fill: bool = True,
    pad_frac: float = 0.02,
    threshold: int = 28,
) -> None:
    im = Image.open(path).convert("RGB")
    if fill:
        im = fill_eye(im, pad_frac=pad_frac, threshold=threshold)
    else:
        im = square_crop(im).resize((W, H), Image.Resampling.LANCZOS)

    data = bytearray()
    for y in range(H):
        for x in range(W):
            r, g, b = im.getpixel((x, y))
            c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data += struct.pack("<H", c)
    with open(out_path, "wb") as f:
        f.write(data)

    # Quick fill report
    box = content_bbox(im, threshold=threshold)
    if box:
        x0, y0, x1, y1 = box
        pct = 100.0 * max(x1 - x0, y1 - y0) / W
    else:
        pct = 0.0
    print(f"{out_path}  {len(data)} bytes  content~{pct:.0f}% of edge  (from {os.path.basename(path)})")


def main() -> None:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    ap = argparse.ArgumentParser(description="Eye images -> 240x240 RGB565 bins (auto-fill round panel)")
    ap.add_argument("--src", default=os.path.join(root, "eye_pic"))
    ap.add_argument("--out", default=os.path.join(root, "main/boards/waveshare/esp32-p4-ai-pet/assets"))
    ap.add_argument("--no-fill", action="store_true", help="Disable auto zoom-to-eye (old letterbox look)")
    ap.add_argument("--pad", type=float, default=0.02, help="Black margin fraction after crop (default 0.02)")
    ap.add_argument("--threshold", type=int, default=28, help="RGB sum threshold for non-black (default 28)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    fill = not args.no_fill

    idle = [f"prompt-2-n-{i}.jpg" for i in range(1, 7)]
    for i, name in enumerate(idle):
        src = os.path.join(args.src, name)
        if not os.path.isfile(src):
            print(f"skip missing {src}")
            continue
        to_rgb565(
            src,
            os.path.join(args.out, f"eye_idle_{i}.bin"),
            fill=fill,
            pad_frac=args.pad,
            threshold=args.threshold,
        )

    for src_name, dst in (
        ("prompt-1-gpt.png", "anchor_gpt.bin"),
        ("prompt_1_grok.jpg", "anchor_grok.bin"),
    ):
        src = os.path.join(args.src, src_name)
        if os.path.isfile(src):
            to_rgb565(
                src,
                os.path.join(args.out, dst),
                fill=fill,
                pad_frac=args.pad,
                threshold=args.threshold,
            )


if __name__ == "__main__":
    main()
