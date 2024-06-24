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
#include "libavutil/attributes.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/aarch64/cpu.h"

void ff_hscale16to15_4_neon_asm(int shift, int16_t *_dst, int dstW,
                      const uint8_t *_src, const int16_t *filter,
                      const int32_t *filterPos, int filterSize);
void ff_hscale16to15_X8_neon_asm(int shift, int16_t *_dst, int dstW,
                      const uint8_t *_src, const int16_t *filter,
                      const int32_t *filterPos, int filterSize);
void ff_hscale16to15_X4_neon_asm(int shift, int16_t *_dst, int dstW,
                      const uint8_t *_src, const int16_t *filter,
                      const int32_t *filterPos, int filterSize);
void ff_hscale16to19_4_neon_asm(int shift, int16_t *_dst, int dstW,
                      const uint8_t *_src, const int16_t *filter,
                      const int32_t *filterPos, int filterSize);
void ff_hscale16to19_X8_neon_asm(int shift, int16_t *_dst, int dstW,
                      const uint8_t *_src, const int16_t *filter,
                      const int32_t *filterPos, int filterSize);
void ff_hscale16to19_X4_neon_asm(int shift, int16_t *_dst, int dstW,
                      const uint8_t *_src, const int16_t *filter,
                      const int32_t *filterPos, int filterSize);

static void ff_hscale16to15_4_neon(SwsContext *c, int16_t *_dst, int dstW,
                      const uint8_t *_src, const int16_t *filter,
                      const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int sh              = desc->comp[0].depth - 1;

    if (sh<15) {
        sh = isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8 ? 13 : (desc->comp[0].depth - 1);
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) { /* float input are process like uint 16bpc */
        sh = 16 - 1;
    }
    ff_hscale16to15_4_neon_asm(sh, _dst, dstW, _src, filter, filterPos, filterSize);

}

static void ff_hscale16to15_X8_neon(SwsContext *c, int16_t *_dst, int dstW,
                      const uint8_t *_src, const int16_t *filter,
                      const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int sh              = desc->comp[0].depth - 1;

    if (sh<15) {
        sh = isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8 ? 13 : (desc->comp[0].depth - 1);
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) { /* float input are process like uint 16bpc */
        sh = 16 - 1;
    }
    ff_hscale16to15_X8_neon_asm(sh, _dst, dstW, _src, filter, filterPos, filterSize);

}

static void ff_hscale16to15_X4_neon(SwsContext *c, int16_t *_dst, int dstW,
                      const uint8_t *_src, const int16_t *filter,
                      const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int sh              = desc->comp[0].depth - 1;

    if (sh<15) {
        sh = isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8 ? 13 : (desc->comp[0].depth - 1);
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) { /* float input are process like uint 16bpc */
        sh = 16 - 1;
    }
    ff_hscale16to15_X4_neon_asm(sh, _dst, dstW, _src, filter, filterPos, filterSize);
}

static void ff_hscale16to19_4_neon(SwsContext *c, int16_t *_dst, int dstW,
                           const uint8_t *_src, const int16_t *filter,
                           const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int bits            = desc->comp[0].depth - 1;
    int sh              = bits - 4;

    if ((isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8) && desc->comp[0].depth<16) {
        sh = 9;
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) { /* float input are process like uint 16bpc */
        sh = 16 - 1 - 4;
    }

    ff_hscale16to19_4_neon_asm(sh, _dst, dstW, _src, filter, filterPos, filterSize);

}

static void ff_hscale16to19_X8_neon(SwsContext *c, int16_t *_dst, int dstW,
                           const uint8_t *_src, const int16_t *filter,
                           const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int bits            = desc->comp[0].depth - 1;
    int sh              = bits - 4;

    if ((isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8) && desc->comp[0].depth<16) {
        sh = 9;
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) { /* float input are process like uint 16bpc */
        sh = 16 - 1 - 4;
    }

    ff_hscale16to19_X8_neon_asm(sh, _dst, dstW, _src, filter, filterPos, filterSize);

}

