/*
 * Copyright (C) 2009 Loren Merritt <lorenm@u.washington.edu>
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
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/gradfun.h"

void ff_gradfun_filter_line_mmxext(intptr_t x, uint8_t *dst, const uint8_t *src,
                                   const uint16_t *dc, int thresh,
                                   const uint16_t *dithers);
void ff_gradfun_filter_line_ssse3(intptr_t x, uint8_t *dst, const uint8_t *src,
                                  const uint16_t *dc, int thresh,
                                  const uint16_t *dithers);

void ff_gradfun_blur_line_movdqa_sse2(intptr_t x, uint16_t *buf,
                                      const uint16_t *buf1, uint16_t *dc,
                                      const uint8_t *src1, const uint8_t *src2);
void ff_gradfun_blur_line_movdqu_sse2(intptr_t x, uint16_t *buf,
                                      const uint16_t *buf1, uint16_t *dc,
                                      const uint8_t *src1, const uint8_t *src2);

#if HAVE_YASM
static void gradfun_filter_line_mmxext(uint8_t *dst, const uint8_t *src,
                                       const uint16_t *dc,
                                       int width, int thresh,
                                       const uint16_t *dithers)
{
    intptr_t x;
    if (width & 3) {
        x = width & ~3;
        ff_gradfun_filter_line_c(dst + x, src + x, dc + x / 2,
                                 width - x, thresh, dithers);
        width = x;
    }
    x = -width;
    ff_gradfun_filter_line_mmxext(x, dst + width, src + width, dc + width / 2,
                                  thresh, dithers);
}

static void gradfun_filter_line_ssse3(uint8_t *dst, const uint8_t *src, const uint16_t *dc,
                                      int width, int thresh,
                                      const uint16_t *dithers)
{
    intptr_t x;
    if (width & 7) {
        // could be 10% faster if I somehow eliminated this
        x = width & ~7;
        ff_gradfun_filter_line_c(dst + x, src + x, dc + x / 2,
                                 width - x, thresh, dithers);
        width = x;
    }
    x = -width;
    ff_gradfun_filter_line_ssse3(x, dst + width, src + width, dc + width / 2,
                                 thresh, dithers);
}

static void gradfun_blur_line_sse2(uint16_t *dc, uint16_t *buf, const uint16_t *buf1,
                                   const uint8_t *src, int src_linesize, int width)
{
    intptr_t x = -2 * width;
    if (((intptr_t) src | src_linesize) & 15)
        ff_gradfun_blur_line_movdqu_sse2(x, buf + width, buf1 + width,
                                         dc + width, src + width * 2,
                                         src + width * 2 + src_linesize);
    else
        ff_gradfun_blur_line_movdqa_sse2(x, buf + width, buf1 + width,
                                         dc + width, src + width * 2,
                                         src + width * 2 + src_linesize);
}
#endif /* HAVE_YASM */

av_cold void ff_gradfun_init_x86(GradFunContext *gf)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMXEXT(cpu_flags))
        gf->filter_line = gradfun_filter_line_mmxext;
    if (EXTERNAL_SSSE3(cpu_flags))
        gf->filter_line = gradfun_filter_line_ssse3;

    if (EXTERNAL_SSE2(cpu_flags))
        gf->blur_line = gradfun_blur_line_sse2;
#endif /* HAVE_YASM */
}
