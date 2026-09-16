/*
 * Bitpacked decoder DSP init
 * Copyright (c) 2017 Savoir-faire Linux, Inc
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

#ifndef AVCODEC_BITPACKED_DEC_INIT_H
#define AVCODEC_BITPACKED_DEC_INIT_H

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "bitpacked_dec.h"

static void bitpacked_unpack_yuv422p10_c(const uint8_t *src,
                                         uint16_t *y, uint16_t *u, uint16_t *v,
                                         int width)
{
    for (int i = 0; i < width; i += 2) {
        *u++ =  (src[0] << 2) | (src[1] >> 6);
        *v++ = ((src[2] << 6) | (src[3] >> 2)) & 0x3ff;
        *y++ = ((src[1] << 4) | (src[2] >> 4)) & 0x3ff;
        *y++ = ((src[3] << 8) |  src[4])       & 0x3ff;
        src += 5;
    }
}

av_unused static av_cold void ff_bitpackeddec_init(struct BitpackedContext *s)
{
    s->unpack_yuv422p10 = bitpacked_unpack_yuv422p10_c;

#if ARCH_AARCH64
    ff_bitpacked_init_aarch64(s);
#elif ARCH_X86 && HAVE_X86ASM
    ff_bitpacked_init_x86(s);
#endif
}

#endif /* AVCODEC_BITPACKED_DEC_INIT_H */
