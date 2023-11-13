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
#include "libavcodec/flacdsp.h"

void ff_flac_decorrelate_ls_16_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);
void ff_flac_decorrelate_rs_16_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);
void ff_flac_decorrelate_ms_16_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);
void ff_flac_decorrelate_ls_32_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);
void ff_flac_decorrelate_rs_32_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);
void ff_flac_decorrelate_ms_32_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);

av_cold void ff_flacdsp_init_riscv(FLACDSPContext *c, enum AVSampleFormat fmt,
                                   int channels)
{
#if HAVE_RVV && (__riscv_xlen >= 64)
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB_ADDR)) {
        switch (fmt) {
        case AV_SAMPLE_FMT_S16:
            c->decorrelate[1] = ff_flac_decorrelate_ls_16_rvv;
            c->decorrelate[2] = ff_flac_decorrelate_rs_16_rvv;
            c->decorrelate[3] = ff_flac_decorrelate_ms_16_rvv;
            break;
        case AV_SAMPLE_FMT_S32:
            c->decorrelate[1] = ff_flac_decorrelate_ls_32_rvv;
            c->decorrelate[2] = ff_flac_decorrelate_rs_32_rvv;
            c->decorrelate[3] = ff_flac_decorrelate_ms_32_rvv;
            break;
        }
    }
#endif
}
