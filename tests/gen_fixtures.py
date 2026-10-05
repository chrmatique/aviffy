#!/usr/bin/env python3
"""Generate deterministic PNG fixtures used by the aviffy test suite."""
import sys
from pathlib import Path

from PIL import Image


def gradient(w, h):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x * 255) // max(1, w - 1), (y * 255) // max(1, h - 1), (x + y) % 256)
    return img


def solid(w, h, color):
    return Image.new("RGB", (w, h), color)


def with_alpha(w, h):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    for y in range(h):
        for x in range(w):
            # Left half opaque red, right half fully transparent.
            a = 255 if x < w // 2 else 0
            px[x, y] = (255, 0, 0, a)
    return img


def orientation(w, h):
    """Blue field with a red block in the top-left quadrant."""
    img = Image.new("RGB", (w, h), (0, 0, 255))
    px = img.load()
    for y in range(h // 2):
        for x in range(3 * w // 8):
            px[x, y] = (255, 0, 0)
    return img


def main():
    outdir = Path(sys.argv[1])
    outdir.mkdir(parents=True, exist_ok=True)

    gradient(120, 80).save(outdir / "base.png")
    solid(64, 48, (10, 200, 90)).save(outdir / "solid.png")
    solid(50, 30, (200, 120, 20)).save(outdir / "odd.png")
    with_alpha(60, 40).save(outdir / "alpha.png")
    orientation(40, 20).save(outdir / "orient.png")

    print(f"wrote fixtures to {outdir}")


if __name__ == "__main__":
    main()
