/*
 * Loongson SIMD optimized h264qpel
 *
 * Copyright (c) 2015 Loongson Technology Corporation Limited
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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
#include "hpeldsp_mips.h"
#include "libavcodec/bit_depth_template.c"
#include "libavutil/mips/asmdefs.h"

static inline void copy_block4_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride, int h)
{
    double ftmp[1];
    uint64_t low32;

    __asm__ volatile (
        "1:                                                             \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp0]                                \n\t"
        "gsswlc1    %[ftmp0],   0x03(%[dst])                            \n\t"
        "gsswrc1    %[ftmp0],   0x00(%[dst])                            \n\t"
        "addi       %[h],       %[h],           -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),
          [dst]"+&r"(dst),                  [src]"+&r"(src),
          [h]"+&r"(h),
          [low32]"=&r"(low32)
        : [dstStride]"r"((mips_reg)dstStride),
          [srcStride]"r"((mips_reg)srcStride)
        : "memory"
    );
}

static inline void copy_block8_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride, int h)
{
    double ftmp[1];

    __asm__ volatile (
        "1:                                                             \n\t"
        "gsldlc1    %[ftmp0],   0x07(%[src])                            \n\t"
        "gsldrc1    %[ftmp0],   0x00(%[src])                            \n\t"
        "gssdlc1    %[ftmp0],   0x07(%[dst])                            \n\t"
        "gssdrc1    %[ftmp0],   0x00(%[dst])                            \n\t"
        "addi       %[h],       %[h],           -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),
          [dst]"+&r"(dst),                  [src]"+&r"(src),
          [h]"+&r"(h)
        : [dstStride]"r"((mips_reg)dstStride),
          [srcStride]"r"((mips_reg)srcStride)
        : "memory"
    );
}

static inline void copy_block16_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride, int h)
{
    double ftmp[1];
    uint64_t tmp[1];

    __asm__ volatile (
        "1:                                                             \n\t"
        "gsldlc1    %[ftmp0],   0x07(%[src])                            \n\t"
        "gsldrc1    %[ftmp0],   0x00(%[src])                            \n\t"
        "ldl        %[tmp0],    0x0f(%[src])                            \n\t"
        "ldr        %[tmp0],    0x08(%[src])                            \n\t"
        "gssdlc1    %[ftmp0],   0x07(%[dst])                            \n\t"
        "gssdrc1    %[ftmp0],   0x00(%[dst])                            \n\t"
        "sdl        %[tmp0],    0x0f(%[dst])                            \n\t"
        "sdr        %[tmp0],    0x08(%[dst])                            \n\t"
        "addi       %[h],       %[h],           -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),
          [tmp0]"=&r"(tmp[0]),
          [dst]"+&r"(dst),                  [src]"+&r"(src),
          [h]"+&r"(h)
        : [dstStride]"r"((mips_reg)dstStride),
          [srcStride]"r"((mips_reg)srcStride)
        : "memory"
    );
}

#define op2_avg(a, b)  a = (((a)+CLIP(((b) + 512)>>10)+1)>>1)
#define op2_put(a, b)  a = CLIP(((b) + 512)>>10)
static void put_h264_qpel4_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    double ftmp[10];
    uint64_t tmp[1];
    uint64_t low32;

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "dli        %[tmp0],    0x04                                    \n\t"
        "1:                                                             \n\t"
        "uld        %[low32],   -0x02(%[src])                           \n\t"
        "mtc1       %[low32],   %[ftmp1]                                \n\t"
        "uld        %[low32],   -0x01(%[src])                           \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp3]                                \n\t"
        "uld        %[low32],   0x01(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp4]                                \n\t"
        "uld        %[low32],   0x02(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp5]                                \n\t"
        "uld        %[low32],   0x03(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp6]                                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp7],   %[ftmp3],       %[ftmp4]                \n\t"
        "paddsh     %[ftmp8],   %[ftmp2],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp1],       %[ftmp6]                \n\t"
        "pmullh     %[ftmp7],   %[ftmp7],       %[ff_pw_20]             \n\t"
        "pmullh     %[ftmp8],   %[ftmp8],       %[ff_pw_5]              \n\t"
        "psubsh     %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp7],       %[ftmp9]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp9],       %[ff_pw_16]             \n\t"
        "psrah      %[ftmp9],   %[ftmp9],       %[ff_pw_5]              \n\t"
        "packushb   %[ftmp9],   %[ftmp9],       %[ftmp0]                \n\t"
        "gsswlc1    %[ftmp9],   0x03(%[dst])                            \n\t"
        "gsswrc1    %[ftmp9],   0x00(%[dst])                            \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp[0]),
          [dst]"+&r"(dst),                  [src]"+&r"(src),
          [low32]"=&r"(low32)
        : [dstStride]"r"((mips_reg)dstStride),
          [srcStride]"r"((mips_reg)srcStride),
          [ff_pw_20]"f"(ff_pw_20),          [ff_pw_5]"f"(ff_pw_5),
          [ff_pw_16]"f"(ff_pw_16)
        : "memory"
    );
}

static void put_h264_qpel8_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    double ftmp[11];
    uint64_t tmp[1];

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "dli        %[tmp0],    0x08                                    \n\t"
        "1:                                                             \n\t"
        "gsldlc1    %[ftmp1],   0x05(%[src])                            \n\t"
        "gsldrc1    %[ftmp1],   -0x02(%[src])                           \n\t"
        "gsldlc1    %[ftmp2],   0x06(%[src])                            \n\t"
        "gsldrc1    %[ftmp2],   -0x01(%[src])                           \n\t"
        "gsldlc1    %[ftmp3],   0x07(%[src])                            \n\t"
        "gsldrc1    %[ftmp3],   0x00(%[src])                            \n\t"
        "gsldlc1    %[ftmp4],   0x08(%[src])                            \n\t"
        "gsldrc1    %[ftmp4],   0x01(%[src])                            \n\t"
        "gsldlc1    %[ftmp5],   0x09(%[src])                            \n\t"
        "gsldrc1    %[ftmp5],   0x02(%[src])                            \n\t"
        "gsldlc1    %[ftmp6],   0x0a(%[src])                            \n\t"
        "gsldrc1    %[ftmp6],   0x03(%[src])                            \n\t"
        "punpcklbh  %[ftmp7],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp10],  %[ftmp4],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp3],   %[ftmp7],       %[ftmp9]                \n\t"
        "paddsh     %[ftmp4],   %[ftmp8],       %[ftmp10]               \n\t"
        "pmullh     %[ftmp3],   %[ftmp3],       %[ff_pw_20]             \n\t"
        "pmullh     %[ftmp4],   %[ftmp4],       %[ff_pw_20]             \n\t"
        "punpcklbh  %[ftmp7],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp10],  %[ftmp5],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp2],   %[ftmp7],       %[ftmp9]                \n\t"
        "paddsh     %[ftmp5],   %[ftmp8],       %[ftmp10]               \n\t"
        "pmullh     %[ftmp2],   %[ftmp2],       %[ff_pw_5]              \n\t"
        "pmullh     %[ftmp5],   %[ftmp5],       %[ff_pw_5]              \n\t"
        "punpcklbh  %[ftmp7],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp6],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp10],  %[ftmp6],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp1],   %[ftmp7],       %[ftmp9]                \n\t"
        "paddsh     %[ftmp6],   %[ftmp8],       %[ftmp10]               \n\t"
        "psubsh     %[ftmp3],   %[ftmp3],       %[ftmp2]                \n\t"
        "psubsh     %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[ff_pw_16]             \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[ff_pw_16]             \n\t"
        "psrah      %[ftmp3],   %[ftmp3],       %[ff_pw_5]              \n\t"
        "psrah      %[ftmp4],   %[ftmp4],       %[ff_pw_5]              \n\t"
        "packushb   %[ftmp9],   %[ftmp3],       %[ftmp4]                \n\t"
        "gssdlc1    %[ftmp9],   0x07(%[dst])                            \n\t"
        "gssdrc1    %[ftmp9],   0x00(%[dst])                            \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),
          [tmp0]"=&r"(tmp[0]),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [dstStride]"r"((mips_reg)dstStride),
          [srcStride]"r"((mips_reg)srcStride),
          [ff_pw_20]"f"(ff_pw_20),          [ff_pw_5]"f"(ff_pw_5),
          [ff_pw_16]"f"(ff_pw_16)
        : "memory"
    );
}

static void put_h264_qpel16_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    put_h264_qpel8_h_lowpass_mmi(dst, src, dstStride, srcStride);
    put_h264_qpel8_h_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    put_h264_qpel8_h_lowpass_mmi(dst, src, dstStride, srcStride);
    put_h264_qpel8_h_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
}

static void avg_h264_qpel4_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    double ftmp[11];
    uint64_t tmp[1];
    uint64_t low32;

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "dli        %[tmp0],    0x04                                    \n\t"
        "1:                                                             \n\t"
        "uld        %[low32],   -0x02(%[src])                           \n\t"
        "mtc1       %[low32],   %[ftmp1]                                \n\t"
        "uld        %[low32],   -0x01(%[src])                           \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp3]                                \n\t"
        "uld        %[low32],   0x01(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp4]                                \n\t"
        "uld        %[low32],   0x02(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp5]                                \n\t"
        "uld        %[low32],   0x03(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp6]                                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp7],   %[ftmp3],       %[ftmp4]                \n\t"
        "paddsh     %[ftmp8],   %[ftmp2],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp1],       %[ftmp6]                \n\t"
        "pmullh     %[ftmp7],   %[ftmp7],       %[ff_pw_20]             \n\t"
        "pmullh     %[ftmp8],   %[ftmp8],       %[ff_pw_5]              \n\t"
        "psubsh     %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp7],       %[ftmp9]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp9],       %[ff_pw_16]             \n\t"
        "psrah      %[ftmp9],   %[ftmp9],       %[ff_pw_5]              \n\t"
        "packushb   %[ftmp9],   %[ftmp9],       %[ftmp0]                \n\t"
        "lwc1       %[ftmp10],  0x00(%[dst])                            \n\t"
        "pavgb      %[ftmp9],   %[ftmp9],       %[ftmp10]               \n\t"
        "gsswlc1    %[ftmp9],   0x03(%[dst])                            \n\t"
        "gsswrc1    %[ftmp9],   0x00(%[dst])                            \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),
          [tmp0]"=&r"(tmp[0]),
          [dst]"+&r"(dst),                  [src]"+&r"(src),
          [low32]"=&r"(low32)
        : [dstStride]"r"((mips_reg)dstStride),
          [srcStride]"r"((mips_reg)srcStride),
          [ff_pw_20]"f"(ff_pw_20),          [ff_pw_5]"f"(ff_pw_5),
          [ff_pw_16]"f"(ff_pw_16)
        : "memory"
    );
}

static void avg_h264_qpel8_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    double ftmp[11];
    uint64_t tmp[1];

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "dli        %[tmp0],    0x08                                    \n\t"
        "1:                                                             \n\t"
        "gsldlc1    %[ftmp1],   0x05(%[src])                            \n\t"
        "gsldrc1    %[ftmp1],   -0x02(%[src])                           \n\t"
        "gsldlc1    %[ftmp2],   0x06(%[src])                            \n\t"
        "gsldrc1    %[ftmp2],   -0x01(%[src])                           \n\t"
        "gsldlc1    %[ftmp3],   0x07(%[src])                            \n\t"
        "gsldrc1    %[ftmp3],   0x00(%[src])                            \n\t"
        "gsldlc1    %[ftmp4],   0x08(%[src])                            \n\t"
        "gsldrc1    %[ftmp4],   0x01(%[src])                            \n\t"
        "gsldlc1    %[ftmp5],   0x09(%[src])                            \n\t"
        "gsldrc1    %[ftmp5],   0x02(%[src])                            \n\t"
        "gsldlc1    %[ftmp6],   0x0a(%[src])                            \n\t"
        "gsldrc1    %[ftmp6],   0x03(%[src])                            \n\t"
        "punpcklbh  %[ftmp7],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp10],  %[ftmp4],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp3],   %[ftmp7],       %[ftmp9]                \n\t"
        "paddsh     %[ftmp4],   %[ftmp8],       %[ftmp10]               \n\t"
        "pmullh     %[ftmp3],   %[ftmp3],       %[ff_pw_20]             \n\t"
        "pmullh     %[ftmp4],   %[ftmp4],       %[ff_pw_20]             \n\t"
        "punpcklbh  %[ftmp7],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp10],  %[ftmp5],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp2],   %[ftmp7],       %[ftmp9]                \n\t"
        "paddsh     %[ftmp5],   %[ftmp8],       %[ftmp10]               \n\t"
        "pmullh     %[ftmp2],   %[ftmp2],       %[ff_pw_5]              \n\t"
        "pmullh     %[ftmp5],   %[ftmp5],       %[ff_pw_5]              \n\t"
        "punpcklbh  %[ftmp7],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp6],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp10],  %[ftmp6],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp1],   %[ftmp7],       %[ftmp9]                \n\t"
        "paddsh     %[ftmp6],   %[ftmp8],       %[ftmp10]               \n\t"
        "psubsh     %[ftmp3],   %[ftmp3],       %[ftmp2]                \n\t"
        "psubsh     %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[ff_pw_16]             \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[ff_pw_16]             \n\t"
        "psrah      %[ftmp3],   %[ftmp3],       %[ff_pw_5]              \n\t"
        "psrah      %[ftmp4],   %[ftmp4],       %[ff_pw_5]              \n\t"
        "packushb   %[ftmp9],   %[ftmp3],       %[ftmp4]                \n\t"
        "ldc1       %[ftmp10],  0x00(%[dst])                            \n\t"
        "pavgb      %[ftmp9],   %[ftmp9],       %[ftmp10]               \n\t"
        "sdc1       %[ftmp9],   0x00(%[dst])                            \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),
          [tmp0]"=&r"(tmp[0]),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [dstStride]"r"((mips_reg)dstStride),
          [srcStride]"r"((mips_reg)srcStride),
          [ff_pw_20]"f"(ff_pw_20),          [ff_pw_5]"f"(ff_pw_5),
          [ff_pw_16]"f"(ff_pw_16)
        : "memory"
    );
}

static void avg_h264_qpel16_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    avg_h264_qpel8_h_lowpass_mmi(dst, src, dstStride, srcStride);
    avg_h264_qpel8_h_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    avg_h264_qpel8_h_lowpass_mmi(dst, src, dstStride, srcStride);
    avg_h264_qpel8_h_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
}

static void put_h264_qpel4_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    double ftmp[12];
    uint64_t tmp[1];
    uint64_t low32;

    src -= 2 * srcStride;

    __asm__ volatile (
        ".set       push                                                \n\t"
        ".set       noreorder                                           \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "dli        %[tmp0],    0x02                                    \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp1]                                \n\t"
        "mtc1       %[tmp0],    %[ftmp10]                               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "dli        %[tmp0],    0x05                                    \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        "mtc1       %[tmp0],    %[ftmp11]                               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp3]                                \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp4]                                \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp5]                                \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp6]                                \n\t"
        "paddh      %[ftmp7],   %[ftmp3],       %[ftmp4]                \n\t"
        "psllh      %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
        "psubh      %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
        "psubh      %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "pmullh     %[ftmp7],   %[ftmp7],       %[ff_pw_5]              \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_16]             \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp6]                \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "psrah      %[ftmp7],   %[ftmp7],       %[ftmp11]               \n\t"
        "packushb   %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "swc1       %[ftmp7],   0x00(%[dst])                            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp1]                                \n\t"
        "paddh      %[ftmp7],   %[ftmp4],       %[ftmp5]                \n\t"
        "psllh      %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
        "psubh      %[ftmp7],   %[ftmp7],       %[ftmp3]                \n\t"
        "psubh      %[ftmp7],   %[ftmp7],       %[ftmp6]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "pmullh     %[ftmp7],   %[ftmp7],       %[ff_pw_5]              \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]             \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]                \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
        "psrah      %[ftmp7],   %[ftmp7],       %[ftmp11]               \n\t"
        "packushb   %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "swc1       %[ftmp7],   0x00(%[dst])                            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        "paddh      %[ftmp7],   %[ftmp5],       %[ftmp6]                \n\t"
        "psllh      %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
        "psubh      %[ftmp7],   %[ftmp7],       %[ftmp4]                \n\t"
        "psubh      %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "pmullh     %[ftmp7],   %[ftmp7],       %[ff_pw_5]              \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]             \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]                \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp3]                \n\t"
        "psrah      %[ftmp7],   %[ftmp7],       %[ftmp11]               \n\t"
        "packushb   %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "swc1       %[ftmp7],   0x00(%[dst])                            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp3]                                \n\t"
        "paddh      %[ftmp7],   %[ftmp6],       %[ftmp1]                \n\t"
        "psllh      %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
        "psubh      %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"
        "psubh      %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "pmullh     %[ftmp7],   %[ftmp7],       %[ff_pw_5]              \n\t"
        "paddh      %[ftmp4],   %[ftmp4],       %[ff_pw_16]             \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "paddh      %[ftmp4],   %[ftmp4],       %[ftmp3]                \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp4]                \n\t"
        "psrah      %[ftmp7],   %[ftmp7],       %[ftmp11]               \n\t"
        "packushb   %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "swc1       %[ftmp7],   0x00(%[dst])                            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        ".set       pop                                                 \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
          [tmp0]"=&r"(tmp[0]),
          [dst]"+&r"(dst),                  [src]"+&r"(src),
          [low32]"=&r"(low32)
        : [dstStride]"r"((mips_reg)dstStride),
          [srcStride]"r"((mips_reg)srcStride),
          [ff_pw_5]"f"(ff_pw_5),            [ff_pw_16]"f"(ff_pw_16)
        : "memory"
    );
}

static void put_h264_qpel8_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    int w = 2;
    int h = 8;
    double ftmp[10];
    uint64_t tmp[1];
    uint64_t low32;

    src -= 2 * srcStride;

    while (w--) {
        __asm__ volatile (
            ".set       push                                            \n\t"
            ".set       noreorder                                       \n\t"
            "dli        %[tmp0],    0x02                                \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "mtc1       %[tmp0],    %[ftmp8]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "dli        %[tmp0],    0x05                                \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "mtc1       %[tmp0],    %[ftmp9]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp3]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp4]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp7]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp5]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp2],       %[ftmp3]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp5]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp3],       %[ftmp4]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp4],       %[ftmp5]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]         \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp5],       %[ftmp0]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp0],       %[ftmp1]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp3]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "punpcklbh  %[ftmp3] ,  %[ftmp3],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ftmp3]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp1],       %[ftmp2]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp4]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp2],       %[ftmp3]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp5]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp5]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp3],       %[ftmp4]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "bne        %[h],       0x10,           2f                  \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp4],       %[ftmp5]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]         \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp5],       %[ftmp0]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp0],       %[ftmp1]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp3]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ftmp3]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp1],       %[ftmp2]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp4]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp2],       %[ftmp3]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp5]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp5]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp3],       %[ftmp4]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp4],       %[ftmp5]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "paddh      %[ftmp6],   %[ftmp5],       %[ftmp0]            \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "2:                                                         \n\t"
            ".set       pop                                             \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
              [tmp0]"=&r"(tmp[0]),
              [src]"+&r"(src),              [dst]"+&r"(dst),
              [h]"+&r"(h),
              [low32]"=&r"(low32)
            : [dstStride]"r"((mips_reg)dstStride),
              [srcStride]"r"((mips_reg)srcStride),
              [ff_pw_5]"f"(ff_pw_5),        [ff_pw_16]"f"(ff_pw_16)
            : "memory"
        );

        src += 4 - (h + 5) * srcStride;
        dst += 4 - h * dstStride;
    }
}

static void put_h264_qpel16_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    put_h264_qpel8_v_lowpass_mmi(dst, src, dstStride, srcStride);
    put_h264_qpel8_v_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    put_h264_qpel8_v_lowpass_mmi(dst, src, dstStride, srcStride);
    put_h264_qpel8_v_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
}

static void avg_h264_qpel4_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    double ftmp[10];
    uint64_t tmp[1];

    src -= 2 * srcStride;

    __asm__ volatile (
        ".set       push                                                \n\t"
        ".set       noreorder                                           \n\t"
        "dli        %[tmp0],    0x02                                    \n\t"
        "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "mtc1       %[tmp0],    %[ftmp9]                                \n\t"
        "dli        %[tmp0],    0x05                                    \n\t"
        "lwc1       %[ftmp0],   0x00(%[src])                            \n\t"
        "mtc1       %[tmp0],    %[ftmp8]                                \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "lwc1       %[ftmp1],   0x00(%[src])                            \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "lwc1       %[ftmp2],   0x00(%[src])                            \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "lwc1       %[ftmp3],   0x00(%[src])                            \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "lwc1       %[ftmp4],   0x00(%[src])                            \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]                \n\t"
        "lwc1       %[ftmp5],   0x00(%[src])                            \n\t"
        "paddh      %[ftmp6],   %[ftmp2],       %[ftmp3]                \n\t"
        "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]                \n\t"
        "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]              \n\t"
        "paddh      %[ftmp0],   %[ftmp0],       %[ff_pw_16]             \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "paddh      %[ftmp0],   %[ftmp0],       %[ftmp5]                \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "lwc1       %[ftmp0],   0x00(%[dst])                            \n\t"
        "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "swc1       %[ftmp6],   0x00(%[dst])                            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "lwc1       %[ftmp0],   0x00(%[src])                            \n\t"
        "paddh      %[ftmp6],   %[ftmp3],       %[ftmp4]                \n\t"
        "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]                \n\t"
        "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]                \n\t"
        "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]                \n\t"
        "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]              \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_16]             \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "lwc1       %[ftmp1],   0x00(%[dst])                            \n\t"
        "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        "swc1       %[ftmp6],   0x00(%[dst])                            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "lwc1       %[ftmp1],   0x00(%[src])                            \n\t"
        "paddh      %[ftmp6],   %[ftmp4],       %[ftmp5]                \n\t"
        "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]                \n\t"
        "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]                \n\t"
        "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]              \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]             \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]                \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]                \n\t"
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "lwc1       %[ftmp2],   0x00(%[dst])                            \n\t"
        "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp2]                \n\t"
        "swc1       %[ftmp6],   0x00(%[dst])                            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        "lwc1       %[ftmp2],   0x00(%[src])                            \n\t"
        "paddh      %[ftmp6],   %[ftmp5],       %[ftmp0]                \n\t"
        "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]                \n\t"
        "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]                \n\t"
        "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]              \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]             \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]                \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ftmp3]                \n\t"
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "lwc1       %[ftmp3],   0x00(%[dst])                            \n\t"
        "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp3]                \n\t"
        "swc1       %[ftmp6],   0x00(%[dst])                            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        ".set       pop                                                 \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp[0]),
          [src]"+&r"(src),              [dst]"+&r"(dst)
        : [dstStride]"r"((mips_reg)dstStride),
          [srcStride]"r"((mips_reg)srcStride),
          [ff_pw_5]"f"(ff_pw_5),        [ff_pw_16]"f"(ff_pw_16)
        : "memory"
    );
}

static void avg_h264_qpel8_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    int w = 2;
    int h = 8;
    double ftmp[10];
    uint64_t tmp[1];
    uint64_t low32;

    src -= 2 * srcStride;

    while (w--) {
        __asm__ volatile (
            ".set       push                                            \n\t"
            ".set       noreorder                                       \n\t"
            "dli        %[tmp0],    0x02                                \n\t"
            "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]            \n\t"
            "mtc1       %[tmp0],    %[ftmp9]                            \n\t"
            "dli        %[tmp0],    0x05                                \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "mtc1       %[tmp0],    %[ftmp8]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp3]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp4]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp7]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp5]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp2],       %[ftmp3]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp5]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp0],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp3],       %[ftmp4]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp1],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp4],       %[ftmp5]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp2],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp5],       %[ftmp0]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp3],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp3]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp0],       %[ftmp1]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ftmp3]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp4],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp4]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp1],       %[ftmp2]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp5],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp5]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp2],       %[ftmp3]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp5]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp0],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp3],       %[ftmp4]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp1],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "bne        %[h],       0x10,           2f                  \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp4],       %[ftmp5]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp2],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp5],       %[ftmp0]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp3],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp3]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp0],       %[ftmp1]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ftmp3]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp4],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp4]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp1],       %[ftmp2]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp5],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp5]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp2],       %[ftmp3]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp5]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp0],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp3],       %[ftmp4]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp1],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp4],       %[ftmp5]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp2],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp5],       %[ftmp0]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]         \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"
            "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
            "lwc1       %[ftmp3],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "swc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "2:                                                         \n\t"
            ".set       pop                                             \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
              [tmp0]"=&r"(tmp[0]),
              [src]"+&r"(src),              [dst]"+&r"(dst),
              [h]"+&r"(h),
              [low32]"=&r"(low32)
            : [dstStride]"r"((mips_reg)dstStride),
              [srcStride]"r"((mips_reg)srcStride),
              [ff_pw_5]"f"(ff_pw_5),        [ff_pw_16]"f"(ff_pw_16)
            : "memory"
        );

        src += 4 - (h + 5) * srcStride;
        dst += 4 - h * dstStride;
    }
}

static void avg_h264_qpel16_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    avg_h264_qpel8_v_lowpass_mmi(dst, src, dstStride, srcStride);
    avg_h264_qpel8_v_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    avg_h264_qpel8_v_lowpass_mmi(dst, src, dstStride, srcStride);
    avg_h264_qpel8_v_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
}

static void put_h264_qpel4_hv_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    INIT_CLIP
    int i;
    int16_t _tmp[36];
    int16_t *tmp = _tmp;
    double ftmp[10];
    uint64_t tmp0;
    uint64_t low32;

    src -= 2*srcStride;

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "dli        %[tmp0],    0x09                                    \n\t"
        "1:                                                             \n\t"
        "uld        %[low32],   -0x02(%[src])                           \n\t"
        "mtc1       %[low32],   %[ftmp1]                                \n\t"
        "uld        %[low32],   -0x01(%[src])                           \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp3]                                \n\t"
        "uld        %[low32],   0x01(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp4]                                \n\t"
        "uld        %[low32],   0x02(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp5]                                \n\t"
        "uld        %[low32],   0x03(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp6]                                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp7],   %[ftmp3],       %[ftmp4]                \n\t"
        "paddsh     %[ftmp8],   %[ftmp2],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp1],       %[ftmp6]                \n\t"
        "pmullh     %[ftmp7],   %[ftmp7],       %[ff_pw_20]             \n\t"
        "pmullh     %[ftmp8],   %[ftmp8],       %[ff_pw_5]              \n\t"
        "psubsh     %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp7],       %[ftmp9]                \n\t"
        "sdc1       %[ftmp9],   0x00(%[tmp])                            \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        PTR_ADDU   "%[tmp],     %[tmp],         %[tmpStride]            \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp0),
          [tmp]"+&r"(tmp),                  [src]"+&r"(src),
          [low32]"=&r"(low32)
        : [tmpStride]"r"(8),
          [srcStride]"r"((mips_reg)srcStride),
          [ff_pw_20]"f"(ff_pw_20),          [ff_pw_5]"f"(ff_pw_5)
        : "memory"
    );

    tmp -= 28;

    for (i=0; i<4; i++) {
        const int16_t tmpB= tmp[-8];
        const int16_t tmpA= tmp[-4];
        const int16_t tmp0= tmp[ 0];
        const int16_t tmp1= tmp[ 4];
        const int16_t tmp2= tmp[ 8];
        const int16_t tmp3= tmp[12];
        const int16_t tmp4= tmp[16];
        const int16_t tmp5= tmp[20];
        const int16_t tmp6= tmp[24];
        op2_put(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));
        op2_put(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));
        op2_put(dst[2*dstStride], (tmp2+tmp3)*20 - (tmp1+tmp4)*5 + (tmp0+tmp5));
        op2_put(dst[3*dstStride], (tmp3+tmp4)*20 - (tmp2+tmp5)*5 + (tmp1+tmp6));
        dst++;
        tmp++;
    }
}

static void put_h264_qpel8or16_hv1_lowpass_mmi(int16_t *tmp,
        const uint8_t *src, ptrdiff_t tmpStride, ptrdiff_t srcStride, int size)
{
    int w = (size + 8) >> 2;
    double ftmp[11];
    uint64_t tmp0;
    uint64_t low32;

    src -= 2 * srcStride + 2;

    while (w--) {
        __asm__ volatile (
            "dli        %[tmp0],    0x02                                \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "mtc1       %[tmp0],    %[ftmp10]                           \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp3]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp4]                            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp7]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp5]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp2],       %[ftmp3]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp5]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "sdc1       %[ftmp6],   0x00(%[tmp])                        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp3],       %[ftmp4]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "sdc1       %[ftmp6],   0x30(%[tmp])                        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp4],       %[ftmp5]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "sdc1       %[ftmp6],   0x60(%[tmp])                        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp5],       %[ftmp0]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "sdc1       %[ftmp6],   0x90(%[tmp])                        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp3]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp0],       %[ftmp1]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ftmp3]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "sdc1       %[ftmp6],   0xc0(%[tmp])                        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp4]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp1],       %[ftmp2]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "sdc1       %[ftmp6],   0xf0(%[tmp])                        \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp5]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp2],       %[ftmp3]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp5]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "sdc1       %[ftmp6],   0x120(%[tmp])                       \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp3],       %[ftmp4]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "sdc1       %[ftmp6],   0x150(%[tmp])                       \n\t"
            "bne        %[size],    0x10,           2f                  \n\t"

            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp4],       %[ftmp5]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "sdc1       %[ftmp6],   0x180(%[tmp])                       \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp5],       %[ftmp0]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "sdc1       %[ftmp6],   0x1b0(%[tmp])                       \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp3]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp0],       %[ftmp1]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ftmp3]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "sdc1       %[ftmp6],   0x1e0(%[tmp])                       \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp4]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp1],       %[ftmp2]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "sdc1       %[ftmp6],   0x210(%[tmp])                       \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp5]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp2],       %[ftmp3]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp5]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "sdc1       %[ftmp6],   0x240(%[tmp])                       \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp0]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp3],       %[ftmp4]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "sdc1       %[ftmp6],   0x270(%[tmp])                       \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp4],       %[ftmp5]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]            \n\t"
            "sdc1       %[ftmp6],   0x2a0(%[tmp])                       \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            "paddh      %[ftmp6],   %[ftmp5],       %[ftmp0]            \n\t"
            "psllh      %[ftmp6],   %[ftmp6],       %[ftmp10]           \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]         \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "psubh      %[ftmp6],   %[ftmp6],       %[ftmp1]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ff_pw_5]          \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[srcStride]        \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "sdc1       %[ftmp6],   0x2d0(%[tmp])                       \n\t"
            "2:                                                         \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
              [ftmp10]"=&f"(ftmp[10]),
              [tmp0]"=&r"(tmp0),
              [src]"+&r"(src),
              [low32]"=&r"(low32)
            : [tmp]"r"(tmp),                [size]"r"(size),
              [srcStride]"r"((mips_reg)srcStride),
              [ff_pw_5]"f"(ff_pw_5),        [ff_pw_16]"f"(ff_pw_16)
            : "memory"
        );

        tmp += 4;
        src += 4 - (size + 5) * srcStride;
    }
}

static void put_h264_qpel8or16_hv2_lowpass_mmi(uint8_t *dst,
        int16_t *tmp, ptrdiff_t dstStride, ptrdiff_t tmpStride, int size)
{
    int w = size >> 4;
    double ftmp[10];
    uint64_t tmp0;

    do {
        int h = size;

        __asm__ volatile (
            "dli        %[tmp0],    0x02                                \n\t"
            "mtc1       %[tmp0],    %[ftmp8]                            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "mtc1       %[tmp0],    %[ftmp9]                            \n\t"
            "1:                                                         \n\t"
            "ldc1       %[ftmp0],   0x00(%[tmp])                        \n\t"
            "ldc1       %[ftmp3],   0x08(%[tmp])                        \n\t"
            "ldc1       %[ftmp6],   0x10(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp1],   0x09(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp1],   0x02(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp4],   0x11(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp4],   0x0a(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp5],   0x19(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp5],   0x12(%[tmp])                        \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp4]            \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ftmp6]            \n\t"
            "gsldlc1    %[ftmp2],   0x0b(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp2],   0x04(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp6],   0x0d(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp6],   0x06(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp5],   0x13(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp5],   0x0c(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp7],   0x15(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp7],   0x0e(%[tmp])                        \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp6]            \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"
            "psubh      %[ftmp0],   %[ftmp0],       %[ftmp1]            \n\t"
            "psubh      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"
            "psrah      %[ftmp0],   %[ftmp0],       %[ftmp8]            \n\t"
            "psrah      %[ftmp3],   %[ftmp3],       %[ftmp8]            \n\t"
            "psubh      %[ftmp0],   %[ftmp0],       %[ftmp1]            \n\t"
            "psubh      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"
            "paddsh     %[ftmp0],   %[ftmp0],       %[ftmp2]            \n\t"
            "paddsh     %[ftmp3] ,  %[ftmp3],       %[ftmp5]            \n\t"
            "psrah      %[ftmp0],   %[ftmp0],       %[ftmp8]            \n\t"
            "psrah      %[ftmp3],   %[ftmp3],       %[ftmp8]            \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp2]            \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"
            "psrah      %[ftmp0],   %[ftmp0],       %[ftmp9]            \n\t"
            "psrah      %[ftmp3],   %[ftmp3],       %[ftmp9]            \n\t"
            "packushb   %[ftmp0],   %[ftmp0],       %[ftmp3]            \n\t"
            "addi       %[h],       %[h],           -0x01               \n\t"
            "gssdlc1    %[ftmp0],   0x07(%[dst])                        \n\t"
            "gssdrc1    %[ftmp0],   0x00(%[dst])                        \n\t"
            PTR_ADDIU  "%[tmp],     %[tmp],         0x30                \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
              [tmp0]"=&r"(tmp0),
              [tmp]"+&r"(tmp),              [dst]"+&r"(dst),
              [h]"+&r"(h)
            : [dstStride]"r"((mips_reg)dstStride)
            : "memory"
        );

        tmp += 8 - size * 24;
        dst += 8 - size * dstStride;
    } while (w--);
}

static void put_h264_qpel8or16_hv_lowpass_mmi(uint8_t *dst, int16_t *tmp,
        const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t tmpStride,
        ptrdiff_t srcStride, int size)
{
    put_h264_qpel8or16_hv1_lowpass_mmi(tmp, src, tmpStride, srcStride, size);
    put_h264_qpel8or16_hv2_lowpass_mmi(dst, tmp, dstStride, tmpStride, size);
}

static void put_h264_qpel8_hv_lowpass_mmi(uint8_t *dst, int16_t *tmp,
        const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t tmpStride,
        ptrdiff_t srcStride)
{
    put_h264_qpel8or16_hv_lowpass_mmi(dst, tmp, src, dstStride, tmpStride,
            srcStride, 8);
}

static void put_h264_qpel16_hv_lowpass_mmi(uint8_t *dst, int16_t *tmp,
        const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t tmpStride,
        ptrdiff_t srcStride)
{
    put_h264_qpel8or16_hv_lowpass_mmi(dst, tmp, src, dstStride, tmpStride,
            srcStride, 16);
}

static void put_h264_qpel8_h_lowpass_l2_mmi(uint8_t *dst, const uint8_t *src,
        const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t src2Stride)
{
    int h = 8;
    double ftmp[9];
    uint64_t tmp[1];
    uint64_t low32;

    __asm__ volatile (
        "dli        %[tmp0],    0x02                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp7]                                \n\t"
        "dli        %[tmp0],    0x05                                    \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "mtc1       %[tmp0],    %[ftmp8]                                \n\t"
        "1:                                                             \n\t"
        "gsldlc1    %[ftmp1],   0x07(%[src])                            \n\t"
        "gsldrc1    %[ftmp1],   0x00(%[src])                            \n\t"
        "gsldlc1    %[ftmp3],   0x08(%[src])                            \n\t"
        "gsldrc1    %[ftmp3],   0x01(%[src])                            \n\t"
        "punpckhbh  %[ftmp2],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp4],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "psllh      %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "psllh      %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "gsldlc1    %[ftmp3],   0x06(%[src])                            \n\t"
        "gsldrc1    %[ftmp3],   -0x01(%[src])                           \n\t"
        "gsldlc1    %[ftmp5],   0x09(%[src])                            \n\t"
        "gsldrc1    %[ftmp5],   0x02(%[src])                            \n\t"
        "punpckhbh  %[ftmp4],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp6],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ftmp4]                \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp5]                \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "pmullh     %[ftmp2],   %[ftmp2],       %[ff_pw_5]              \n\t"
        "pmullh     %[ftmp1],   %[ftmp1],       %[ff_pw_5]              \n\t"
        "uld        %[low32],   -0x02(%[src])                           \n\t"
        "mtc1       %[low32],   %[ftmp3]                                \n\t"
        "uld        %[low32],   0x07(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp6]                                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "paddh      %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_16]             \n\t"
        "paddh      %[ftmp5],   %[ftmp5],       %[ff_pw_16]             \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "psrah      %[ftmp1],   %[ftmp1],       %[ftmp8]                \n\t"
        "psrah      %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "gsldlc1    %[ftmp5],   0x07(%[src2])                           \n\t"
        "gsldrc1    %[ftmp5],   0x00(%[src2])                           \n\t"
        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        PTR_ADDU   "%[src],     %[src],         %[dstStride]            \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        PTR_ADDU   "%[h],       %[h],           -0x01                   \n\t"
        "sdc1       %[ftmp1],   0x00(%[dst])                            \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        PTR_ADDU   "%[src2],    %[src2],        %[src2Stride]           \n\t"
        "bgtz       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [tmp0]"=&r"(tmp[0]),
          [src]"+&r"(src),                  [dst]"+&r"(dst),
          [src2]"+&r"(src2),                [h]"+&r"(h),
          [low32]"=&r"(low32)
        : [src2Stride]"r"((mips_reg)src2Stride),
          [dstStride]"r"((mips_reg)dstStride),
          [ff_pw_5]"f"(ff_pw_5),            [ff_pw_16]"f"(ff_pw_16)
        : "memory"
    );
}

static void put_pixels8_l2_shift5_mmi(uint8_t *dst, int16_t *src16,
        const uint8_t *src8, ptrdiff_t dstStride, ptrdiff_t src8Stride, int h)
{
    double ftmp[7];
    uint64_t tmp0;

    do {
        __asm__ volatile (
            "dli        %[tmp0],    0x05                                \n\t"
            "gsldlc1    %[ftmp0],   0x07(%[src16])                      \n\t"
            "gsldrc1    %[ftmp0],   0x00(%[src16])                      \n\t"
            "mtc1       %[tmp0],    %[ftmp6]                            \n\t"
            "gsldlc1    %[ftmp1],   0x0f(%[src16])                      \n\t"
            "gsldrc1    %[ftmp1],   0x08(%[src16])                      \n\t"
            "gsldlc1    %[ftmp2],   0x37(%[src16])                      \n\t"
            "gsldrc1    %[ftmp2],   0x30(%[src16])                      \n\t"
            "gsldlc1    %[ftmp3],   0x3f(%[src16])                      \n\t"
            "gsldrc1    %[ftmp3],   0x38(%[src16])                      \n\t"
            "psrah      %[ftmp0],   %[ftmp0],       %[ftmp6]            \n\t"
            "psrah      %[ftmp1],   %[ftmp1],       %[ftmp6]            \n\t"
            "psrah      %[ftmp2],   %[ftmp2],       %[ftmp6]            \n\t"
            "psrah      %[ftmp3],   %[ftmp3],       %[ftmp6]            \n\t"
            "packushb   %[ftmp0],   %[ftmp0],       %[ftmp1]            \n\t"
            "packushb   %[ftmp2],   %[ftmp2],       %[ftmp3]            \n\t"
            "ldc1       %[ftmp5],   0x00(%[src8])                       \n\t"
            "gsldxc1    %[ftmp4],   0x00(%[src8],   %[src8Stride])      \n\t"
            "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp5]            \n\t"
            "pavgb      %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "sdc1       %[ftmp0],   0x00(%[dst])                        \n\t"
            "gssdxc1    %[ftmp2],   0x00(%[dst],    %[dstStride])       \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),
              [tmp0]"=&r"(tmp0)
            : [src8]"r"(src8),              [src16]"r"(src16),
              [dst]"r"(dst),
              [src8Stride]"r"((mips_reg)src8Stride),
              [dstStride]"r"((mips_reg)dstStride)
            : "memory"
        );

        src8  += 2 * src8Stride;
        src16 += 48;
        dst   += 2 * dstStride;
    } while (h -= 2);
}

static void put_h264_qpel16_h_lowpass_l2_mmi(uint8_t *dst, const uint8_t *src,
        const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t src2Stride)
{
    put_h264_qpel8_h_lowpass_l2_mmi(dst, src, src2, dstStride, src2Stride);
    put_h264_qpel8_h_lowpass_l2_mmi(dst + 8, src + 8, src2 + 8, dstStride,
            src2Stride);

    src += 8 * dstStride;
    dst += 8 * dstStride;
    src2 += 8 * src2Stride;

    put_h264_qpel8_h_lowpass_l2_mmi(dst, src, src2, dstStride, src2Stride);
    put_h264_qpel8_h_lowpass_l2_mmi(dst + 8, src + 8, src2 + 8, dstStride,
            src2Stride);
}

static void put_pixels16_l2_shift5_mmi(uint8_t *dst, int16_t *src16,
        const uint8_t *src8, ptrdiff_t dstStride, ptrdiff_t src8Stride, int h)
{
    put_pixels8_l2_shift5_mmi(dst, src16, src8, dstStride, src8Stride, h);
    put_pixels8_l2_shift5_mmi(dst + 8, src16 + 8, src8 + 8, dstStride,
            src8Stride, h);
}

static void avg_h264_qpel4_hv_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    INIT_CLIP
    int i;
    int16_t _tmp[36];
    int16_t *tmp = _tmp;
    double ftmp[10];
    uint64_t tmp0;
    uint64_t low32;

    src -= 2*srcStride;

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "dli        %[tmp0],    0x09                                    \n\t"
        "1:                                                             \n\t"
        "uld        %[low32],   -0x02(%[src])                           \n\t"
        "mtc1       %[low32],   %[ftmp1]                                \n\t"
        "uld        %[low32],   -0x01(%[src])                           \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        "uld        %[low32],   0x00(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp3]                                \n\t"
        "uld        %[low32],   0x01(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp4]                                \n\t"
        "uld        %[low32],   0x02(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp5]                                \n\t"
        "uld        %[low32],   0x03(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp6]                                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp7],   %[ftmp3],       %[ftmp4]                \n\t"
        "paddsh     %[ftmp8],   %[ftmp2],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp1],       %[ftmp6]                \n\t"
        "pmullh     %[ftmp7],   %[ftmp7],       %[ff_pw_20]             \n\t"
        "pmullh     %[ftmp8],   %[ftmp8],       %[ff_pw_5]              \n\t"
        "psubsh     %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp7],       %[ftmp9]                \n\t"
        "sdc1       %[ftmp9],   0x00(%[tmp])                            \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcStride]            \n\t"
        PTR_ADDU   "%[tmp],     %[tmp],         %[tmpStride]            \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp0),
          [tmp]"+&r"(tmp),                  [src]"+&r"(src),
          [low32]"=&r"(low32)
        : [tmpStride]"r"(8),
          [srcStride]"r"((mips_reg)srcStride),
          [ff_pw_20]"f"(ff_pw_20),          [ff_pw_5]"f"(ff_pw_5)
        : "memory"
    );

    tmp -= 28;

    for (i=0; i<4; i++) {
        const int16_t tmpB= tmp[-8];
        const int16_t tmpA= tmp[-4];
        const int16_t tmp0= tmp[ 0];
        const int16_t tmp1= tmp[ 4];
        const int16_t tmp2= tmp[ 8];
        const int16_t tmp3= tmp[12];
        const int16_t tmp4= tmp[16];
        const int16_t tmp5= tmp[20];
        const int16_t tmp6= tmp[24];
        op2_avg(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));
        op2_avg(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));
        op2_avg(dst[2*dstStride], (tmp2+tmp3)*20 - (tmp1+tmp4)*5 + (tmp0+tmp5));
        op2_avg(dst[3*dstStride], (tmp3+tmp4)*20 - (tmp2+tmp5)*5 + (tmp1+tmp6));
        dst++;
        tmp++;
    }
}

static void avg_h264_qpel8or16_hv2_lowpass_mmi(uint8_t *dst,
        int16_t *tmp, ptrdiff_t dstStride, ptrdiff_t tmpStride, int size)
{
    int w = size >> 4;
    double ftmp[11];
    uint64_t tmp0;

    do {
        int h = size;
        __asm__ volatile (
            "dli        %[tmp0],    0x02                                \n\t"
            "mtc1       %[tmp0],    %[ftmp9]                            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "mtc1       %[tmp0],    %[ftmp10]                           \n\t"
            "1:                                                         \n\t"
            "ldc1       %[ftmp0],   0x00(%[tmp])                        \n\t"
            "ldc1       %[ftmp3],   0x08(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp1],   0x09(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp1],   0x02(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp4],   0x11(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp4],   0x0a(%[tmp])                        \n\t"
            "ldc1       %[ftmp7],   0x10(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp8],   0x19(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp8],   0x12(%[tmp])                        \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp4]            \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp8]            \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ftmp7]            \n\t"
            "gsldlc1    %[ftmp2],   0x0b(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp2],   0x04(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp5],   0x13(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp5],   0x0c(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp7],   0x0d(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp7],   0x06(%[tmp])                        \n\t"
            "gsldlc1    %[ftmp8],   0x15(%[tmp])                        \n\t"
            "gsldrc1    %[ftmp8],   0x0e(%[tmp])                        \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ftmp8]            \n\t"
            "psubh      %[ftmp0],   %[ftmp0],       %[ftmp1]            \n\t"
            "psubh      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"
            "psrah      %[ftmp0],   %[ftmp0],       %[ftmp9]            \n\t"
            "psrah      %[ftmp3],   %[ftmp3],       %[ftmp9]            \n\t"
            "psubh      %[ftmp0],   %[ftmp0],       %[ftmp1]            \n\t"
            "psubh      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"
            "paddsh     %[ftmp0],   %[ftmp0],       %[ftmp2]            \n\t"
            "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"
            "psrah      %[ftmp0],   %[ftmp0],       %[ftmp9]            \n\t"
            "psrah      %[ftmp3],   %[ftmp3],       %[ftmp9]            \n\t"
            "paddh      %[ftmp0],   %[ftmp0],       %[ftmp2]            \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"
            "psrah      %[ftmp0],   %[ftmp0],       %[ftmp10]           \n\t"
            "psrah      %[ftmp3],   %[ftmp3],       %[ftmp10]           \n\t"
            "packushb   %[ftmp0],   %[ftmp0],       %[ftmp3]            \n\t"
            "ldc1       %[ftmp6],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp6]            \n\t"
            "sdc1       %[ftmp0],   0x00(%[dst])                        \n\t"
            "addi       %[h],       %[h],           -0x01               \n\t"
            PTR_ADDI   "%[tmp],     %[tmp],         0x30                \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[dstStride]        \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
              [ftmp10]"=&f"(ftmp[10]),
              [tmp0]"=&r"(tmp0),
              [tmp]"+&r"(tmp),              [dst]"+&r"(dst),
              [h]"+&r"(h)
            : [dstStride]"r"((mips_reg)dstStride)
            : "memory"
        );

        tmp += 8 - size * 24;
        dst += 8 - size * dstStride;
    } while (w--);
}

static void avg_h264_qpel8or16_hv_lowpass_mmi(uint8_t *dst, int16_t *tmp,
        const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t tmpStride,
        ptrdiff_t srcStride, int size)
{
    put_h264_qpel8or16_hv1_lowpass_mmi(tmp, src, tmpStride, srcStride, size);
    avg_h264_qpel8or16_hv2_lowpass_mmi(dst, tmp, dstStride, tmpStride, size);
}

static void avg_h264_qpel8_hv_lowpass_mmi(uint8_t *dst, int16_t *tmp,
        const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t tmpStride,
        ptrdiff_t srcStride)
{
    avg_h264_qpel8or16_hv_lowpass_mmi(dst, tmp, src, dstStride, tmpStride,
            srcStride, 8);
}

static void avg_h264_qpel16_hv_lowpass_mmi(uint8_t *dst, int16_t *tmp,
        const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t tmpStride,
        ptrdiff_t srcStride)
{
    avg_h264_qpel8or16_hv_lowpass_mmi(dst, tmp, src, dstStride, tmpStride,
            srcStride, 16);
}

static void avg_h264_qpel8_h_lowpass_l2_mmi(uint8_t *dst, const uint8_t *src,
        const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t src2Stride)
{
    double ftmp[10];
    uint64_t tmp[2];
    uint64_t low32;

    __asm__ volatile (
        "dli        %[tmp1],    0x02                                    \n\t"
        "ori        %[tmp0],    $0,             0x8                     \n\t"
        "mtc1       %[tmp1],    %[ftmp7]                                \n\t"
        "dli        %[tmp1],    0x05                                    \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "mtc1       %[tmp1],    %[ftmp8]                                \n\t"
        "1:                                                             \n\t"
        "gsldlc1    %[ftmp1],   0x07(%[src])                            \n\t"
        "gsldrc1    %[ftmp1],   0x00(%[src])                            \n\t"
        "gsldlc1    %[ftmp2],   0x08(%[src])                            \n\t"
        "gsldrc1    %[ftmp2],   0x01(%[src])                            \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp4],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "psllh      %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "psllh      %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "gsldlc1    %[ftmp2],   0x06(%[src])                            \n\t"
        "gsldrc1    %[ftmp2],   -0x01(%[src])                           \n\t"
        "gsldlc1    %[ftmp5],   0x09(%[src])                            \n\t"
        "gsldrc1    %[ftmp5],   0x02(%[src])                            \n\t"
        "punpckhbh  %[ftmp4],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp6],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ftmp4]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp6]                \n\t"
        "pmullh     %[ftmp1],   %[ftmp1],       %[ff_pw_5]              \n\t"
        "pmullh     %[ftmp3],   %[ftmp3],       %[ff_pw_5]              \n\t"
        "uld        %[low32],   -0x02(%[src])                           \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        "uld        %[low32],   0x07(%[src])                            \n\t"
        "mtc1       %[low32],   %[ftmp6]                                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "paddh      %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_16]             \n\t"
        "paddh      %[ftmp5],   %[ftmp5],       %[ff_pw_16]             \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp5]                \n\t"
        "psrah      %[ftmp1],   %[ftmp1],       %[ftmp8]                \n\t"
        "psrah      %[ftmp3],   %[ftmp3],       %[ftmp8]                \n\t"
        "gsldlc1    %[ftmp5],   0x07(%[src2])                           \n\t"
        "gsldrc1    %[ftmp5],   0x00(%[src2])                           \n\t"
        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "ldc1       %[ftmp9],   0x00(%[dst])                            \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp9]                \n\t"
        PTR_ADDU   "%[src],     %[src],         %[dstStride]            \n\t"
        "sdc1       %[ftmp1],   0x00(%[dst])                            \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstStride]            \n\t"
        PTR_ADDU   "%[src2],    %[src2],        %[src2Stride]           \n\t"
        "bgtz       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          [dst]"+&r"(dst),                  [src]"+&r"(src),
          [src2]"+&r"(src2),
          [low32]"=&r"(low32)
        : [dstStride]"r"((mips_reg)dstStride),
          [src2Stride]"r"((mips_reg)src2Stride),
          [ff_pw_5]"f"(ff_pw_5),            [ff_pw_16]"f"(ff_pw_16)
        : "memory"
    );
}

static void avg_h264_qpel16_h_lowpass_l2_mmi(uint8_t *dst, const uint8_t *src,
        const uint8_t *src2, ptrdiff_t dstStride, ptrdiff_t src2Stride)
{
    avg_h264_qpel8_h_lowpass_l2_mmi(dst, src, src2, dstStride, src2Stride);
    avg_h264_qpel8_h_lowpass_l2_mmi(dst + 8, src + 8, src2 + 8, dstStride,
            src2Stride);

    src += 8 * dstStride;
    dst += 8 * dstStride;
    src2 += 8 * src2Stride;

    avg_h264_qpel8_h_lowpass_l2_mmi(dst, src, src2, dstStride, src2Stride);
    avg_h264_qpel8_h_lowpass_l2_mmi(dst + 8, src + 8, src2 + 8, dstStride,
            src2Stride);
}

static void avg_pixels8_l2_shift5_mmi(uint8_t *dst, int16_t *src16,
        const uint8_t *src8, ptrdiff_t dstStride, ptrdiff_t src8Stride, int b)
{
    double ftmp[8];
    uint64_t tmp0;

    do {
        __asm__ volatile (
            "dli        %[tmp0],    0x05                                \n\t"
            "gsldlc1    %[ftmp0],   0x07(%[src16])                      \n\t"
            "gsldrc1    %[ftmp0],   0x00(%[src16])                      \n\t"
            "mtc1       %[tmp0],    %[ftmp6]                            \n\t"
            "gsldlc1    %[ftmp1],   0x0f(%[src16])                      \n\t"
            "gsldrc1    %[ftmp1],   0x08(%[src16])                      \n\t"
            "gsldlc1    %[ftmp2],   0x37(%[src16])                      \n\t"
            "gsldrc1    %[ftmp2],   0x30(%[src16])                      \n\t"
            "gsldlc1    %[ftmp3],   0x3f(%[src16])                      \n\t"
            "gsldrc1    %[ftmp3],   0x38(%[src16])                      \n\t"
            "psrah      %[ftmp0],   %[ftmp0],       %[ftmp6]            \n\t"
            "psrah      %[ftmp1],   %[ftmp1],       %[ftmp6]            \n\t"
            "psrah      %[ftmp2],   %[ftmp2],       %[ftmp6]            \n\t"
            "psrah      %[ftmp3],   %[ftmp3],       %[ftmp6]            \n\t"
            "packushb   %[ftmp0],   %[ftmp0],       %[ftmp1]            \n\t"
            "ldc1       %[ftmp4],   0x00(%[src8])                       \n\t"
            "gsldxc1    %[ftmp5],   0x00(%[src8],   %[src8Stride])      \n\t"
            "packushb   %[ftmp2],   %[ftmp2],       %[ftmp3]            \n\t"
            "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp4]            \n\t"
            "pavgb      %[ftmp2],   %[ftmp2],       %[ftmp5]            \n\t"
            "ldc1       %[ftmp7],   0x00(%[dst])                        \n\t"
            "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp7]            \n\t"
            "sdc1       %[ftmp0],   0x00(%[dst])                        \n\t"
            "gsldxc1    %[ftmp7],   0x00(%[dst],    %[dstStride])       \n\t"
            "pavgb      %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "gssdxc1    %[ftmp2],   0x00(%[dst],    %[dstStride])       \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [tmp0]"=&r"(tmp0)
            : [src8]"r"(src8),              [src16]"r"(src16),
              [dst]"r"(dst),
              [src8Stride]"r"((mips_reg)src8Stride),
              [dstStride]"r"((mips_reg)dstStride)
            : "memory"
        );

        src8  += 2 * src8Stride;
        src16 += 48;
        dst   += 2 * dstStride;
    } while (b -= 2);
}

static void avg_pixels16_l2_shift5_mmi(uint8_t *dst, int16_t *src16,
        const uint8_t *src8, ptrdiff_t dstStride, ptrdiff_t src8Stride, int b)
{
    avg_pixels8_l2_shift5_mmi(dst, src16, src8, dstStride, src8Stride, b);
    avg_pixels8_l2_shift5_mmi(dst + 8, src16 + 8, src8 + 8, dstStride,
            src8Stride, b);
}

//DEF_H264_MC_MMI(put_, 4)
void ff_put_h264_qpel4_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    ff_put_pixels4_8_mmi(dst, src, stride, 4);
}

void ff_put_h264_qpel4_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[16];
    put_h264_qpel4_h_lowpass_mmi(half, src, 4, stride);
    ff_put_pixels4_l2_8_mmi(dst, src, half, stride, stride, 4, 4);
}

void ff_put_h264_qpel4_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_h264_qpel4_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_put_h264_qpel4_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[16];
    put_h264_qpel4_h_lowpass_mmi(half, src, 4, stride);
    ff_put_pixels4_l2_8_mmi(dst, src+1, half, stride, stride, 4, 4);
}

void ff_put_h264_qpel4_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t half[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(half, full_mid, 4, 4);
    ff_put_pixels4_l2_8_mmi(dst, full_mid, half, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(dst, full_mid, stride, 4);
}

void ff_put_h264_qpel4_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t half[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(half, full_mid, 4, 4);
    ff_put_pixels4_l2_8_mmi(dst, full_mid+4, half, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    ff_put_pixels4_l2_8_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    ff_put_pixels4_l2_8_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    ff_put_pixels4_l2_8_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    ff_put_pixels4_l2_8_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_h264_qpel4_hv_lowpass_mmi(dst, src, stride, stride);
}

void ff_put_h264_qpel4_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[16];
    uint8_t halfHV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    ff_put_pixels4_l2_8_mmi(dst, halfH, halfHV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[16];
    uint8_t halfHV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    ff_put_pixels4_l2_8_mmi(dst, halfH, halfHV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfV[16];
    uint8_t halfHV[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    ff_put_pixels4_l2_8_mmi(dst, halfV, halfHV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfV[16];
    uint8_t halfHV[16];
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    ff_put_pixels4_l2_8_mmi(dst, halfV, halfHV, stride, 4, 4, 4);
}

//DEF_H264_MC_MMI(avg_, 4)
void ff_avg_h264_qpel4_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    ff_avg_pixels4_8_mmi(dst, src, stride, 4);
}

void ff_avg_h264_qpel4_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[16];
    put_h264_qpel4_h_lowpass_mmi(half, src, 4, stride);
    ff_avg_pixels4_l2_8_mmi(dst, src, half, stride, stride, 4, 4);
}

void ff_avg_h264_qpel4_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_h264_qpel4_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_avg_h264_qpel4_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[16];
    put_h264_qpel4_h_lowpass_mmi(half, src, 4, stride);
    ff_avg_pixels4_l2_8_mmi(dst, src+1, half, stride, stride, 4, 4);
}

void ff_avg_h264_qpel4_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t half[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(half, full_mid, 4, 4);
    ff_avg_pixels4_l2_8_mmi(dst, full_mid, half, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    avg_h264_qpel4_v_lowpass_mmi(dst, full_mid, stride, 4);
}

void ff_avg_h264_qpel4_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t half[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(half, full_mid, 4, 4);
    ff_avg_pixels4_l2_8_mmi(dst, full_mid+4, half, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    ff_avg_pixels4_l2_8_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    ff_avg_pixels4_l2_8_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    ff_avg_pixels4_l2_8_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    ff_avg_pixels4_l2_8_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_h264_qpel4_hv_lowpass_mmi(dst, src, stride, stride);
}

void ff_avg_h264_qpel4_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[16];
    uint8_t halfHV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    ff_avg_pixels4_l2_8_mmi(dst, halfH, halfHV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[16];
    uint8_t halfHV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    ff_avg_pixels4_l2_8_mmi(dst, halfH, halfHV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfV[16];
    uint8_t halfHV[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    ff_avg_pixels4_l2_8_mmi(dst, halfV, halfHV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfV[16];
    uint8_t halfHV[16];
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    ff_avg_pixels4_l2_8_mmi(dst, halfV, halfHV, stride, 4, 4, 4);
}

//DEF_H264_MC_MMI(put_, 8)
void ff_put_h264_qpel8_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    ff_put_pixels8_8_mmi(dst, src, stride, 8);
}

void ff_put_h264_qpel8_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[64];
    put_h264_qpel8_h_lowpass_mmi(half, src, 8, stride);
    ff_put_pixels8_l2_8_mmi(dst, src, half, stride, stride, 8, 8);
}

void ff_put_h264_qpel8_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_h264_qpel8_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_put_h264_qpel8_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[64];
    put_h264_qpel8_h_lowpass_mmi(half, src, 8, stride);
    ff_put_pixels8_l2_8_mmi(dst, src+1, half, stride, stride, 8, 8);
}

void ff_put_h264_qpel8_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t half[64];
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(half, full_mid, 8, 8);
    ff_put_pixels8_l2_8_mmi(dst, full_mid, half, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(dst, full_mid, stride, 8);
}

void ff_put_h264_qpel8_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t half[64];
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(half, full_mid, 8, 8);
    ff_put_pixels8_l2_8_mmi(dst, full_mid+8, half, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src, 8, stride);
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    ff_put_pixels8_l2_8_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src, 8, stride);
    copy_block8_mmi(full, src - stride*2 + 1, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    ff_put_pixels8_l2_8_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src + stride, 8, stride);
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    ff_put_pixels8_l2_8_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src + stride, 8, stride);
    copy_block8_mmi(full, src - stride*2 + 1, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    ff_put_pixels8_l2_8_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint16_t __attribute__ ((aligned(8))) temp[192];

    put_h264_qpel8_hv_lowpass_mmi(dst, temp, src, stride, 8, stride);
}

void ff_put_h264_qpel8_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[448];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 64);

    put_h264_qpel8_hv_lowpass_mmi(halfHV, halfV, src, 8, 8, stride);
    put_h264_qpel8_h_lowpass_l2_mmi(dst, src, halfHV, stride, 8);
}

void ff_put_h264_qpel8_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[448];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 64);

    put_h264_qpel8_hv_lowpass_mmi(halfHV, halfV, src, 8, 8, stride);
    put_h264_qpel8_h_lowpass_l2_mmi(dst, src + stride, halfHV, stride, 8);
}

void ff_put_h264_qpel8_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[448];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 64);

    put_h264_qpel8_hv_lowpass_mmi(halfHV, halfV, src, 8, 8, stride);
    put_pixels8_l2_shift5_mmi(dst, halfV + 2, halfHV, stride, 8, 8);
}

void ff_put_h264_qpel8_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[448];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 64);

    put_h264_qpel8_hv_lowpass_mmi(halfHV, halfV, src, 8, 8, stride);
    put_pixels8_l2_shift5_mmi(dst, halfV + 3, halfHV, stride, 8, 8);
}

//DEF_H264_MC_MMI(avg_, 8)
void ff_avg_h264_qpel8_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    ff_avg_pixels8_8_mmi(dst, src, stride, 8);
}

void ff_avg_h264_qpel8_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[64];
    put_h264_qpel8_h_lowpass_mmi(half, src, 8, stride);
    ff_avg_pixels8_l2_8_mmi(dst, src, half, stride, stride, 8, 8);
}

void ff_avg_h264_qpel8_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_h264_qpel8_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_avg_h264_qpel8_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[64];
    put_h264_qpel8_h_lowpass_mmi(half, src, 8, stride);
    ff_avg_pixels8_l2_8_mmi(dst, src+1, half, stride, stride, 8, 8);
}

void ff_avg_h264_qpel8_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t half[64];
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(half, full_mid, 8, 8);
    ff_avg_pixels8_l2_8_mmi(dst, full_mid, half, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    avg_h264_qpel8_v_lowpass_mmi(dst, full_mid, stride, 8);
}

void ff_avg_h264_qpel8_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t half[64];
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(half, full_mid, 8, 8);
    ff_avg_pixels8_l2_8_mmi(dst, full_mid+8, half, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src, 8, stride);
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    ff_avg_pixels8_l2_8_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src, 8, stride);
    copy_block8_mmi(full, src - stride*2 + 1, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    ff_avg_pixels8_l2_8_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src + stride, 8, stride);
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    ff_avg_pixels8_l2_8_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src + stride, 8, stride);
    copy_block8_mmi(full, src - stride*2 + 1, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    ff_avg_pixels8_l2_8_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint16_t __attribute__ ((aligned(8))) temp[192];

    avg_h264_qpel8_hv_lowpass_mmi(dst, temp, src, stride, 8, stride);
}

void ff_avg_h264_qpel8_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[448];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 64);

    put_h264_qpel8_hv_lowpass_mmi(halfHV, halfV, src, 8, 8, stride);
    avg_h264_qpel8_h_lowpass_l2_mmi(dst, src, halfHV, stride, 8);
}

void ff_avg_h264_qpel8_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[448];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 64);

    put_h264_qpel8_hv_lowpass_mmi(halfHV, halfV, src, 8, 8, stride);
    avg_h264_qpel8_h_lowpass_l2_mmi(dst, src + stride, halfHV, stride, 8);
}

void ff_avg_h264_qpel8_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[448];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 64);

    put_h264_qpel8_hv_lowpass_mmi(halfHV, halfV, src, 8, 8, stride);
    avg_pixels8_l2_shift5_mmi(dst, halfV + 2, halfHV, stride, 8, 8);
}

void ff_avg_h264_qpel8_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[448];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 64);

    put_h264_qpel8_hv_lowpass_mmi(halfHV, halfV, src, 8, 8, stride);
    avg_pixels8_l2_shift5_mmi(dst, halfV + 3, halfHV, stride, 8, 8);
}

//DEF_H264_MC_MMI(put_, 16)
void ff_put_h264_qpel16_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    ff_put_pixels16_8_mmi(dst, src, stride, 16);
}

void ff_put_h264_qpel16_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[256];
    put_h264_qpel16_h_lowpass_mmi(half, src, 16, stride);
    ff_put_pixels16_l2_8_mmi(dst, src, half, stride, stride, 16, 16);
}

void ff_put_h264_qpel16_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_h264_qpel16_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_put_h264_qpel16_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[256];
    put_h264_qpel16_h_lowpass_mmi(half, src, 16, stride);
    ff_put_pixels16_l2_8_mmi(dst, src+1, half, stride, stride, 16, 16);
}

void ff_put_h264_qpel16_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t half[256];
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(half, full_mid, 16, 16);
    ff_put_pixels16_l2_8_mmi(dst, full_mid, half, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(dst, full_mid, stride, 16);
}

void ff_put_h264_qpel16_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t half[256];
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(half, full_mid, 16, 16);
    ff_put_pixels16_l2_8_mmi(dst, full_mid+16, half, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src, 16, stride);
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    ff_put_pixels16_l2_8_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src, 16, stride);
    copy_block16_mmi(full, src - stride*2 + 1, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    ff_put_pixels16_l2_8_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src + stride, 16, stride);
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    ff_put_pixels16_l2_8_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src + stride, 16, stride);
    copy_block16_mmi(full, src - stride*2 + 1, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    ff_put_pixels16_l2_8_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint16_t __attribute__ ((aligned(8))) temp[384];

    put_h264_qpel16_hv_lowpass_mmi(dst, temp, src, stride, 16, stride);
}

void ff_put_h264_qpel16_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[1024];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 256);

    put_h264_qpel16_hv_lowpass_mmi(halfHV, halfV, src, 16, 16, stride);
    put_h264_qpel16_h_lowpass_l2_mmi(dst, src, halfHV, stride, 16);
}

void ff_put_h264_qpel16_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[1024];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 256);

    put_h264_qpel16_hv_lowpass_mmi(halfHV, halfV, src, 16, 16, stride);
    put_h264_qpel16_h_lowpass_l2_mmi(dst, src + stride, halfHV, stride, 16);
}

void ff_put_h264_qpel16_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[1024];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 256);

    put_h264_qpel16_hv_lowpass_mmi(halfHV, halfV, src, 16, 16, stride);
    put_pixels16_l2_shift5_mmi(dst, halfV + 2, halfHV, stride, 16, 16);
}

void ff_put_h264_qpel16_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[1024];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 256);

    put_h264_qpel16_hv_lowpass_mmi(halfHV, halfV, src, 16, 16, stride);
    put_pixels16_l2_shift5_mmi(dst, halfV + 3, halfHV, stride, 16, 16);
}

//DEF_H264_MC_MMI(avg_, 16)
void ff_avg_h264_qpel16_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    ff_avg_pixels16_8_mmi(dst, src, stride, 16);
}

void ff_avg_h264_qpel16_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[256];
    put_h264_qpel16_h_lowpass_mmi(half, src, 16, stride);
    ff_avg_pixels16_l2_8_mmi(dst, src, half, stride, stride, 16, 16);
}

void ff_avg_h264_qpel16_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_h264_qpel16_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_avg_h264_qpel16_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[256];
    put_h264_qpel16_h_lowpass_mmi(half, src, 16, stride);
    ff_avg_pixels16_l2_8_mmi(dst, src+1, half, stride, stride, 16, 16);
}

void ff_avg_h264_qpel16_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t half[256];
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(half, full_mid, 16, 16);
    ff_avg_pixels16_l2_8_mmi(dst, full_mid, half, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    avg_h264_qpel16_v_lowpass_mmi(dst, full_mid, stride, 16);
}

void ff_avg_h264_qpel16_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t half[256];
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(half, full_mid, 16, 16);
    ff_avg_pixels16_l2_8_mmi(dst, full_mid+16, half, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src, 16, stride);
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    ff_avg_pixels16_l2_8_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src, 16, stride);
    copy_block16_mmi(full, src - stride*2 + 1, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    ff_avg_pixels16_l2_8_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src + stride, 16, stride);
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    ff_avg_pixels16_l2_8_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src + stride, 16, stride);
    copy_block16_mmi(full, src - stride*2 + 1, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    ff_avg_pixels16_l2_8_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint16_t __attribute__ ((aligned(8))) temp[384];

    avg_h264_qpel16_hv_lowpass_mmi(dst, temp, src, stride, 16, stride);
}

void ff_avg_h264_qpel16_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[1024];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 256);

    put_h264_qpel16_hv_lowpass_mmi(halfHV, halfV, src, 16, 16, stride);
    avg_h264_qpel16_h_lowpass_l2_mmi(dst, src, halfHV, stride, 16);
}

void ff_avg_h264_qpel16_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[1024];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 256);

    put_h264_qpel16_hv_lowpass_mmi(halfHV, halfV, src, 16, 16, stride);
    avg_h264_qpel16_h_lowpass_l2_mmi(dst, src + stride, halfHV, stride, 16);
}

void ff_avg_h264_qpel16_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[1024];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 256);

    put_h264_qpel16_hv_lowpass_mmi(halfHV, halfV, src, 16, 16, stride);
    avg_pixels16_l2_shift5_mmi(dst, halfV + 2, halfHV, stride, 16, 16);
}

void ff_avg_h264_qpel16_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t __attribute__ ((aligned(8))) temp[1024];
    uint8_t *const halfHV = temp;
    int16_t *const halfV = (int16_t *) (temp + 256);

    put_h264_qpel16_hv_lowpass_mmi(halfHV, halfV, src, 16, 16, stride);
    avg_pixels16_l2_shift5_mmi(dst, halfV + 3, halfHV, stride, 16, 16);
}

#undef op2_avg
#undef op2_put
