/*
 * aod-vita: FreeType replacement for com.backflipstudios.bf_ui.TextRendering.
 *
 * Reproduces the Java sizing algorithm exactly (prepareToRenderText: binary search on line
 * height, image/render sizes, handle numbering) and approximates android.text.StaticLayout for
 * drawing: greedy word wrap, left/center/right alignment, line spacing multiplier
 * (1 + outlinePercentage), end ellipsis when lines exceed maxLines, stroke-then-fill outline,
 * vertically flipped output like the Java canvas. It does not do bidi/RTL reordering or complex
 * shaping. Fonts are the TTFs bundled in the game's own assets
 * (Roboto-Regular/Roboto-Bold for sans-serif, DroidSerif-Bold for serif).
 *
 * In-game text is drawn by libgame's own FreeType; this path is only used by the platform UI
 * service, if at all. Every use is logged.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */

#include "aod/text_render.h"
#include "aod/port.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H

#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#ifndef DATA_PATH
#define DATA_PATH "ux0:data/aodd/"
#endif
#define FONT_DIR DATA_PATH "assets/assets.archondb/"

typedef struct stored {
	int handle;
	aod_text_params p;       /* text owned */
	float font_point;
	int image_width, image_height, outline_width, stride;
	struct stored *next;
} stored;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static FT_Library ft;
static FT_Face faces[3];          /* 0 sans regular, 1 sans bold, 2 serif (bold) */
static int ft_state;              /* 0 untried, 1 ok, -1 failed */
static int next_handle;
static stored *pending;

static FT_Face load_font(const char *prefix) {
	DIR *d = opendir(FONT_DIR);
	if (!d) return NULL;
	struct dirent *e;
	char path[512] = "";
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, prefix, strlen(prefix)) == 0 && strstr(e->d_name, ".ttf")) {
			snprintf(path, sizeof(path), "%s%s", FONT_DIR, e->d_name);
			break;
		}
	}
	closedir(d);
	FT_Face f = NULL;
	if (!path[0] || FT_New_Face(ft, path, 0, &f)) {
		aod_log("TextRendering: font %s* not found in %s", prefix, FONT_DIR);
		return NULL;
	}
	return f;
}

static int ensure_ft(void) {
	if (ft_state) return ft_state > 0;
	ft_state = -1;
	if (FT_Init_FreeType(&ft)) return 0;
	faces[0] = load_font("Roboto-Regular-");
	faces[1] = load_font("Roboto-Bold-");
	faces[2] = load_font("DroidSerif-Bold-");
	if (!faces[0]) faces[0] = faces[1];
	if (!faces[1]) faces[1] = faces[0];
	if (!faces[2]) faces[2] = faces[1];
	if (!faces[1]) return 0;
	ft_state = 1;
	return 1;
}

static FT_Face typeface(int serif, int bold) { return serif ? faces[2] : faces[bold ? 1 : 0]; }

/* Paint.getFontMetrics() at text size 1: -ascent + descent + leading, in em units. */
static float em_height(FT_Face f) {
	return (float)(f->ascender - f->descender) / (float)f->units_per_EM;
}

static uint32_t next_cp(const char **s) {
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t c = *p++;
	int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	if (extra) c &= 0x3F >> extra;
	while (extra-- && (*p & 0xC0) == 0x80) c = (c << 6) | (*p++ & 0x3F);
	*s = (const char *)p;
	return c;
}

static float advance(FT_Face f, uint32_t cp) {
	if (FT_Load_Char(f, cp, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP)) return 0;
	return f->glyph->advance.x / 64.0f;
}

static float measure(FT_Face f, const char *s, const char *end) {
	float w = 0;
	while (s < end && *s) w += advance(f, next_cp(&s));
	return w;
}

typedef struct { const char *start, *end; } line_t;

/* Greedy word wrap like StaticLayout. Returns the line count; fills up to max_out lines. */
static int wrap(FT_Face f, const char *text, float width, line_t *out, int max_out) {
	int n = 0;
	const char *p = text;
	while (*p) {
		const char *line = p, *last_break = NULL, *q = p;
		float w = 0;
		while (*q && *q != '\n') {
			const char *prev = q;
			uint32_t cp = next_cp(&q);
			float a = advance(f, cp);
			if (cp == ' ') last_break = q;
			if (w + a > width && prev > line && cp != ' ') {
				q = last_break ? last_break : prev;
				break;
			}
			w += a;
		}
		const char *end = q;
		while (end > line && end[-1] == ' ') end--;
		if (n < max_out) { out[n].start = line; out[n].end = end; }
		n++;
		p = q;
		if (*p == '\n') p++;
		else while (*p == ' ') p++;
		if (!*p && q > text && q[-1] == '\n') break;
	}
	return n ? n : 1;
}

