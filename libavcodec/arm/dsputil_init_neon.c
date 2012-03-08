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

#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "dsputil_arm.h"

void ff_simple_idct_neon(DCTELEM *data);
void ff_simple_idct_put_neon(uint8_t *dest, int line_size, DCTELEM *data);
void ff_simple_idct_add_neon(uint8_t *dest, int line_size, DCTELEM *data);

void ff_vp3_idct_neon(DCTELEM *data);
void ff_vp3_idct_put_neon(uint8_t *dest, int line_size, DCTELEM *data);
void ff_vp3_idct_add_neon(uint8_t *dest, int line_size, DCTELEM *data);
void ff_vp3_idct_dc_add_neon(uint8_t *dest, int line_size, const DCTELEM *data);

void ff_clear_block_neon(DCTELEM *block);
void ff_clear_blocks_neon(DCTELEM *blocks);

void ff_put_pixels16_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_x2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_y2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_xy2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_x2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_y2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_xy2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_x2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_y2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_xy2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_x2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_y2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_xy2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);

void ff_avg_pixels16_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels16_x2_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels16_y2_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels16_xy2_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels8_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels8_x2_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels8_y2_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels8_xy2_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels16_x2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels16_y2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels16_xy2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels8_x2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels8_y2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_avg_pixels8_xy2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);

void ff_add_pixels_clamped_neon(const DCTELEM *, uint8_t *, int);
void ff_put_pixels_clamped_neon(const DCTELEM *, uint8_t *, int);
void ff_put_signed_pixels_clamped_neon(const DCTELEM *, uint8_t *, int);

void ff_put_h264_qpel16_mc00_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc10_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc20_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc30_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc01_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc11_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc21_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc31_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc02_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc12_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc22_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc32_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc03_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc13_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc23_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc33_neon(uint8_t *, uint8_t *, int);

void ff_put_h264_qpel8_mc00_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc10_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc20_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc30_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc01_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc11_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc21_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc31_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc02_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc12_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc22_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc32_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc03_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc13_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc23_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc33_neon(uint8_t *, uint8_t *, int);

void ff_avg_h264_qpel16_mc00_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc10_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc20_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc30_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc01_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc11_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc21_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc31_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc02_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc12_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc22_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc32_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc03_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc13_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc23_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel16_mc33_neon(uint8_t *, uint8_t *, int);

void ff_avg_h264_qpel8_mc00_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc10_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc20_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc30_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc01_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc11_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc21_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc31_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc02_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc12_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc22_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc32_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc03_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc13_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc23_neon(uint8_t *, uint8_t *, int);
void ff_avg_h264_qpel8_mc33_neon(uint8_t *, uint8_t *, int);

void ff_put_h264_chroma_mc8_neon(uint8_t *, uint8_t *, int, int, int, int);
void ff_put_h264_chroma_mc4_neon(uint8_t *, uint8_t *, int, int, int, int);
void ff_put_h264_chroma_mc2_neon(uint8_t *, uint8_t *, int, int, int, int);

void ff_avg_h264_chroma_mc8_neon(uint8_t *, uint8_t *, int, int, int, int);
void ff_avg_h264_chroma_mc4_neon(uint8_t *, uint8_t *, int, int, int, int);
void ff_avg_h264_chroma_mc2_neon(uint8_t *, uint8_t *, int, int, int, int);

void ff_vp3_v_loop_filter_neon(uint8_t *, int, int *);
void ff_vp3_h_loop_filter_neon(uint8_t *, int, int *);

void ff_vector_fmul_neon(float *dst, const float *src0, const float *src1, int len);
void ff_vector_fmul_window_neon(float *dst, const float *src0,
                                const float *src1, const float *win, int len);
void ff_vector_fmul_scalar_neon(float *dst, const float *src, float mul,
                                int len);
void ff_vector_fmac_scalar_neon(float *dst, const float *src, float mul,
                                int len);
void ff_butterflies_float_neon(float *v1, float *v2, int len);
float ff_scalarproduct_float_neon(const float *v1, const float *v2, int len);
void ff_vector_fmul_reverse_neon(float *dst, const float *src0,
                                 const float *src1, int len);
void ff_vector_fmul_add_neon(float *dst, const float *src0, const float *src1,
                             const float *src2, int len);

