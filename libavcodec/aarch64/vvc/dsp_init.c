/*
 * VVC filters DSP
 *
 * Copyright (C) 2024 Zhao Zhili
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

#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/aarch64/h26x/dsp.h"
#include "libavcodec/vvc/dsp.h"
#include "libavcodec/vvc/dec.h"
#include "libavcodec/vvc/ctu.h"

#define BIT_DEPTH 8
#include "alf_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "alf_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 12
#include "alf_template.c"
#undef BIT_DEPTH

int ff_vvc_sad_neon(const int16_t *src0, const int16_t *src1, int dx, int dy,
                    const int block_w, const int block_h);

void ff_vvc_avg_8_neon(uint8_t *dst, ptrdiff_t dst_stride,
                       const int16_t *src0, const int16_t *src1, int width,
                       int height);
void ff_vvc_avg_10_neon(uint8_t *dst, ptrdiff_t dst_stride,
                       const int16_t *src0, const int16_t *src1, int width,
                       int height);
void ff_vvc_avg_12_neon(uint8_t *dst, ptrdiff_t dst_stride,
                        const int16_t *src0, const int16_t *src1, int width,
                        int height);

void ff_vvc_w_avg_8_neon(uint8_t *_dst, ptrdiff_t _dst_stride,
                         const int16_t *src0, const int16_t *src1,
                         int width, int height,
                         uintptr_t w0_w1, uintptr_t offset_shift);
void ff_vvc_w_avg_10_neon(uint8_t *_dst, ptrdiff_t _dst_stride,
                         const int16_t *src0, const int16_t *src1,
                         int width, int height,
                         uintptr_t w0_w1, uintptr_t offset_shift);
void ff_vvc_w_avg_12_neon(uint8_t *_dst, ptrdiff_t _dst_stride,
                          const int16_t *src0, const int16_t *src1,
                          int width, int height,
                          uintptr_t w0_w1, uintptr_t offset_shift);
/* When passing arguments to functions, Apple platforms diverge from the ARM64
 * standard ABI for functions that require passing arguments on the stack. To
 * simplify portability in the assembly function interface, use a different
 * function signature that doesn't require passing arguments on the stack.
 */
#define W_AVG_FUN(bit_depth) \
static void vvc_w_avg_ ## bit_depth(uint8_t *dst, ptrdiff_t dst_stride, \
    const int16_t *src0, const int16_t *src1, int width, int height, \
    int denom, int w0, int w1, int o0, int o1) \
{ \
    int shift = denom + FFMAX(3, 15 - bit_depth); \
    int offset = ((o0 + o1) * (1 << (bit_depth - 8)) + 1) * (1 << (shift - 1)); \
    uintptr_t w0_w1 = ((uintptr_t)w0 << 32) | (uint32_t)w1; \
    uintptr_t offset_shift = ((uintptr_t)offset << 32) | (uint32_t)shift; \
    ff_vvc_w_avg_ ## bit_depth ## _neon(dst, dst_stride, src0, src1, width, height, w0_w1, offset_shift); \
}

W_AVG_FUN(8)
W_AVG_FUN(10)
W_AVG_FUN(12)

#define DMVR_FUN(fn, bd) \
    void ff_vvc_dmvr_ ## fn ## bd ## _neon(int16_t *dst, \
        const uint8_t *_src, ptrdiff_t _src_stride, int height, \
        intptr_t mx, intptr_t my, int width);

DMVR_FUN(, 8)
DMVR_FUN(, 12)
DMVR_FUN(hv_, 8)
DMVR_FUN(hv_, 10)
DMVR_FUN(hv_, 12)

