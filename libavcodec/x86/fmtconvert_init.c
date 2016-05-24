/*
 * Format Conversion Utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/fmtconvert.h"

#if HAVE_YASM

void ff_int32_to_float_fmul_scalar_sse (float *dst, const int32_t *src, float mul, int len);
void ff_int32_to_float_fmul_scalar_sse2(float *dst, const int32_t *src, float mul, int len);

#endif /* HAVE_YASM */

av_cold void ff_fmt_convert_init_x86(FmtConvertContext *c, AVCodecContext *avctx)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE(cpu_flags)) {
        c->int32_to_float_fmul_scalar = ff_int32_to_float_fmul_scalar_sse;
    }
    if (EXTERNAL_SSE2(cpu_flags)) {
        c->int32_to_float_fmul_scalar = ff_int32_to_float_fmul_scalar_sse2;
    }
#endif /* HAVE_YASM */
}
