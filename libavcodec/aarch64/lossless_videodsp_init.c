/*
 * Copyright (c) 2026 Gyula Sebok <gyula.sbk@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/lossless_videodsp.h"

int ff_add_left_pred_neon(uint8_t *dst, const uint8_t *src,
                          ptrdiff_t w, int acc);
int ff_add_left_pred_int16_neon(uint16_t *dst, const uint16_t *src,
                                unsigned mask, ptrdiff_t w, unsigned acc);
void ff_add_gradient_pred_neon(uint8_t *src, const ptrdiff_t stride,
                               const ptrdiff_t width);

av_cold void ff_llviddsp_init_aarch64(LLVidDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        c->add_left_pred       = ff_add_left_pred_neon;
        c->add_left_pred_int16 = ff_add_left_pred_int16_neon;
        c->add_gradient_pred   = ff_add_gradient_pred_neon;
    }
}
