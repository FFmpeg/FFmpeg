/*
 * Copyright (C) 2010 David Conrad
 * Copyright (C) 2010 Ronald S. Bultje
 * Copyright (C) 2014 Peter Ross
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

/**
 * @file
 * VP8 compatible video decoder
 */

#include "config_components.h"

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

#include "mathops.h"
#include "vp8dsp.h"

#define MK_IDCT_DC_ADD4_C(name)                                               \
static void name ## _idct_dc_add4uv_c(uint8_t *dst, int16_t block[4][16],     \
                                      ptrdiff_t stride)                       \
{                                                                             \
    name ## _idct_dc_add_c(dst + stride * 0 + 0, block[0], stride);           \
    name ## _idct_dc_add_c(dst + stride * 0 + 4, block[1], stride);           \
    name ## _idct_dc_add_c(dst + stride * 4 + 0, block[2], stride);           \
    name ## _idct_dc_add_c(dst + stride * 4 + 4, block[3], stride);           \
}                                                                             \
                                                                              \
static void name ## _idct_dc_add4y_c(uint8_t *dst, int16_t block[4][16],      \
                                     ptrdiff_t stride)                        \
{                                                                             \
    name ## _idct_dc_add_c(dst +  0, block[0], stride);                       \
    name ## _idct_dc_add_c(dst +  4, block[1], stride);                       \
    name ## _idct_dc_add_c(dst +  8, block[2], stride);                       \
    name ## _idct_dc_add_c(dst + 12, block[3], stride);                       \
}

#if CONFIG_VP7_DECODER
static void vp7_luma_dc_wht_c(int16_t block[4][4][16], int16_t dc[16])
{
    int i;
    unsigned a1, b1, c1, d1;
    int16_t tmp[16];

    for (i = 0; i < 4; i++) {
        a1 = (dc[i * 4 + 0] + dc[i * 4 + 2]) * 23170;
        b1 = (dc[i * 4 + 0] - dc[i * 4 + 2]) * 23170;
        c1 = dc[i * 4 + 1] * 12540 - dc[i * 4 + 3] * 30274;
        d1 = dc[i * 4 + 1] * 30274 + dc[i * 4 + 3] * 12540;
        tmp[i * 4 + 0] = (int)(a1 + d1) >> 14;
        tmp[i * 4 + 3] = (int)(a1 - d1) >> 14;
        tmp[i * 4 + 1] = (int)(b1 + c1) >> 14;
        tmp[i * 4 + 2] = (int)(b1 - c1) >> 14;
    }

    for (i = 0; i < 4; i++) {
        a1 = (tmp[i + 0] + tmp[i + 8]) * 23170;
        b1 = (tmp[i + 0] - tmp[i + 8]) * 23170;
        c1 = tmp[i + 4] * 12540 - tmp[i + 12] * 30274;
        d1 = tmp[i + 4] * 30274 + tmp[i + 12] * 12540;
        AV_ZERO64(dc + i * 4);
        block[0][i][0] = (int)(a1 + d1 + 0x20000) >> 18;
        block[3][i][0] = (int)(a1 - d1 + 0x20000) >> 18;
        block[1][i][0] = (int)(b1 + c1 + 0x20000) >> 18;
        block[2][i][0] = (int)(b1 - c1 + 0x20000) >> 18;
    }
}

static void vp7_luma_dc_wht_dc_c(int16_t block[4][4][16], int16_t dc[16])
{
    int i, val = (23170 * (23170 * dc[0] >> 14) + 0x20000) >> 18;
    dc[0] = 0;

    for (i = 0; i < 4; i++) {
        block[i][0][0] = val;
        block[i][1][0] = val;
        block[i][2][0] = val;
        block[i][3][0] = val;
    }
}

