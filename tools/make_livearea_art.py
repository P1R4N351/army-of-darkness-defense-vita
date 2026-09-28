#!/usr/bin/env python3
"""
Build the Vita LiveArea/bubble PNGs from the user-supplied artwork by mechanical resampling only.

No redrawing and no generated content. Every output is one uniform (aspect-preserving) Pillow
LANCZOS resample of a supplied image; nothing is ever stretched. Results are 8-bit palette PNGs
(PNG colour type 3), the encoding of the VitaSDK template assets.

  title art 02.png (1280x800 RGB) -> pic0.png 960x544, bg0.png 840x500
      Both targets are wider than 16:10. The art is scaled uniformly and surplus rows are cropped
      from the top/bottom margins only, split in proportion to the free space above the title
      (including its black outline) and below the publisher logo; those rows are measured from the
      image and never cut. Where the margins cannot absorb the whole difference (pic0), exactly the
      free rows are cropped and the remaining width is extended on both sides with the plain
      landscape purple. No stretching, no redrawing.
  icon art 05.png (1254x1254 RGBA) -> icon0.png 128x128 (fit, transparent padding; unchanged)
  logo art 06-startup-logo.png (597x288 RGBA) -> startup.png 280x158
      Fitted proportionally and alpha-composited onto an opaque deep purple (the median colour of
      the 02.png landscape), so the card is opaque; the logo pixels are only resampled.
  cover 07-submission-cover.png (1025x1535 RGB): checked and preserved for an eventual
      submission; not used in the VPK.

Usage: make_livearea_art.py ORIGINALS_DIR OUT_DIR
"""
import hashlib
import json
import os
import sys

from PIL import Image

SOURCES = {
    "02.png": "11556469fb547054a6c36e79735da83c8ef2101e7131f6e57c50f2e7cdd050a8",
    "05.png": "0a84c3c4fecdd0e56af4a23673c7a3a9929b517f64d296a99681c71a26fc2656",
    "06-startup-logo.png": "fcbce1c1f4fdbb4697549fbba9779d6135331bdaa9954d4af21d59b98a6a21f0",
    "07-submission-cover.png": "621762b2f19646719e4a78b2e03f80ed99bd6303ba39c1b43c326303e39c91c7",
}
CONTENT_GUARD = 6   # source rows kept free between a crop edge and the title or logo
TARGETS = [
    # name, source, (w, h)
    ("icon0.png", "05.png", (128, 128)),
    ("pic0.png", "02.png", (960, 544)),
    ("bg0.png", "02.png", (840, 500)),
    ("startup.png", "06-startup-logo.png", (280, 158)),
]


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def fit(src, size):
    """Uniform scale to fit inside size; returns (scaled image, placement box)."""
    sw, sh = src.size
    tw, th = size
    scale = min(tw / sw, th / sh)
    w, h = max(1, round(sw * scale)), max(1, round(sh * scale))
    scaled = src.resize((w, h), Image.Resampling.LANCZOS)
    x, y = (tw - w) // 2, (th - h) // 2
    return scaled, (x, y, w, h), scale


def content_rows(img):
    """Rows spanned by the title (red lettering, white DEFENSE) and the publisher logo in 02.png."""
    px, (W, H) = img.load(), img.size
    red = lambda p: p[0] > 170 and p[1] < 60 and p[2] < 60
    white = lambda p: p[0] > 220 and p[1] > 220 and p[2] > 220
    def span(pred, x0, x1, y0, y1):
        ys = [y for y in range(y0, y1) if any(pred(px[x, y]) for x in range(x0, x1, 2))]
        return (min(ys), max(ys)) if ys else None
    dark = lambda p: sum(p) < 60   # the title's black outline; tree branches above it lie at x >= 550
    parts = [span(red, 0, 820, 0, 400), span(dark, 40, 540, 0, 120), span(white, 480, 800, 200, 340), span(red, 950, W, 600, H),
             span(white, 1030, W, 650, H)]
    if any(p is None for p in parts):
        sys.exit("error: title/logo rows not found in 02.png")
    return min(p[0] for p in parts), max(p[1] for p in parts)


def fill_width_protected(src, size, fill):
    """Target is wider than the source. Scale to the target width and crop the surplus rows from the
    top/bottom margins, split in proportion to the free space outside the title/logo rows. If the
    margins are too small for that, crop exactly the free rows (which fixes the scale) and extend the
    remaining width on both sides with the plain fill colour. One uniform resample either way."""
    sw, sh = src.size
    tw, th = size
    top, bottom = content_rows(src)
    free_top, free_bottom = top - CONTENT_GUARD, (sh - 1 - bottom) - CONTENT_GUARD
    free = free_top + free_bottom
    need = sh - th * sw / tw                    # surplus source rows at scale tw/sw
    if need < 0 or free < 0:
        sys.exit("error: unexpected aspect or content rows")
    crop = min(need, free)
    scale = th / (sh - crop)
    w = min(tw, round(sw * scale))
    y0 = crop * free_top / free if free else 0.0
    box = (0.0, y0, float(sw), y0 + (sh - crop))
    scaled = src.resize((w, th), Image.Resampling.LANCZOS, box=box)
    out = Image.new("RGB", size, fill)
    x = (tw - w) // 2
    out.paste(scaled, (x, 0))
    return out, {"scale": round(scale, 6), "crop_box_src": [round(v, 2) for v in box], "cropped_rows_src": round(crop, 2),
                 "content_rows_src": [top, bottom], "margin_above_title_src": round(top - y0, 2),
                 "image_box_xywh": [x, 0, w, th], "side_fill_px": [x, tw - w - x], "side_fill_rgb": list(fill) if w < tw else None,
                 "margin_below_logo_src": round(box[3] - 1 - bottom, 2)}


