#!/usr/bin/env python3
"""Generate the VLMS brand assets: the logo mark and the three wordmark banners.

VLMS stands for Virtual Library Management System.  The mark is drawn here as
vector geometry in the slate blues the application already uses in Theme.cpp,
and each banner sets "VLMS" over the full name in that banner's language.

Text is converted to outlines so the SVGs do not depend on a font being
installed -- Qt's SVG renderer in particular has no @font-face support.
"""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

import uharfbuzz as hb
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

REPO = Path(__file__).resolve().parent.parent
FONT = REPO / "applications/vlms/resources/fonts/Cairo-Variable.ttf"
OUT = REPO / "applications/vlms/resources/images/brand"

# ---------------------------------------------------------------------------
# Palette -- Tailwind slate, the same ramp Theme.cpp is built from.
# The leaning book is slate-500 on light and slate-400 on dark, which is the only
# mid-tone "blue" the theme actually owns.
# ---------------------------------------------------------------------------
LIGHT = {
    "ink": "#0f172a",      # slate-900  -- outer upright spines
    "ink_mid": "#475569",  # slate-600  -- tall middle spine
    "accent": "#64748b",   # slate-500  -- leaning book
    "text": "#0f172a",
    "sub": "#64748b",
}
DARK = {
    "ink": "#f8fafc",      # slate-50
    "ink_mid": "#cbd5e1",  # slate-300
    "accent": "#94a3b8",   # slate-400
    "text": "#f8fafc",
    "sub": "#94a3b8",
}

# ---------------------------------------------------------------------------
# The mark: a shelf of four books, the first one leaning left against the
# rest so that the pair opens with a V.  Drawn in its own 204 x 174 box, with
# every spine standing on the same baseline at y=172.
# ---------------------------------------------------------------------------
MARK_W, MARK_H = 204.0, 174.0

BASELINE = 172.0
SPINE_W = 34.0
SPINE_R = 6.0

# (x, height, tone) for the three upright spines, left to right.
UPRIGHTS = [(80.0, 122.0, "ink"), (124.0, 170.0, "ink_mid"), (168.0, 140.0, "ink")]

# The leaning book pivots on its bottom-left corner.  16 degrees is enough to
# read as a lean at 16 px and still keeps the top corner inside the box.
LEAN_X, LEAN_H, LEAN_ANGLE = 44.0, 150.0, -16.0


def mark(p: dict) -> str:
    """The logo mark as a <g>, drawn in its native box."""
    rects = [
        f'<rect x="{x:g}" y="{BASELINE - h:g}" width="{SPINE_W:g}" height="{h:g}" '
        f'rx="{SPINE_R:g}" fill="{p[tone]}"/>'
        for x, h, tone in UPRIGHTS
    ]
    rects.insert(0,
        f'<rect x="{LEAN_X:g}" y="{BASELINE - LEAN_H:g}" width="{SPINE_W:g}" '
        f'height="{LEAN_H:g}" rx="{SPINE_R:g}" fill="{p["accent"]}" '
        f'transform="rotate({LEAN_ANGLE:g} {LEAN_X:g} {BASELINE:g})"/>')
    return "<g>\n    " + "\n    ".join(rects) + "\n  </g>"


# ---------------------------------------------------------------------------
# Text -> outlines
# ---------------------------------------------------------------------------
_instances: dict[int, tuple[TTFont, hb.Face]] = {}


def _instance(weight: int) -> tuple[TTFont, hb.Face]:
    if weight not in _instances:
        static = instancer.instantiateVariableFont(
            TTFont(FONT), {"wght": weight}, inplace=False, updateFontNames=False
        )
        blob = Path(tempfile.gettempdir()) / f"vlms-cairo-{weight}.ttf"
        static.save(str(blob))
        data = Path(blob).read_bytes()
        _instances[weight] = (TTFont(str(blob)), hb.Face(data))
    return _instances[weight]


