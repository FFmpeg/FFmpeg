/*
 * Copyright (c) 2026 Devin Heitmueller <dheitmueller@ltnglobal.com>
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/bitpacked_dec.h"

void ff_bitpacked_unpack_yuv422p10_ssse3(const uint8_t *src,
                                         uint16_t *y, uint16_t *u, uint16_t *v,
                                         int width);
#if HAVE_AVX2_EXTERNAL
void ff_bitpacked_unpack_yuv422p10_avx2(const uint8_t *src,
                                        uint16_t *y, uint16_t *u, uint16_t *v,
                                        int width);
#endif
#if HAVE_AVX512ICL_EXTERNAL
void ff_bitpacked_unpack_yuv422p10_avx512icl(const uint8_t *src,
                                             uint16_t *y, uint16_t *u,
                                             uint16_t *v, int width);
#endif

av_cold void ff_bitpacked_init_x86(struct BitpackedContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSSE3(cpu_flags))
        s->unpack_yuv422p10 = ff_bitpacked_unpack_yuv422p10_ssse3;

#if HAVE_AVX2_EXTERNAL
    if (EXTERNAL_AVX2(cpu_flags))
        s->unpack_yuv422p10 = ff_bitpacked_unpack_yuv422p10_avx2;
#endif

#if HAVE_AVX512ICL_EXTERNAL
    if (EXTERNAL_AVX512ICL(cpu_flags))
        s->unpack_yuv422p10 = ff_bitpacked_unpack_yuv422p10_avx512icl;
#endif
}