static void vp7_idct_add_c(uint8_t *dst, int16_t block[16], ptrdiff_t stride)
{
    int i;
    unsigned a1, b1, c1, d1;
    int16_t tmp[16];

    for (i = 0; i < 4; i++) {
        a1 = (block[i * 4 + 0] + block[i * 4 + 2]) * 23170;
        b1 = (block[i * 4 + 0] - block[i * 4 + 2]) * 23170;
        c1 = block[i * 4 + 1] * 12540 - block[i * 4 + 3] * 30274;
        d1 = block[i * 4 + 1] * 30274 + block[i * 4 + 3] * 12540;
        AV_ZERO64(block + i * 4);
        tmp[i * 4 + 0] = (int)(a1 + d1) >> 14;
        tmp[i * 4 + 3] = (int)(a1 - d1) >> 14;
        tmp[i * 4 + 1] = (int)(b1 + c1) >> 14;
        tmp[i * 4 + 2] = (int)(b1 - c1) >> 14;
    }

    for (i = 0; i < 4; i++) {
        a1 = (tmp[i + 0] + tmp[i + 8]) * 23170;
        b1 = (tmp[i + 0] - tmp[i + 8]) * 23170;
        c1 = tmp[i + 4] * 12540 - tmp[i + 12] * 30274;
        d1 = tmp[i + 4] * 30274 + tmp[i + 12] * 12540;
        dst[0 * stride + i] = av_clip_uint8(dst[0 * stride + i] +
                                            ((int)(a1 + d1 + 0x20000) >> 18));
        dst[3 * stride + i] = av_clip_uint8(dst[3 * stride + i] +
                                            ((int)(a1 - d1 + 0x20000) >> 18));
        dst[1 * stride + i] = av_clip_uint8(dst[1 * stride + i] +
                                            ((int)(b1 + c1 + 0x20000) >> 18));
        dst[2 * stride + i] = av_clip_uint8(dst[2 * stride + i] +
                                            ((int)(b1 - c1 + 0x20000) >> 18));
    }
}

static void vp7_idct_dc_add_c(uint8_t *dst, int16_t block[16], ptrdiff_t stride)
{
    int i, dc = (23170 * (23170 * block[0] >> 14) + 0x20000) >> 18;
    block[0] = 0;

    for (i = 0; i < 4; i++) {
        dst[0] = av_clip_uint8(dst[0] + dc);
        dst[1] = av_clip_uint8(dst[1] + dc);
        dst[2] = av_clip_uint8(dst[2] + dc);
        dst[3] = av_clip_uint8(dst[3] + dc);
        dst   += stride;
    }
}

MK_IDCT_DC_ADD4_C(vp7)
#endif /* CONFIG_VP7_DECODER */

// TODO: Maybe add dequant
#if CONFIG_VP8_DECODER
static void vp8_luma_dc_wht_c(int16_t block[4][4][16], int16_t dc[16])
{
    int i, t0, t1, t2, t3;

    for (i = 0; i < 4; i++) {
        t0 = dc[0 * 4 + i] + dc[3 * 4 + i];
        t1 = dc[1 * 4 + i] + dc[2 * 4 + i];
        t2 = dc[1 * 4 + i] - dc[2 * 4 + i];
        t3 = dc[0 * 4 + i] - dc[3 * 4 + i];

        dc[0 * 4 + i] = t0 + t1;
        dc[1 * 4 + i] = t3 + t2;
        dc[2 * 4 + i] = t0 - t1;
        dc[3 * 4 + i] = t3 - t2;
    }

    for (i = 0; i < 4; i++) {
        t0 = dc[i * 4 + 0] + dc[i * 4 + 3] + 3; // rounding
        t1 = dc[i * 4 + 1] + dc[i * 4 + 2];
        t2 = dc[i * 4 + 1] - dc[i * 4 + 2];
        t3 = dc[i * 4 + 0] - dc[i * 4 + 3] + 3; // rounding
        AV_ZERO64(dc + i * 4);

        block[i][0][0] = (t0 + t1) >> 3;
        block[i][1][0] = (t3 + t2) >> 3;
        block[i][2][0] = (t0 - t1) >> 3;
        block[i][3][0] = (t3 - t2) >> 3;
    }
}

static void vp8_luma_dc_wht_dc_c(int16_t block[4][4][16], int16_t dc[16])
{
    int i, val = (dc[0] + 3) >> 3;
    dc[0] = 0;

    for (i = 0; i < 4; i++) {
        block[i][0][0] = val;
        block[i][1][0] = val;
        block[i][2][0] = val;
        block[i][3][0] = val;
    }
}

#define MUL_20091(a) ((((a) * 20091) >> 16) + (a))
#define MUL_35468(a)  (((a) * 35468) >> 16)

