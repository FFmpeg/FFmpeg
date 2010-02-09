/*
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
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

#include <stdint.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "dsputil_arm.h"

void ff_simple_idct_armv6(DCTELEM *data);
void ff_simple_idct_put_armv6(uint8_t *dest, int line_size, DCTELEM *data);
void ff_simple_idct_add_armv6(uint8_t *dest, int line_size, DCTELEM *data);

void ff_put_pixels16_armv6(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_x2_armv6(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_y2_armv6(uint8_t *, const uint8_t *, int, int);

void ff_put_pixels16_x2_no_rnd_armv6(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_y2_no_rnd_armv6(uint8_t *, const uint8_t *, int, int);

void ff_avg_pixels16_armv6(uint8_t *, const uint8_t *, int, int);

void ff_put_pixels8_armv6(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_x2_armv6(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_y2_armv6(uint8_t *, const uint8_t *, int, int);

void ff_put_pixels8_x2_no_rnd_armv6(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_y2_no_rnd_armv6(uint8_t *, const uint8_t *, int, int);

void ff_avg_pixels8_armv6(uint8_t *, const uint8_t *, int, int);

void ff_add_pixels_clamped_armv6(const DCTELEM *block,
                                 uint8_t *restrict pixels,
                                 int line_size);

void ff_get_pixels_armv6(DCTELEM *block, const uint8_t *pixels, int stride);
void ff_diff_pixels_armv6(DCTELEM *block, const uint8_t *s1,
                          const uint8_t *s2, int stride);

int ff_pix_abs16_armv6(void *s, uint8_t *blk1, uint8_t *blk2,
                       int line_size, int h);
int ff_pix_abs16_x2_armv6(void *s, uint8_t *blk1, uint8_t *blk2,
                          int line_size, int h);
int ff_pix_abs16_y2_armv6(void *s, uint8_t *blk1, uint8_t *blk2,
                          int line_size, int h);

int ff_pix_abs8_armv6(void *s, uint8_t *blk1, uint8_t *blk2,
                       int line_size, int h);

int ff_sse16_armv6(void *s, uint8_t *blk1, uint8_t *blk2,
                   int line_size, int h);

int ff_pix_norm1_armv6(uint8_t *pix, int line_size);
int ff_pix_sum_armv6(uint8_t *pix, int line_size);

void av_cold ff_dsputil_init_armv6(DSPContext* c, AVCodecContext *avctx)
{
    if (!avctx->lowres && (avctx->idct_algo == FF_IDCT_AUTO ||
                           avctx->idct_algo == FF_IDCT_SIMPLEARMV6)) {
        c->idct_put              = ff_simple_idct_put_armv6;
        c->idct_add              = ff_simple_idct_add_armv6;
        c->idct                  = ff_simple_idct_armv6;
        c->idct_permutation_type = FF_LIBMPEG2_IDCT_PERM;
    }

    c->put_pixels_tab[0][0] = ff_put_pixels16_armv6;
    c->put_pixels_tab[0][1] = ff_put_pixels16_x2_armv6;
    c->put_pixels_tab[0][2] = ff_put_pixels16_y2_armv6;
/*     c->put_pixels_tab[0][3] = ff_put_pixels16_xy2_armv6; */
    c->put_pixels_tab[1][0] = ff_put_pixels8_armv6;
    c->put_pixels_tab[1][1] = ff_put_pixels8_x2_armv6;
    c->put_pixels_tab[1][2] = ff_put_pixels8_y2_armv6;
/*     c->put_pixels_tab[1][3] = ff_put_pixels8_xy2_armv6; */

    c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_armv6;
    c->put_no_rnd_pixels_tab[0][1] = ff_put_pixels16_x2_no_rnd_armv6;
    c->put_no_rnd_pixels_tab[0][2] = ff_put_pixels16_y2_no_rnd_armv6;
/*     c->put_no_rnd_pixels_tab[0][3] = ff_put_pixels16_xy2_no_rnd_armv6; */
    c->put_no_rnd_pixels_tab[1][0] = ff_put_pixels8_armv6;
    c->put_no_rnd_pixels_tab[1][1] = ff_put_pixels8_x2_no_rnd_armv6;
    c->put_no_rnd_pixels_tab[1][2] = ff_put_pixels8_y2_no_rnd_armv6;
/*     c->put_no_rnd_pixels_tab[1][3] = ff_put_pixels8_xy2_no_rnd_armv6; */

    c->avg_pixels_tab[0][0] = ff_avg_pixels16_armv6;
    c->avg_pixels_tab[1][0] = ff_avg_pixels8_armv6;

    c->add_pixels_clamped = ff_add_pixels_clamped_armv6;
    c->get_pixels = ff_get_pixels_armv6;
    c->diff_pixels = ff_diff_pixels_armv6;

    c->pix_abs[0][0] = ff_pix_abs16_armv6;
    c->pix_abs[0][1] = ff_pix_abs16_x2_armv6;
    c->pix_abs[0][2] = ff_pix_abs16_y2_armv6;

    c->pix_abs[1][0] = ff_pix_abs8_armv6;

    c->sad[0] = ff_pix_abs16_armv6;
    c->sad[1] = ff_pix_abs8_armv6;

    c->sse[0] = ff_sse16_armv6;

    c->pix_norm1 = ff_pix_norm1_armv6;
    c->pix_sum   = ff_pix_sum_armv6;
}
