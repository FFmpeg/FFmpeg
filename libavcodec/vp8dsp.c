/**
 * VP8 compatible video decoder
 *
 * Copyright (C) 2010 David Conrad
 * Copyright (C) 2010 Ronald S. Bultje
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

#include "dsputil.h"
#include "vp8dsp.h"

// TODO: Maybe add dequant
static void vp8_luma_dc_wht_c(DCTELEM block[4][4][16], DCTELEM dc[16])
{
    int i, t0, t1, t2, t3;

    for (i = 0; i < 4; i++) {
        t0 = dc[0*4+i] + dc[3*4+i];
        t1 = dc[1*4+i] + dc[2*4+i];
        t2 = dc[1*4+i] - dc[2*4+i];
        t3 = dc[0*4+i] - dc[3*4+i];

        dc[0*4+i] = t0 + t1;
        dc[1*4+i] = t3 + t2;
        dc[2*4+i] = t0 - t1;
        dc[3*4+i] = t3 - t2;
    }

    for (i = 0; i < 4; i++) {
        t0 = dc[i*4+0] + dc[i*4+3] + 3; // rounding
        t1 = dc[i*4+1] + dc[i*4+2];
        t2 = dc[i*4+1] - dc[i*4+2];
        t3 = dc[i*4+0] - dc[i*4+3] + 3; // rounding
        dc[i*4+0] = 0;
        dc[i*4+1] = 0;
        dc[i*4+2] = 0;
        dc[i*4+3] = 0;

        block[i][0][0] = (t0 + t1) >> 3;
        block[i][1][0] = (t3 + t2) >> 3;
        block[i][2][0] = (t0 - t1) >> 3;
        block[i][3][0] = (t3 - t2) >> 3;
    }
}

static void vp8_luma_dc_wht_dc_c(DCTELEM block[4][4][16], DCTELEM dc[16])
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

#define MUL_20091(a) ((((a)*20091) >> 16) + (a))
#define MUL_35468(a)  (((a)*35468) >> 16)

static void vp8_idct_add_c(uint8_t *dst, DCTELEM block[16], int stride)
{
    int i, t0, t1, t2, t3;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    DCTELEM tmp[16];

    for (i = 0; i < 4; i++) {
        t0 = block[0*4+i] + block[2*4+i];
        t1 = block[0*4+i] - block[2*4+i];
        t2 = MUL_35468(block[1*4+i]) - MUL_20091(block[3*4+i]);
        t3 = MUL_20091(block[1*4+i]) + MUL_35468(block[3*4+i]);
        block[0*4+i] = 0;
        block[1*4+i] = 0;
        block[2*4+i] = 0;
        block[3*4+i] = 0;

        tmp[i*4+0] = t0 + t3;
        tmp[i*4+1] = t1 + t2;
        tmp[i*4+2] = t1 - t2;
        tmp[i*4+3] = t0 - t3;
    }

    for (i = 0; i < 4; i++) {
        t0 = tmp[0*4+i] + tmp[2*4+i];
        t1 = tmp[0*4+i] - tmp[2*4+i];
        t2 = MUL_35468(tmp[1*4+i]) - MUL_20091(tmp[3*4+i]);
        t3 = MUL_20091(tmp[1*4+i]) + MUL_35468(tmp[3*4+i]);

        dst[0] = cm[dst[0] + ((t0 + t3 + 4) >> 3)];
        dst[1] = cm[dst[1] + ((t1 + t2 + 4) >> 3)];
        dst[2] = cm[dst[2] + ((t1 - t2 + 4) >> 3)];
        dst[3] = cm[dst[3] + ((t0 - t3 + 4) >> 3)];
        dst += stride;
    }
}

static void vp8_idct_dc_add_c(uint8_t *dst, DCTELEM block[16], int stride)
{
    int i, dc = (block[0] + 4) >> 3;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP + dc;
    block[0] = 0;

    for (i = 0; i < 4; i++) {
        dst[0] = cm[dst[0]];
        dst[1] = cm[dst[1]];
        dst[2] = cm[dst[2]];
        dst[3] = cm[dst[3]];
        dst += stride;
    }
}

static void vp8_idct_dc_add4uv_c(uint8_t *dst, DCTELEM block[4][16], int stride)
{
    vp8_idct_dc_add_c(dst+stride*0+0, block[0], stride);
    vp8_idct_dc_add_c(dst+stride*0+4, block[1], stride);
    vp8_idct_dc_add_c(dst+stride*4+0, block[2], stride);
    vp8_idct_dc_add_c(dst+stride*4+4, block[3], stride);
}

static void vp8_idct_dc_add4y_c(uint8_t *dst, DCTELEM block[4][16], int stride)
{
    vp8_idct_dc_add_c(dst+ 0, block[0], stride);
    vp8_idct_dc_add_c(dst+ 4, block[1], stride);
    vp8_idct_dc_add_c(dst+ 8, block[2], stride);
    vp8_idct_dc_add_c(dst+12, block[3], stride);
}

// because I like only having two parameters to pass functions...
#define LOAD_PIXELS\
    int av_unused p3 = p[-4*stride];\
    int av_unused p2 = p[-3*stride];\
    int av_unused p1 = p[-2*stride];\
    int av_unused p0 = p[-1*stride];\
    int av_unused q0 = p[ 0*stride];\
    int av_unused q1 = p[ 1*stride];\
    int av_unused q2 = p[ 2*stride];\
    int av_unused q3 = p[ 3*stride];

#define clip_int8(n) (cm[n+0x80]-0x80)

static av_always_inline void filter_common(uint8_t *p, int stride, int is4tap)
{
    LOAD_PIXELS
    int a, f1, f2;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    a = 3*(q0 - p0);

    if (is4tap)
        a += clip_int8(p1 - q1);

    a = clip_int8(a);

    // We deviate from the spec here with c(a+3) >> 3
    // since that's what libvpx does.
    f1 = FFMIN(a+4, 127) >> 3;
    f2 = FFMIN(a+3, 127) >> 3;

    // Despite what the spec says, we do need to clamp here to
    // be bitexact with libvpx.
    p[-1*stride] = cm[p0 + f2];
    p[ 0*stride] = cm[q0 - f1];

    // only used for _inner on blocks without high edge variance
    if (!is4tap) {
        a = (f1+1)>>1;
        p[-2*stride] = cm[p1 + a];
        p[ 1*stride] = cm[q1 - a];
    }
}

static av_always_inline int simple_limit(uint8_t *p, int stride, int flim)
{
    LOAD_PIXELS
    return 2*FFABS(p0-q0) + (FFABS(p1-q1) >> 1) <= flim;
}

/**
 * E - limit at the macroblock edge
 * I - limit for interior difference
 */
