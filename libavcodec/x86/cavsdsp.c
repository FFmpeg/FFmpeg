/*
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder.
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
 *
 * MMX-optimized DSP functions, based on H.264 optimizations by
 * Michael Niedermayer and Loren Merritt
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem_internal.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/cavsdsp.h"
#include "libavcodec/idctdsp.h"
#include "fpel.h"
#include "idctdsp.h"
#include "config.h"


#if HAVE_SSE2_EXTERNAL

void ff_cavs_idct8_sse2(int16_t *out, const int16_t *in);

static void cavs_idct8_add_sse2(uint8_t *dst, int16_t *block, ptrdiff_t stride)
{
    LOCAL_ALIGNED(16, int16_t, b2, [64]);
    ff_cavs_idct8_sse2(b2, block);
    ff_add_pixels_clamped_sse2(b2, dst, stride);
}

#endif /* HAVE_SSE2_EXTERNAL */

static av_cold void cavsdsp_init_mmx(CAVSDSPContext *c)
{
#if HAVE_MMX_EXTERNAL
    c->put_cavs_qpel_pixels_tab[1][0] = ff_put_pixels8x8_mmx;
#endif /* HAVE_MMX_EXTERNAL */
}

#if HAVE_SSE2_EXTERNAL
#define DEF_QPEL(OPNAME) \
    void ff_ ## OPNAME ## _cavs_qpel8_mc20_sse2(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);     \
    void ff_ ## OPNAME ## _cavs_qpel8_mc02_sse2(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);     \
    void ff_ ## OPNAME ## _cavs_qpel8_mc03_sse2(uint8_t *dst, const uint8_t *src, ptrdiff_t stride);     \
    void ff_ ## OPNAME ## _cavs_qpel8_h_sse2(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h); \
    void ff_ ## OPNAME ## _cavs_qpel8_v2_sse2(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h);\
    void ff_ ## OPNAME ## _cavs_qpel8_v3_sse2(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h);\

DEF_QPEL(put)
DEF_QPEL(avg)

#define QPEL_CAVS_XMM(OPNAME, XMM) \
static void OPNAME ## _cavs_qpel16_mc02_ ## XMM(uint8_t *dst, const uint8_t *src, ptrdiff_t stride) \
{                                                                                                   \
    ff_ ## OPNAME ## _cavs_qpel8_v2_ ## XMM(dst,     src,     stride, 16);                          \
    ff_ ## OPNAME ## _cavs_qpel8_v2_ ## XMM(dst + 8, src + 8, stride, 16);                          \
}                                                                                                   \
static void OPNAME ## _cavs_qpel16_mc03_ ## XMM(uint8_t *dst, const uint8_t *src, ptrdiff_t stride) \
{                                                                                                   \
    ff_ ## OPNAME ## _cavs_qpel8_v3_ ## XMM(dst,     src,     stride, 16);                          \
    ff_ ## OPNAME ## _cavs_qpel8_v3_ ## XMM(dst + 8, src + 8, stride, 16);                          \
}                                                                                                   \
static void OPNAME ## _cavs_qpel8_mc01_ ## XMM(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)  \
{                                                                                                   \
    ff_ ## OPNAME ## _cavs_qpel8_mc03_ ## XMM(dst + 7 * stride, src + 8 * stride, -stride);         \
}                                                                                                   \
static void OPNAME ## _cavs_qpel16_mc01_ ## XMM(uint8_t *dst, const uint8_t *src, ptrdiff_t stride) \
{                                                                                                   \
    OPNAME ## _cavs_qpel16_mc03_ ## XMM(dst + 15 * stride, src + 16 * stride, -stride);             \
}                                                                                                   \
static void OPNAME ## _cavs_qpel16_mc20_ ## XMM(uint8_t *dst, const uint8_t *src, ptrdiff_t stride) \
{                                                                                                   \
    ff_ ## OPNAME ## _cavs_qpel8_h_ ## XMM(dst,     src,     stride, 16);                           \
    ff_ ## OPNAME ## _cavs_qpel8_h_ ## XMM(dst + 8, src + 8, stride, 16);                           \
}

QPEL_CAVS_XMM(put, sse2)
QPEL_CAVS_XMM(avg, sse2)
#endif

av_cold void ff_cavsdsp_init_x86(CAVSDSPContext *c)
{
    av_unused int cpu_flags = av_get_cpu_flags();

    if (X86_MMX(cpu_flags))
        cavsdsp_init_mmx(c);

#if HAVE_MMX_EXTERNAL
    if (EXTERNAL_MMXEXT(cpu_flags)) {
        c->avg_cavs_qpel_pixels_tab[1][0] = ff_avg_pixels8x8_mmxext;
    }
#endif
#if HAVE_SSE2_EXTERNAL
    if (EXTERNAL_SSE2(cpu_flags)) {
        c->put_cavs_qpel_pixels_tab[0][ 0] = ff_put_pixels16x16_sse2;
        c->put_cavs_qpel_pixels_tab[0][ 2] = put_cavs_qpel16_mc20_sse2;
        c->put_cavs_qpel_pixels_tab[0][ 4] = put_cavs_qpel16_mc01_sse2;
        c->put_cavs_qpel_pixels_tab[0][ 8] = put_cavs_qpel16_mc02_sse2;
        c->put_cavs_qpel_pixels_tab[0][12] = put_cavs_qpel16_mc03_sse2;
        c->put_cavs_qpel_pixels_tab[1][ 2] = ff_put_cavs_qpel8_mc20_sse2;
        c->put_cavs_qpel_pixels_tab[1][ 4] = put_cavs_qpel8_mc01_sse2;
        c->put_cavs_qpel_pixels_tab[1][ 8] = ff_put_cavs_qpel8_mc02_sse2;
        c->put_cavs_qpel_pixels_tab[1][12] = ff_put_cavs_qpel8_mc03_sse2;

        c->avg_cavs_qpel_pixels_tab[0][ 0] = ff_avg_pixels16x16_sse2;
        c->avg_cavs_qpel_pixels_tab[0][ 2] = avg_cavs_qpel16_mc20_sse2;
        c->avg_cavs_qpel_pixels_tab[0][ 4] = avg_cavs_qpel16_mc01_sse2;
        c->avg_cavs_qpel_pixels_tab[0][ 8] = avg_cavs_qpel16_mc02_sse2;
        c->avg_cavs_qpel_pixels_tab[0][12] = avg_cavs_qpel16_mc03_sse2;
        c->avg_cavs_qpel_pixels_tab[1][ 2] = ff_avg_cavs_qpel8_mc20_sse2;
        c->avg_cavs_qpel_pixels_tab[1][ 4] = avg_cavs_qpel8_mc01_sse2;
        c->avg_cavs_qpel_pixels_tab[1][ 8] = ff_avg_cavs_qpel8_mc02_sse2;
        c->avg_cavs_qpel_pixels_tab[1][12] = ff_avg_cavs_qpel8_mc03_sse2;

        c->cavs_idct8_add = cavs_idct8_add_sse2;
        c->idct_perm      = FF_IDCT_PERM_TRANSPOSE;
    }
#endif
}
