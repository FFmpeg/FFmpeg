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
    }
}
#endif  // #if HAVE_MSA

void ff_hevc_dsp_init_mips(HEVCDSPContext *c, const int bit_depth)
{
#if HAVE_MSA
    hevc_dsp_init_msa(c, bit_depth);
#endif  // #if HAVE_MSA
}
