/*
 * Copyright (c) 2004-2005 Michael Niedermayer, Loren Merritt
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/h264dsp.h"
#include "dsputil_x86.h"

/***********************************/
/* IDCT */
#define IDCT_ADD_FUNC(NUM, DEPTH, OPT)                                  \
void ff_h264_idct ## NUM ## _add_ ## DEPTH ## _ ## OPT(uint8_t *dst,    \
                                                       int16_t *block,  \
                                                       int stride);

IDCT_ADD_FUNC(, 8, mmx)
IDCT_ADD_FUNC(, 10, sse2)
IDCT_ADD_FUNC(_dc, 8, mmxext)
IDCT_ADD_FUNC(_dc, 10, mmxext)
IDCT_ADD_FUNC(8_dc, 8, mmxext)
IDCT_ADD_FUNC(8_dc, 10, sse2)
IDCT_ADD_FUNC(8, 8, mmx)
IDCT_ADD_FUNC(8, 8, sse2)
IDCT_ADD_FUNC(8, 10, sse2)
IDCT_ADD_FUNC(, 10, avx)
IDCT_ADD_FUNC(8_dc, 10, avx)
IDCT_ADD_FUNC(8, 10, avx)


#define IDCT_ADD_REP_FUNC(NUM, REP, DEPTH, OPT)                         \
void ff_h264_idct ## NUM ## _add ## REP ## _ ## DEPTH ## _ ## OPT       \
    (uint8_t *dst, const int *block_offset,                             \
     int16_t *block, int stride, const uint8_t nnzc[6 * 8]);

IDCT_ADD_REP_FUNC(8, 4, 8, mmx)
IDCT_ADD_REP_FUNC(8, 4, 8, mmxext)
IDCT_ADD_REP_FUNC(8, 4, 8, sse2)
IDCT_ADD_REP_FUNC(8, 4, 10, sse2)
IDCT_ADD_REP_FUNC(8, 4, 10, avx)
IDCT_ADD_REP_FUNC(, 16, 8, mmx)
IDCT_ADD_REP_FUNC(, 16, 8, mmxext)
IDCT_ADD_REP_FUNC(, 16, 8, sse2)
IDCT_ADD_REP_FUNC(, 16, 10, sse2)
IDCT_ADD_REP_FUNC(, 16intra, 8, mmx)
IDCT_ADD_REP_FUNC(, 16intra, 8, mmxext)
IDCT_ADD_REP_FUNC(, 16intra, 8, sse2)
IDCT_ADD_REP_FUNC(, 16intra, 10, sse2)
IDCT_ADD_REP_FUNC(, 16, 10, avx)
IDCT_ADD_REP_FUNC(, 16intra, 10, avx)


#define IDCT_ADD_REP_FUNC2(NUM, REP, DEPTH, OPT)                      \
void ff_h264_idct ## NUM ## _add ## REP ## _ ## DEPTH ## _ ## OPT     \
    (uint8_t **dst, const int *block_offset,                          \
     int16_t *block, int stride, const uint8_t nnzc[6 * 8]);

IDCT_ADD_REP_FUNC2(, 8, 8, mmx)
IDCT_ADD_REP_FUNC2(, 8, 8, mmxext)
IDCT_ADD_REP_FUNC2(, 8, 8, sse2)
IDCT_ADD_REP_FUNC2(, 8, 10, sse2)
IDCT_ADD_REP_FUNC2(, 8, 10, avx)

void ff_h264_luma_dc_dequant_idct_mmx(int16_t *output, int16_t *input, int qmul);
void ff_h264_luma_dc_dequant_idct_sse2(int16_t *output, int16_t *input, int qmul);

/***********************************/
/* deblocking */

void ff_h264_loop_filter_strength_mmxext(int16_t bS[2][4][4], uint8_t nnz[40],
                                         int8_t ref[2][40],
                                         int16_t mv[2][40][2],
                                         int bidir, int edges, int step,
                                         int mask_mv0, int mask_mv1, int field);

#define LF_FUNC(DIR, TYPE, DEPTH, OPT)                                        \
void ff_deblock_ ## DIR ## _ ## TYPE ## _ ## DEPTH ## _ ## OPT(uint8_t *pix,  \
                                                               int stride,    \
                                                               int alpha,     \
                                                               int beta,      \
                                                               int8_t *tc0);
