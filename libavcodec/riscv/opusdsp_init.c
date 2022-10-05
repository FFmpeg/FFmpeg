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
#include "libavcodec/opusdsp.h"

void ff_opus_postfilter_rvv_128(float *data, int period, float *g, int len);
void ff_opus_postfilter_rvv_256(float *data, int period, float *g, int len);
void ff_opus_postfilter_rvv_512(float *data, int period, float *g, int len);
void ff_opus_postfilter_rvv_1024(float *data, int period, float *g, int len);

av_cold void ff_opus_dsp_init_riscv(OpusDSP *d)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_F32)
        switch (ff_get_rv_vlenb()) {
        case 16:
            d->postfilter = ff_opus_postfilter_rvv_128;
            break;
        case 32:
            d->postfilter = ff_opus_postfilter_rvv_256;
            break;
        case 64:
            d->postfilter = ff_opus_postfilter_rvv_512;
            break;
        case 128:
            d->postfilter = ff_opus_postfilter_rvv_512;
            break;
        }
#endif
}
