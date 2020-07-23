/*
 * Copyright (c) 2015 Parag Salasakar (Parag.Salasakar@imgtec.com)
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

#include "libavutil/mips/cpu.h"
#include "qpeldsp_mips.h"

void ff_qpeldsp_init_mips(QpelDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_msa(cpu_flags)) {
        c->put_qpel_pixels_tab[0][0] = ff_copy_16x16_msa;
        c->put_qpel_pixels_tab[0][1] = ff_horiz_mc_qpel_aver_src0_16width_msa;
        c->put_qpel_pixels_tab[0][2] = ff_horiz_mc_qpel_16width_msa;
        c->put_qpel_pixels_tab[0][3] = ff_horiz_mc_qpel_aver_src1_16width_msa;
        c->put_qpel_pixels_tab[0][4] = ff_vert_mc_qpel_aver_src0_16x16_msa;
        c->put_qpel_pixels_tab[0][5] = ff_hv_mc_qpel_aver_hv_src00_16x16_msa;
        c->put_qpel_pixels_tab[0][6] = ff_hv_mc_qpel_aver_v_src0_16x16_msa;
        c->put_qpel_pixels_tab[0][7] = ff_hv_mc_qpel_aver_hv_src10_16x16_msa;
        c->put_qpel_pixels_tab[0][8] = ff_vert_mc_qpel_16x16_msa;
        c->put_qpel_pixels_tab[0][9] = ff_hv_mc_qpel_aver_h_src0_16x16_msa;
        c->put_qpel_pixels_tab[0][10] = ff_hv_mc_qpel_16x16_msa;
        c->put_qpel_pixels_tab[0][11] = ff_hv_mc_qpel_aver_h_src1_16x16_msa;
        c->put_qpel_pixels_tab[0][12] = ff_vert_mc_qpel_aver_src1_16x16_msa;
        c->put_qpel_pixels_tab[0][13] = ff_hv_mc_qpel_aver_hv_src01_16x16_msa;
        c->put_qpel_pixels_tab[0][14] = ff_hv_mc_qpel_aver_v_src1_16x16_msa;
        c->put_qpel_pixels_tab[0][15] = ff_hv_mc_qpel_aver_hv_src11_16x16_msa;

        c->put_qpel_pixels_tab[1][0] = ff_copy_8x8_msa;
        c->put_qpel_pixels_tab[1][1] = ff_horiz_mc_qpel_aver_src0_8width_msa;
        c->put_qpel_pixels_tab[1][2] = ff_horiz_mc_qpel_8width_msa;
        c->put_qpel_pixels_tab[1][3] = ff_horiz_mc_qpel_aver_src1_8width_msa;
        c->put_qpel_pixels_tab[1][4] = ff_vert_mc_qpel_aver_src0_8x8_msa;
        c->put_qpel_pixels_tab[1][5] = ff_hv_mc_qpel_aver_hv_src00_8x8_msa;
        c->put_qpel_pixels_tab[1][6] = ff_hv_mc_qpel_aver_v_src0_8x8_msa;
        c->put_qpel_pixels_tab[1][7] = ff_hv_mc_qpel_aver_hv_src10_8x8_msa;
        c->put_qpel_pixels_tab[1][8] = ff_vert_mc_qpel_8x8_msa;
        c->put_qpel_pixels_tab[1][9] = ff_hv_mc_qpel_aver_h_src0_8x8_msa;
        c->put_qpel_pixels_tab[1][10] = ff_hv_mc_qpel_8x8_msa;
        c->put_qpel_pixels_tab[1][11] = ff_hv_mc_qpel_aver_h_src1_8x8_msa;
        c->put_qpel_pixels_tab[1][12] = ff_vert_mc_qpel_aver_src1_8x8_msa;
        c->put_qpel_pixels_tab[1][13] = ff_hv_mc_qpel_aver_hv_src01_8x8_msa;
        c->put_qpel_pixels_tab[1][14] = ff_hv_mc_qpel_aver_v_src1_8x8_msa;
        c->put_qpel_pixels_tab[1][15] = ff_hv_mc_qpel_aver_hv_src11_8x8_msa;

        c->put_no_rnd_qpel_pixels_tab[0][0] = ff_copy_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][1] =
            ff_horiz_mc_qpel_no_rnd_aver_src0_16width_msa;
        c->put_no_rnd_qpel_pixels_tab[0][2] = ff_horiz_mc_qpel_no_rnd_16width_msa;
        c->put_no_rnd_qpel_pixels_tab[0][3] =
            ff_horiz_mc_qpel_no_rnd_aver_src1_16width_msa;
        c->put_no_rnd_qpel_pixels_tab[0][4] =
            ff_vert_mc_qpel_no_rnd_aver_src0_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][5] =
            ff_hv_mc_qpel_no_rnd_aver_hv_src00_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][6] =
            ff_hv_mc_qpel_no_rnd_aver_v_src0_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][7] =
            ff_hv_mc_qpel_no_rnd_aver_hv_src10_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][8] = ff_vert_mc_qpel_no_rnd_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][9] =
            ff_hv_mc_qpel_no_rnd_aver_h_src0_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][10] = ff_hv_mc_qpel_no_rnd_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][11] =
            ff_hv_mc_qpel_no_rnd_aver_h_src1_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][12] =
            ff_vert_mc_qpel_no_rnd_aver_src1_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][13] =
            ff_hv_mc_qpel_no_rnd_aver_hv_src01_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][14] =
            ff_hv_mc_qpel_no_rnd_aver_v_src1_16x16_msa;
        c->put_no_rnd_qpel_pixels_tab[0][15] =
            ff_hv_mc_qpel_no_rnd_aver_hv_src11_16x16_msa;

        c->put_no_rnd_qpel_pixels_tab[1][0] = ff_copy_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][1] =
            ff_horiz_mc_qpel_no_rnd_aver_src0_8width_msa;
        c->put_no_rnd_qpel_pixels_tab[1][2] = ff_horiz_mc_qpel_no_rnd_8width_msa;
        c->put_no_rnd_qpel_pixels_tab[1][3] =
            ff_horiz_mc_qpel_no_rnd_aver_src1_8width_msa;
        c->put_no_rnd_qpel_pixels_tab[1][4] =
            ff_vert_mc_qpel_no_rnd_aver_src0_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][5] =
            ff_hv_mc_qpel_no_rnd_aver_hv_src00_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][6] =
            ff_hv_mc_qpel_no_rnd_aver_v_src0_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][7] =
            ff_hv_mc_qpel_no_rnd_aver_hv_src10_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][8] = ff_vert_mc_qpel_no_rnd_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][9] =
            ff_hv_mc_qpel_no_rnd_aver_h_src0_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][10] = ff_hv_mc_qpel_no_rnd_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][11] =
            ff_hv_mc_qpel_no_rnd_aver_h_src1_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][12] =
            ff_vert_mc_qpel_no_rnd_aver_src1_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][13] =
            ff_hv_mc_qpel_no_rnd_aver_hv_src01_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][14] =
            ff_hv_mc_qpel_no_rnd_aver_v_src1_8x8_msa;
        c->put_no_rnd_qpel_pixels_tab[1][15] =
            ff_hv_mc_qpel_no_rnd_aver_hv_src11_8x8_msa;

        c->avg_qpel_pixels_tab[0][0] = ff_avg_width16_msa;
        c->avg_qpel_pixels_tab[0][1] =
            ff_horiz_mc_qpel_avg_dst_aver_src0_16width_msa;
        c->avg_qpel_pixels_tab[0][2] = ff_horiz_mc_qpel_avg_dst_16width_msa;
        c->avg_qpel_pixels_tab[0][3] =
            ff_horiz_mc_qpel_avg_dst_aver_src1_16width_msa;
        c->avg_qpel_pixels_tab[0][4] = ff_vert_mc_qpel_avg_dst_aver_src0_16x16_msa;
        c->avg_qpel_pixels_tab[0][5] =
            ff_hv_mc_qpel_avg_dst_aver_hv_src00_16x16_msa;
        c->avg_qpel_pixels_tab[0][6] = ff_hv_mc_qpel_avg_dst_aver_v_src0_16x16_msa;
        c->avg_qpel_pixels_tab[0][7] =
            ff_hv_mc_qpel_avg_dst_aver_hv_src10_16x16_msa;
        c->avg_qpel_pixels_tab[0][8] = ff_vert_mc_qpel_avg_dst_16x16_msa;
        c->avg_qpel_pixels_tab[0][9] = ff_hv_mc_qpel_avg_dst_aver_h_src0_16x16_msa;
        c->avg_qpel_pixels_tab[0][10] = ff_hv_mc_qpel_avg_dst_16x16_msa;
        c->avg_qpel_pixels_tab[0][11] = ff_hv_mc_qpel_avg_dst_aver_h_src1_16x16_msa;
        c->avg_qpel_pixels_tab[0][12] = ff_vert_mc_qpel_avg_dst_aver_src1_16x16_msa;
        c->avg_qpel_pixels_tab[0][13] =
            ff_hv_mc_qpel_avg_dst_aver_hv_src01_16x16_msa;
        c->avg_qpel_pixels_tab[0][14] = ff_hv_mc_qpel_avg_dst_aver_v_src1_16x16_msa;
        c->avg_qpel_pixels_tab[0][15] =
            ff_hv_mc_qpel_avg_dst_aver_hv_src11_16x16_msa;

        c->avg_qpel_pixels_tab[1][0] = ff_avg_width8_msa;
        c->avg_qpel_pixels_tab[1][1] =
            ff_horiz_mc_qpel_avg_dst_aver_src0_8width_msa;
        c->avg_qpel_pixels_tab[1][2] = ff_horiz_mc_qpel_avg_dst_8width_msa;
        c->avg_qpel_pixels_tab[1][3] =
            ff_horiz_mc_qpel_avg_dst_aver_src1_8width_msa;
        c->avg_qpel_pixels_tab[1][4] = ff_vert_mc_qpel_avg_dst_aver_src0_8x8_msa;
        c->avg_qpel_pixels_tab[1][5] = ff_hv_mc_qpel_avg_dst_aver_hv_src00_8x8_msa;
        c->avg_qpel_pixels_tab[1][6] = ff_hv_mc_qpel_avg_dst_aver_v_src0_8x8_msa;
        c->avg_qpel_pixels_tab[1][7] = ff_hv_mc_qpel_avg_dst_aver_hv_src10_8x8_msa;
        c->avg_qpel_pixels_tab[1][8] = ff_vert_mc_qpel_avg_dst_8x8_msa;
        c->avg_qpel_pixels_tab[1][9] = ff_hv_mc_qpel_avg_dst_aver_h_src0_8x8_msa;
        c->avg_qpel_pixels_tab[1][10] = ff_hv_mc_qpel_avg_dst_8x8_msa;
        c->avg_qpel_pixels_tab[1][11] = ff_hv_mc_qpel_avg_dst_aver_h_src1_8x8_msa;
        c->avg_qpel_pixels_tab[1][12] = ff_vert_mc_qpel_avg_dst_aver_src1_8x8_msa;
        c->avg_qpel_pixels_tab[1][13] = ff_hv_mc_qpel_avg_dst_aver_hv_src01_8x8_msa;
        c->avg_qpel_pixels_tab[1][14] = ff_hv_mc_qpel_avg_dst_aver_v_src1_8x8_msa;
        c->avg_qpel_pixels_tab[1][15] = ff_hv_mc_qpel_avg_dst_aver_hv_src11_8x8_msa;
    }
}