static void vp8_idct_add_c(uint8_t *dst, int16_t block[16], ptrdiff_t stride)
{
    int i, t0, t1, t2, t3;
    int16_t tmp[16];

    for (i = 0; i < 4; i++) {
        t0 = block[0 * 4 + i] + block[2 * 4 + i];
        t1 = block[0 * 4 + i] - block[2 * 4 + i];
        t2 = MUL_35468(block[1 * 4 + i]) - MUL_20091(block[3 * 4 + i]);
        t3 = MUL_20091(block[1 * 4 + i]) + MUL_35468(block[3 * 4 + i]);
        block[0 * 4 + i] = 0;
        block[1 * 4 + i] = 0;
        block[2 * 4 + i] = 0;
        block[3 * 4 + i] = 0;

        tmp[i * 4 + 0] = t0 + t3;
        tmp[i * 4 + 1] = t1 + t2;
        tmp[i * 4 + 2] = t1 - t2;
        tmp[i * 4 + 3] = t0 - t3;
    }

    for (i = 0; i < 4; i++) {
        t0 = tmp[0 * 4 + i] + tmp[2 * 4 + i];
        t1 = tmp[0 * 4 + i] - tmp[2 * 4 + i];
        t2 = MUL_35468(tmp[1 * 4 + i]) - MUL_20091(tmp[3 * 4 + i]);
        t3 = MUL_20091(tmp[1 * 4 + i]) + MUL_35468(tmp[3 * 4 + i]);

        dst[0] = av_clip_uint8(dst[0] + ((t0 + t3 + 4) >> 3));
        dst[1] = av_clip_uint8(dst[1] + ((t1 + t2 + 4) >> 3));
        dst[2] = av_clip_uint8(dst[2] + ((t1 - t2 + 4) >> 3));
        dst[3] = av_clip_uint8(dst[3] + ((t0 - t3 + 4) >> 3));
        dst   += stride;
    }
}

static void vp8_idct_dc_add_c(uint8_t *dst, int16_t block[16], ptrdiff_t stride)
{
    int i, dc = (block[0] + 4) >> 3;
    block[0] = 0;

    for (i = 0; i < 4; i++) {
        dst[0] = av_clip_uint8(dst[0] + dc);
        dst[1] = av_clip_uint8(dst[1] + dc);
        dst[2] = av_clip_uint8(dst[2] + dc);
        dst[3] = av_clip_uint8(dst[3] + dc);
        dst   += stride;
    }
}

MK_IDCT_DC_ADD4_C(vp8)
#endif /* CONFIG_VP8_DECODER */

// because I like only having two parameters to pass functions...
#define LOAD_PIXELS                                                           \
    int av_unused p3 = p[-4 * stride];                                        \
    int av_unused p2 = p[-3 * stride];                                        \
    int av_unused p1 = p[-2 * stride];                                        \
    int av_unused p0 = p[-1 * stride];                                        \
    int av_unused q0 = p[ 0 * stride];                                        \
    int av_unused q1 = p[ 1 * stride];                                        \
    int av_unused q2 = p[ 2 * stride];                                        \
    int av_unused q3 = p[ 3 * stride];

#define clip_int8(n) (cm[(n) + 0x80] - 0x80)

static av_always_inline void filter_common(uint8_t *p, ptrdiff_t stride,
                                           int is4tap, int is_vp7)
{
    LOAD_PIXELS
    int a, f1, f2;
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;

    a = 3 * (q0 - p0);

    if (is4tap)
        a += clip_int8(p1 - q1);

    a = clip_int8(a);

    // We deviate from the spec here with c(a+3) >> 3
    // since that's what libvpx does.
    f1 = FFMIN(a + 4, 127) >> 3;

    if (is_vp7)
        f2 = f1 - ((a & 7) == 4);
    else
        f2 = FFMIN(a + 3, 127) >> 3;

    // Despite what the spec says, we do need to clamp here to
    // be bitexact with libvpx.
    p[-1 * stride] = cm[p0 + f2];
    p[ 0 * stride] = cm[q0 - f1];

    // only used for _inner on blocks without high edge variance
    if (!is4tap) {
        a              = (f1 + 1) >> 1;
        p[-2 * stride] = cm[p1 + a];
        p[ 1 * stride] = cm[q1 - a];
    }
}

static av_always_inline void vp7_filter_common(uint8_t *p, ptrdiff_t stride,
                                               int is4tap)
{
    filter_common(p, stride, is4tap, IS_VP7);
}

static av_always_inline void vp8_filter_common(uint8_t *p, ptrdiff_t stride,
                                               int is4tap)
{
    filter_common(p, stride, is4tap, IS_VP8);
}

static av_always_inline int vp7_simple_limit(uint8_t *p, ptrdiff_t stride,
                                             int flim)
{
    LOAD_PIXELS
    return FFABS(p0 - q0) <= flim;
}

