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
#include "libavcodec/huffyuvdsp.h"

void ff_add_int16_rvv(uint16_t *dst, const uint16_t *src, unsigned m, int w);
void ff_add_hfyu_left_pred_bgr32_rvv(uint8_t *dst, const uint8_t *src,
                                     intptr_t w, uint8_t *left);

av_cold void ff_huffyuvdsp_init_riscv(HuffYUVDSPContext *c,
                                      enum AVPixelFormat pix_fmt)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB)) {
        c->add_int16 = ff_add_int16_rvv;
        c->add_hfyu_left_pred_bgr32 = ff_add_hfyu_left_pred_bgr32_rvv;
    }
#endif
}
