/*
 * MMX optimized DSP utils
 * Copyright (c) 2007  Aurelien Jacobs <aurel@gnuage.org>
 *
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

#ifndef AVCODEC_X86_DSPUTIL_X86_H
#define AVCODEC_X86_DSPUTIL_X86_H

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

void ff_gmc_sse(uint8_t *dst, uint8_t *src,
                int stride, int h, int ox, int oy,
                int dxx, int dxy, int dyx, int dyy,
                int shift, int r, int width, int height);

void ff_vector_clipf_sse(float *dst, const float *src,
                         float min, float max, int len);


void ff_mmx_idct(int16_t *block);
void ff_mmxext_idct(int16_t *block);

#endif /* AVCODEC_X86_DSPUTIL_X86_H */