static av_always_inline int vp8_simple_limit(uint8_t *p, ptrdiff_t stride,
                                             int flim)
{
    LOAD_PIXELS
    return 2 * FFABS(p0 - q0) + (FFABS(p1 - q1) >> 1) <= flim;
}

/**
 * E - limit at the macroblock edge
 * I - limit for interior difference
 */
#define NORMAL_LIMIT(vpn)                                                     \
static av_always_inline int vp ## vpn ## _normal_limit(uint8_t *p,            \
                                                       ptrdiff_t stride,      \
                                                       int E, int I)          \
{                                                                             \
    LOAD_PIXELS                                                               \
    return vp ## vpn ## _simple_limit(p, stride, E) &&                        \
           FFABS(p3 - p2) <= I && FFABS(p2 - p1) <= I &&                      \
           FFABS(p1 - p0) <= I && FFABS(q3 - q2) <= I &&                      \
           FFABS(q2 - q1) <= I && FFABS(q1 - q0) <= I;                        \
}

NORMAL_LIMIT(7)
NORMAL_LIMIT(8)

// high edge variance
static av_always_inline int hev(uint8_t *p, ptrdiff_t stride, int thresh)
{
    LOAD_PIXELS
    return FFABS(p1 - p0) > thresh || FFABS(q1 - q0) > thresh;
}

static av_always_inline void filter_mbedge(uint8_t *p, ptrdiff_t stride)
{
    int a0, a1, a2, w;
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;

    LOAD_PIXELS

    w = clip_int8(p1 - q1);
    w = clip_int8(w + 3 * (q0 - p0));

    a0 = (27 * w + 63) >> 7;
    a1 = (18 * w + 63) >> 7;
    a2 =  (9 * w + 63) >> 7;

    p[-3 * stride] = cm[p2 + a2];
    p[-2 * stride] = cm[p1 + a1];
    p[-1 * stride] = cm[p0 + a0];
    p[ 0 * stride] = cm[q0 - a0];
    p[ 1 * stride] = cm[q1 - a1];
    p[ 2 * stride] = cm[q2 - a2];
}

#define LOOP_FILTER(vpn, dir, size, stridea, strideb, maybe_inline)           \
static maybe_inline                                                           \
void vpn ## _ ## dir ## _loop_filter ## size ## _c(uint8_t *dst,              \
                                                   ptrdiff_t stride,          \
                                                   int flim_E, int flim_I,    \
                                                   int hev_thresh)            \
{                                                                             \
    int i;                                                                    \
    for (i = 0; i < size; i++)                                                \
        if (vpn ## _normal_limit(dst + i * stridea, strideb,                  \
                                 flim_E, flim_I)) {                           \
            if (hev(dst + i * stridea, strideb, hev_thresh))                  \
                vpn ## _filter_common(dst + i * stridea, strideb, 1);         \
            else                                                              \
                filter_mbedge(dst + i * stridea, strideb);                    \
        }                                                                     \
}                                                                             \
                                                                              \
static maybe_inline                                                           \
void vpn ## _ ## dir ## _loop_filter ## size ## _inner_c(uint8_t *dst,        \
                                                         ptrdiff_t stride,    \
                                                         int flim_E,          \
                                                         int flim_I,          \
                                                         int hev_thresh)      \
{                                                                             \
    int i;                                                                    \
    for (i = 0; i < size; i++)                                                \
        if (vpn ## _normal_limit(dst + i * stridea, strideb,                  \
                                 flim_E, flim_I)) {                           \
            int hv = hev(dst + i * stridea, strideb, hev_thresh);             \
            if (hv)                                                           \
                vpn ## _filter_common(dst + i * stridea, strideb, 1);         \
            else                                                              \
                vpn ## _filter_common(dst + i * stridea, strideb, 0);         \
        }                                                                     \
}

#define UV_LOOP_FILTER(vpn, dir, stridea, strideb)                            \
LOOP_FILTER(vpn, dir, 8, stridea, strideb, av_always_inline)                  \
static void vpn ## _ ## dir ## _loop_filter8uv_c(uint8_t *dstU,               \
                                                 uint8_t *dstV,               \
                                                 ptrdiff_t stride, int fE,    \
                                                 int fI, int hev_thresh)      \
{                                                                             \
    vpn ## _ ## dir ## _loop_filter8_c(dstU, stride, fE, fI, hev_thresh);     \
    vpn ## _ ## dir ## _loop_filter8_c(dstV, stride, fE, fI, hev_thresh);     \
}                                                                             \
                                                                              \