#define LF_IFUNC(DIR, TYPE, DEPTH, OPT) \
void ff_deblock_ ## DIR ## _ ## TYPE ## _ ## DEPTH ## _ ## OPT(uint8_t *pix,  \
                                                               int stride,    \
                                                               int alpha,     \
                                                               int beta);

#define LF_FUNCS(type, depth)                   \
LF_FUNC(h,  chroma,       depth, mmxext)        \
LF_IFUNC(h, chroma_intra, depth, mmxext)        \
LF_FUNC(v,  chroma,       depth, mmxext)        \
LF_IFUNC(v, chroma_intra, depth, mmxext)        \
LF_FUNC(h,  luma,         depth, mmxext)        \
LF_IFUNC(h, luma_intra,   depth, mmxext)        \
LF_FUNC(h,  luma,         depth, sse2)          \
LF_IFUNC(h, luma_intra,   depth, sse2)          \
LF_FUNC(v,  luma,         depth, sse2)          \
LF_IFUNC(v, luma_intra,   depth, sse2)          \
LF_FUNC(h,  chroma,       depth, sse2)          \
LF_IFUNC(h, chroma_intra, depth, sse2)          \
LF_FUNC(v,  chroma,       depth, sse2)          \
LF_IFUNC(v, chroma_intra, depth, sse2)          \
LF_FUNC(h,  luma,         depth, avx)           \
LF_IFUNC(h, luma_intra,   depth, avx)           \
LF_FUNC(v,  luma,         depth, avx)           \
LF_IFUNC(v, luma_intra,   depth, avx)           \
LF_FUNC(h,  chroma,       depth, avx)           \
LF_IFUNC(h, chroma_intra, depth, avx)           \
LF_FUNC(v,  chroma,       depth, avx)           \
LF_IFUNC(v, chroma_intra, depth, avx)

LF_FUNCS(uint8_t,   8)
LF_FUNCS(uint16_t, 10)

#if ARCH_X86_32 && HAVE_MMXEXT_EXTERNAL
LF_FUNC(v8, luma, 8, mmxext)
static void deblock_v_luma_8_mmxext(uint8_t *pix, int stride, int alpha,
                                    int beta, int8_t *tc0)
{
    if ((tc0[0] & tc0[1]) >= 0)
        ff_deblock_v8_luma_8_mmxext(pix + 0, stride, alpha, beta, tc0);
    if ((tc0[2] & tc0[3]) >= 0)
        ff_deblock_v8_luma_8_mmxext(pix + 8, stride, alpha, beta, tc0 + 2);
}
LF_IFUNC(v8, luma_intra, 8, mmxext)
static void deblock_v_luma_intra_8_mmxext(uint8_t *pix, int stride,
                                          int alpha, int beta)
{
    ff_deblock_v8_luma_intra_8_mmxext(pix + 0, stride, alpha, beta);
    ff_deblock_v8_luma_intra_8_mmxext(pix + 8, stride, alpha, beta);
}
#endif /* ARCH_X86_32 && HAVE_MMXEXT_EXTERNAL */

LF_FUNC(v,  luma,       10, mmxext)
LF_IFUNC(v, luma_intra, 10, mmxext)

/***********************************/
/* weighted prediction */

#define H264_WEIGHT(W, OPT)                                             \
void ff_h264_weight_ ## W ## _ ## OPT(uint8_t *dst, int stride,         \
                                      int height, int log2_denom,       \
                                      int weight, int offset);

#define H264_BIWEIGHT(W, OPT)                                           \
void ff_h264_biweight_ ## W ## _ ## OPT(uint8_t *dst, uint8_t *src,     \
                                        int stride, int height,         \
                                        int log2_denom, int weightd,    \
                                        int weights, int offset);

#define H264_BIWEIGHT_MMX(W)                    \
    H264_WEIGHT(W, mmxext)                      \
    H264_BIWEIGHT(W, mmxext)

#define H264_BIWEIGHT_MMX_SSE(W)                \
    H264_BIWEIGHT_MMX(W)                        \
    H264_WEIGHT(W, sse2)                        \
    H264_BIWEIGHT(W, sse2)                      \
    H264_BIWEIGHT(W, ssse3)

