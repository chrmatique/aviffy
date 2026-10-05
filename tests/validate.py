#!/usr/bin/env python3
"""Small JPEG assertions used by the aviffy test suite."""
import sys

from PIL import Image


def dims(path):
    w, h = Image.open(path).size
    print(f"{w}x{h}")


def pixel(path, x, y):
    r, g, b = Image.open(path).convert("RGB").getpixel((int(x), int(y)))
    print(f"{r} {g} {b}")


def near(path, x, y, r, g, b, tol):
    pr, pg, pb = Image.open(path).convert("RGB").getpixel((int(x), int(y)))
    ok = (
        abs(pr - int(r)) <= int(tol)
        and abs(pg - int(g)) <= int(tol)
        and abs(pb - int(b)) <= int(tol)
    )
    if not ok:
        print(f"pixel({x},{y}) = {pr},{pg},{pb}, expected ~{r},{g},{b} (+-{tol})")
    sys.exit(0 if ok else 1)


def is_jpeg(path):
    with open(path, "rb") as fh:
        head = fh.read(2)
    sys.exit(0 if head == b"\xff\xd8" else 1)


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "dims":
        dims(sys.argv[2])
    elif cmd == "pixel":
        pixel(*sys.argv[2:])
    elif cmd == "near":
        near(*sys.argv[2:])
    elif cmd == "is_jpeg":
        is_jpeg(sys.argv[2])
    else:
        sys.stderr.write(f"unknown command: {cmd}\n")
        sys.exit(2)