static void vpn ## _ ## dir ## _loop_filter8uv_inner_c(uint8_t *dstU,         \
                                                       uint8_t *dstV,         \
                                                       ptrdiff_t stride,      \
                                                       int fE, int fI,        \
                                                       int hev_thresh)        \
{                                                                             \
    vpn ## _ ## dir ## _loop_filter8_inner_c(dstU, stride, fE, fI,            \
                                             hev_thresh);                     \
    vpn ## _ ## dir ## _loop_filter8_inner_c(dstV, stride, fE, fI,            \
                                             hev_thresh);                     \
}

#define LOOP_FILTER_SIMPLE(vpn)                                               \
static void vpn ## _v_loop_filter_simple_c(uint8_t *dst, ptrdiff_t stride,    \
                                           int flim)                          \
{                                                                             \
    int i;                                                                    \
    for (i = 0; i < 16; i++)                                                  \
        if (vpn ## _simple_limit(dst + i, stride, flim))                      \
            vpn ## _filter_common(dst + i, stride, 1);                        \
}                                                                             \
                                                                              \
static void vpn ## _h_loop_filter_simple_c(uint8_t *dst, ptrdiff_t stride,    \
                                           int flim)                          \
{                                                                             \
    int i;                                                                    \
    for (i = 0; i < 16; i++)                                                  \
        if (vpn ## _simple_limit(dst + i * stride, 1, flim))                  \
            vpn ## _filter_common(dst + i * stride, 1, 1);                    \
}

#define LOOP_FILTERS(vpn)                \
    LOOP_FILTER(vpn, v, 16, 1, stride, ) \
    LOOP_FILTER(vpn, h, 16, stride, 1, ) \
    UV_LOOP_FILTER(vpn, v, 1, stride)    \
    UV_LOOP_FILTER(vpn, h, stride, 1)    \
    LOOP_FILTER_SIMPLE(vpn)              \

static const uint8_t subpel_filters[7][6] = {
    { 0,  6, 123,  12,  1, 0 },
    { 2, 11, 108,  36,  8, 1 },
    { 0,  9,  93,  50,  6, 0 },
    { 3, 16,  77,  77, 16, 3 },
    { 0,  6,  50,  93,  9, 0 },
    { 1,  8,  36, 108, 11, 2 },
    { 0,  1,  12, 123,  6, 0 },
};

#define PUT_PIXELS(WIDTH)                                                     \
static void put_vp8_pixels ## WIDTH ## _c(uint8_t *dst, ptrdiff_t dststride,  \
                                          uint8_t *src, ptrdiff_t srcstride,  \
                                          int h, int x, int y)                \
{                                                                             \
    int i;                                                                    \
    for (i = 0; i < h; i++, dst += dststride, src += srcstride)               \
        memcpy(dst, src, WIDTH);                                              \
}

PUT_PIXELS(16)
PUT_PIXELS(8)
PUT_PIXELS(4)

#define FILTER_6TAP(src, F, stride)                                           \
    cm[(F[2] * src[x + 0 * stride] - F[1] * src[x - 1 * stride] +             \
        F[0] * src[x - 2 * stride] + F[3] * src[x + 1 * stride] -             \
        F[4] * src[x + 2 * stride] + F[5] * src[x + 3 * stride] + 64) >> 7]

#define FILTER_4TAP(src, F, stride)                                           \
    cm[(F[2] * src[x + 0 * stride] - F[1] * src[x - 1 * stride] +             \
        F[3] * src[x + 1 * stride] - F[4] * src[x + 2 * stride] + 64) >> 7]

#define VP8_EPEL_H(SIZE, TAPS)                                                \
static void put_vp8_epel ## SIZE ## _h ## TAPS ## _c(uint8_t *dst,            \
                                                     ptrdiff_t dststride,     \
                                                     uint8_t *src,            \
                                                     ptrdiff_t srcstride,     \
                                                     int h, int mx, int my)   \
{                                                                             \
    const uint8_t *filter = subpel_filters[mx - 1];                           \
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;                       \
    int x, y;                                                                 \
    for (y = 0; y < h; y++) {                                                 \
        for (x = 0; x < SIZE; x++)                                            \
            dst[x] = FILTER_ ## TAPS ## TAP(src, filter, 1);                  \
        dst += dststride;                                                     \
        src += srcstride;                                                     \
    }                                                                         \
}