def text_path(s: str, weight: int, size: float, tracking: float = 0.0,
              rtl: bool = False) -> tuple[str, float, float, float]:
    """Shape `s` and outline it, with the baseline at y=0 and the origin at x=0.

    Returns (path data, advance width, ink top, ink bottom).  The ink bounds are
    in SVG coordinates -- y grows downward, so `top` is negative for anything
    above the baseline -- and the banner layout centres on them rather than on
    the em box, which is what keeps the all-caps Latin and the Arabic optically
    level against the mark.

    `tracking` is extra letter spacing in em units.  It is only ever applied to
    Latin -- adding it to Arabic would pull the joined letterforms apart.
    """
    font, face = _instance(weight)
    upem = font["head"].unitsPerEm
    scale = size / upem
    glyf = font.getGlyphSet()
    order = font.getGlyphOrder()

    hbfont = hb.Font(face)
    buf = hb.Buffer()
    buf.add_str(s)
    buf.direction = "rtl" if rtl else "ltr"
    buf.script = "Arab" if rtl else "Latn"
    buf.language = "ar" if rtl else "en"
    hb.shape(hbfont, buf, {"kern": True, "liga": True})

    parts: list[str] = []
    x = 0.0
    track = tracking * upem
    top, bottom = None, None
    for info, pos in zip(buf.glyph_infos, buf.glyph_positions):
        glyph = glyf[order[info.codepoint]]
        pen = SVGPathPen(glyf, ntos=lambda v: f"{v:.1f}")
        glyph.draw(pen)
        d = pen.getCommands()
        if d:
            gx = (x + pos.x_offset) * scale
            gy = -pos.y_offset * scale
            parts.append(
                f'<path transform="translate({gx:.2f} {gy:.2f}) '
                f'scale({scale:.5f} {-scale:.5f})" d="{d}"/>'
            )
            bounds = BoundsPen(glyf)
            glyph.draw(bounds)
            if bounds.bounds:
                _, y0, _, y1 = bounds.bounds
                # Font units run up, SVG units run down.
                lo, hi = gy - y1 * scale, gy - y0 * scale
                top = lo if top is None else min(top, lo)
                bottom = hi if bottom is None else max(bottom, hi)
        x += pos.x_advance + track
    if s:
        x -= track
    return "".join(parts), x * scale, top or 0.0, bottom or 0.0


# ---------------------------------------------------------------------------
# Banners
# ---------------------------------------------------------------------------
# Each row is (text, weight, size, tracking, rtl).  The first row is the
# acronym; the rows after it are the full name, set smaller and lighter.
# "VLMS" stays in Latin letters on the Arabic banner too.
BANNERS = [
    dict(
        name="banner-en",
        title=("VLMS", 800, 96, 0.08, False),
        sub=[("VIRTUAL LIBRARY", 600, 34, 0.16, False),
             ("MANAGEMENT SYSTEM", 600, 34, 0.16, False)],
        rtl=False,
    ),
    dict(
        name="banner-fr",
        title=("VLMS", 800, 96, 0.08, False),
        sub=[("SYSTÈME DE GESTION DE", 600, 34, 0.16, False),
             ("BIBLIOTHÈQUE VIRTUELLE", 600, 34, 0.16, False)],
        rtl=False,
    ),
    dict(
        name="banner-ar",
        title=("VLMS", 800, 96, 0.08, False),
        sub=[("نظام إدارة المكتبة الافتراضية", 700, 44, 0.0, True)],
        rtl=True,
    ),
]

GAP = 56.0            # space between the mark and the wordmark
PAD = 32.0            # outer padding
MARK_H_BANNER = 168.0
BANNER_H = 264.0      # shared by all three so they swap cleanly in one header


