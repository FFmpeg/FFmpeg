/*
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

void ff_h264_idct_add_8_rvv(uint8_t *dst, int16_t *block, int stride);
void ff_h264_idct8_add_8_rvv(uint8_t *dst, int16_t *block, int stride);
void ff_h264_idct_add16_8_rvv(uint8_t *dst, const int *blockoffset,
                              int16_t *block, int stride,
                              const uint8_t nnzc[5 * 8]);
void ff_h264_idct_add16intra_8_rvv(uint8_t *dst, const int *blockoffset,
                                   int16_t *block, int stride,
                                   const uint8_t nnzc[5 * 8]);
void ff_h264_idct8_add4_8_rvv(uint8_t *dst, const int *blockoffset,
                              int16_t *block, int stride,
                              const uint8_t nnzc[5 * 8]);

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
        if (bit_depth == 8 && ff_rv_vlen_least(128)) {
            for (int i = 0; i < 4; i++) {
                dsp->weight_h264_pixels_tab[i] =
                    ff_h264_weight_funcs_8_rvv[i].weight;
                dsp->biweight_h264_pixels_tab[i] =
                    ff_h264_weight_funcs_8_rvv[i].biweight;
            }

            dsp->h264_v_loop_filter_luma = ff_h264_v_loop_filter_luma_8_rvv;
            dsp->h264_h_loop_filter_luma = ff_h264_h_loop_filter_luma_8_rvv;
            dsp->h264_h_loop_filter_luma_mbaff =
                ff_h264_h_loop_filter_luma_mbaff_8_rvv;

            dsp->h264_idct_add = ff_h264_idct_add_8_rvv;
            dsp->h264_idct8_add = ff_h264_idct8_add_8_rvv;
#  if __riscv_xlen == 64
            dsp->h264_idct_add16 = ff_h264_idct_add16_8_rvv;
            dsp->h264_idct_add16intra = ff_h264_idct_add16intra_8_rvv;
            dsp->h264_idct8_add4 = ff_h264_idct8_add4_8_rvv;
#  endif
        }
        dsp->startcode_find_candidate = ff_startcode_find_candidate_rvv;
    }
# endif
#endif
}