#define VP8_EPEL_V(SIZE, TAPS)                                                \
static void put_vp8_epel ## SIZE ## _v ## TAPS ## _c(uint8_t *dst,            \
                                                     ptrdiff_t dststride,     \
                                                     uint8_t *src,            \
                                                     ptrdiff_t srcstride,     \
                                                     int h, int mx, int my)   \
{                                                                             \
    const uint8_t *filter = subpel_filters[my - 1];                           \
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;                       \
    int x, y;                                                                 \
    for (y = 0; y < h; y++) {                                                 \
        for (x = 0; x < SIZE; x++)                                            \
            dst[x] = FILTER_ ## TAPS ## TAP(src, filter, srcstride);          \
        dst += dststride;                                                     \
        src += srcstride;                                                     \
    }                                                                         \
}

#define VP8_EPEL_HV(SIZE, HTAPS, VTAPS)                                       \
static void                                                                   \
put_vp8_epel ## SIZE ## _h ## HTAPS ## v ## VTAPS ## _c(uint8_t *dst,         \
                                                        ptrdiff_t dststride,  \
                                                        uint8_t *src,         \
                                                        ptrdiff_t srcstride,  \
                                                        int h, int mx,        \
                                                        int my)               \
{                                                                             \
    const uint8_t *filter = subpel_filters[mx - 1];                           \
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;                       \
    int x, y;                                                                 \
    uint8_t tmp_array[(2 * SIZE + VTAPS - 1) * SIZE];                         \
    uint8_t *tmp = tmp_array;                                                 \
    src -= (2 - (VTAPS == 4)) * srcstride;                                    \
                                                                              \
    for (y = 0; y < h + VTAPS - 1; y++) {                                     \
        for (x = 0; x < SIZE; x++)                                            \
            tmp[x] = FILTER_ ## HTAPS ## TAP(src, filter, 1);                 \
        tmp += SIZE;                                                          \
        src += srcstride;                                                     \
    }                                                                         \
    tmp    = tmp_array + (2 - (VTAPS == 4)) * SIZE;                           \
    filter = subpel_filters[my - 1];                                          \
                                                                              \
    for (y = 0; y < h; y++) {                                                 \
        for (x = 0; x < SIZE; x++)                                            \
            dst[x] = FILTER_ ## VTAPS ## TAP(tmp, filter, SIZE);              \
        dst += dststride;                                                     \
        tmp += SIZE;                                                          \
    }                                                                         \
}

VP8_EPEL_H(16, 4)
VP8_EPEL_H(8,  4)
VP8_EPEL_H(4,  4)
VP8_EPEL_H(16, 6)
VP8_EPEL_H(8,  6)
VP8_EPEL_H(4,  6)
VP8_EPEL_V(16, 4)
VP8_EPEL_V(8,  4)
VP8_EPEL_V(4,  4)
VP8_EPEL_V(16, 6)
VP8_EPEL_V(8,  6)
VP8_EPEL_V(4,  6)

VP8_EPEL_HV(16, 4, 4)
VP8_EPEL_HV(8,  4, 4)
VP8_EPEL_HV(4,  4, 4)
VP8_EPEL_HV(16, 4, 6)
VP8_EPEL_HV(8,  4, 6)
VP8_EPEL_HV(4,  4, 6)
VP8_EPEL_HV(16, 6, 4)
VP8_EPEL_HV(8,  6, 4)
VP8_EPEL_HV(4,  6, 4)
VP8_EPEL_HV(16, 6, 6)
VP8_EPEL_HV(8,  6, 6)
VP8_EPEL_HV(4,  6, 6)

#define VP8_BILINEAR(SIZE)                                                    \
static void put_vp8_bilinear ## SIZE ## _h_c(uint8_t *dst, ptrdiff_t dstride, \
                                             uint8_t *src, ptrdiff_t sstride, \
                                             int h, int mx, int my)           \
{                                                                             \
    int a = 8 - mx, b = mx;                                                   \
    int x, y;                                                                 \
    for (y = 0; y < h; y++) {                                                 \
        for (x = 0; x < SIZE; x++)                                            \
            dst[x] = (a * src[x] + b * src[x + 1] + 4) >> 3;                  \
        dst += dstride;                                                       \
        src += sstride;                                                       \
    }                                                                         \
}                                                                             \
                                                                              \
