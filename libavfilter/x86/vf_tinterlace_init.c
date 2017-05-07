/*
 * Copyright (C) 2014 Kieran Kunhya <kierank@obe.tv>
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
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"

#include "libavfilter/tinterlace.h"

void ff_lowpass_line_sse2(uint8_t *dstp, ptrdiff_t linesize,
                          const uint8_t *srcp,
                          ptrdiff_t mref, ptrdiff_t pref);
void ff_lowpass_line_avx (uint8_t *dstp, ptrdiff_t linesize,
                          const uint8_t *srcp,
                          ptrdiff_t mref, ptrdiff_t pref);

void ff_lowpass_line_complex_sse2(uint8_t *dstp, ptrdiff_t linesize,
                                  const uint8_t *srcp,
                                  ptrdiff_t mref, ptrdiff_t pref);

av_cold void ff_tinterlace_init_x86(TInterlaceContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        if (!(s->flags & TINTERLACE_FLAG_CVLPF))
            s->lowpass_line = ff_lowpass_line_sse2;
        else
            s->lowpass_line = ff_lowpass_line_complex_sse2;
    }
    if (EXTERNAL_AVX(cpu_flags))
        if (!(s->flags & TINTERLACE_FLAG_CVLPF))
            s->lowpass_line = ff_lowpass_line_avx;
}
