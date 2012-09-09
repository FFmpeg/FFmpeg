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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/vp3dsp.h"
#include "config.h"

void ff_vp3_idct_put_mmx(uint8_t *dest, int line_size, DCTELEM *block);
void ff_vp3_idct_add_mmx(uint8_t *dest, int line_size, DCTELEM *block);

void ff_vp3_idct_put_sse2(uint8_t *dest, int line_size, DCTELEM *block);
void ff_vp3_idct_add_sse2(uint8_t *dest, int line_size, DCTELEM *block);

void ff_vp3_idct_dc_add_mmx2(uint8_t *dest, int line_size,
                             const DCTELEM *block);

void ff_vp3_v_loop_filter_mmx2(uint8_t *src, int stride, int *bounding_values);
void ff_vp3_h_loop_filter_mmx2(uint8_t *src, int stride, int *bounding_values);

av_cold void ff_vp3dsp_init_x86(VP3DSPContext *c, int flags)
{
    int cpuflags = av_get_cpu_flags();

#if ARCH_X86_32
    if (EXTERNAL_MMX(cpuflags)) {
        c->idct_put  = ff_vp3_idct_put_mmx;
        c->idct_add  = ff_vp3_idct_add_mmx;
        c->idct_perm = FF_PARTTRANS_IDCT_PERM;
    }
#endif

    if (EXTERNAL_MMXEXT(cpuflags)) {
        c->idct_dc_add = ff_vp3_idct_dc_add_mmx2;

        if (!(flags & CODEC_FLAG_BITEXACT)) {
            c->v_loop_filter = ff_vp3_v_loop_filter_mmx2;
            c->h_loop_filter = ff_vp3_h_loop_filter_mmx2;
        }
    }

    if (EXTERNAL_SSE2(cpuflags)) {
        c->idct_put  = ff_vp3_idct_put_sse2;
        c->idct_add  = ff_vp3_idct_add_sse2;
        c->idct_perm = FF_TRANSPOSE_IDCT_PERM;
    }
}
