/*
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
#include "libavutil/aarch64/cpu.h"

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
static int ifmt##_to_##ofmt##_neon_wrapper(SwsInternal *c, const uint8_t *const src[],      \
                                           const int srcStride[], int srcSliceY,            \
                                           int srcSliceH, uint8_t *const dst[],             \
                                           const int dstStride[]) {                         \
    const int16_t yuv2rgb_table[] = { YUV_TO_RGB_TABLE };                                   \
                                                                                            \
    return ff_##ifmt##_to_##ofmt##_neon(c->opts.src_w, srcSliceH,                           \
                                        dst[0] + srcSliceY * dstStride[0], dstStride[0],    \
                                        src[0], srcStride[0],                               \
                                        src[1], srcStride[1],                               \
                                        src[2], srcStride[2],                               \
                                        yuv2rgb_table,                                      \
                                        c->yuv2rgb_y_offset >> 6,                           \
                                        c->yuv2rgb_y_coeff);                                \
}                                                                                           \

#define DECLARE_FF_YUVX_TO_GBRP_FUNCS(ifmt, ofmt)                                           \
int ff_##ifmt##_to_##ofmt##_neon(int w, int h,                                              \
                                 uint8_t *dst, int linesize,                                \
                                 const uint8_t *srcY, int linesizeY,                        \
                                 const uint8_t *srcU, int linesizeU,                        \
                                 const uint8_t *srcV, int linesizeV,                        \
                                 const int16_t *table,                                      \
                                 int y_offset,                                              \
                                 int y_coeff,                                               \
                                 uint8_t *dst1, int linesize1,                              \
                                 uint8_t *dst2, int linesize2);                             \
                                                                                            \
static int ifmt##_to_##ofmt##_neon_wrapper(SwsInternal *c, const uint8_t *const src[],      \
                                           const int srcStride[], int srcSliceY,            \
                                           int srcSliceH, uint8_t *const dst[],             \
                                           const int dstStride[]) {                         \
    const int16_t yuv2rgb_table[] = { YUV_TO_RGB_TABLE };                                   \
                                                                                            \
    return ff_##ifmt##_to_##ofmt##_neon(c->opts.src_w, srcSliceH,                           \
                                        dst[0] + srcSliceY * dstStride[0], dstStride[0],    \
                                        src[0], srcStride[0],                               \
                                        src[1], srcStride[1],                               \
                                        src[2], srcStride[2],                               \
                                        yuv2rgb_table,                                      \
                                        c->yuv2rgb_y_offset >> 6,                           \
                                        c->yuv2rgb_y_coeff,                                 \
                                        dst[1] + srcSliceY * dstStride[1], dstStride[1],    \
                                        dst[2] + srcSliceY * dstStride[2], dstStride[2]);   \
}                                                                                           \

#define DECLARE_FF_YUVX_TO_ALL_RGBX_FUNCS(yuvx)                                             \
DECLARE_FF_YUVX_TO_RGBX_FUNCS(yuvx, argb)                                                   \
DECLARE_FF_YUVX_TO_RGBX_FUNCS(yuvx, rgba)                                                   \
DECLARE_FF_YUVX_TO_RGBX_FUNCS(yuvx, abgr)                                                   \
DECLARE_FF_YUVX_TO_RGBX_FUNCS(yuvx, bgra)                                                   \
DECLARE_FF_YUVX_TO_GBRP_FUNCS(yuvx, gbrp)                                                   \
DECLARE_FF_YUVX_TO_RGBX_FUNCS(yuvx, rgb24)                                                  \
DECLARE_FF_YUVX_TO_RGBX_FUNCS(yuvx, bgr24)                                                  \

DECLARE_FF_YUVX_TO_ALL_RGBX_FUNCS(yuv420p)
DECLARE_FF_YUVX_TO_ALL_RGBX_FUNCS(yuv422p)

#define DECLARE_FF_YUVA420P_TO_RGBX_FUNCS(ofmt)                                            \
int ff_yuva420p_to_##ofmt##_neon(int w, int h,                                              \
                                 uint8_t *dst, int linesize,                                \
                                 const uint8_t *srcY, int linesizeY,                        \
                                 const uint8_t *srcU, int linesizeU,                        \
                                 const uint8_t *srcV, int linesizeV,                        \
                                 const int16_t *table,                                      \
                                 int y_offset, int y_coeff,                                 \
                                 const uint8_t *srcA, int linesizeA);                       \
                                                                                            \
static int yuva420p_to_##ofmt##_neon_wrapper(SwsInternal *c,                                \
                                             const uint8_t *const src[],                    \
                                             const int srcStride[], int srcSliceY,          \
                                             int srcSliceH, uint8_t *const dst[],           \
                                             const int dstStride[]) {                       \
    const int16_t yuv2rgb_table[] = { YUV_TO_RGB_TABLE };                                   \
                                                                                            \
    return ff_yuva420p_to_##ofmt##_neon(c->opts.src_w, srcSliceH,                           \
                                        dst[0] + srcSliceY * dstStride[0], dstStride[0],    \
                                        src[0], srcStride[0],                               \
                                        src[1], srcStride[1],                               \
                                        src[2], srcStride[2],                               \
                                        yuv2rgb_table,                                      \
                                        c->yuv2rgb_y_offset >> 6,                           \
                                        c->yuv2rgb_y_coeff,                                 \
                                        src[3], srcStride[3]);                              \
}

DECLARE_FF_YUVA420P_TO_RGBX_FUNCS(argb)
DECLARE_FF_YUVA420P_TO_RGBX_FUNCS(rgba)
DECLARE_FF_YUVA420P_TO_RGBX_FUNCS(abgr)
DECLARE_FF_YUVA420P_TO_RGBX_FUNCS(bgra)

#define DECLARE_FF_NVX_TO_RGBX_FUNCS(ifmt, ofmt)                                            \
int ff_##ifmt##_to_##ofmt##_neon(int w, int h,                                              \
                                 uint8_t *dst, int linesize,                                \
                                 const uint8_t *srcY, int linesizeY,                        \
                                 const uint8_t *srcC, int linesizeC,                        \
                                 const int16_t *table,                                      \
                                 int y_offset,                                              \
                                 int y_coeff);                                              \
                                                                                            \
static int ifmt##_to_##ofmt##_neon_wrapper(SwsInternal *c, const uint8_t *const src[],      \
                                           const int srcStride[], int srcSliceY,            \
                                           int srcSliceH, uint8_t *const dst[],             \
                                           const int dstStride[]) {                         \
    const int16_t yuv2rgb_table[] = { YUV_TO_RGB_TABLE };                                   \
                                                                                            \
    return ff_##ifmt##_to_##ofmt##_neon(c->opts.src_w, srcSliceH,                           \
                                        dst[0] + srcSliceY * dstStride[0], dstStride[0],    \
                                        src[0], srcStride[0], src[1], srcStride[1],         \
                                        yuv2rgb_table,                                      \
                                        c->yuv2rgb_y_offset >> 6,                           \
                                        c->yuv2rgb_y_coeff);                                \
}                                                                                           \

#define DECLARE_FF_NVX_TO_GBRP_FUNCS(ifmt, ofmt)                                            \
int ff_##ifmt##_to_##ofmt##_neon(int w, int h,                                              \
                                 uint8_t *dst, int linesize,                                \
                                 const uint8_t *srcY, int linesizeY,                        \
                                 const uint8_t *srcC, int linesizeC,                        \
                                 const int16_t *table,                                      \
                                 int y_offset,                                              \
                                 int y_coeff,                                               \
                                 uint8_t *dst1, int linesize1,                              \
                                 uint8_t *dst2, int linesize2);                             \
                                                                                            \
static int ifmt##_to_##ofmt##_neon_wrapper(SwsInternal *c, const uint8_t *const src[],      \
                                           const int srcStride[], int srcSliceY,            \
                                           int srcSliceH, uint8_t *const dst[],             \
                                           const int dstStride[]) {                         \
    const int16_t yuv2rgb_table[] = { YUV_TO_RGB_TABLE };                                   \
                                                                                            \
    return ff_##ifmt##_to_##ofmt##_neon(c->opts.src_w, srcSliceH,                           \
                                        dst[0] + srcSliceY * dstStride[0], dstStride[0],    \
                                        src[0], srcStride[0], src[1], srcStride[1],         \
                                        yuv2rgb_table,                                      \
                                        c->yuv2rgb_y_offset >> 6,                           \
                                        c->yuv2rgb_y_coeff,                                 \
                                        dst[1] + srcSliceY * dstStride[1], dstStride[1],    \
                                        dst[2] + srcSliceY * dstStride[2], dstStride[2]);   \
}                                                                                           \

void ff_nv24_to_yuv420p_chroma_neon(uint8_t *dst1, int dstStride1,
                                    uint8_t *dst2, int dstStride2,
                                    const uint8_t *src, int srcStride,
                                    int w, int h);

static int nv24_to_yuv420p_neon_wrapper(SwsInternal *c, const uint8_t *const src[],
                                        const int srcStride[], int srcSliceY, int srcSliceH,
                                        uint8_t *const dst[], const int dstStride[])
{
    uint8_t *dst1 = dst[1] + dstStride[1] * srcSliceY / 2;
    uint8_t *dst2 = dst[2] + dstStride[2] * srcSliceY / 2;

    ff_copyPlane(src[0], srcStride[0], srcSliceY, srcSliceH, c->opts.src_w,
                 dst[0], dstStride[0]);

    if (c->opts.src_format == AV_PIX_FMT_NV24)
        ff_nv24_to_yuv420p_chroma_neon(dst1, dstStride[1], dst2, dstStride[2],
                                       src[1], srcStride[1], c->opts.src_w / 2,
                                       srcSliceH);
    else
        ff_nv24_to_yuv420p_chroma_neon(dst2, dstStride[2], dst1, dstStride[1],
                                       src[1], srcStride[1], c->opts.src_w / 2,
                                       srcSliceH);

    return srcSliceH;
}

#define DECLARE_FF_NVX_TO_ALL_RGBX_FUNCS(nvx)                                               \
DECLARE_FF_NVX_TO_RGBX_FUNCS(nvx, argb)                                                     \
DECLARE_FF_NVX_TO_RGBX_FUNCS(nvx, rgba)                                                     \
DECLARE_FF_NVX_TO_RGBX_FUNCS(nvx, abgr)                                                     \
DECLARE_FF_NVX_TO_RGBX_FUNCS(nvx, bgra)                                                     \
DECLARE_FF_NVX_TO_GBRP_FUNCS(nvx, gbrp)                                                     \
DECLARE_FF_NVX_TO_RGBX_FUNCS(nvx, rgb24)                                                    \
DECLARE_FF_NVX_TO_RGBX_FUNCS(nvx, bgr24)                                                    \

DECLARE_FF_NVX_TO_ALL_RGBX_FUNCS(nv12)
DECLARE_FF_NVX_TO_ALL_RGBX_FUNCS(nv21)

/* We need a 16 pixel width alignment. This constraint can easily be removed
 * for input reading but for the output which is 4-bytes per pixel (RGBA) the
 * assembly might be writing as much as 4*15=60 extra bytes at the end of the
 * line, which won't fit the 32-bytes buffer alignment. */
