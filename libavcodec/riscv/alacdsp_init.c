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
#include "libavcodec/avcodec.h"
#include "libavcodec/alacdsp.h"

void ff_alac_decorrelate_stereo_rvv(int32_t *buffer[2], int nb_samples,
                                    int decorr_shift, int decorr_left_weight);
void ff_alac_append_extra_bits_mono_rvv(int32_t *buffer[2],
                                        int32_t *extra_bits_buf[2],
                                        int extra_bits, int channels,
                                        int nb_samples);
void ff_alac_append_extra_bits_stereo_rvv(int32_t *buffer[2],
                                          int32_t *extra_bits_buf[2],
                                          int extra_bits, int channels,
                                          int nb_samples);

av_cold void ff_alacdsp_init_riscv(ALACDSPContext *c)
{
#if HAVE_RVV && (__riscv_xlen == 64)
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB)) {
        c->decorrelate_stereo = ff_alac_decorrelate_stereo_rvv;
        c->append_extra_bits[0] = ff_alac_append_extra_bits_mono_rvv;
        c->append_extra_bits[1] = ff_alac_append_extra_bits_stereo_rvv;
    }
#endif
}
