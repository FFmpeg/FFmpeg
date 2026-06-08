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

#define DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(ifmt, ofmt)                                     \
int ff_##ifmt##_to_##ofmt##_neon(int w, int h,                                              \
                                 int y_offset,                                              \
                                 int y_coeff,                                               \
                                 const int16_t *table,                                      \
                                 const uint8_t *const src[], const int srcStride[],         \
                                 uint8_t *dst, int linesize);                               \
                                                                                            \
static int ifmt##_to_##ofmt##_neon_wrapper(SwsInternal *c, const uint8_t *const src[],      \
                                           const int srcStride[], int srcSliceY,            \
                                           int srcSliceH, uint8_t *const dst[],             \
                                           const int dstStride[]) {                         \
    const int16_t yuv2rgb_table[] = { YUV_TO_RGB_TABLE };                                   \
                                                                                            \
    return ff_##ifmt##_to_##ofmt##_neon(c->opts.src_w, srcSliceH,                           \
                                        c->yuv2rgb_y_offset >> 6,                           \
                                        c->yuv2rgb_y_coeff,                                 \
                                        yuv2rgb_table,                                      \
                                        src, srcStride,                                     \
                                        dst[0] + srcSliceY * dstStride[0],                  \
                                        dstStride[0]);                                      \
}                                                                                           \

#define DECLARE_FF_YUVX_TO_PLANAR_RGB_FUNCS(ifmt, ofmt)                                     \
int ff_##ifmt##_to_##ofmt##_neon(int w, int h,                                              \
                                 int y_offset,                                              \
                                 int y_coeff,                                               \
                                 const int16_t *table,                                      \
                                 const uint8_t *const src[], const int srcStride[],         \
                                 uint8_t *dst0, int linesize0,                              \
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
                                        c->yuv2rgb_y_offset >> 6,                           \
                                        c->yuv2rgb_y_coeff,                                 \
                                        yuv2rgb_table,                                      \
                                        src, srcStride,                                     \
                                        dst[0] + srcSliceY * dstStride[0], dstStride[0],    \
                                        dst[1] + srcSliceY * dstStride[1], dstStride[1],    \
                                        dst[2] + srcSliceY * dstStride[2], dstStride[2]);   \
}                                                                                           \

#define DECLARE_FF_YUVX_TO_ALL_RGBX_FUNCS(yuvx)                                             \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, argb)                                             \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, rgba)                                             \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, abgr)                                             \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, bgra)                                             \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, rgb24)                                            \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, bgr24)                                            \
DECLARE_FF_YUVX_TO_PLANAR_RGB_FUNCS(yuvx, gbrp)                                             \

DECLARE_FF_YUVX_TO_ALL_RGBX_FUNCS(nv12)
DECLARE_FF_YUVX_TO_ALL_RGBX_FUNCS(nv21)
DECLARE_FF_YUVX_TO_ALL_RGBX_FUNCS(yuv420p)
DECLARE_FF_YUVX_TO_ALL_RGBX_FUNCS(yuv422p)

#define DECLARE_FF_YUVX_TO_ALL_RGB16_FUNCS(yuvx)                                            \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, rgb565le)                                         \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, bgr565le)                                         \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, rgb555le)                                         \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, bgr555le)                                         \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, rgb565be)                                         \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, bgr565be)                                         \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, rgb555be)                                         \
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuvx, bgr555be)                                         \

DECLARE_FF_YUVX_TO_ALL_RGB16_FUNCS(nv12)
DECLARE_FF_YUVX_TO_ALL_RGB16_FUNCS(nv21)
DECLARE_FF_YUVX_TO_ALL_RGB16_FUNCS(yuv420p)
DECLARE_FF_YUVX_TO_ALL_RGB16_FUNCS(yuv422p)

DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuva420p, argb)
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuva420p, rgba)
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuva420p, abgr)
DECLARE_FF_YUVX_TO_PACKED_RGB_FUNCS(yuva420p, bgra)

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

/* We need a 16 pixel width alignment. This constraint can easily be removed
 * for input reading but for the output which is 4-bytes per pixel (RGBA) the
 * assembly might be writing as much as 4*15=60 extra bytes at the end of the
 * line, which won't fit the 32-bytes buffer alignment. */
#define SET_FF_YUVX_TO_RGBX_FUNC(ifmt, IFMT, ofmt, OFMT, accurate_rnd) do {                 \
    if (c->opts.src_format == AV_PIX_FMT_##IFMT                                             \
        && c->opts.dst_format == AV_PIX_FMT_##OFMT                                          \
        && !(c->opts.src_h & 1)                                                             \
        && !(c->opts.src_w & 15)                                                            \
        && !accurate_rnd)                                                                   \
        c->convert_unscaled = ifmt##_to_##ofmt##_neon_wrapper;                              \
} while (0)

#define SET_FF_YUVX_TO_ALL_RGBX_FUNC(yuvx, YUVX, accurate_rnd) do {                         \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, argb,  ARGB,  accurate_rnd);                       \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, rgba,  RGBA,  accurate_rnd);                       \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, abgr,  ABGR,  accurate_rnd);                       \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, bgra,  BGRA,  accurate_rnd);                       \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, gbrp,  GBRP,  accurate_rnd);                       \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, rgb24, RGB24, accurate_rnd);                       \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, bgr24, BGR24, accurate_rnd);                       \
} while (0)

