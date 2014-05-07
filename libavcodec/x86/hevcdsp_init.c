/*
 * Copyright (c) 2013 Seppo Tomperi
 * Copyright (c) 2013 - 2014 Pierre-Edouard Lepere
 *
 *
 * This file is part of ffmpeg.
 *
 * ffmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ffmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ffmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/get_bits.h" /* required for hevcdsp.h GetBitContext */
#include "libavcodec/hevcdsp.h"
#include "libavcodec/x86/hevcdsp.h"

#if ARCH_X86_64

#define mc_rep_func(name, bitd, step, W) \
void ff_hevc_put_hevc_##name##W##_##bitd##_sse4(int16_t *_dst, ptrdiff_t dststride,                             \
                                                uint8_t *_src, ptrdiff_t _srcstride, int height,                \
                                                intptr_t mx, intptr_t my, int width)                            \
{                                                                                                               \
    int i;                                                                                                      \
    uint8_t *src;                                                                                               \
    int16_t *dst;                                                                                               \
    for (i = 0; i < W; i += step) {                                                                             \
        src  = _src + (i * ((bitd + 7) / 8));                                                                   \
        dst = _dst + i;                                                                                         \
        ff_hevc_put_hevc_##name##step##_##bitd##_sse4(dst, dststride, src, _srcstride, height, mx, my, width);  \
    }                                                                                                           \
}
#define mc_rep_uni_func(name, bitd, step, W) \
void ff_hevc_put_hevc_uni_##name##W##_##bitd##_sse4(uint8_t *_dst, ptrdiff_t dststride,                         \
                                                    uint8_t *_src, ptrdiff_t _srcstride, int height,            \
                                                    intptr_t mx, intptr_t my, int width)                        \
{                                                                                                               \
    int i;                                                                                                      \
    uint8_t *src;                                                                                               \
    uint8_t *dst;                                                                                               \
    for (i = 0; i < W; i += step) {                                                                             \
        src = _src + (i * ((bitd + 7) / 8));                                                                    \
        dst = _dst + (i * ((bitd + 7) / 8));                                                                    \
        ff_hevc_put_hevc_uni_##name##step##_##bitd##_sse4(dst, dststride, src, _srcstride,                      \
                                                          height, mx, my, width);                               \
    }                                                                                                           \
}
#define mc_rep_bi_func(name, bitd, step, W) \
void ff_hevc_put_hevc_bi_##name##W##_##bitd##_sse4(uint8_t *_dst, ptrdiff_t dststride, uint8_t *_src,           \
                                                   ptrdiff_t _srcstride, int16_t* _src2, ptrdiff_t _src2stride, \
                                                   int height, intptr_t mx, intptr_t my, int width)             \
{                                                                                                               \
    int i;                                                                                                      \
    uint8_t  *src;                                                                                              \
    uint8_t  *dst;                                                                                              \
    int16_t  *src2;                                                                                             \
    for (i = 0; i < W ; i += step) {                                                                            \
        src  = _src + (i * ((bitd + 7) / 8));                                                                   \
        dst  = _dst + (i * ((bitd + 7) / 8));                                                                   \
        src2 = _src2 + i;                                                                                       \
        ff_hevc_put_hevc_bi_##name##step##_##bitd##_sse4(dst, dststride, src, _srcstride, src2,                 \
                                                         _src2stride, height, mx, my, width);                   \
    }                                                                                                           \
}

#define mc_rep_funcs(name, bitd, step, W)        \
    mc_rep_func(name, bitd, step, W);            \
    mc_rep_uni_func(name, bitd, step, W);        \
    mc_rep_bi_func(name, bitd, step, W)


mc_rep_funcs(pel_pixels, 8, 16, 64);
mc_rep_funcs(pel_pixels, 8, 16, 48);
mc_rep_funcs(pel_pixels, 8, 16, 32);
mc_rep_funcs(pel_pixels, 8,  8, 24);

mc_rep_funcs(pel_pixels,10,  8, 64);
mc_rep_funcs(pel_pixels,10,  8, 48);
mc_rep_funcs(pel_pixels,10,  8, 32);
mc_rep_funcs(pel_pixels,10,  8, 24);
mc_rep_funcs(pel_pixels,10,  8, 16);
mc_rep_funcs(pel_pixels,10,  4, 12);

