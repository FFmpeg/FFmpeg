/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003-2010 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "h264qpel.h"

#define pixeltmp int16_t
#define BIT_DEPTH 8
#include "h264qpel_template.c"

static void put_h264_qpel2_h_lowpass_8(uint8_t *dst, const uint8_t *restrict src, int dstStride, int srcStride)
{
    const int h = 2;
    for (int i = 0; i < h; ++i) {
        dst[0] = av_clip_uint8(((src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3]) + 16) >> 5);
        dst[1] = av_clip_uint8(((src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4]) + 16) >> 5);
        dst += dstStride;
        src += srcStride;
    }
}

static void put_h264_qpel2_v_lowpass_8(uint8_t *dst, const uint8_t *restrict src, int dstStride, int srcStride)
{
    const int w = 2;
    for (int i = 0; i < w; ++i) {
        const int srcB = src[-2*srcStride];
        const int srcA = src[-1*srcStride];
        const int src0 = src[0 *srcStride];
        const int src1 = src[1 *srcStride];
        const int src2 = src[2 *srcStride];
        const int src3 = src[3 *srcStride];
        const int src4 = src[4 *srcStride];
        dst[0*dstStride] = av_clip_uint8(((src0+src1)*20 - (srcA+src2)*5 + (srcB+src3) + 16) >> 5);
        dst[1*dstStride] = av_clip_uint8(((src1+src2)*20 - (src0+src3)*5 + (srcA+src4) + 16) >> 5);
        dst++;
        src++;
    }
}

static void put_h264_qpel2_hv_lowpass_8(uint8_t *dst, pixeltmp *tmp, const uint8_t *restrict src, int dstStride, int tmpStride, int srcStride)
{
    const int h = 2;
    const int w = 2;
    src -= 2*srcStride;
    for (int i = 0; i < h + 5; ++i) {
        tmp[0] = (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3]);
        tmp[1] = (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4]);
        tmp += tmpStride;
        src += srcStride;
    }
    tmp -= tmpStride*(h+5-2);
    for (int i = 0; i < w; ++i) {
        const int tmpB = tmp[-2*tmpStride];
        const int tmpA = tmp[-1*tmpStride];
        const int tmp0 = tmp[0 *tmpStride];
        const int tmp1 = tmp[1 *tmpStride];
        const int tmp2 = tmp[2 *tmpStride];
        const int tmp3 = tmp[3 *tmpStride];
        const int tmp4 = tmp[4 *tmpStride];
        dst[0*dstStride] = av_clip_uint8(((tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3) + 512) >> 10);
        dst[1*dstStride] = av_clip_uint8(((tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4) + 512) >> 10);
        dst++;
        tmp++;
    }
}

H264_MC(put_, 2)

#undef BIT_DEPTH

#define BIT_DEPTH 9
#include "h264qpel_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "h264qpel_template.c"
#undef BIT_DEPTH
#undef pixeltmp

#define pixeltmp int32_t
#define BIT_DEPTH 12
#include "h264qpel_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 14
#include "h264qpel_template.c"
#undef BIT_DEPTH


av_cold void ff_h264qpel_init(H264QpelContext *c, int bit_depth)
{
#undef FUNCC
#define FUNCC(f, depth) f ## _ ## depth ## _c

#define dspfunc2(PFX, IDX, NUM, depth)                                  \
    c->PFX ## _pixels_tab[IDX][ 0] = FUNCC(PFX ## NUM ## _mc00, depth); \
    c->PFX ## _pixels_tab[IDX][ 1] = FUNCC(PFX ## NUM ## _mc10, depth); \
    c->PFX ## _pixels_tab[IDX][ 2] = FUNCC(PFX ## NUM ## _mc20, depth); \
    c->PFX ## _pixels_tab[IDX][ 3] = FUNCC(PFX ## NUM ## _mc30, depth); \
    c->PFX ## _pixels_tab[IDX][ 4] = FUNCC(PFX ## NUM ## _mc01, depth); \
    c->PFX ## _pixels_tab[IDX][ 5] = FUNCC(PFX ## NUM ## _mc11, depth); \
    c->PFX ## _pixels_tab[IDX][ 6] = FUNCC(PFX ## NUM ## _mc21, depth); \
    c->PFX ## _pixels_tab[IDX][ 7] = FUNCC(PFX ## NUM ## _mc31, depth); \
    c->PFX ## _pixels_tab[IDX][ 8] = FUNCC(PFX ## NUM ## _mc02, depth); \
    c->PFX ## _pixels_tab[IDX][ 9] = FUNCC(PFX ## NUM ## _mc12, depth); \
    c->PFX ## _pixels_tab[IDX][10] = FUNCC(PFX ## NUM ## _mc22, depth); \
    c->PFX ## _pixels_tab[IDX][11] = FUNCC(PFX ## NUM ## _mc32, depth); \
    c->PFX ## _pixels_tab[IDX][12] = FUNCC(PFX ## NUM ## _mc03, depth); \
    c->PFX ## _pixels_tab[IDX][13] = FUNCC(PFX ## NUM ## _mc13, depth); \
    c->PFX ## _pixels_tab[IDX][14] = FUNCC(PFX ## NUM ## _mc23, depth); \
    c->PFX ## _pixels_tab[IDX][15] = FUNCC(PFX ## NUM ## _mc33, depth)

#define SET_QPEL(depth)                         \
    dspfunc2(put_h264_qpel, 0, 16, depth);      \
    dspfunc2(put_h264_qpel, 1,  8, depth);      \
    dspfunc2(put_h264_qpel, 2,  4, depth);      \
    dspfunc2(avg_h264_qpel, 0, 16, depth);      \
    dspfunc2(avg_h264_qpel, 1,  8, depth);      \
    dspfunc2(avg_h264_qpel, 2,  4, depth)

    switch (bit_depth) {
    default:
        SET_QPEL(8);
        dspfunc2(put_h264_qpel, 3,  2, 8); // only used by Snow
        break;
    case 9:
        SET_QPEL(9);
        break;
    case 10:
        SET_QPEL(10);
        break;
    case 12:
        SET_QPEL(12);
        break;
    case 14:
        SET_QPEL(14);
        break;
    }

#if ARCH_AARCH64
    ff_h264qpel_init_aarch64(c, bit_depth);
#elif ARCH_ARM
    ff_h264qpel_init_arm(c, bit_depth);
#elif ARCH_PPC
    ff_h264qpel_init_ppc(c, bit_depth);
#elif ARCH_RISCV
    ff_h264qpel_init_riscv(c, bit_depth);
#elif ARCH_X86
    ff_h264qpel_init_x86(c, bit_depth);
#elif ARCH_MIPS
    ff_h264qpel_init_mips(c, bit_depth);
#elif ARCH_LOONGARCH64
    ff_h264qpel_init_loongarch(c, bit_depth);
#endif
}
