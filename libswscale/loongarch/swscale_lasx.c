/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 * Contributed by Hao Chen(chenhao@loongson.cn)
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

#include "swscale_loongarch.h"
#include "libavutil/loongarch/loongson_intrinsics.h"
#include "libavutil/intreadwrite.h"

#define SCALE_8_16(_sh)                                               \
{                                                                     \
    src0    = __lasx_xvldrepl_d(src + filterPos[0], 0);               \
    src1    = __lasx_xvldrepl_d(src + filterPos[1], 0);               \
    src2    = __lasx_xvldrepl_d(src + filterPos[2], 0);               \
    src3    = __lasx_xvldrepl_d(src + filterPos[3], 0);               \
    src4    = __lasx_xvldrepl_d(src + filterPos[4], 0);               \
    src5    = __lasx_xvldrepl_d(src + filterPos[5], 0);               \
    src6    = __lasx_xvldrepl_d(src + filterPos[6], 0);               \
    src7    = __lasx_xvldrepl_d(src + filterPos[7], 0);               \
    src8    = __lasx_xvldrepl_d(src + filterPos[8], 0);               \
    src9    = __lasx_xvldrepl_d(src + filterPos[9], 0);               \
    src10   = __lasx_xvldrepl_d(src + filterPos[10], 0);              \
    src11   = __lasx_xvldrepl_d(src + filterPos[11], 0);              \
    src12   = __lasx_xvldrepl_d(src + filterPos[12], 0);              \
    src13   = __lasx_xvldrepl_d(src + filterPos[13], 0);              \
    src14   = __lasx_xvldrepl_d(src + filterPos[14], 0);              \
    src15   = __lasx_xvldrepl_d(src + filterPos[15], 0);              \
    DUP4_ARG2(__lasx_xvld, filter, 0, filter, 32, filter, 64,         \
              filter, 96, filter0, filter1, filter2, filter3);        \
    DUP4_ARG2(__lasx_xvld, filter, 128, filter, 160,                  \
              filter, 192, filter, 224, filter4,                      \
              filter5, filter6, filter7);                             \
    DUP4_ARG2(__lasx_xvilvl_d, src1, src0, src3, src2,                \
              src5, src4, src7, src6, src0, src2, src4, src6);        \
    DUP4_ARG2(__lasx_xvilvl_d, src9, src8, src11, src10,              \
              src13, src12, src15, src14, src8, src10, src12, src14); \
    DUP4_ARG1(__lasx_vext2xv_hu_bu, src0, src2, src4, src6,           \
              src0, src2, src4, src6);                                \
    DUP4_ARG1(__lasx_vext2xv_hu_bu, src8, src10, src12,               \
              src14, src8, src10, src12, src14);                      \
    DUP4_ARG2(__lasx_xvdp2_w_h, filter0, src0, filter1, src2,         \
              filter2, src4, filter3, src6, src0, src1, src2, src3);  \
    DUP4_ARG2(__lasx_xvdp2_w_h, filter4, src8, filter5, src10,        \
              filter6, src12, filter7, src14, src4, src5, src6, src7);\
    src0 = __lasx_xvhaddw_d_w(src0, src0);                            \
    src1 = __lasx_xvhaddw_d_w(src1, src1);                            \
    src2 = __lasx_xvhaddw_d_w(src2, src2);                            \
    src3 = __lasx_xvhaddw_d_w(src3, src3);                            \
    src4 = __lasx_xvhaddw_d_w(src4, src4);                            \
    src5 = __lasx_xvhaddw_d_w(src5, src5);                            \
    src6 = __lasx_xvhaddw_d_w(src6, src6);                            \
    src7 = __lasx_xvhaddw_d_w(src7, src7);                            \
    DUP4_ARG2(__lasx_xvpickev_w, src1, src0, src3, src2,              \
              src5, src4, src7, src6, src0, src1, src2, src3);        \
    src0 = __lasx_xvhaddw_d_w(src0, src0);                            \
    src1 = __lasx_xvhaddw_d_w(src1, src1);                            \
    src2 = __lasx_xvhaddw_d_w(src2, src2);                            \
    src3 = __lasx_xvhaddw_d_w(src3, src3);                            \
    src0 = __lasx_xvpickev_w(src1, src0);                             \
    src1 = __lasx_xvpickev_w(src3, src2);                             \
    src0 = __lasx_xvsrai_w(src0, _sh);                                \
    src1 = __lasx_xvsrai_w(src1, _sh);                                \
    src0 = __lasx_xvmin_w(src0, vmax);                                \
    src1 = __lasx_xvmin_w(src1, vmax);                                \
    src0 = __lasx_xvperm_w(src0, shuf);                               \
    src1 = __lasx_xvperm_w(src1, shuf);                               \
    src0 = __lasx_xvpickev_h(src1, src0);                             \
    src0 = __lasx_xvpermi_d(src0, 0xd8);                              \
    __lasx_xvst(src0, dst, 0);                                        \
    filterPos += 16;                                                  \
    filter    += 128;                                                 \
    dst       += 16;                                                  \
}