def landscape_purple(src):
    import statistics
    px, (W, H) = src.load(), src.size
    c = [px[x, y] for x in range(W) for y in range(0, H, 4)]
    return tuple(int(statistics.median(ch)) for ch in zip(*c))


def to_palette(img):
    """8-bit palette PNG. RGBA keeps per-entry alpha (tRNS); RGB uses median cut."""
    if img.mode == "RGBA":
        return img.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG)
    return img.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)


def keep_exact_transparency(canvas, pal):
    """Pixels that are fully transparent (alpha 0) in the RGBA canvas must stay alpha 0 after
    quantisation. The octree quantiser can merge them with faint shadow pixels into an entry of
    small non-zero alpha. Only if that happened, map them to one unused palette index with
    RGBA (0,0,0,0). Returns (image, remapped pixel count); the image is unchanged when not needed."""
    zero_mask = canvas.getchannel("A").point(lambda a: 255 if a == 0 else 0)
    n_zero = zero_mask.histogram()[255]
    if n_zero == 0 or pal.palette.mode != "RGBA":
        return pal, 0
    rgba = pal.getpalette(rawmode="RGBA")
    flat = lambda im: list(im.get_flattened_data()) if hasattr(im, "get_flattened_data") else list(im.getdata())
    idx, zm = flat(pal), flat(zero_mask)
    if all(rgba[4 * i + 3] == 0 for i, m in zip(idx, zm) if m):
        return pal, 0
    n_entries = len(rgba) // 4
    used = set(idx)
    free = [i for i in range(256) if i not in used]
    if not free:
        sys.exit("error: no free palette entry for exact transparency")
    u = free[0]
    if u >= n_entries:
        rgba += [0, 0, 0, 255] * (u + 1 - n_entries)
    rgba[4 * u:4 * u + 4] = [0, 0, 0, 0]
    pal.putpalette(rgba, rawmode="RGBA")
    pal.paste(u, mask=zero_mask)
    return pal, n_zero


def main():
    src_dir, out_dir = sys.argv[1], sys.argv[2]
    for name, want in SOURCES.items():
        got = sha256(os.path.join(src_dir, name))
        if got != want:
            sys.exit(f"error: {name} sha256 {got} != expected {want}")
    os.makedirs(out_dir, exist_ok=True)
    report = {"pillow": Image.__version__ if hasattr(Image, "__version__") else None, "outputs": []}
    import PIL
    report["pillow"] = PIL.__version__
    purple = landscape_purple(Image.open(os.path.join(src_dir, "02.png")).convert("RGB"))
    report["startup_fill_rgb"] = list(purple)
    for name, src_name, size in TARGETS:
        src = Image.open(os.path.join(src_dir, src_name))
        src.load()
        has_alpha = "A" in src.getbands() or "transparency" in src.info
        if src_name == "02.png":
            canvas, info = fill_width_protected(src.convert("RGB"), size, purple)
            pal = to_palette(canvas)
            out = os.path.join(out_dir, name)
            pal.save(out, format="PNG", optimize=False)
            report["outputs"].append(dict(file=name, source=src_name, size=list(size), method="uniform scale, proportional margin crop, plain purple side fill only if the margins run out",
                                          sha256=sha256(out), **info))
            continue
        if name == "startup.png":
            scaled, box, scale = fit(src.convert("RGBA"), size)
            canvas = Image.new("RGBA", size, purple + (255,))
            canvas.alpha_composite(scaled, box[:2])
            pal = to_palette(canvas.convert("RGB"))
            out = os.path.join(out_dir, name)
            pal.save(out, format="PNG", optimize=False)
            report["outputs"].append({"file": name, "source": src_name, "size": list(size), "method": "fit, alpha-composited on opaque purple",
                                      "scale": round(scale, 6), "image_box_xywh": list(box), "fill_rgb": list(purple), "sha256": sha256(out)})
            continue
        if has_alpha:
            src = src.convert("RGBA")
            scaled, box, scale = fit(src, size)
            canvas = Image.new("RGBA", size, (0, 0, 0, 0))
            canvas.paste(scaled, box[:2])
        else:
            src = src.convert("RGB")
            scaled, box, scale = fit(src, size)
            canvas = Image.new("RGB", size, (0, 0, 0))
            canvas.paste(scaled, box[:2])
        pal = to_palette(canvas)
        exact_fix = 0
        if has_alpha:
            pal, exact_fix = keep_exact_transparency(canvas, pal)
        out = os.path.join(out_dir, name)
        pal.save(out, format="PNG", optimize=False)
        report["outputs"].append({
            "file": name, "source": src_name, "size": list(size),
            "scale": round(scale, 6), "image_box_xywh": list(box),
            "letterbox": "transparent" if has_alpha else "black",
            "exact_transparency_remapped_px": exact_fix,
            "sha256": sha256(out),
        })
    with open(os.path.join(out_dir, "artwork-report.json"), "w") as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