static av_always_inline int normal_limit(uint8_t *p, int stride, int E, int I)
{
    LOAD_PIXELS
    return simple_limit(p, stride, E)
        && FFABS(p3-p2) <= I && FFABS(p2-p1) <= I && FFABS(p1-p0) <= I
        && FFABS(q3-q2) <= I && FFABS(q2-q1) <= I && FFABS(q1-q0) <= I;
}

// high edge variance
static av_always_inline int hev(uint8_t *p, int stride, int thresh)
{
    LOAD_PIXELS
    return FFABS(p1-p0) > thresh || FFABS(q1-q0) > thresh;
}

static av_always_inline void filter_mbedge(uint8_t *p, int stride)
{
    int a0, a1, a2, w;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    LOAD_PIXELS

    w = clip_int8(p1-q1);
    w = clip_int8(w + 3*(q0-p0));

    a0 = (27*w + 63) >> 7;
    a1 = (18*w + 63) >> 7;
    a2 = ( 9*w + 63) >> 7;

    p[-3*stride] = cm[p2 + a2];
    p[-2*stride] = cm[p1 + a1];
    p[-1*stride] = cm[p0 + a0];
    p[ 0*stride] = cm[q0 - a0];
    p[ 1*stride] = cm[q1 - a1];
    p[ 2*stride] = cm[q2 - a2];
}