mc_rep_funcs(epel_h, 8, 16, 64);
mc_rep_funcs(epel_h, 8, 16, 48);
mc_rep_funcs(epel_h, 8, 16, 32);
mc_rep_funcs(epel_h, 8,  8, 24);
mc_rep_funcs(epel_h,10,  8, 64);
mc_rep_funcs(epel_h,10,  8, 48);
mc_rep_funcs(epel_h,10,  8, 32);
mc_rep_funcs(epel_h,10,  8, 24);
mc_rep_funcs(epel_h,10,  8, 16);
mc_rep_funcs(epel_h,10,  4, 12);
mc_rep_funcs(epel_v, 8, 16, 64);
mc_rep_funcs(epel_v, 8, 16, 48);
mc_rep_funcs(epel_v, 8, 16, 32);
mc_rep_funcs(epel_v, 8,  8, 24);
mc_rep_funcs(epel_v,10,  8, 64);
mc_rep_funcs(epel_v,10,  8, 48);
mc_rep_funcs(epel_v,10,  8, 32);
mc_rep_funcs(epel_v,10,  8, 24);
mc_rep_funcs(epel_v,10,  8, 16);
mc_rep_funcs(epel_v,10,  4, 12);
mc_rep_funcs(epel_hv, 8,  8, 64);
mc_rep_funcs(epel_hv, 8,  8, 48);
mc_rep_funcs(epel_hv, 8,  8, 32);
mc_rep_funcs(epel_hv, 8,  8, 24);
mc_rep_funcs(epel_hv, 8,  8, 16);
mc_rep_funcs(epel_hv, 8,  4, 12);
mc_rep_funcs(epel_hv,10,  8, 64);
mc_rep_funcs(epel_hv,10,  8, 48);
mc_rep_funcs(epel_hv,10,  8, 32);
mc_rep_funcs(epel_hv,10,  8, 24);
mc_rep_funcs(epel_hv,10,  8, 16);
mc_rep_funcs(epel_hv,10,  4, 12);

mc_rep_funcs(qpel_h, 8, 16, 64);
mc_rep_funcs(qpel_h, 8, 16, 48);
mc_rep_funcs(qpel_h, 8, 16, 32);
mc_rep_funcs(qpel_h, 8,  8, 24);
mc_rep_funcs(qpel_h,10,  8, 64);
mc_rep_funcs(qpel_h,10,  8, 48);
mc_rep_funcs(qpel_h,10,  8, 32);
mc_rep_funcs(qpel_h,10,  8, 24);
mc_rep_funcs(qpel_h,10,  8, 16);
mc_rep_funcs(qpel_h,10,  4, 12);
mc_rep_funcs(qpel_v, 8, 16, 64);
mc_rep_funcs(qpel_v, 8, 16, 48);
mc_rep_funcs(qpel_v, 8, 16, 32);
mc_rep_funcs(qpel_v, 8,  8, 24);
mc_rep_funcs(qpel_v,10,  8, 64);
mc_rep_funcs(qpel_v,10,  8, 48);
mc_rep_funcs(qpel_v,10,  8, 32);
mc_rep_funcs(qpel_v,10,  8, 24);
mc_rep_funcs(qpel_v,10,  8, 16);
mc_rep_funcs(qpel_v,10,  4, 12);
mc_rep_funcs(qpel_hv, 8,  8, 64);
mc_rep_funcs(qpel_hv, 8,  8, 48);
mc_rep_funcs(qpel_hv, 8,  8, 32);
mc_rep_funcs(qpel_hv, 8,  8, 24);
mc_rep_funcs(qpel_hv, 8,  8, 16);
mc_rep_funcs(qpel_hv, 8,  4, 12);
mc_rep_funcs(qpel_hv,10,  8, 64);
mc_rep_funcs(qpel_hv,10,  8, 48);
mc_rep_funcs(qpel_hv,10,  8, 32);
mc_rep_funcs(qpel_hv,10,  8, 24);
mc_rep_funcs(qpel_hv,10,  8, 16);
mc_rep_funcs(qpel_hv,10,  4, 12);

