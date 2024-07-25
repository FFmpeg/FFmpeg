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
#include "libavutil/common.h"
#include "audiodsp.h"

static void vector_clipf_c(float *dst, const float *src, int len,
                           float min, float max)
{
    for (int i = 0; i < len; i += 8) {
        float tmp[8];

        for (int j = 0; j < 8; j++)
            tmp[j]= av_clipf(src[i + j], min, max);
        for (int j = 0; j < 8; j++)
            dst[i + j] = tmp[j];
    }
}

static int32_t scalarproduct_int16_c(const int16_t *v1, const int16_t *v2,
                                     int order)
{
    unsigned res = 0;

    while (order--)
        res += *v1++ **v2++;

    return res;
}

static void vector_clip_int32_c(int32_t *dst, const int32_t *src, int32_t min,
                                int32_t max, unsigned int len)
{
    do {
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        *dst++ = av_clip(*src++, min, max);
        len   -= 8;
    } while (len > 0);
}

av_cold void ff_audiodsp_init(AudioDSPContext *c)
{
    c->scalarproduct_int16 = scalarproduct_int16_c;
    c->vector_clip_int32   = vector_clip_int32_c;
    c->vector_clipf        = vector_clipf_c;

#if ARCH_ARM
    ff_audiodsp_init_arm(c);
#elif ARCH_PPC
    ff_audiodsp_init_ppc(c);
#elif ARCH_RISCV
    ff_audiodsp_init_riscv(c);
#elif ARCH_X86
    ff_audiodsp_init_x86(c);
#endif
}
