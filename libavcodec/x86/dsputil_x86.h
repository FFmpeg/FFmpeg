/*
 * MMX optimized DSP utils
 * Copyright (c) 2007  Aurelien Jacobs <aurel@gnuage.org>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_X86_DSPUTIL_X86_H
#define AVCODEC_X86_DSPUTIL_X86_H

#include <stddef.h>
#include <stdint.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"

void ff_dsputilenc_init_mmx(DSPContext *c, AVCodecContext *avctx,
                            unsigned high_bit_depth);
void ff_dsputil_init_pix_mmx(DSPContext *c, AVCodecContext *avctx);

void ff_add_pixels_clamped_mmx(const int16_t *block, uint8_t *pixels,
                               int line_size);
void ff_put_pixels_clamped_mmx(const int16_t *block, uint8_t *pixels,
                               int line_size);
void ff_put_signed_pixels_clamped_mmx(const int16_t *block, uint8_t *pixels,
                                      int line_size);

void ff_clear_block_mmx(int16_t *block);
void ff_clear_block_sse(int16_t *block);
void ff_clear_blocks_mmx(int16_t *blocks);
void ff_clear_blocks_sse(int16_t *blocks);

void ff_add_bytes_mmx(uint8_t *dst, uint8_t *src, int w);

void ff_add_hfyu_median_prediction_cmov(uint8_t *dst, const uint8_t *top,
                                        const uint8_t *diff, int w,
                                        int *left, int *left_top);

void ff_draw_edges_mmx(uint8_t *buf, int wrap, int width, int height,
                       int w, int h, int sides);

void ff_gmc_mmx(uint8_t *dst, uint8_t *src,
                int stride, int h, int ox, int oy,
                int dxx, int dxy, int dyx, int dyy,
                int shift, int r, int width, int height);

void ff_vector_clipf_sse(float *dst, const float *src,
                         float min, float max, int len);

void ff_avg_pixels8_mmx(uint8_t *block, const uint8_t *pixels,
                        ptrdiff_t line_size, int h);
void ff_avg_pixels16_mmx(uint8_t *block, const uint8_t *pixels,
                         ptrdiff_t line_size, int h);
void ff_put_pixels8_mmx(uint8_t *block, const uint8_t *pixels,
                        ptrdiff_t line_size, int h);
void ff_put_pixels16_mmx(uint8_t *block, const uint8_t *pixels,
                         ptrdiff_t line_size, int h);
void ff_avg_pixels8_mmxext(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h);
void ff_put_pixels8_mmxext(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h);
void ff_avg_pixels16_sse2(uint8_t *block, const uint8_t *pixels,
                          ptrdiff_t line_size, int h);
void ff_put_pixels16_sse2(uint8_t *block, const uint8_t *pixels,
                          ptrdiff_t line_size, int h);

void ff_avg_pixels8_x2_mmx(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h);

void ff_avg_pixels8_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h);
void ff_avg_pixels16_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);

void ff_put_pixels8_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h);
void ff_put_pixels16_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);

void ff_deinterlace_line_mmx(uint8_t *dst,
                             const uint8_t *lum_m4, const uint8_t *lum_m3,
                             const uint8_t *lum_m2, const uint8_t *lum_m1,
                             const uint8_t *lum,
                             int size);

void ff_deinterlace_line_inplace_mmx(const uint8_t *lum_m4,
                                     const uint8_t *lum_m3,
                                     const uint8_t *lum_m2,
                                     const uint8_t *lum_m1,
                                     const uint8_t *lum, int size);

#define PIXELS16(STATIC, PFX1, PFX2, TYPE, CPUEXT)                      \
STATIC void PFX1 ## _pixels16 ## TYPE ## CPUEXT(uint8_t *block,         \
                                                const uint8_t *pixels,  \
                                                ptrdiff_t line_size,    \
                                                int h)                  \
{                                                                       \
    PFX2 ## PFX1 ## _pixels8 ## TYPE ## CPUEXT(block, pixels,           \
                                               line_size, h);           \
    PFX2 ## PFX1 ## _pixels8 ## TYPE ## CPUEXT(block + 8, pixels + 8,   \
                                               line_size, h);           \
}

#endif /* AVCODEC_X86_DSPUTIL_X86_H */
