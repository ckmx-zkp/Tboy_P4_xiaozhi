#!/usr/bin/env python3
"""Generate placeholder C1 eye assets (240x240 RGB565 .bin) for the AI pet board.

These are rough programmatic stand-ins so firmware development can continue
without waiting for polished GPT/Grok artwork. Replace with AI-generated art
later (same file names) — firmware does not care.

Outputs (little-endian RGB565, matching C uint16_t packing; firmware
byte-swaps on flush for the GC9A01):
  eye_master.bin  eye_happy.bin  eye_angry.bin  eye_sad.bin  eye_joy.bin
  eye_blink_30.bin  eye_blink_70.bin  eye_blink_closed.bin
PNG previews go to eye_pic/generated/ for a quick look on the PC.
"""
from __future__ import annotations

import os
import struct
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    print("Need Pillow: pip install pillow", file=sys.stderr)
    sys.exit(1)

W = H = 240
CX = CY = W // 2
R = 118          # eye circle radius; outside stays pure black
PR = 38          # default pupil radius

# Emotion palettes: (outer iris RGB, inner iris RGB, pupil radius, brightness)
EMOTIONS = {
    "master": ((26, 70, 170), (70, 205, 255), 38, 1.00),
    "happy":  ((36, 110, 175), (125, 230, 220), 38, 1.05),
    "angry":  ((130, 38, 40), (255, 115, 55), 34, 1.00),
    "sad":    ((36, 46, 105), (85, 125, 195), 34, 0.85),
    "joy":    ((45, 130, 215), (150, 248, 255), 42, 1.15),
}


def lerp(a: int, b: int, t: float) -> int:
    return int(a + (b - a) * t)


def draw_base(outer, inner, pr, brightness) -> Image.Image:
    """Circular nebula-ish iris + pupil + highlights on pure black."""
    im = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(im)
    # Radial gradient: concentric rings from outer edge to pupil edge
    steps = R - pr
    for i in range(steps):
        r = R - i
        t = i / max(1, steps - 1)
        # slight ease so the inner glow concentrates near the pupil
        tt = t * t * (3 - 2 * t)
        col = tuple(
            min(255, int(lerp(outer[c], inner[c], tt) * brightness))
            for c in range(3)
        )
        d.ellipse((CX - r, CY - r, CX + r, CY + r), fill=col)
    # Pupil: deep blue-black with a tiny bright core
    d.ellipse((CX - pr, CY - pr, CX + pr, CY + pr), fill=(8, 16, 28))
    d.ellipse((CX - 6, CY - 6, CX + 6, CY + 6), fill=(200, 240, 255))
    # Two fixed cold-white highlights, upper-left
    d.ellipse((CX - 46, CY - 52, CX - 14, CY - 20), fill=(230, 245, 255))
    d.ellipse((CX - 8, CY - 30, CX + 6, CY - 16), fill=(210, 235, 250))
    return im


def lid_top(im: Image.Image, y_edge: int) -> None:
    """Curved black upper eyelid whose lower edge passes through y_edge."""
    d = ImageDraw.Draw(im)
    r = 200
    d.ellipse((CX - r, y_edge - 2 * r, CX + r, y_edge), fill=(0, 0, 0))


def lid_bottom(im: Image.Image, y_edge: int) -> None:
    d = ImageDraw.Draw(im)
    r = 200
    d.ellipse((CX - r, y_edge, CX + r, y_edge + 2 * r), fill=(0, 0, 0))


def lid_angry(im: Image.Image) -> None:
    """Slanted lid pressed down toward the middle."""
    d = ImageDraw.Draw(im)
    d.polygon([(0, 0), (W, 0), (W, 34), (CX, 88), (0, 34)], fill=(0, 0, 0))


def circle_mask(im: Image.Image) -> Image.Image:
    """Force everything outside the eye circle to pure black."""
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).ellipse((CX - R, CY - R, CX + R, CY + R), fill=255)
    black = Image.new("RGB", (W, H), (0, 0, 0))
    return Image.composite(im, black, mask)


def to_bin(im: Image.Image, out_path: str) -> None:
    data = bytearray()
    px = im.load()
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data += struct.pack("<H", c)
    with open(out_path, "wb") as f:
        f.write(data)


def main() -> None:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    out_bin = os.path.join(root, "main/boards/waveshare/esp32-p4-ai-pet/assets")
    out_png = os.path.join(root, "eye_pic/generated")
    os.makedirs(out_bin, exist_ok=True)
    os.makedirs(out_png, exist_ok=True)

    frames: dict[str, Image.Image] = {}

    for name, (outer, inner, pr, bright) in EMOTIONS.items():
        im = draw_base(outer, inner, pr, bright)
        if name == "happy":
            lid_top(im, 40)          # gentle smile arc, ~15% covered
        elif name == "angry":
            lid_angry(im)
        elif name == "sad":
            lid_top(im, 62)          # drooped, ~25% covered
        frames[f"eye_{name}"] = circle_mask(im)

    # Blink keyframes from the neutral eye
    neutral = draw_base(*EMOTIONS["master"][:3], EMOTIONS["master"][3])
    b30 = neutral.copy(); lid_top(b30, 74)                       # ~30% closed
    frames["eye_blink_30"] = circle_mask(b30)
    b70 = neutral.copy(); lid_top(b70, 168); lid_bottom(b70, 200)  # ~70%
    frames["eye_blink_70"] = circle_mask(b70)
    bc = neutral.copy(); lid_top(bc, 196); lid_bottom(bc, 214)   # thin slit
    frames["eye_blink_closed"] = circle_mask(bc)
    # Fully closed eye (sleep) — whole circle black
    frames["eye_closed"] = Image.new("RGB", (W, H), (0, 0, 0))

    for name, im in frames.items():
        im.save(os.path.join(out_png, name + ".png"))
        to_bin(im, os.path.join(out_bin, name + ".bin"))
        print(f"{name}: png -> eye_pic/generated, bin -> assets")


if __name__ == "__main__":
    main()