static void put_vp8_bilinear ## SIZE ## _v_c(uint8_t *dst, ptrdiff_t dstride, \
                                             uint8_t *src, ptrdiff_t sstride, \
                                             int h, int mx, int my)           \
{                                                                             \
    int c = 8 - my, d = my;                                                   \
    int x, y;                                                                 \
    for (y = 0; y < h; y++) {                                                 \
        for (x = 0; x < SIZE; x++)                                            \
            dst[x] = (c * src[x] + d * src[x + sstride] + 4) >> 3;            \
        dst += dstride;                                                       \
        src += sstride;                                                       \
    }                                                                         \
}                                                                             \
                                                                              \
static void put_vp8_bilinear ## SIZE ## _hv_c(uint8_t *dst,                   \
                                              ptrdiff_t dstride,              \
                                              uint8_t *src,                   \
                                              ptrdiff_t sstride,              \
                                              int h, int mx, int my)          \
{                                                                             \
    int a = 8 - mx, b = mx;                                                   \
    int c = 8 - my, d = my;                                                   \
    int x, y;                                                                 \
    uint8_t tmp_array[(2 * SIZE + 1) * SIZE];                                 \
    uint8_t *tmp = tmp_array;                                                 \
    for (y = 0; y < h + 1; y++) {                                             \
        for (x = 0; x < SIZE; x++)                                            \
            tmp[x] = (a * src[x] + b * src[x + 1] + 4) >> 3;                  \
        tmp += SIZE;                                                          \
        src += sstride;                                                       \
    }                                                                         \
    tmp = tmp_array;                                                          \
    for (y = 0; y < h; y++) {                                                 \
        for (x = 0; x < SIZE; x++)                                            \
            dst[x] = (c * tmp[x] + d * tmp[x + SIZE] + 4) >> 3;               \
        dst += dstride;                                                       \
        tmp += SIZE;                                                          \
    }                                                                         \
}

VP8_BILINEAR(16)
VP8_BILINEAR(8)
VP8_BILINEAR(4)

#define VP78_MC_FUNC(IDX, SIZE)                                               \
    dsp->put_vp8_epel_pixels_tab[IDX][0][0] = put_vp8_pixels ## SIZE ## _c;   \
    dsp->put_vp8_epel_pixels_tab[IDX][0][1] = put_vp8_epel ## SIZE ## _h4_c;  \
    dsp->put_vp8_epel_pixels_tab[IDX][0][2] = put_vp8_epel ## SIZE ## _h6_c;  \
    dsp->put_vp8_epel_pixels_tab[IDX][1][0] = put_vp8_epel ## SIZE ## _v4_c;  \
    dsp->put_vp8_epel_pixels_tab[IDX][1][1] = put_vp8_epel ## SIZE ## _h4v4_c; \
    dsp->put_vp8_epel_pixels_tab[IDX][1][2] = put_vp8_epel ## SIZE ## _h6v4_c; \
    dsp->put_vp8_epel_pixels_tab[IDX][2][0] = put_vp8_epel ## SIZE ## _v6_c;  \
    dsp->put_vp8_epel_pixels_tab[IDX][2][1] = put_vp8_epel ## SIZE ## _h4v6_c; \
    dsp->put_vp8_epel_pixels_tab[IDX][2][2] = put_vp8_epel ## SIZE ## _h6v6_c

#define VP78_BILINEAR_MC_FUNC(IDX, SIZE)                                      \
    dsp->put_vp8_bilinear_pixels_tab[IDX][0][0] = put_vp8_pixels   ## SIZE ## _c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][0][1] = put_vp8_bilinear ## SIZE ## _h_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][0][2] = put_vp8_bilinear ## SIZE ## _h_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][1][0] = put_vp8_bilinear ## SIZE ## _v_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][1][1] = put_vp8_bilinear ## SIZE ## _hv_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][1][2] = put_vp8_bilinear ## SIZE ## _hv_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][2][0] = put_vp8_bilinear ## SIZE ## _v_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][2][1] = put_vp8_bilinear ## SIZE ## _hv_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][2][2] = put_vp8_bilinear ## SIZE ## _hv_c

