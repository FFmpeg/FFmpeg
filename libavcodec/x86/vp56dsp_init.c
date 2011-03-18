/*
 * VP6 MMX/SSE2 optimizations
 * Copyright (C) 2009  Sebastien Lucas <sebastien.lucas@gmail.com>
 * Copyright (C) 2009  Zuxy Meng <zuxy.meng@gmail.com>
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

#include "libavutil/cpu.h"
#include "libavutil/x86_cpu.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/vp56dsp.h"

void ff_vp6_filter_diag4_mmx(uint8_t *dst, uint8_t *src, int stride,
                             const int16_t *h_weights,const int16_t *v_weights);
void ff_vp6_filter_diag4_sse2(uint8_t *dst, uint8_t *src, int stride,
                              const int16_t *h_weights,const int16_t *v_weights);

av_cold void ff_vp56dsp_init_x86(VP56DSPContext* c, enum CodecID codec)
{
#if HAVE_YASM
    int mm_flags = av_get_cpu_flags();

    if (CONFIG_VP6_DECODER && codec == CODEC_ID_VP6) {
        if (mm_flags & AV_CPU_FLAG_MMX) {
            c->vp6_filter_diag4 = ff_vp6_filter_diag4_mmx;
        }

        if (mm_flags & AV_CPU_FLAG_SSE2) {
            c->vp6_filter_diag4 = ff_vp6_filter_diag4_sse2;
        }
    }
#endif
}