#define SCALE_8_8(_sh)                                                \
{                                                                     \
    src0    = __lasx_xvldrepl_d(src + filterPos[0], 0);               \
    src1    = __lasx_xvldrepl_d(src + filterPos[1], 0);               \
    src2    = __lasx_xvldrepl_d(src + filterPos[2], 0);               \
    src3    = __lasx_xvldrepl_d(src + filterPos[3], 0);               \
    src4    = __lasx_xvldrepl_d(src + filterPos[4], 0);               \
    src5    = __lasx_xvldrepl_d(src + filterPos[5], 0);               \
    src6    = __lasx_xvldrepl_d(src + filterPos[6], 0);               \
    src7    = __lasx_xvldrepl_d(src + filterPos[7], 0);               \
    DUP4_ARG2(__lasx_xvld, filter, 0, filter, 32, filter, 64,         \
              filter, 96, filter0, filter1, filter2, filter3);        \
    filterPos += 8;                                                   \
    filter    += 64;                                                  \
    DUP4_ARG2(__lasx_xvilvl_d, src1, src0, src3, src2,                \
              src5, src4, src7, src6, src0, src2, src4, src6);        \
    DUP4_ARG1(__lasx_vext2xv_hu_bu, src0, src2, src4, src6,           \
              src0, src2, src4, src6);                                \
    DUP4_ARG2(__lasx_xvdp2_w_h, filter0, src0, filter1, src2,         \
              filter2, src4, filter3, src6, src0, src1, src2,src3);   \
    src0 = __lasx_xvhaddw_d_w(src0, src0);                            \
    src1 = __lasx_xvhaddw_d_w(src1, src1);                            \
    src2 = __lasx_xvhaddw_d_w(src2, src2);                            \
    src3 = __lasx_xvhaddw_d_w(src3, src3);                            \
    src0 = __lasx_xvpickev_w(src1, src0);                             \
    src1 = __lasx_xvpickev_w(src3, src2);                             \
    src0 = __lasx_xvhaddw_d_w(src0, src0);                            \
    src1 = __lasx_xvhaddw_d_w(src1, src1);                            \
    src0 = __lasx_xvpickev_w(src1, src0);                             \
    src0 = __lasx_xvsrai_w(src0, _sh);                                \
    src0 = __lasx_xvmin_w(src0, vmax);                                \
    src0 = __lasx_xvperm_w(src0, shuf);                               \
}

#define SCALE_8_4(_sh)                                                    \
{                                                                         \
    src0    = __lasx_xvldrepl_d(src + filterPos[0], 0);                   \
    src1    = __lasx_xvldrepl_d(src + filterPos[1], 0);                   \
    src2    = __lasx_xvldrepl_d(src + filterPos[2], 0);                   \
    src3    = __lasx_xvldrepl_d(src + filterPos[3], 0);                   \
    filter0 = __lasx_xvld(filter, 0);                                     \
    filter1 = __lasx_xvld(filter, 32);                                    \
    filterPos += 4;                                                       \
    filter    += 32;                                                      \
    src0    = __lasx_xvilvl_d(src1, src0);                                \
    src2    = __lasx_xvilvl_d(src3, src2);                                \
    src0    = __lasx_vext2xv_hu_bu(src0);                                 \
    src2    = __lasx_vext2xv_hu_bu(src2);                                 \
    src0    = __lasx_xvdp2_w_h(src0, filter0);                            \
    src1    = __lasx_xvdp2_w_h(src2, filter1);                            \
    src0 = __lasx_xvhaddw_d_w(src0, src0);                                \
    src1 = __lasx_xvhaddw_d_w(src1, src1);                                \
    src0 = __lasx_xvpickev_w(src1, src0);                                 \
    src0 = __lasx_xvhaddw_d_w(src0, src0);                                \
    src0 = __lasx_xvpickev_w(src0, src0);                                 \
    src0 = __lasx_xvsrai_w(src0, _sh);                                    \
    src0 = __lasx_xvmin_w(src0, vmax);                                    \
    src0 = __lasx_xvperm_w(src0, shuf);                                   \
}

#define SCALE_8_2(_sh)                                                \
{                                                                     \
    src0    = __lasx_xvldrepl_d(src + filterPos[0], 0);               \
    src1    = __lasx_xvldrepl_d(src + filterPos[1], 0);               \
    filter0 = __lasx_xvld(filter, 0);                                 \
    src0 = __lasx_xvilvl_d(src1, src0);                               \
    src0 = __lasx_vext2xv_hu_bu(src0);                                \
    src0 = __lasx_xvdp2_w_h(filter0, src0);                           \
    src0 = __lasx_xvhaddw_d_w(src0, src0);                            \
    src0 = __lasx_xvhaddw_q_d(src0, src0);                            \
    src0 = __lasx_xvsrai_w(src0, _sh);                                \
    src0 = __lasx_xvmin_w(src0, vmax);                                \
    dst[0] = __lasx_xvpickve2gr_w(src0, 0);                           \
    dst[1] = __lasx_xvpickve2gr_w(src0, 4);                           \
    filterPos += 2;                                                   \
    filter    += 16;                                                  \
    dst       += 2;                                                   \
}

