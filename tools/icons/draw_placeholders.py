#!/usr/bin/env python3
"""
draw_placeholders.py — first-pass app icons for MacBrowser + BridgeAgent.

Run this once to produce 32×32 + 16×16 PNGs in this directory. The
build system feeds them into png2icn.py to emit the ICN#/ics# Rez
blocks each app's .r file embeds.

These are placeholder pixel-art icons drawn directly with PIL — easy
to iterate on without depending on rsvg-convert. Replace the call
sites with proper SVG-derived art (Lucide / Phosphor) later; the
ImageDraw API stays the same downstream.
"""
from PIL import Image, ImageDraw
from pathlib import Path

HERE = Path(__file__).resolve().parent

# 1-bit canvas ergonomics: black foreground on transparent background.
FG = (0, 0, 0, 255)


def make_canvas(size: int) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def macbrowser_icon(size: int) -> Image.Image:
    """Globe with crosshair + horizontal latitudes — small enough to
    survive the 32×32 → 16×16 downscale without becoming mush."""
    img, d = make_canvas(size)
    # Outer circle, inset 2px from edge, 1-px stroke
    inset = max(1, size // 16)
    d.ellipse((inset, inset, size - inset - 1, size - inset - 1),
              outline=FG, width=1)
    # Equator
    cy = size // 2
    d.line((inset + 1, cy, size - inset - 2, cy), fill=FG, width=1)
    # Vertical meridian — short arc-ish: just a vertical line at center
    cx = size // 2
    d.line((cx, inset + 1, cx, size - inset - 2), fill=FG, width=1)
    # One curved line above + below for a hint of latitude
    if size >= 24:
        # Top latitude band
        d.line((inset + 4, cy - size // 4 + 1,
                size - inset - 5, cy - size // 4 + 1),
               fill=FG, width=1)
        # Bottom latitude band
        d.line((inset + 4, cy + size // 4 - 1,
                size - inset - 5, cy + size // 4 - 1),
               fill=FG, width=1)
    return img


def bridge_icon(size: int) -> Image.Image:
    """Two squares connected by a horizontal line — host ↔ guest
    bridge metaphor. Plus a small dot in the middle to suggest a
    packet in flight. Period-correct simple."""
    img, d = make_canvas(size)
    box = max(4, size // 4)
    pad = max(2, size // 16)
    cy = size // 2
    # Left box
    d.rectangle((pad, cy - box // 2, pad + box, cy + box // 2 - 1),
                outline=FG, width=1)
    # Right box
    d.rectangle((size - pad - box - 1, cy - box // 2,
                 size - pad - 1, cy + box // 2 - 1),
                outline=FG, width=1)
    # Connecting wire
    d.line((pad + box, cy, size - pad - box - 1, cy), fill=FG, width=1)
    # Mid-point packet dot
    if size >= 16:
        cx = size // 2
        d.rectangle((cx - 1, cy - 1, cx, cy), fill=FG)
    # Tiny "screen" detail inside each box for size>=24
    if size >= 24:
        for x_box_left in (pad + 2, size - pad - box + 1):
            d.rectangle((x_box_left, cy - box // 2 + 2,
                         x_box_left + box - 5, cy - box // 2 + 3),
                        fill=FG)
    return img


def save(img: Image.Image, name: str) -> None:
    out = HERE / name
    img.save(out, "PNG")
    print(f"  wrote {out}")


def main() -> None:
    print("MacBrowser icons:")
    save(macbrowser_icon(32), "macbrowser-32.png")
    save(macbrowser_icon(16), "macbrowser-16.png")
    print("BridgeAgent icons:")
    save(bridge_icon(32), "bridge-32.png")
    save(bridge_icon(16), "bridge-16.png")


if __name__ == "__main__":
    main()
