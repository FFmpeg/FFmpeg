/*
 * Copyright (C) 2002-2012 Michael Niedermayer
 * Copyright (C) 2012 Ronald S. Bultje
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
#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/videodsp.h"

#if HAVE_X86ASM
typedef void emu_edge_vfix_func(uint8_t *dst, x86_reg dst_stride,
                                const uint8_t *src, x86_reg src_stride,
                                x86_reg start_y, x86_reg end_y, x86_reg bh);
typedef void emu_edge_vvar_func(uint8_t *dst, x86_reg dst_stride,
                                const uint8_t *src, x86_reg src_stride,
                                x86_reg start_y, x86_reg end_y, x86_reg bh,
                                x86_reg w);

extern emu_edge_vfix_func ff_emu_edge_vfix1_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix2_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix3_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix4_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix5_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix6_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix7_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix8_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix9_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix10_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix11_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix12_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix13_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix14_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix15_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix16_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix17_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix18_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix19_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix20_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix21_sse2;
extern emu_edge_vfix_func ff_emu_edge_vfix22_sse2;
static emu_edge_vfix_func * const vfixtbl_sse2[22] = {
    ff_emu_edge_vfix1_sse2,  ff_emu_edge_vfix2_sse2,  ff_emu_edge_vfix3_sse2,
    ff_emu_edge_vfix4_sse2,  ff_emu_edge_vfix5_sse2,  ff_emu_edge_vfix6_sse2,
    ff_emu_edge_vfix7_sse2,  ff_emu_edge_vfix8_sse2,  ff_emu_edge_vfix9_sse2,
    ff_emu_edge_vfix10_sse2, ff_emu_edge_vfix11_sse2, ff_emu_edge_vfix12_sse2,
    ff_emu_edge_vfix13_sse2, ff_emu_edge_vfix14_sse2, ff_emu_edge_vfix15_sse2,
    ff_emu_edge_vfix16_sse2, ff_emu_edge_vfix17_sse2, ff_emu_edge_vfix18_sse2,
    ff_emu_edge_vfix19_sse2, ff_emu_edge_vfix20_sse2, ff_emu_edge_vfix21_sse2,
    ff_emu_edge_vfix22_sse2
};
extern emu_edge_vvar_func ff_emu_edge_vvar_sse;

typedef void emu_edge_hfix_func(uint8_t *dst, x86_reg dst_stride,
                                x86_reg start_x, x86_reg bh);
typedef void emu_edge_hvar_func(uint8_t *dst, x86_reg dst_stride,
                                x86_reg start_x, x86_reg n_words, x86_reg bh);

extern emu_edge_hfix_func ff_emu_edge_hfix2_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix4_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix6_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix8_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix10_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix12_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix14_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix16_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix18_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix20_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix22_sse2;
static emu_edge_hfix_func * const hfixtbl_sse2[11] = {
    ff_emu_edge_hfix2_sse2,  ff_emu_edge_hfix4_sse2,  ff_emu_edge_hfix6_sse2,
    ff_emu_edge_hfix8_sse2,  ff_emu_edge_hfix10_sse2, ff_emu_edge_hfix12_sse2,
    ff_emu_edge_hfix14_sse2, ff_emu_edge_hfix16_sse2, ff_emu_edge_hfix18_sse2,
    ff_emu_edge_hfix20_sse2, ff_emu_edge_hfix22_sse2
};
extern emu_edge_hvar_func ff_emu_edge_hvar_sse2;
#if HAVE_AVX2_EXTERNAL
extern emu_edge_hfix_func ff_emu_edge_hfix8_avx2;
extern emu_edge_hfix_func ff_emu_edge_hfix10_avx2;
extern emu_edge_hfix_func ff_emu_edge_hfix12_avx2;
extern emu_edge_hfix_func ff_emu_edge_hfix14_avx2;
extern emu_edge_hfix_func ff_emu_edge_hfix16_avx2;
extern emu_edge_hfix_func ff_emu_edge_hfix18_avx2;
extern emu_edge_hfix_func ff_emu_edge_hfix20_avx2;
extern emu_edge_hfix_func ff_emu_edge_hfix22_avx2;
static emu_edge_hfix_func * const hfixtbl_avx2[11] = {
    ff_emu_edge_hfix2_sse2,  ff_emu_edge_hfix4_sse2,  ff_emu_edge_hfix6_sse2,
    ff_emu_edge_hfix8_avx2,  ff_emu_edge_hfix10_avx2, ff_emu_edge_hfix12_avx2,
    ff_emu_edge_hfix14_avx2, ff_emu_edge_hfix16_avx2, ff_emu_edge_hfix18_avx2,
    ff_emu_edge_hfix20_avx2, ff_emu_edge_hfix22_avx2
};
extern emu_edge_hvar_func ff_emu_edge_hvar_avx2;
#endif

static av_always_inline void emulated_edge_mc(uint8_t *dst, const uint8_t *src,
                                              ptrdiff_t dst_stride,
                                              ptrdiff_t src_stride,
                                              x86_reg block_w, x86_reg block_h,
                                              x86_reg src_x, x86_reg src_y,
                                              x86_reg w, x86_reg h,
                                              emu_edge_vfix_func * const *vfix_tbl,
                                              emu_edge_vvar_func *v_extend_var,
                                              emu_edge_hfix_func * const *hfix_tbl,
                                              emu_edge_hvar_func *h_extend_var)
{
    x86_reg start_y, start_x, end_y, end_x, src_y_add = 0, p;

    if (!w || !h)
        return;

    av_assert2(block_w <= FFABS(dst_stride));

    if (src_y >= h) {
        src -= src_y*src_stride;
        src_y_add = h - 1;
        src_y     = h - 1;
    } else if (src_y <= -block_h) {
        src -= src_y*src_stride;
        src_y_add = 1 - block_h;
        src_y     = 1 - block_h;
    }
    if (src_x >= w) {
        src   += w - 1 - src_x;
        src_x  = w - 1;
    } else if (src_x <= -block_w) {
        src   += 1 - block_w - src_x;
        src_x  = 1 - block_w;
    }

    start_y = FFMAX(0, -src_y);
    start_x = FFMAX(0, -src_x);
    end_y   = FFMIN(block_h, h-src_y);
    end_x   = FFMIN(block_w, w-src_x);
    av_assert2(start_x < end_x && block_w > 0);
    av_assert2(start_y < end_y && block_h > 0);

    // fill in the to-be-copied part plus all above/below
    src += (src_y_add + start_y) * src_stride + start_x;
    w = end_x - start_x;
    if (w <= 22) {
        vfix_tbl[w - 1](dst + start_x, dst_stride, src, src_stride,
                        start_y, end_y, block_h);
    } else {
        v_extend_var(dst + start_x, dst_stride, src, src_stride,
                     start_y, end_y, block_h, w);
    }

    // fill left
    if (start_x) {
        if (start_x <= 22) {
            hfix_tbl[(start_x - 1) >> 1](dst, dst_stride, start_x, block_h);
        } else {
            h_extend_var(dst, dst_stride,
                         start_x, (start_x + 1) >> 1, block_h);
        }
    }

    // fill right
    p = block_w - end_x;
    if (p) {
        if (p <= 22) {
            hfix_tbl[(p - 1) >> 1](dst + end_x - (p & 1), dst_stride,
                                   -!(p & 1), block_h);
        } else {
            h_extend_var(dst + end_x - (p & 1), dst_stride,
                         -!(p & 1), (p + 1) >> 1, block_h);
        }
    }
}

static av_noinline void emulated_edge_mc_sse2(uint8_t *buf, const uint8_t *src,
                                              ptrdiff_t buf_stride,
                                              ptrdiff_t src_stride,
                                              int block_w, int block_h,
                                              int src_x, int src_y, int w,
                                              int h)
{
    emulated_edge_mc(buf, src, buf_stride, src_stride, block_w, block_h,
                     src_x, src_y, w, h, vfixtbl_sse2, &ff_emu_edge_vvar_sse,
                     hfixtbl_sse2, &ff_emu_edge_hvar_sse2);
}

#if HAVE_AVX2_EXTERNAL
static av_noinline void emulated_edge_mc_avx2(uint8_t *buf, const uint8_t *src,
                                              ptrdiff_t buf_stride,
                                              ptrdiff_t src_stride,
                                              int block_w, int block_h,
                                              int src_x, int src_y, int w,
                                              int h)
{
    emulated_edge_mc(buf, src, buf_stride, src_stride, block_w, block_h,
                     src_x, src_y, w, h, vfixtbl_sse2, &ff_emu_edge_vvar_sse,
                     hfixtbl_avx2, &ff_emu_edge_hvar_avx2);
}
#endif /* HAVE_AVX2_EXTERNAL */
#endif /* HAVE_X86ASM */

void ff_prefetch_mmxext(const uint8_t *buf, ptrdiff_t stride, int h);

av_cold void ff_videodsp_init_x86(VideoDSPContext *ctx, int bpc)
{
#if HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        ctx->prefetch = ff_prefetch_mmxext;
    }
    if (EXTERNAL_SSE2(cpu_flags) && bpc <= 8) {
        ctx->emulated_edge_mc = emulated_edge_mc_sse2;
    }
#if HAVE_AVX2_EXTERNAL
    if (EXTERNAL_AVX2(cpu_flags) && bpc <= 8) {
        ctx->emulated_edge_mc = emulated_edge_mc_avx2;
    }
#endif
#endif /* HAVE_X86ASM */
}