def build_banner(spec: dict, p: dict) -> str:
    scale = MARK_H_BANNER / MARK_H
    mw = MARK_W * scale

    # Stack the lines on their baselines first, then measure the ink they cover.
    rows = [spec["title"]] + spec["sub"]
    fills = [p["text"]] + [p["sub"]] * len(spec["sub"])

    blocks = []       # (path data, advance, baseline, fill)
    baseline = 0.0
    ink_top, ink_bottom = None, None
    for i, ((s, w, size, tr, rtl), fill) in enumerate(zip(rows, fills)):
        d, adv, top, bottom = text_path(s, w, size, tr, rtl)
        if i:
            # The name sits further below the acronym than its lines do apart.
            baseline += size * (1.75 if i == 1 else 1.3)
        blocks.append((d, adv, baseline, fill))
        lo, hi = baseline + top, baseline + bottom
        ink_top = lo if ink_top is None else min(ink_top, lo)
        ink_bottom = hi if ink_bottom is None else max(ink_bottom, hi)

    tw = max(b[1] for b in blocks)
    width = PAD * 2 + mw + GAP + tw
    height = BANNER_H

    mark_y = (height - MARK_H_BANNER) / 2
    # Shift the baselines so the text's ink block is centred on the canvas.
    text_dy = (height - (ink_bottom - ink_top)) / 2 - ink_top

    if spec["rtl"]:
        mark_x = width - PAD - mw
        text_anchor = width - PAD - mw - GAP     # right edge of the text block
    else:
        mark_x = PAD
        text_anchor = PAD + mw + GAP             # left edge of the text block

    body = [f'  <g transform="translate({mark_x:.2f} {mark_y:.2f}) '
            f'scale({scale:.5f})">{mark(p)}</g>']
    for d, adv, dy, fill in blocks:
        tx = text_anchor - adv if spec["rtl"] else text_anchor
        body.append(f'  <g transform="translate({tx:.2f} {text_dy + dy:.2f})" '
                    f'fill="{fill}">{d}</g>')

    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width:.0f}" '
        f'height="{height:.0f}" viewBox="0 0 {width:.2f} {height:.2f}" fill="none">\n'
        + "\n".join(body) + "\n</svg>\n"
    )


def build_logo(p: dict) -> str:
    """Square icon: the mark centred in a 256 box with a little optical padding."""
    box, inset = 256.0, 16.0
    # Fit the longer side, so a mark wider than it is tall stays in the box.
    scale = (box - inset * 2) / max(MARK_W, MARK_H)
    x = (box - MARK_W * scale) / 2
    y = (box - MARK_H * scale) / 2
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" '
        f'viewBox="0 0 256 256" fill="none">\n'
        f'  <g transform="translate({x:.2f} {y:.2f}) scale({scale:.5f})">'
        f'{mark(p)}</g>\n</svg>\n'
    )


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    written = []

    for suffix, p in (("", LIGHT), ("-dark", DARK)):
        (OUT / f"logo{suffix}.svg").write_text(build_logo(p))
        written.append(OUT / f"logo{suffix}.svg")
        for spec in BANNERS:
            f = OUT / f"{spec['name']}{suffix}.svg"
            f.write_text(build_banner(spec, p))
            written.append(f)

    # The SVGs above are the masters, but the application links only Core, Gui
    # and Widgets (see cmake/QtSupport.cmake) -- reading them would depend on
    # the qsvg image plugin being deployed, which the portable Windows stage does
    # not guarantee.  So everything Qt actually loads is also emitted as PNG.
    sizes = [16, 24, 32, 48, 64, 128, 256]
    pngs = []
    for s in sizes:
        png = OUT / f"icon-{s}.png"
        subprocess.run(
            ["rsvg-convert", "-w", str(s), "-h", str(s),
             "-o", str(png), str(OUT / "logo.svg")], check=True)
        pngs.append(str(png))
        written.append(png)

    # .ico for installer/vlms.iss and the Windows executable resource.
    ico = OUT / "vlms.ico"
    subprocess.run(["magick"] + pngs + [str(ico)], check=True)
    written.append(ico)

    # Banners at 1x and 2x of a 120 px header strip.
    for suffix in ("", "-dark"):
        for spec in BANNERS:
            for tag, h in (("", 120), ("@2x", 240)):
                png = OUT / f"{spec['name']}{suffix}{tag}.png"
                subprocess.run(
                    ["rsvg-convert", "-h", str(h), "-o", str(png),
                     str(OUT / f"{spec['name']}{suffix}.svg")], check=True)
                written.append(png)

    for f in written:
        print(f.relative_to(REPO))


if __name__ == "__main__":
    main()
