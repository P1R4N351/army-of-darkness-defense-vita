#ifndef AOD_TEXT_RENDER_H
#define AOD_TEXT_RENDER_H

#include <stdint.h>

/* Native equivalent of com.backflipstudios.bf_ui.TextRendering (Android Canvas/StaticLayout). */

#define AOD_TEXT_OUTLINE    1
#define AOD_TEXT_SERIF      2
#define AOD_TEXT_BOLD       4
#define AOD_TEXT_ALPHA_ONLY 8

typedef struct {
	char *text;
	float line_width, line_height, min_line_height, outline_percentage, x_content_scale, y_content_scale;
	int max_lines;
	uint32_t font_color, outline_color;   /* Android ARGB */
	int alignment;                        /* 0 left, 1 center, 2 right */
	int options;
} aod_text_params;

typedef struct {
	int image_width, image_height, render_width, render_height, bits_per_pixel, render_handle;
} aod_text_prepare_result;

void aod_text_prepare(const aod_text_params *p, aod_text_prepare_result *out);
/* Returns malloc'd pixels (RGBA8888 or A8, rows bottom-up like the Java canvas flip), or NULL for a bad handle. */
uint8_t *aod_text_render(int handle, int *w, int *h, int *bytes_per_pixel);

#endif
