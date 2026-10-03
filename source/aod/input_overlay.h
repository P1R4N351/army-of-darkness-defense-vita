/*
 * MIT License
 *
 * Copyright (c) 2026 aod-input-support contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef AOD_INPUT_OVERLAY_H
#define AOD_INPUT_OVERLAY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * aod_input_overlay_draw - Draw a visible cursor diamond overlay via VitaGL.
 *
 * Call only when the pointer is visible. Renders a 12-pixel diamond cursor
 * (solid cyan fill, dark border) using GL_TRIANGLES in an immediate-mode
 * ortho pass. All touched GL state is explicitly saved and restored.
 *
 * Parameters:
 *   width   - framebuffer width  in pixels; must be finite, in [1, 32767]
 *   height  - framebuffer height in pixels; must be finite, in [1, 32767]
 *   x       - cursor X in screen pixels; must be finite, in [0, width-1]
 *   y       - cursor Y in screen pixels; must be finite, in [0, height-1]
 *
 * Returns true on a successful draw; returns false without any GL side
 * effects if any parameter is invalid or GL_MAX_TEXTURE_UNITS is out of
 * the expected range [1, 16].
 *
 * CMake integration note (mandatory):
 *   set_source_files_properties(source/aod/input_overlay.c
 *       PROPERTIES COMPILE_FLAGS "-fno-fast-math")
 *
 * Library integration note: VitaGL void GL calls have no return value to
 * check. Correctness is library-integration-level, not hardware-proof.
 * The caller's pending glGetError state is never consumed or cleared.
 */
bool aod_input_overlay_draw(float width, float height, float x, float y);

#ifdef __cplusplus
}
#endif

#endif /* AOD_INPUT_OVERLAY_H */