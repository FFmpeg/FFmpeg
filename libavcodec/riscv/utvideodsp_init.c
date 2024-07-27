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
#include "libavcodec/utvideodsp.h"

void ff_restore_rgb_planes_rvv(uint8_t *r, uint8_t *g, uint8_t *b,
                               ptrdiff_t linesize_r, ptrdiff_t linesize_g,
                               ptrdiff_t linesize_b, int width, int height);
void ff_restore_rgb_planes10_rvv(uint16_t *r, uint16_t *g, uint16_t *b,
                                 ptrdiff_t linesize_r, ptrdiff_t linesize_g,
                                 ptrdiff_t linesize_b, int width, int height);

av_cold void ff_utvideodsp_init_riscv(UTVideoDSPContext *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_I32) {
        c->restore_rgb_planes = ff_restore_rgb_planes_rvv;

        if (flags & AV_CPU_FLAG_RVB)
            c->restore_rgb_planes10 = ff_restore_rgb_planes10_rvv;
   }
#endif
}