#define mc_rep_uni_w(bitd, step, W) \
void ff_hevc_put_hevc_uni_w##W##_##bitd##_sse4(uint8_t *_dst, ptrdiff_t dststride, int16_t *_src, ptrdiff_t _srcstride, \
                                               int height, int denom,  int _wx, int _ox)                                \
{                                                                                                                       \
    int i;                                                                                                              \
    int16_t *src;                                                                                                       \
    uint8_t *dst;                                                                                                       \
    for (i = 0; i < W; i += step) {                                                                                     \
        src= _src + i;                                                                                                  \
        dst= _dst + (i * ((bitd + 7) / 8));                                                                             \
        ff_hevc_put_hevc_uni_w##step##_##bitd##_sse4(dst, dststride, src, _srcstride,                                   \
                                                     height, denom, _wx, _ox);                                          \
    }                                                                                                                   \
}

mc_rep_uni_w(8, 6, 12);
mc_rep_uni_w(8, 8, 16);
mc_rep_uni_w(8, 8, 24);
mc_rep_uni_w(8, 8, 32);
mc_rep_uni_w(8, 8, 48);
mc_rep_uni_w(8, 8, 64);

mc_rep_uni_w(10, 6, 12);
mc_rep_uni_w(10, 8, 16);
mc_rep_uni_w(10, 8, 24);
mc_rep_uni_w(10, 8, 32);
mc_rep_uni_w(10, 8, 48);
mc_rep_uni_w(10, 8, 64);

#define mc_rep_bi_w(bitd, step, W) \
void ff_hevc_put_hevc_bi_w##W##_##bitd##_sse4(uint8_t *_dst, ptrdiff_t dststride, int16_t *_src, ptrdiff_t _srcstride,  \
                                              int16_t *_src2, ptrdiff_t _src2stride, int height,                        \
                                              int denom,  int _wx0,  int _wx1, int _ox0, int _ox1)                      \
{                                                                                                                       \
    int i;                                                                                                              \
    int16_t *src;                                                                                                       \
    int16_t *src2;                                                                                                      \
    uint8_t *dst;                                                                                                       \
    for (i = 0; i < W; i += step) {                                                                                     \
        src  = _src  + i;                                                                                               \
        src2 = _src2 + i;                                                                                               \
        dst  = _dst  + (i * ((bitd + 7) / 8));                                                                          \
        ff_hevc_put_hevc_bi_w##step##_##bitd##_sse4(dst, dststride, src, _srcstride, src2, _src2stride,                 \
                                                    height, denom, _wx0, _wx1, _ox0, _ox1);                             \
    }                                                                                                                   \
}

mc_rep_bi_w(8, 6, 12);
mc_rep_bi_w(8, 8, 16);
mc_rep_bi_w(8, 8, 24);
mc_rep_bi_w(8, 8, 32);
mc_rep_bi_w(8, 8, 48);
mc_rep_bi_w(8, 8, 64);

mc_rep_bi_w(10, 6, 12);
mc_rep_bi_w(10, 8, 16);
mc_rep_bi_w(10, 8, 24);
mc_rep_bi_w(10, 8, 32);
mc_rep_bi_w(10, 8, 48);
mc_rep_bi_w(10, 8, 64);

#define mc_uni_w_func(name, bitd, W) \
void ff_hevc_put_hevc_uni_w_##name##W##_##bitd##_sse4(uint8_t *_dst, ptrdiff_t _dststride,          \
                                                      uint8_t *_src, ptrdiff_t _srcstride,          \
                                                      int height, int denom,                        \
                                                      int _wx, int _ox,                             \
                                                      intptr_t mx, intptr_t my, int width)          \
{                                                                                                   \
    LOCAL_ALIGNED_16(int16_t, temp, [71 * 64]);                                                     \
    ff_hevc_put_hevc_##name##W##_##bitd##_sse4(temp, 64, _src, _srcstride, height, mx, my, width);  \
    ff_hevc_put_hevc_uni_w##W##_##bitd##_sse4(_dst, _dststride, temp, 64, height, denom, _wx, _ox); \
}

