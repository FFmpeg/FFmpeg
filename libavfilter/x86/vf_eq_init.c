/*
 *
 * Original MPlayer filters by Richard Felker.
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavfilter/vf_eq.h"

extern void ff_process_one_line_mmxext(const uint8_t *src, uint8_t *dst, short contrast,
                                       short brightness, int w);
extern void ff_process_one_line_sse2(const uint8_t *src, uint8_t *dst, short contrast,
                                     short brightness, int w);

#if HAVE_X86ASM
static void process_mmxext(EQParameters *param, uint8_t *dst, int dst_stride,
                           const uint8_t *src, int src_stride, int w, int h)
{
    short contrast = (short) (param->contrast * 256 * 16);
    short brightness = ((short) (100.0 * param->brightness + 100.0) * 511)
                       / 200 - 128 - contrast / 32;

    while (h--) {
        ff_process_one_line_mmxext(src, dst, contrast, brightness, w);
        src += src_stride;
        dst += dst_stride;
    }
    emms_c();
}

static void process_sse2(EQParameters *param, uint8_t *dst, int dst_stride,
                         const uint8_t *src, int src_stride, int w, int h)
{
    short contrast = (short) (param->contrast * 256 * 16);
    short brightness = ((short) (100.0 * param->brightness + 100.0) * 511)
                       / 200 - 128 - contrast / 32;

    while (h--) {
        ff_process_one_line_sse2(src, dst, contrast, brightness, w);
        src += src_stride;
        dst += dst_stride;
    }
}
#endif

av_cold void ff_eq_init_x86(EQContext *eq)
{
#if HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();
    if (EXTERNAL_MMXEXT(cpu_flags)) {
        eq->process = process_mmxext;
    }
    if (EXTERNAL_SSE2(cpu_flags)) {
        eq->process = process_sse2;
    }
#endif
}
