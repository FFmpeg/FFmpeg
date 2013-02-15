/*
 * ARM NEON optimised DSP functions
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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

#include "libavutil/attributes.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "dsputil_arm.h"

void ff_simple_idct_neon(int16_t *data);
void ff_simple_idct_put_neon(uint8_t *dest, int line_size, int16_t *data);
void ff_simple_idct_add_neon(uint8_t *dest, int line_size, int16_t *data);

void ff_clear_block_neon(int16_t *block);
void ff_clear_blocks_neon(int16_t *blocks);

void ff_put_pixels16_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels16_x2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels16_y2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels16_xy2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels8_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels8_x2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels8_y2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels8_xy2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels16_x2_no_rnd_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels16_y2_no_rnd_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels16_xy2_no_rnd_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels8_x2_no_rnd_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels8_y2_no_rnd_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_put_pixels8_xy2_no_rnd_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);

void ff_avg_pixels16_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_avg_pixels16_x2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_avg_pixels16_y2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_avg_pixels16_xy2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_avg_pixels8_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_avg_pixels8_x2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_avg_pixels8_y2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_avg_pixels8_xy2_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_avg_pixels16_x2_no_rnd_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_avg_pixels16_y2_no_rnd_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);
void ff_avg_pixels16_xy2_no_rnd_neon(uint8_t *, const uint8_t *, ptrdiff_t, int);

void ff_add_pixels_clamped_neon(const int16_t *, uint8_t *, int);
void ff_put_pixels_clamped_neon(const int16_t *, uint8_t *, int);
void ff_put_signed_pixels_clamped_neon(const int16_t *, uint8_t *, int);

void ff_vector_clipf_neon(float *dst, const float *src, float min, float max,
                          int len);
void ff_vector_clip_int32_neon(int32_t *dst, const int32_t *src, int32_t min,
                               int32_t max, unsigned int len);

int32_t ff_scalarproduct_int16_neon(const int16_t *v1, const int16_t *v2, int len);
int32_t ff_scalarproduct_and_madd_int16_neon(int16_t *v1, const int16_t *v2,
                                             const int16_t *v3, int len, int mul);

void ff_apply_window_int16_neon(int16_t *dst, const int16_t *src,
                                const int16_t *window, unsigned n);

av_cold void ff_dsputil_init_neon(DSPContext *c, AVCodecContext *avctx)
{
    const int high_bit_depth = avctx->bits_per_raw_sample > 8;

    if (!avctx->lowres && avctx->bits_per_raw_sample <= 8) {
        if (avctx->idct_algo == FF_IDCT_AUTO ||
            avctx->idct_algo == FF_IDCT_SIMPLENEON) {
            c->idct_put              = ff_simple_idct_put_neon;
            c->idct_add              = ff_simple_idct_add_neon;
            c->idct                  = ff_simple_idct_neon;
            c->idct_permutation_type = FF_PARTTRANS_IDCT_PERM;
        }
    }

    if (!high_bit_depth) {
        c->clear_block  = ff_clear_block_neon;
        c->clear_blocks = ff_clear_blocks_neon;

        c->put_pixels_tab[0][0] = ff_put_pixels16_neon;
        c->put_pixels_tab[0][1] = ff_put_pixels16_x2_neon;
        c->put_pixels_tab[0][2] = ff_put_pixels16_y2_neon;
        c->put_pixels_tab[0][3] = ff_put_pixels16_xy2_neon;
        c->put_pixels_tab[1][0] = ff_put_pixels8_neon;
        c->put_pixels_tab[1][1] = ff_put_pixels8_x2_neon;
        c->put_pixels_tab[1][2] = ff_put_pixels8_y2_neon;
        c->put_pixels_tab[1][3] = ff_put_pixels8_xy2_neon;

        c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_neon;
        c->put_no_rnd_pixels_tab[0][1] = ff_put_pixels16_x2_no_rnd_neon;
        c->put_no_rnd_pixels_tab[0][2] = ff_put_pixels16_y2_no_rnd_neon;
        c->put_no_rnd_pixels_tab[0][3] = ff_put_pixels16_xy2_no_rnd_neon;
        c->put_no_rnd_pixels_tab[1][0] = ff_put_pixels8_neon;
        c->put_no_rnd_pixels_tab[1][1] = ff_put_pixels8_x2_no_rnd_neon;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_pixels8_y2_no_rnd_neon;
        c->put_no_rnd_pixels_tab[1][3] = ff_put_pixels8_xy2_no_rnd_neon;

        c->avg_pixels_tab[0][0] = ff_avg_pixels16_neon;
        c->avg_pixels_tab[0][1] = ff_avg_pixels16_x2_neon;
        c->avg_pixels_tab[0][2] = ff_avg_pixels16_y2_neon;
        c->avg_pixels_tab[0][3] = ff_avg_pixels16_xy2_neon;
        c->avg_pixels_tab[1][0] = ff_avg_pixels8_neon;
        c->avg_pixels_tab[1][1] = ff_avg_pixels8_x2_neon;
        c->avg_pixels_tab[1][2] = ff_avg_pixels8_y2_neon;
        c->avg_pixels_tab[1][3] = ff_avg_pixels8_xy2_neon;

        c->avg_no_rnd_pixels_tab[0] = ff_avg_pixels16_neon;
        c->avg_no_rnd_pixels_tab[1] = ff_avg_pixels16_x2_no_rnd_neon;
        c->avg_no_rnd_pixels_tab[2] = ff_avg_pixels16_y2_no_rnd_neon;
        c->avg_no_rnd_pixels_tab[3] = ff_avg_pixels16_xy2_no_rnd_neon;
    }

    c->add_pixels_clamped = ff_add_pixels_clamped_neon;
    c->put_pixels_clamped = ff_put_pixels_clamped_neon;
    c->put_signed_pixels_clamped = ff_put_signed_pixels_clamped_neon;

    c->vector_clipf               = ff_vector_clipf_neon;
    c->vector_clip_int32          = ff_vector_clip_int32_neon;

    c->scalarproduct_int16 = ff_scalarproduct_int16_neon;
    c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_neon;

    c->apply_window_int16 = ff_apply_window_int16_neon;
}