void ff_vector_clipf_neon(float *dst, const float *src, float min, float max,
                          int len);
void ff_vector_clip_int32_neon(int32_t *dst, const int32_t *src, int32_t min,
                               int32_t max, unsigned int len);

void ff_vorbis_inverse_coupling_neon(float *mag, float *ang, int blocksize);

int32_t ff_scalarproduct_int16_neon(const int16_t *v1, const int16_t *v2, int len);
int32_t ff_scalarproduct_and_madd_int16_neon(int16_t *v1, const int16_t *v2,
                                             const int16_t *v3, int len, int mul);

void ff_apply_window_int16_neon(int16_t *dst, const int16_t *src,
                                const int16_t *window, unsigned n);

void ff_dsputil_init_neon(DSPContext *c, AVCodecContext *avctx)
{
    const int high_bit_depth = avctx->bits_per_raw_sample > 8;

    if (!avctx->lowres && avctx->bits_per_raw_sample <= 8) {
        if (avctx->idct_algo == FF_IDCT_AUTO ||
            avctx->idct_algo == FF_IDCT_SIMPLENEON) {
            c->idct_put              = ff_simple_idct_put_neon;
            c->idct_add              = ff_simple_idct_add_neon;
            c->idct                  = ff_simple_idct_neon;
            c->idct_permutation_type = FF_PARTTRANS_IDCT_PERM;
        } else if ((CONFIG_VP3_DECODER || CONFIG_VP5_DECODER ||
                    CONFIG_VP6_DECODER) &&
                   avctx->idct_algo == FF_IDCT_VP3) {
            c->idct_put              = ff_vp3_idct_put_neon;
            c->idct_add              = ff_vp3_idct_add_neon;
            c->idct                  = ff_vp3_idct_neon;
            c->idct_permutation_type = FF_TRANSPOSE_IDCT_PERM;
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

        c->avg_no_rnd_pixels_tab[0][0] = ff_avg_pixels16_neon;
        c->avg_no_rnd_pixels_tab[0][1] = ff_avg_pixels16_x2_no_rnd_neon;
        c->avg_no_rnd_pixels_tab[0][2] = ff_avg_pixels16_y2_no_rnd_neon;
        c->avg_no_rnd_pixels_tab[0][3] = ff_avg_pixels16_xy2_no_rnd_neon;
        c->avg_no_rnd_pixels_tab[1][0] = ff_avg_pixels8_neon;
        c->avg_no_rnd_pixels_tab[1][1] = ff_avg_pixels8_x2_no_rnd_neon;
        c->avg_no_rnd_pixels_tab[1][2] = ff_avg_pixels8_y2_no_rnd_neon;
        c->avg_no_rnd_pixels_tab[1][3] = ff_avg_pixels8_xy2_no_rnd_neon;
    }

    c->add_pixels_clamped = ff_add_pixels_clamped_neon;
    c->put_pixels_clamped = ff_put_pixels_clamped_neon;
    c->put_signed_pixels_clamped = ff_put_signed_pixels_clamped_neon;

    if (CONFIG_H264_DECODER && !high_bit_depth) {
        c->put_h264_chroma_pixels_tab[0] = ff_put_h264_chroma_mc8_neon;
        c->put_h264_chroma_pixels_tab[1] = ff_put_h264_chroma_mc4_neon;
        c->put_h264_chroma_pixels_tab[2] = ff_put_h264_chroma_mc2_neon;

        c->avg_h264_chroma_pixels_tab[0] = ff_avg_h264_chroma_mc8_neon;
        c->avg_h264_chroma_pixels_tab[1] = ff_avg_h264_chroma_mc4_neon;
        c->avg_h264_chroma_pixels_tab[2] = ff_avg_h264_chroma_mc2_neon;

        c->put_h264_qpel_pixels_tab[0][ 0] = ff_put_h264_qpel16_mc00_neon;
        c->put_h264_qpel_pixels_tab[0][ 1] = ff_put_h264_qpel16_mc10_neon;
        c->put_h264_qpel_pixels_tab[0][ 2] = ff_put_h264_qpel16_mc20_neon;
        c->put_h264_qpel_pixels_tab[0][ 3] = ff_put_h264_qpel16_mc30_neon;
        c->put_h264_qpel_pixels_tab[0][ 4] = ff_put_h264_qpel16_mc01_neon;
        c->put_h264_qpel_pixels_tab[0][ 5] = ff_put_h264_qpel16_mc11_neon;
        c->put_h264_qpel_pixels_tab[0][ 6] = ff_put_h264_qpel16_mc21_neon;
        c->put_h264_qpel_pixels_tab[0][ 7] = ff_put_h264_qpel16_mc31_neon;
        c->put_h264_qpel_pixels_tab[0][ 8] = ff_put_h264_qpel16_mc02_neon;
        c->put_h264_qpel_pixels_tab[0][ 9] = ff_put_h264_qpel16_mc12_neon;
        c->put_h264_qpel_pixels_tab[0][10] = ff_put_h264_qpel16_mc22_neon;
        c->put_h264_qpel_pixels_tab[0][11] = ff_put_h264_qpel16_mc32_neon;
        c->put_h264_qpel_pixels_tab[0][12] = ff_put_h264_qpel16_mc03_neon;
        c->put_h264_qpel_pixels_tab[0][13] = ff_put_h264_qpel16_mc13_neon;
        c->put_h264_qpel_pixels_tab[0][14] = ff_put_h264_qpel16_mc23_neon;
        c->put_h264_qpel_pixels_tab[0][15] = ff_put_h264_qpel16_mc33_neon;

        c->put_h264_qpel_pixels_tab[1][ 0] = ff_put_h264_qpel8_mc00_neon;
        c->put_h264_qpel_pixels_tab[1][ 1] = ff_put_h264_qpel8_mc10_neon;
        c->put_h264_qpel_pixels_tab[1][ 2] = ff_put_h264_qpel8_mc20_neon;
        c->put_h264_qpel_pixels_tab[1][ 3] = ff_put_h264_qpel8_mc30_neon;
        c->put_h264_qpel_pixels_tab[1][ 4] = ff_put_h264_qpel8_mc01_neon;
        c->put_h264_qpel_pixels_tab[1][ 5] = ff_put_h264_qpel8_mc11_neon;
        c->put_h264_qpel_pixels_tab[1][ 6] = ff_put_h264_qpel8_mc21_neon;
        c->put_h264_qpel_pixels_tab[1][ 7] = ff_put_h264_qpel8_mc31_neon;
        c->put_h264_qpel_pixels_tab[1][ 8] = ff_put_h264_qpel8_mc02_neon;
        c->put_h264_qpel_pixels_tab[1][ 9] = ff_put_h264_qpel8_mc12_neon;
        c->put_h264_qpel_pixels_tab[1][10] = ff_put_h264_qpel8_mc22_neon;
        c->put_h264_qpel_pixels_tab[1][11] = ff_put_h264_qpel8_mc32_neon;
        c->put_h264_qpel_pixels_tab[1][12] = ff_put_h264_qpel8_mc03_neon;
        c->put_h264_qpel_pixels_tab[1][13] = ff_put_h264_qpel8_mc13_neon;
        c->put_h264_qpel_pixels_tab[1][14] = ff_put_h264_qpel8_mc23_neon;
        c->put_h264_qpel_pixels_tab[1][15] = ff_put_h264_qpel8_mc33_neon;

        c->avg_h264_qpel_pixels_tab[0][ 0] = ff_avg_h264_qpel16_mc00_neon;
        c->avg_h264_qpel_pixels_tab[0][ 1] = ff_avg_h264_qpel16_mc10_neon;
        c->avg_h264_qpel_pixels_tab[0][ 2] = ff_avg_h264_qpel16_mc20_neon;
        c->avg_h264_qpel_pixels_tab[0][ 3] = ff_avg_h264_qpel16_mc30_neon;
        c->avg_h264_qpel_pixels_tab[0][ 4] = ff_avg_h264_qpel16_mc01_neon;
        c->avg_h264_qpel_pixels_tab[0][ 5] = ff_avg_h264_qpel16_mc11_neon;
        c->avg_h264_qpel_pixels_tab[0][ 6] = ff_avg_h264_qpel16_mc21_neon;
        c->avg_h264_qpel_pixels_tab[0][ 7] = ff_avg_h264_qpel16_mc31_neon;
        c->avg_h264_qpel_pixels_tab[0][ 8] = ff_avg_h264_qpel16_mc02_neon;
        c->avg_h264_qpel_pixels_tab[0][ 9] = ff_avg_h264_qpel16_mc12_neon;
        c->avg_h264_qpel_pixels_tab[0][10] = ff_avg_h264_qpel16_mc22_neon;
        c->avg_h264_qpel_pixels_tab[0][11] = ff_avg_h264_qpel16_mc32_neon;
        c->avg_h264_qpel_pixels_tab[0][12] = ff_avg_h264_qpel16_mc03_neon;
        c->avg_h264_qpel_pixels_tab[0][13] = ff_avg_h264_qpel16_mc13_neon;
        c->avg_h264_qpel_pixels_tab[0][14] = ff_avg_h264_qpel16_mc23_neon;
        c->avg_h264_qpel_pixels_tab[0][15] = ff_avg_h264_qpel16_mc33_neon;

        c->avg_h264_qpel_pixels_tab[1][ 0] = ff_avg_h264_qpel8_mc00_neon;
        c->avg_h264_qpel_pixels_tab[1][ 1] = ff_avg_h264_qpel8_mc10_neon;
        c->avg_h264_qpel_pixels_tab[1][ 2] = ff_avg_h264_qpel8_mc20_neon;
        c->avg_h264_qpel_pixels_tab[1][ 3] = ff_avg_h264_qpel8_mc30_neon;
        c->avg_h264_qpel_pixels_tab[1][ 4] = ff_avg_h264_qpel8_mc01_neon;
        c->avg_h264_qpel_pixels_tab[1][ 5] = ff_avg_h264_qpel8_mc11_neon;
        c->avg_h264_qpel_pixels_tab[1][ 6] = ff_avg_h264_qpel8_mc21_neon;
        c->avg_h264_qpel_pixels_tab[1][ 7] = ff_avg_h264_qpel8_mc31_neon;
        c->avg_h264_qpel_pixels_tab[1][ 8] = ff_avg_h264_qpel8_mc02_neon;
        c->avg_h264_qpel_pixels_tab[1][ 9] = ff_avg_h264_qpel8_mc12_neon;
        c->avg_h264_qpel_pixels_tab[1][10] = ff_avg_h264_qpel8_mc22_neon;
        c->avg_h264_qpel_pixels_tab[1][11] = ff_avg_h264_qpel8_mc32_neon;
        c->avg_h264_qpel_pixels_tab[1][12] = ff_avg_h264_qpel8_mc03_neon;
        c->avg_h264_qpel_pixels_tab[1][13] = ff_avg_h264_qpel8_mc13_neon;
        c->avg_h264_qpel_pixels_tab[1][14] = ff_avg_h264_qpel8_mc23_neon;
        c->avg_h264_qpel_pixels_tab[1][15] = ff_avg_h264_qpel8_mc33_neon;
    }

    if (CONFIG_VP3_DECODER) {
        c->vp3_v_loop_filter = ff_vp3_v_loop_filter_neon;
        c->vp3_h_loop_filter = ff_vp3_h_loop_filter_neon;
        c->vp3_idct_dc_add   = ff_vp3_idct_dc_add_neon;
    }

    c->vector_fmul                = ff_vector_fmul_neon;
    c->vector_fmul_window         = ff_vector_fmul_window_neon;
    c->vector_fmul_scalar         = ff_vector_fmul_scalar_neon;
    c->vector_fmac_scalar         = ff_vector_fmac_scalar_neon;
    c->butterflies_float          = ff_butterflies_float_neon;
    c->scalarproduct_float        = ff_scalarproduct_float_neon;
    c->vector_fmul_reverse        = ff_vector_fmul_reverse_neon;
    c->vector_fmul_add            = ff_vector_fmul_add_neon;
    c->vector_clipf               = ff_vector_clipf_neon;
    c->vector_clip_int32          = ff_vector_clip_int32_neon;

    if (CONFIG_VORBIS_DECODER)
        c->vorbis_inverse_coupling = ff_vorbis_inverse_coupling_neon;

    c->scalarproduct_int16 = ff_scalarproduct_int16_neon;
    c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_neon;

    c->apply_window_int16 = ff_apply_window_int16_neon;
}
