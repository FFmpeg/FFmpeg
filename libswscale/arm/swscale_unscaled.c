/*
 * Copyright (C) 2013 Xiaolei Yu <dreifachstein@gmail.com>
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
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/arm/cpu.h"

extern void rgbx_to_nv12_neon_32(const uint8_t *src, uint8_t *y, uint8_t *chroma,
                int width, int height,
                int y_stride, int c_stride, int src_stride,
                int32_t coeff_tbl[9]);

extern void rgbx_to_nv12_neon_16(const uint8_t *src, uint8_t *y, uint8_t *chroma,
                int width, int height,
                int y_stride, int c_stride, int src_stride,
                int32_t coeff_tbl[9]);

static int rgbx_to_nv12_neon_32_wrapper(SwsContext *context, const uint8_t *src[],
                        int srcStride[], int srcSliceY, int srcSliceH,
                        uint8_t *dst[], int dstStride[]) {

    rgbx_to_nv12_neon_32(src[0] + srcSliceY * srcStride[0],
            dst[0] + srcSliceY * dstStride[0],
            dst[1] + (srcSliceY / 2) * dstStride[1],
            context->srcW, srcSliceH,
            dstStride[0], dstStride[1], srcStride[0],
            context->input_rgb2yuv_table);

    return 0;
}

static int rgbx_to_nv12_neon_16_wrapper(SwsContext *context, const uint8_t *src[],
                        int srcStride[], int srcSliceY, int srcSliceH,
                        uint8_t *dst[], int dstStride[]) {

    rgbx_to_nv12_neon_16(src[0] + srcSliceY * srcStride[0],
            dst[0] + srcSliceY * dstStride[0],
            dst[1] + (srcSliceY / 2) * dstStride[1],
            context->srcW, srcSliceH,
            dstStride[0], dstStride[1], srcStride[0],
            context->input_rgb2yuv_table);

    return 0;
}

#define YUV_TO_RGB_TABLE                                                                    \
        c->yuv2rgb_v2r_coeff,                                                               \
        c->yuv2rgb_u2g_coeff,                                                               \
        c->yuv2rgb_v2g_coeff,                                                               \
        c->yuv2rgb_u2b_coeff,                                                               \

#define DECLARE_FF_YUVX_TO_RGBX_FUNCS(ifmt, ofmt)                                           \
int ff_##ifmt##_to_##ofmt##_neon(int w, int h,                                              \
                                 uint8_t *dst, int linesize,                                \
                                 const uint8_t *srcY, int linesizeY,                        \
                                 const uint8_t *srcU, int linesizeU,                        \
                                 const uint8_t *srcV, int linesizeV,                        \
                                 const int16_t *table,                                      \
                                 int y_offset,                                              \
                                 int y_coeff);                                              \
                                                                                            \
static int ifmt##_to_##ofmt##_neon_wrapper(SwsContext *c, const uint8_t *src[],             \
                                           int srcStride[], int srcSliceY, int srcSliceH,   \
                                           uint8_t *dst[], int dstStride[]) {               \
    const int16_t yuv2rgb_table[] = { YUV_TO_RGB_TABLE };                                   \
                                                                                            \
    ff_##ifmt##_to_##ofmt##_neon(c->srcW, srcSliceH,                                        \
                                 dst[0] + srcSliceY * dstStride[0], dstStride[0],           \
                                 src[0], srcStride[0],                                      \
                                 src[1], srcStride[1],                                      \
                                 src[2], srcStride[2],                                      \
                                 yuv2rgb_table,                                             \
                                 c->yuv2rgb_y_offset >> 6,                                  \
                                 c->yuv2rgb_y_coeff);                                       \
                                                                                            \
    return 0;                                                                               \
}                                                                                           \

#define DECLARE_FF_YUVX_TO_ALL_RGBX_FUNCS(yuvx)                                             \
DECLARE_FF_YUVX_TO_RGBX_FUNCS(yuvx, argb)                                                   \
DECLARE_FF_YUVX_TO_RGBX_FUNCS(yuvx, rgba)                                                   \
DECLARE_FF_YUVX_TO_RGBX_FUNCS(yuvx, abgr)                                                   \
DECLARE_FF_YUVX_TO_RGBX_FUNCS(yuvx, bgra)                                                   \

DECLARE_FF_YUVX_TO_ALL_RGBX_FUNCS(yuv420p)
DECLARE_FF_YUVX_TO_ALL_RGBX_FUNCS(yuv422p)

#define DECLARE_FF_NVX_TO_RGBX_FUNCS(ifmt, ofmt)                                            \
int ff_##ifmt##_to_##ofmt##_neon(int w, int h,                                              \
                                 uint8_t *dst, int linesize,                                \
                                 const uint8_t *srcY, int linesizeY,                        \
                                 const uint8_t *srcC, int linesizeC,                        \
                                 const int16_t *table,                                      \
                                 int y_offset,                                              \
                                 int y_coeff);                                              \
                                                                                            \
static int ifmt##_to_##ofmt##_neon_wrapper(SwsContext *c, const uint8_t *src[],             \
                                           int srcStride[], int srcSliceY, int srcSliceH,   \
                                           uint8_t *dst[], int dstStride[]) {               \
    const int16_t yuv2rgb_table[] = { YUV_TO_RGB_TABLE };                                   \
                                                                                            \
    ff_##ifmt##_to_##ofmt##_neon(c->srcW, srcSliceH,                                        \
                                 dst[0] + srcSliceY * dstStride[0], dstStride[0],           \
                                 src[0], srcStride[0], src[1], srcStride[1],                \
                                 yuv2rgb_table,                                             \
                                 c->yuv2rgb_y_offset >> 6,                                  \
                                 c->yuv2rgb_y_coeff);                                       \
                                                                                            \
    return 0;                                                                               \
}                                                                                           \

#define DECLARE_FF_NVX_TO_ALL_RGBX_FUNCS(nvx)                                               \
DECLARE_FF_NVX_TO_RGBX_FUNCS(nvx, argb)                                                     \
DECLARE_FF_NVX_TO_RGBX_FUNCS(nvx, rgba)                                                     \
DECLARE_FF_NVX_TO_RGBX_FUNCS(nvx, abgr)                                                     \
DECLARE_FF_NVX_TO_RGBX_FUNCS(nvx, bgra)                                                     \

DECLARE_FF_NVX_TO_ALL_RGBX_FUNCS(nv12)
DECLARE_FF_NVX_TO_ALL_RGBX_FUNCS(nv21)

/* We need a 16 pixel width alignment. This constraint can easily be removed
 * for input reading but for the output which is 4-bytes per pixel (RGBA) the
 * assembly might be writing as much as 4*15=60 extra bytes at the end of the
 * line, which won't fit the 32-bytes buffer alignment. */
