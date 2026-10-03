"""
build_input_settings_art.py — Generate static raster artwork for the
input-settings screen and its livearea button.

Font provenance: DejaVu Sans is derived from Bitstream Vera Sans.
Bitstream Vera is copyright Bitstream Inc.; the DejaVu additions are
placed in the public domain by the DejaVu authors.  Full license text is
installed at /usr/share/doc/fonts-dejavu-core/copyright on Debian/Ubuntu
systems.  No font binary is embedded here; the file is loaded at runtime
from the path supplied via --font (default /usr/share/fonts/truetype/
dejavu/DejaVuSans.ttf).

Output files (written to --output-dir):
  input-settings.png        160x80  RGBA  Controls button art
  input-settings-screen.png 960x544 RGBA  Settings panel art

The native controller overlay independently draws:
  • amber selected-row outline
  • cyan persisted-choice square at x 883-899, y (row+28)..(row+44)
  • opaque background rects over error lines when errors are inactive
Static art must NOT paint those elements.
"""

import argparse
import os
import sys
from PIL import Image, ImageDraw, ImageFont

BG = (16, 24, 32, 255)
OFFWHITE = (220, 220, 215, 255)
CYAN = (0, 220, 220, 255)
AMBER = (255, 176, 0, 255)
DIM = (140, 140, 140, 255)
RED = (220, 80, 80, 255)
BTN_BG = (40, 56, 72, 255)
BTN_BDR = (80, 120, 160, 255)

ROWS = [
    ("Rear touch (left/right)", "Rear halves control left / right.  Front touch stays available."),
    ("Shoulder buttons (L/R)",  "L / R control left / right.  Front touch stays available."),
    ("Both",                    "Rear touch and L / R.  Front touch stays available."),
]
ROW_YS = [128, 226, 324]


def load_fonts(font_path: str) -> dict:
    sizes = {"title": 24, "desc": 17, "err": 16, "btn": 24}
    fonts: dict = {}
    for key, sz in sizes.items():
        fonts[key] = ImageFont.truetype(font_path, sz)
    return fonts


def draw_text(draw: ImageDraw.ImageDraw, xy: tuple, text: str,
              font: ImageFont.FreeTypeFont, fill: tuple) -> None:
    draw.text(xy, text, font=font, fill=fill)


def build_button(font_path: str) -> Image.Image:
    img = Image.new("RGBA", (160, 80), BG)
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(font_path, 24)
    draw_text(d, (48, 34), "Controls", font, OFFWHITE)
    return img


def build_panel(font_path: str) -> Image.Image:
    img = Image.new("RGBA", (960, 544), BG)
    d = ImageDraw.Draw(img)
    f = load_fonts(font_path)

    draw_text(d, (48, 34), "Controls", f["title"], OFFWHITE)
    draw_text(d, (48, 91), "Front touch always available in all modes.", f["desc"], DIM)
    draw_text(d, (560, 76), "● = saved choice", f["desc"], CYAN)

    for i, (label, desc) in enumerate(ROWS):
        y = ROW_YS[i]
        d.rectangle([48, y, 912, y + 78], outline=BTN_BDR, width=1)
        draw_text(d, (64, y + 10), label, f["title"], OFFWHITE)
        draw_text(d, (64, y + 40), desc, f["desc"], DIM)

    for bx1, bx2, label in ((600, 748, "Save"), (764, 912, "Cancel")):
        d.rectangle([bx1, 432, bx2, 490], fill=BTN_BG, outline=BTN_BDR, width=2)
        draw_text(d, (bx1 + 16, 448), label, f["btn"], OFFWHITE)

    draw_text(d, (48, 434), "↑↓ choose   ✕ Save   ○ Cancel", f["desc"], DIM)
    draw_text(d, (48, 460), "D-pad Up/Down to select row", f["desc"], DIM)

    draw_text(d, (48, 498),
              "Could not read saved controls.  Using Shoulders.", f["err"], RED)
    draw_text(d, (48, 522),
              "Save failed.  Previous setting kept.  Retry or Cancel.", f["err"], RED)

    return img


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate input-settings artwork.")
    ap.add_argument("--output-dir", required=True, help="Directory to write PNG files into.")
    ap.add_argument("--font", default="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                    help="Path to TrueType font file.")
    args = ap.parse_args()

    if not os.path.isfile(args.font):
        print(f"ERROR: font not found: {args.font}", file=sys.stderr)
        return 1
    try:
        open(args.font, "rb").close()
    except OSError as exc:
        print(f"ERROR: cannot read font: {exc}", file=sys.stderr)
        return 1

    out = args.output_dir
    if not os.path.isdir(out):
        try:
            os.makedirs(out, exist_ok=True)
        except OSError as exc:
            print(f"ERROR: cannot create output dir: {exc}", file=sys.stderr)
            return 1

    try:
        build_button(args.font).save(os.path.join(out, "input-settings.png"))
        build_panel(args.font).save(os.path.join(out, "input-settings-screen.png"))
    except Exception as exc:  # noqa: BLE001
        print(f"ERROR: image generation failed: {exc}", file=sys.stderr)
        return 1

    print(f"Wrote input-settings.png and input-settings-screen.png to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())