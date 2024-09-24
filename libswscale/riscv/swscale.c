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
#include "libavutil/riscv/cpu.h"
#include "libswscale/swscale_internal.h"

void ff_range_lum_to_jpeg_16_rvv(int16_t *, int);
void ff_range_chr_to_jpeg_16_rvv(int16_t *, int16_t *, int);
void ff_range_lum_from_jpeg_16_rvv(int16_t *, int);
void ff_range_chr_from_jpeg_16_rvv(int16_t *, int16_t *, int);

av_cold void ff_sws_init_range_convert_riscv(SwsInternal *c)
{
    /* This code is currently disabled because of changes in the base
     * implementation of these functions. This code should be enabled
     * again once those changes are ported to this architecture. */
#if 0
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    static const struct {
        void (*lum)(int16_t *, int);
        void (*chr)(int16_t *, int16_t *, int);
    } convs[2] = {
        { ff_range_lum_to_jpeg_16_rvv, ff_range_chr_to_jpeg_16_rvv },
        { ff_range_lum_from_jpeg_16_rvv, ff_range_chr_from_jpeg_16_rvv },
    };

    if (c->dstBpc <= 14 &&
        (flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB)) {
        bool from = c->opts.src_range != 0;

        c->lumConvertRange = convs[from].lum;
        c->chrConvertRange = convs[from].chr;
    }
#endif
#endif
}

#define RVV_INPUT(name) \
void ff_##name##ToY_rvv(uint8_t *dst, const uint8_t *src, const uint8_t *, \
                        const uint8_t *, int w, uint32_t *coeffs, void *); \
void ff_##name##ToUV_rvv(uint8_t *, uint8_t *, const uint8_t *, \
                         const uint8_t *, const uint8_t *, int w, \
                         uint32_t *coeffs, void *); \
void ff_##name##ToUV_half_rvv(uint8_t *, uint8_t *, const uint8_t *, \
                              const uint8_t *, const uint8_t *, int w, \
                              uint32_t *coeffs, void *)

RVV_INPUT(abgr32);
RVV_INPUT(argb32);
RVV_INPUT(bgr24);
RVV_INPUT(bgra32);
RVV_INPUT(rgb24);
RVV_INPUT(rgba32);

av_cold void ff_sws_init_swscale_riscv(SwsInternal *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB)) {
        switch (c->opts.src_format) {
            case AV_PIX_FMT_ABGR:
                c->lumToYV12 = ff_abgr32ToY_rvv;
                if (c->chrSrcHSubSample)
                    c->chrToYV12 = ff_abgr32ToUV_half_rvv;
                else
                    c->chrToYV12 = ff_abgr32ToUV_rvv;
                break;

            case AV_PIX_FMT_ARGB:
                c->lumToYV12 = ff_argb32ToY_rvv;
                if (c->chrSrcHSubSample)
                    c->chrToYV12 = ff_argb32ToUV_half_rvv;
                else
                    c->chrToYV12 = ff_argb32ToUV_rvv;
                break;

            case AV_PIX_FMT_BGR24:
                c->lumToYV12 = ff_bgr24ToY_rvv;
                if (c->chrSrcHSubSample)
                    c->chrToYV12 = ff_bgr24ToUV_half_rvv;
                else
                    c->chrToYV12 = ff_bgr24ToUV_rvv;
                break;

            case AV_PIX_FMT_BGRA:
                c->lumToYV12 = ff_bgra32ToY_rvv;
                if (c->chrSrcHSubSample)
                    c->chrToYV12 = ff_bgra32ToUV_half_rvv;
                else
                    c->chrToYV12 = ff_bgra32ToUV_rvv;
                break;

            case AV_PIX_FMT_RGB24:
                c->lumToYV12 = ff_rgb24ToY_rvv;
                if (c->chrSrcHSubSample)
                    c->chrToYV12 = ff_rgb24ToUV_half_rvv;
                else
                    c->chrToYV12 = ff_rgb24ToUV_rvv;
                break;

            case AV_PIX_FMT_RGBA:
                c->lumToYV12 = ff_rgba32ToY_rvv;
                if (c->chrSrcHSubSample)
                    c->chrToYV12 = ff_rgba32ToUV_half_rvv;
                else
                    c->chrToYV12 = ff_rgba32ToUV_rvv;
                break;
        }
    }
#endif
}