#define mc_uni_w_funcs(name, bitd)       \
        mc_uni_w_func(name, bitd, 4);    \
        mc_uni_w_func(name, bitd, 8);    \
        mc_uni_w_func(name, bitd, 12);   \
        mc_uni_w_func(name, bitd, 16);   \
        mc_uni_w_func(name, bitd, 24);   \
        mc_uni_w_func(name, bitd, 32);   \
        mc_uni_w_func(name, bitd, 48);   \
        mc_uni_w_func(name, bitd, 64)

mc_uni_w_funcs(pel_pixels, 8);
mc_uni_w_func(pel_pixels, 8, 6);
mc_uni_w_funcs(epel_h, 8);
mc_uni_w_func(epel_h, 8, 6);
mc_uni_w_funcs(epel_v, 8);
mc_uni_w_func(epel_v, 8, 6);
mc_uni_w_funcs(epel_hv, 8);
mc_uni_w_func(epel_hv, 8, 6);
mc_uni_w_funcs(qpel_h, 8);
mc_uni_w_funcs(qpel_v, 8);
mc_uni_w_funcs(qpel_hv, 8);

mc_uni_w_funcs(pel_pixels, 10);
mc_uni_w_func(pel_pixels, 10, 6);
mc_uni_w_funcs(epel_h, 10);
mc_uni_w_func(epel_h, 10, 6);
mc_uni_w_funcs(epel_v, 10);
mc_uni_w_func(epel_v, 10, 6);
mc_uni_w_funcs(epel_hv, 10);
mc_uni_w_func(epel_hv, 10, 6);
mc_uni_w_funcs(qpel_h, 10);
mc_uni_w_funcs(qpel_v, 10);
mc_uni_w_funcs(qpel_hv, 10);


#define mc_bi_w_func(name, bitd, W) \
void ff_hevc_put_hevc_bi_w_##name##W##_##bitd##_sse4(uint8_t *_dst, ptrdiff_t _dststride,            \
                                                     uint8_t *_src, ptrdiff_t _srcstride,            \
                                                     int16_t *_src2, ptrdiff_t _src2stride,          \
                                                     int height, int denom,                          \
                                                     int _wx0, int _wx1, int _ox0, int _ox1,         \
                                                     intptr_t mx, intptr_t my, int width)            \
{                                                                                                    \
    LOCAL_ALIGNED_16(int16_t, temp, [71 * 64]);                                                      \
    ff_hevc_put_hevc_##name##W##_##bitd##_sse4(temp, 64, _src, _srcstride, height, mx, my, width);   \
    ff_hevc_put_hevc_bi_w##W##_##bitd##_sse4(_dst, _dststride, temp, 64, _src2, _src2stride,         \
                                             height, denom, _wx0, _wx1, _ox0, _ox1);                 \
}

#define mc_bi_w_funcs(name, bitd)       \
        mc_bi_w_func(name, bitd, 4);    \
        mc_bi_w_func(name, bitd, 8);    \
        mc_bi_w_func(name, bitd, 12);   \
        mc_bi_w_func(name, bitd, 16);   \
        mc_bi_w_func(name, bitd, 24);   \
        mc_bi_w_func(name, bitd, 32);   \
        mc_bi_w_func(name, bitd, 48);   \
        mc_bi_w_func(name, bitd, 64)

mc_bi_w_funcs(pel_pixels, 8);
mc_bi_w_func(pel_pixels, 8, 6);
mc_bi_w_funcs(epel_h, 8);
mc_bi_w_func(epel_h, 8, 6);
mc_bi_w_funcs(epel_v, 8);
mc_bi_w_func(epel_v, 8, 6);
mc_bi_w_funcs(epel_hv, 8);
mc_bi_w_func(epel_hv, 8, 6);
mc_bi_w_funcs(qpel_h, 8);
mc_bi_w_funcs(qpel_v, 8);
mc_bi_w_funcs(qpel_hv, 8);