#define SET_FF_NVX_TO_RGBX_FUNC(ifmt, IFMT, ofmt, OFMT, accurate_rnd) do {                  \
    if (c->opts.src_format == AV_PIX_FMT_##IFMT                                             \
        && c->opts.dst_format == AV_PIX_FMT_##OFMT                                          \
        && !(c->opts.src_h & 1)                                                             \
        && !(c->opts.src_w & 15)                                                            \
        && !accurate_rnd)                                                                   \
        c->convert_unscaled = ifmt##_to_##ofmt##_neon_wrapper;                              \
} while (0)

#define SET_FF_NVX_TO_ALL_RGBX_FUNC(nvx, NVX, accurate_rnd) do {                            \
    SET_FF_NVX_TO_RGBX_FUNC(nvx, NVX, argb, ARGB, accurate_rnd);                            \
    SET_FF_NVX_TO_RGBX_FUNC(nvx, NVX, rgba, RGBA, accurate_rnd);                            \
    SET_FF_NVX_TO_RGBX_FUNC(nvx, NVX, abgr, ABGR, accurate_rnd);                            \
    SET_FF_NVX_TO_RGBX_FUNC(nvx, NVX, bgra, BGRA, accurate_rnd);                            \
    SET_FF_NVX_TO_RGBX_FUNC(nvx, NVX, gbrp, GBRP, accurate_rnd);                            \
    SET_FF_NVX_TO_RGBX_FUNC(nvx, NVX, rgb24, RGB24, accurate_rnd);                          \
    SET_FF_NVX_TO_RGBX_FUNC(nvx, NVX, bgr24, BGR24, accurate_rnd);                          \
} while (0)

