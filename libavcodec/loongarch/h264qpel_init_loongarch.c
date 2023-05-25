/*
 * Copyright (c) 2020 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
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

#include "h264qpel_loongarch.h"
#include "libavutil/attributes.h"
#include "libavutil/loongarch/cpu.h"
#include "libavcodec/h264qpel.h"

av_cold void ff_h264qpel_init_loongarch(H264QpelContext *c, int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_lsx(cpu_flags)) {
        if (8 == bit_depth) {
            c->put_h264_qpel_pixels_tab[0][0]  = ff_put_h264_qpel16_mc00_lsx;
            c->put_h264_qpel_pixels_tab[0][1]  = ff_put_h264_qpel16_mc10_lsx;
            c->put_h264_qpel_pixels_tab[0][2]  = ff_put_h264_qpel16_mc20_lsx;
            c->put_h264_qpel_pixels_tab[0][3]  = ff_put_h264_qpel16_mc30_lsx;
            c->put_h264_qpel_pixels_tab[0][4]  = ff_put_h264_qpel16_mc01_lsx;
            c->put_h264_qpel_pixels_tab[0][5]  = ff_put_h264_qpel16_mc11_lsx;
            c->put_h264_qpel_pixels_tab[0][6]  = ff_put_h264_qpel16_mc21_lsx;
            c->put_h264_qpel_pixels_tab[0][7]  = ff_put_h264_qpel16_mc31_lsx;
            c->put_h264_qpel_pixels_tab[0][8]  = ff_put_h264_qpel16_mc02_lsx;
            c->put_h264_qpel_pixels_tab[0][9]  = ff_put_h264_qpel16_mc12_lsx;
            c->put_h264_qpel_pixels_tab[0][10] = ff_put_h264_qpel16_mc22_lsx;
            c->put_h264_qpel_pixels_tab[0][11] = ff_put_h264_qpel16_mc32_lsx;
            c->put_h264_qpel_pixels_tab[0][12] = ff_put_h264_qpel16_mc03_lsx;
            c->put_h264_qpel_pixels_tab[0][13] = ff_put_h264_qpel16_mc13_lsx;
            c->put_h264_qpel_pixels_tab[0][14] = ff_put_h264_qpel16_mc23_lsx;
            c->put_h264_qpel_pixels_tab[0][15] = ff_put_h264_qpel16_mc33_lsx;

            c->avg_h264_qpel_pixels_tab[0][0]  = ff_avg_h264_qpel16_mc00_lsx;
            c->avg_h264_qpel_pixels_tab[0][1]  = ff_avg_h264_qpel16_mc10_lsx;
            c->avg_h264_qpel_pixels_tab[0][2]  = ff_avg_h264_qpel16_mc20_lsx;
            c->avg_h264_qpel_pixels_tab[0][3]  = ff_avg_h264_qpel16_mc30_lsx;
            c->avg_h264_qpel_pixels_tab[0][4]  = ff_avg_h264_qpel16_mc01_lsx;
            c->avg_h264_qpel_pixels_tab[0][5]  = ff_avg_h264_qpel16_mc11_lsx;
            c->avg_h264_qpel_pixels_tab[0][6]  = ff_avg_h264_qpel16_mc21_lsx;
            c->avg_h264_qpel_pixels_tab[0][7]  = ff_avg_h264_qpel16_mc31_lsx;
            c->avg_h264_qpel_pixels_tab[0][8]  = ff_avg_h264_qpel16_mc02_lsx;
            c->avg_h264_qpel_pixels_tab[0][9]  = ff_avg_h264_qpel16_mc12_lsx;
            c->avg_h264_qpel_pixels_tab[0][10] = ff_avg_h264_qpel16_mc22_lsx;
            c->avg_h264_qpel_pixels_tab[0][11] = ff_avg_h264_qpel16_mc32_lsx;
            c->avg_h264_qpel_pixels_tab[0][12] = ff_avg_h264_qpel16_mc03_lsx;
            c->avg_h264_qpel_pixels_tab[0][13] = ff_avg_h264_qpel16_mc13_lsx;
            c->avg_h264_qpel_pixels_tab[0][14] = ff_avg_h264_qpel16_mc23_lsx;
            c->avg_h264_qpel_pixels_tab[0][15] = ff_avg_h264_qpel16_mc33_lsx;

            c->put_h264_qpel_pixels_tab[1][0]  = ff_put_h264_qpel8_mc00_lsx;
            c->put_h264_qpel_pixels_tab[1][1]  = ff_put_h264_qpel8_mc10_lsx;
            c->put_h264_qpel_pixels_tab[1][2]  = ff_put_h264_qpel8_mc20_lsx;
            c->put_h264_qpel_pixels_tab[1][3]  = ff_put_h264_qpel8_mc30_lsx;
            c->put_h264_qpel_pixels_tab[1][4]  = ff_put_h264_qpel8_mc01_lsx;
            c->put_h264_qpel_pixels_tab[1][5]  = ff_put_h264_qpel8_mc11_lsx;
            c->put_h264_qpel_pixels_tab[1][6]  = ff_put_h264_qpel8_mc21_lsx;
            c->put_h264_qpel_pixels_tab[1][7]  = ff_put_h264_qpel8_mc31_lsx;
            c->put_h264_qpel_pixels_tab[1][8]  = ff_put_h264_qpel8_mc02_lsx;
            c->put_h264_qpel_pixels_tab[1][9]  = ff_put_h264_qpel8_mc12_lsx;
            c->put_h264_qpel_pixels_tab[1][10] = ff_put_h264_qpel8_mc22_lsx;
            c->put_h264_qpel_pixels_tab[1][11] = ff_put_h264_qpel8_mc32_lsx;
            c->put_h264_qpel_pixels_tab[1][12] = ff_put_h264_qpel8_mc03_lsx;
            c->put_h264_qpel_pixels_tab[1][13] = ff_put_h264_qpel8_mc13_lsx;
            c->put_h264_qpel_pixels_tab[1][14] = ff_put_h264_qpel8_mc23_lsx;
            c->put_h264_qpel_pixels_tab[1][15] = ff_put_h264_qpel8_mc33_lsx;

            c->avg_h264_qpel_pixels_tab[1][0]  = ff_avg_h264_qpel8_mc00_lsx;
            c->avg_h264_qpel_pixels_tab[1][1]  = ff_avg_h264_qpel8_mc10_lsx;
            c->avg_h264_qpel_pixels_tab[1][2]  = ff_avg_h264_qpel8_mc20_lsx;
            c->avg_h264_qpel_pixels_tab[1][3]  = ff_avg_h264_qpel8_mc30_lsx;
            c->avg_h264_qpel_pixels_tab[1][5]  = ff_avg_h264_qpel8_mc11_lsx;
            c->avg_h264_qpel_pixels_tab[1][6]  = ff_avg_h264_qpel8_mc21_lsx;
            c->avg_h264_qpel_pixels_tab[1][7]  = ff_avg_h264_qpel8_mc31_lsx;
            c->avg_h264_qpel_pixels_tab[1][8]  = ff_avg_h264_qpel8_mc02_lsx;
            c->avg_h264_qpel_pixels_tab[1][9]  = ff_avg_h264_qpel8_mc12_lsx;
            c->avg_h264_qpel_pixels_tab[1][10] = ff_avg_h264_qpel8_mc22_lsx;
            c->avg_h264_qpel_pixels_tab[1][11] = ff_avg_h264_qpel8_mc32_lsx;
            c->avg_h264_qpel_pixels_tab[1][13] = ff_avg_h264_qpel8_mc13_lsx;
            c->avg_h264_qpel_pixels_tab[1][14] = ff_avg_h264_qpel8_mc23_lsx;
            c->avg_h264_qpel_pixels_tab[1][15] = ff_avg_h264_qpel8_mc33_lsx;
        }
    }
#if HAVE_LASX
    if (have_lasx(cpu_flags)) {
        if (8 == bit_depth) {
            c->put_h264_qpel_pixels_tab[0][0]  = ff_put_h264_qpel16_mc00_lasx;
            c->put_h264_qpel_pixels_tab[0][1]  = ff_put_h264_qpel16_mc10_lasx;
            c->put_h264_qpel_pixels_tab[0][2]  = ff_put_h264_qpel16_mc20_lasx;
            c->put_h264_qpel_pixels_tab[0][3]  = ff_put_h264_qpel16_mc30_lasx;
            c->put_h264_qpel_pixels_tab[0][4]  = ff_put_h264_qpel16_mc01_lasx;
            c->put_h264_qpel_pixels_tab[0][5]  = ff_put_h264_qpel16_mc11_lasx;

            c->put_h264_qpel_pixels_tab[0][6]  = ff_put_h264_qpel16_mc21_lasx;
            c->put_h264_qpel_pixels_tab[0][7]  = ff_put_h264_qpel16_mc31_lasx;
            c->put_h264_qpel_pixels_tab[0][8]  = ff_put_h264_qpel16_mc02_lasx;
            c->put_h264_qpel_pixels_tab[0][9]  = ff_put_h264_qpel16_mc12_lasx;
            c->put_h264_qpel_pixels_tab[0][10] = ff_put_h264_qpel16_mc22_lasx;
            c->put_h264_qpel_pixels_tab[0][11] = ff_put_h264_qpel16_mc32_lasx;
            c->put_h264_qpel_pixels_tab[0][12] = ff_put_h264_qpel16_mc03_lasx;
            c->put_h264_qpel_pixels_tab[0][13] = ff_put_h264_qpel16_mc13_lasx;
            c->put_h264_qpel_pixels_tab[0][14] = ff_put_h264_qpel16_mc23_lasx;
            c->put_h264_qpel_pixels_tab[0][15] = ff_put_h264_qpel16_mc33_lasx;
            c->avg_h264_qpel_pixels_tab[0][0]  = ff_avg_h264_qpel16_mc00_lasx;
            c->avg_h264_qpel_pixels_tab[0][1]  = ff_avg_h264_qpel16_mc10_lasx;
            c->avg_h264_qpel_pixels_tab[0][2]  = ff_avg_h264_qpel16_mc20_lasx;
            c->avg_h264_qpel_pixels_tab[0][3]  = ff_avg_h264_qpel16_mc30_lasx;
            c->avg_h264_qpel_pixels_tab[0][4]  = ff_avg_h264_qpel16_mc01_lasx;
            c->avg_h264_qpel_pixels_tab[0][5]  = ff_avg_h264_qpel16_mc11_lasx;
            c->avg_h264_qpel_pixels_tab[0][6]  = ff_avg_h264_qpel16_mc21_lasx;
            c->avg_h264_qpel_pixels_tab[0][7]  = ff_avg_h264_qpel16_mc31_lasx;
            c->avg_h264_qpel_pixels_tab[0][8]  = ff_avg_h264_qpel16_mc02_lasx;
            c->avg_h264_qpel_pixels_tab[0][9]  = ff_avg_h264_qpel16_mc12_lasx;
            c->avg_h264_qpel_pixels_tab[0][10] = ff_avg_h264_qpel16_mc22_lasx;
            c->avg_h264_qpel_pixels_tab[0][11] = ff_avg_h264_qpel16_mc32_lasx;
            c->avg_h264_qpel_pixels_tab[0][12] = ff_avg_h264_qpel16_mc03_lasx;
            c->avg_h264_qpel_pixels_tab[0][13] = ff_avg_h264_qpel16_mc13_lasx;
            c->avg_h264_qpel_pixels_tab[0][14] = ff_avg_h264_qpel16_mc23_lasx;
            c->avg_h264_qpel_pixels_tab[0][15] = ff_avg_h264_qpel16_mc33_lasx;

            c->put_h264_qpel_pixels_tab[1][0]  = ff_put_h264_qpel8_mc00_lasx;
            c->put_h264_qpel_pixels_tab[1][1]  = ff_put_h264_qpel8_mc10_lasx;
            c->put_h264_qpel_pixels_tab[1][2]  = ff_put_h264_qpel8_mc20_lasx;
            c->put_h264_qpel_pixels_tab[1][3]  = ff_put_h264_qpel8_mc30_lasx;
            c->put_h264_qpel_pixels_tab[1][4]  = ff_put_h264_qpel8_mc01_lasx;
            c->put_h264_qpel_pixels_tab[1][5]  = ff_put_h264_qpel8_mc11_lasx;
            c->put_h264_qpel_pixels_tab[1][6]  = ff_put_h264_qpel8_mc21_lasx;
            c->put_h264_qpel_pixels_tab[1][7]  = ff_put_h264_qpel8_mc31_lasx;
            c->put_h264_qpel_pixels_tab[1][8]  = ff_put_h264_qpel8_mc02_lasx;
            c->put_h264_qpel_pixels_tab[1][9]  = ff_put_h264_qpel8_mc12_lasx;
            c->put_h264_qpel_pixels_tab[1][10] = ff_put_h264_qpel8_mc22_lasx;
            c->put_h264_qpel_pixels_tab[1][11] = ff_put_h264_qpel8_mc32_lasx;
            c->put_h264_qpel_pixels_tab[1][12] = ff_put_h264_qpel8_mc03_lasx;
            c->put_h264_qpel_pixels_tab[1][13] = ff_put_h264_qpel8_mc13_lasx;
            c->put_h264_qpel_pixels_tab[1][14] = ff_put_h264_qpel8_mc23_lasx;
            c->put_h264_qpel_pixels_tab[1][15] = ff_put_h264_qpel8_mc33_lasx;
            c->avg_h264_qpel_pixels_tab[1][0]  = ff_avg_h264_qpel8_mc00_lasx;
            c->avg_h264_qpel_pixels_tab[1][1]  = ff_avg_h264_qpel8_mc10_lasx;
            c->avg_h264_qpel_pixels_tab[1][2]  = ff_avg_h264_qpel8_mc20_lasx;
            c->avg_h264_qpel_pixels_tab[1][3]  = ff_avg_h264_qpel8_mc30_lasx;
            c->avg_h264_qpel_pixels_tab[1][5]  = ff_avg_h264_qpel8_mc11_lasx;
            c->avg_h264_qpel_pixels_tab[1][6]  = ff_avg_h264_qpel8_mc21_lasx;
            c->avg_h264_qpel_pixels_tab[1][7]  = ff_avg_h264_qpel8_mc31_lasx;
            c->avg_h264_qpel_pixels_tab[1][8]  = ff_avg_h264_qpel8_mc02_lasx;
            c->avg_h264_qpel_pixels_tab[1][9]  = ff_avg_h264_qpel8_mc12_lasx;
            c->avg_h264_qpel_pixels_tab[1][10] = ff_avg_h264_qpel8_mc22_lasx;
            c->avg_h264_qpel_pixels_tab[1][11] = ff_avg_h264_qpel8_mc32_lasx;
            c->avg_h264_qpel_pixels_tab[1][13] = ff_avg_h264_qpel8_mc13_lasx;
            c->avg_h264_qpel_pixels_tab[1][14] = ff_avg_h264_qpel8_mc23_lasx;
            c->avg_h264_qpel_pixels_tab[1][15] = ff_avg_h264_qpel8_mc33_lasx;
        }
    }
#endif
}