H264_BIWEIGHT_MMX_SSE(16)
H264_BIWEIGHT_MMX_SSE(8)
H264_BIWEIGHT_MMX(4)

#define H264_WEIGHT_10(W, DEPTH, OPT)                                   \
void ff_h264_weight_ ## W ## _ ## DEPTH ## _ ## OPT(uint8_t *dst,       \
                                                    int stride,         \
                                                    int height,         \
                                                    int log2_denom,     \
                                                    int weight,         \
                                                    int offset);

#define H264_BIWEIGHT_10(W, DEPTH, OPT)                                 \
void ff_h264_biweight_ ## W ## _ ## DEPTH ## _ ## OPT(uint8_t *dst,     \
                                                      uint8_t *src,     \
                                                      int stride,       \
                                                      int height,       \
                                                      int log2_denom,   \
                                                      int weightd,      \
                                                      int weights,      \
                                                      int offset);

#define H264_BIWEIGHT_10_SSE(W, DEPTH)          \
    H264_WEIGHT_10(W, DEPTH, sse2)              \
    H264_WEIGHT_10(W, DEPTH, sse4)              \
    H264_BIWEIGHT_10(W, DEPTH, sse2)            \
    H264_BIWEIGHT_10(W, DEPTH, sse4)

H264_BIWEIGHT_10_SSE(16, 10)
H264_BIWEIGHT_10_SSE(8,  10)
H264_BIWEIGHT_10_SSE(4,  10)

