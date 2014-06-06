/*
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
#include "libavutil/internal.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/simple_idct.h"
#include "libavcodec/version.h"
#include "dsputil_x86.h"
#include "idct_xvid.h"

int32_t ff_scalarproduct_int16_mmxext(const int16_t *v1, const int16_t *v2,
                                      int order);
int32_t ff_scalarproduct_int16_sse2(const int16_t *v1, const int16_t *v2,
                                    int order);

void ff_bswap32_buf_ssse3(uint32_t *dst, const uint32_t *src, int w);
void ff_bswap32_buf_sse2(uint32_t *dst, const uint32_t *src, int w);

void ff_vector_clip_int32_mmx(int32_t *dst, const int32_t *src,
                              int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_sse2(int32_t *dst, const int32_t *src,
                               int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_int_sse2(int32_t *dst, const int32_t *src,
                                   int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_sse4(int32_t *dst, const int32_t *src,
                               int32_t min, int32_t max, unsigned int len);

static av_cold void dsputil_init_mmx(DSPContext *c, AVCodecContext *avctx,
                                     int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_MMX_INLINE
    c->put_pixels_clamped        = ff_put_pixels_clamped_mmx;
    c->add_pixels_clamped        = ff_add_pixels_clamped_mmx;

    if (!high_bit_depth) {
        c->draw_edges   = ff_draw_edges_mmx;
    }

    if (avctx->lowres == 0 && !high_bit_depth) {
        switch (avctx->idct_algo) {
        case FF_IDCT_AUTO:
        case FF_IDCT_SIMPLEMMX:
            c->idct_put              = ff_simple_idct_put_mmx;
            c->idct_add              = ff_simple_idct_add_mmx;
            c->idct                  = ff_simple_idct_mmx;
            c->idct_permutation_type = FF_SIMPLE_IDCT_PERM;
            break;
        case FF_IDCT_XVIDMMX:
            c->idct_put              = ff_idct_xvid_mmx_put;
            c->idct_add              = ff_idct_xvid_mmx_add;
            c->idct                  = ff_idct_xvid_mmx;
            break;
        }
    }

#if CONFIG_VIDEODSP && (ARCH_X86_32 || !HAVE_YASM)
    c->gmc = ff_gmc_mmx;
#endif
#endif /* HAVE_MMX_INLINE */

#if HAVE_MMX_EXTERNAL
    if (!high_bit_depth) {
        c->clear_block  = ff_clear_block_mmx;
        c->clear_blocks = ff_clear_blocks_mmx;
    }
    c->vector_clip_int32 = ff_vector_clip_int32_mmx;
    c->put_signed_pixels_clamped = ff_put_signed_pixels_clamped_mmx;
#endif /* HAVE_MMX_EXTERNAL */
}

static av_cold void dsputil_init_mmxext(DSPContext *c, AVCodecContext *avctx,
                                        int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_MMXEXT_INLINE
    if (!high_bit_depth && avctx->idct_algo == FF_IDCT_XVIDMMX && avctx->lowres == 0) {
        c->idct_put = ff_idct_xvid_mmxext_put;
        c->idct_add = ff_idct_xvid_mmxext_add;
        c->idct     = ff_idct_xvid_mmxext;
    }
#endif /* HAVE_MMXEXT_INLINE */

#if HAVE_MMXEXT_EXTERNAL
    c->scalarproduct_int16          = ff_scalarproduct_int16_mmxext;
#endif /* HAVE_MMXEXT_EXTERNAL */
}

static av_cold void dsputil_init_sse(DSPContext *c, AVCodecContext *avctx,
                                     int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_YASM
#if HAVE_SSE_EXTERNAL
    c->vector_clipf = ff_vector_clipf_sse;

    /* XvMCCreateBlocks() may not allocate 16-byte aligned blocks */
    if (CONFIG_XVMC && avctx->hwaccel && avctx->hwaccel->decode_mb)
        return;

    if (!high_bit_depth) {
        c->clear_block  = ff_clear_block_sse;
        c->clear_blocks = ff_clear_blocks_sse;
    }
#endif
#if HAVE_INLINE_ASM && CONFIG_VIDEODSP
    c->gmc = ff_gmc_sse;
#endif
#endif /* HAVE_YASM */
}

static av_cold void dsputil_init_sse2(DSPContext *c, AVCodecContext *avctx,
                                      int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_SSE2_INLINE
    if (!high_bit_depth && avctx->idct_algo == FF_IDCT_XVIDMMX && avctx->lowres == 0) {
        c->idct_put              = ff_idct_xvid_sse2_put;
        c->idct_add              = ff_idct_xvid_sse2_add;
        c->idct                  = ff_idct_xvid_sse2;
        c->idct_permutation_type = FF_SSE2_IDCT_PERM;
    }
#endif /* HAVE_SSE2_INLINE */

#if HAVE_SSE2_EXTERNAL
    c->scalarproduct_int16          = ff_scalarproduct_int16_sse2;
    if (cpu_flags & AV_CPU_FLAG_ATOM) {
        c->vector_clip_int32 = ff_vector_clip_int32_int_sse2;
    } else {
        c->vector_clip_int32 = ff_vector_clip_int32_sse2;
    }
    c->bswap_buf = ff_bswap32_buf_sse2;
    c->put_signed_pixels_clamped = ff_put_signed_pixels_clamped_sse2;
#endif /* HAVE_SSE2_EXTERNAL */
}

static av_cold void dsputil_init_ssse3(DSPContext *c, AVCodecContext *avctx,
                                       int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_SSSE3_EXTERNAL
    c->bswap_buf = ff_bswap32_buf_ssse3;
#endif /* HAVE_SSSE3_EXTERNAL */
}

static av_cold void dsputil_init_sse4(DSPContext *c, AVCodecContext *avctx,
                                      int cpu_flags, unsigned high_bit_depth)
{
#if HAVE_SSE4_EXTERNAL
    c->vector_clip_int32 = ff_vector_clip_int32_sse4;
#endif /* HAVE_SSE4_EXTERNAL */
}

av_cold void ff_dsputil_init_x86(DSPContext *c, AVCodecContext *avctx,
                                 unsigned high_bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (X86_MMX(cpu_flags))
        dsputil_init_mmx(c, avctx, cpu_flags, high_bit_depth);

    if (X86_MMXEXT(cpu_flags))
        dsputil_init_mmxext(c, avctx, cpu_flags, high_bit_depth);

    if (X86_SSE(cpu_flags))
        dsputil_init_sse(c, avctx, cpu_flags, high_bit_depth);

    if (X86_SSE2(cpu_flags))
        dsputil_init_sse2(c, avctx, cpu_flags, high_bit_depth);

    if (EXTERNAL_SSSE3(cpu_flags))
        dsputil_init_ssse3(c, avctx, cpu_flags, high_bit_depth);

    if (EXTERNAL_SSE4(cpu_flags))
        dsputil_init_sse4(c, avctx, cpu_flags, high_bit_depth);

    if (CONFIG_ENCODERS)
        ff_dsputilenc_init_mmx(c, avctx, high_bit_depth);
}
