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
#include "libavcodec/lossless_audiodsp.h"

int32_t ff_scalarproduct_and_madd_int16_rvv(int16_t *v1, const int16_t *v2,
                                            const int16_t *v3, int len,
                                            int mul);
int32_t ff_scalarproduct_and_madd_int32_rvv(int16_t *v1, const int32_t *v2,
                                            const int16_t *v3, int len,
                                            int mul);

av_cold void ff_llauddsp_init_riscv(LLAudDSPContext *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32)  && (flags & AV_CPU_FLAG_RVB)) {
        c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_rvv;
        c->scalarproduct_and_madd_int32 = ff_scalarproduct_and_madd_int32_rvv;
    }
#endif
}
