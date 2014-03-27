/*
 * VC3/DNxHD SIMD functions
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
 *
 * VC-3 encoder funded by the British Broadcasting Corporation
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
#include "libavutil/x86/cpu.h"
#include "libavcodec/dnxhdenc.h"

void ff_get_pixels_8x4_sym_sse2(int16_t *block, const uint8_t *pixels,
                                ptrdiff_t line_size);

av_cold void ff_dnxhdenc_init_x86(DNXHDEncContext *ctx)
{
#if HAVE_SSE2_EXTERNAL
    if (EXTERNAL_SSE2(av_get_cpu_flags())) {
        if (ctx->cid_table->bit_depth == 8)
            ctx->get_pixels_8x4_sym = ff_get_pixels_8x4_sym_sse2;
    }
#endif /* HAVE_SSE2_EXTERNAL */
}
