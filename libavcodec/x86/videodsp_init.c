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
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/videodsp.h"

#if HAVE_YASM
typedef void emu_edge_vfix_func(uint8_t *dst, x86_reg dst_stride,
                                const uint8_t *src, x86_reg src_stride,
                                x86_reg start_y, x86_reg end_y, x86_reg bh);
typedef void emu_edge_vvar_func(uint8_t *dst, x86_reg dst_stride,
                                const uint8_t *src, x86_reg src_stride,
                                x86_reg start_y, x86_reg end_y, x86_reg bh,
                                x86_reg w);

extern emu_edge_vfix_func ff_emu_edge_vfix1_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix2_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix3_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix4_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix5_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix6_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix7_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix8_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix9_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix10_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix11_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix12_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix13_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix14_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix15_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix16_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix17_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix18_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix19_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix20_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix21_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix22_mmx;
#if ARCH_X86_32
static emu_edge_vfix_func *vfixtbl_mmx[22] = {
    &ff_emu_edge_vfix1_mmx,  &ff_emu_edge_vfix2_mmx,  &ff_emu_edge_vfix3_mmx,
    &ff_emu_edge_vfix4_mmx,  &ff_emu_edge_vfix5_mmx,  &ff_emu_edge_vfix6_mmx,
    &ff_emu_edge_vfix7_mmx,  &ff_emu_edge_vfix8_mmx,  &ff_emu_edge_vfix9_mmx,
    &ff_emu_edge_vfix10_mmx, &ff_emu_edge_vfix11_mmx, &ff_emu_edge_vfix12_mmx,
    &ff_emu_edge_vfix13_mmx, &ff_emu_edge_vfix14_mmx, &ff_emu_edge_vfix15_mmx,
    &ff_emu_edge_vfix16_mmx, &ff_emu_edge_vfix17_mmx, &ff_emu_edge_vfix18_mmx,
    &ff_emu_edge_vfix19_mmx, &ff_emu_edge_vfix20_mmx, &ff_emu_edge_vfix21_mmx,
    &ff_emu_edge_vfix22_mmx
};
#endif
extern emu_edge_vvar_func ff_emu_edge_vvar_mmx;
extern emu_edge_vfix_func ff_emu_edge_vfix16_sse;
extern emu_edge_vfix_func ff_emu_edge_vfix17_sse;
extern emu_edge_vfix_func ff_emu_edge_vfix18_sse;
extern emu_edge_vfix_func ff_emu_edge_vfix19_sse;
extern emu_edge_vfix_func ff_emu_edge_vfix20_sse;
extern emu_edge_vfix_func ff_emu_edge_vfix21_sse;
extern emu_edge_vfix_func ff_emu_edge_vfix22_sse;
static emu_edge_vfix_func *vfixtbl_sse[22] = {
    ff_emu_edge_vfix1_mmx,  ff_emu_edge_vfix2_mmx,  ff_emu_edge_vfix3_mmx,
    ff_emu_edge_vfix4_mmx,  ff_emu_edge_vfix5_mmx,  ff_emu_edge_vfix6_mmx,
    ff_emu_edge_vfix7_mmx,  ff_emu_edge_vfix8_mmx,  ff_emu_edge_vfix9_mmx,
    ff_emu_edge_vfix10_mmx, ff_emu_edge_vfix11_mmx, ff_emu_edge_vfix12_mmx,
    ff_emu_edge_vfix13_mmx, ff_emu_edge_vfix14_mmx, ff_emu_edge_vfix15_mmx,
    ff_emu_edge_vfix16_sse, ff_emu_edge_vfix17_sse, ff_emu_edge_vfix18_sse,
    ff_emu_edge_vfix19_sse, ff_emu_edge_vfix20_sse, ff_emu_edge_vfix21_sse,
    ff_emu_edge_vfix22_sse
};
extern emu_edge_vvar_func ff_emu_edge_vvar_sse;

typedef void emu_edge_hfix_func(uint8_t *dst, x86_reg dst_stride,
                                x86_reg start_x, x86_reg bh);
typedef void emu_edge_hvar_func(uint8_t *dst, x86_reg dst_stride,
                                x86_reg start_x, x86_reg n_words, x86_reg bh);