static void ff_hscale16to19_X4_neon(SwsContext *c, int16_t *_dst, int dstW,
                           const uint8_t *_src, const int16_t *filter,
                           const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int bits            = desc->comp[0].depth - 1;
    int sh              = bits - 4;

    if ((isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8) && desc->comp[0].depth<16) {
        sh = 9;
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) { /* float input are process like uint 16bpc */
        sh = 16 - 1 - 4;
    }

    ff_hscale16to19_X4_neon_asm(sh, _dst, dstW, _src, filter, filterPos, filterSize);

}

#define SCALE_FUNC(filter_n, from_bpc, to_bpc, opt) \
void ff_hscale ## from_bpc ## to ## to_bpc ## _ ## filter_n ## _ ## opt( \
                                                SwsContext *c, int16_t *data, \
                                                int dstW, const uint8_t *src, \
                                                const int16_t *filter, \
                                                const int32_t *filterPos, int filterSize)
#define SCALE_FUNCS(filter_n, opt) \
    SCALE_FUNC(filter_n, 8, 15, opt); \
    SCALE_FUNC(filter_n, 8, 19, opt);
#define ALL_SCALE_FUNCS(opt) \
    SCALE_FUNCS(4, opt); \
    SCALE_FUNCS(X8, opt); \
    SCALE_FUNCS(X4, opt)

ALL_SCALE_FUNCS(neon);

void ff_yuv2planeX_8_neon(const int16_t *filter, int filterSize,
                          const int16_t **src, uint8_t *dest, int dstW,
                          const uint8_t *dither, int offset);
void ff_yuv2plane1_8_neon(
        const int16_t *src,
        uint8_t *dest,
        int dstW,
        const uint8_t *dither,
        int offset);

#define ASSIGN_SCALE_FUNC2(hscalefn, filtersize, opt) do {              \
    if (c->srcBpc == 8) {                                               \
        if(c->dstBpc <= 14) {                                           \
            hscalefn =                                                  \
                ff_hscale8to15_ ## filtersize ## _ ## opt;              \
        } else                                                          \
            hscalefn =                                                  \
                ff_hscale8to19_ ## filtersize ## _ ## opt;              \
    } else {                                                            \
        if (c->dstBpc <= 14)                                            \
            hscalefn =                                                  \
                ff_hscale16to15_ ## filtersize ## _ ## opt;             \
        else                                                            \
            hscalefn =                                                  \
                ff_hscale16to19_ ## filtersize ## _ ## opt;             \
    }                                                                   \
} while (0)

#define ASSIGN_SCALE_FUNC(hscalefn, filtersize, opt) do {               \
    if (filtersize == 4)                                                \
        ASSIGN_SCALE_FUNC2(hscalefn, 4, opt);                           \
    else if (filtersize % 8 == 0)                                       \
        ASSIGN_SCALE_FUNC2(hscalefn, X8, opt);                          \
    else if (filtersize % 4 == 0 && filtersize % 8 != 0)                \
        ASSIGN_SCALE_FUNC2(hscalefn, X4, opt);                          \
} while (0)

#define ASSIGN_VSCALE_FUNC(vscalefn, opt)                               \
    switch (c->dstBpc) {                                                \
    case 8: vscalefn = ff_yuv2plane1_8_  ## opt;  break;                \
    default: break;                                                     \
    }

#define NEON_INPUT(name) \
void ff_##name##ToY_neon(uint8_t *dst, const uint8_t *src, const uint8_t *, \
                        const uint8_t *, int w, uint32_t *coeffs, void *); \
void ff_##name##ToUV_neon(uint8_t *, uint8_t *, const uint8_t *, \
                         const uint8_t *, const uint8_t *, int w, \
                         uint32_t *coeffs, void *); \
