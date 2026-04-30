#!/usr/bin/env python3
"""
draw_placeholders.py — fallback hand-pixeled app icons.

The committed PNGs in this directory are sourced from the GNOME
HighContrast theme (LGPL-2.1+), which is GPL-2 compatible:
  /usr/share/icons/HighContrast/{16,32}x{16,32}/apps/web-browser.png
      → macbrowser-{16,32}.png
  /usr/share/icons/HighContrast/{16,32}x{16,32}/apps/preferences-system-network.png
      → bridge-{16,32}.png

This script regenerates throwaway hand-drawn pixel art if the source
PNGs are missing for some reason. Normal flow: copy a clean 1-bit
silhouette PNG over the file, run png2icn.py, rebuild.
"""
from PIL import Image, ImageDraw
from pathlib import Path

HERE = Path(__file__).resolve().parent

FG = (0, 0, 0, 255)


def _img(size: int) -> Image.Image:
    return Image.new("RGBA", (size, size), (0, 0, 0, 0))


def _set(img: Image.Image, x: int, y: int) -> None:
    if 0 <= x < img.size[0] and 0 <= y < img.size[1]:
        img.putpixel((x, y), FG)


def _rows(img: Image.Image, rows: list[str]) -> None:
    """Stamp a bitmap-art string array (one row per line, '#' = on,
    anything else = off). Caller pads each row to the canvas width."""
    for y, line in enumerate(rows):
        for x, ch in enumerate(line):
            if ch == "#":
                _set(img, x, y)


# 32x32 globe — chunky outline + crossed meridians + polar caps.
# Designed so that at 16x16 it still reads as a sphere with grid.
MACBROWSER_32 = [
    "................................",
    "............########............",
    ".........###..####..###.........",
    ".......##....##..##....##.......",
    "......#.....##....##.....#......",
    ".....#......#......#......#.....",
    "....#......#........#......#....",
    "...#.......#........#.......#...",
    "...#.......#........#.......#...",
    "..#........#........#........#..",
    "..#........#........#........#..",
    "..#........#........#........#..",
    "..#........#........#........#..",
    "..############################..",
    "..#........#........#........#..",
    "..#........#........#........#..",
    "..#........#........#........#..",
    "..#........#........#........#..",
    "..#........#........#........#..",
    "..#........#........#........#..",
    "...#.......#........#.......#...",
    "...#.......#........#.......#...",
    "....#......#........#......#....",
    ".....#......#......#......#.....",
    "......#.....##....##.....#......",
    ".......##....##..##....##.......",
    ".........###..####..###.........",
    "............########............",
    "................................",
    "................................",
    "................................",
    "................................",
]

# 16x16 globe — looser, fewer meridians so the grid still reads.
MACBROWSER_16 = [
    "................",
    "....########....",
    "..##........##..",
    ".#....#..#....#.",
    ".#....#..#....#.",
    "#.....#..#.....#",
    "#.....#..#.....#",
    "################",
    "#.....#..#.....#",
    "#.....#..#.....#",
    "#.....#..#.....#",
    ".#....#..#....#.",
    ".#....#..#....#.",
    "..##........##..",
    "....########....",
    "................",
]


# 32x32 bridge — Roman/arch-bridge silhouette: a flat deck on top
# resting on three semicircular arches, plus a water line below.
# This silhouette survives the 1-bit downscale far better than a
# suspension-cable design (whose diagonal cables alias into mush).
BRIDGE_32 = [
    "................................",
    "................................",
    "................................",
    "................................",
    "..############################..",
    "..############################..",
    "..##..####....####....####..##..",
    "..##.##..##..##..##..##..##.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "..##.#....#..#....#..#....#.##..",
    "................................",
    "..#.##..##..##..##..##..##..#...",
    "...##..##..##..##..##..##..##...",
    "................................",
    "..##..##..##..##..##..##..##....",
    "...##..##..##..##..##..##..##...",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
]


# 16x16 bridge — single arch shrunk to fit, with the wavy water line
# below. Two arches at this size collide; one bigger arch reads
# better.
BRIDGE_16 = [
    "................",
    "................",
    ".##############.",
    ".##############.",
    ".##.##....##.##.",
    ".##.#......#.##.",
    ".##.#......#.##.",
    ".##.#......#.##.",
    ".##.#......#.##.",
    ".##.#......#.##.",
    ".##.#......#.##.",
    "................",
    ".##.##.##.##.##.",
    "..##.##.##.##.##",
    "................",
    "................",
]


def from_rows(rows: list[str], size: int) -> Image.Image:
    img = _img(size)
    _rows(img, rows)
    return img


def save(img: Image.Image, name: str) -> None:
    out = HERE / name
    img.save(out, "PNG")
    print(f"  wrote {out}")


def main() -> None:
    print("MacBrowser icons:")
    save(from_rows(MACBROWSER_32, 32), "macbrowser-32.png")
    save(from_rows(MACBROWSER_16, 16), "macbrowser-16.png")
    print("BridgeAgent icons:")
    save(from_rows(BRIDGE_32, 32), "bridge-32.png")
    save(from_rows(BRIDGE_16, 16), "bridge-16.png")


if __name__ == "__main__":
    main()