#define SET_FF_NVX_TO_RGBX_FUNC(ifmt, IFMT, ofmt, OFMT, accurate_rnd) do {                  \
    if (c->srcFormat == AV_PIX_FMT_##IFMT                                                   \
        && c->dstFormat == AV_PIX_FMT_##OFMT                                                \
        && !(c->srcH & 1)                                                                   \
        && !(c->srcW & 15)                                                                  \
        && !accurate_rnd) {                                                                 \
        c->swscale = ifmt##_to_##ofmt##_neon_wrapper;                                       \
    }                                                                                       \
} while (0)

#define SET_FF_NVX_TO_ALL_RGBX_FUNC(nvx, NVX, accurate_rnd) do {                            \
    SET_FF_NVX_TO_RGBX_FUNC(nvx, NVX, argb, ARGB, accurate_rnd);                            \
    SET_FF_NVX_TO_RGBX_FUNC(nvx, NVX, rgba, RGBA, accurate_rnd);                            \
    SET_FF_NVX_TO_RGBX_FUNC(nvx, NVX, abgr, ABGR, accurate_rnd);                            \
    SET_FF_NVX_TO_RGBX_FUNC(nvx, NVX, bgra, BGRA, accurate_rnd);                            \
} while (0)

static void get_unscaled_swscale_neon(SwsContext *c) {
    int accurate_rnd = c->flags & SWS_ACCURATE_RND;
    if (c->srcFormat == AV_PIX_FMT_RGBA
            && c->dstFormat == AV_PIX_FMT_NV12
            && (c->srcW >= 16)) {
        c->swscale = accurate_rnd ? rgbx_to_nv12_neon_32_wrapper
                        : rgbx_to_nv12_neon_16_wrapper;
    }

    SET_FF_NVX_TO_ALL_RGBX_FUNC(nv12, NV12, accurate_rnd);
    SET_FF_NVX_TO_ALL_RGBX_FUNC(nv21, NV21, accurate_rnd);
    SET_FF_NVX_TO_ALL_RGBX_FUNC(yuv420p, YUV420P, accurate_rnd);
    SET_FF_NVX_TO_ALL_RGBX_FUNC(yuv422p, YUV422P, accurate_rnd);
}

void ff_get_unscaled_swscale_arm(SwsContext *c)
{
    int cpu_flags = av_get_cpu_flags();
    if (have_neon(cpu_flags))
        get_unscaled_swscale_neon(c);
}
