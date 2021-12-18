/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Hecai Yuan <yuanhecai@loongson.cn>
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

/**
 * @file
 * VP8 compatible video decoder
 */

#include "libavutil/loongarch/cpu.h"
#include "libavcodec/vp8dsp.h"
#include "libavutil/attributes.h"
#include "vp8dsp_loongarch.h"

#define VP8_MC_LOONGARCH_FUNC(IDX, SIZE)                                          \
    dsp->put_vp8_epel_pixels_tab[IDX][0][2] = ff_put_vp8_epel##SIZE##_h6_lsx;     \
    dsp->put_vp8_epel_pixels_tab[IDX][1][0] = ff_put_vp8_epel##SIZE##_v4_lsx;     \
    dsp->put_vp8_epel_pixels_tab[IDX][1][2] = ff_put_vp8_epel##SIZE##_h6v4_lsx;   \
    dsp->put_vp8_epel_pixels_tab[IDX][2][0] = ff_put_vp8_epel##SIZE##_v6_lsx;     \
    dsp->put_vp8_epel_pixels_tab[IDX][2][1] = ff_put_vp8_epel##SIZE##_h4v6_lsx;   \
    dsp->put_vp8_epel_pixels_tab[IDX][2][2] = ff_put_vp8_epel##SIZE##_h6v6_lsx;

#define VP8_MC_LOONGARCH_COPY(IDX, SIZE)                                          \
    dsp->put_vp8_epel_pixels_tab[IDX][0][0] = ff_put_vp8_pixels##SIZE##_lsx;      \
    dsp->put_vp8_bilinear_pixels_tab[IDX][0][0] = ff_put_vp8_pixels##SIZE##_lsx;

av_cold void ff_vp8dsp_init_loongarch(VP8DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_lsx(cpu_flags)) {
        VP8_MC_LOONGARCH_FUNC(0, 16);
        VP8_MC_LOONGARCH_FUNC(1, 8);

        VP8_MC_LOONGARCH_COPY(0, 16);
        VP8_MC_LOONGARCH_COPY(1, 8);

        dsp->vp8_v_loop_filter16y = ff_vp8_v_loop_filter16_lsx;
        dsp->vp8_h_loop_filter16y = ff_vp8_h_loop_filter16_lsx;
        dsp->vp8_v_loop_filter8uv = ff_vp8_v_loop_filter8uv_lsx;
        dsp->vp8_h_loop_filter8uv = ff_vp8_h_loop_filter8uv_lsx;

        dsp->vp8_v_loop_filter16y_inner = ff_vp8_v_loop_filter16_inner_lsx;
        dsp->vp8_h_loop_filter16y_inner = ff_vp8_h_loop_filter16_inner_lsx;
    }
}
