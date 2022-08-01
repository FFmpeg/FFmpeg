/*
 * Copyright (c) 2014 James Almer
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
#include "libavutil/x86/cpu.h"
#include "libavcodec/flacencdsp.h"

void ff_flac_enc_lpc_16_sse4(int32_t *, const int32_t *, int, int, const int32_t *,int);

av_cold void ff_flacencdsp_init_x86(FLACEncDSPContext *c)
{
#if HAVE_X86ASM && CONFIG_GPL
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE4(cpu_flags)) {
        if (CONFIG_GPL)
            c->lpc16_encode = ff_flac_enc_lpc_16_sse4;
    }
#endif /* HAVE_X86ASM */
}