static void get_unscaled_swscale_neon(SwsInternal *c) {
    int accurate_rnd = c->opts.flags & SWS_ACCURATE_RND;

    SET_FF_NVX_TO_ALL_RGBX_FUNC(nv12, NV12, accurate_rnd);
    SET_FF_NVX_TO_ALL_RGBX_FUNC(nv21, NV21, accurate_rnd);
    SET_FF_NVX_TO_ALL_RGBX_FUNC(yuv420p, YUV420P, accurate_rnd);
    SET_FF_NVX_TO_ALL_RGBX_FUNC(yuv422p, YUV422P, accurate_rnd);
    SET_FF_NVX_TO_RGBX_FUNC(yuva420p, YUVA420P, argb, ARGB, accurate_rnd);
    SET_FF_NVX_TO_RGBX_FUNC(yuva420p, YUVA420P, rgba, RGBA, accurate_rnd);
    SET_FF_NVX_TO_RGBX_FUNC(yuva420p, YUVA420P, abgr, ABGR, accurate_rnd);
    SET_FF_NVX_TO_RGBX_FUNC(yuva420p, YUVA420P, bgra, BGRA, accurate_rnd);
    SET_FF_NVX_TO_RGBX_FUNC(yuv420p, YUVA420P, rgb24, RGB24, accurate_rnd);
    SET_FF_NVX_TO_RGBX_FUNC(yuv420p, YUVA420P, bgr24, BGR24, accurate_rnd);
    SET_FF_NVX_TO_RGBX_FUNC(yuv420p, YUVA420P, gbrp,  GBRP,  accurate_rnd);

    if (c->opts.dst_format == AV_PIX_FMT_YUV420P &&
        (c->opts.src_format == AV_PIX_FMT_NV24 || c->opts.src_format == AV_PIX_FMT_NV42) &&
        !(c->opts.src_h & 1) && !(c->opts.src_w & 15) && !accurate_rnd)
        c->convert_unscaled = nv24_to_yuv420p_neon_wrapper;
}