#define SET_FF_YUVX_TO_ALL_RGB16_FUNC(yuvx, YUVX, accurate_rnd) do {                        \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, rgb565le, RGB565LE, accurate_rnd);                 \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, bgr565le, BGR565LE, accurate_rnd);                 \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, rgb555le, RGB555LE, accurate_rnd);                 \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, bgr555le, BGR555LE, accurate_rnd);                 \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, rgb565be, RGB565BE, accurate_rnd);                 \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, bgr565be, BGR565BE, accurate_rnd);                 \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, rgb555be, RGB555BE, accurate_rnd);                 \
    SET_FF_YUVX_TO_RGBX_FUNC(yuvx, YUVX, bgr555be, BGR555BE, accurate_rnd);                 \
} while (0)

static void get_unscaled_swscale_neon(SwsInternal *c) {
    int accurate_rnd = c->opts.flags & SWS_ACCURATE_RND;

    SET_FF_YUVX_TO_ALL_RGBX_FUNC(nv12,    NV12,    accurate_rnd);
    SET_FF_YUVX_TO_ALL_RGBX_FUNC(nv21,    NV21,    accurate_rnd);
    SET_FF_YUVX_TO_ALL_RGBX_FUNC(yuv420p, YUV420P, accurate_rnd);
    SET_FF_YUVX_TO_ALL_RGBX_FUNC(yuv422p, YUV422P, accurate_rnd);
    SET_FF_YUVX_TO_ALL_RGB16_FUNC(nv12,    NV12,    accurate_rnd);
    SET_FF_YUVX_TO_ALL_RGB16_FUNC(nv21,    NV21,    accurate_rnd);
    SET_FF_YUVX_TO_ALL_RGB16_FUNC(yuv420p, YUV420P, accurate_rnd);
    SET_FF_YUVX_TO_ALL_RGB16_FUNC(yuv422p, YUV422P, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuva420p, YUVA420P, argb, ARGB, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuva420p, YUVA420P, rgba, RGBA, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuva420p, YUVA420P, abgr, ABGR, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuva420p, YUVA420P, bgra, BGRA, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuv420p, YUVA420P, rgb24, RGB24, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuv420p, YUVA420P, bgr24, BGR24, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuv420p, YUVA420P, gbrp,  GBRP,  accurate_rnd);
    /* yuva420p -> 16bpp: alpha is dropped, route through yuv420p NEON path */
    SET_FF_YUVX_TO_RGBX_FUNC(yuv420p, YUVA420P, rgb565le, RGB565LE, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuv420p, YUVA420P, bgr565le, BGR565LE, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuv420p, YUVA420P, rgb555le, RGB555LE, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuv420p, YUVA420P, bgr555le, BGR555LE, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuv420p, YUVA420P, rgb565be, RGB565BE, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuv420p, YUVA420P, bgr565be, BGR565BE, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuv420p, YUVA420P, rgb555be, RGB555BE, accurate_rnd);
    SET_FF_YUVX_TO_RGBX_FUNC(yuv420p, YUVA420P, bgr555be, BGR555BE, accurate_rnd);

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
        case AV_PIX_FMT_RGB565LE: return yuv420p_to_rgb565le_neon_wrapper;
        case AV_PIX_FMT_BGR565LE: return yuv420p_to_bgr565le_neon_wrapper;
        case AV_PIX_FMT_RGB555LE: return yuv420p_to_rgb555le_neon_wrapper;
        case AV_PIX_FMT_BGR555LE: return yuv420p_to_bgr555le_neon_wrapper;
        case AV_PIX_FMT_RGB565BE: return yuv420p_to_rgb565be_neon_wrapper;
        case AV_PIX_FMT_BGR565BE: return yuv420p_to_bgr565be_neon_wrapper;
        case AV_PIX_FMT_RGB555BE: return yuv420p_to_rgb555be_neon_wrapper;
        case AV_PIX_FMT_BGR555BE: return yuv420p_to_bgr555be_neon_wrapper;
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
        /* 16bpp targets drop alpha, share yuv420p path */
        case AV_PIX_FMT_RGB565LE: return yuv420p_to_rgb565le_neon_wrapper;
        case AV_PIX_FMT_BGR565LE: return yuv420p_to_bgr565le_neon_wrapper;
        case AV_PIX_FMT_RGB555LE: return yuv420p_to_rgb555le_neon_wrapper;
        case AV_PIX_FMT_BGR555LE: return yuv420p_to_bgr555le_neon_wrapper;
        case AV_PIX_FMT_RGB565BE: return yuv420p_to_rgb565be_neon_wrapper;
        case AV_PIX_FMT_BGR565BE: return yuv420p_to_bgr565be_neon_wrapper;
        case AV_PIX_FMT_RGB555BE: return yuv420p_to_rgb555be_neon_wrapper;
        case AV_PIX_FMT_BGR555BE: return yuv420p_to_bgr555be_neon_wrapper;
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
        case AV_PIX_FMT_RGB565LE: return yuv422p_to_rgb565le_neon_wrapper;
        case AV_PIX_FMT_BGR565LE: return yuv422p_to_bgr565le_neon_wrapper;
        case AV_PIX_FMT_RGB555LE: return yuv422p_to_rgb555le_neon_wrapper;
        case AV_PIX_FMT_BGR555LE: return yuv422p_to_bgr555le_neon_wrapper;
        case AV_PIX_FMT_RGB565BE: return yuv422p_to_rgb565be_neon_wrapper;
        case AV_PIX_FMT_BGR565BE: return yuv422p_to_bgr565be_neon_wrapper;
        case AV_PIX_FMT_RGB555BE: return yuv422p_to_rgb555be_neon_wrapper;
        case AV_PIX_FMT_BGR555BE: return yuv422p_to_bgr555be_neon_wrapper;
        }
    }
    return NULL;
}
