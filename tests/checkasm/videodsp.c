/*
 * Copyright (c) 2016 Ronald S. Bultje <rsbultje@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>
#include "checkasm.h"
#include "libavcodec/videodsp.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define randomize_buffers(w, h)                         \
    do {                                                \
        int i;                                          \
        for (i = 0; i < w * h * sizeof(*src0); i += 4)  \
            AV_WN32A(((uint8_t *) src0) + i, rnd());    \
    } while (0)

#define iter_1d(type, fix, fix_val, var, var_start, var_end)        \
    for (fix = fix_val, var = var_start; var <= var_end; var++) {   \
        call_ref((type *) dst0, (const type *) (src0 + y * pw + x), \
                 bw * sizeof(type), pw * sizeof(type),              \
                 bw, bh, x, y, pw, ph);                             \
        call_new((type *) dst1, (const type *) (src1 + y * pw + x), \
                 bw * sizeof(type), pw * sizeof(type),              \
                 bw, bh, x, y, pw, ph);                             \
        if (memcmp(dst0, dst1, bw * bh * sizeof(type)))             \
            fail();                                                 \
        bench_new((type *) dst1, (const type *) (src1 + y * pw + x),\
                  bw * sizeof(type), pw * sizeof(type),             \
                  bw, bh, x, y, pw, ph);                            \
    }

#define check_emu_edge_size(type, src_w, src_h, dst_w, dst_h)   \
    do {                                                        \
        LOCAL_ALIGNED_16(type, src0, [src_w * src_h]);          \
        LOCAL_ALIGNED_16(type, src1, [src_w * src_h]);          \
        int bw = dst_w, bh = dst_h;                             \
        int pw = src_w, ph = src_h;                             \
        int y, x;                                               \
        randomize_buffers(src_w, src_h);                        \
        memcpy(src1, src0, pw * ph * sizeof(type));             \
        iter_1d(type, y, 0 - src_h, x, 0 - src_w, src_w - 0);   \
        iter_1d(type, x, src_w - 0, y, 0 - src_h, src_h - 0);   \
        iter_1d(type, y, src_h - 0, x, 0 - src_w, src_w - 0);   \
        iter_1d(type, x, 0 - src_w, y, 0 - src_h, src_h - 0);   \
    } while (0)

#define check_emu_edge(type)                                    \
    do {                                                        \
        LOCAL_ALIGNED_16(type, dst0, [64 * 64]);                \
        LOCAL_ALIGNED_16(type, dst1, [64 * 64]);                \
        declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT, \
                          void, type *dst, const type *src,     \
                          ptrdiff_t dst_linesize,               \
                          ptrdiff_t src_linesize,               \
                          int block_w, int block_h,             \
                          int src_x, int src_y,                 \
                          int src_w, int src_h);                \
        check_emu_edge_size(type, 16,  1, 64, 64);              \
        check_emu_edge_size(type, 16, 16, 64, 64);              \
        check_emu_edge_size(type, 64, 64, 64, 64);              \
    } while (0)

void checkasm_check_videodsp(void)
{
    VideoDSPContext vdsp;

    ff_videodsp_init(&vdsp, 8);
    if (check_func(vdsp.emulated_edge_mc, "emulated_edge_mc_8"))
        check_emu_edge(uint8_t);

    report("emulated_edge_mc");
}