void ff_##name##ToUV_half_neon(uint8_t *, uint8_t *, const uint8_t *, \
                              const uint8_t *, const uint8_t *, int w, \
                              uint32_t *coeffs, void *)

NEON_INPUT(abgr32);
NEON_INPUT(argb32);
NEON_INPUT(bgr24);
NEON_INPUT(bgra32);
NEON_INPUT(rgb24);
NEON_INPUT(rgba32);

void ff_lumRangeFromJpeg_neon(int16_t *dst, int width);
void ff_chrRangeFromJpeg_neon(int16_t *dstU, int16_t *dstV, int width);
void ff_lumRangeToJpeg_neon(int16_t *dst, int width);
void ff_chrRangeToJpeg_neon(int16_t *dstU, int16_t *dstV, int width);

av_cold void ff_sws_init_range_convert_aarch64(SwsContext *c)
{
    if (c->srcRange != c->dstRange && !isAnyRGB(c->dstFormat)) {
        if (c->dstBpc <= 14) {
            if (c->srcRange) {
                c->lumConvertRange = ff_lumRangeFromJpeg_neon;
                c->chrConvertRange = ff_chrRangeFromJpeg_neon;
            } else {
                c->lumConvertRange = ff_lumRangeToJpeg_neon;
                c->chrConvertRange = ff_chrRangeToJpeg_neon;
            }
        }
    }
}

av_cold void ff_sws_init_swscale_aarch64(SwsContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        ASSIGN_SCALE_FUNC(c->hyScale, c->hLumFilterSize, neon);
        ASSIGN_SCALE_FUNC(c->hcScale, c->hChrFilterSize, neon);
        ASSIGN_VSCALE_FUNC(c->yuv2plane1, neon);
        if (c->dstBpc == 8) {
            c->yuv2planeX = ff_yuv2planeX_8_neon;
        }
        switch (c->srcFormat) {
        case AV_PIX_FMT_ABGR:
            c->lumToYV12 = ff_abgr32ToY_neon;
            if (c->chrSrcHSubSample)
                c->chrToYV12 = ff_abgr32ToUV_half_neon;
            else
                c->chrToYV12 = ff_abgr32ToUV_neon;
            break;

        case AV_PIX_FMT_ARGB:
            c->lumToYV12 = ff_argb32ToY_neon;
            if (c->chrSrcHSubSample)
                c->chrToYV12 = ff_argb32ToUV_half_neon;
            else
                c->chrToYV12 = ff_argb32ToUV_neon;
            break;
        case AV_PIX_FMT_BGR24:
            c->lumToYV12 = ff_bgr24ToY_neon;
            if (c->chrSrcHSubSample)
                c->chrToYV12 = ff_bgr24ToUV_half_neon;
            else
                c->chrToYV12 = ff_bgr24ToUV_neon;
            break;
        case AV_PIX_FMT_BGRA:
            c->lumToYV12 = ff_bgra32ToY_neon;
            if (c->chrSrcHSubSample)
                c->chrToYV12 = ff_bgra32ToUV_half_neon;
            else
                c->chrToYV12 = ff_bgra32ToUV_neon;
            break;
        case AV_PIX_FMT_RGB24:
            c->lumToYV12 = ff_rgb24ToY_neon;
            if (c->chrSrcHSubSample)
                c->chrToYV12 = ff_rgb24ToUV_half_neon;
            else
                c->chrToYV12 = ff_rgb24ToUV_neon;
            break;
        case AV_PIX_FMT_RGBA:
            c->lumToYV12 = ff_rgba32ToY_neon;
            if (c->chrSrcHSubSample)
                c->chrToYV12 = ff_rgba32ToUV_half_neon;
            else
                c->chrToYV12 = ff_rgba32ToUV_neon;
            break;
        default:
            break;
        }
        ff_sws_init_range_convert_aarch64(c);
    }
}
