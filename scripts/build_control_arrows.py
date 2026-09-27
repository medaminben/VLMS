#!/usr/bin/env python3
"""Generate the little arrows on combo boxes, spin boxes and date fields.

Qt cannot draw one from a stylesheet. The usual CSS trick -- a zero-sized box
with three coloured borders -- renders in Qt as a grey rectangle, which is
exactly what the drop-downs were showing; `image: url(...)` is the only way a
stylesheet supplies an arrow, and the moment a subcontrol like ::drop-down is
styled at all, Qt stops drawing the style's own arrow underneath.

So the arrows are files. Two tints, one per theme, because a stylesheet image
carries its own colour and cannot be recoloured by a rule; @2x alongside each,
which Qt picks up automatically on a high-DPI screen.

Run from the repo root after changing a size or a colour:

    python3 scripts/build_control_arrows.py
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "applications/vlms/resources/images/ui"

# textMuted from each palette in Theme.cpp. Kept in step by hand: there are two
# values here and a comment there, and no build step reads one from the other.
TINTS = {
    "light": (0x64, 0x74, 0x8B),
    "dark": (0x94, 0xA3, 0xB8),
}

WIDTH = 9
HEIGHT = 6
SUPERSAMPLE = 8


def triangle(width: int, height: int, rgb: tuple[int, int, int], pointing_down: bool) -> Image.Image:
    """One solid triangle, drawn large and shrunk so the diagonals are smooth."""
    big = Image.new("RGBA", (width * SUPERSAMPLE, height * SUPERSAMPLE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(big)
    w = big.width - 1
    h = big.height - 1
    points = [(0, 0), (w, 0), (w // 2, h)] if pointing_down else [(0, h), (w, h), (w // 2, 0)]
    draw.polygon(points, fill=(*rgb, 255))
    return big.resize((width, height), Image.LANCZOS)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for mode, rgb in TINTS.items():
        for name, down in (("down", True), ("up", False)):
            for scale, suffix in ((1, ""), (2, "@2x")):
                image = triangle(WIDTH * scale, HEIGHT * scale, rgb, down)
                path = OUT / f"arrow-{name}-{mode}{suffix}.png"
                image.save(path)
                print(f"wrote {path.relative_to(REPO)}")


if __name__ == "__main__":
    main()