static int line_count(FT_Face f, float point, const aod_text_params *p, int image_width) {
	FT_Set_Char_Size(f, 0, (FT_F26Dot6)(point * 64.0f), 72, 72);
	return wrap(f, p->text, (float)image_width, NULL, 0);
}

void aod_text_prepare(const aod_text_params *p, aod_text_prepare_result *out) {
	memset(out, 0, sizeof(*out));
	pthread_mutex_lock(&lock);
	if (!ensure_ft()) {
		aod_log("TextRendering: FreeType/fonts unavailable; prepare returns empty result");
		pthread_mutex_unlock(&lock);
		return;
	}
	FT_Face f = typeface(p->options & AOD_TEXT_SERIF, p->options & AOD_TEXT_BOLD);
	int image_width = (int)ceilf(p->line_width * p->x_content_scale);
	float top = p->line_height * p->y_content_scale;
	float bottom = p->min_line_height * p->y_content_scale;
	float font_point = 0;
	int lines = 0, ok = 0;

	if (top - bottom < 1.0 || p->min_line_height <= 0.0f) {
		float adj = top - 2.0f * top * p->outline_percentage;
		font_point = adj / em_height(f);
		if (font_point > 0) {
			lines = line_count(f, font_point, p, image_width);
			if (lines > p->max_lines) lines = p->max_lines;
			ok = 1;
		} else {
			aod_log("TextRendering: calculated font point is invalid. Aborting preparation for font rendering.");
		}
	} else {
		ok = 1;
		while (top - bottom >= 1.0) {
			float lh = (top - bottom) / 2.0f + bottom;
			float adj = lh - 2.0f * lh * p->outline_percentage;
			float fp = adj / em_height(f);
			if (fp <= 0) break;
			font_point = fp;
			lines = line_count(f, fp, p, image_width);
			if (lines <= p->max_lines) bottom = lh;
			else { lines = p->max_lines; top = lh; }
		}
	}
	if (ok) {
		stored *s = calloc(1, sizeof(*s));
		if (s) {
			s->p = *p;
			s->p.text = strdup(p->text ? p->text : "");
			s->font_point = font_point;
			s->image_width = image_width;
			s->image_height = (int)ceilf(lines * bottom);
			s->outline_width = (int)ceilf(p->outline_percentage * bottom);
			s->stride = (p->options & AOD_TEXT_ALPHA_ONLY) ? 1 : 4;
			s->handle = next_handle++;
			s->next = pending;
			pending = s;
			out->image_width = s->image_width;
			out->image_height = s->image_height;
			out->render_width = (int)ceilf(p->line_width);
			out->render_height = (int)ceilf(s->image_height / p->y_content_scale);
			out->bits_per_pixel = s->stride * 8;
			out->render_handle = s->handle;
		}
	}
	aod_log("TextRendering.prepare \"%.40s\" -> %dx%d handle %d", p->text ? p->text : "", out->image_width,
	        out->image_height, out->render_handle);
	pthread_mutex_unlock(&lock);
}

/* Rasterise one glyph's coverage (fill or stroke) into cov[w*h] at pen position. */
static void blit(FT_Face f, uint32_t cp, float pen_x, float baseline, FT_Stroker stroker, uint8_t *cov, int w, int h) {
	if (FT_Load_Char(f, cp, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP)) return;
	FT_Glyph g;
	if (FT_Get_Glyph(f->glyph, &g)) return;
	if (stroker && g->format == FT_GLYPH_FORMAT_OUTLINE) FT_Glyph_Stroke(&g, stroker, 1);
	FT_Vector origin = { (FT_Pos)((pen_x - floorf(pen_x)) * 64.0f), 0 };
	if (FT_Glyph_To_Bitmap(&g, FT_RENDER_MODE_NORMAL, &origin, 1) == 0) {
		FT_BitmapGlyph bg = (FT_BitmapGlyph)g;
		int x0 = (int)floorf(pen_x) + bg->left, y0 = (int)roundf(baseline) - bg->top;
		for (unsigned yy = 0; yy < bg->bitmap.rows; yy++) {
			int y = y0 + (int)yy;
			if (y < 0 || y >= h) continue;
			for (unsigned xx = 0; xx < bg->bitmap.width; xx++) {
				int x = x0 + (int)xx;
				if (x < 0 || x >= w) continue;
				uint8_t v = bg->bitmap.buffer[yy * bg->bitmap.pitch + xx];
				if (v > cov[y * w + x]) cov[y * w + x] = v;
			}
		}
	}
	FT_Done_Glyph(g);
}