void ff_get_unscaled_swscale_aarch64(SwsInternal *c)
{
    int cpu_flags = av_get_cpu_flags();
    if (have_neon(cpu_flags))
        get_unscaled_swscale_neon(c);
}

av_cold SwsFunc ff_yuv2rgb_init_aarch64(SwsInternal *c)
{
    int cpu_flags = av_get_cpu_flags();
    if (!have_neon(cpu_flags) ||
        (c->opts.src_h & 1) || (c->opts.src_w & 15) ||
        (c->opts.flags & SWS_ACCURATE_RND))
        return NULL;

    if (c->opts.src_format == AV_PIX_FMT_YUV420P) {
        switch (c->opts.dst_format) {
        case AV_PIX_FMT_ARGB:  return yuv420p_to_argb_neon_wrapper;
        case AV_PIX_FMT_RGBA:  return yuv420p_to_rgba_neon_wrapper;
        case AV_PIX_FMT_ABGR:  return yuv420p_to_abgr_neon_wrapper;
        case AV_PIX_FMT_BGRA:  return yuv420p_to_bgra_neon_wrapper;
        case AV_PIX_FMT_RGB24: return yuv420p_to_rgb24_neon_wrapper;
        case AV_PIX_FMT_BGR24: return yuv420p_to_bgr24_neon_wrapper;
        case AV_PIX_FMT_GBRP:  return yuv420p_to_gbrp_neon_wrapper;
        }
    } else if (c->opts.src_format == AV_PIX_FMT_YUVA420P) {
        switch (c->opts.dst_format) {
#if CONFIG_SWSCALE_ALPHA
        case AV_PIX_FMT_ARGB:  return yuva420p_to_argb_neon_wrapper;
        case AV_PIX_FMT_RGBA:  return yuva420p_to_rgba_neon_wrapper;
        case AV_PIX_FMT_ABGR:  return yuva420p_to_abgr_neon_wrapper;
        case AV_PIX_FMT_BGRA:  return yuva420p_to_bgra_neon_wrapper;
#endif
        case AV_PIX_FMT_RGB24: return yuv420p_to_rgb24_neon_wrapper;
        case AV_PIX_FMT_BGR24: return yuv420p_to_bgr24_neon_wrapper;
        case AV_PIX_FMT_GBRP:  return yuv420p_to_gbrp_neon_wrapper;
        }
    } else if (c->opts.src_format == AV_PIX_FMT_YUV422P) {
        switch (c->opts.dst_format) {
        case AV_PIX_FMT_ARGB:  return yuv422p_to_argb_neon_wrapper;
        case AV_PIX_FMT_RGBA:  return yuv422p_to_rgba_neon_wrapper;
        case AV_PIX_FMT_ABGR:  return yuv422p_to_abgr_neon_wrapper;
        case AV_PIX_FMT_BGRA:  return yuv422p_to_bgra_neon_wrapper;
        case AV_PIX_FMT_RGB24: return yuv422p_to_rgb24_neon_wrapper;
        case AV_PIX_FMT_BGR24: return yuv422p_to_bgr24_neon_wrapper;
        case AV_PIX_FMT_GBRP:  return yuv422p_to_gbrp_neon_wrapper;
        }
    }
    return NULL;
}
