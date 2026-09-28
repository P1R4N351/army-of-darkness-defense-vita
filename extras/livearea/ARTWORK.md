# LiveArea artwork

All files are produced by `tools/make_livearea_art.py ORIGINALS_DIR OUT_DIR` from presentation artwork supplied by the port author (the originals are not part of the source tree).
- The generator checks every original's sha256 before doing anything.
- Every output is **one uniform, aspect-preserving LANCZOS resample** of a supplied image. There is no stretching, no redrawing and no generated content.
- Outputs are 8-bit palette PNGs, as in the SDK templates.
- Exact numbers are in `artwork-report.json`.

| output | size | source | method |
|---|---|---|---|
| `icon0.png` | 128×128 | `05.png` 1254×1254 RGBA | fit with transparent padding; **unchanged**, byte-identical to the approved file (sha256 `ad308a61…ea02`) |
| `pic0.png` | 960×544 | `02.png` 1280×800 RGB | scale 0.723404; top/bottom margins cropped (48 source rows, box y 33–785); image 926×544 centred, with **17 px of plain purple (45,23,68) on each side** |
| `bg0.png` | 840×500 | `02.png` | scale 0.65625; top/bottom margins cropped (38.1 source rows, box y 26.19–788.10); no fill needed |
| `startup.png` | 280×158 | `06-startup-logo.png` 597×288 RGBA | fitted (scale 0.469012, 280×135 at y 11) and alpha-composited onto **opaque** purple (45,23,68); no transparency left |

## Choices

- **Purple (45,23,68) / `#2D1744`** is the median colour of the `02.png` landscape (every pixel column, every 4th row). The same colour is used for the startup card and for the pic0 side fill.
- **Why crop and fill: aspect ratio.** 16:10 art (1.60) must become 1.765 (pic0) or 1.68 (bg0). Stretching is excluded, so either rows are removed or width is added.
- **Crop limits (measured from the image):**
  - the title, including its black outline, spans source rows 39–≈330;
  - the Backflip logo spans rows up to 778;
  - a 6-row guard is kept on both, leaving 33 free rows above the title and 15 below the logo.
- **How the crop is split:** between top and bottom in proportion to those free rows.
- **bg0** needs 38.1 rows, which fits inside the free rows: crop only.
- **pic0** would need 74.7 rows, more than the 48 free. So exactly the free rows are cropped, which sets the scale, and the remaining 34 px of width is plain purple split 17/17.
- **Result:** the full title and logo survive in both. The earlier version letterboxed with black bars (pic0 45 px per side, bg0 20 px per side).
- **startup:** the transparent logo needed an opaque background. The logo pixels are only resampled and alpha-composited, never recoloured.
- **Unchanged:** `template.xml`.

## Supplied cover (preserved, not packaged)

`artwork/originals/07-submission-cover.png`, kept for an eventual submission. It is not used in the VPK, not modified, and not copied into this repository. The generator verifies its hash on every run.

| property | value |
|---|---|
| sha256 | `621762b2f19646719e4a78b2e03f80ed99bd6303ba39c1b43c326303e39c91c7` |
| size | 1 872 719 bytes |
| pixels | 1025 × 1535 (aspect 0.6678, ≈ 2:3 portrait) |
| format | PNG, 8-bit truecolour RGB (colour type 2), no alpha |
| chunks | IHDR, sRGB (perceptual), eXIf (ColorSpace = sRGB, PixelX/YDimension = 1025/1535), IDAT ×115, IEND; no text or author chunks |

Nothing has been published or submitted.
