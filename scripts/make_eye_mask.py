#!/usr/bin/env python3
"""Generate eyelid masks and composite previews for AI pet eyes (no PS needed).

Mask convention (L mode):
  255 = show iris (open)
  0   = eyelid / blocked

Examples:
  python scripts/make_eye_mask.py
  python scripts/make_eye_mask.py --master eye_pic/neutral_master_gpt.png --preset happy
  python scripts/make_eye_mask.py --preset weary --eye-h 72
"""
from __future__ import annotations

import argparse
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:
    print("Need Pillow: pip install pillow", file=sys.stderr)
    sys.exit(1)

W = H = 240

# name -> (eye_w, eye_h, cy_offset, blur)
PRESETS = {
    "happy": (200, 92, 6, 2.5),    # squint smile-ish aperture
    "weary": (210, 70, 2, 2.0),    # flat half-lid blank/helpless
    "alert": (195, 150, 0, 2.0),   # large almond-ish open
    "neutral_open": (210, 200, 0, 1.5),  # almost full open (for testing)
}


def square_resize(im: Image.Image, size: int = W) -> Image.Image:
    im = im.convert("RGB")
    w, h = im.size
    side = min(w, h)
    left = (w - side) // 2
    top = (h - side) // 2
    return im.crop((left, top, left + side, top + side)).resize((size, size), Image.Resampling.LANCZOS)


def content_fill(im: Image.Image, pad_frac: float = 0.02, threshold: int = 28) -> Image.Image:
    """Zoom non-black content to fill canvas (same idea as convert_eye_pic)."""
    im = square_resize(im, max(im.size))
    w, h = im.size
    px = im.load()
    minx, miny, maxx, maxy = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if r + g + b > threshold:
                minx = min(minx, x)
                miny = min(miny, y)
                maxx = max(maxx, x)
                maxy = max(maxy, y)
    if maxx < 0:
        return im.resize((W, H), Image.Resampling.LANCZOS)
    cw, ch = maxx - minx + 1, maxy - miny + 1
    side = max(cw, ch)
    pad = max(1, int(side * pad_frac))
    side = side + pad * 2
    cx = (minx + maxx) // 2
    cy = (miny + maxy) // 2
    half = side // 2
    left = max(0, cx - half)
    top = max(0, cy - half)
    right = min(w, left + side)
    bottom = min(h, top + side)
    side2 = min(right - left, bottom - top)
    im = im.crop((left, top, left + side2, top + side2))
    return im.resize((W, H), Image.Resampling.LANCZOS)


