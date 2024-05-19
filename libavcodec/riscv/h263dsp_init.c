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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/h263dsp.h"

void ff_h263_h_loop_filter_rvv(uint8_t *src, int stride, int q);
void ff_h263_v_loop_filter_rvv(uint8_t *src, int stride, int q);

av_cold void ff_h263dsp_init_riscv(H263DSPContext *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32) && ff_rv_vlen_least(128)) {
        c->h263_h_loop_filter = ff_h263_h_loop_filter_rvv;
        c->h263_v_loop_filter = ff_h263_v_loop_filter_rvv;
    }
#endif
}
