/*
 * Copyright (C) 2016 Thomas Mundt <loudmax@yahoo.de>
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
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/bwdifdsp.h"

void ff_bwdif_filter_line_sse2(void *dst, const void *prev, const void *cur, const void *next,
                               int w, int prefs, int mrefs, int prefs2,
                               int mrefs2, int prefs3, int mrefs3, int prefs4,
                               int mrefs4, int parity, int clip_max);
void ff_bwdif_filter_line_ssse3(void *dst, const void *prev, const void *cur, const void *next,
                                int w, int prefs, int mrefs, int prefs2,
                                int mrefs2, int prefs3, int mrefs3, int prefs4,
                                int mrefs4, int parity, int clip_max);
void ff_bwdif_filter_line_avx2(void *dst, const void *prev, const void *cur, const void *next,
                               int w, int prefs, int mrefs, int prefs2,
                               int mrefs2, int prefs3, int mrefs3, int prefs4,
                               int mrefs4, int parity, int clip_max);

void ff_bwdif_filter_line_12bit_sse2(void *dst, const void *prev, const void *cur, const void *next,
                                     int w, int prefs, int mrefs, int prefs2,
                                     int mrefs2, int prefs3, int mrefs3, int prefs4,
                                     int mrefs4, int parity, int clip_max);
void ff_bwdif_filter_line_12bit_ssse3(void *dst, const void *prev, const void *cur, const void *next,
                                      int w, int prefs, int mrefs, int prefs2,
                                      int mrefs2, int prefs3, int mrefs3, int prefs4,
                                      int mrefs4, int parity, int clip_max);
void ff_bwdif_filter_line_12bit_avx2(void *dst, const void *prev, const void *cur, const void *next,
                                     int w, int prefs, int mrefs, int prefs2,
                                     int mrefs2, int prefs3, int mrefs3, int prefs4,
                                     int mrefs4, int parity, int clip_max);

av_cold void ff_bwdif_init_x86(BWDIFDSPContext *bwdif, int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (bit_depth <= 8) {
        if (EXTERNAL_SSE2(cpu_flags))
            bwdif->filter_line = ff_bwdif_filter_line_sse2;
        if (EXTERNAL_SSSE3(cpu_flags))
            bwdif->filter_line = ff_bwdif_filter_line_ssse3;
        if (ARCH_X86_64 && EXTERNAL_AVX2_FAST(cpu_flags))
            bwdif->filter_line = ff_bwdif_filter_line_avx2;
    } else if (bit_depth <= 12) {
        if (EXTERNAL_SSE2(cpu_flags))
            bwdif->filter_line = ff_bwdif_filter_line_12bit_sse2;
        if (EXTERNAL_SSSE3(cpu_flags))
            bwdif->filter_line = ff_bwdif_filter_line_12bit_ssse3;
        if (ARCH_X86_64 && EXTERNAL_AVX2_FAST(cpu_flags))
            bwdif->filter_line = ff_bwdif_filter_line_12bit_avx2;
    }
}
