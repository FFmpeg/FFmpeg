/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
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

#include "libavcodec/hevc/dsp.h"
#include "libavcodec/riscv/h26x/h2656dsp.h"

#define RVV_FNASSIGN(member, v, h, fn, ext) \
        member[1][v][h] = ff_h2656_put_pixels_##8_##ext;  \
        member[3][v][h] = ff_h2656_put_pixels_##8_##ext;  \
        member[5][v][h] = ff_h2656_put_pixels_##8_##ext; \
        member[7][v][h] = ff_h2656_put_pixels_##8_##ext; \
        member[9][v][h] = ff_h2656_put_pixels_##8_##ext;

void ff_hevc_dsp_init_riscv(HEVCDSPContext *c, const int bit_depth)
{
#if HAVE_RVV
    const int flags = av_get_cpu_flags();
    int vlenb;

    if (!(flags & AV_CPU_FLAG_RVV_I32) || !(flags & AV_CPU_FLAG_RVB))
        return;

    vlenb = ff_get_rv_vlenb();
    if (vlenb >= 32) {
        switch (bit_depth) {
            case 8:
                RVV_FNASSIGN(c->put_hevc_qpel, 0, 0, pel_pixels, rvv_256);
                RVV_FNASSIGN(c->put_hevc_epel, 0, 0, pel_pixels, rvv_256);
                break;
            default:
                break;
        }
    } else if (vlenb >= 16) {
        switch (bit_depth) {
            case 8:
                RVV_FNASSIGN(c->put_hevc_qpel, 0, 0, pel_pixels, rvv_128);
                RVV_FNASSIGN(c->put_hevc_epel, 0, 0, pel_pixels, rvv_128);
                break;
            default:
                break;
        }
    }
#endif
}
