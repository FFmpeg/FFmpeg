/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "libavcodec/pixels.h"
#include "dsputil_x86.h"
#include "inline_asm.h"

#if HAVE_INLINE_ASM

#define DEF(x, y) ff_ ## x ## _ ## y ## _mmx
#define SET_RND  MOVQ_WTWO
#define PAVGBP(a, b, c, d, e, f)        PAVGBP_MMX(a, b, c, d, e, f)
#define PAVGB(a, b, c, e)               PAVGB_MMX(a, b, c, e)
#define STATIC

#include "rnd_template.c"

CALL_2X_PIXELS_EXPORT(ff_avg_pixels16_xy2_mmx, ff_avg_pixels8_xy2_mmx, 8)
CALL_2X_PIXELS_EXPORT(ff_put_pixels16_xy2_mmx, ff_put_pixels8_xy2_mmx, 8)

#endif /* HAVE_INLINE_ASM */
