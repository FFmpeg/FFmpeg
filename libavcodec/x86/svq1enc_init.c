/*
 * Copyright (c) 2007 Loren Merritt
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/svq1enc.h"

int ff_ssd_int8_vs_int16_mmx(const int8_t *pix1, const int16_t *pix2,
                             intptr_t size);
int ff_ssd_int8_vs_int16_sse2(const int8_t *pix1, const int16_t *pix2,
                              intptr_t size);

av_cold void ff_svq1enc_init_x86(SVQ1EncContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(cpu_flags)) {
        c->ssd_int8_vs_int16 = ff_ssd_int8_vs_int16_mmx;
    }
    if (EXTERNAL_SSE2(cpu_flags)) {
        c->ssd_int8_vs_int16 = ff_ssd_int8_vs_int16_sse2;
    }
}
