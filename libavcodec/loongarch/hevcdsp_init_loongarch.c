/*
 * Copyright (c) 2022 Loongson Technology Corporation Limited
 * Contributed by Lu Wang <wanglu@loongson.cn>
 *                Hao Chen <chenhao@loongson.cn>
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

#include "libavutil/loongarch/cpu.h"
#include "hevcdsp_lsx.h"
#include "hevcdsp_lasx.h"

void ff_hevc_dsp_init_loongarch(HEVCDSPContext *c, const int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_lsx(cpu_flags)) {
        if (bit_depth == 8) {
            c->put_hevc_qpel[1][0][0] = ff_hevc_put_hevc_pel_pixels4_8_lsx;
            c->put_hevc_qpel[2][0][0] = ff_hevc_put_hevc_pel_pixels6_8_lsx;
            c->put_hevc_qpel[3][0][0] = ff_hevc_put_hevc_pel_pixels8_8_lsx;
            c->put_hevc_qpel[4][0][0] = ff_hevc_put_hevc_pel_pixels12_8_lsx;
            c->put_hevc_qpel[5][0][0] = ff_hevc_put_hevc_pel_pixels16_8_lsx;
            c->put_hevc_qpel[6][0][0] = ff_hevc_put_hevc_pel_pixels24_8_lsx;
            c->put_hevc_qpel[7][0][0] = ff_hevc_put_hevc_pel_pixels32_8_lsx;
            c->put_hevc_qpel[8][0][0] = ff_hevc_put_hevc_pel_pixels48_8_lsx;
            c->put_hevc_qpel[9][0][0] = ff_hevc_put_hevc_pel_pixels64_8_lsx;

            c->put_hevc_epel[1][0][0] = ff_hevc_put_hevc_pel_pixels4_8_lsx;
            c->put_hevc_epel[2][0][0] = ff_hevc_put_hevc_pel_pixels6_8_lsx;
            c->put_hevc_epel[3][0][0] = ff_hevc_put_hevc_pel_pixels8_8_lsx;
            c->put_hevc_epel[4][0][0] = ff_hevc_put_hevc_pel_pixels12_8_lsx;
            c->put_hevc_epel[5][0][0] = ff_hevc_put_hevc_pel_pixels16_8_lsx;
            c->put_hevc_epel[6][0][0] = ff_hevc_put_hevc_pel_pixels24_8_lsx;
            c->put_hevc_epel[7][0][0] = ff_hevc_put_hevc_pel_pixels32_8_lsx;

            c->put_hevc_qpel[1][0][1] = ff_hevc_put_hevc_qpel_h4_8_lsx;
            c->put_hevc_qpel[3][0][1] = ff_hevc_put_hevc_qpel_h8_8_lsx;
            c->put_hevc_qpel[4][0][1] = ff_hevc_put_hevc_qpel_h12_8_lsx;
            c->put_hevc_qpel[5][0][1] = ff_hevc_put_hevc_qpel_h16_8_lsx;
            c->put_hevc_qpel[6][0][1] = ff_hevc_put_hevc_qpel_h24_8_lsx;
            c->put_hevc_qpel[7][0][1] = ff_hevc_put_hevc_qpel_h32_8_lsx;
            c->put_hevc_qpel[8][0][1] = ff_hevc_put_hevc_qpel_h48_8_lsx;
            c->put_hevc_qpel[9][0][1] = ff_hevc_put_hevc_qpel_h64_8_lsx;

            c->put_hevc_qpel[1][1][0] = ff_hevc_put_hevc_qpel_v4_8_lsx;
            c->put_hevc_qpel[3][1][0] = ff_hevc_put_hevc_qpel_v8_8_lsx;
            c->put_hevc_qpel[4][1][0] = ff_hevc_put_hevc_qpel_v12_8_lsx;
            c->put_hevc_qpel[5][1][0] = ff_hevc_put_hevc_qpel_v16_8_lsx;
            c->put_hevc_qpel[6][1][0] = ff_hevc_put_hevc_qpel_v24_8_lsx;
            c->put_hevc_qpel[7][1][0] = ff_hevc_put_hevc_qpel_v32_8_lsx;
            c->put_hevc_qpel[8][1][0] = ff_hevc_put_hevc_qpel_v48_8_lsx;
            c->put_hevc_qpel[9][1][0] = ff_hevc_put_hevc_qpel_v64_8_lsx;

            c->put_hevc_qpel[1][1][1] = ff_hevc_put_hevc_qpel_hv4_8_lsx;
            c->put_hevc_qpel[3][1][1] = ff_hevc_put_hevc_qpel_hv8_8_lsx;
            c->put_hevc_qpel[4][1][1] = ff_hevc_put_hevc_qpel_hv12_8_lsx;
            c->put_hevc_qpel[5][1][1] = ff_hevc_put_hevc_qpel_hv16_8_lsx;
            c->put_hevc_qpel[6][1][1] = ff_hevc_put_hevc_qpel_hv24_8_lsx;
            c->put_hevc_qpel[7][1][1] = ff_hevc_put_hevc_qpel_hv32_8_lsx;
            c->put_hevc_qpel[8][1][1] = ff_hevc_put_hevc_qpel_hv48_8_lsx;
            c->put_hevc_qpel[9][1][1] = ff_hevc_put_hevc_qpel_hv64_8_lsx;

            c->put_hevc_epel[7][0][1] = ff_hevc_put_hevc_epel_h32_8_lsx;

            c->put_hevc_epel[5][1][0] = ff_hevc_put_hevc_epel_v16_8_lsx;
            c->put_hevc_epel[6][1][0] = ff_hevc_put_hevc_epel_v24_8_lsx;
            c->put_hevc_epel[7][1][0] = ff_hevc_put_hevc_epel_v32_8_lsx;

            c->put_hevc_epel[3][1][1] = ff_hevc_put_hevc_epel_hv8_8_lsx;
            c->put_hevc_epel[4][1][1] = ff_hevc_put_hevc_epel_hv12_8_lsx;
            c->put_hevc_epel[5][1][1] = ff_hevc_put_hevc_epel_hv16_8_lsx;
            c->put_hevc_epel[6][1][1] = ff_hevc_put_hevc_epel_hv24_8_lsx;
            c->put_hevc_epel[7][1][1] = ff_hevc_put_hevc_epel_hv32_8_lsx;

            c->put_hevc_qpel_bi[1][0][0] = ff_hevc_put_hevc_bi_pel_pixels4_8_lsx;
            c->put_hevc_qpel_bi[3][0][0] = ff_hevc_put_hevc_bi_pel_pixels8_8_lsx;
            c->put_hevc_qpel_bi[4][0][0] = ff_hevc_put_hevc_bi_pel_pixels12_8_lsx;
            c->put_hevc_qpel_bi[5][0][0] = ff_hevc_put_hevc_bi_pel_pixels16_8_lsx;
            c->put_hevc_qpel_bi[6][0][0] = ff_hevc_put_hevc_bi_pel_pixels24_8_lsx;
            c->put_hevc_qpel_bi[7][0][0] = ff_hevc_put_hevc_bi_pel_pixels32_8_lsx;
            c->put_hevc_qpel_bi[8][0][0] = ff_hevc_put_hevc_bi_pel_pixels48_8_lsx;
            c->put_hevc_qpel_bi[9][0][0] = ff_hevc_put_hevc_bi_pel_pixels64_8_lsx;

            c->put_hevc_epel_bi[1][0][0] = ff_hevc_put_hevc_bi_pel_pixels4_8_lsx;
            c->put_hevc_epel_bi[2][0][0] = ff_hevc_put_hevc_bi_pel_pixels6_8_lsx;
            c->put_hevc_epel_bi[3][0][0] = ff_hevc_put_hevc_bi_pel_pixels8_8_lsx;
            c->put_hevc_epel_bi[4][0][0] = ff_hevc_put_hevc_bi_pel_pixels12_8_lsx;
            c->put_hevc_epel_bi[5][0][0] = ff_hevc_put_hevc_bi_pel_pixels16_8_lsx;
            c->put_hevc_epel_bi[6][0][0] = ff_hevc_put_hevc_bi_pel_pixels24_8_lsx;
            c->put_hevc_epel_bi[7][0][0] = ff_hevc_put_hevc_bi_pel_pixels32_8_lsx;

            c->put_hevc_qpel_bi[3][1][0] = ff_hevc_put_hevc_bi_qpel_v8_8_lsx;
            c->put_hevc_qpel_bi[5][1][0] = ff_hevc_put_hevc_bi_qpel_v16_8_lsx;
            c->put_hevc_qpel_bi[6][1][0] = ff_hevc_put_hevc_bi_qpel_v24_8_lsx;
            c->put_hevc_qpel_bi[7][1][0] = ff_hevc_put_hevc_bi_qpel_v32_8_lsx;
            c->put_hevc_qpel_bi[8][1][0] = ff_hevc_put_hevc_bi_qpel_v48_8_lsx;
            c->put_hevc_qpel_bi[9][1][0] = ff_hevc_put_hevc_bi_qpel_v64_8_lsx;

            c->put_hevc_qpel_bi[3][1][1] = ff_hevc_put_hevc_bi_qpel_hv8_8_lsx;
            c->put_hevc_qpel_bi[5][1][1] = ff_hevc_put_hevc_bi_qpel_hv16_8_lsx;
            c->put_hevc_qpel_bi[6][1][1] = ff_hevc_put_hevc_bi_qpel_hv24_8_lsx;
            c->put_hevc_qpel_bi[7][1][1] = ff_hevc_put_hevc_bi_qpel_hv32_8_lsx;
            c->put_hevc_qpel_bi[8][1][1] = ff_hevc_put_hevc_bi_qpel_hv48_8_lsx;
            c->put_hevc_qpel_bi[9][1][1] = ff_hevc_put_hevc_bi_qpel_hv64_8_lsx;

            c->put_hevc_qpel_bi[5][0][1] = ff_hevc_put_hevc_bi_qpel_h16_8_lsx;
            c->put_hevc_qpel_bi[6][0][1] = ff_hevc_put_hevc_bi_qpel_h24_8_lsx;
            c->put_hevc_qpel_bi[7][0][1] = ff_hevc_put_hevc_bi_qpel_h32_8_lsx;
            c->put_hevc_qpel_bi[8][0][1] = ff_hevc_put_hevc_bi_qpel_h48_8_lsx;
            c->put_hevc_qpel_bi[9][0][1] = ff_hevc_put_hevc_bi_qpel_h64_8_lsx;

            c->put_hevc_epel_bi[1][0][1] = ff_hevc_put_hevc_bi_epel_h4_8_lsx;
            c->put_hevc_epel_bi[2][0][1] = ff_hevc_put_hevc_bi_epel_h6_8_lsx;
            c->put_hevc_epel_bi[3][0][1] = ff_hevc_put_hevc_bi_epel_h8_8_lsx;
            c->put_hevc_epel_bi[4][0][1] = ff_hevc_put_hevc_bi_epel_h12_8_lsx;
            c->put_hevc_epel_bi[5][0][1] = ff_hevc_put_hevc_bi_epel_h16_8_lsx;
            c->put_hevc_epel_bi[6][0][1] = ff_hevc_put_hevc_bi_epel_h24_8_lsx;
            c->put_hevc_epel_bi[7][0][1] = ff_hevc_put_hevc_bi_epel_h32_8_lsx;
            c->put_hevc_epel_bi[8][0][1] = ff_hevc_put_hevc_bi_epel_h48_8_lsx;
            c->put_hevc_epel_bi[9][0][1] = ff_hevc_put_hevc_bi_epel_h64_8_lsx;

            c->put_hevc_epel_bi[4][1][0] = ff_hevc_put_hevc_bi_epel_v12_8_lsx;
            c->put_hevc_epel_bi[5][1][0] = ff_hevc_put_hevc_bi_epel_v16_8_lsx;
            c->put_hevc_epel_bi[6][1][0] = ff_hevc_put_hevc_bi_epel_v24_8_lsx;
            c->put_hevc_epel_bi[7][1][0] = ff_hevc_put_hevc_bi_epel_v32_8_lsx;

            c->put_hevc_epel_bi[2][1][1] = ff_hevc_put_hevc_bi_epel_hv6_8_lsx;
            c->put_hevc_epel_bi[3][1][1] = ff_hevc_put_hevc_bi_epel_hv8_8_lsx;
            c->put_hevc_epel_bi[5][1][1] = ff_hevc_put_hevc_bi_epel_hv16_8_lsx;
            c->put_hevc_epel_bi[6][1][1] = ff_hevc_put_hevc_bi_epel_hv24_8_lsx;
            c->put_hevc_epel_bi[7][1][1] = ff_hevc_put_hevc_bi_epel_hv32_8_lsx;

            c->put_hevc_qpel_uni[1][0][1] = ff_hevc_put_hevc_uni_qpel_h4_8_lsx;
            c->put_hevc_qpel_uni[2][0][1] = ff_hevc_put_hevc_uni_qpel_h6_8_lsx;
            c->put_hevc_qpel_uni[3][0][1] = ff_hevc_put_hevc_uni_qpel_h8_8_lsx;
            c->put_hevc_qpel_uni[4][0][1] = ff_hevc_put_hevc_uni_qpel_h12_8_lsx;
            c->put_hevc_qpel_uni[5][0][1] = ff_hevc_put_hevc_uni_qpel_h16_8_lsx;
            c->put_hevc_qpel_uni[6][0][1] = ff_hevc_put_hevc_uni_qpel_h24_8_lsx;
            c->put_hevc_qpel_uni[7][0][1] = ff_hevc_put_hevc_uni_qpel_h32_8_lsx;
            c->put_hevc_qpel_uni[8][0][1] = ff_hevc_put_hevc_uni_qpel_h48_8_lsx;
            c->put_hevc_qpel_uni[9][0][1] = ff_hevc_put_hevc_uni_qpel_h64_8_lsx;

            c->put_hevc_qpel_uni[6][1][0] = ff_hevc_put_hevc_uni_qpel_v24_8_lsx;
            c->put_hevc_qpel_uni[7][1][0] = ff_hevc_put_hevc_uni_qpel_v32_8_lsx;
            c->put_hevc_qpel_uni[8][1][0] = ff_hevc_put_hevc_uni_qpel_v48_8_lsx;
            c->put_hevc_qpel_uni[9][1][0] = ff_hevc_put_hevc_uni_qpel_v64_8_lsx;

            c->put_hevc_qpel_uni[3][1][1] = ff_hevc_put_hevc_uni_qpel_hv8_8_lsx;
            c->put_hevc_qpel_uni[5][1][1] = ff_hevc_put_hevc_uni_qpel_hv16_8_lsx;
            c->put_hevc_qpel_uni[6][1][1] = ff_hevc_put_hevc_uni_qpel_hv24_8_lsx;
            c->put_hevc_qpel_uni[7][1][1] = ff_hevc_put_hevc_uni_qpel_hv32_8_lsx;
            c->put_hevc_qpel_uni[8][1][1] = ff_hevc_put_hevc_uni_qpel_hv48_8_lsx;
            c->put_hevc_qpel_uni[9][1][1] = ff_hevc_put_hevc_uni_qpel_hv64_8_lsx;

            c->put_hevc_epel_uni[6][1][0] = ff_hevc_put_hevc_uni_epel_v24_8_lsx;
            c->put_hevc_epel_uni[7][1][0] = ff_hevc_put_hevc_uni_epel_v32_8_lsx;

            c->put_hevc_epel_uni[3][1][1] = ff_hevc_put_hevc_uni_epel_hv8_8_lsx;
            c->put_hevc_epel_uni[4][1][1] = ff_hevc_put_hevc_uni_epel_hv12_8_lsx;
            c->put_hevc_epel_uni[5][1][1] = ff_hevc_put_hevc_uni_epel_hv16_8_lsx;
            c->put_hevc_epel_uni[6][1][1] = ff_hevc_put_hevc_uni_epel_hv24_8_lsx;
            c->put_hevc_epel_uni[7][1][1] = ff_hevc_put_hevc_uni_epel_hv32_8_lsx;

            c->put_hevc_qpel_uni_w[1][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels4_8_lsx;
            c->put_hevc_qpel_uni_w[2][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels6_8_lsx;
            c->put_hevc_qpel_uni_w[3][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels8_8_lsx;
            c->put_hevc_qpel_uni_w[4][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels12_8_lsx;
            c->put_hevc_qpel_uni_w[5][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels16_8_lsx;
            c->put_hevc_qpel_uni_w[6][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels24_8_lsx;
            c->put_hevc_qpel_uni_w[7][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels32_8_lsx;
            c->put_hevc_qpel_uni_w[8][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels48_8_lsx;
            c->put_hevc_qpel_uni_w[9][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels64_8_lsx;

            c->put_hevc_epel_uni_w[1][1][1] = ff_hevc_put_hevc_epel_uni_w_hv4_8_lsx;
            c->put_hevc_epel_uni_w[2][1][1] = ff_hevc_put_hevc_epel_uni_w_hv6_8_lsx;
            c->put_hevc_epel_uni_w[3][1][1] = ff_hevc_put_hevc_epel_uni_w_hv8_8_lsx;
            c->put_hevc_epel_uni_w[4][1][1] = ff_hevc_put_hevc_epel_uni_w_hv12_8_lsx;
            c->put_hevc_epel_uni_w[5][1][1] = ff_hevc_put_hevc_epel_uni_w_hv16_8_lsx;
            c->put_hevc_epel_uni_w[6][1][1] = ff_hevc_put_hevc_epel_uni_w_hv24_8_lsx;
            c->put_hevc_epel_uni_w[7][1][1] = ff_hevc_put_hevc_epel_uni_w_hv32_8_lsx;
            c->put_hevc_epel_uni_w[8][1][1] = ff_hevc_put_hevc_epel_uni_w_hv48_8_lsx;
            c->put_hevc_epel_uni_w[9][1][1] = ff_hevc_put_hevc_epel_uni_w_hv64_8_lsx;

            c->put_hevc_epel_uni_w[1][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels4_8_lsx;
            c->put_hevc_epel_uni_w[2][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels6_8_lsx;
            c->put_hevc_epel_uni_w[3][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels8_8_lsx;
            c->put_hevc_epel_uni_w[4][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels12_8_lsx;
            c->put_hevc_epel_uni_w[5][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels16_8_lsx;
            c->put_hevc_epel_uni_w[6][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels24_8_lsx;
            c->put_hevc_epel_uni_w[7][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels32_8_lsx;
            c->put_hevc_epel_uni_w[8][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels48_8_lsx;
            c->put_hevc_epel_uni_w[9][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels64_8_lsx;

            c->put_hevc_epel_uni_w[1][0][1] = ff_hevc_put_hevc_epel_uni_w_h4_8_lsx;
            c->put_hevc_epel_uni_w[2][0][1] = ff_hevc_put_hevc_epel_uni_w_h6_8_lsx;
            c->put_hevc_epel_uni_w[3][0][1] = ff_hevc_put_hevc_epel_uni_w_h8_8_lsx;
            c->put_hevc_epel_uni_w[4][0][1] = ff_hevc_put_hevc_epel_uni_w_h12_8_lsx;
            c->put_hevc_epel_uni_w[5][0][1] = ff_hevc_put_hevc_epel_uni_w_h16_8_lsx;
            c->put_hevc_epel_uni_w[6][0][1] = ff_hevc_put_hevc_epel_uni_w_h24_8_lsx;
            c->put_hevc_epel_uni_w[7][0][1] = ff_hevc_put_hevc_epel_uni_w_h32_8_lsx;
            c->put_hevc_epel_uni_w[8][0][1] = ff_hevc_put_hevc_epel_uni_w_h48_8_lsx;
            c->put_hevc_epel_uni_w[9][0][1] = ff_hevc_put_hevc_epel_uni_w_h64_8_lsx;

            c->put_hevc_epel_uni_w[1][1][0] = ff_hevc_put_hevc_epel_uni_w_v4_8_lsx;
            c->put_hevc_epel_uni_w[2][1][0] = ff_hevc_put_hevc_epel_uni_w_v6_8_lsx;
            c->put_hevc_epel_uni_w[3][1][0] = ff_hevc_put_hevc_epel_uni_w_v8_8_lsx;
            c->put_hevc_epel_uni_w[4][1][0] = ff_hevc_put_hevc_epel_uni_w_v12_8_lsx;
            c->put_hevc_epel_uni_w[5][1][0] = ff_hevc_put_hevc_epel_uni_w_v16_8_lsx;
            c->put_hevc_epel_uni_w[6][1][0] = ff_hevc_put_hevc_epel_uni_w_v24_8_lsx;
            c->put_hevc_epel_uni_w[7][1][0] = ff_hevc_put_hevc_epel_uni_w_v32_8_lsx;
            c->put_hevc_epel_uni_w[8][1][0] = ff_hevc_put_hevc_epel_uni_w_v48_8_lsx;
            c->put_hevc_epel_uni_w[9][1][0] = ff_hevc_put_hevc_epel_uni_w_v64_8_lsx;

            c->put_hevc_qpel_uni_w[3][1][1] = ff_hevc_put_hevc_uni_w_qpel_hv8_8_lsx;
            c->put_hevc_qpel_uni_w[5][1][1] = ff_hevc_put_hevc_uni_w_qpel_hv16_8_lsx;
            c->put_hevc_qpel_uni_w[6][1][1] = ff_hevc_put_hevc_uni_w_qpel_hv24_8_lsx;
            c->put_hevc_qpel_uni_w[7][1][1] = ff_hevc_put_hevc_uni_w_qpel_hv32_8_lsx;
            c->put_hevc_qpel_uni_w[8][1][1] = ff_hevc_put_hevc_uni_w_qpel_hv48_8_lsx;
            c->put_hevc_qpel_uni_w[9][1][1] = ff_hevc_put_hevc_uni_w_qpel_hv64_8_lsx;

            c->put_hevc_qpel_uni_w[1][1][0] = ff_hevc_put_hevc_qpel_uni_w_v4_8_lsx;
            c->put_hevc_qpel_uni_w[2][1][0] = ff_hevc_put_hevc_qpel_uni_w_v6_8_lsx;
            c->put_hevc_qpel_uni_w[3][1][0] = ff_hevc_put_hevc_qpel_uni_w_v8_8_lsx;
            c->put_hevc_qpel_uni_w[4][1][0] = ff_hevc_put_hevc_qpel_uni_w_v12_8_lsx;
            c->put_hevc_qpel_uni_w[5][1][0] = ff_hevc_put_hevc_qpel_uni_w_v16_8_lsx;
            c->put_hevc_qpel_uni_w[6][1][0] = ff_hevc_put_hevc_qpel_uni_w_v24_8_lsx;
            c->put_hevc_qpel_uni_w[7][1][0] = ff_hevc_put_hevc_qpel_uni_w_v32_8_lsx;
            c->put_hevc_qpel_uni_w[8][1][0] = ff_hevc_put_hevc_qpel_uni_w_v48_8_lsx;
            c->put_hevc_qpel_uni_w[9][1][0] = ff_hevc_put_hevc_qpel_uni_w_v64_8_lsx;

            c->put_hevc_qpel_uni_w[1][0][1] = ff_hevc_put_hevc_qpel_uni_w_h4_8_lsx;
            c->put_hevc_qpel_uni_w[2][0][1] = ff_hevc_put_hevc_qpel_uni_w_h6_8_lsx;
            c->put_hevc_qpel_uni_w[3][0][1] = ff_hevc_put_hevc_qpel_uni_w_h8_8_lsx;
            c->put_hevc_qpel_uni_w[4][0][1] = ff_hevc_put_hevc_qpel_uni_w_h12_8_lsx;
            c->put_hevc_qpel_uni_w[5][0][1] = ff_hevc_put_hevc_qpel_uni_w_h16_8_lsx;
            c->put_hevc_qpel_uni_w[6][0][1] = ff_hevc_put_hevc_qpel_uni_w_h24_8_lsx;
            c->put_hevc_qpel_uni_w[7][0][1] = ff_hevc_put_hevc_qpel_uni_w_h32_8_lsx;
            c->put_hevc_qpel_uni_w[8][0][1] = ff_hevc_put_hevc_qpel_uni_w_h48_8_lsx;
            c->put_hevc_qpel_uni_w[9][0][1] = ff_hevc_put_hevc_qpel_uni_w_h64_8_lsx;

            c->sao_edge_filter[0] = ff_hevc_sao_edge_filter_8_lsx;
            c->sao_edge_filter[1] = ff_hevc_sao_edge_filter_8_lsx;
            c->sao_edge_filter[2] = ff_hevc_sao_edge_filter_8_lsx;
            c->sao_edge_filter[3] = ff_hevc_sao_edge_filter_8_lsx;
            c->sao_edge_filter[4] = ff_hevc_sao_edge_filter_8_lsx;

            c->hevc_h_loop_filter_luma = ff_hevc_loop_filter_luma_h_8_lsx;
            c->hevc_v_loop_filter_luma = ff_hevc_loop_filter_luma_v_8_lsx;

            c->hevc_h_loop_filter_luma_c = ff_hevc_loop_filter_luma_h_8_lsx;
            c->hevc_v_loop_filter_luma_c = ff_hevc_loop_filter_luma_v_8_lsx;

            c->hevc_h_loop_filter_chroma = ff_hevc_loop_filter_chroma_h_8_lsx;
            c->hevc_v_loop_filter_chroma = ff_hevc_loop_filter_chroma_v_8_lsx;

            c->hevc_h_loop_filter_chroma_c = ff_hevc_loop_filter_chroma_h_8_lsx;
            c->hevc_v_loop_filter_chroma_c = ff_hevc_loop_filter_chroma_v_8_lsx;

            c->idct[0] = ff_hevc_idct_4x4_lsx;
            c->idct[1] = ff_hevc_idct_8x8_lsx;
            c->idct[2] = ff_hevc_idct_16x16_lsx;
            c->idct[3] = ff_hevc_idct_32x32_lsx;

            c->add_residual[0] = ff_hevc_add_residual4x4_8_lsx;
            c->add_residual[1] = ff_hevc_add_residual8x8_8_lsx;
            c->add_residual[2] = ff_hevc_add_residual16x16_8_lsx;
            c->add_residual[3] = ff_hevc_add_residual32x32_8_lsx;
        }
    }

    if (have_lasx(cpu_flags)) {
        if (bit_depth == 8) {
            c->put_hevc_qpel_uni_w[2][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels6_8_lasx;
            c->put_hevc_qpel_uni_w[3][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels8_8_lasx;
            c->put_hevc_qpel_uni_w[4][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels12_8_lasx;
            c->put_hevc_qpel_uni_w[5][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels16_8_lasx;
            c->put_hevc_qpel_uni_w[6][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels24_8_lasx;
            c->put_hevc_qpel_uni_w[7][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels32_8_lasx;
            c->put_hevc_qpel_uni_w[8][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels48_8_lasx;
            c->put_hevc_qpel_uni_w[9][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels64_8_lasx;

            c->put_hevc_epel_uni_w[2][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels6_8_lasx;
            c->put_hevc_epel_uni_w[3][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels8_8_lasx;
            c->put_hevc_epel_uni_w[4][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels12_8_lasx;
            c->put_hevc_epel_uni_w[5][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels16_8_lasx;
            c->put_hevc_epel_uni_w[6][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels24_8_lasx;
            c->put_hevc_epel_uni_w[7][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels32_8_lasx;
            c->put_hevc_epel_uni_w[8][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels48_8_lasx;
            c->put_hevc_epel_uni_w[9][0][0] = ff_hevc_put_hevc_pel_uni_w_pixels64_8_lasx;

            c->put_hevc_epel_uni_w[2][1][1] = ff_hevc_put_hevc_epel_uni_w_hv6_8_lasx;
            c->put_hevc_epel_uni_w[3][1][1] = ff_hevc_put_hevc_epel_uni_w_hv8_8_lasx;
            c->put_hevc_epel_uni_w[4][1][1] = ff_hevc_put_hevc_epel_uni_w_hv12_8_lasx;
            c->put_hevc_epel_uni_w[5][1][1] = ff_hevc_put_hevc_epel_uni_w_hv16_8_lasx;
            c->put_hevc_epel_uni_w[6][1][1] = ff_hevc_put_hevc_epel_uni_w_hv24_8_lasx;
            c->put_hevc_epel_uni_w[7][1][1] = ff_hevc_put_hevc_epel_uni_w_hv32_8_lasx;
            c->put_hevc_epel_uni_w[8][1][1] = ff_hevc_put_hevc_epel_uni_w_hv48_8_lasx;
            c->put_hevc_epel_uni_w[9][1][1] = ff_hevc_put_hevc_epel_uni_w_hv64_8_lasx;

            c->put_hevc_epel_uni_w[2][0][1] = ff_hevc_put_hevc_epel_uni_w_h6_8_lasx;
            c->put_hevc_epel_uni_w[3][0][1] = ff_hevc_put_hevc_epel_uni_w_h8_8_lasx;
            c->put_hevc_epel_uni_w[4][0][1] = ff_hevc_put_hevc_epel_uni_w_h12_8_lasx;
            c->put_hevc_epel_uni_w[5][0][1] = ff_hevc_put_hevc_epel_uni_w_h16_8_lasx;
            c->put_hevc_epel_uni_w[6][0][1] = ff_hevc_put_hevc_epel_uni_w_h24_8_lasx;
            c->put_hevc_epel_uni_w[7][0][1] = ff_hevc_put_hevc_epel_uni_w_h32_8_lasx;
            c->put_hevc_epel_uni_w[8][0][1] = ff_hevc_put_hevc_epel_uni_w_h48_8_lasx;
            c->put_hevc_epel_uni_w[9][0][1] = ff_hevc_put_hevc_epel_uni_w_h64_8_lasx;

            c->put_hevc_qpel_uni_w[3][1][0] = ff_hevc_put_hevc_qpel_uni_w_v8_8_lasx;
            c->put_hevc_qpel_uni_w[4][1][0] = ff_hevc_put_hevc_qpel_uni_w_v12_8_lasx;
            c->put_hevc_qpel_uni_w[5][1][0] = ff_hevc_put_hevc_qpel_uni_w_v16_8_lasx;
            c->put_hevc_qpel_uni_w[6][1][0] = ff_hevc_put_hevc_qpel_uni_w_v24_8_lasx;
            c->put_hevc_qpel_uni_w[7][1][0] = ff_hevc_put_hevc_qpel_uni_w_v32_8_lasx;
            c->put_hevc_qpel_uni_w[8][1][0] = ff_hevc_put_hevc_qpel_uni_w_v48_8_lasx;
            c->put_hevc_qpel_uni_w[9][1][0] = ff_hevc_put_hevc_qpel_uni_w_v64_8_lasx;

            c->put_hevc_epel_uni_w[2][1][0] = ff_hevc_put_hevc_epel_uni_w_v6_8_lasx;
            c->put_hevc_epel_uni_w[3][1][0] = ff_hevc_put_hevc_epel_uni_w_v8_8_lasx;
            c->put_hevc_epel_uni_w[4][1][0] = ff_hevc_put_hevc_epel_uni_w_v12_8_lasx;
            c->put_hevc_epel_uni_w[5][1][0] = ff_hevc_put_hevc_epel_uni_w_v16_8_lasx;
            c->put_hevc_epel_uni_w[6][1][0] = ff_hevc_put_hevc_epel_uni_w_v24_8_lasx;
            c->put_hevc_epel_uni_w[7][1][0] = ff_hevc_put_hevc_epel_uni_w_v32_8_lasx;
            c->put_hevc_epel_uni_w[8][1][0] = ff_hevc_put_hevc_epel_uni_w_v48_8_lasx;
            c->put_hevc_epel_uni_w[9][1][0] = ff_hevc_put_hevc_epel_uni_w_v64_8_lasx;

            c->put_hevc_qpel_uni_w[1][0][1] = ff_hevc_put_hevc_qpel_uni_w_h4_8_lasx;
            c->put_hevc_qpel_uni_w[2][0][1] = ff_hevc_put_hevc_qpel_uni_w_h6_8_lasx;
            c->put_hevc_qpel_uni_w[3][0][1] = ff_hevc_put_hevc_qpel_uni_w_h8_8_lasx;
            c->put_hevc_qpel_uni_w[4][0][1] = ff_hevc_put_hevc_qpel_uni_w_h12_8_lasx;
            c->put_hevc_qpel_uni_w[5][0][1] = ff_hevc_put_hevc_qpel_uni_w_h16_8_lasx;
            c->put_hevc_qpel_uni_w[6][0][1] = ff_hevc_put_hevc_qpel_uni_w_h24_8_lasx;
            c->put_hevc_qpel_uni_w[7][0][1] = ff_hevc_put_hevc_qpel_uni_w_h32_8_lasx;
            c->put_hevc_qpel_uni_w[8][0][1] = ff_hevc_put_hevc_qpel_uni_w_h48_8_lasx;
            c->put_hevc_qpel_uni_w[9][0][1] = ff_hevc_put_hevc_qpel_uni_w_h64_8_lasx;

            c->put_hevc_qpel_uni[4][0][1] = ff_hevc_put_hevc_uni_qpel_h12_8_lasx;
            c->put_hevc_qpel_uni[5][0][1] = ff_hevc_put_hevc_uni_qpel_h16_8_lasx;
            c->put_hevc_qpel_uni[6][0][1] = ff_hevc_put_hevc_uni_qpel_h24_8_lasx;
            c->put_hevc_qpel_uni[7][0][1] = ff_hevc_put_hevc_uni_qpel_h32_8_lasx;
            c->put_hevc_qpel_uni[8][0][1] = ff_hevc_put_hevc_uni_qpel_h48_8_lasx;
            c->put_hevc_qpel_uni[9][0][1] = ff_hevc_put_hevc_uni_qpel_h64_8_lasx;

            c->put_hevc_epel_bi[4][0][1] = ff_hevc_put_hevc_bi_epel_h12_8_lasx;
            c->put_hevc_epel_bi[5][0][1] = ff_hevc_put_hevc_bi_epel_h16_8_lasx;
            c->put_hevc_epel_bi[7][0][1] = ff_hevc_put_hevc_bi_epel_h32_8_lasx;
            c->put_hevc_epel_bi[8][0][1] = ff_hevc_put_hevc_bi_epel_h48_8_lasx;
            c->put_hevc_epel_bi[9][0][1] = ff_hevc_put_hevc_bi_epel_h64_8_lasx;

            c->idct[3] = ff_hevc_idct_32x32_lasx;
        }
    }
}
