/*
 * Copyright (c) 2015 Parag Salasakar (Parag.Salasakar@imgtec.com)
 *                    Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#include "h264dsp_mips.h"

#if HAVE_MSA
static av_cold void h264qpel_init_msa(H264QpelContext *c, int bit_depth)
{
    if (8 == bit_depth) {
        c->put_h264_qpel_pixels_tab[0][0] = ff_put_h264_qpel16_mc00_msa;
        c->put_h264_qpel_pixels_tab[0][1] = ff_put_h264_qpel16_mc10_msa;
        c->put_h264_qpel_pixels_tab[0][2] = ff_put_h264_qpel16_mc20_msa;
        c->put_h264_qpel_pixels_tab[0][3] = ff_put_h264_qpel16_mc30_msa;
        c->put_h264_qpel_pixels_tab[0][4] = ff_put_h264_qpel16_mc01_msa;
        c->put_h264_qpel_pixels_tab[0][5] = ff_put_h264_qpel16_mc11_msa;
        c->put_h264_qpel_pixels_tab[0][6] = ff_put_h264_qpel16_mc21_msa;
        c->put_h264_qpel_pixels_tab[0][7] = ff_put_h264_qpel16_mc31_msa;
        c->put_h264_qpel_pixels_tab[0][8] = ff_put_h264_qpel16_mc02_msa;
        c->put_h264_qpel_pixels_tab[0][9] = ff_put_h264_qpel16_mc12_msa;
        c->put_h264_qpel_pixels_tab[0][10] = ff_put_h264_qpel16_mc22_msa;
        c->put_h264_qpel_pixels_tab[0][11] = ff_put_h264_qpel16_mc32_msa;
        c->put_h264_qpel_pixels_tab[0][12] = ff_put_h264_qpel16_mc03_msa;
        c->put_h264_qpel_pixels_tab[0][13] = ff_put_h264_qpel16_mc13_msa;
        c->put_h264_qpel_pixels_tab[0][14] = ff_put_h264_qpel16_mc23_msa;
        c->put_h264_qpel_pixels_tab[0][15] = ff_put_h264_qpel16_mc33_msa;

        c->put_h264_qpel_pixels_tab[1][0] = ff_put_h264_qpel8_mc00_msa;
        c->put_h264_qpel_pixels_tab[1][1] = ff_put_h264_qpel8_mc10_msa;
        c->put_h264_qpel_pixels_tab[1][2] = ff_put_h264_qpel8_mc20_msa;
        c->put_h264_qpel_pixels_tab[1][3] = ff_put_h264_qpel8_mc30_msa;
        c->put_h264_qpel_pixels_tab[1][4] = ff_put_h264_qpel8_mc01_msa;
        c->put_h264_qpel_pixels_tab[1][5] = ff_put_h264_qpel8_mc11_msa;
        c->put_h264_qpel_pixels_tab[1][6] = ff_put_h264_qpel8_mc21_msa;
        c->put_h264_qpel_pixels_tab[1][7] = ff_put_h264_qpel8_mc31_msa;
        c->put_h264_qpel_pixels_tab[1][8] = ff_put_h264_qpel8_mc02_msa;
        c->put_h264_qpel_pixels_tab[1][9] = ff_put_h264_qpel8_mc12_msa;
        c->put_h264_qpel_pixels_tab[1][10] = ff_put_h264_qpel8_mc22_msa;
        c->put_h264_qpel_pixels_tab[1][11] = ff_put_h264_qpel8_mc32_msa;
        c->put_h264_qpel_pixels_tab[1][12] = ff_put_h264_qpel8_mc03_msa;
        c->put_h264_qpel_pixels_tab[1][13] = ff_put_h264_qpel8_mc13_msa;
        c->put_h264_qpel_pixels_tab[1][14] = ff_put_h264_qpel8_mc23_msa;
        c->put_h264_qpel_pixels_tab[1][15] = ff_put_h264_qpel8_mc33_msa;

        c->put_h264_qpel_pixels_tab[2][1] = ff_put_h264_qpel4_mc10_msa;
        c->put_h264_qpel_pixels_tab[2][2] = ff_put_h264_qpel4_mc20_msa;
        c->put_h264_qpel_pixels_tab[2][3] = ff_put_h264_qpel4_mc30_msa;
        c->put_h264_qpel_pixels_tab[2][4] = ff_put_h264_qpel4_mc01_msa;
        c->put_h264_qpel_pixels_tab[2][5] = ff_put_h264_qpel4_mc11_msa;
        c->put_h264_qpel_pixels_tab[2][6] = ff_put_h264_qpel4_mc21_msa;
        c->put_h264_qpel_pixels_tab[2][7] = ff_put_h264_qpel4_mc31_msa;
        c->put_h264_qpel_pixels_tab[2][8] = ff_put_h264_qpel4_mc02_msa;
        c->put_h264_qpel_pixels_tab[2][9] = ff_put_h264_qpel4_mc12_msa;
        c->put_h264_qpel_pixels_tab[2][10] = ff_put_h264_qpel4_mc22_msa;
        c->put_h264_qpel_pixels_tab[2][11] = ff_put_h264_qpel4_mc32_msa;
        c->put_h264_qpel_pixels_tab[2][12] = ff_put_h264_qpel4_mc03_msa;
        c->put_h264_qpel_pixels_tab[2][13] = ff_put_h264_qpel4_mc13_msa;
        c->put_h264_qpel_pixels_tab[2][14] = ff_put_h264_qpel4_mc23_msa;
        c->put_h264_qpel_pixels_tab[2][15] = ff_put_h264_qpel4_mc33_msa;

        c->avg_h264_qpel_pixels_tab[0][0] = ff_avg_h264_qpel16_mc00_msa;
        c->avg_h264_qpel_pixels_tab[0][1] = ff_avg_h264_qpel16_mc10_msa;
        c->avg_h264_qpel_pixels_tab[0][2] = ff_avg_h264_qpel16_mc20_msa;
        c->avg_h264_qpel_pixels_tab[0][3] = ff_avg_h264_qpel16_mc30_msa;
        c->avg_h264_qpel_pixels_tab[0][4] = ff_avg_h264_qpel16_mc01_msa;
        c->avg_h264_qpel_pixels_tab[0][5] = ff_avg_h264_qpel16_mc11_msa;
        c->avg_h264_qpel_pixels_tab[0][6] = ff_avg_h264_qpel16_mc21_msa;
        c->avg_h264_qpel_pixels_tab[0][7] = ff_avg_h264_qpel16_mc31_msa;
        c->avg_h264_qpel_pixels_tab[0][8] = ff_avg_h264_qpel16_mc02_msa;
        c->avg_h264_qpel_pixels_tab[0][9] = ff_avg_h264_qpel16_mc12_msa;
        c->avg_h264_qpel_pixels_tab[0][10] = ff_avg_h264_qpel16_mc22_msa;
        c->avg_h264_qpel_pixels_tab[0][11] = ff_avg_h264_qpel16_mc32_msa;
        c->avg_h264_qpel_pixels_tab[0][12] = ff_avg_h264_qpel16_mc03_msa;
        c->avg_h264_qpel_pixels_tab[0][13] = ff_avg_h264_qpel16_mc13_msa;
        c->avg_h264_qpel_pixels_tab[0][14] = ff_avg_h264_qpel16_mc23_msa;
        c->avg_h264_qpel_pixels_tab[0][15] = ff_avg_h264_qpel16_mc33_msa;

        c->avg_h264_qpel_pixels_tab[1][0] = ff_avg_h264_qpel8_mc00_msa;
        c->avg_h264_qpel_pixels_tab[1][1] = ff_avg_h264_qpel8_mc10_msa;
        c->avg_h264_qpel_pixels_tab[1][2] = ff_avg_h264_qpel8_mc20_msa;
        c->avg_h264_qpel_pixels_tab[1][3] = ff_avg_h264_qpel8_mc30_msa;
        c->avg_h264_qpel_pixels_tab[1][4] = ff_avg_h264_qpel8_mc01_msa;
        c->avg_h264_qpel_pixels_tab[1][5] = ff_avg_h264_qpel8_mc11_msa;
        c->avg_h264_qpel_pixels_tab[1][6] = ff_avg_h264_qpel8_mc21_msa;
        c->avg_h264_qpel_pixels_tab[1][7] = ff_avg_h264_qpel8_mc31_msa;
        c->avg_h264_qpel_pixels_tab[1][8] = ff_avg_h264_qpel8_mc02_msa;
        c->avg_h264_qpel_pixels_tab[1][9] = ff_avg_h264_qpel8_mc12_msa;
        c->avg_h264_qpel_pixels_tab[1][10] = ff_avg_h264_qpel8_mc22_msa;
        c->avg_h264_qpel_pixels_tab[1][11] = ff_avg_h264_qpel8_mc32_msa;
        c->avg_h264_qpel_pixels_tab[1][12] = ff_avg_h264_qpel8_mc03_msa;
        c->avg_h264_qpel_pixels_tab[1][13] = ff_avg_h264_qpel8_mc13_msa;
        c->avg_h264_qpel_pixels_tab[1][14] = ff_avg_h264_qpel8_mc23_msa;
        c->avg_h264_qpel_pixels_tab[1][15] = ff_avg_h264_qpel8_mc33_msa;

        c->avg_h264_qpel_pixels_tab[2][0] = ff_avg_h264_qpel4_mc00_msa;
        c->avg_h264_qpel_pixels_tab[2][1] = ff_avg_h264_qpel4_mc10_msa;
        c->avg_h264_qpel_pixels_tab[2][2] = ff_avg_h264_qpel4_mc20_msa;
        c->avg_h264_qpel_pixels_tab[2][3] = ff_avg_h264_qpel4_mc30_msa;
        c->avg_h264_qpel_pixels_tab[2][4] = ff_avg_h264_qpel4_mc01_msa;
        c->avg_h264_qpel_pixels_tab[2][5] = ff_avg_h264_qpel4_mc11_msa;
        c->avg_h264_qpel_pixels_tab[2][6] = ff_avg_h264_qpel4_mc21_msa;
        c->avg_h264_qpel_pixels_tab[2][7] = ff_avg_h264_qpel4_mc31_msa;
        c->avg_h264_qpel_pixels_tab[2][8] = ff_avg_h264_qpel4_mc02_msa;
        c->avg_h264_qpel_pixels_tab[2][9] = ff_avg_h264_qpel4_mc12_msa;
        c->avg_h264_qpel_pixels_tab[2][10] = ff_avg_h264_qpel4_mc22_msa;
        c->avg_h264_qpel_pixels_tab[2][11] = ff_avg_h264_qpel4_mc32_msa;
        c->avg_h264_qpel_pixels_tab[2][12] = ff_avg_h264_qpel4_mc03_msa;
        c->avg_h264_qpel_pixels_tab[2][13] = ff_avg_h264_qpel4_mc13_msa;
        c->avg_h264_qpel_pixels_tab[2][14] = ff_avg_h264_qpel4_mc23_msa;
        c->avg_h264_qpel_pixels_tab[2][15] = ff_avg_h264_qpel4_mc33_msa;
    }
}
#endif  // #if HAVE_MSA

#if HAVE_MMI
static av_cold void h264qpel_init_mmi(H264QpelContext *c, int bit_depth)
{
    if (8 == bit_depth) {
        c->put_h264_qpel_pixels_tab[0][0] = ff_put_h264_qpel16_mc00_mmi;
        c->put_h264_qpel_pixels_tab[0][1] = ff_put_h264_qpel16_mc10_mmi;
        c->put_h264_qpel_pixels_tab[0][2] = ff_put_h264_qpel16_mc20_mmi;
        c->put_h264_qpel_pixels_tab[0][3] = ff_put_h264_qpel16_mc30_mmi;
        c->put_h264_qpel_pixels_tab[0][4] = ff_put_h264_qpel16_mc01_mmi;
        c->put_h264_qpel_pixels_tab[0][5] = ff_put_h264_qpel16_mc11_mmi;
        c->put_h264_qpel_pixels_tab[0][6] = ff_put_h264_qpel16_mc21_mmi;
        c->put_h264_qpel_pixels_tab[0][7] = ff_put_h264_qpel16_mc31_mmi;
        c->put_h264_qpel_pixels_tab[0][8] = ff_put_h264_qpel16_mc02_mmi;
        c->put_h264_qpel_pixels_tab[0][9] = ff_put_h264_qpel16_mc12_mmi;
        c->put_h264_qpel_pixels_tab[0][10] = ff_put_h264_qpel16_mc22_mmi;
        c->put_h264_qpel_pixels_tab[0][11] = ff_put_h264_qpel16_mc32_mmi;
        c->put_h264_qpel_pixels_tab[0][12] = ff_put_h264_qpel16_mc03_mmi;
        c->put_h264_qpel_pixels_tab[0][13] = ff_put_h264_qpel16_mc13_mmi;
        c->put_h264_qpel_pixels_tab[0][14] = ff_put_h264_qpel16_mc23_mmi;
        c->put_h264_qpel_pixels_tab[0][15] = ff_put_h264_qpel16_mc33_mmi;

        c->put_h264_qpel_pixels_tab[1][0] = ff_put_h264_qpel8_mc00_mmi;
        c->put_h264_qpel_pixels_tab[1][1] = ff_put_h264_qpel8_mc10_mmi;
        c->put_h264_qpel_pixels_tab[1][2] = ff_put_h264_qpel8_mc20_mmi;
        c->put_h264_qpel_pixels_tab[1][3] = ff_put_h264_qpel8_mc30_mmi;
        c->put_h264_qpel_pixels_tab[1][4] = ff_put_h264_qpel8_mc01_mmi;
        c->put_h264_qpel_pixels_tab[1][5] = ff_put_h264_qpel8_mc11_mmi;
        c->put_h264_qpel_pixels_tab[1][6] = ff_put_h264_qpel8_mc21_mmi;
        c->put_h264_qpel_pixels_tab[1][7] = ff_put_h264_qpel8_mc31_mmi;
        c->put_h264_qpel_pixels_tab[1][8] = ff_put_h264_qpel8_mc02_mmi;
        c->put_h264_qpel_pixels_tab[1][9] = ff_put_h264_qpel8_mc12_mmi;
        c->put_h264_qpel_pixels_tab[1][10] = ff_put_h264_qpel8_mc22_mmi;
        c->put_h264_qpel_pixels_tab[1][11] = ff_put_h264_qpel8_mc32_mmi;
        c->put_h264_qpel_pixels_tab[1][12] = ff_put_h264_qpel8_mc03_mmi;
        c->put_h264_qpel_pixels_tab[1][13] = ff_put_h264_qpel8_mc13_mmi;
        c->put_h264_qpel_pixels_tab[1][14] = ff_put_h264_qpel8_mc23_mmi;
        c->put_h264_qpel_pixels_tab[1][15] = ff_put_h264_qpel8_mc33_mmi;

        c->put_h264_qpel_pixels_tab[2][0] = ff_put_h264_qpel4_mc00_mmi;
        c->put_h264_qpel_pixels_tab[2][1] = ff_put_h264_qpel4_mc10_mmi;
        c->put_h264_qpel_pixels_tab[2][2] = ff_put_h264_qpel4_mc20_mmi;
        c->put_h264_qpel_pixels_tab[2][3] = ff_put_h264_qpel4_mc30_mmi;
        c->put_h264_qpel_pixels_tab[2][4] = ff_put_h264_qpel4_mc01_mmi;
        c->put_h264_qpel_pixels_tab[2][5] = ff_put_h264_qpel4_mc11_mmi;
        c->put_h264_qpel_pixels_tab[2][6] = ff_put_h264_qpel4_mc21_mmi;
        c->put_h264_qpel_pixels_tab[2][7] = ff_put_h264_qpel4_mc31_mmi;
        c->put_h264_qpel_pixels_tab[2][8] = ff_put_h264_qpel4_mc02_mmi;
        c->put_h264_qpel_pixels_tab[2][9] = ff_put_h264_qpel4_mc12_mmi;
        c->put_h264_qpel_pixels_tab[2][10] = ff_put_h264_qpel4_mc22_mmi;
        c->put_h264_qpel_pixels_tab[2][11] = ff_put_h264_qpel4_mc32_mmi;
        c->put_h264_qpel_pixels_tab[2][12] = ff_put_h264_qpel4_mc03_mmi;
        c->put_h264_qpel_pixels_tab[2][13] = ff_put_h264_qpel4_mc13_mmi;
        c->put_h264_qpel_pixels_tab[2][14] = ff_put_h264_qpel4_mc23_mmi;
        c->put_h264_qpel_pixels_tab[2][15] = ff_put_h264_qpel4_mc33_mmi;

        c->avg_h264_qpel_pixels_tab[0][0] = ff_avg_h264_qpel16_mc00_mmi;
        c->avg_h264_qpel_pixels_tab[0][1] = ff_avg_h264_qpel16_mc10_mmi;
        c->avg_h264_qpel_pixels_tab[0][2] = ff_avg_h264_qpel16_mc20_mmi;
        c->avg_h264_qpel_pixels_tab[0][3] = ff_avg_h264_qpel16_mc30_mmi;
        c->avg_h264_qpel_pixels_tab[0][4] = ff_avg_h264_qpel16_mc01_mmi;
        c->avg_h264_qpel_pixels_tab[0][5] = ff_avg_h264_qpel16_mc11_mmi;
        c->avg_h264_qpel_pixels_tab[0][6] = ff_avg_h264_qpel16_mc21_mmi;
        c->avg_h264_qpel_pixels_tab[0][7] = ff_avg_h264_qpel16_mc31_mmi;
        c->avg_h264_qpel_pixels_tab[0][8] = ff_avg_h264_qpel16_mc02_mmi;
        c->avg_h264_qpel_pixels_tab[0][9] = ff_avg_h264_qpel16_mc12_mmi;
        c->avg_h264_qpel_pixels_tab[0][10] = ff_avg_h264_qpel16_mc22_mmi;
        c->avg_h264_qpel_pixels_tab[0][11] = ff_avg_h264_qpel16_mc32_mmi;
        c->avg_h264_qpel_pixels_tab[0][12] = ff_avg_h264_qpel16_mc03_mmi;
        c->avg_h264_qpel_pixels_tab[0][13] = ff_avg_h264_qpel16_mc13_mmi;
        c->avg_h264_qpel_pixels_tab[0][14] = ff_avg_h264_qpel16_mc23_mmi;
        c->avg_h264_qpel_pixels_tab[0][15] = ff_avg_h264_qpel16_mc33_mmi;

        c->avg_h264_qpel_pixels_tab[1][0] = ff_avg_h264_qpel8_mc00_mmi;
        c->avg_h264_qpel_pixels_tab[1][1] = ff_avg_h264_qpel8_mc10_mmi;
        c->avg_h264_qpel_pixels_tab[1][2] = ff_avg_h264_qpel8_mc20_mmi;
        c->avg_h264_qpel_pixels_tab[1][3] = ff_avg_h264_qpel8_mc30_mmi;
        c->avg_h264_qpel_pixels_tab[1][4] = ff_avg_h264_qpel8_mc01_mmi;
        c->avg_h264_qpel_pixels_tab[1][5] = ff_avg_h264_qpel8_mc11_mmi;
        c->avg_h264_qpel_pixels_tab[1][6] = ff_avg_h264_qpel8_mc21_mmi;
        c->avg_h264_qpel_pixels_tab[1][7] = ff_avg_h264_qpel8_mc31_mmi;
        c->avg_h264_qpel_pixels_tab[1][8] = ff_avg_h264_qpel8_mc02_mmi;
        c->avg_h264_qpel_pixels_tab[1][9] = ff_avg_h264_qpel8_mc12_mmi;
        c->avg_h264_qpel_pixels_tab[1][10] = ff_avg_h264_qpel8_mc22_mmi;
        c->avg_h264_qpel_pixels_tab[1][11] = ff_avg_h264_qpel8_mc32_mmi;
        c->avg_h264_qpel_pixels_tab[1][12] = ff_avg_h264_qpel8_mc03_mmi;
        c->avg_h264_qpel_pixels_tab[1][13] = ff_avg_h264_qpel8_mc13_mmi;
        c->avg_h264_qpel_pixels_tab[1][14] = ff_avg_h264_qpel8_mc23_mmi;
        c->avg_h264_qpel_pixels_tab[1][15] = ff_avg_h264_qpel8_mc33_mmi;

        c->avg_h264_qpel_pixels_tab[2][0] = ff_avg_h264_qpel4_mc00_mmi;
        c->avg_h264_qpel_pixels_tab[2][1] = ff_avg_h264_qpel4_mc10_mmi;
        c->avg_h264_qpel_pixels_tab[2][2] = ff_avg_h264_qpel4_mc20_mmi;
        c->avg_h264_qpel_pixels_tab[2][3] = ff_avg_h264_qpel4_mc30_mmi;
        c->avg_h264_qpel_pixels_tab[2][4] = ff_avg_h264_qpel4_mc01_mmi;
        c->avg_h264_qpel_pixels_tab[2][5] = ff_avg_h264_qpel4_mc11_mmi;
        c->avg_h264_qpel_pixels_tab[2][6] = ff_avg_h264_qpel4_mc21_mmi;
        c->avg_h264_qpel_pixels_tab[2][7] = ff_avg_h264_qpel4_mc31_mmi;
        c->avg_h264_qpel_pixels_tab[2][8] = ff_avg_h264_qpel4_mc02_mmi;
        c->avg_h264_qpel_pixels_tab[2][9] = ff_avg_h264_qpel4_mc12_mmi;
        c->avg_h264_qpel_pixels_tab[2][10] = ff_avg_h264_qpel4_mc22_mmi;
        c->avg_h264_qpel_pixels_tab[2][11] = ff_avg_h264_qpel4_mc32_mmi;
        c->avg_h264_qpel_pixels_tab[2][12] = ff_avg_h264_qpel4_mc03_mmi;
        c->avg_h264_qpel_pixels_tab[2][13] = ff_avg_h264_qpel4_mc13_mmi;
        c->avg_h264_qpel_pixels_tab[2][14] = ff_avg_h264_qpel4_mc23_mmi;
        c->avg_h264_qpel_pixels_tab[2][15] = ff_avg_h264_qpel4_mc33_mmi;
    }
}
#endif /* HAVE_MMI */

av_cold void ff_h264qpel_init_mips(H264QpelContext *c, int bit_depth)
{
#if HAVE_MSA
    h264qpel_init_msa(c, bit_depth);
#endif  // #if HAVE_MSA
#if HAVE_MMI
    h264qpel_init_mmi(c, bit_depth);
#endif /* HAVE_MMI */
}
