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
#include "libavcodec/videodsp.h"

#if HAVE_YASM
typedef void emu_edge_core_func(uint8_t *buf, const uint8_t *src,
                                x86_reg linesize, x86_reg start_y,
                                x86_reg end_y, x86_reg block_h,
                                x86_reg start_x, x86_reg end_x,
                                x86_reg block_w);
extern emu_edge_core_func ff_emu_edge_core_mmx;
extern emu_edge_core_func ff_emu_edge_core_sse;

static av_always_inline void emulated_edge_mc(uint8_t *buf, const uint8_t *src,
                                              ptrdiff_t linesize_arg,
                                              int block_w, int block_h,
                                              int src_x, int src_y,
                                              int w, int h,
                                              emu_edge_core_func *core_fn)
{
    int start_y, start_x, end_y, end_x, src_y_add = 0;
    int linesize = linesize_arg;

    if(!w || !h)
        return;

    if (src_y >= h) {
        src -= src_y*linesize;
        src_y_add = h - 1;
        src_y     = h - 1;
    } else if (src_y <= -block_h) {
        src -= src_y*linesize;
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
    src += (src_y_add + start_y) * linesize + start_x;
    buf += start_x;
    core_fn(buf, src, linesize, start_y, end_y,
            block_h, start_x, end_x, block_w);
}

#if ARCH_X86_32
static av_noinline void emulated_edge_mc_mmx(uint8_t *buf, const uint8_t *src,
                                             ptrdiff_t linesize,
                                             int block_w, int block_h,
                                             int src_x, int src_y, int w, int h)
{
    emulated_edge_mc(buf, src, linesize, block_w, block_h, src_x, src_y,
                     w, h, &ff_emu_edge_core_mmx);
}
#endif

static av_noinline void emulated_edge_mc_sse(uint8_t *buf, const uint8_t *src,
                                             ptrdiff_t linesize,
                                             int block_w, int block_h,
                                             int src_x, int src_y, int w, int h)
{
    emulated_edge_mc(buf, src, linesize, block_w, block_h, src_x, src_y,
                     w, h, &ff_emu_edge_core_sse);
}
#endif /* HAVE_YASM */

void ff_prefetch_mmxext(uint8_t *buf, ptrdiff_t stride, int h);
void ff_prefetch_3dnow(uint8_t *buf, ptrdiff_t stride, int h);

av_cold void ff_videodsp_init_x86(VideoDSPContext *ctx, int bpc)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

#if ARCH_X86_32
    if (bpc <= 8 && cpu_flags & AV_CPU_FLAG_MMX) {
        ctx->emulated_edge_mc = emulated_edge_mc_mmx;
    }
    if (cpu_flags & AV_CPU_FLAG_3DNOW) {
        ctx->prefetch = ff_prefetch_3dnow;
    }
#endif /* ARCH_X86_32 */
    if (cpu_flags & AV_CPU_FLAG_MMXEXT) {
        ctx->prefetch = ff_prefetch_mmxext;
    }
    if (bpc <= 8 && cpu_flags & AV_CPU_FLAG_SSE) {
        ctx->emulated_edge_mc = emulated_edge_mc_sse;
    }
#endif /* HAVE_YASM */
}