#define LOOP_FILTER(dir, size, stridea, strideb, maybe_inline) \
static maybe_inline void vp8_ ## dir ## _loop_filter ## size ## _c(uint8_t *dst, int stride,\
                                     int flim_E, int flim_I, int hev_thresh)\
{\
    int i;\
\
    for (i = 0; i < size; i++)\
        if (normal_limit(dst+i*stridea, strideb, flim_E, flim_I)) {\
            if (hev(dst+i*stridea, strideb, hev_thresh))\
                filter_common(dst+i*stridea, strideb, 1);\
            else\
                filter_mbedge(dst+i*stridea, strideb);\
        }\
}\
\
static maybe_inline void vp8_ ## dir ## _loop_filter ## size ## _inner_c(uint8_t *dst, int stride,\
                                      int flim_E, int flim_I, int hev_thresh)\
{\
    int i;\
\
    for (i = 0; i < size; i++)\
        if (normal_limit(dst+i*stridea, strideb, flim_E, flim_I)) {\
            int hv = hev(dst+i*stridea, strideb, hev_thresh);\
            if (hv) \
                filter_common(dst+i*stridea, strideb, 1);\
            else \
                filter_common(dst+i*stridea, strideb, 0);\
        }\
}

LOOP_FILTER(v, 16, 1, stride,)
LOOP_FILTER(h, 16, stride, 1,)

#define UV_LOOP_FILTER(dir, stridea, strideb) \
LOOP_FILTER(dir, 8, stridea, strideb, av_always_inline) \
static void vp8_ ## dir ## _loop_filter8uv_c(uint8_t *dstU, uint8_t *dstV, int stride,\
                                      int fE, int fI, int hev_thresh)\
{\
  vp8_ ## dir ## _loop_filter8_c(dstU, stride, fE, fI, hev_thresh);\
  vp8_ ## dir ## _loop_filter8_c(dstV, stride, fE, fI, hev_thresh);\
}\
static void vp8_ ## dir ## _loop_filter8uv_inner_c(uint8_t *dstU, uint8_t *dstV, int stride,\
                                      int fE, int fI, int hev_thresh)\
{\
  vp8_ ## dir ## _loop_filter8_inner_c(dstU, stride, fE, fI, hev_thresh);\
  vp8_ ## dir ## _loop_filter8_inner_c(dstV, stride, fE, fI, hev_thresh);\
}

UV_LOOP_FILTER(v, 1, stride)
UV_LOOP_FILTER(h, stride, 1)

static void vp8_v_loop_filter_simple_c(uint8_t *dst, int stride, int flim)
{
    int i;

    for (i = 0; i < 16; i++)
        if (simple_limit(dst+i, stride, flim))
            filter_common(dst+i, stride, 1);
}

static void vp8_h_loop_filter_simple_c(uint8_t *dst, int stride, int flim)
{
    int i;

    for (i = 0; i < 16; i++)
        if (simple_limit(dst+i*stride, 1, flim))
            filter_common(dst+i*stride, 1, 1);
}

static const uint8_t subpel_filters[7][6] = {
    { 0,   6, 123,  12,   1,   0 },
    { 2,  11, 108,  36,   8,   1 },
    { 0,   9,  93,  50,   6,   0 },
    { 3,  16,  77,  77,  16,   3 },
    { 0,   6,  50,  93,   9,   0 },
    { 1,   8,  36, 108,  11,   2 },
    { 0,   1,  12, 123,   6,   0 },
};

#define PUT_PIXELS(WIDTH) \
static void put_vp8_pixels ## WIDTH ##_c(uint8_t *dst, int dststride, uint8_t *src, int srcstride, int h, int x, int y) { \
    int i; \
    for (i = 0; i < h; i++, dst+= dststride, src+= srcstride) { \
        memcpy(dst, src, WIDTH); \
    } \
}

