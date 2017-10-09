/*
 * Copyright (c) 2015 - 2017 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

#ifndef AVCODEC_MIPS_HEVC_MACROS_MSA_H
#define AVCODEC_MIPS_HEVC_MACROS_MSA_H

#define HEVC_PCK_SW_SB2(in0, in1, out)                            \
{                                                                 \
    v8i16 tmp0_m;                                                 \
                                                                  \
    tmp0_m = __msa_pckev_h((v8i16) in0, (v8i16) in1);             \
    out = (v4i32) __msa_pckev_b((v16i8) tmp0_m, (v16i8) tmp0_m);  \
}

#define HEVC_PCK_SW_SB4(in0, in1, in2, in3, out)                  \
{                                                                 \
    v8i16 tmp0_m, tmp1_m;                                         \
                                                                  \
    PCKEV_H2_SH(in0, in1, in2, in3, tmp0_m, tmp1_m);              \
    out = (v4i32) __msa_pckev_b((v16i8) tmp1_m, (v16i8) tmp0_m);  \
}

#define HEVC_PCK_SW_SB8(in0, in1, in2, in3, in4, in5, in6, in7, out0, out1)  \
{                                                                            \
    v8i16 tmp0_m, tmp1_m, tmp2_m, tmp3_m;                                    \
                                                                             \
    PCKEV_H4_SH(in0, in1, in2, in3, in4, in5, in6, in7,                      \
                tmp0_m, tmp1_m, tmp2_m, tmp3_m);                             \
    PCKEV_B2_SW(tmp1_m, tmp0_m, tmp3_m, tmp2_m, out0, out1);                 \
}

#define HEVC_PCK_SW_SB12(in0, in1, in2, in3, in4, in5, in6, in7,   \
                         in8, in9, in10, in11, out0, out1, out2)   \
{                                                                  \
    v8i16 tmp0_m, tmp1_m, tmp2_m, tmp3_m, tmp4_m, tmp5_m;          \
                                                                   \
    PCKEV_H4_SH(in0, in1, in2, in3, in4, in5, in6, in7,            \
                tmp0_m, tmp1_m, tmp2_m, tmp3_m);                   \
    PCKEV_H2_SH(in8, in9, in10, in11, tmp4_m, tmp5_m);             \
    PCKEV_B2_SW(tmp1_m, tmp0_m, tmp3_m, tmp2_m, out0, out1);       \
    out2 = (v4i32) __msa_pckev_b((v16i8) tmp5_m, (v16i8) tmp4_m);  \
}

#define HEVC_FILT_8TAP_SH(in0, in1, in2, in3,                    \
                          filt0, filt1, filt2, filt3)            \
( {                                                              \
    v8i16 out_m;                                                 \
                                                                 \
    out_m = __msa_dotp_s_h((v16i8) in0, (v16i8) filt0);          \
    out_m = __msa_dpadd_s_h(out_m, (v16i8) in1, (v16i8) filt1);  \
    DPADD_SB2_SH(in2, in3, filt2, filt3, out_m, out_m);          \
    out_m;                                                       \
} )

#define HEVC_FILT_8TAP(in0, in1, in2, in3,                       \
                       filt0, filt1, filt2, filt3)               \
( {                                                              \
    v4i32 out_m;                                                 \
                                                                 \
    out_m = __msa_dotp_s_w((v8i16) in0, (v8i16) filt0);          \
    out_m = __msa_dpadd_s_w(out_m, (v8i16) in1, (v8i16) filt1);  \
    DPADD_SH2_SW(in2, in3, filt2, filt3, out_m, out_m);          \
    out_m;                                                       \
} )

#define HEVC_FILT_4TAP(in0, in1, filt0, filt1)           \
( {                                                      \
    v4i32 out_m;                                         \
                                                         \
    out_m = __msa_dotp_s_w(in0, (v8i16) filt0);          \
    out_m = __msa_dpadd_s_w(out_m, in1, (v8i16) filt1);  \
    out_m;                                               \
} )

#endif  /* AVCODEC_MIPS_HEVC_MACROS_MSA_H */
