/*
 * Copyright (c) 2015 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

#include "libavcodec/mips/hevcdsp_mips.h"

#if HAVE_MMI
static av_cold void hevc_dsp_init_mmi(HEVCDSPContext *c,
                                      const int bit_depth)
{
    if (8 == bit_depth) {
        c->put_hevc_qpel[1][1][1] = ff_hevc_put_hevc_qpel_hv4_8_mmi;
        c->put_hevc_qpel[3][1][1] = ff_hevc_put_hevc_qpel_hv8_8_mmi;
        c->put_hevc_qpel[4][1][1] = ff_hevc_put_hevc_qpel_hv12_8_mmi;
        c->put_hevc_qpel[5][1][1] = ff_hevc_put_hevc_qpel_hv16_8_mmi;
        c->put_hevc_qpel[6][1][1] = ff_hevc_put_hevc_qpel_hv24_8_mmi;
        c->put_hevc_qpel[7][1][1] = ff_hevc_put_hevc_qpel_hv32_8_mmi;
        c->put_hevc_qpel[8][1][1] = ff_hevc_put_hevc_qpel_hv48_8_mmi;
        c->put_hevc_qpel[9][1][1] = ff_hevc_put_hevc_qpel_hv64_8_mmi;

        c->put_hevc_qpel_bi[1][1][1] = ff_hevc_put_hevc_qpel_bi_hv4_8_mmi;
        c->put_hevc_qpel_bi[3][1][1] = ff_hevc_put_hevc_qpel_bi_hv8_8_mmi;
        c->put_hevc_qpel_bi[4][1][1] = ff_hevc_put_hevc_qpel_bi_hv12_8_mmi;
        c->put_hevc_qpel_bi[5][1][1] = ff_hevc_put_hevc_qpel_bi_hv16_8_mmi;
        c->put_hevc_qpel_bi[6][1][1] = ff_hevc_put_hevc_qpel_bi_hv24_8_mmi;
        c->put_hevc_qpel_bi[7][1][1] = ff_hevc_put_hevc_qpel_bi_hv32_8_mmi;
        c->put_hevc_qpel_bi[8][1][1] = ff_hevc_put_hevc_qpel_bi_hv48_8_mmi;
        c->put_hevc_qpel_bi[9][1][1] = ff_hevc_put_hevc_qpel_bi_hv64_8_mmi;

        c->put_hevc_qpel_bi[3][0][0] = ff_hevc_put_hevc_pel_bi_pixels8_8_mmi;
        c->put_hevc_qpel_bi[5][0][0] = ff_hevc_put_hevc_pel_bi_pixels16_8_mmi;
        c->put_hevc_qpel_bi[6][0][0] = ff_hevc_put_hevc_pel_bi_pixels24_8_mmi;
        c->put_hevc_qpel_bi[7][0][0] = ff_hevc_put_hevc_pel_bi_pixels32_8_mmi;
        c->put_hevc_qpel_bi[8][0][0] = ff_hevc_put_hevc_pel_bi_pixels48_8_mmi;
        c->put_hevc_qpel_bi[9][0][0] = ff_hevc_put_hevc_pel_bi_pixels64_8_mmi;

        c->put_hevc_epel_bi[3][0][0] = ff_hevc_put_hevc_pel_bi_pixels8_8_mmi;
        c->put_hevc_epel_bi[5][0][0] = ff_hevc_put_hevc_pel_bi_pixels16_8_mmi;
        c->put_hevc_epel_bi[6][0][0] = ff_hevc_put_hevc_pel_bi_pixels24_8_mmi;
        c->put_hevc_epel_bi[7][0][0] = ff_hevc_put_hevc_pel_bi_pixels32_8_mmi;
    }
}
#endif // #if HAVE_MMI

#if HAVE_MSA
static av_cold void hevc_dsp_init_msa(HEVCDSPContext *c,
                                      const int bit_depth)
{
    if (8 == bit_depth) {
        c->put_hevc_qpel[1][0][0] = ff_hevc_put_hevc_pel_pixels4_8_msa;
        c->put_hevc_qpel[2][0][0] = ff_hevc_put_hevc_pel_pixels6_8_msa;
        c->put_hevc_qpel[3][0][0] = ff_hevc_put_hevc_pel_pixels8_8_msa;
        c->put_hevc_qpel[4][0][0] = ff_hevc_put_hevc_pel_pixels12_8_msa;
        c->put_hevc_qpel[5][0][0] = ff_hevc_put_hevc_pel_pixels16_8_msa;
        c->put_hevc_qpel[6][0][0] = ff_hevc_put_hevc_pel_pixels24_8_msa;
        c->put_hevc_qpel[7][0][0] = ff_hevc_put_hevc_pel_pixels32_8_msa;
        c->put_hevc_qpel[8][0][0] = ff_hevc_put_hevc_pel_pixels48_8_msa;
        c->put_hevc_qpel[9][0][0] = ff_hevc_put_hevc_pel_pixels64_8_msa;

        c->put_hevc_qpel[1][0][1] = ff_hevc_put_hevc_qpel_h4_8_msa;
        c->put_hevc_qpel[3][0][1] = ff_hevc_put_hevc_qpel_h8_8_msa;
        c->put_hevc_qpel[4][0][1] = ff_hevc_put_hevc_qpel_h12_8_msa;
        c->put_hevc_qpel[5][0][1] = ff_hevc_put_hevc_qpel_h16_8_msa;
        c->put_hevc_qpel[6][0][1] = ff_hevc_put_hevc_qpel_h24_8_msa;
        c->put_hevc_qpel[7][0][1] = ff_hevc_put_hevc_qpel_h32_8_msa;
        c->put_hevc_qpel[8][0][1] = ff_hevc_put_hevc_qpel_h48_8_msa;
        c->put_hevc_qpel[9][0][1] = ff_hevc_put_hevc_qpel_h64_8_msa;

        c->put_hevc_qpel[1][1][0] = ff_hevc_put_hevc_qpel_v4_8_msa;
        c->put_hevc_qpel[3][1][0] = ff_hevc_put_hevc_qpel_v8_8_msa;
        c->put_hevc_qpel[4][1][0] = ff_hevc_put_hevc_qpel_v12_8_msa;
        c->put_hevc_qpel[5][1][0] = ff_hevc_put_hevc_qpel_v16_8_msa;
        c->put_hevc_qpel[6][1][0] = ff_hevc_put_hevc_qpel_v24_8_msa;
        c->put_hevc_qpel[7][1][0] = ff_hevc_put_hevc_qpel_v32_8_msa;
        c->put_hevc_qpel[8][1][0] = ff_hevc_put_hevc_qpel_v48_8_msa;
        c->put_hevc_qpel[9][1][0] = ff_hevc_put_hevc_qpel_v64_8_msa;

        c->put_hevc_qpel[1][1][1] = ff_hevc_put_hevc_qpel_hv4_8_msa;
        c->put_hevc_qpel[3][1][1] = ff_hevc_put_hevc_qpel_hv8_8_msa;
        c->put_hevc_qpel[4][1][1] = ff_hevc_put_hevc_qpel_hv12_8_msa;
        c->put_hevc_qpel[5][1][1] = ff_hevc_put_hevc_qpel_hv16_8_msa;
        c->put_hevc_qpel[6][1][1] = ff_hevc_put_hevc_qpel_hv24_8_msa;
        c->put_hevc_qpel[7][1][1] = ff_hevc_put_hevc_qpel_hv32_8_msa;
        c->put_hevc_qpel[8][1][1] = ff_hevc_put_hevc_qpel_hv48_8_msa;
        c->put_hevc_qpel[9][1][1] = ff_hevc_put_hevc_qpel_hv64_8_msa;

        c->put_hevc_epel[1][0][0] = ff_hevc_put_hevc_pel_pixels4_8_msa;
        c->put_hevc_epel[2][0][0] = ff_hevc_put_hevc_pel_pixels6_8_msa;
        c->put_hevc_epel[3][0][0] = ff_hevc_put_hevc_pel_pixels8_8_msa;
        c->put_hevc_epel[4][0][0] = ff_hevc_put_hevc_pel_pixels12_8_msa;
        c->put_hevc_epel[5][0][0] = ff_hevc_put_hevc_pel_pixels16_8_msa;
        c->put_hevc_epel[6][0][0] = ff_hevc_put_hevc_pel_pixels24_8_msa;
        c->put_hevc_epel[7][0][0] = ff_hevc_put_hevc_pel_pixels32_8_msa;

        c->put_hevc_epel[1][0][1] = ff_hevc_put_hevc_epel_h4_8_msa;
        c->put_hevc_epel[2][0][1] = ff_hevc_put_hevc_epel_h6_8_msa;
        c->put_hevc_epel[3][0][1] = ff_hevc_put_hevc_epel_h8_8_msa;
        c->put_hevc_epel[4][0][1] = ff_hevc_put_hevc_epel_h12_8_msa;
        c->put_hevc_epel[5][0][1] = ff_hevc_put_hevc_epel_h16_8_msa;
        c->put_hevc_epel[6][0][1] = ff_hevc_put_hevc_epel_h24_8_msa;
        c->put_hevc_epel[7][0][1] = ff_hevc_put_hevc_epel_h32_8_msa;

        c->put_hevc_epel[1][1][0] = ff_hevc_put_hevc_epel_v4_8_msa;
        c->put_hevc_epel[2][1][0] = ff_hevc_put_hevc_epel_v6_8_msa;
        c->put_hevc_epel[3][1][0] = ff_hevc_put_hevc_epel_v8_8_msa;
        c->put_hevc_epel[4][1][0] = ff_hevc_put_hevc_epel_v12_8_msa;
        c->put_hevc_epel[5][1][0] = ff_hevc_put_hevc_epel_v16_8_msa;
        c->put_hevc_epel[6][1][0] = ff_hevc_put_hevc_epel_v24_8_msa;
        c->put_hevc_epel[7][1][0] = ff_hevc_put_hevc_epel_v32_8_msa;

        c->put_hevc_epel[1][1][1] = ff_hevc_put_hevc_epel_hv4_8_msa;
        c->put_hevc_epel[2][1][1] = ff_hevc_put_hevc_epel_hv6_8_msa;
        c->put_hevc_epel[3][1][1] = ff_hevc_put_hevc_epel_hv8_8_msa;
        c->put_hevc_epel[4][1][1] = ff_hevc_put_hevc_epel_hv12_8_msa;
        c->put_hevc_epel[5][1][1] = ff_hevc_put_hevc_epel_hv16_8_msa;
        c->put_hevc_epel[6][1][1] = ff_hevc_put_hevc_epel_hv24_8_msa;
        c->put_hevc_epel[7][1][1] = ff_hevc_put_hevc_epel_hv32_8_msa;

        c->put_hevc_qpel_uni[3][0][0] = ff_hevc_put_hevc_uni_pel_pixels8_8_msa;
        c->put_hevc_qpel_uni[4][0][0] = ff_hevc_put_hevc_uni_pel_pixels12_8_msa;
        c->put_hevc_qpel_uni[5][0][0] = ff_hevc_put_hevc_uni_pel_pixels16_8_msa;
        c->put_hevc_qpel_uni[6][0][0] = ff_hevc_put_hevc_uni_pel_pixels24_8_msa;
        c->put_hevc_qpel_uni[7][0][0] = ff_hevc_put_hevc_uni_pel_pixels32_8_msa;
        c->put_hevc_qpel_uni[8][0][0] = ff_hevc_put_hevc_uni_pel_pixels48_8_msa;
        c->put_hevc_qpel_uni[9][0][0] = ff_hevc_put_hevc_uni_pel_pixels64_8_msa;

        c->put_hevc_qpel_uni[1][0][1] = ff_hevc_put_hevc_uni_qpel_h4_8_msa;
        c->put_hevc_qpel_uni[3][0][1] = ff_hevc_put_hevc_uni_qpel_h8_8_msa;
        c->put_hevc_qpel_uni[4][0][1] = ff_hevc_put_hevc_uni_qpel_h12_8_msa;
        c->put_hevc_qpel_uni[5][0][1] = ff_hevc_put_hevc_uni_qpel_h16_8_msa;
        c->put_hevc_qpel_uni[6][0][1] = ff_hevc_put_hevc_uni_qpel_h24_8_msa;
        c->put_hevc_qpel_uni[7][0][1] = ff_hevc_put_hevc_uni_qpel_h32_8_msa;
        c->put_hevc_qpel_uni[8][0][1] = ff_hevc_put_hevc_uni_qpel_h48_8_msa;
        c->put_hevc_qpel_uni[9][0][1] = ff_hevc_put_hevc_uni_qpel_h64_8_msa;

        c->put_hevc_qpel_uni[1][1][0] = ff_hevc_put_hevc_uni_qpel_v4_8_msa;
        c->put_hevc_qpel_uni[3][1][0] = ff_hevc_put_hevc_uni_qpel_v8_8_msa;
        c->put_hevc_qpel_uni[4][1][0] = ff_hevc_put_hevc_uni_qpel_v12_8_msa;
        c->put_hevc_qpel_uni[5][1][0] = ff_hevc_put_hevc_uni_qpel_v16_8_msa;
        c->put_hevc_qpel_uni[6][1][0] = ff_hevc_put_hevc_uni_qpel_v24_8_msa;
        c->put_hevc_qpel_uni[7][1][0] = ff_hevc_put_hevc_uni_qpel_v32_8_msa;
        c->put_hevc_qpel_uni[8][1][0] = ff_hevc_put_hevc_uni_qpel_v48_8_msa;
        c->put_hevc_qpel_uni[9][1][0] = ff_hevc_put_hevc_uni_qpel_v64_8_msa;

        c->put_hevc_qpel_uni[1][1][1] = ff_hevc_put_hevc_uni_qpel_hv4_8_msa;
        c->put_hevc_qpel_uni[3][1][1] = ff_hevc_put_hevc_uni_qpel_hv8_8_msa;
        c->put_hevc_qpel_uni[4][1][1] = ff_hevc_put_hevc_uni_qpel_hv12_8_msa;
        c->put_hevc_qpel_uni[5][1][1] = ff_hevc_put_hevc_uni_qpel_hv16_8_msa;
        c->put_hevc_qpel_uni[6][1][1] = ff_hevc_put_hevc_uni_qpel_hv24_8_msa;
        c->put_hevc_qpel_uni[7][1][1] = ff_hevc_put_hevc_uni_qpel_hv32_8_msa;
        c->put_hevc_qpel_uni[8][1][1] = ff_hevc_put_hevc_uni_qpel_hv48_8_msa;
        c->put_hevc_qpel_uni[9][1][1] = ff_hevc_put_hevc_uni_qpel_hv64_8_msa;

        c->put_hevc_epel_uni[3][0][0] = ff_hevc_put_hevc_uni_pel_pixels8_8_msa;
        c->put_hevc_epel_uni[4][0][0] = ff_hevc_put_hevc_uni_pel_pixels12_8_msa;
        c->put_hevc_epel_uni[5][0][0] = ff_hevc_put_hevc_uni_pel_pixels16_8_msa;
        c->put_hevc_epel_uni[6][0][0] = ff_hevc_put_hevc_uni_pel_pixels24_8_msa;
        c->put_hevc_epel_uni[7][0][0] = ff_hevc_put_hevc_uni_pel_pixels32_8_msa;

        c->put_hevc_epel_uni[1][0][1] = ff_hevc_put_hevc_uni_epel_h4_8_msa;
        c->put_hevc_epel_uni[2][0][1] = ff_hevc_put_hevc_uni_epel_h6_8_msa;
        c->put_hevc_epel_uni[3][0][1] = ff_hevc_put_hevc_uni_epel_h8_8_msa;
        c->put_hevc_epel_uni[4][0][1] = ff_hevc_put_hevc_uni_epel_h12_8_msa;
        c->put_hevc_epel_uni[5][0][1] = ff_hevc_put_hevc_uni_epel_h16_8_msa;
        c->put_hevc_epel_uni[6][0][1] = ff_hevc_put_hevc_uni_epel_h24_8_msa;
        c->put_hevc_epel_uni[7][0][1] = ff_hevc_put_hevc_uni_epel_h32_8_msa;

        c->put_hevc_epel_uni[1][1][0] = ff_hevc_put_hevc_uni_epel_v4_8_msa;
        c->put_hevc_epel_uni[2][1][0] = ff_hevc_put_hevc_uni_epel_v6_8_msa;
        c->put_hevc_epel_uni[3][1][0] = ff_hevc_put_hevc_uni_epel_v8_8_msa;
        c->put_hevc_epel_uni[4][1][0] = ff_hevc_put_hevc_uni_epel_v12_8_msa;
        c->put_hevc_epel_uni[5][1][0] = ff_hevc_put_hevc_uni_epel_v16_8_msa;
        c->put_hevc_epel_uni[6][1][0] = ff_hevc_put_hevc_uni_epel_v24_8_msa;
        c->put_hevc_epel_uni[7][1][0] = ff_hevc_put_hevc_uni_epel_v32_8_msa;

        c->put_hevc_epel_uni[1][1][1] = ff_hevc_put_hevc_uni_epel_hv4_8_msa;
        c->put_hevc_epel_uni[2][1][1] = ff_hevc_put_hevc_uni_epel_hv6_8_msa;
        c->put_hevc_epel_uni[3][1][1] = ff_hevc_put_hevc_uni_epel_hv8_8_msa;
        c->put_hevc_epel_uni[4][1][1] = ff_hevc_put_hevc_uni_epel_hv12_8_msa;
        c->put_hevc_epel_uni[5][1][1] = ff_hevc_put_hevc_uni_epel_hv16_8_msa;
        c->put_hevc_epel_uni[6][1][1] = ff_hevc_put_hevc_uni_epel_hv24_8_msa;
        c->put_hevc_epel_uni[7][1][1] = ff_hevc_put_hevc_uni_epel_hv32_8_msa;

        c->put_hevc_qpel_uni_w[1][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels4_8_msa;
        c->put_hevc_qpel_uni_w[3][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels8_8_msa;
        c->put_hevc_qpel_uni_w[4][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels12_8_msa;
        c->put_hevc_qpel_uni_w[5][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels16_8_msa;
        c->put_hevc_qpel_uni_w[6][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels24_8_msa;
        c->put_hevc_qpel_uni_w[7][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels32_8_msa;
        c->put_hevc_qpel_uni_w[8][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels48_8_msa;
        c->put_hevc_qpel_uni_w[9][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels64_8_msa;

        c->put_hevc_qpel_uni_w[1][0][1] = ff_hevc_put_hevc_uni_w_qpel_h4_8_msa;
        c->put_hevc_qpel_uni_w[3][0][1] = ff_hevc_put_hevc_uni_w_qpel_h8_8_msa;
        c->put_hevc_qpel_uni_w[4][0][1] = ff_hevc_put_hevc_uni_w_qpel_h12_8_msa;
        c->put_hevc_qpel_uni_w[5][0][1] = ff_hevc_put_hevc_uni_w_qpel_h16_8_msa;
        c->put_hevc_qpel_uni_w[6][0][1] = ff_hevc_put_hevc_uni_w_qpel_h24_8_msa;
        c->put_hevc_qpel_uni_w[7][0][1] = ff_hevc_put_hevc_uni_w_qpel_h32_8_msa;
        c->put_hevc_qpel_uni_w[8][0][1] = ff_hevc_put_hevc_uni_w_qpel_h48_8_msa;
        c->put_hevc_qpel_uni_w[9][0][1] = ff_hevc_put_hevc_uni_w_qpel_h64_8_msa;

        c->put_hevc_qpel_uni_w[1][1][0] = ff_hevc_put_hevc_uni_w_qpel_v4_8_msa;
        c->put_hevc_qpel_uni_w[3][1][0] = ff_hevc_put_hevc_uni_w_qpel_v8_8_msa;
        c->put_hevc_qpel_uni_w[4][1][0] = ff_hevc_put_hevc_uni_w_qpel_v12_8_msa;
        c->put_hevc_qpel_uni_w[5][1][0] = ff_hevc_put_hevc_uni_w_qpel_v16_8_msa;
        c->put_hevc_qpel_uni_w[6][1][0] = ff_hevc_put_hevc_uni_w_qpel_v24_8_msa;
        c->put_hevc_qpel_uni_w[7][1][0] = ff_hevc_put_hevc_uni_w_qpel_v32_8_msa;
        c->put_hevc_qpel_uni_w[8][1][0] = ff_hevc_put_hevc_uni_w_qpel_v48_8_msa;
        c->put_hevc_qpel_uni_w[9][1][0] = ff_hevc_put_hevc_uni_w_qpel_v64_8_msa;

        c->put_hevc_qpel_uni_w[1][1][1] = ff_hevc_put_hevc_uni_w_qpel_hv4_8_msa;
        c->put_hevc_qpel_uni_w[3][1][1] = ff_hevc_put_hevc_uni_w_qpel_hv8_8_msa;
        c->put_hevc_qpel_uni_w[4][1][1] =
            ff_hevc_put_hevc_uni_w_qpel_hv12_8_msa;
        c->put_hevc_qpel_uni_w[5][1][1] =
            ff_hevc_put_hevc_uni_w_qpel_hv16_8_msa;
        c->put_hevc_qpel_uni_w[6][1][1] =
            ff_hevc_put_hevc_uni_w_qpel_hv24_8_msa;
        c->put_hevc_qpel_uni_w[7][1][1] =
            ff_hevc_put_hevc_uni_w_qpel_hv32_8_msa;
        c->put_hevc_qpel_uni_w[8][1][1] =
            ff_hevc_put_hevc_uni_w_qpel_hv48_8_msa;
        c->put_hevc_qpel_uni_w[9][1][1] =
            ff_hevc_put_hevc_uni_w_qpel_hv64_8_msa;

        c->put_hevc_epel_uni_w[1][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels4_8_msa;
        c->put_hevc_epel_uni_w[2][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels6_8_msa;
        c->put_hevc_epel_uni_w[3][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels8_8_msa;
        c->put_hevc_epel_uni_w[4][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels12_8_msa;
        c->put_hevc_epel_uni_w[5][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels16_8_msa;
        c->put_hevc_epel_uni_w[6][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels24_8_msa;
        c->put_hevc_epel_uni_w[7][0][0] =
            ff_hevc_put_hevc_uni_w_pel_pixels32_8_msa;

        c->put_hevc_epel_uni_w[1][0][1] = ff_hevc_put_hevc_uni_w_epel_h4_8_msa;
        c->put_hevc_epel_uni_w[2][0][1] = ff_hevc_put_hevc_uni_w_epel_h6_8_msa;
        c->put_hevc_epel_uni_w[3][0][1] = ff_hevc_put_hevc_uni_w_epel_h8_8_msa;
        c->put_hevc_epel_uni_w[4][0][1] = ff_hevc_put_hevc_uni_w_epel_h12_8_msa;
        c->put_hevc_epel_uni_w[5][0][1] = ff_hevc_put_hevc_uni_w_epel_h16_8_msa;
        c->put_hevc_epel_uni_w[6][0][1] = ff_hevc_put_hevc_uni_w_epel_h24_8_msa;
        c->put_hevc_epel_uni_w[7][0][1] = ff_hevc_put_hevc_uni_w_epel_h32_8_msa;

        c->put_hevc_epel_uni_w[1][1][0] = ff_hevc_put_hevc_uni_w_epel_v4_8_msa;
        c->put_hevc_epel_uni_w[2][1][0] = ff_hevc_put_hevc_uni_w_epel_v6_8_msa;
        c->put_hevc_epel_uni_w[3][1][0] = ff_hevc_put_hevc_uni_w_epel_v8_8_msa;
        c->put_hevc_epel_uni_w[4][1][0] = ff_hevc_put_hevc_uni_w_epel_v12_8_msa;
        c->put_hevc_epel_uni_w[5][1][0] = ff_hevc_put_hevc_uni_w_epel_v16_8_msa;
        c->put_hevc_epel_uni_w[6][1][0] = ff_hevc_put_hevc_uni_w_epel_v24_8_msa;
        c->put_hevc_epel_uni_w[7][1][0] = ff_hevc_put_hevc_uni_w_epel_v32_8_msa;

        c->put_hevc_epel_uni_w[1][1][1] = ff_hevc_put_hevc_uni_w_epel_hv4_8_msa;
        c->put_hevc_epel_uni_w[2][1][1] = ff_hevc_put_hevc_uni_w_epel_hv6_8_msa;
        c->put_hevc_epel_uni_w[3][1][1] = ff_hevc_put_hevc_uni_w_epel_hv8_8_msa;
        c->put_hevc_epel_uni_w[4][1][1] =
            ff_hevc_put_hevc_uni_w_epel_hv12_8_msa;
        c->put_hevc_epel_uni_w[5][1][1] =
            ff_hevc_put_hevc_uni_w_epel_hv16_8_msa;
        c->put_hevc_epel_uni_w[6][1][1] =
            ff_hevc_put_hevc_uni_w_epel_hv24_8_msa;
        c->put_hevc_epel_uni_w[7][1][1] =
            ff_hevc_put_hevc_uni_w_epel_hv32_8_msa;

        c->put_hevc_qpel_bi[1][0][0] = ff_hevc_put_hevc_bi_pel_pixels4_8_msa;
        c->put_hevc_qpel_bi[3][0][0] = ff_hevc_put_hevc_bi_pel_pixels8_8_msa;
        c->put_hevc_qpel_bi[4][0][0] = ff_hevc_put_hevc_bi_pel_pixels12_8_msa;
        c->put_hevc_qpel_bi[5][0][0] = ff_hevc_put_hevc_bi_pel_pixels16_8_msa;
        c->put_hevc_qpel_bi[6][0][0] = ff_hevc_put_hevc_bi_pel_pixels24_8_msa;
        c->put_hevc_qpel_bi[7][0][0] = ff_hevc_put_hevc_bi_pel_pixels32_8_msa;
        c->put_hevc_qpel_bi[8][0][0] = ff_hevc_put_hevc_bi_pel_pixels48_8_msa;
        c->put_hevc_qpel_bi[9][0][0] = ff_hevc_put_hevc_bi_pel_pixels64_8_msa;

        c->put_hevc_qpel_bi[1][0][1] = ff_hevc_put_hevc_bi_qpel_h4_8_msa;
        c->put_hevc_qpel_bi[3][0][1] = ff_hevc_put_hevc_bi_qpel_h8_8_msa;
        c->put_hevc_qpel_bi[4][0][1] = ff_hevc_put_hevc_bi_qpel_h12_8_msa;
        c->put_hevc_qpel_bi[5][0][1] = ff_hevc_put_hevc_bi_qpel_h16_8_msa;
        c->put_hevc_qpel_bi[6][0][1] = ff_hevc_put_hevc_bi_qpel_h24_8_msa;
        c->put_hevc_qpel_bi[7][0][1] = ff_hevc_put_hevc_bi_qpel_h32_8_msa;
        c->put_hevc_qpel_bi[8][0][1] = ff_hevc_put_hevc_bi_qpel_h48_8_msa;
        c->put_hevc_qpel_bi[9][0][1] = ff_hevc_put_hevc_bi_qpel_h64_8_msa;

        c->put_hevc_qpel_bi[1][1][0] = ff_hevc_put_hevc_bi_qpel_v4_8_msa;
        c->put_hevc_qpel_bi[3][1][0] = ff_hevc_put_hevc_bi_qpel_v8_8_msa;
        c->put_hevc_qpel_bi[4][1][0] = ff_hevc_put_hevc_bi_qpel_v12_8_msa;
        c->put_hevc_qpel_bi[5][1][0] = ff_hevc_put_hevc_bi_qpel_v16_8_msa;
        c->put_hevc_qpel_bi[6][1][0] = ff_hevc_put_hevc_bi_qpel_v24_8_msa;
        c->put_hevc_qpel_bi[7][1][0] = ff_hevc_put_hevc_bi_qpel_v32_8_msa;
        c->put_hevc_qpel_bi[8][1][0] = ff_hevc_put_hevc_bi_qpel_v48_8_msa;
        c->put_hevc_qpel_bi[9][1][0] = ff_hevc_put_hevc_bi_qpel_v64_8_msa;

        c->put_hevc_qpel_bi[1][1][1] = ff_hevc_put_hevc_bi_qpel_hv4_8_msa;
        c->put_hevc_qpel_bi[3][1][1] = ff_hevc_put_hevc_bi_qpel_hv8_8_msa;
        c->put_hevc_qpel_bi[4][1][1] = ff_hevc_put_hevc_bi_qpel_hv12_8_msa;
        c->put_hevc_qpel_bi[5][1][1] = ff_hevc_put_hevc_bi_qpel_hv16_8_msa;
        c->put_hevc_qpel_bi[6][1][1] = ff_hevc_put_hevc_bi_qpel_hv24_8_msa;
        c->put_hevc_qpel_bi[7][1][1] = ff_hevc_put_hevc_bi_qpel_hv32_8_msa;
        c->put_hevc_qpel_bi[8][1][1] = ff_hevc_put_hevc_bi_qpel_hv48_8_msa;
        c->put_hevc_qpel_bi[9][1][1] = ff_hevc_put_hevc_bi_qpel_hv64_8_msa;

        c->put_hevc_epel_bi[1][0][0] = ff_hevc_put_hevc_bi_pel_pixels4_8_msa;
        c->put_hevc_epel_bi[2][0][0] = ff_hevc_put_hevc_bi_pel_pixels6_8_msa;
        c->put_hevc_epel_bi[3][0][0] = ff_hevc_put_hevc_bi_pel_pixels8_8_msa;
        c->put_hevc_epel_bi[4][0][0] = ff_hevc_put_hevc_bi_pel_pixels12_8_msa;
        c->put_hevc_epel_bi[5][0][0] = ff_hevc_put_hevc_bi_pel_pixels16_8_msa;
        c->put_hevc_epel_bi[6][0][0] = ff_hevc_put_hevc_bi_pel_pixels24_8_msa;
        c->put_hevc_epel_bi[7][0][0] = ff_hevc_put_hevc_bi_pel_pixels32_8_msa;

        c->put_hevc_epel_bi[1][0][1] = ff_hevc_put_hevc_bi_epel_h4_8_msa;
        c->put_hevc_epel_bi[2][0][1] = ff_hevc_put_hevc_bi_epel_h6_8_msa;
        c->put_hevc_epel_bi[3][0][1] = ff_hevc_put_hevc_bi_epel_h8_8_msa;
        c->put_hevc_epel_bi[4][0][1] = ff_hevc_put_hevc_bi_epel_h12_8_msa;
        c->put_hevc_epel_bi[5][0][1] = ff_hevc_put_hevc_bi_epel_h16_8_msa;
        c->put_hevc_epel_bi[6][0][1] = ff_hevc_put_hevc_bi_epel_h24_8_msa;
        c->put_hevc_epel_bi[7][0][1] = ff_hevc_put_hevc_bi_epel_h32_8_msa;

        c->put_hevc_epel_bi[1][1][0] = ff_hevc_put_hevc_bi_epel_v4_8_msa;
        c->put_hevc_epel_bi[2][1][0] = ff_hevc_put_hevc_bi_epel_v6_8_msa;
        c->put_hevc_epel_bi[3][1][0] = ff_hevc_put_hevc_bi_epel_v8_8_msa;
        c->put_hevc_epel_bi[4][1][0] = ff_hevc_put_hevc_bi_epel_v12_8_msa;
        c->put_hevc_epel_bi[5][1][0] = ff_hevc_put_hevc_bi_epel_v16_8_msa;
        c->put_hevc_epel_bi[6][1][0] = ff_hevc_put_hevc_bi_epel_v24_8_msa;
        c->put_hevc_epel_bi[7][1][0] = ff_hevc_put_hevc_bi_epel_v32_8_msa;

        c->put_hevc_epel_bi[1][1][1] = ff_hevc_put_hevc_bi_epel_hv4_8_msa;
        c->put_hevc_epel_bi[2][1][1] = ff_hevc_put_hevc_bi_epel_hv6_8_msa;
        c->put_hevc_epel_bi[3][1][1] = ff_hevc_put_hevc_bi_epel_hv8_8_msa;
        c->put_hevc_epel_bi[4][1][1] = ff_hevc_put_hevc_bi_epel_hv12_8_msa;
        c->put_hevc_epel_bi[5][1][1] = ff_hevc_put_hevc_bi_epel_hv16_8_msa;
        c->put_hevc_epel_bi[6][1][1] = ff_hevc_put_hevc_bi_epel_hv24_8_msa;
        c->put_hevc_epel_bi[7][1][1] = ff_hevc_put_hevc_bi_epel_hv32_8_msa;

        c->put_hevc_qpel_bi_w[1][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels4_8_msa;
        c->put_hevc_qpel_bi_w[3][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels8_8_msa;
        c->put_hevc_qpel_bi_w[4][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels12_8_msa;
        c->put_hevc_qpel_bi_w[5][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels16_8_msa;
        c->put_hevc_qpel_bi_w[6][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels24_8_msa;
        c->put_hevc_qpel_bi_w[7][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels32_8_msa;
        c->put_hevc_qpel_bi_w[8][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels48_8_msa;
        c->put_hevc_qpel_bi_w[9][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels64_8_msa;

        c->put_hevc_qpel_bi_w[1][0][1] = ff_hevc_put_hevc_bi_w_qpel_h4_8_msa;
        c->put_hevc_qpel_bi_w[3][0][1] = ff_hevc_put_hevc_bi_w_qpel_h8_8_msa;
        c->put_hevc_qpel_bi_w[4][0][1] = ff_hevc_put_hevc_bi_w_qpel_h12_8_msa;
        c->put_hevc_qpel_bi_w[5][0][1] = ff_hevc_put_hevc_bi_w_qpel_h16_8_msa;
        c->put_hevc_qpel_bi_w[6][0][1] = ff_hevc_put_hevc_bi_w_qpel_h24_8_msa;
        c->put_hevc_qpel_bi_w[7][0][1] = ff_hevc_put_hevc_bi_w_qpel_h32_8_msa;
        c->put_hevc_qpel_bi_w[8][0][1] = ff_hevc_put_hevc_bi_w_qpel_h48_8_msa;
        c->put_hevc_qpel_bi_w[9][0][1] = ff_hevc_put_hevc_bi_w_qpel_h64_8_msa;

        c->put_hevc_qpel_bi_w[1][1][0] = ff_hevc_put_hevc_bi_w_qpel_v4_8_msa;
        c->put_hevc_qpel_bi_w[3][1][0] = ff_hevc_put_hevc_bi_w_qpel_v8_8_msa;
        c->put_hevc_qpel_bi_w[4][1][0] = ff_hevc_put_hevc_bi_w_qpel_v12_8_msa;
        c->put_hevc_qpel_bi_w[5][1][0] = ff_hevc_put_hevc_bi_w_qpel_v16_8_msa;
        c->put_hevc_qpel_bi_w[6][1][0] = ff_hevc_put_hevc_bi_w_qpel_v24_8_msa;
        c->put_hevc_qpel_bi_w[7][1][0] = ff_hevc_put_hevc_bi_w_qpel_v32_8_msa;
        c->put_hevc_qpel_bi_w[8][1][0] = ff_hevc_put_hevc_bi_w_qpel_v48_8_msa;
        c->put_hevc_qpel_bi_w[9][1][0] = ff_hevc_put_hevc_bi_w_qpel_v64_8_msa;

        c->put_hevc_qpel_bi_w[1][1][1] = ff_hevc_put_hevc_bi_w_qpel_hv4_8_msa;
        c->put_hevc_qpel_bi_w[3][1][1] = ff_hevc_put_hevc_bi_w_qpel_hv8_8_msa;
        c->put_hevc_qpel_bi_w[4][1][1] = ff_hevc_put_hevc_bi_w_qpel_hv12_8_msa;
        c->put_hevc_qpel_bi_w[5][1][1] = ff_hevc_put_hevc_bi_w_qpel_hv16_8_msa;
        c->put_hevc_qpel_bi_w[6][1][1] = ff_hevc_put_hevc_bi_w_qpel_hv24_8_msa;
        c->put_hevc_qpel_bi_w[7][1][1] = ff_hevc_put_hevc_bi_w_qpel_hv32_8_msa;
        c->put_hevc_qpel_bi_w[8][1][1] = ff_hevc_put_hevc_bi_w_qpel_hv48_8_msa;
        c->put_hevc_qpel_bi_w[9][1][1] = ff_hevc_put_hevc_bi_w_qpel_hv64_8_msa;

        c->put_hevc_epel_bi_w[1][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels4_8_msa;
        c->put_hevc_epel_bi_w[2][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels6_8_msa;
        c->put_hevc_epel_bi_w[3][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels8_8_msa;
        c->put_hevc_epel_bi_w[4][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels12_8_msa;
        c->put_hevc_epel_bi_w[5][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels16_8_msa;
        c->put_hevc_epel_bi_w[6][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels24_8_msa;
        c->put_hevc_epel_bi_w[7][0][0] =
            ff_hevc_put_hevc_bi_w_pel_pixels32_8_msa;

        c->put_hevc_epel_bi_w[1][0][1] = ff_hevc_put_hevc_bi_w_epel_h4_8_msa;
        c->put_hevc_epel_bi_w[2][0][1] = ff_hevc_put_hevc_bi_w_epel_h6_8_msa;
        c->put_hevc_epel_bi_w[3][0][1] = ff_hevc_put_hevc_bi_w_epel_h8_8_msa;
        c->put_hevc_epel_bi_w[4][0][1] = ff_hevc_put_hevc_bi_w_epel_h12_8_msa;
        c->put_hevc_epel_bi_w[5][0][1] = ff_hevc_put_hevc_bi_w_epel_h16_8_msa;
        c->put_hevc_epel_bi_w[6][0][1] = ff_hevc_put_hevc_bi_w_epel_h24_8_msa;
        c->put_hevc_epel_bi_w[7][0][1] = ff_hevc_put_hevc_bi_w_epel_h32_8_msa;

        c->put_hevc_epel_bi_w[1][1][0] = ff_hevc_put_hevc_bi_w_epel_v4_8_msa;
        c->put_hevc_epel_bi_w[2][1][0] = ff_hevc_put_hevc_bi_w_epel_v6_8_msa;
        c->put_hevc_epel_bi_w[3][1][0] = ff_hevc_put_hevc_bi_w_epel_v8_8_msa;
        c->put_hevc_epel_bi_w[4][1][0] = ff_hevc_put_hevc_bi_w_epel_v12_8_msa;
        c->put_hevc_epel_bi_w[5][1][0] = ff_hevc_put_hevc_bi_w_epel_v16_8_msa;
        c->put_hevc_epel_bi_w[6][1][0] = ff_hevc_put_hevc_bi_w_epel_v24_8_msa;
        c->put_hevc_epel_bi_w[7][1][0] = ff_hevc_put_hevc_bi_w_epel_v32_8_msa;

        c->put_hevc_epel_bi_w[1][1][1] = ff_hevc_put_hevc_bi_w_epel_hv4_8_msa;
        c->put_hevc_epel_bi_w[2][1][1] = ff_hevc_put_hevc_bi_w_epel_hv6_8_msa;
        c->put_hevc_epel_bi_w[3][1][1] = ff_hevc_put_hevc_bi_w_epel_hv8_8_msa;
        c->put_hevc_epel_bi_w[4][1][1] = ff_hevc_put_hevc_bi_w_epel_hv12_8_msa;
        c->put_hevc_epel_bi_w[5][1][1] = ff_hevc_put_hevc_bi_w_epel_hv16_8_msa;
        c->put_hevc_epel_bi_w[6][1][1] = ff_hevc_put_hevc_bi_w_epel_hv24_8_msa;
        c->put_hevc_epel_bi_w[7][1][1] = ff_hevc_put_hevc_bi_w_epel_hv32_8_msa;

        c->sao_band_filter[0] =
        c->sao_band_filter[1] =
        c->sao_band_filter[2] =
        c->sao_band_filter[3] =
        c->sao_band_filter[4] = ff_hevc_sao_band_filter_0_8_msa;

        c->sao_edge_filter[0] =
        c->sao_edge_filter[1] =
        c->sao_edge_filter[2] =
        c->sao_edge_filter[3] =
        c->sao_edge_filter[4] = ff_hevc_sao_edge_filter_8_msa;

        c->hevc_h_loop_filter_luma = ff_hevc_loop_filter_luma_h_8_msa;
        c->hevc_v_loop_filter_luma = ff_hevc_loop_filter_luma_v_8_msa;

        c->hevc_h_loop_filter_chroma = ff_hevc_loop_filter_chroma_h_8_msa;
        c->hevc_v_loop_filter_chroma = ff_hevc_loop_filter_chroma_v_8_msa;

        c->hevc_h_loop_filter_luma_c = ff_hevc_loop_filter_luma_h_8_msa;
        c->hevc_v_loop_filter_luma_c = ff_hevc_loop_filter_luma_v_8_msa;

        c->hevc_h_loop_filter_chroma_c =
            ff_hevc_loop_filter_chroma_h_8_msa;
        c->hevc_v_loop_filter_chroma_c =
            ff_hevc_loop_filter_chroma_v_8_msa;

        c->idct[0] = ff_hevc_idct_4x4_msa;
        c->idct[1] = ff_hevc_idct_8x8_msa;
        c->idct[2] = ff_hevc_idct_16x16_msa;
        c->idct[3] = ff_hevc_idct_32x32_msa;
        c->idct_dc[0] = ff_hevc_idct_dc_4x4_msa;
        c->idct_dc[1] = ff_hevc_idct_dc_8x8_msa;
        c->idct_dc[2] = ff_hevc_idct_dc_16x16_msa;
        c->idct_dc[3] = ff_hevc_idct_dc_32x32_msa;
        c->add_residual[0] = ff_hevc_addblk_4x4_msa;
        c->add_residual[1] = ff_hevc_addblk_8x8_msa;
        c->add_residual[2] = ff_hevc_addblk_16x16_msa;
        c->add_residual[3] = ff_hevc_addblk_32x32_msa;
        c->transform_4x4_luma = ff_hevc_idct_luma_4x4_msa;
    }
}
#endif  // #if HAVE_MSA

void ff_hevc_dsp_init_mips(HEVCDSPContext *c, const int bit_depth)
{
#if HAVE_MMI
    hevc_dsp_init_mmi(c, bit_depth);
#endif  // #if HAVE_MMI
#if HAVE_MSA
    hevc_dsp_init_msa(c, bit_depth);
#endif  // #if HAVE_MSA
}