#define SCALE_4_16(_sh)                                               \
{                                                                     \
    src0    = __lasx_xvldrepl_w(src + filterPos[0], 0);               \
    src1    = __lasx_xvldrepl_w(src + filterPos[1], 0);               \
    src2    = __lasx_xvldrepl_w(src + filterPos[2], 0);               \
    src3    = __lasx_xvldrepl_w(src + filterPos[3], 0);               \
    src4    = __lasx_xvldrepl_w(src + filterPos[4], 0);               \
    src5    = __lasx_xvldrepl_w(src + filterPos[5], 0);               \
    src6    = __lasx_xvldrepl_w(src + filterPos[6], 0);               \
    src7    = __lasx_xvldrepl_w(src + filterPos[7], 0);               \
    src8    = __lasx_xvldrepl_w(src + filterPos[8], 0);               \
    src9    = __lasx_xvldrepl_w(src + filterPos[9], 0);               \
    src10   = __lasx_xvldrepl_w(src + filterPos[10], 0);              \
    src11   = __lasx_xvldrepl_w(src + filterPos[11], 0);              \
    src12   = __lasx_xvldrepl_w(src + filterPos[12], 0);              \
    src13   = __lasx_xvldrepl_w(src + filterPos[13], 0);              \
    src14   = __lasx_xvldrepl_w(src + filterPos[14], 0);              \
    src15   = __lasx_xvldrepl_w(src + filterPos[15], 0);              \
    DUP4_ARG2(__lasx_xvld, filter, 0, filter, 32, filter, 64,         \
              filter, 96, filter0, filter1, filter2, filter3);        \
    DUP4_ARG2(__lasx_xvilvl_w, src1, src0, src3, src2, src5,          \
              src4, src7, src6, src0, src2, src4, src6);              \
    DUP4_ARG2(__lasx_xvilvl_w, src9, src8, src11, src10, src13,       \
              src12, src15, src14, src8, src10, src12, src14);        \
    DUP4_ARG2(__lasx_xvilvl_d, src2, src0, src6, src4, src10,         \
                   src8, src14, src12, src0, src1, src2, src3);       \
    DUP4_ARG1(__lasx_vext2xv_hu_bu, src0, src1, src2, src3,           \
              src0, src1, src2, src3);                                \
    DUP4_ARG2(__lasx_xvdp2_w_h, filter0, src0, filter1, src1,         \
              filter2, src2, filter3, src3, src0, src1, src2, src3);  \
    src0 = __lasx_xvhaddw_d_w(src0, src0);                            \
    src1 = __lasx_xvhaddw_d_w(src1, src1);                            \
    src2 = __lasx_xvhaddw_d_w(src2, src2);                            \
    src3 = __lasx_xvhaddw_d_w(src3, src3);                            \
    src0 = __lasx_xvpickev_w(src1, src0);                             \
    src1 = __lasx_xvpickev_w(src3, src2);                             \
    src0 = __lasx_xvsrai_w(src0, _sh);                                \
    src1 = __lasx_xvsrai_w(src1, _sh);                                \
    src0 = __lasx_xvmin_w(src0, vmax);                                \
    src1 = __lasx_xvmin_w(src1, vmax);                                \
    src0 = __lasx_xvpickev_h(src1, src0);                             \
    src0 = __lasx_xvperm_w(src0, shuf);                               \
    __lasx_xvst(src0, dst, 0);                                        \
    filterPos += 16;                                                  \
    filter    += 64;                                                  \
    dst       += 16;                                                  \
}

#define SCALE_4_8(_sh)                                                    \
{                                                                         \
    src0    = __lasx_xvldrepl_w(src + filterPos[0], 0);                   \
    src1    = __lasx_xvldrepl_w(src + filterPos[1], 0);                   \
    src2    = __lasx_xvldrepl_w(src + filterPos[2], 0);                   \
    src3    = __lasx_xvldrepl_w(src + filterPos[3], 0);                   \
    src4    = __lasx_xvldrepl_w(src + filterPos[4], 0);                   \
    src5    = __lasx_xvldrepl_w(src + filterPos[5], 0);                   \
    src6    = __lasx_xvldrepl_w(src + filterPos[6], 0);                   \
    src7    = __lasx_xvldrepl_w(src + filterPos[7], 0);                   \
    filter0 = __lasx_xvld(filter, 0);                                     \
    filter1 = __lasx_xvld(filter, 32);                                    \
    filterPos += 8;                                                       \
    filter    += 32;                                                      \
    DUP4_ARG2(__lasx_xvilvl_w, src1, src0, src3, src2, src5,              \
              src4, src7, src6, src0, src2, src4, src6);                  \
    src0 = __lasx_xvilvl_d(src2, src0);                                   \
    src1 = __lasx_xvilvl_d(src6, src4);                                   \
                                                                          \
    src0 = __lasx_vext2xv_hu_bu(src0);                                    \
    src1 = __lasx_vext2xv_hu_bu(src1);                                    \
    src0 = __lasx_xvdp2_w_h(filter0, src0);                               \
    src1 = __lasx_xvdp2_w_h(filter1, src1);                               \
    src0 = __lasx_xvhaddw_d_w(src0, src0);                                \
    src1 = __lasx_xvhaddw_d_w(src1, src1);                                \
    src0 = __lasx_xvpickev_w(src1, src0);                                 \
    src0 = __lasx_xvsrai_w(src0, _sh);                                    \
    src0 = __lasx_xvmin_w(src0, vmax);                                    \
}

#define SCALE_4_4(_sh)                                                \
{                                                                     \
    src0    = __lasx_xvldrepl_w(src + filterPos[0], 0);               \
    src1    = __lasx_xvldrepl_w(src + filterPos[1], 0);               \
    src2    = __lasx_xvldrepl_w(src + filterPos[2], 0);               \
    src3    = __lasx_xvldrepl_w(src + filterPos[3], 0);               \
    filter0 = __lasx_xvld(filter, 0);                                 \
    filterPos += 4;                                                   \
    filter    += 16;                                                  \
    src0    = __lasx_xvilvl_w(src1, src0);                            \
    src1    = __lasx_xvilvl_w(src3, src2);                            \
                                                                      \
    src0 = __lasx_xvilvl_d(src1, src0);                               \
    src0 = __lasx_vext2xv_hu_bu(src0);                                \
    src0 = __lasx_xvdp2_w_h(filter0, src0);                           \
    src0 = __lasx_xvhaddw_d_w(src0, src0);                            \
    src0 = __lasx_xvsrai_w(src0, _sh);                                \
    src0 = __lasx_xvmin_w(src0, vmax);                                \
    src0 = __lasx_xvpickev_w(src0, src0);                             \
    src0 = __lasx_xvpermi_d(src0, 0xd8);                              \
}

#define SCALE_4_2(_sh)                                                \
{                                                                     \
    src0    = __lasx_xvldrepl_w(src + filterPos[0], 0);               \
    src1    = __lasx_xvldrepl_w(src + filterPos[1], 0);               \
    filter0 = __lasx_xvld(filter, 0);                                 \
    src0 = __lasx_xvilvl_w(src1, src0);                               \
    src0 = __lasx_vext2xv_hu_bu(src0);                                \
    src0 = __lasx_xvdp2_w_h(filter0, src0);                           \
    src0 = __lasx_xvhaddw_d_w(src0, src0);                            \
    src0 = __lasx_xvsrai_w(src0, _sh);                                \
    src0 = __lasx_xvmin_w(src0, vmax);                                \
    dst[0] = __lasx_xvpickve2gr_w(src0, 0);                           \
    dst[1] = __lasx_xvpickve2gr_w(src0, 2);                           \
    filterPos += 2;                                                   \
    filter    += 8;                                                   \
    dst       += 2;                                                   \
}

