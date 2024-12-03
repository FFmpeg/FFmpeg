/*
 * Copyright (c) 2024 J. Dekker <jdek@itanimul.li>
 * Copyright © 2024 Rémi Denis-Courmont.
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

#include "config.h"

#include <stdint.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/h264dsp.h"

extern const struct {
    const h264_weight_func weight;
    const h264_biweight_func biweight;
} ff_h264_weight_funcs_8_rvv[];

void ff_h264_v_loop_filter_luma_8_rvv(uint8_t *pix, ptrdiff_t stride,
                                      int alpha, int beta, int8_t *tc0);
void ff_h264_h_loop_filter_luma_8_rvv(uint8_t *pix, ptrdiff_t stride,
                                      int alpha, int beta, int8_t *tc0);
void ff_h264_h_loop_filter_luma_mbaff_8_rvv(uint8_t *pix, ptrdiff_t stride,
                                            int alpha, int beta, int8_t *tc0);
void ff_h264_v_loop_filter_luma_intra_8_rvv(uint8_t *pix, ptrdiff_t stride,
                                            int alpha, int beta);
void ff_h264_h_loop_filter_luma_intra_8_rvv(uint8_t *pix, ptrdiff_t stride,
                                            int alpha, int beta);
void ff_h264_h_loop_filter_luma_mbaff_intra_8_rvv(uint8_t *pix, ptrdiff_t s,
                                                  int a, int b);
void ff_h264_v_loop_filter_chroma_8_rvv(uint8_t *pix, ptrdiff_t stride,
                                        int alpha, int beta, int8_t *tc0);
void ff_h264_h_loop_filter_chroma_8_rvv(uint8_t *pix, ptrdiff_t stride,
                                        int alpha, int beta, int8_t *tc0);
void ff_h264_h_loop_filter_chroma_mbaff_8_rvv(uint8_t *pix, ptrdiff_t stride,
                                              int alpha, int beta,
                                              int8_t *tc0);
void ff_h264_v_loop_filter_chroma_intra_8_rvv(uint8_t *pix, ptrdiff_t stride,
                                              int alpha, int beta);
void ff_h264_h_loop_filter_chroma_intra_8_rvv(uint8_t *pix, ptrdiff_t stride,
                                              int alpha, int beta);
void ff_h264_h_loop_filter_chroma_mbaff_intra_8_rvv(uint8_t *pix,
                                                    ptrdiff_t stride,
                                                    int alpha, int beta);

#define IDCT_DEPTH(depth) \
void ff_h264_idct_add_##depth##_rvv(uint8_t *d, int16_t *s, int stride); \
void ff_h264_idct8_add_##depth##_rvv(uint8_t *d, int16_t *s, int stride); \
void ff_h264_idct4_dc_add_##depth##_rvv(uint8_t *, int16_t *, int); \
void ff_h264_idct8_dc_add_##depth##_rvv(uint8_t *, int16_t *, int); \
void ff_h264_idct_add16_##depth##_rvv(uint8_t *d, const int *soffset, \
                                      int16_t *s, int stride, \
                                      const uint8_t nnzc[5 * 8]); \
void ff_h264_idct_add16intra_##depth##_rvv(uint8_t *d, const int *soffset, \
                                   int16_t *s, int stride, \
                                   const uint8_t nnzc[5 * 8]); \
void ff_h264_idct8_add4_##depth##_rvv(uint8_t *d, const int *soffset, \
                                      int16_t *s, int stride, \
                                      const uint8_t nnzc[5 * 8]); \
void ff_h264_idct4_add8_##depth##_rvv(uint8_t **d, const int *soffset, \
                                      int16_t *s, int stride, \
                                      const uint8_t nnzc[5 * 8]); \
void ff_h264_idct4_add8_422_##depth##_rvv(uint8_t **d, const int *soffset, \
                                          int16_t *s, int stride, \
                                          const uint8_t nnzc[5 * 8]);

IDCT_DEPTH(8)
IDCT_DEPTH(9)
IDCT_DEPTH(10)
IDCT_DEPTH(12)
IDCT_DEPTH(14)
#undef IDCT_DEPTH

void ff_h264_add_pixels8_8_rvv(uint8_t *dst, int16_t *block, int stride);
void ff_h264_add_pixels4_8_rvv(uint8_t *dst, int16_t *block, int stride);
void ff_h264_add_pixels8_16_rvv(uint8_t *dst, int16_t *block, int stride);
void ff_h264_add_pixels4_16_rvv(uint8_t *dst, int16_t *block, int stride);

extern int ff_startcode_find_candidate_rvb(const uint8_t *, int);
extern int ff_startcode_find_candidate_rvv(const uint8_t *, int);

av_cold void ff_h264dsp_init_riscv(H264DSPContext *dsp, const int bit_depth,
                                   const int chroma_format_idc)
{
#if HAVE_RV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVB_BASIC)
        dsp->startcode_find_candidate = ff_startcode_find_candidate_rvb;
# if HAVE_RVV
    if (flags & AV_CPU_FLAG_RVV_I32) {
        const bool zvl128b = ff_rv_vlen_least(128);

        if (bit_depth == 8) {
            if (zvl128b) {
                if (flags & AV_CPU_FLAG_RVB)
                    dsp->weight_h264_pixels_tab[0] =
                        ff_h264_weight_funcs_8_rvv[0].weight;
                dsp->biweight_h264_pixels_tab[0] =
                    ff_h264_weight_funcs_8_rvv[0].biweight;
            }
            if (flags & AV_CPU_FLAG_RVV_I64) {
                dsp->weight_h264_pixels_tab[1] =
                    ff_h264_weight_funcs_8_rvv[1].weight;
                dsp->biweight_h264_pixels_tab[1] =
                    ff_h264_weight_funcs_8_rvv[1].biweight;
            }
            dsp->weight_h264_pixels_tab[2] =
                 ff_h264_weight_funcs_8_rvv[2].weight;
            dsp->biweight_h264_pixels_tab[2] =
                 ff_h264_weight_funcs_8_rvv[2].biweight;
            dsp->weight_h264_pixels_tab[3] =
                 ff_h264_weight_funcs_8_rvv[3].weight;
            dsp->biweight_h264_pixels_tab[3] =
                 ff_h264_weight_funcs_8_rvv[3].biweight;
        }

        if (bit_depth == 8 && zvl128b) {
            dsp->h264_v_loop_filter_luma = ff_h264_v_loop_filter_luma_8_rvv;
            dsp->h264_h_loop_filter_luma = ff_h264_h_loop_filter_luma_8_rvv;
            dsp->h264_h_loop_filter_luma_mbaff =
                ff_h264_h_loop_filter_luma_mbaff_8_rvv;
            dsp->h264_v_loop_filter_luma_intra =
                ff_h264_v_loop_filter_luma_intra_8_rvv;
            dsp->h264_h_loop_filter_luma_intra =
                ff_h264_h_loop_filter_luma_intra_8_rvv;
            dsp->h264_h_loop_filter_luma_mbaff_intra =
                ff_h264_h_loop_filter_luma_mbaff_intra_8_rvv;
            dsp->h264_v_loop_filter_chroma =
                ff_h264_v_loop_filter_chroma_8_rvv;
            dsp->h264_v_loop_filter_chroma_intra =
                ff_h264_v_loop_filter_chroma_intra_8_rvv;

            if (chroma_format_idc <= 1) {
                dsp->h264_h_loop_filter_chroma =
                    ff_h264_h_loop_filter_chroma_8_rvv;
                dsp->h264_h_loop_filter_chroma_mbaff =
                    ff_h264_h_loop_filter_chroma_mbaff_8_rvv;
                dsp->h264_h_loop_filter_chroma_intra =
                    ff_h264_h_loop_filter_chroma_intra_8_rvv;
                dsp->h264_h_loop_filter_chroma_mbaff_intra =
                    ff_h264_h_loop_filter_chroma_mbaff_intra_8_rvv;
            }

            dsp->h264_idct_add  = ff_h264_idct_add_8_rvv;
            dsp->h264_idct8_add = ff_h264_idct8_add_8_rvv;
            if (flags & AV_CPU_FLAG_RVB) {
                dsp->h264_idct_dc_add     = ff_h264_idct4_dc_add_8_rvv;
                dsp->h264_idct_add16      = ff_h264_idct_add16_8_rvv;
                dsp->h264_idct_add16intra = ff_h264_idct_add16intra_8_rvv;
#  if __riscv_xlen == 64
                dsp->h264_idct8_add4      = ff_h264_idct8_add4_8_rvv;
                if (chroma_format_idc <= 1)
                    dsp->h264_idct_add8   = ff_h264_idct4_add8_8_rvv;
                else
                    dsp->h264_idct_add8   = ff_h264_idct4_add8_422_8_rvv;
#  endif
            }
            if (flags & AV_CPU_FLAG_RVV_I64) {
                dsp->h264_add_pixels8_clear = ff_h264_add_pixels8_8_rvv;
                if (flags & AV_CPU_FLAG_RVB)
                    dsp->h264_idct8_dc_add = ff_h264_idct8_dc_add_8_rvv;
            }
            dsp->h264_add_pixels4_clear = ff_h264_add_pixels4_8_rvv;
        }

#define IDCT_DEPTH(depth) \
        if (bit_depth == depth) { \
            if (zvl128b) \
                dsp->h264_idct_add = ff_h264_idct_add_##depth##_rvv; \
            if (flags & AV_CPU_FLAG_RVB) \
                dsp->h264_idct8_add = ff_h264_idct8_add_##depth##_rvv; \
            if (zvl128b && (flags & AV_CPU_FLAG_RVB)) { \
                dsp->h264_idct_dc_add  = ff_h264_idct4_dc_add_##depth##_rvv; \
                dsp->h264_idct8_dc_add = ff_h264_idct8_dc_add_##depth##_rvv; \
                dsp->h264_idct_add16 = ff_h264_idct_add16_##depth##_rvv; \
                dsp->h264_idct_add16intra = \
                    ff_h264_idct_add16intra_##depth##_rvv; \
                if (__riscv_xlen == 64) { \
                    if (chroma_format_idc <= 1) \
                        dsp->h264_idct_add8 = \
                            ff_h264_idct4_add8_##depth##_rvv; \
                    else \
                        dsp->h264_idct_add8 = \
                            ff_h264_idct4_add8_422_##depth##_rvv; \
                } \
            } \
            if (__riscv_xlen == 64 && (flags & AV_CPU_FLAG_RVB)) \
                dsp->h264_idct8_add4 = ff_h264_idct8_add4_##depth##_rvv; \
        }

        IDCT_DEPTH(9)
        IDCT_DEPTH(10)
        IDCT_DEPTH(12)
        IDCT_DEPTH(14)

        if (bit_depth > 8 && zvl128b) {
            dsp->h264_add_pixels8_clear = ff_h264_add_pixels8_16_rvv;
            if (flags & AV_CPU_FLAG_RVV_I64)
                dsp->h264_add_pixels4_clear = ff_h264_add_pixels4_16_rvv;
        }

        dsp->startcode_find_candidate = ff_startcode_find_candidate_rvv;
    }
# endif
#endif
}