def make_aperture_mask(eye_w: int, eye_h: int, cy_offset: int, blur: float) -> Image.Image:
    """White ellipse = open eye; black = lid."""
    mask = Image.new("L", (W, H), 0)
    draw = ImageDraw.Draw(mask)
    cx, cy = W // 2, H // 2 + cy_offset
    x0 = cx - eye_w // 2
    y0 = cy - eye_h // 2
    x1 = cx + eye_w // 2
    y1 = cy + eye_h // 2
    draw.ellipse([x0, y0, x1, y1], fill=255)
    # For squint presets, slightly widen a second shallower ellipse (union) for softer lids
    if eye_h < 120:
        m2 = Image.new("L", (W, H), 0)
        d2 = ImageDraw.Draw(m2)
        d2.ellipse([x0 - 6, y0 + max(4, eye_h // 10), x1 + 6, y1 - max(2, eye_h // 12)], fill=255)
        a, b = mask.load(), m2.load()
        for y in range(H):
            for x in range(W):
                if b[x, y] > a[x, y]:
                    a[x, y] = b[x, y]
    if blur > 0:
        mask = mask.filter(ImageFilter.GaussianBlur(radius=blur))
    return mask


def apply_mask(master: Image.Image, mask: Image.Image) -> Image.Image:
    master = master.convert("RGB")
    out = Image.new("RGB", (W, H), (0, 0, 0))
    mp, op, msk = master.load(), out.load(), mask.load()
    for y in range(H):
        for x in range(W):
            a = msk[x, y] / 255.0
            r, g, b = mp[x, y]
            op[x, y] = (int(r * a), int(g * a), int(b * a))
    return out


def to_rgb565(im: Image.Image, path: str) -> None:
    import struct

    im = im.convert("RGB")
    data = bytearray()
    for y in range(H):
        for x in range(W):
            r, g, b = im.getpixel((x, y))
            c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data += struct.pack("<H", c)
    with open(path, "wb") as f:
        f.write(data)


def main() -> None:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    ap = argparse.ArgumentParser(description="Pet eye lid mask + composite")
    ap.add_argument(
        "--master",
        action="append",
        default=None,
        help="Master image path (can repeat). Default: neutral masters under eye_pic/",
    )
    ap.add_argument(
        "--preset",
        choices=list(PRESETS.keys()) + ["all"],
        default="all",
        help="Mask shape preset (default all)",
    )
    ap.add_argument("--eye-w", type=int, default=None)
    ap.add_argument("--eye-h", type=int, default=None)
    ap.add_argument("--cy", type=int, default=None, help="Vertical offset of aperture center")
    ap.add_argument("--blur", type=float, default=None)
    ap.add_argument(
        "--out",
        default=os.path.join(root, "eye_pic", "masks"),
        help="Output directory",
    )
    ap.add_argument("--no-fill", action="store_true", help="Do not auto-zoom master content")
    ap.add_argument("--bin", action="store_true", help="Also write RGB565 .bin for composites")
    args = ap.parse_args()

    masters = args.master
    if not masters:
        candidates = [
            os.path.join(root, "eye_pic", "neutral_master_gpt.png"),
            os.path.join(root, "eye_pic", "neutral_master_grok.jpg"),
            os.path.join(root, "eye_pic", "prompt-1-gpt.png"),
            os.path.join(root, "eye_pic", "prompt_1_grok.jpg"),
        ]
        masters = [p for p in candidates if os.path.isfile(p)]
    if not masters:
        print("No master image found. Pass --master path", file=sys.stderr)
        sys.exit(1)

    presets = list(PRESETS.keys()) if args.preset == "all" else [args.preset]
    os.makedirs(args.out, exist_ok=True)

    for preset in presets:
        eye_w, eye_h, cy_off, blur = PRESETS[preset]
        if args.eye_w is not None:
            eye_w = args.eye_w
        if args.eye_h is not None:
            eye_h = args.eye_h
        if args.cy is not None:
            cy_off = args.cy
        if args.blur is not None:
            blur = args.blur

        mask = make_aperture_mask(eye_w, eye_h, cy_off, blur)
        mask_path = os.path.join(args.out, f"mask_{preset}_L.png")
        mask.save(mask_path)
        # gray preview of mask alone
        mask.convert("RGB").save(os.path.join(args.out, f"mask_{preset}_preview.png"))
        print(f"mask  {mask_path}  aperture={eye_w}x{eye_h} cy+{cy_off} blur={blur}")

        for mp in masters:
            name = os.path.splitext(os.path.basename(mp))[0]
            raw = Image.open(mp)
            base = square_resize(raw) if args.no_fill else content_fill(raw)
            base_path = os.path.join(args.out, f"base240_{name}.png")
            if not os.path.isfile(base_path):
                base.save(base_path)

            comp = apply_mask(base, mask)
            out_png = os.path.join(args.out, f"eye_{preset}_{name}.png")
            comp.save(out_png)
            print(f"comp  {out_png}")

            if args.bin:
                out_bin = os.path.join(args.out, f"eye_{preset}_{name}.bin")
                to_rgb565(comp, out_bin)
                print(f"bin   {out_bin}")

    print(f"Done. Outputs in: {args.out}")


if __name__ == "__main__":
    main()
