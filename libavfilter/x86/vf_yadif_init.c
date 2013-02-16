/*
 * Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
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
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/x86/dsputil_mmx.h"
#include "libavfilter/yadif.h"

void ff_yadif_filter_line_mmxext(void *dst, void *prev, void *cur,
                                 void *next, int w, int prefs,
                                 int mrefs, int parity, int mode);
void ff_yadif_filter_line_sse2(void *dst, void *prev, void *cur,
                               void *next, int w, int prefs,
                               int mrefs, int parity, int mode);
void ff_yadif_filter_line_ssse3(void *dst, void *prev, void *cur,
                                void *next, int w, int prefs,
                                int mrefs, int parity, int mode);

av_cold void ff_yadif_init_x86(YADIFContext *yadif)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_YASM
#if ARCH_X86_32
    if (EXTERNAL_MMXEXT(cpu_flags)) {
        yadif->filter_line = ff_yadif_filter_line_mmxext;
        yadif->req_align   = 8;
    }
#endif /* ARCH_X86_32 */
    if (EXTERNAL_SSE2(cpu_flags)) {
        yadif->filter_line = ff_yadif_filter_line_sse2;
        yadif->req_align   = 16;
    }
    if (EXTERNAL_SSSE3(cpu_flags)) {
        yadif->filter_line = ff_yadif_filter_line_ssse3;
        yadif->req_align   = 16;
    }
#endif /* HAVE_YASM */
}