#define SCALE_16                                                      \
{                                                                     \
    int dex  = j << 1;                                                \
    src0     = __lasx_xvldrepl_d((srcPos1 + j), 0);                   \
    src1     = __lasx_xvldrepl_d((srcPos2 + j), 0);                   \
    src2     = __lasx_xvldrepl_d((srcPos3 + j), 0);                   \
    src3     = __lasx_xvldrepl_d((srcPos4 + j), 0);                   \
    DUP4_ARG2(__lasx_xvldx, filterStart1, dex, filterStart2, dex,     \
              filterStart3, dex, filterStart4, dex, filter0,          \
              filter1, filter2, filter3);                             \
    src0     = __lasx_xvpermi_q(src0, src1, 0x02);                    \
    src1     = __lasx_xvpermi_q(src2, src3, 0x02);                    \
    filter0  = __lasx_xvpermi_q(filter0, filter1, 0x02);              \
    filter1  = __lasx_xvpermi_q(filter2, filter3, 0x02);              \
    src0     = __lasx_xvilvl_b(zero, src0);                           \
    src1     = __lasx_xvilvl_b(zero, src1);                           \
    out0     = __lasx_xvdp2_w_h(filter0, src0);                       \
    out1     = __lasx_xvdp2_w_h(filter1, src1);                       \
    src0     = __lasx_xvhaddw_d_w(out0, out0);                        \
    src1     = __lasx_xvhaddw_d_w(out1, out1);                        \
    out0     = __lasx_xvpackev_d(src1, src0);                         \
    out1     = __lasx_xvpackod_d(src1, src0);                         \
    out0     = __lasx_xvadd_w(out0, out1);                            \
    out      = __lasx_xvadd_w(out, out0);                             \
}

