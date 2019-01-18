/*
 * Copyright (c) 2019 Shiyou Yin (yinshiyou-hf@loongson.cn)
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

#include "libavcodec/bit_depth_template.c"
#include "libavcodec/mips/hevcdsp_mips.h"
#include "libavutil/mips/mmiutils.h"

#define PUT_HEVC_PEL_BI_PIXELS(w, x_step, src_step, dst_step, src2_step)  \
void ff_hevc_put_hevc_pel_bi_pixels##w##_8_mmi(uint8_t *_dst,             \
                                               ptrdiff_t _dststride,      \
                                               uint8_t *_src,             \
                                               ptrdiff_t _srcstride,      \
                                               int16_t *src2, int height, \
                                               intptr_t mx, intptr_t my,  \
                                               int width)                 \
{                                                                         \
    int x, y;                                                             \
    pixel *src          = (pixel *)_src;                                  \
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);                     \
    pixel *dst          = (pixel *)_dst;                                  \
    ptrdiff_t dststride = _dststride / sizeof(pixel);                     \
    uint64_t ftmp[12];                                                    \
    uint64_t rtmp[1];                                                     \
    int shift = 7;                                                        \
                                                                          \
    y = height;                                                           \
    x = width >> 3;                                                       \
    __asm__ volatile(                                                     \
        "xor          %[ftmp0],      %[ftmp0],      %[ftmp0]    \n\t"     \
        "li           %[rtmp0],      0x06                       \n\t"     \
        "dmtc1        %[rtmp0],      %[ftmp1]                   \n\t"     \
        "li           %[rtmp0],      0x10                       \n\t"     \
        "dmtc1        %[rtmp0],      %[ftmp10]                  \n\t"     \
        "li           %[rtmp0],      0x40                       \n\t"     \
        "dmtc1        %[rtmp0],      %[offset]                  \n\t"     \
        "punpcklhw    %[offset],     %[offset],     %[offset]   \n\t"     \
        "punpcklwd    %[offset],     %[offset],     %[offset]   \n\t"     \
                                                                          \
        "1:                                                     \n\t"     \
        "2:                                                     \n\t"     \
        "gsldlc1      %[ftmp5],      0x07(%[src])               \n\t"     \
        "gsldrc1      %[ftmp5],      0x00(%[src])               \n\t"     \
        "gsldlc1      %[ftmp2],      0x07(%[src2])              \n\t"     \
        "gsldrc1      %[ftmp2],      0x00(%[src2])              \n\t"     \
        "gsldlc1      %[ftmp3],      0x0f(%[src2])              \n\t"     \
        "gsldrc1      %[ftmp3],      0x08(%[src2])              \n\t"     \
        "punpcklbh    %[ftmp4],      %[ftmp5],      %[ftmp0]    \n\t"     \
        "punpckhbh    %[ftmp5],      %[ftmp5],      %[ftmp0]    \n\t"     \
        "psllh        %[ftmp4],      %[ftmp4],      %[ftmp1]    \n\t"     \
        "psllh        %[ftmp5],      %[ftmp5],      %[ftmp1]    \n\t"     \
        "paddh        %[ftmp4],      %[ftmp4],      %[offset]   \n\t"     \
        "paddh        %[ftmp5],      %[ftmp5],      %[offset]   \n\t"     \
        "punpcklhw    %[ftmp6],      %[ftmp4],      %[ftmp0]    \n\t"     \
        "punpckhhw    %[ftmp7],      %[ftmp4],      %[ftmp0]    \n\t"     \
        "punpcklhw    %[ftmp8],      %[ftmp5],      %[ftmp0]    \n\t"     \
        "punpckhhw    %[ftmp9],      %[ftmp5],      %[ftmp0]    \n\t"     \
        "punpcklhw    %[ftmp4],      %[ftmp0],      %[ftmp3]    \n\t"     \
        "punpckhhw    %[ftmp5],      %[ftmp0],      %[ftmp3]    \n\t"     \
        "punpckhhw    %[ftmp3],      %[ftmp0],      %[ftmp2]    \n\t"     \
        "punpcklhw    %[ftmp2],      %[ftmp0],      %[ftmp2]    \n\t"     \
        "psraw        %[ftmp2],      %[ftmp2],      %[ftmp10]   \n\t"     \
        "psraw        %[ftmp3],      %[ftmp3],      %[ftmp10]   \n\t"     \
        "psraw        %[ftmp4],      %[ftmp4],      %[ftmp10]   \n\t"     \
        "psraw        %[ftmp5],      %[ftmp5],      %[ftmp10]   \n\t"     \
        "paddw        %[ftmp2],      %[ftmp2],      %[ftmp6]    \n\t"     \
        "paddw        %[ftmp3],      %[ftmp3],      %[ftmp7]    \n\t"     \
        "paddw        %[ftmp4],      %[ftmp4],      %[ftmp8]    \n\t"     \
        "paddw        %[ftmp5],      %[ftmp5],      %[ftmp9]    \n\t"     \
        "psraw        %[ftmp2],      %[ftmp2],      %[shift]    \n\t"     \
        "psraw        %[ftmp3],      %[ftmp3],      %[shift]    \n\t"     \
        "psraw        %[ftmp4],      %[ftmp4],      %[shift]    \n\t"     \
        "psraw        %[ftmp5],      %[ftmp5],      %[shift]    \n\t"     \
        "packsswh     %[ftmp2],      %[ftmp2],      %[ftmp3]    \n\t"     \
        "packsswh     %[ftmp4],      %[ftmp4],      %[ftmp5]    \n\t"     \
        "pcmpgth      %[ftmp3],      %[ftmp2],      %[ftmp0]    \n\t"     \
        "pcmpgth      %[ftmp5],      %[ftmp4],      %[ftmp0]    \n\t"     \
        "and          %[ftmp2],      %[ftmp2],      %[ftmp3]    \n\t"     \
        "and          %[ftmp4],      %[ftmp4],      %[ftmp5]    \n\t"     \
        "packushb     %[ftmp2],      %[ftmp2],      %[ftmp4]    \n\t"     \
        "gssdlc1      %[ftmp2],      0x07(%[dst])               \n\t"     \
        "gssdrc1      %[ftmp2],      0x00(%[dst])               \n\t"     \
                                                                          \
        "daddi        %[x],          %[x],         -0x01        \n\t"     \
        PTR_ADDIU    "%[src],        %[src],        0x08        \n\t"     \
        PTR_ADDIU    "%[dst],        %[dst],        0x08        \n\t"     \
        PTR_ADDIU    "%[src2],       %[src2],       0x10        \n\t"     \
        "bnez         %[x],          2b                         \n\t"     \
                                                                          \
        PTR_ADDIU    "%[src],        %[src],     " #src_step "  \n\t"     \
        PTR_ADDIU    "%[dst],        %[dst],     " #dst_step "  \n\t"     \
        PTR_ADDIU    "%[src2],       %[src2],    " #src2_step " \n\t"     \
        "li           %[x],        " #x_step "                  \n\t"     \
        "daddi        %[y],          %[y],         -0x01        \n\t"     \
        PTR_ADDU     "%[src],        %[src],       %[srcstride] \n\t"     \
        PTR_ADDU     "%[dst],        %[dst],       %[dststride] \n\t"     \
        PTR_ADDIU    "%[src2],       %[src2],       0x80        \n\t"     \
        "bnez         %[y],          1b                         \n\t"     \
        : [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                   \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                   \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                   \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                   \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                   \
          [ftmp10]"=&f"(ftmp[10]), [offset]"=&f"(ftmp[11]),               \
          [src2]"+&r"(src2), [dst]"+&r"(dst), [src]"+&r"(src),            \
          [x]"+&r"(x), [y]"+&r"(y), [rtmp0]"=&r"(rtmp[0])                 \
        : [dststride]"r"(dststride), [shift]"f"(shift),                   \
          [srcstride]"r"(srcstride)                                       \
        : "memory"                                                        \
    );                                                                    \
}                                                                         \

PUT_HEVC_PEL_BI_PIXELS(8, 1, -8, -8, -16);
PUT_HEVC_PEL_BI_PIXELS(16, 2, -16, -16, -32);
PUT_HEVC_PEL_BI_PIXELS(24, 3, -24, -24, -48);
PUT_HEVC_PEL_BI_PIXELS(32, 4, -32, -32, -64);
PUT_HEVC_PEL_BI_PIXELS(48, 6, -48, -48, -96);
PUT_HEVC_PEL_BI_PIXELS(64, 8, -64, -64, -128);