av_cold void ff_vp78dsp_init(VP8DSPContext *dsp)
{
    VP78_MC_FUNC(0, 16);
    VP78_MC_FUNC(1, 8);
    VP78_MC_FUNC(2, 4);

    VP78_BILINEAR_MC_FUNC(0, 16);
    VP78_BILINEAR_MC_FUNC(1, 8);
    VP78_BILINEAR_MC_FUNC(2, 4);

    if (ARCH_AARCH64)
        ff_vp78dsp_init_aarch64(dsp);
    if (ARCH_ARM)
        ff_vp78dsp_init_arm(dsp);
    if (ARCH_PPC)
        ff_vp78dsp_init_ppc(dsp);
    if (ARCH_X86)
        ff_vp78dsp_init_x86(dsp);
}

#if CONFIG_VP7_DECODER
LOOP_FILTERS(vp7)

av_cold void ff_vp7dsp_init(VP8DSPContext *dsp)
{
    dsp->vp8_luma_dc_wht    = vp7_luma_dc_wht_c;
    dsp->vp8_luma_dc_wht_dc = vp7_luma_dc_wht_dc_c;
    dsp->vp8_idct_add       = vp7_idct_add_c;
    dsp->vp8_idct_dc_add    = vp7_idct_dc_add_c;
    dsp->vp8_idct_dc_add4y  = vp7_idct_dc_add4y_c;
    dsp->vp8_idct_dc_add4uv = vp7_idct_dc_add4uv_c;

    dsp->vp8_v_loop_filter16y = vp7_v_loop_filter16_c;
    dsp->vp8_h_loop_filter16y = vp7_h_loop_filter16_c;
    dsp->vp8_v_loop_filter8uv = vp7_v_loop_filter8uv_c;
    dsp->vp8_h_loop_filter8uv = vp7_h_loop_filter8uv_c;

    dsp->vp8_v_loop_filter16y_inner = vp7_v_loop_filter16_inner_c;
    dsp->vp8_h_loop_filter16y_inner = vp7_h_loop_filter16_inner_c;
    dsp->vp8_v_loop_filter8uv_inner = vp7_v_loop_filter8uv_inner_c;
    dsp->vp8_h_loop_filter8uv_inner = vp7_h_loop_filter8uv_inner_c;

    dsp->vp8_v_loop_filter_simple = vp7_v_loop_filter_simple_c;
    dsp->vp8_h_loop_filter_simple = vp7_h_loop_filter_simple_c;
}
#endif /* CONFIG_VP7_DECODER */

#if CONFIG_VP8_DECODER
LOOP_FILTERS(vp8)

av_cold void ff_vp8dsp_init(VP8DSPContext *dsp)
{
    dsp->vp8_luma_dc_wht    = vp8_luma_dc_wht_c;
    dsp->vp8_luma_dc_wht_dc = vp8_luma_dc_wht_dc_c;
    dsp->vp8_idct_add       = vp8_idct_add_c;
    dsp->vp8_idct_dc_add    = vp8_idct_dc_add_c;
    dsp->vp8_idct_dc_add4y  = vp8_idct_dc_add4y_c;
    dsp->vp8_idct_dc_add4uv = vp8_idct_dc_add4uv_c;

    dsp->vp8_v_loop_filter16y = vp8_v_loop_filter16_c;
    dsp->vp8_h_loop_filter16y = vp8_h_loop_filter16_c;
    dsp->vp8_v_loop_filter8uv = vp8_v_loop_filter8uv_c;
    dsp->vp8_h_loop_filter8uv = vp8_h_loop_filter8uv_c;

    dsp->vp8_v_loop_filter16y_inner = vp8_v_loop_filter16_inner_c;
    dsp->vp8_h_loop_filter16y_inner = vp8_h_loop_filter16_inner_c;
    dsp->vp8_v_loop_filter8uv_inner = vp8_v_loop_filter8uv_inner_c;
    dsp->vp8_h_loop_filter8uv_inner = vp8_h_loop_filter8uv_inner_c;

    dsp->vp8_v_loop_filter_simple = vp8_v_loop_filter_simple_c;
    dsp->vp8_h_loop_filter_simple = vp8_h_loop_filter_simple_c;

    if (ARCH_AARCH64)
        ff_vp8dsp_init_aarch64(dsp);
    if (ARCH_ARM)
        ff_vp8dsp_init_arm(dsp);
    if (ARCH_X86)
        ff_vp8dsp_init_x86(dsp);
    if (ARCH_MIPS)
        ff_vp8dsp_init_mips(dsp);
    if (ARCH_LOONGARCH)
        ff_vp8dsp_init_loongarch(dsp);
}
#endif /* CONFIG_VP8_DECODER */
