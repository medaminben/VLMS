#!/usr/bin/env python3
"""Generate the checkmark glyph drawn on a checked QCheckBox indicator.

Qt cannot draw one from a stylesheet -- same limitation as the combo box
arrows, see build_control_arrows.py. Without this file the checked indicator
was only a flat colour swap (unchecked box -> filled box), which reads as
"a differently coloured box" rather than "checked" at a glance.

The glyph is painted in each theme's primaryFg -- the same colour already
used for text on a primaryBg fill (e.g. the primary button label) -- so it
stays legible against the indicator's checked background in both themes.
@2x alongside each, which Qt picks up automatically on a high-DPI screen.

Run from the repo root after changing the size or a colour:

    python3 scripts/build_check_mark.py
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "applications/vlms/resources/images/ui"

# primaryFg from each palette in Theme.cpp. Kept in step by hand: there are
# two values here and a comment there, and no build step reads one from the
# other.
TINTS = {
    "light": (0xFF, 0xFF, 0xFF),
    "dark": (0x0F, 0x17, 0x2A),
}

SIDE = 16
SUPERSAMPLE = 8

# Checkmark stroke, in a 16x16 box, as fractions of the side.
POINTS = [(0.22, 0.55), (0.42, 0.75), (0.80, 0.30)]
STROKE_WIDTH_FRACTION = 0.14


def checkmark(side: int, rgb: tuple[int, int, int]) -> Image.Image:
    """One checkmark stroke, drawn large and shrunk so the joints are smooth."""
    big = Image.new("RGBA", (side * SUPERSAMPLE, side * SUPERSAMPLE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(big)
    points = [(x * big.width, y * big.height) for x, y in POINTS]
    width = max(1, round(STROKE_WIDTH_FRACTION * big.width))
    draw.line(points, fill=(*rgb, 255), width=width, joint="curve")
    radius = width / 2
    for x, y in (points[0], points[-1]):
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=(*rgb, 255))
    return big.resize((side, side), Image.LANCZOS)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for mode, rgb in TINTS.items():
        for scale, suffix in ((1, ""), (2, "@2x")):
            image = checkmark(SIDE * scale, rgb)
            path = OUT / f"check-{mode}{suffix}.png"
            image.save(path)
            print(f"wrote {path.relative_to(REPO)}")


if __name__ == "__main__":
    main()