static void draw_lines(FT_Face f, const stored *s, const line_t *lines, int n, float line_h, float ascent,
                       FT_Stroker stroker, uint8_t *cov) {
	int w = s->image_width, h = s->image_height;
	for (int i = 0; i < n; i++) {
		float lw = measure(f, lines[i].start, lines[i].end);
		float x = 0;
		if (s->p.alignment == 1) x = (w - lw) / 2.0f;
		else if (s->p.alignment == 2) x = w - lw;
		x += s->outline_width;
		float baseline = i * line_h + ascent;
		const char *c = lines[i].start;
		while (c < lines[i].end && *c) {
			uint32_t cp = next_cp(&c);
			blit(f, cp, x, baseline, stroker, cov, w, h);
			x += advance(f, cp);
		}
	}
}

uint8_t *aod_text_render(int handle, int *out_w, int *out_h, int *out_bpp) {
	pthread_mutex_lock(&lock);
	stored **pp = &pending, *s = NULL;
	while (*pp) { if ((*pp)->handle == handle) { s = *pp; *pp = s->next; break; } pp = &(*pp)->next; }
	if (!s) {
		aod_log("TextRendering: invlid render handle.  Aborting renderText.");
		pthread_mutex_unlock(&lock);
		return NULL;
	}
	int w = s->image_width, h = s->image_height, stride = s->stride;
	uint8_t *px = calloc((size_t)w * h * stride + 1, 1);
	if (!px || !ensure_ft() || w <= 0 || h <= 0) goto done;

	FT_Face f = typeface(0, 1);   /* Java renderText() always uses sans-serif bold */
	FT_Set_Char_Size(f, 0, (FT_F26Dot6)(s->font_point * 64.0f), 72, 72);
	float ascent = f->size->metrics.ascender / 64.0f;
	float line_h = (f->size->metrics.ascender - f->size->metrics.descender) / 64.0f * (1.0f + s->p.outline_percentage);

	enum { MAXL = 64 };
	line_t lines[MAXL];
	int n = wrap(f, s->p.text, (float)w, lines, MAXL);
	if (n > MAXL) n = MAXL;
	char *ell = NULL;
	if (n > s->p.max_lines && s->p.max_lines > 0) {
		/* Ellipsize the last visible line at width - 2*outline (TextUtils.ellipsize END). */
		int last = s->p.max_lines - 1;
		float avail = w - 2.0f * s->outline_width - advance(f, 0x2026);
		const char *st = lines[last].start, *c = st, *cut = st;
		float acc = 0;
		while (*c) {
			const char *prev = c;
			float a = advance(f, next_cp(&c));
			if (acc + a > avail) break;
			acc += a;
			cut = c;
			(void)prev;
		}
		size_t len = (size_t)(cut - st);
		ell = malloc(len + 4);
		if (ell) {
			memcpy(ell, st, len);
			memcpy(ell + len, "\xE2\x80\xA6", 4);
			lines[last].start = ell;
			lines[last].end = ell + len + 3;
		}
		n = s->p.max_lines;
	}

	uint8_t *fill = calloc((size_t)w * h, 1), *stroke = calloc((size_t)w * h, 1);
	if (fill && stroke) {
		if (s->outline_width > 0) {
			FT_Stroker st;
			if (FT_Stroker_New(ft, &st) == 0) {
				/* Paint.setStrokeWidth(outlineWidth * 2): radius = outlineWidth */
				FT_Stroker_Set(st, (FT_Fixed)(s->outline_width * 64), FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
				draw_lines(f, s, lines, n, line_h, ascent, st, stroke);
				FT_Stroker_Done(st);
			}
		}
		draw_lines(f, s, lines, n, line_h, ascent, NULL, fill);
		uint32_t fc = s->p.font_color, oc = s->p.outline_color;
		float fa = ((fc >> 24) & 0xFF) / 255.0f, oa = ((oc >> 24) & 0xFF) / 255.0f;
		for (int y = 0; y < h; y++) {
			int dst_row = h - 1 - y;   /* canvas.scale(1,-1): output is bottom-up */
			for (int x = 0; x < w; x++) {
				float a1 = fill[y * w + x] / 255.0f * fa, a0 = stroke[y * w + x] / 255.0f * oa;
				float a = a1 + a0 * (1.0f - a1);
				uint8_t *o = px + ((size_t)dst_row * w + x) * stride;
				if (stride == 1) { o[0] = (uint8_t)(a * 255.0f + 0.5f); continue; }
				if (a <= 0) continue;
				for (int k = 0; k < 3; k++) {
					float c1 = ((fc >> (16 - 8 * k)) & 0xFF), c0 = ((oc >> (16 - 8 * k)) & 0xFF);
					o[k] = (uint8_t)((c1 * a1 + c0 * a0 * (1.0f - a1)) / a + 0.5f);
				}
				o[3] = (uint8_t)(a * 255.0f + 0.5f);
			}
		}
	}
	free(fill);
	free(stroke);
	free(ell);
done:
	*out_w = w; *out_h = h; *out_bpp = stride;
	free(s->p.text);
	free(s);
	pthread_mutex_unlock(&lock);
	return px;
}