av_cold void ff_h264dsp_init_x86(H264DSPContext *c, const int bit_depth,
                                 const int chroma_format_idc)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

    if (chroma_format_idc == 1 && EXTERNAL_MMXEXT(cpu_flags))
        c->h264_loop_filter_strength = ff_h264_loop_filter_strength_mmxext;

    if (bit_depth == 8) {
        if (EXTERNAL_MMX(cpu_flags)) {
            c->h264_idct_dc_add   =
            c->h264_idct_add      = ff_h264_idct_add_8_mmx;
            c->h264_idct8_dc_add  =
            c->h264_idct8_add     = ff_h264_idct8_add_8_mmx;

            c->h264_idct_add16 = ff_h264_idct_add16_8_mmx;
            c->h264_idct8_add4 = ff_h264_idct8_add4_8_mmx;
            if (chroma_format_idc == 1)
                c->h264_idct_add8 = ff_h264_idct_add8_8_mmx;
            c->h264_idct_add16intra = ff_h264_idct_add16intra_8_mmx;
            if (cpu_flags & AV_CPU_FLAG_CMOV)
                c->h264_luma_dc_dequant_idct = ff_h264_luma_dc_dequant_idct_mmx;

            if (EXTERNAL_MMXEXT(cpu_flags)) {
                c->h264_idct_dc_add  = ff_h264_idct_dc_add_8_mmxext;
                c->h264_idct8_dc_add = ff_h264_idct8_dc_add_8_mmxext;
                c->h264_idct_add16   = ff_h264_idct_add16_8_mmxext;
                c->h264_idct8_add4   = ff_h264_idct8_add4_8_mmxext;
                if (chroma_format_idc == 1)
                    c->h264_idct_add8 = ff_h264_idct_add8_8_mmxext;
                c->h264_idct_add16intra = ff_h264_idct_add16intra_8_mmxext;

                c->h264_v_loop_filter_chroma       = ff_deblock_v_chroma_8_mmxext;
                c->h264_v_loop_filter_chroma_intra = ff_deblock_v_chroma_intra_8_mmxext;
                if (chroma_format_idc == 1) {
                    c->h264_h_loop_filter_chroma       = ff_deblock_h_chroma_8_mmxext;
                    c->h264_h_loop_filter_chroma_intra = ff_deblock_h_chroma_intra_8_mmxext;
                }
#if ARCH_X86_32 && HAVE_MMXEXT_EXTERNAL
                c->h264_v_loop_filter_luma       = deblock_v_luma_8_mmxext;
                c->h264_h_loop_filter_luma       = ff_deblock_h_luma_8_mmxext;
                c->h264_v_loop_filter_luma_intra = deblock_v_luma_intra_8_mmxext;
                c->h264_h_loop_filter_luma_intra = ff_deblock_h_luma_intra_8_mmxext;
#endif /* ARCH_X86_32 && HAVE_MMXEXT_EXTERNAL */
                c->weight_h264_pixels_tab[0] = ff_h264_weight_16_mmxext;
                c->weight_h264_pixels_tab[1] = ff_h264_weight_8_mmxext;
                c->weight_h264_pixels_tab[2] = ff_h264_weight_4_mmxext;

                c->biweight_h264_pixels_tab[0] = ff_h264_biweight_16_mmxext;
                c->biweight_h264_pixels_tab[1] = ff_h264_biweight_8_mmxext;
                c->biweight_h264_pixels_tab[2] = ff_h264_biweight_4_mmxext;

                if (EXTERNAL_SSE2(cpu_flags)) {
                    c->h264_idct8_add  = ff_h264_idct8_add_8_sse2;

                    c->h264_idct_add16 = ff_h264_idct_add16_8_sse2;
                    c->h264_idct8_add4 = ff_h264_idct8_add4_8_sse2;
                    if (chroma_format_idc == 1)
                        c->h264_idct_add8 = ff_h264_idct_add8_8_sse2;
                    c->h264_idct_add16intra      = ff_h264_idct_add16intra_8_sse2;
                    c->h264_luma_dc_dequant_idct = ff_h264_luma_dc_dequant_idct_sse2;

                    c->weight_h264_pixels_tab[0] = ff_h264_weight_16_sse2;
                    c->weight_h264_pixels_tab[1] = ff_h264_weight_8_sse2;

                    c->biweight_h264_pixels_tab[0] = ff_h264_biweight_16_sse2;
                    c->biweight_h264_pixels_tab[1] = ff_h264_biweight_8_sse2;

                    c->h264_v_loop_filter_luma       = ff_deblock_v_luma_8_sse2;
                    c->h264_h_loop_filter_luma       = ff_deblock_h_luma_8_sse2;
                    c->h264_v_loop_filter_luma_intra = ff_deblock_v_luma_intra_8_sse2;
                    c->h264_h_loop_filter_luma_intra = ff_deblock_h_luma_intra_8_sse2;
                }
                if (EXTERNAL_SSSE3(cpu_flags)) {
                    c->biweight_h264_pixels_tab[0] = ff_h264_biweight_16_ssse3;
                    c->biweight_h264_pixels_tab[1] = ff_h264_biweight_8_ssse3;
                }
                if (EXTERNAL_AVX(cpu_flags)) {
                    c->h264_v_loop_filter_luma       = ff_deblock_v_luma_8_avx;
                    c->h264_h_loop_filter_luma       = ff_deblock_h_luma_8_avx;
                    c->h264_v_loop_filter_luma_intra = ff_deblock_v_luma_intra_8_avx;
                    c->h264_h_loop_filter_luma_intra = ff_deblock_h_luma_intra_8_avx;
                }
            }
        }
    } else if (bit_depth == 10) {
        if (EXTERNAL_MMX(cpu_flags)) {
            if (EXTERNAL_MMXEXT(cpu_flags)) {
#if ARCH_X86_32
                c->h264_v_loop_filter_chroma       = ff_deblock_v_chroma_10_mmxext;
                c->h264_v_loop_filter_chroma_intra = ff_deblock_v_chroma_intra_10_mmxext;
                c->h264_v_loop_filter_luma         = ff_deblock_v_luma_10_mmxext;
                c->h264_h_loop_filter_luma         = ff_deblock_h_luma_10_mmxext;
                c->h264_v_loop_filter_luma_intra   = ff_deblock_v_luma_intra_10_mmxext;
                c->h264_h_loop_filter_luma_intra   = ff_deblock_h_luma_intra_10_mmxext;
#endif /* ARCH_X86_32 */
                c->h264_idct_dc_add = ff_h264_idct_dc_add_10_mmxext;
                if (EXTERNAL_SSE2(cpu_flags)) {
                    c->h264_idct_add     = ff_h264_idct_add_10_sse2;
                    c->h264_idct8_dc_add = ff_h264_idct8_dc_add_10_sse2;

                    c->h264_idct_add16 = ff_h264_idct_add16_10_sse2;
                    if (chroma_format_idc == 1)
                        c->h264_idct_add8 = ff_h264_idct_add8_10_sse2;
                    c->h264_idct_add16intra = ff_h264_idct_add16intra_10_sse2;
#if HAVE_ALIGNED_STACK
                    c->h264_idct8_add  = ff_h264_idct8_add_10_sse2;
                    c->h264_idct8_add4 = ff_h264_idct8_add4_10_sse2;
#endif /* HAVE_ALIGNED_STACK */

                    c->weight_h264_pixels_tab[0] = ff_h264_weight_16_10_sse2;
                    c->weight_h264_pixels_tab[1] = ff_h264_weight_8_10_sse2;
                    c->weight_h264_pixels_tab[2] = ff_h264_weight_4_10_sse2;

                    c->biweight_h264_pixels_tab[0] = ff_h264_biweight_16_10_sse2;
                    c->biweight_h264_pixels_tab[1] = ff_h264_biweight_8_10_sse2;
                    c->biweight_h264_pixels_tab[2] = ff_h264_biweight_4_10_sse2;

                    c->h264_v_loop_filter_chroma       = ff_deblock_v_chroma_10_sse2;
                    c->h264_v_loop_filter_chroma_intra = ff_deblock_v_chroma_intra_10_sse2;
#if HAVE_ALIGNED_STACK
                    c->h264_v_loop_filter_luma       = ff_deblock_v_luma_10_sse2;
                    c->h264_h_loop_filter_luma       = ff_deblock_h_luma_10_sse2;
                    c->h264_v_loop_filter_luma_intra = ff_deblock_v_luma_intra_10_sse2;
                    c->h264_h_loop_filter_luma_intra = ff_deblock_h_luma_intra_10_sse2;
#endif /* HAVE_ALIGNED_STACK */
                }
                if (EXTERNAL_SSE4(cpu_flags)) {
                    c->weight_h264_pixels_tab[0] = ff_h264_weight_16_10_sse4;
                    c->weight_h264_pixels_tab[1] = ff_h264_weight_8_10_sse4;
                    c->weight_h264_pixels_tab[2] = ff_h264_weight_4_10_sse4;

                    c->biweight_h264_pixels_tab[0] = ff_h264_biweight_16_10_sse4;
                    c->biweight_h264_pixels_tab[1] = ff_h264_biweight_8_10_sse4;
                    c->biweight_h264_pixels_tab[2] = ff_h264_biweight_4_10_sse4;
                }
                if (EXTERNAL_AVX(cpu_flags)) {
                    c->h264_idct_dc_add  =
                    c->h264_idct_add     = ff_h264_idct_add_10_avx;
                    c->h264_idct8_dc_add = ff_h264_idct8_dc_add_10_avx;

                    c->h264_idct_add16 = ff_h264_idct_add16_10_avx;
                    if (chroma_format_idc == 1)
                        c->h264_idct_add8 = ff_h264_idct_add8_10_avx;
                    c->h264_idct_add16intra = ff_h264_idct_add16intra_10_avx;
#if HAVE_ALIGNED_STACK
                    c->h264_idct8_add  = ff_h264_idct8_add_10_avx;
                    c->h264_idct8_add4 = ff_h264_idct8_add4_10_avx;
#endif /* HAVE_ALIGNED_STACK */

                    c->h264_v_loop_filter_chroma       = ff_deblock_v_chroma_10_avx;
                    c->h264_v_loop_filter_chroma_intra = ff_deblock_v_chroma_intra_10_avx;
#if HAVE_ALIGNED_STACK
                    c->h264_v_loop_filter_luma         = ff_deblock_v_luma_10_avx;
                    c->h264_h_loop_filter_luma         = ff_deblock_h_luma_10_avx;
                    c->h264_v_loop_filter_luma_intra   = ff_deblock_v_luma_intra_10_avx;
                    c->h264_h_loop_filter_luma_intra   = ff_deblock_h_luma_intra_10_avx;
#endif /* HAVE_ALIGNED_STACK */
                }
            }
        }
    }
#endif
}