mc_bi_w_funcs(pel_pixels, 10);
mc_bi_w_func(pel_pixels, 10, 6);
mc_bi_w_funcs(epel_h, 10);
mc_bi_w_func(epel_h, 10, 6);
mc_bi_w_funcs(epel_v, 10);
mc_bi_w_func(epel_v, 10, 6);
mc_bi_w_funcs(epel_hv, 10);
mc_bi_w_func(epel_hv, 10, 6);
mc_bi_w_funcs(qpel_h, 10);
mc_bi_w_funcs(qpel_v, 10);
mc_bi_w_funcs(qpel_hv, 10);

#endif //ARCH_X86_64


#define EPEL_LINKS(pointer, my, mx, fname, bitd)           \
        PEL_LINK(pointer, 1, my , mx , fname##4 ,  bitd ); \
        PEL_LINK(pointer, 2, my , mx , fname##6 ,  bitd ); \
        PEL_LINK(pointer, 3, my , mx , fname##8 ,  bitd ); \
        PEL_LINK(pointer, 4, my , mx , fname##12,  bitd ); \
        PEL_LINK(pointer, 5, my , mx , fname##16,  bitd ); \
        PEL_LINK(pointer, 6, my , mx , fname##24,  bitd ); \
        PEL_LINK(pointer, 7, my , mx , fname##32,  bitd ); \
        PEL_LINK(pointer, 8, my , mx , fname##48,  bitd ); \
        PEL_LINK(pointer, 9, my , mx , fname##64,  bitd )
#define QPEL_LINKS(pointer, my, mx, fname, bitd)           \
        PEL_LINK(pointer, 1, my , mx , fname##4 ,  bitd ); \
        PEL_LINK(pointer, 3, my , mx , fname##8 ,  bitd ); \
        PEL_LINK(pointer, 4, my , mx , fname##12,  bitd ); \
        PEL_LINK(pointer, 5, my , mx , fname##16,  bitd ); \
        PEL_LINK(pointer, 6, my , mx , fname##24,  bitd ); \
        PEL_LINK(pointer, 7, my , mx , fname##32,  bitd ); \
        PEL_LINK(pointer, 8, my , mx , fname##48,  bitd ); \
        PEL_LINK(pointer, 9, my , mx , fname##64,  bitd )


void ff_hevcdsp_init_x86(HEVCDSPContext *c, const int bit_depth)
{
    int mm_flags = av_get_cpu_flags();

    if (bit_depth == 8) {
        if (EXTERNAL_SSE4(mm_flags) && ARCH_X86_64) {

            EPEL_LINKS(c->put_hevc_epel, 0, 0, pel_pixels,  8);
            EPEL_LINKS(c->put_hevc_epel, 0, 1, epel_h,      8);
            EPEL_LINKS(c->put_hevc_epel, 1, 0, epel_v,      8);
            EPEL_LINKS(c->put_hevc_epel, 1, 1, epel_hv,     8);

            QPEL_LINKS(c->put_hevc_qpel, 0, 0, pel_pixels, 8);
            QPEL_LINKS(c->put_hevc_qpel, 0, 1, qpel_h,     8);
            QPEL_LINKS(c->put_hevc_qpel, 1, 0, qpel_v,     8);
            QPEL_LINKS(c->put_hevc_qpel, 1, 1, qpel_hv,    8);

        }
    } else if (bit_depth == 10) {
        if (EXTERNAL_SSE4(mm_flags) && ARCH_X86_64) {

            EPEL_LINKS(c->put_hevc_epel, 0, 0, pel_pixels, 10);
            EPEL_LINKS(c->put_hevc_epel, 0, 1, epel_h,     10);
            EPEL_LINKS(c->put_hevc_epel, 1, 0, epel_v,     10);
            EPEL_LINKS(c->put_hevc_epel, 1, 1, epel_hv,    10);

            QPEL_LINKS(c->put_hevc_qpel, 0, 0, pel_pixels, 10);
            QPEL_LINKS(c->put_hevc_qpel, 0, 1, qpel_h,     10);
            QPEL_LINKS(c->put_hevc_qpel, 1, 0, qpel_v,     10);
            QPEL_LINKS(c->put_hevc_qpel, 1, 1, qpel_hv,    10);
        }
    }
}