PUT_PIXELS(16)
PUT_PIXELS(8)
PUT_PIXELS(4)

#define FILTER_6TAP(src, F, stride) \
    cm[(F[2]*src[x+0*stride] - F[1]*src[x-1*stride] + F[0]*src[x-2*stride] + \
        F[3]*src[x+1*stride] - F[4]*src[x+2*stride] + F[5]*src[x+3*stride] + 64) >> 7]

#define FILTER_4TAP(src, F, stride) \
    cm[(F[2]*src[x+0*stride] - F[1]*src[x-1*stride] + \
        F[3]*src[x+1*stride] - F[4]*src[x+2*stride] + 64) >> 7]

#define VP8_EPEL_H(SIZE, TAPS) \
static void put_vp8_epel ## SIZE ## _h ## TAPS ## _c(uint8_t *dst, int dststride, uint8_t *src, int srcstride, int h, int mx, int my) \
{ \
    const uint8_t *filter = subpel_filters[mx-1]; \
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP; \
    int x, y; \
\
    for (y = 0; y < h; y++) { \
        for (x = 0; x < SIZE; x++) \
            dst[x] = FILTER_ ## TAPS ## TAP(src, filter, 1); \
        dst += dststride; \
        src += srcstride; \
    } \
}
#define VP8_EPEL_V(SIZE, TAPS) \
static void put_vp8_epel ## SIZE ## _v ## TAPS ## _c(uint8_t *dst, int dststride, uint8_t *src, int srcstride, int h, int mx, int my) \
{ \
    const uint8_t *filter = subpel_filters[my-1]; \
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP; \
    int x, y; \
\
    for (y = 0; y < h; y++) { \
        for (x = 0; x < SIZE; x++) \
            dst[x] = FILTER_ ## TAPS ## TAP(src, filter, srcstride); \
        dst += dststride; \
        src += srcstride; \
    } \
}
#define VP8_EPEL_HV(SIZE, HTAPS, VTAPS) \
static void put_vp8_epel ## SIZE ## _h ## HTAPS ## v ## VTAPS ## _c(uint8_t *dst, int dststride, uint8_t *src, int srcstride, int h, int mx, int my) \
{ \
    const uint8_t *filter = subpel_filters[mx-1]; \
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP; \
    int x, y; \
    uint8_t tmp_array[(2*SIZE+VTAPS-1)*SIZE]; \
    uint8_t *tmp = tmp_array; \
    src -= (2-(VTAPS==4))*srcstride; \
\
    for (y = 0; y < h+VTAPS-1; y++) { \
        for (x = 0; x < SIZE; x++) \
            tmp[x] = FILTER_ ## HTAPS ## TAP(src, filter, 1); \
        tmp += SIZE; \
        src += srcstride; \
    } \
\
    tmp = tmp_array + (2-(VTAPS==4))*SIZE; \
    filter = subpel_filters[my-1]; \
\
    for (y = 0; y < h; y++) { \
        for (x = 0; x < SIZE; x++) \
            dst[x] = FILTER_ ## VTAPS ## TAP(tmp, filter, SIZE); \
        dst += dststride; \
        tmp += SIZE; \
    } \
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

#define VP8_BILINEAR(SIZE) \
static void put_vp8_bilinear ## SIZE ## _h_c(uint8_t *dst, int stride, uint8_t *src, int s2, int h, int mx, int my) \
{ \
    int a = 8-mx, b = mx; \
    int x, y; \
\
    for (y = 0; y < h; y++) { \
        for (x = 0; x < SIZE; x++) \
            dst[x] = (a*src[x] + b*src[x+1] + 4) >> 3; \
        dst += stride; \
        src += stride; \
    } \
} \
static void put_vp8_bilinear ## SIZE ## _v_c(uint8_t *dst, int stride, uint8_t *src, int s2, int h, int mx, int my) \
{ \
    int c = 8-my, d = my; \
    int x, y; \
\
    for (y = 0; y < h; y++) { \
        for (x = 0; x < SIZE; x++) \
            dst[x] = (c*src[x] + d*src[x+stride] + 4) >> 3; \
        dst += stride; \
        src += stride; \
    } \
} \
\
static void put_vp8_bilinear ## SIZE ## _hv_c(uint8_t *dst, int stride, uint8_t *src, int s2, int h, int mx, int my) \
{ \
    int a = 8-mx, b = mx; \
    int c = 8-my, d = my; \
    int x, y; \
    uint8_t tmp_array[(2*SIZE+1)*SIZE]; \
    uint8_t *tmp = tmp_array; \
\
    for (y = 0; y < h+1; y++) { \
        for (x = 0; x < SIZE; x++) \
            tmp[x] = (a*src[x] + b*src[x+1] + 4) >> 3; \
        tmp += SIZE; \
        src += stride; \
    } \
\
    tmp = tmp_array; \
\
    for (y = 0; y < h; y++) { \
        for (x = 0; x < SIZE; x++) \
            dst[x] = (c*tmp[x] + d*tmp[x+SIZE] + 4) >> 3; \
        dst += stride; \
        tmp += SIZE; \
    } \
}

VP8_BILINEAR(16)
VP8_BILINEAR(8)
VP8_BILINEAR(4)

#define VP8_MC_FUNC(IDX, SIZE) \
    dsp->put_vp8_epel_pixels_tab[IDX][0][0] = put_vp8_pixels ## SIZE ## _c; \
    dsp->put_vp8_epel_pixels_tab[IDX][0][1] = put_vp8_epel ## SIZE ## _h4_c; \
    dsp->put_vp8_epel_pixels_tab[IDX][0][2] = put_vp8_epel ## SIZE ## _h6_c; \
    dsp->put_vp8_epel_pixels_tab[IDX][1][0] = put_vp8_epel ## SIZE ## _v4_c; \
    dsp->put_vp8_epel_pixels_tab[IDX][1][1] = put_vp8_epel ## SIZE ## _h4v4_c; \
    dsp->put_vp8_epel_pixels_tab[IDX][1][2] = put_vp8_epel ## SIZE ## _h6v4_c; \
    dsp->put_vp8_epel_pixels_tab[IDX][2][0] = put_vp8_epel ## SIZE ## _v6_c; \
    dsp->put_vp8_epel_pixels_tab[IDX][2][1] = put_vp8_epel ## SIZE ## _h4v6_c; \
    dsp->put_vp8_epel_pixels_tab[IDX][2][2] = put_vp8_epel ## SIZE ## _h6v6_c

#define VP8_BILINEAR_MC_FUNC(IDX, SIZE) \
    dsp->put_vp8_bilinear_pixels_tab[IDX][0][0] = put_vp8_pixels ## SIZE ## _c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][0][1] = put_vp8_bilinear ## SIZE ## _h_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][0][2] = put_vp8_bilinear ## SIZE ## _h_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][1][0] = put_vp8_bilinear ## SIZE ## _v_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][1][1] = put_vp8_bilinear ## SIZE ## _hv_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][1][2] = put_vp8_bilinear ## SIZE ## _hv_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][2][0] = put_vp8_bilinear ## SIZE ## _v_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][2][1] = put_vp8_bilinear ## SIZE ## _hv_c; \
    dsp->put_vp8_bilinear_pixels_tab[IDX][2][2] = put_vp8_bilinear ## SIZE ## _hv_c

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

    VP8_MC_FUNC(0, 16);
    VP8_MC_FUNC(1, 8);
    VP8_MC_FUNC(2, 4);

    VP8_BILINEAR_MC_FUNC(0, 16);
    VP8_BILINEAR_MC_FUNC(1, 8);
    VP8_BILINEAR_MC_FUNC(2, 4);

    if (HAVE_MMX)
        ff_vp8dsp_init_x86(dsp);
    if (HAVE_ALTIVEC)
        ff_vp8dsp_init_altivec(dsp);
    if (ARCH_ARM)
        ff_vp8dsp_init_arm(dsp);
}
