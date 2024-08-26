/*
 * Copyright © 2024 Rémi Denis-Courmont.
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

#include "libavutil/cpu.h"
#include "libavcodec/mpegvideoencdsp.h"

int ff_try_8x8basis_rvv(const int16_t rem[64], const int16_t weight[64],
                        const int16_t basis[16], int scale);
void ff_add_8x8basis_rvv(int16_t rem[64], const int16_t basis[16], int scale);
int ff_pix_sum_rvv(const uint8_t *pix, ptrdiff_t line_size);
int ff_pix_norm1_rvv(const uint8_t *pix, ptrdiff_t line_size);

av_cold void ff_mpegvideoencdsp_init_riscv(MpegvideoEncDSPContext *c,
                                           AVCodecContext *avctx)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_I32) {
        if (flags & AV_CPU_FLAG_RVB) {
            c->try_8x8basis = ff_try_8x8basis_rvv;
            c->add_8x8basis = ff_add_8x8basis_rvv;
        }

        if (flags & AV_CPU_FLAG_RVV_I64) {
            if ((flags & AV_CPU_FLAG_RVB) && ff_rv_vlen_least(128))
                c->pix_sum = ff_pix_sum_rvv;
            c->pix_norm1 = ff_pix_norm1_rvv;
        }
    }
#endif
}
