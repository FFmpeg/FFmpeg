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
#include "libavcodec/lpc.h"

void ff_lpc_apply_welch_window_rvv(const int32_t *, ptrdiff_t, double *);
void ff_lpc_compute_autocorr_rvv(const double *, ptrdiff_t, int, double *);

av_cold void ff_lpc_init_riscv(LPCContext *c)
{
#if HAVE_RVV && (__riscv_xlen >= 64)
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_F64) && (flags & AV_CPU_FLAG_RVB)) {
        c->lpc_apply_welch_window = ff_lpc_apply_welch_window_rvv;

        if (ff_get_rv_vlenb() > c->max_order)
            c->lpc_compute_autocorr = ff_lpc_compute_autocorr_rvv;
    }
#endif
}