extern emu_edge_hfix_func ff_emu_edge_hfix2_mmx;
extern emu_edge_hfix_func ff_emu_edge_hfix4_mmx;
extern emu_edge_hfix_func ff_emu_edge_hfix6_mmx;
extern emu_edge_hfix_func ff_emu_edge_hfix8_mmx;
extern emu_edge_hfix_func ff_emu_edge_hfix10_mmx;
extern emu_edge_hfix_func ff_emu_edge_hfix12_mmx;
extern emu_edge_hfix_func ff_emu_edge_hfix14_mmx;
extern emu_edge_hfix_func ff_emu_edge_hfix16_mmx;
extern emu_edge_hfix_func ff_emu_edge_hfix18_mmx;
extern emu_edge_hfix_func ff_emu_edge_hfix20_mmx;
extern emu_edge_hfix_func ff_emu_edge_hfix22_mmx;
#if ARCH_X86_32
static emu_edge_hfix_func *hfixtbl_mmx[11] = {
    ff_emu_edge_hfix2_mmx,  ff_emu_edge_hfix4_mmx,  ff_emu_edge_hfix6_mmx,
    ff_emu_edge_hfix8_mmx,  ff_emu_edge_hfix10_mmx, ff_emu_edge_hfix12_mmx,
    ff_emu_edge_hfix14_mmx, ff_emu_edge_hfix16_mmx, ff_emu_edge_hfix18_mmx,
    ff_emu_edge_hfix20_mmx, ff_emu_edge_hfix22_mmx
};
#endif
extern emu_edge_hvar_func ff_emu_edge_hvar_mmx;
extern emu_edge_hfix_func ff_emu_edge_hfix16_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix18_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix20_sse2;
extern emu_edge_hfix_func ff_emu_edge_hfix22_sse2;
static emu_edge_hfix_func *hfixtbl_sse2[11] = {
    ff_emu_edge_hfix2_mmx,  ff_emu_edge_hfix4_mmx,  ff_emu_edge_hfix6_mmx,
    ff_emu_edge_hfix8_mmx,  ff_emu_edge_hfix10_mmx, ff_emu_edge_hfix12_mmx,
    ff_emu_edge_hfix14_mmx, ff_emu_edge_hfix16_sse2, ff_emu_edge_hfix18_sse2,
    ff_emu_edge_hfix20_sse2, ff_emu_edge_hfix22_sse2
};
extern emu_edge_hvar_func ff_emu_edge_hvar_sse2;

static av_always_inline void emulated_edge_mc(uint8_t *dst, const uint8_t *src,
                                              ptrdiff_t dst_stride,
                                              ptrdiff_t src_stride,
                                              x86_reg block_w, x86_reg block_h,
                                              x86_reg src_x, x86_reg src_y,
                                              x86_reg w, x86_reg h,
                                              emu_edge_vfix_func **vfix_tbl,
                                              emu_edge_vvar_func *v_extend_var,
                                              emu_edge_hfix_func **hfix_tbl,
                                              emu_edge_hvar_func *h_extend_var)
{
    x86_reg start_y, start_x, end_y, end_x, src_y_add = 0, p;

    if (!w || !h)
        return;

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

#if ARCH_X86_32
static av_noinline void emulated_edge_mc_mmx(uint8_t *buf, const uint8_t *src,
                                             ptrdiff_t buf_stride,
                                             ptrdiff_t src_stride,
                                             int block_w, int block_h,
                                             int src_x, int src_y, int w, int h)
{
    emulated_edge_mc(buf, src, buf_stride, src_stride, block_w, block_h,
                     src_x, src_y, w, h, vfixtbl_mmx, &ff_emu_edge_vvar_mmx,
                     hfixtbl_mmx, &ff_emu_edge_hvar_mmx);
}

static av_noinline void emulated_edge_mc_sse(uint8_t *buf, const uint8_t *src,
                                             ptrdiff_t buf_stride,
                                             ptrdiff_t src_stride,
                                             int block_w, int block_h,
                                             int src_x, int src_y, int w, int h)
{
    emulated_edge_mc(buf, src, buf_stride, src_stride, block_w, block_h,
                     src_x, src_y, w, h, vfixtbl_sse, &ff_emu_edge_vvar_sse,
                     hfixtbl_mmx, &ff_emu_edge_hvar_mmx);
}
#endif

static av_noinline void emulated_edge_mc_sse2(uint8_t *buf, const uint8_t *src,
                                              ptrdiff_t buf_stride,
                                              ptrdiff_t src_stride,
                                              int block_w, int block_h,
                                              int src_x, int src_y, int w,
                                              int h)
{
    emulated_edge_mc(buf, src, buf_stride, src_stride, block_w, block_h,
                     src_x, src_y, w, h, vfixtbl_sse, &ff_emu_edge_vvar_sse,
                     hfixtbl_sse2, &ff_emu_edge_hvar_sse2);
}
#endif /* HAVE_YASM */

void ff_prefetch_mmxext(uint8_t *buf, ptrdiff_t stride, int h);
void ff_prefetch_3dnow(uint8_t *buf, ptrdiff_t stride, int h);

av_cold void ff_videodsp_init_x86(VideoDSPContext *ctx, int bpc)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

#if ARCH_X86_32
    if (EXTERNAL_MMX(cpu_flags) && bpc <= 8) {
        ctx->emulated_edge_mc = emulated_edge_mc_mmx;
    }
    if (EXTERNAL_AMD3DNOW(cpu_flags)) {
        ctx->prefetch = ff_prefetch_3dnow;
    }
#endif /* ARCH_X86_32 */
    if (EXTERNAL_MMXEXT(cpu_flags)) {
        ctx->prefetch = ff_prefetch_mmxext;
    }
#if ARCH_X86_32
    if (EXTERNAL_SSE(cpu_flags) && bpc <= 8) {
        ctx->emulated_edge_mc = emulated_edge_mc_sse;
    }
#endif /* ARCH_X86_32 */
    if (EXTERNAL_SSE2(cpu_flags) && bpc <= 8) {
        ctx->emulated_edge_mc = emulated_edge_mc_sse2;
    }
#endif /* HAVE_YASM */
}