void ff_hscale_8_to_15_lasx(SwsContext *c, int16_t *dst, int dstW,
                            const uint8_t *src, const int16_t *filter,
                            const int32_t *filterPos, int filterSize)
{
    int i;
    int max = (1 << 15) - 1;

    if (filterSize == 8) {
        __m256i src0, src1, src2, src3, src4, src5, src6, src7;
        __m256i src8, src9, src10, src11, src12, src13, src14, src15;
        __m256i filter0, filter1, filter2, filter3;
        __m256i filter4, filter5, filter6, filter7;
        __m256i vmax = __lasx_xvreplgr2vr_w(max);
        __m256i shuf = {0x0000000400000000, 0x0000000500000001,
                        0x0000000600000002, 0x0000000700000003};
        int len = dstW >> 4;
        int res = dstW & 15;
        while (len--) {
            SCALE_8_16(7);
        }
        if (res & 8) {
            SCALE_8_8(7);
            src0 = __lasx_xvpickev_h(src0, src0);
            __lasx_xvstelm_d(src0, dst, 0, 0);
            __lasx_xvstelm_d(src0, dst, 8, 2);
            dst       += 8;
        }
        if (res & 4) {
            SCALE_8_4(7);
            src0 = __lasx_xvpickev_h(src0, src0);
            __lasx_xvstelm_d(src0, dst, 0, 0);
            dst       += 4;
        }
        if (res & 2) {
            SCALE_8_2(7);
        }
        if (res & 1) {
            int val = 0;
            src0    = __lasx_xvldrepl_d(src + filterPos[0], 0);
            filter0 = __lasx_xvld(filter, 0);
            src0 = __lasx_vext2xv_hu_bu(src0);
            src0 = __lasx_xvdp2_w_h(filter0, src0);
            src0    = __lasx_xvhaddw_d_w(src0, src0);
            src0    = __lasx_xvhaddw_q_d(src0, src0);
            val     = __lasx_xvpickve2gr_w(src0, 0);
            dst[0]  = FFMIN(val >> 7, max);
        }
    } else if (filterSize == 4) {
        __m256i src0, src1, src2, src3, src4, src5, src6, src7;
        __m256i src8, src9, src10, src11, src12, src13, src14, src15;
        __m256i filter0, filter1, filter2, filter3;
        __m256i vmax = __lasx_xvreplgr2vr_w(max);
        __m256i shuf = {0x0000000400000000, 0x0000000500000001,
                        0x0000000600000002, 0x0000000700000003};
        int len = dstW >> 4;
        int res = dstW & 15;
        while (len--) {
            SCALE_4_16(7);
        }
        if (res & 8) {
            SCALE_4_8(7);
            src0 = __lasx_xvpickev_h(src1, src0);
            src0 = __lasx_xvperm_w(src0, shuf);
            __lasx_xvstelm_d(src0, dst, 0, 0);
            __lasx_xvstelm_d(src0, dst, 8, 1);
            dst       += 8;
        }
        if (res & 4) {
            SCALE_4_4(7);
            src0 = __lasx_xvpickev_h(src0, src0);
            __lasx_xvstelm_d(src0, dst, 0, 0);
            dst       += 4;
        }
        if (res & 2) {
            SCALE_4_2(7);
        }
        if (res & 1) {
            int val = 0;
            const uint8_t *srcPos = src + filterPos[0];

            for (int j = 0; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[0] = FFMIN(val >> 7, max);
        }
    } else if (filterSize > 8) {
        int filterlen = filterSize - 7;
        int len = dstW >> 2;
        int res = dstW & 3;
        __m256i zero = __lasx_xvldi(0);

        while (len--) {
            __m256i src0, src1, src2, src3;
            __m256i filter0, filter1, filter2, filter3, out0, out1;
            __m256i out = zero;
            const uint8_t *srcPos1 = src + filterPos[0];
            const uint8_t *srcPos2 = src + filterPos[1];
            const uint8_t *srcPos3 = src + filterPos[2];
            const uint8_t *srcPos4 = src + filterPos[3];
            const int16_t *filterStart1 = filter;
            const int16_t *filterStart2 = filterStart1 + filterSize;
            const int16_t *filterStart3 = filterStart2 + filterSize;
            const int16_t *filterStart4 = filterStart3 + filterSize;
            int j, val1 = 0, val2 = 0, val3 = 0, val4 = 0;

            for (j = 0; j < filterlen; j += 8) {
                SCALE_16
            }
            val1 = __lasx_xvpickve2gr_w(out, 0);
            val2 = __lasx_xvpickve2gr_w(out, 4);
            val3 = __lasx_xvpickve2gr_w(out, 2);
            val4 = __lasx_xvpickve2gr_w(out, 6);
            for (; j < filterSize; j++) {
                val1 += ((int)srcPos1[j]) * filterStart1[j];
                val2 += ((int)srcPos2[j]) * filterStart2[j];
                val3 += ((int)srcPos3[j]) * filterStart3[j];
                val4 += ((int)srcPos4[j]) * filterStart4[j];
            }
            dst[0] = FFMIN(val1 >> 7, max);
            dst[1] = FFMIN(val2 >> 7, max);
            dst[2] = FFMIN(val3 >> 7, max);
            dst[3] = FFMIN(val4 >> 7, max);
            dst       += 4;
            filterPos += 4;
            filter     = filterStart4 + filterSize;
        }
        for(i = 0; i < res; i++) {
            int j, val = 0;
            const uint8_t *srcPos = src + filterPos[i];
            __m256i src1, filter0, out0;

            for (j = 0; j < filterlen; j += 8) {
                src1   = __lasx_xvldrepl_d((srcPos + j), 0);
                filter0 = __lasx_xvld(filter + j, 0);
                src1 = __lasx_xvilvl_b(zero, src1);
                out0 = __lasx_xvdp2_w_h(filter0, src1);
                out0 = __lasx_xvhaddw_d_w(out0, out0);
                out0 = __lasx_xvhaddw_q_d(out0, out0);
                val += __lasx_xvpickve2gr_w(out0, 0);
            }
            for (; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[i]  = FFMIN(val >> 7, max);
            filter += filterSize;
        }
    } else {
        for (i = 0; i < dstW; i++) {
            int val = 0;
            const uint8_t *srcPos = src + filterPos[i];

            for (int j = 0; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[i]  = FFMIN(val >> 7, max);
            filter += filterSize;
        }
    }
}

void ff_hscale_8_to_19_lasx(SwsContext *c, int16_t *_dst, int dstW,
                            const uint8_t *src, const int16_t *filter,
                            const int32_t *filterPos, int filterSize)
{
    int i;
    int max = (1 << 19) - 1;
    int32_t *dst = (int32_t *) _dst;

    if (filterSize == 8) {
        __m256i src0, src1, src2, src3, src4, src5, src6, src7;
        __m256i filter0, filter1, filter2, filter3;
        __m256i vmax = __lasx_xvreplgr2vr_w(max);
        __m256i shuf = {0x0000000400000000, 0x0000000500000001,
                        0x0000000600000002, 0x0000000700000003};
        int len = dstW >> 3;
        int res = dstW & 7;
        while (len--) {
            SCALE_8_8(3);
            __lasx_xvst(src0, dst, 0);
            dst       += 8;
        }
        if (res & 4) {
            SCALE_8_4(3);
            __lasx_xvstelm_d(src0, dst, 0, 0);
            __lasx_xvstelm_d(src0, dst, 8, 1);
            dst       += 4;
        }
        if (res & 2) {
            SCALE_8_2(3);
        }
        if (res & 1) {
            int val = 0;
            __m256i src0, filter0, out0;

            src0    = __lasx_xvldrepl_d(src + filterPos[0], 0);
            filter0 = __lasx_xvld(filter, 0);
            src0 = __lasx_vext2xv_hu_bu(src0);
            out0 = __lasx_xvdp2_w_h(filter0, src0);
            out0    = __lasx_xvhaddw_d_w(out0, out0);
            out0    = __lasx_xvhaddw_q_d(out0, out0);
            val     = __lasx_xvpickve2gr_w(out0, 0);
            dst[0]  = FFMIN(val >> 3, max);
        }
    } else if (filterSize == 4) {
        __m256i src0, src1, src2, src3, src4, src5, src6, src7;
        __m256i filter0, filter1;
        __m256i vmax = __lasx_xvreplgr2vr_w(max);
        __m256i shuf = {0x0000000100000000, 0x0000000500000004,
                        0x0000000300000002, 0x0000000700000006};
        int len = dstW >> 3;
        int res = dstW & 7;
        while (len--) {
            SCALE_4_8(3);
            src0 = __lasx_xvperm_w(src0, shuf);
            __lasx_xvst(src0, dst, 0);
            dst       += 8;
        }
        if (res & 4) {
            SCALE_4_4(3);
            __lasx_xvstelm_d(src0, dst, 0, 0);
            __lasx_xvstelm_d(src0, dst, 8, 1);
            dst       += 4;
        }
        if (res & 2) {
            SCALE_4_2(3);
        }
        if (res & 1) {
            int val = 0;
            const uint8_t *srcPos = src + filterPos[0];

            for (int j = 0; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[0] = FFMIN(val >> 3, max);
        }
    } else if (filterSize > 8) {
        int len = dstW >> 2;
        int res = dstW & 3;
        int filterlen = filterSize - 7;
        __m256i zero = __lasx_xvldi(0);

        while (len--) {
            __m256i src0, src1, src2, src3;
            __m256i filter0, filter1, filter2, filter3, out0, out1;
            __m256i out = zero;
            const uint8_t *srcPos1 = src + filterPos[0];
            const uint8_t *srcPos2 = src + filterPos[1];
            const uint8_t *srcPos3 = src + filterPos[2];
            const uint8_t *srcPos4 = src + filterPos[3];
            const int16_t *filterStart1 = filter;
            const int16_t *filterStart2 = filterStart1 + filterSize;
            const int16_t *filterStart3 = filterStart2 + filterSize;
            const int16_t *filterStart4 = filterStart3 + filterSize;
            int j, val1 = 0, val2 = 0, val3 = 0, val4 = 0;

            for (j = 0; j < filterlen; j += 8) {
                SCALE_16
            }
            val1 = __lasx_xvpickve2gr_w(out, 0);
            val2 = __lasx_xvpickve2gr_w(out, 4);
            val3 = __lasx_xvpickve2gr_w(out, 2);
            val4 = __lasx_xvpickve2gr_w(out, 6);
            for (; j < filterSize; j++) {
                val1 += ((int)srcPos1[j]) * filterStart1[j];
                val2 += ((int)srcPos2[j]) * filterStart2[j];
                val3 += ((int)srcPos3[j]) * filterStart3[j];
                val4 += ((int)srcPos4[j]) * filterStart4[j];
            }
            dst[0] = FFMIN(val1 >> 3, max);
            dst[1] = FFMIN(val2 >> 3, max);
            dst[2] = FFMIN(val3 >> 3, max);
            dst[3] = FFMIN(val4 >> 3, max);
            dst       += 4;
            filterPos += 4;
            filter     = filterStart4 + filterSize;
        }
        for (i = 0; i < res; i++) {
            int j, val = 0;
            const uint8_t *srcPos = src + filterPos[i];
            __m256i src1, filter0, out0;

            for (j = 0; j < filterlen; j += 8) {
                src1   = __lasx_xvldrepl_d((srcPos + j), 0);
                filter0 = __lasx_xvld(filter + j, 0);
                src1 = __lasx_xvilvl_b(zero, src1);
                out0 = __lasx_xvdp2_w_h(filter0, src1);
                out0 = __lasx_xvhaddw_d_w(out0, out0);
                out0 = __lasx_xvhaddw_q_d(out0, out0);
                val += __lasx_xvpickve2gr_w(out0, 0);
            }
            for (; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[i] = FFMIN(val >> 3, max);
            filter += filterSize;
        }
    } else {
        for (i = 0; i < dstW; i++) {
            int val = 0;
            const uint8_t *srcPos = src + filterPos[i];

            for (int j = 0; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[i]  = FFMIN(val >> 3, max);
            filter += filterSize;
        }
    }
}

#undef SCALE_16

#define SCALE_8                                                              \
{                                                                            \
    __m256i src0, src1, src2, src3, filter0, filter1, out0, out1;            \
    DUP4_ARG2(__lasx_xvld, src + filterPos[0], 0, src + filterPos[1], 0,     \
              src + filterPos[2], 0, src + filterPos[3], 0, src0, src1, src2,\
              src3);                                                         \
    filter0 = __lasx_xvld(filter, 0);                                        \
    filter1 = __lasx_xvld(filter, 32);                                       \
    src0    = __lasx_xvpermi_q(src0, src1, 0x02);                            \
    src2    = __lasx_xvpermi_q(src2, src3, 0x02);                            \
    out0    = __lasx_xvdp2_w_hu_h(src0, filter0);                            \
    out1    = __lasx_xvdp2_w_hu_h(src2, filter1);                            \
    src0    = __lasx_xvhaddw_d_w(out0, out0);                                \
    src1    = __lasx_xvhaddw_d_w(out1, out1);                                \
    out0    = __lasx_xvpackev_d(src1, src0);                                 \
    out1    = __lasx_xvpackod_d(src1, src0);                                 \
    out0    = __lasx_xvadd_w(out0, out1);                                    \
    out0    = __lasx_xvsra_w(out0, shift);                                   \
    out0    = __lasx_xvmin_w(out0, v_max);                                   \
    dst[0]  = __lasx_xvpickve2gr_w(out0, 0);                                 \
    dst[1]  = __lasx_xvpickve2gr_w(out0, 4);                                 \
    dst[2]  = __lasx_xvpickve2gr_w(out0, 2);                                 \
    dst[3]  = __lasx_xvpickve2gr_w(out0, 6);                                 \
    filterPos += 4;                                                          \
    filter += 32;                                                            \
    dst += 4;                                                                \
}

#define SCALE_16                                                             \
{                                                                            \
    int dex = j << 1;                                                        \
    DUP4_ARG2(__lasx_xvldx, srcPos1, dex, srcPos2, dex, srcPos3, dex,        \
              srcPos4, dex, src0, src1, src2, src3);                         \
    DUP4_ARG2(__lasx_xvldx, filterStart1, dex, filterStart2, dex,            \
              filterStart3, dex, filterStart4, dex, filter0,                 \
              filter1, filter2, filter3);                                    \
    src0     = __lasx_xvpermi_q(src0, src1, 0x02);                           \
    src1     = __lasx_xvpermi_q(src2, src3, 0x02);                           \
    filter0  = __lasx_xvpermi_q(filter0, filter1, 0x02);                     \
    filter1  = __lasx_xvpermi_q(filter2, filter3, 0x02);                     \
    out0     = __lasx_xvdp2_w_hu_h(src0, filter0);                           \
    out1     = __lasx_xvdp2_w_hu_h(src1, filter1);                           \
    src0     = __lasx_xvhaddw_d_w(out0, out0);                               \
    src1     = __lasx_xvhaddw_d_w(out1, out1);                               \
    out0     = __lasx_xvpackev_d(src1, src0);                                \
    out1     = __lasx_xvpackod_d(src1, src0);                                \
    out0     = __lasx_xvadd_w(out0, out1);                                   \
    out      = __lasx_xvadd_w(out, out0);                                    \
}

void ff_hscale_16_to_15_lasx(SwsContext *c, int16_t *dst, int dstW,
                             const uint8_t *_src, const int16_t *filter,
                             const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int i;
    const uint16_t *src = (const uint16_t *) _src;
    int sh              = desc->comp[0].depth - 1;
    int max = (1 << 15) - 1;
    int len = dstW >> 2;
    int res = dstW & 3;
    __m256i shift;
    __m256i zero = __lasx_xvldi(0);

    if (sh < 15) {
        sh = isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8 ? 13 :
                      (desc->comp[0].depth - 1);
    } else if (desc->flags && AV_PIX_FMT_FLAG_FLOAT) {
        sh = 15;
    }
    shift = __lasx_xvreplgr2vr_w(sh);

    if (filterSize == 8) {
        __m256i v_max = __lasx_xvreplgr2vr_w(max);
        for (i = 0; i < len; i++) {
            SCALE_8
        }
        for (i = 0; i < res; i++) {
            int val = 0;
            __m256i src0, filter0, out0;

            src0    = __lasx_xvld(src + filterPos[i], 0);
            filter0 = __lasx_xvld(filter, 0);
            out0 = __lasx_xvdp2_w_hu_h(src0, filter0);
            out0    = __lasx_xvhaddw_d_w(out0, out0);
            out0    = __lasx_xvhaddw_q_d(out0, out0);
            val     = __lasx_xvpickve2gr_w(out0, 0);
            dst[i]  = FFMIN(val >> sh, max);
            filter += 8;
        }
    } else if (filterSize == 4) {
        __m256i v_max = __lasx_xvreplgr2vr_w(max);
        for (i = 0; i < len; i++) {
            __m256i src1, src2, src3, src4, src0, filter0, out0;

            src1 = __lasx_xvldrepl_d(src + filterPos[0], 0);
            src2 = __lasx_xvldrepl_d(src + filterPos[1], 0);
            src3 = __lasx_xvldrepl_d(src + filterPos[2], 0);
            src4 = __lasx_xvldrepl_d(src + filterPos[3], 0);
            filter0 = __lasx_xvld(filter, 0);
            src1 = __lasx_xvextrins_d(src1, src2, 0x10);
            src3 = __lasx_xvextrins_d(src3, src4, 0x10);
            src0 = __lasx_xvpermi_q(src1, src3, 0x02);
            out0 = __lasx_xvdp2_w_hu_h(src0, filter0);
            out0 = __lasx_xvhaddw_d_w(out0, out0);
            out0 = __lasx_xvsra_w(out0, shift);
            out0 = __lasx_xvmin_w(out0, v_max);
            dst[0] = __lasx_xvpickve2gr_w(out0, 0);
            dst[1] = __lasx_xvpickve2gr_w(out0, 2);
            dst[2] = __lasx_xvpickve2gr_w(out0, 4);
            dst[3] = __lasx_xvpickve2gr_w(out0, 6);
            dst       += 4;
            filterPos += 4;
            filter    += 16;
        }
        for (i = 0; i < res; i++) {
            int val = 0;
            const uint16_t *srcPos = src + filterPos[i];

            for (int j = 0; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[i]  = FFMIN(val >> sh, max);
            filter += 4;
        }
    } else if (filterSize > 8) {
        int filterlen = filterSize - 7;

        for (i = 0; i < len; i++) {
            __m256i src0, src1, src2, src3;
            __m256i filter0, filter1, filter2, filter3, out0, out1;
            __m256i out = zero;
            const uint16_t *srcPos1 = src + filterPos[0];
            const uint16_t *srcPos2 = src + filterPos[1];
            const uint16_t *srcPos3 = src + filterPos[2];
            const uint16_t *srcPos4 = src + filterPos[3];
            const int16_t *filterStart1 = filter;
            const int16_t *filterStart2 = filterStart1 + filterSize;
            const int16_t *filterStart3 = filterStart2 + filterSize;
            const int16_t *filterStart4 = filterStart3 + filterSize;
            int j, val1 = 0, val2 = 0, val3 = 0, val4 = 0;

            for (j = 0; j < filterlen; j += 8) {
                SCALE_16
            }
            val1 = __lasx_xvpickve2gr_w(out, 0);
            val2 = __lasx_xvpickve2gr_w(out, 4);
            val3 = __lasx_xvpickve2gr_w(out, 2);
            val4 = __lasx_xvpickve2gr_w(out, 6);
            for (; j < filterSize; j++) {
                val1 += ((int)srcPos1[j]) * filterStart1[j];
                val2 += ((int)srcPos2[j]) * filterStart2[j];
                val3 += ((int)srcPos3[j]) * filterStart3[j];
                val4 += ((int)srcPos4[j]) * filterStart4[j];
            }
            dst[0] = FFMIN(val1 >> sh, max);
            dst[1] = FFMIN(val2 >> sh, max);
            dst[2] = FFMIN(val3 >> sh, max);
            dst[3] = FFMIN(val4 >> sh, max);
            dst       += 4;
            filterPos += 4;
            filter     = filterStart4 + filterSize;
        }
        for (i = 0; i < res; i++) {
            int j, val = 0;
            const uint16_t *srcPos      = src + filterPos[i];
            __m256i src0, filter0, out0;

            for (j = 0; j < filterlen; j += 8) {
                int dex = j << 1;
                src0    = __lasx_xvldx(srcPos, dex);
                filter0 = __lasx_xvldx(filter, dex);
                out0 = __lasx_xvdp2_w_hu_h(src0, filter0);
                out0    = __lasx_xvhaddw_d_w(out0, out0);
                out0    = __lasx_xvhaddw_q_d(out0, out0);
                val    += __lasx_xvpickve2gr_w(out0, 0);
            }
            for (; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[i]  = FFMIN(val >> sh, max);
            filter += filterSize;
        }
    } else {
        for (i = 0; i < dstW; i++) {
            int val = 0;
            const uint16_t *srcPos = src + filterPos[i];

            for (int j = 0; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[i]  = FFMIN(val >> sh, max);
            filter += filterSize;
        }
    }
}

void ff_hscale_16_to_19_lasx(SwsContext *c, int16_t *_dst, int dstW,
                             const uint8_t *_src, const int16_t *filter,
                             const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int i;
    int32_t *dst        = (int32_t *) _dst;
    const uint16_t *src = (const uint16_t *) _src;
    int sh              = desc->comp[0].depth - 5;
    int max = (1 << 19) - 1;
    int len = dstW >> 2;
    int res = dstW & 3;
    __m256i shift;
    __m256i zero = __lasx_xvldi(0);

    if ((isAnyRGB(c->srcFormat) || c->srcFormat == AV_PIX_FMT_PAL8)
         && desc->comp[0].depth<16) {
        sh = 9;
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) {
        sh = 11;
    }
    shift = __lasx_xvreplgr2vr_w(sh);

    if (filterSize == 8) {
        __m256i v_max = __lasx_xvreplgr2vr_w(max);
        for (i = 0; i < len; i++) {
            SCALE_8
        }
        for (i = 0; i < res; i++) {
            int val = 0;
            __m256i src0, filter0, out0;

            src0 = __lasx_xvld(src + filterPos[i], 0);
            filter0 = __lasx_xvld(filter, 0);
            out0 = __lasx_xvdp2_w_hu_h(src0, filter0);
            out0 = __lasx_xvhaddw_d_w(out0, out0);
            out0 = __lasx_xvhaddw_q_d(out0, out0);
            val  = __lasx_xvpickve2gr_w(out0, 0);
            dst[i] = FFMIN(val >> sh, max);
            filter += 8;
        }
    } else if (filterSize == 4) {
        __m256i v_max = __lasx_xvreplgr2vr_w(max);
        for (i = 0; i < len; i++) {
            __m256i src1, src2, src3, src4, src0, filter0, out0;

            src1 = __lasx_xvldrepl_d(src + filterPos[0], 0);
            src2 = __lasx_xvldrepl_d(src + filterPos[1], 0);
            src3 = __lasx_xvldrepl_d(src + filterPos[2], 0);
            src4 = __lasx_xvldrepl_d(src + filterPos[3], 0);
            filter0 = __lasx_xvld(filter, 0);
            src1 = __lasx_xvextrins_d(src1, src2, 0x10);
            src3 = __lasx_xvextrins_d(src3, src4, 0x10);
            src0 = __lasx_xvpermi_q(src1, src3, 0x02);
            out0 = __lasx_xvdp2_w_hu_h(src0, filter0);
            out0 = __lasx_xvhaddw_d_w(out0, out0);
            out0 = __lasx_xvsra_w(out0, shift);
            out0 = __lasx_xvmin_w(out0, v_max);
            dst[0] = __lasx_xvpickve2gr_w(out0, 0);
            dst[1] = __lasx_xvpickve2gr_w(out0, 2);
            dst[2] = __lasx_xvpickve2gr_w(out0, 4);
            dst[3] = __lasx_xvpickve2gr_w(out0, 6);
            dst       += 4;
            filterPos += 4;
            filter    += 16;
        }
        for (i = 0; i < res; i++) {
            int val = 0;
            const uint16_t *srcPos = src + filterPos[i];

            for (int j = 0; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[i]  = FFMIN(val >> sh, max);
            filter += 4;
        }
    } else if (filterSize > 8) {
        int filterlen = filterSize - 7;

        for (i = 0; i < len; i ++) {
            __m256i src0, src1, src2, src3;
            __m256i filter0, filter1, filter2, filter3, out0, out1;
            __m256i out = zero;
            const uint16_t *srcPos1 = src + filterPos[0];
            const uint16_t *srcPos2 = src + filterPos[1];
            const uint16_t *srcPos3 = src + filterPos[2];
            const uint16_t *srcPos4 = src + filterPos[3];
            const int16_t *filterStart1 = filter;
            const int16_t *filterStart2 = filterStart1 + filterSize;
            const int16_t *filterStart3 = filterStart2 + filterSize;
            const int16_t *filterStart4 = filterStart3 + filterSize;
            int j, val1 = 0, val2 = 0, val3 = 0, val4 = 0;

            for (j = 0; j < filterlen; j += 8) {
                SCALE_16
            }
            val1 = __lasx_xvpickve2gr_w(out, 0);
            val2 = __lasx_xvpickve2gr_w(out, 4);
            val3 = __lasx_xvpickve2gr_w(out, 2);
            val4 = __lasx_xvpickve2gr_w(out, 6);
            for (; j < filterSize; j++) {
                val1 += ((int)srcPos1[j]) * filterStart1[j];
                val2 += ((int)srcPos2[j]) * filterStart2[j];
                val3 += ((int)srcPos3[j]) * filterStart3[j];
                val4 += ((int)srcPos4[j]) * filterStart4[j];
            }
            dst[0] = FFMIN(val1 >> sh, max);
            dst[1] = FFMIN(val2 >> sh, max);
            dst[2] = FFMIN(val3 >> sh, max);
            dst[3] = FFMIN(val4 >> sh, max);
            dst       += 4;
            filterPos += 4;
            filter     = filterStart4 + filterSize;
        }
        for (i = 0; i < res; i++) {
            int j, val = 0;
            const uint16_t *srcPos      = src + filterPos[i];
            __m256i src0, filter0, out0;

            for (j = 0; j < filterlen; j += 8) {
                int dex = j << 1;
                src0 = __lasx_xvldx(srcPos, dex);
                filter0 = __lasx_xvldx(filter, dex);
                out0 = __lasx_xvdp2_w_hu_h(src0, filter0);
                out0    = __lasx_xvhaddw_d_w(out0, out0);
                out0    = __lasx_xvhaddw_q_d(out0, out0);
                val    += __lasx_xvpickve2gr_w(out0, 0);
            }
            for (; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[i]  = FFMIN(val >> sh, max);
            filter += filterSize;
        }
    } else {
        for (i = 0; i < dstW; i++) {
            int val = 0;
            const uint16_t *srcPos = src + filterPos[i];

            for (int j = 0; j < filterSize; j++) {
                val += ((int)srcPos[j]) * filter[j];
            }
            dst[i]  = FFMIN(val >> sh, max);
            filter += filterSize;
        }
    }
}

#undef SCALE_8
#undef SCALE_16
