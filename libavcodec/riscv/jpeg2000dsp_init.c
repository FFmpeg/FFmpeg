/*
 * Copyright © 2023 Rémi Denis-Courmont.
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
#include "libavcodec/jpeg2000dsp.h"

void ff_ict_float_rvv(void *src0, void *src1, void *src2, int csize);
void ff_rct_int_rvv(void *src0, void *src1, void *src2, int csize);

av_cold void ff_jpeg2000dsp_init_riscv(Jpeg2000DSPContext *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB)) {
        if (flags & AV_CPU_FLAG_RVV_F32)
            c->mct_decode[FF_DWT97] = ff_ict_float_rvv;
        c->mct_decode[FF_DWT53] = ff_rct_int_rvv;
    }
#endif
}
