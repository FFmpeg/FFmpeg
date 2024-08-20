/*
 * VVC DSP init for x86
 *
 * Copyright (C) 2022-2024 Nuo Mi
 * Copyright (c) 2023-2024 Wu Jianhua
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

#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/vvc/dec.h"
#include "libavcodec/vvc/ctu.h"
#include "libavcodec/vvc/dsp.h"
#include "libavcodec/x86/h26x/h2656dsp.h"

#define PUT_PROTOTYPE(name, depth, opt) \
void ff_vvc_put_ ## name ## _ ## depth ## _##opt(int16_t *dst, const uint8_t *src, ptrdiff_t srcstride, int height, const int8_t *hf, const int8_t *vf, int width);

#define PUT_PROTOTYPES(name, bitd, opt) \
        PUT_PROTOTYPE(name##2,   bitd, opt) \
        PUT_PROTOTYPE(name##4,   bitd, opt) \
        PUT_PROTOTYPE(name##8,   bitd, opt) \
        PUT_PROTOTYPE(name##12,  bitd, opt) \
        PUT_PROTOTYPE(name##16,  bitd, opt) \
        PUT_PROTOTYPE(name##24,  bitd, opt) \
        PUT_PROTOTYPE(name##32,  bitd, opt) \
        PUT_PROTOTYPE(name##48,  bitd, opt) \
        PUT_PROTOTYPE(name##64,  bitd, opt) \
        PUT_PROTOTYPE(name##128, bitd, opt)

#define PUT_BPC_PROTOTYPES(name, opt) \
    PUT_PROTOTYPES(name,  8, opt)     \
    PUT_PROTOTYPES(name, 10, opt)     \
    PUT_PROTOTYPES(name, 12, opt)

#define PUT_TAP_PROTOTYPES(n, opt) \
    PUT_BPC_PROTOTYPES(n##tap_h,  opt) \
    PUT_BPC_PROTOTYPES(n##tap_v,  opt) \
    PUT_BPC_PROTOTYPES(n##tap_hv, opt)

PUT_BPC_PROTOTYPES(pixels, sse4)
PUT_BPC_PROTOTYPES(pixels, avx2)

PUT_TAP_PROTOTYPES(4, sse4)
PUT_TAP_PROTOTYPES(8, sse4)
PUT_TAP_PROTOTYPES(4, avx2)
PUT_TAP_PROTOTYPES(8, avx2)

#define bf(fn, bd,  opt) fn##_##bd##_##opt
#define BF(fn, bpc, opt) fn##_##bpc##bpc_##opt

#define AVG_BPC_PROTOTYPES(bpc, opt)                                                                 \
void BF(ff_vvc_avg, bpc, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                    \
    const int16_t *src0, const int16_t *src1, intptr_t width, intptr_t height, intptr_t pixel_max);  \
void BF(ff_vvc_w_avg, bpc, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                  \
    const int16_t *src0, const int16_t *src1, intptr_t width, intptr_t height,                       \
    intptr_t denom, intptr_t w0, intptr_t w1,  intptr_t o0, intptr_t o1, intptr_t pixel_max);

#define AVG_PROTOTYPES(bd, opt)                                                                      \
void bf(ff_vvc_avg, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                     \
    const int16_t *src0, const int16_t *src1, int width, int height);                                \
void bf(ff_vvc_w_avg, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                   \
    const int16_t *src0, const int16_t *src1, int width, int height,                                 \
    int denom, int w0, int w1, int o0, int o1);

AVG_BPC_PROTOTYPES( 8, avx2)
AVG_BPC_PROTOTYPES(16, avx2)

AVG_PROTOTYPES( 8, avx2)
AVG_PROTOTYPES(10, avx2)
AVG_PROTOTYPES(12, avx2)


#define DMVR_PROTOTYPES(bd, opt)                                                                    \
void ff_vvc_dmvr_##bd##_##opt(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,               \
     int height, intptr_t mx, intptr_t my, int width);                                              \
void ff_vvc_dmvr_h_##bd##_##opt(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,             \
     int height, intptr_t mx, intptr_t my, int width);                                              \
void ff_vvc_dmvr_v_##bd##_##opt(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,             \
     int height, intptr_t mx, intptr_t my, int width);                                              \
void ff_vvc_dmvr_hv_##bd##_##opt(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,            \
     int height, intptr_t mx, intptr_t my, int width);                                              \

DMVR_PROTOTYPES( 8, avx2)
DMVR_PROTOTYPES(10, avx2)
DMVR_PROTOTYPES(12, avx2)

void ff_vvc_apply_bdof_avx2(uint8_t *dst, ptrdiff_t dst_stride,                                     \
    const int16_t *src0, const int16_t *src1, int w, int h, int pixel_max);                         \

#define OF_PROTOTYPES(bd, opt)                                                                      \
static void ff_vvc_apply_bdof_##bd##_##opt(uint8_t *dst, ptrdiff_t dst_stride,                      \
    const int16_t *src0, const int16_t *src1, int w, int h)                                         \
{                                                                                                   \
    ff_vvc_apply_bdof##_##opt(dst, dst_stride, src0, src1, w, h, (1 << bd)  - 1);                   \
}                                                                                                   \

OF_PROTOTYPES( 8, avx2)
OF_PROTOTYPES(10, avx2)
OF_PROTOTYPES(12, avx2)

#define ALF_BPC_PROTOTYPES(bpc, opt)                                                                                     \
void BF(ff_vvc_alf_filter_luma, bpc, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                            \
    const uint8_t *src, ptrdiff_t src_stride, ptrdiff_t width, ptrdiff_t height,                                         \
    const int16_t *filter, const int16_t *clip, ptrdiff_t stride, ptrdiff_t vb_pos, ptrdiff_t pixel_max);                \
void BF(ff_vvc_alf_filter_chroma, bpc, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                          \
    const uint8_t *src, ptrdiff_t src_stride, ptrdiff_t width, ptrdiff_t height,                                         \
    const int16_t *filter, const int16_t *clip, ptrdiff_t stride, ptrdiff_t vb_pos, ptrdiff_t pixel_max);                \
void BF(ff_vvc_alf_classify_grad, bpc, opt)(int *gradient_sum,                                                           \
    const uint8_t *src, ptrdiff_t src_stride, intptr_t width, intptr_t height, intptr_t vb_pos);                         \
void BF(ff_vvc_alf_classify, bpc, opt)(int *class_idx, int *transpose_idx, const int *gradient_sum,                      \
    intptr_t width, intptr_t height, intptr_t vb_pos, intptr_t bit_depth);                                               \

#define ALF_PROTOTYPES(bpc, bd, opt)                                                                                     \
void bf(ff_vvc_alf_filter_luma, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,   \
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos);                                \
void bf(ff_vvc_alf_filter_chroma, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride, \
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos);                                \
void bf(ff_vvc_alf_classify, bd, opt)(int *class_idx, int *transpose_idx,                                                \
    const uint8_t *src, ptrdiff_t src_stride, int width, int height, int vb_pos, int *gradient_tmp);                     \

ALF_BPC_PROTOTYPES(8,  avx2)
ALF_BPC_PROTOTYPES(16, avx2)

ALF_PROTOTYPES(8,  8,  avx2)
ALF_PROTOTYPES(16, 10, avx2)
ALF_PROTOTYPES(16, 12, avx2)

#if ARCH_X86_64
#if HAVE_SSE4_EXTERNAL
#define FW_PUT(name, depth, opt) \
void ff_vvc_put_ ## name ## _ ## depth ## _##opt(int16_t *dst, const uint8_t *src, ptrdiff_t srcstride,        \
                                                 int height, const int8_t *hf, const int8_t *vf, int width)    \
{                                                                                                              \
    ff_h2656_put_## name ## _ ## depth ## _##opt(dst, 2 * MAX_PB_SIZE, src, srcstride, height, hf, vf, width); \
}

#define FW_PUT_TAP(fname, bitd, opt ) \
    FW_PUT(fname##4,   bitd, opt )    \
    FW_PUT(fname##8,   bitd, opt )    \
    FW_PUT(fname##16,  bitd, opt )    \
    FW_PUT(fname##32,  bitd, opt )    \
    FW_PUT(fname##64,  bitd, opt )    \
    FW_PUT(fname##128, bitd, opt )    \

#define FW_PUT_4TAP(fname, bitd, opt) \
    FW_PUT(fname ## 2, bitd, opt)     \
    FW_PUT_TAP(fname,  bitd, opt)

#define FW_PUT_4TAP_SSE4(bitd)       \
    FW_PUT_4TAP(pixels,  bitd, sse4) \
    FW_PUT_4TAP(4tap_h,  bitd, sse4) \
    FW_PUT_4TAP(4tap_v,  bitd, sse4) \
    FW_PUT_4TAP(4tap_hv, bitd, sse4)

#define FW_PUT_8TAP_SSE4(bitd)      \
    FW_PUT_TAP(8tap_h,  bitd, sse4) \
    FW_PUT_TAP(8tap_v,  bitd, sse4) \
    FW_PUT_TAP(8tap_hv, bitd, sse4)

#define FW_PUT_SSE4(bitd)  \
    FW_PUT_4TAP_SSE4(bitd) \
    FW_PUT_8TAP_SSE4(bitd)

FW_PUT_SSE4( 8)
FW_PUT_SSE4(10)
FW_PUT_SSE4(12)
#endif

#if HAVE_AVX2_EXTERNAL
#define FW_PUT_TAP_AVX2(n, bitd)        \
    FW_PUT(n ## tap_h32,   bitd, avx2)  \
    FW_PUT(n ## tap_h64,   bitd, avx2)  \
    FW_PUT(n ## tap_h128,  bitd, avx2)  \
    FW_PUT(n ## tap_v32,   bitd, avx2)  \
    FW_PUT(n ## tap_v64,   bitd, avx2)  \
    FW_PUT(n ## tap_v128,  bitd, avx2)

#define FW_PUT_AVX2(bitd) \
    FW_PUT(pixels32,  bitd, avx2) \
    FW_PUT(pixels64,  bitd, avx2) \
    FW_PUT(pixels128, bitd, avx2) \
    FW_PUT_TAP_AVX2(4, bitd)      \
    FW_PUT_TAP_AVX2(8, bitd)      \

FW_PUT_AVX2( 8)
FW_PUT_AVX2(10)
FW_PUT_AVX2(12)

#define FW_PUT_TAP_16BPC_AVX2(n, bitd) \
    FW_PUT(n ## tap_h16,   bitd, avx2) \
    FW_PUT(n ## tap_v16,   bitd, avx2) \
    FW_PUT(n ## tap_hv16,  bitd, avx2) \
    FW_PUT(n ## tap_hv32,  bitd, avx2) \
    FW_PUT(n ## tap_hv64,  bitd, avx2) \
    FW_PUT(n ## tap_hv128, bitd, avx2)

#define FW_PUT_16BPC_AVX2(bitd)     \
    FW_PUT(pixels16, bitd, avx2)    \
    FW_PUT_TAP_16BPC_AVX2(4, bitd)  \
    FW_PUT_TAP_16BPC_AVX2(8, bitd)

FW_PUT_16BPC_AVX2(10)
FW_PUT_16BPC_AVX2(12)

#define AVG_FUNCS(bpc, bd, opt)                                                                     \
void bf(ff_vvc_avg, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                    \
    const int16_t *src0, const int16_t *src1, int width, int height)                                \
{                                                                                                   \
    BF(ff_vvc_avg, bpc, opt)(dst, dst_stride, src0, src1, width, height, (1 << bd)  - 1);           \
}                                                                                                   \
void bf(ff_vvc_w_avg, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                  \
    const int16_t *src0, const int16_t *src1, int width, int height,                                \
    int denom, int w0, int w1, int o0, int o1)                                                      \
{                                                                                                   \
    BF(ff_vvc_w_avg, bpc, opt)(dst, dst_stride, src0, src1, width, height,                          \
        denom, w0, w1, o0, o1, (1 << bd)  - 1);                                                     \
}

AVG_FUNCS(8,  8,  avx2)
AVG_FUNCS(16, 10, avx2)
AVG_FUNCS(16, 12, avx2)

#define ALF_FUNCS(bpc, bd, opt)                                                                                          \
void bf(ff_vvc_alf_filter_luma, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,   \
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)                                 \
{                                                                                                                        \
    const int param_stride  = (width >> 2) * ALF_NUM_COEFF_LUMA;                                                         \
    BF(ff_vvc_alf_filter_luma, bpc, opt)(dst, dst_stride, src, src_stride, width, height,                                \
        filter, clip, param_stride, vb_pos, (1 << bd)  - 1);                                                             \
}                                                                                                                        \
void bf(ff_vvc_alf_filter_chroma, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride, \
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)                                 \
{                                                                                                                        \
    BF(ff_vvc_alf_filter_chroma, bpc, opt)(dst, dst_stride, src, src_stride, width, height,                              \
        filter, clip, 0, vb_pos,(1 << bd)  - 1);                                                                         \
}                                                                                                                        \
void bf(ff_vvc_alf_classify, bd, opt)(int *class_idx, int *transpose_idx,                                                \
    const uint8_t *src, ptrdiff_t src_stride, int width, int height, int vb_pos, int *gradient_tmp)                      \
{                                                                                                                        \
    BF(ff_vvc_alf_classify_grad, bpc, opt)(gradient_tmp, src, src_stride, width, height, vb_pos);                        \
    BF(ff_vvc_alf_classify, bpc, opt)(class_idx, transpose_idx, gradient_tmp, width, height, vb_pos, bd);                \
}                                                                                                                        \

ALF_FUNCS(8,  8,  avx2)
ALF_FUNCS(16, 10, avx2)
ALF_FUNCS(16, 12, avx2)

#endif

#define PEL_LINK(dst, C, W, idx1, idx2, name, D, opt)                              \
    dst[C][W][idx1][idx2] = ff_vvc_put_## name ## _ ## D ## _##opt;                \
    dst ## _uni[C][W][idx1][idx2] = ff_h2656_put_uni_ ## name ## _ ## D ## _##opt; \

#define MC_TAP_LINKS(pointer, C, my, mx, fname, bitd, opt )          \
    PEL_LINK(pointer, C, 1, my , mx , fname##4 ,  bitd, opt );       \
    PEL_LINK(pointer, C, 2, my , mx , fname##8 ,  bitd, opt );       \
    PEL_LINK(pointer, C, 3, my , mx , fname##16,  bitd, opt );       \
    PEL_LINK(pointer, C, 4, my , mx , fname##32,  bitd, opt );       \
    PEL_LINK(pointer, C, 5, my , mx , fname##64,  bitd, opt );       \
    PEL_LINK(pointer, C, 6, my , mx , fname##128, bitd, opt );

#define MC_8TAP_LINKS(pointer, my, mx, fname, bitd, opt)             \
    MC_TAP_LINKS(pointer, LUMA, my, mx, fname, bitd, opt)

#define MC_8TAP_LINKS_SSE4(bd)                                       \
    MC_8TAP_LINKS(c->inter.put, 0, 0, pixels, bd, sse4);             \
    MC_8TAP_LINKS(c->inter.put, 0, 1, 8tap_h, bd, sse4);             \
    MC_8TAP_LINKS(c->inter.put, 1, 0, 8tap_v, bd, sse4);             \
    MC_8TAP_LINKS(c->inter.put, 1, 1, 8tap_hv, bd, sse4)

#define MC_4TAP_LINKS(pointer, my, mx, fname, bitd, opt)             \
    PEL_LINK(pointer, CHROMA, 0, my , mx , fname##2 ,  bitd, opt );  \
    MC_TAP_LINKS(pointer, CHROMA, my, mx, fname, bitd, opt)          \

#define MC_4TAP_LINKS_SSE4(bd)                                       \
    MC_4TAP_LINKS(c->inter.put, 0, 0, pixels, bd, sse4);             \
    MC_4TAP_LINKS(c->inter.put, 0, 1, 4tap_h, bd, sse4);             \
    MC_4TAP_LINKS(c->inter.put, 1, 0, 4tap_v, bd, sse4);             \
    MC_4TAP_LINKS(c->inter.put, 1, 1, 4tap_hv, bd, sse4)

#define MC_LINK_SSE4(bd)                                             \
    MC_4TAP_LINKS_SSE4(bd)                                           \
    MC_8TAP_LINKS_SSE4(bd)

#define MC_TAP_LINKS_AVX2(C,tap,bd) do {                             \
        PEL_LINK(c->inter.put, C, 4, 0, 0, pixels32,      bd, avx2)  \
        PEL_LINK(c->inter.put, C, 5, 0, 0, pixels64,      bd, avx2)  \
        PEL_LINK(c->inter.put, C, 6, 0, 0, pixels128,     bd, avx2)  \
        PEL_LINK(c->inter.put, C, 4, 0, 1, tap##tap_h32,  bd, avx2)  \
        PEL_LINK(c->inter.put, C, 5, 0, 1, tap##tap_h64,  bd, avx2)  \
        PEL_LINK(c->inter.put, C, 6, 0, 1, tap##tap_h128, bd, avx2)  \
        PEL_LINK(c->inter.put, C, 4, 1, 0, tap##tap_v32,  bd, avx2)  \
        PEL_LINK(c->inter.put, C, 5, 1, 0, tap##tap_v64,  bd, avx2)  \
        PEL_LINK(c->inter.put, C, 6, 1, 0, tap##tap_v128, bd, avx2)  \
    } while (0)

#define MC_LINKS_AVX2(bd)                                            \
    MC_TAP_LINKS_AVX2(LUMA,   8, bd);                                \
    MC_TAP_LINKS_AVX2(CHROMA, 4, bd);

#define MC_TAP_LINKS_16BPC_AVX2(C, tap, bd) do {                     \
        PEL_LINK(c->inter.put, C, 3, 0, 0, pixels16, bd, avx2)       \
        PEL_LINK(c->inter.put, C, 3, 0, 1, tap##tap_h16, bd, avx2)   \
        PEL_LINK(c->inter.put, C, 3, 1, 0, tap##tap_v16, bd, avx2)   \
        PEL_LINK(c->inter.put, C, 3, 1, 1, tap##tap_hv16, bd, avx2)  \
        PEL_LINK(c->inter.put, C, 4, 1, 1, tap##tap_hv32, bd, avx2)  \
        PEL_LINK(c->inter.put, C, 5, 1, 1, tap##tap_hv64, bd, avx2)  \
        PEL_LINK(c->inter.put, C, 6, 1, 1, tap##tap_hv128, bd, avx2) \
    } while (0)

#define MC_LINKS_16BPC_AVX2(bd)                                      \
    MC_TAP_LINKS_16BPC_AVX2(LUMA,   8, bd);                          \
    MC_TAP_LINKS_16BPC_AVX2(CHROMA, 4, bd);

#define AVG_INIT(bd, opt) do {                                       \
    c->inter.avg    = bf(ff_vvc_avg, bd, opt);                       \
    c->inter.w_avg  = bf(ff_vvc_w_avg, bd, opt);                     \
} while (0)

#define DMVR_INIT(bd) do {                                           \
    c->inter.dmvr[0][0]   = ff_vvc_dmvr_##bd##_avx2;                 \
    c->inter.dmvr[0][1]   = ff_vvc_dmvr_h_##bd##_avx2;               \
    c->inter.dmvr[1][0]   = ff_vvc_dmvr_v_##bd##_avx2;               \
    c->inter.dmvr[1][1]   = ff_vvc_dmvr_hv_##bd##_avx2;              \
} while (0)

#define OF_INIT(bd) do {                                             \
    c->inter.apply_bdof   = ff_vvc_apply_bdof_##bd##_avx2;           \
} while (0)

#define ALF_INIT(bd) do {                                            \
    c->alf.filter[LUMA]   = ff_vvc_alf_filter_luma_##bd##_avx2;      \
    c->alf.filter[CHROMA] = ff_vvc_alf_filter_chroma_##bd##_avx2;    \
    c->alf.classify       = ff_vvc_alf_classify_##bd##_avx2;         \
} while (0)

int ff_vvc_sad_avx2(const int16_t *src0, const int16_t *src1, int dx, int dy, int block_w, int block_h);
#define SAD_INIT() c->inter.sad = ff_vvc_sad_avx2
#endif

void ff_vvc_dsp_init_x86(VVCDSPContext *const c, const int bd)
{
#if ARCH_X86_64
    const int cpu_flags = av_get_cpu_flags();

    switch (bd) {
    case 8:
        if (EXTERNAL_SSE4(cpu_flags)) {
            MC_LINK_SSE4(8);
        }
        if (EXTERNAL_AVX2_FAST(cpu_flags)) {
            ALF_INIT(8);
            AVG_INIT(8, avx2);
            MC_LINKS_AVX2(8);
            OF_INIT(8);
            DMVR_INIT(8);
            SAD_INIT();
        }
        break;
    case 10:
        if (EXTERNAL_SSE4(cpu_flags)) {
            MC_LINK_SSE4(10);
        }
        if (EXTERNAL_AVX2_FAST(cpu_flags)) {
            ALF_INIT(10);
            AVG_INIT(10, avx2);
            MC_LINKS_AVX2(10);
            MC_LINKS_16BPC_AVX2(10);
            OF_INIT(10);
            DMVR_INIT(10);
            SAD_INIT();
        }
        break;
    case 12:
        if (EXTERNAL_SSE4(cpu_flags)) {
            MC_LINK_SSE4(12);
        }
        if (EXTERNAL_AVX2_FAST(cpu_flags)) {
            ALF_INIT(12);
            AVG_INIT(12, avx2);
            MC_LINKS_AVX2(12);
            MC_LINKS_16BPC_AVX2(12);
            OF_INIT(12);
            DMVR_INIT(12);
            SAD_INIT();
        }
        break;
    default:
        break;
    }
#endif
}