void ff_vvc_dsp_init_aarch64(VVCDSPContext *const c, const int bd)
{
    int cpu_flags = av_get_cpu_flags();
    if (!have_neon(cpu_flags))
        return;

    if (bd == 8) {
        c->inter.put[0][1][0][0] = ff_vvc_put_pel_pixels4_8_neon;
        c->inter.put[0][2][0][0] = ff_vvc_put_pel_pixels8_8_neon;
        c->inter.put[0][3][0][0] = ff_vvc_put_pel_pixels16_8_neon;
        c->inter.put[0][4][0][0] = ff_vvc_put_pel_pixels32_8_neon;
        c->inter.put[0][5][0][0] = ff_vvc_put_pel_pixels64_8_neon;
        c->inter.put[0][6][0][0] = ff_vvc_put_pel_pixels128_8_neon;

        c->inter.put[0][1][0][1] = ff_vvc_put_qpel_h4_8_neon;
        c->inter.put[0][2][0][1] = ff_vvc_put_qpel_h8_8_neon;
        c->inter.put[0][3][0][1] = ff_vvc_put_qpel_h16_8_neon;
        c->inter.put[0][4][0][1] =
        c->inter.put[0][5][0][1] =
        c->inter.put[0][6][0][1] = ff_vvc_put_qpel_h32_8_neon;

        c->inter.put[0][1][1][0] = ff_vvc_put_qpel_v4_8_neon;
        c->inter.put[0][2][1][0] =
        c->inter.put[0][3][1][0] =
        c->inter.put[0][4][1][0] =
        c->inter.put[0][5][1][0] =
        c->inter.put[0][6][1][0] = ff_vvc_put_qpel_v8_8_neon;

        c->inter.put[0][1][1][1] = ff_vvc_put_qpel_hv4_8_neon;
        c->inter.put[0][2][1][1] = ff_vvc_put_qpel_hv8_8_neon;
        c->inter.put[0][3][1][1] = ff_vvc_put_qpel_hv16_8_neon;
        c->inter.put[0][4][1][1] = ff_vvc_put_qpel_hv32_8_neon;
        c->inter.put[0][5][1][1] = ff_vvc_put_qpel_hv64_8_neon;
        c->inter.put[0][6][1][1] = ff_vvc_put_qpel_hv128_8_neon;

        c->inter.put[1][1][0][1] = ff_vvc_put_epel_h4_8_neon;
        c->inter.put[1][2][0][1] = ff_vvc_put_epel_h8_8_neon;
        c->inter.put[1][3][0][1] = ff_vvc_put_epel_h16_8_neon;
        c->inter.put[1][4][0][1] =
        c->inter.put[1][5][0][1] =
        c->inter.put[1][6][0][1] = ff_vvc_put_epel_h32_8_neon;

        c->inter.put[1][1][1][1] = ff_vvc_put_epel_hv4_8_neon;
        c->inter.put[1][2][1][1] = ff_vvc_put_epel_hv8_8_neon;
        c->inter.put[1][3][1][1] = ff_vvc_put_epel_hv16_8_neon;
        c->inter.put[1][4][1][1] = ff_vvc_put_epel_hv32_8_neon;
        c->inter.put[1][5][1][1] = ff_vvc_put_epel_hv64_8_neon;
        c->inter.put[1][6][1][1] = ff_vvc_put_epel_hv128_8_neon;

        c->inter.put_uni[0][1][0][0] = ff_vvc_put_pel_uni_pixels4_8_neon;
        c->inter.put_uni[0][2][0][0] = ff_vvc_put_pel_uni_pixels8_8_neon;
        c->inter.put_uni[0][3][0][0] = ff_vvc_put_pel_uni_pixels16_8_neon;
        c->inter.put_uni[0][4][0][0] = ff_vvc_put_pel_uni_pixels32_8_neon;
        c->inter.put_uni[0][5][0][0] = ff_vvc_put_pel_uni_pixels64_8_neon;
        c->inter.put_uni[0][6][0][0] = ff_vvc_put_pel_uni_pixels128_8_neon;

        c->inter.put_uni[0][1][0][1] = ff_vvc_put_qpel_uni_h4_8_neon;
        c->inter.put_uni[0][2][0][1] = ff_vvc_put_qpel_uni_h8_8_neon;
        c->inter.put_uni[0][3][0][1] = ff_vvc_put_qpel_uni_h16_8_neon;
        c->inter.put_uni[0][4][0][1] =
        c->inter.put_uni[0][5][0][1] =
        c->inter.put_uni[0][6][0][1] = ff_vvc_put_qpel_uni_h32_8_neon;

        c->inter.put_uni_w[0][1][0][0] = ff_vvc_put_pel_uni_w_pixels4_8_neon;
        c->inter.put_uni_w[0][2][0][0] = ff_vvc_put_pel_uni_w_pixels8_8_neon;
        c->inter.put_uni_w[0][3][0][0] = ff_vvc_put_pel_uni_w_pixels16_8_neon;
        c->inter.put_uni_w[0][4][0][0] = ff_vvc_put_pel_uni_w_pixels32_8_neon;
        c->inter.put_uni_w[0][5][0][0] = ff_vvc_put_pel_uni_w_pixels64_8_neon;
        c->inter.put_uni_w[0][6][0][0] = ff_vvc_put_pel_uni_w_pixels128_8_neon;

        c->inter.avg = ff_vvc_avg_8_neon;
        c->inter.w_avg = vvc_w_avg_8;
        c->inter.dmvr[0][0] = ff_vvc_dmvr_8_neon;
        c->inter.dmvr[1][1] = ff_vvc_dmvr_hv_8_neon;

        for (int i = 0; i < FF_ARRAY_ELEMS(c->sao.band_filter); i++)
            c->sao.band_filter[i] = ff_h26x_sao_band_filter_8x8_8_neon;
        c->sao.edge_filter[0] = ff_vvc_sao_edge_filter_8x8_8_neon;
        for (int i = 1; i < FF_ARRAY_ELEMS(c->sao.edge_filter); i++)
            c->sao.edge_filter[i] = ff_vvc_sao_edge_filter_16x16_8_neon;
        c->alf.filter[LUMA] = alf_filter_luma_8_neon;
        c->alf.filter[CHROMA] = alf_filter_chroma_8_neon;

        if (have_i8mm(cpu_flags)) {
            c->inter.put[0][1][0][1] = ff_vvc_put_qpel_h4_8_neon_i8mm;
            c->inter.put[0][2][0][1] = ff_vvc_put_qpel_h8_8_neon_i8mm;
            c->inter.put[0][3][0][1] = ff_vvc_put_qpel_h16_8_neon_i8mm;
            c->inter.put[0][4][0][1] = ff_vvc_put_qpel_h32_8_neon_i8mm;
            c->inter.put[0][5][0][1] = ff_vvc_put_qpel_h64_8_neon_i8mm;
            c->inter.put[0][6][0][1] = ff_vvc_put_qpel_h128_8_neon_i8mm;

            c->inter.put[0][1][1][1] = ff_vvc_put_qpel_hv4_8_neon_i8mm;
            c->inter.put[0][2][1][1] = ff_vvc_put_qpel_hv8_8_neon_i8mm;
            c->inter.put[0][3][1][1] = ff_vvc_put_qpel_hv16_8_neon_i8mm;
            c->inter.put[0][4][1][1] = ff_vvc_put_qpel_hv32_8_neon_i8mm;
            c->inter.put[0][5][1][1] = ff_vvc_put_qpel_hv64_8_neon_i8mm;
            c->inter.put[0][6][1][1] = ff_vvc_put_qpel_hv128_8_neon_i8mm;

            c->inter.put[1][1][0][1] = ff_vvc_put_epel_h4_8_neon_i8mm;
            c->inter.put[1][2][0][1] = ff_vvc_put_epel_h8_8_neon_i8mm;
            c->inter.put[1][3][0][1] = ff_vvc_put_epel_h16_8_neon_i8mm;
            c->inter.put[1][4][0][1] = ff_vvc_put_epel_h32_8_neon_i8mm;
            c->inter.put[1][5][0][1] = ff_vvc_put_epel_h64_8_neon_i8mm;
            c->inter.put[1][6][0][1] = ff_vvc_put_epel_h128_8_neon_i8mm;

            c->inter.put[1][1][1][1] = ff_vvc_put_epel_hv4_8_neon_i8mm;
            c->inter.put[1][2][1][1] = ff_vvc_put_epel_hv8_8_neon_i8mm;
            c->inter.put[1][3][1][1] = ff_vvc_put_epel_hv16_8_neon_i8mm;
            c->inter.put[1][4][1][1] = ff_vvc_put_epel_hv32_8_neon_i8mm;
            c->inter.put[1][5][1][1] = ff_vvc_put_epel_hv64_8_neon_i8mm;
            c->inter.put[1][6][1][1] = ff_vvc_put_epel_hv128_8_neon_i8mm;
        }
    } else if (bd == 10) {
        c->inter.avg = ff_vvc_avg_10_neon;
        c->inter.w_avg = vvc_w_avg_10;
        c->inter.dmvr[1][1] = ff_vvc_dmvr_hv_10_neon;

        c->alf.filter[LUMA] = alf_filter_luma_10_neon;
        c->alf.filter[CHROMA] = alf_filter_chroma_10_neon;
    } else if (bd == 12) {
        c->inter.avg = ff_vvc_avg_12_neon;
        c->inter.w_avg = vvc_w_avg_12;
        c->inter.dmvr[0][0] = ff_vvc_dmvr_12_neon;
        c->inter.dmvr[1][1] = ff_vvc_dmvr_hv_12_neon;

        c->alf.filter[LUMA] = alf_filter_luma_12_neon;
        c->alf.filter[CHROMA] = alf_filter_chroma_12_neon;
    }

    c->inter.sad = ff_vvc_sad_neon;
}
