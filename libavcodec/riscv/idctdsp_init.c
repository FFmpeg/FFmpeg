/*
 * Copyright © 2022 Rémi Denis-Courmont.
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/idctdsp.h"

void ff_put_pixels_clamped_rvv(const int16_t *block, uint8_t *pixels,
                               ptrdiff_t stride);
void ff_put_signed_pixels_clamped_rvv(const int16_t *block, uint8_t *pixels,
                                      ptrdiff_t stride);
void ff_add_pixels_clamped_rvv(const int16_t *block, uint8_t *pixels,
                               ptrdiff_t stride);

av_cold void ff_idctdsp_init_riscv(IDCTDSPContext *c, AVCodecContext *avctx,
                                   unsigned high_bit_depth)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I64) && ff_rv_vlen_least(128)) {
        c->put_pixels_clamped = ff_put_pixels_clamped_rvv;
        c->put_signed_pixels_clamped = ff_put_signed_pixels_clamped_rvv;
        c->add_pixels_clamped = ff_add_pixels_clamped_rvv;
    }
#endif
}
