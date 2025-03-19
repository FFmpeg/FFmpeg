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

#include "libavcodec/hevc/hevcdec.h"
#include "libavcodec/bit_depth_template.c"
#include "libavcodec/mips/hevcdsp_mips.h"
#include "libavutil/intfloat.h"
#include "libavutil/mips/mmiutils.h"

#define PUT_HEVC_QPEL_H(w, x_step, src_step, dst_step)                   \
void ff_hevc_put_hevc_qpel_h##w##_8_mmi(int16_t *dst, const uint8_t *_src, \
                                        ptrdiff_t _srcstride,            \
                                        int height, intptr_t mx,         \
                                        intptr_t my, int width)          \
{                                                                        \
    int x, y;                                                            \
    const pixel *src = (const pixel*)_src - 3;                           \
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);                    \
    double ftmp[15];                                                     \
    uint64_t rtmp[1];                                                    \
    const int8_t *filter = ff_hevc_qpel_filters[mx];                     \
    DECLARE_VAR_ALL64;                                                   \
                                                                         \
    x = x_step;                                                          \
    y = height;                                                          \
    __asm__ volatile(                                                    \
        MMI_LDC1(%[ftmp1], %[filter], 0x00)                              \
        "li           %[rtmp0],      0x08                       \n\t"    \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"    \
        "punpckhbh    %[ftmp2],      %[ftmp0],      %[ftmp1]    \n\t"    \
        "punpcklbh    %[ftmp1],      %[ftmp0],      %[ftmp1]    \n\t"    \
        "psrah        %[ftmp1],      %[ftmp1],      %[ftmp0]    \n\t"    \
        "psrah        %[ftmp2],      %[ftmp2],      %[ftmp0]    \n\t"    \
        "pxor         %[ftmp0],      %[ftmp0],      %[ftmp0]    \n\t"    \
                                                                         \
        "1:                                                     \n\t"    \
        "2:                                                     \n\t"    \
        MMI_ULDC1(%[ftmp3], %[src], 0x00)                                \
        MMI_ULDC1(%[ftmp4], %[src], 0x01)                                \
        MMI_ULDC1(%[ftmp5], %[src], 0x02)                                \
        MMI_ULDC1(%[ftmp6], %[src], 0x03)                                \
        "punpcklbh    %[ftmp7],      %[ftmp3],      %[ftmp0]    \n\t"    \
        "punpckhbh    %[ftmp8],      %[ftmp3],      %[ftmp0]    \n\t"    \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"    \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"    \
        "paddh        %[ftmp3],      %[ftmp7],      %[ftmp8]    \n\t"    \
        "punpcklbh    %[ftmp7],      %[ftmp4],      %[ftmp0]    \n\t"    \
        "punpckhbh    %[ftmp8],      %[ftmp4],      %[ftmp0]    \n\t"    \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"    \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"    \
        "paddh        %[ftmp4],      %[ftmp7],      %[ftmp8]    \n\t"    \
        "punpcklbh    %[ftmp7],      %[ftmp5],      %[ftmp0]    \n\t"    \
        "punpckhbh    %[ftmp8],      %[ftmp5],      %[ftmp0]    \n\t"    \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"    \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"    \
        "paddh        %[ftmp5],      %[ftmp7],      %[ftmp8]    \n\t"    \
        "punpcklbh    %[ftmp7],      %[ftmp6],      %[ftmp0]    \n\t"    \
        "punpckhbh    %[ftmp8],      %[ftmp6],      %[ftmp0]    \n\t"    \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"    \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"    \
        "paddh        %[ftmp6],      %[ftmp7],      %[ftmp8]    \n\t"    \
        TRANSPOSE_4H(%[ftmp3], %[ftmp4], %[ftmp5], %[ftmp6],             \
                     %[ftmp7], %[ftmp8], %[ftmp9], %[ftmp10])            \
        "paddh        %[ftmp3],      %[ftmp3],      %[ftmp4]    \n\t"    \
        "paddh        %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"    \
        "paddh        %[ftmp3],      %[ftmp3],      %[ftmp5]    \n\t"    \
        MMI_USDC1(%[ftmp3], %[dst], 0x00)                                \
                                                                         \
        "daddi        %[x],          %[x],         -0x01        \n\t"    \
        PTR_ADDIU    "%[src],        %[src],        0x04        \n\t"    \
        PTR_ADDIU    "%[dst],        %[dst],        0x08        \n\t"    \
        "bnez         %[x],          2b                         \n\t"    \
                                                                         \
        "daddi        %[y],          %[y],         -0x01        \n\t"    \
        "li           %[x],        " #x_step "                  \n\t"    \
        PTR_ADDIU    "%[src],        %[src],     " #src_step "  \n\t"    \
        PTR_ADDIU    "%[dst],        %[dst],     " #dst_step "  \n\t"    \
        PTR_ADDU     "%[src],        %[src],        %[stride]   \n\t"    \
        PTR_ADDIU    "%[dst],        %[dst],        0x80        \n\t"    \
        "bnez         %[y],          1b                         \n\t"    \
        : RESTRICT_ASM_ALL64                                             \
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                  \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                  \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                  \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                  \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                  \
          [ftmp10]"=&f"(ftmp[10]), [rtmp0]"=&r"(rtmp[0]),                \
          [src]"+&r"(src), [dst]"+&r"(dst), [y]"+&r"(y),                 \
          [x]"+&r"(x)                                                    \
        : [filter]"r"(filter), [stride]"r"(srcstride)                    \
        : "memory"                                                       \
    );                                                                   \
}

PUT_HEVC_QPEL_H(4, 1, -4, -8);
PUT_HEVC_QPEL_H(8, 2, -8, -16);
PUT_HEVC_QPEL_H(12, 3, -12, -24);
PUT_HEVC_QPEL_H(16, 4, -16, -32);
PUT_HEVC_QPEL_H(24, 6, -24, -48);
PUT_HEVC_QPEL_H(32, 8, -32, -64);
PUT_HEVC_QPEL_H(48, 12, -48, -96);
PUT_HEVC_QPEL_H(64, 16, -64, -128);

#define PUT_HEVC_QPEL_HV(w, x_step, src_step, dst_step)                  \
void ff_hevc_put_hevc_qpel_hv##w##_8_mmi(int16_t *dst, const uint8_t *_src,\
                                     ptrdiff_t _srcstride,               \
                                     int height, intptr_t mx,            \
                                     intptr_t my, int width)             \
{                                                                        \
    int x, y;                                                            \
    const int8_t *filter;                                                \
    const pixel *src = (const pixel*)_src;                               \
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);                    \
    int16_t tmp_array[(MAX_PB_SIZE + QPEL_EXTRA) * MAX_PB_SIZE];         \
    int16_t *tmp = tmp_array;                                            \
    double ftmp[15];                                                     \
    uint64_t rtmp[1];                                                    \
    DECLARE_VAR_ALL64;                                                   \
                                                                         \
    src   -= (QPEL_EXTRA_BEFORE * srcstride + 3);                        \
    filter = ff_hevc_qpel_filters[mx];                                   \
    x = x_step;                                                          \
    y = height + QPEL_EXTRA;                                             \
    __asm__ volatile(                                                    \
        MMI_LDC1(%[ftmp1], %[filter], 0x00)                              \
        "li           %[rtmp0],      0x08                       \n\t"    \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"    \
        "punpckhbh    %[ftmp2],      %[ftmp0],      %[ftmp1]    \n\t"    \
        "punpcklbh    %[ftmp1],      %[ftmp0],      %[ftmp1]    \n\t"    \
        "psrah        %[ftmp1],      %[ftmp1],      %[ftmp0]    \n\t"    \
        "psrah        %[ftmp2],      %[ftmp2],      %[ftmp0]    \n\t"    \
        "pxor         %[ftmp0],      %[ftmp0],      %[ftmp0]    \n\t"    \
                                                                         \
        "1:                                                     \n\t"    \
        "2:                                                     \n\t"    \
        MMI_ULDC1(%[ftmp3], %[src], 0x00)                                \
        MMI_ULDC1(%[ftmp4], %[src], 0x01)                                \
        MMI_ULDC1(%[ftmp5], %[src], 0x02)                                \
        MMI_ULDC1(%[ftmp6], %[src], 0x03)                                \
        "punpcklbh    %[ftmp7],      %[ftmp3],      %[ftmp0]    \n\t"    \
        "punpckhbh    %[ftmp8],      %[ftmp3],      %[ftmp0]    \n\t"    \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"    \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"    \
        "paddh        %[ftmp3],      %[ftmp7],      %[ftmp8]    \n\t"    \
        "punpcklbh    %[ftmp7],      %[ftmp4],      %[ftmp0]    \n\t"    \
        "punpckhbh    %[ftmp8],      %[ftmp4],      %[ftmp0]    \n\t"    \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"    \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"    \
        "paddh        %[ftmp4],      %[ftmp7],      %[ftmp8]    \n\t"    \
        "punpcklbh    %[ftmp7],      %[ftmp5],      %[ftmp0]    \n\t"    \
        "punpckhbh    %[ftmp8],      %[ftmp5],      %[ftmp0]    \n\t"    \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"    \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"    \
        "paddh        %[ftmp5],      %[ftmp7],      %[ftmp8]    \n\t"    \
        "punpcklbh    %[ftmp7],      %[ftmp6],      %[ftmp0]    \n\t"    \
        "punpckhbh    %[ftmp8],      %[ftmp6],      %[ftmp0]    \n\t"    \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"    \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"    \
        "paddh        %[ftmp6],      %[ftmp7],      %[ftmp8]    \n\t"    \
        TRANSPOSE_4H(%[ftmp3], %[ftmp4], %[ftmp5], %[ftmp6],             \
                     %[ftmp7], %[ftmp8], %[ftmp9], %[ftmp10])            \
        "paddh        %[ftmp3],      %[ftmp3],      %[ftmp4]    \n\t"    \
        "paddh        %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"    \
        "paddh        %[ftmp3],      %[ftmp3],      %[ftmp5]    \n\t"    \
        MMI_USDC1(%[ftmp3], %[tmp], 0x00)                                \
                                                                         \
        "daddi        %[x],          %[x],         -0x01        \n\t"    \
        PTR_ADDIU    "%[src],        %[src],        0x04        \n\t"    \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x08        \n\t"    \
        "bnez         %[x],          2b                         \n\t"    \
                                                                         \
        "daddi        %[y],          %[y],         -0x01        \n\t"    \
        "li           %[x],        " #x_step "                  \n\t"    \
        PTR_ADDIU    "%[src],        %[src],     " #src_step "  \n\t"    \
        PTR_ADDIU    "%[tmp],        %[tmp],     " #dst_step "  \n\t"    \
        PTR_ADDU     "%[src],        %[src],        %[stride]   \n\t"    \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"    \
        "bnez         %[y],          1b                         \n\t"    \
        : RESTRICT_ASM_ALL64                                             \
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                  \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                  \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                  \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                  \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                  \
          [ftmp10]"=&f"(ftmp[10]), [rtmp0]"=&r"(rtmp[0]),                \
          [src]"+&r"(src), [tmp]"+&r"(tmp), [y]"+&r"(y),                 \
          [x]"+&r"(x)                                                    \
        : [filter]"r"(filter), [stride]"r"(srcstride)                    \
        : "memory"                                                       \
    );                                                                   \
                                                                         \
    tmp    = tmp_array + QPEL_EXTRA_BEFORE * 4 -12;                      \
    filter = ff_hevc_qpel_filters[my];                                   \
    x = x_step;                                                          \
    y = height;                                                          \
    __asm__ volatile(                                                    \
        MMI_LDC1(%[ftmp1], %[filter], 0x00)                              \
        "li           %[rtmp0],      0x08                       \n\t"    \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"    \
        "punpckhbh    %[ftmp2],      %[ftmp0],      %[ftmp1]    \n\t"    \
        "punpcklbh    %[ftmp1],      %[ftmp0],      %[ftmp1]    \n\t"    \
        "psrah        %[ftmp1],      %[ftmp1],      %[ftmp0]    \n\t"    \
        "psrah        %[ftmp2],      %[ftmp2],      %[ftmp0]    \n\t"    \
        "li           %[rtmp0],      0x06                       \n\t"    \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"    \
                                                                         \
        "1:                                                     \n\t"    \
        "2:                                                     \n\t"    \
        MMI_ULDC1(%[ftmp3], %[tmp], 0x00)                                \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"    \
        MMI_ULDC1(%[ftmp4], %[tmp], 0x00)                                \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"    \
        MMI_ULDC1(%[ftmp5], %[tmp], 0x00)                                \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"    \
        MMI_ULDC1(%[ftmp6], %[tmp], 0x00)                                \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"    \
        MMI_ULDC1(%[ftmp7], %[tmp], 0x00)                                \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"    \
        MMI_ULDC1(%[ftmp8], %[tmp], 0x00)                                \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"    \
        MMI_ULDC1(%[ftmp9], %[tmp], 0x00)                                \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"    \
        MMI_ULDC1(%[ftmp10], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        -0x380      \n\t"    \
        TRANSPOSE_4H(%[ftmp3], %[ftmp4], %[ftmp5], %[ftmp6],             \
                     %[ftmp11], %[ftmp12], %[ftmp13], %[ftmp14])         \
        TRANSPOSE_4H(%[ftmp7], %[ftmp8], %[ftmp9], %[ftmp10],            \
                     %[ftmp11], %[ftmp12], %[ftmp13], %[ftmp14])         \
        "pmaddhw      %[ftmp11],     %[ftmp3],      %[ftmp1]    \n\t"    \
        "pmaddhw      %[ftmp12],     %[ftmp7],      %[ftmp2]    \n\t"    \
        "pmaddhw      %[ftmp13],     %[ftmp4],      %[ftmp1]    \n\t"    \
        "pmaddhw      %[ftmp14],     %[ftmp8],      %[ftmp2]    \n\t"    \
        "paddw        %[ftmp11],     %[ftmp11],     %[ftmp12]   \n\t"    \
        "paddw        %[ftmp13],     %[ftmp13],     %[ftmp14]   \n\t"    \
        TRANSPOSE_2W(%[ftmp11], %[ftmp13], %[ftmp3], %[ftmp4])           \
        "paddw        %[ftmp3],      %[ftmp3],      %[ftmp4]    \n\t"    \
        "psraw        %[ftmp3],      %[ftmp3],      %[ftmp0]    \n\t"    \
        "pmaddhw      %[ftmp11],     %[ftmp5],      %[ftmp1]    \n\t"    \
        "pmaddhw      %[ftmp12],     %[ftmp9],      %[ftmp2]    \n\t"    \
        "pmaddhw      %[ftmp13],     %[ftmp6],      %[ftmp1]    \n\t"    \
        "pmaddhw      %[ftmp14],     %[ftmp10],     %[ftmp2]    \n\t"    \
        "paddw        %[ftmp11],     %[ftmp11],     %[ftmp12]   \n\t"    \
        "paddw        %[ftmp13],     %[ftmp13],     %[ftmp14]   \n\t"    \
        TRANSPOSE_2W(%[ftmp11], %[ftmp13], %[ftmp5], %[ftmp6])           \
        "paddw        %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"    \
        "psraw        %[ftmp5],      %[ftmp5],      %[ftmp0]    \n\t"    \
        "packsswh     %[ftmp3],      %[ftmp3],      %[ftmp5]    \n\t"    \
        MMI_USDC1(%[ftmp3], %[dst], 0x00)                               \
                                                                         \
        "daddi        %[x],          %[x],         -0x01        \n\t"    \
        PTR_ADDIU    "%[dst],        %[dst],        0x08        \n\t"    \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x08        \n\t"    \
        "bnez         %[x],          2b                         \n\t"    \
                                                                         \
        "daddi        %[y],          %[y],         -0x01        \n\t"    \
        "li           %[x],        " #x_step "                  \n\t"    \
        PTR_ADDIU    "%[dst],        %[dst],     " #dst_step "  \n\t"    \
        PTR_ADDIU    "%[tmp],        %[tmp],     " #dst_step "  \n\t"    \
        PTR_ADDIU    "%[dst],        %[dst],        0x80        \n\t"    \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"    \
        "bnez         %[y],          1b                         \n\t"    \
        : RESTRICT_ASM_ALL64                                             \
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                  \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                  \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                  \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                  \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                  \
          [ftmp10]"=&f"(ftmp[10]), [ftmp11]"=&f"(ftmp[11]),              \
          [ftmp12]"=&f"(ftmp[12]), [ftmp13]"=&f"(ftmp[13]),              \
          [ftmp14]"=&f"(ftmp[14]), [rtmp0]"=&r"(rtmp[0]),                \
          [dst]"+&r"(dst), [tmp]"+&r"(tmp), [y]"+&r"(y),                 \
          [x]"+&r"(x)                                                    \
        : [filter]"r"(filter), [stride]"r"(srcstride)                    \
        : "memory"                                                       \
    );                                                                   \
}

PUT_HEVC_QPEL_HV(4, 1, -4, -8);
PUT_HEVC_QPEL_HV(8, 2, -8, -16);
PUT_HEVC_QPEL_HV(12, 3, -12, -24);
PUT_HEVC_QPEL_HV(16, 4, -16, -32);
PUT_HEVC_QPEL_HV(24, 6, -24, -48);
PUT_HEVC_QPEL_HV(32, 8, -32, -64);
PUT_HEVC_QPEL_HV(48, 12, -48, -96);
PUT_HEVC_QPEL_HV(64, 16, -64, -128);

#define PUT_HEVC_QPEL_BI_H(w, x_step, src_step, src2_step, dst_step)    \
void ff_hevc_put_hevc_qpel_bi_h##w##_8_mmi(uint8_t *_dst,               \
                                           ptrdiff_t _dststride,        \
                                           const uint8_t *_src,         \
                                           ptrdiff_t _srcstride,        \
                                           const int16_t *src2, int height, \
                                           intptr_t mx, intptr_t my,    \
                                           int width)                   \
{                                                                       \
    int x, y;                                                           \
    const pixel  *src       = (const pixel*)_src - 3;                   \
    ptrdiff_t     srcstride = _srcstride / sizeof(pixel);               \
    pixel *dst          = (pixel *)_dst;                                \
    ptrdiff_t dststride = _dststride / sizeof(pixel);                   \
    const int8_t *filter    = ff_hevc_qpel_filters[mx];                 \
    double ftmp[20];                                                    \
    uint64_t rtmp[1];                                                   \
    union av_intfloat64 shift;                                          \
    union av_intfloat64 offset;                                         \
    DECLARE_VAR_ALL64;                                                  \
    DECLARE_VAR_LOW32;                                                  \
    shift.i = 7;                                                        \
    offset.i = 64;                                                      \
                                                                        \
    x = width >> 2;                                                     \
    y = height;                                                         \
    __asm__ volatile(                                                   \
        MMI_LDC1(%[ftmp1], %[filter], 0x00)                             \
        "li           %[rtmp0],      0x08                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"   \
        "punpckhbh    %[ftmp2],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "punpcklbh    %[ftmp1],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "psrah        %[ftmp1],      %[ftmp1],      %[ftmp0]    \n\t"   \
        "psrah        %[ftmp2],      %[ftmp2],      %[ftmp0]    \n\t"   \
        "pxor         %[ftmp0],      %[ftmp0],      %[ftmp0]    \n\t"   \
        "punpcklhw    %[offset],     %[offset],     %[offset]   \n\t"   \
        "punpcklwd    %[offset],     %[offset],     %[offset]   \n\t"   \
                                                                        \
        "1:                                                     \n\t"   \
        "li           %[x],        " #x_step "                  \n\t"   \
        "2:                                                     \n\t"   \
        MMI_ULDC1(%[ftmp3], %[src], 0x00)                               \
        MMI_ULDC1(%[ftmp4], %[src], 0x01)                               \
        MMI_ULDC1(%[ftmp5], %[src], 0x02)                               \
        MMI_ULDC1(%[ftmp6], %[src], 0x03)                               \
        "punpcklbh    %[ftmp7],      %[ftmp3],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp3],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp3],      %[ftmp7],      %[ftmp8]    \n\t"   \
        "punpcklbh    %[ftmp7],      %[ftmp4],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp4],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp4],      %[ftmp7],      %[ftmp8]    \n\t"   \
        "punpcklbh    %[ftmp7],      %[ftmp5],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp5],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp5],      %[ftmp7],      %[ftmp8]    \n\t"   \
        "punpcklbh    %[ftmp7],      %[ftmp6],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp6],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp6],      %[ftmp7],      %[ftmp8]    \n\t"   \
        TRANSPOSE_4H(%[ftmp3], %[ftmp4], %[ftmp5], %[ftmp6],            \
                     %[ftmp7], %[ftmp8], %[ftmp9], %[ftmp10])           \
        "paddh        %[ftmp3],      %[ftmp3],      %[ftmp4]    \n\t"   \
        "paddh        %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"   \
        "paddh        %[ftmp3],      %[ftmp3],      %[ftmp5]    \n\t"   \
        "paddh        %[ftmp3],      %[ftmp3],      %[offset]   \n\t"   \
        MMI_ULDC1(%[ftmp4], %[src2], 0x00)                              \
        "li           %[rtmp0],      0x10                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp8]                   \n\t"   \
        "punpcklhw    %[ftmp5],      %[ftmp0],      %[ftmp3]    \n\t"   \
        "punpckhhw    %[ftmp6],      %[ftmp0],      %[ftmp3]    \n\t"   \
        "punpckhhw    %[ftmp3],      %[ftmp0],      %[ftmp4]    \n\t"   \
        "punpcklhw    %[ftmp4],      %[ftmp0],      %[ftmp4]    \n\t"   \
        "psraw        %[ftmp5],      %[ftmp5],      %[ftmp8]    \n\t"   \
        "psraw        %[ftmp6],      %[ftmp6],      %[ftmp8]    \n\t"   \
        "psraw        %[ftmp3],      %[ftmp3],      %[ftmp8]    \n\t"   \
        "psraw        %[ftmp4],      %[ftmp4],      %[ftmp8]    \n\t"   \
        "paddw        %[ftmp5],      %[ftmp5],      %[ftmp4]    \n\t"   \
        "paddw        %[ftmp6],      %[ftmp6],      %[ftmp3]    \n\t"   \
        "psraw        %[ftmp5],      %[ftmp5],      %[shift]    \n\t"   \
        "psraw        %[ftmp6],      %[ftmp6],      %[shift]    \n\t"   \
        "packsswh     %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"   \
        "pcmpgth      %[ftmp7],      %[ftmp5],      %[ftmp0]    \n\t"   \
        "pand         %[ftmp3],      %[ftmp5],      %[ftmp7]    \n\t"   \
        "packushb     %[ftmp3],      %[ftmp3],      %[ftmp3]    \n\t"   \
        MMI_USWC1(%[ftmp3], %[dst], 0x00)                               \
                                                                        \
        "daddi        %[x],          %[x],         -0x01        \n\t"   \
        PTR_ADDIU    "%[src],        %[src],        0x04        \n\t"   \
        PTR_ADDIU    "%[dst],        %[dst],        0x04        \n\t"   \
        PTR_ADDIU    "%[src2],       %[src2],       0x08        \n\t"   \
        "bnez         %[x],          2b                         \n\t"   \
                                                                        \
        "daddi        %[y],          %[y],         -0x01        \n\t"   \
        PTR_ADDIU    "%[src],        %[src],     " #src_step "  \n\t"   \
        PTR_ADDIU    "%[dst],        %[dst],     " #dst_step "  \n\t"   \
        PTR_ADDIU    "%[src2],       %[src2],    " #src2_step " \n\t"   \
        PTR_ADDU     "%[src],        %[src],    %[src_stride]   \n\t"   \
        PTR_ADDU     "%[dst],        %[dst],    %[dst_stride]   \n\t"   \
        PTR_ADDIU    "%[src2],       %[src2],       0x80        \n\t"   \
        "bnez         %[y],          1b                         \n\t"   \
        : RESTRICT_ASM_ALL64 RESTRICT_ASM_LOW32                         \
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                 \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                 \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                 \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                 \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                 \
          [ftmp10]"=&f"(ftmp[10]), [ftmp11]"=&f"(ftmp[11]),             \
          [ftmp12]"=&f"(ftmp[12]), [src2]"+&r"(src2),                   \
          [dst]"+&r"(dst), [src]"+&r"(src), [y]"+&r"(y), [x]"=&r"(x),   \
          [offset]"+&f"(offset.f), [rtmp0]"=&r"(rtmp[0])                \
        : [src_stride]"r"(srcstride), [dst_stride]"r"(dststride),       \
          [filter]"r"(filter), [shift]"f"(shift.f)                      \
        : "memory"                                                      \
    );                                                                  \
}

PUT_HEVC_QPEL_BI_H(4, 1, -4, -8, -4);
PUT_HEVC_QPEL_BI_H(8, 2, -8, -16, -8);
PUT_HEVC_QPEL_BI_H(12, 3, -12, -24, -12);
PUT_HEVC_QPEL_BI_H(16, 4, -16, -32, -16);
PUT_HEVC_QPEL_BI_H(24, 6, -24, -48, -24);
PUT_HEVC_QPEL_BI_H(32, 8, -32, -64, -32);
PUT_HEVC_QPEL_BI_H(48, 12, -48, -96, -48);
PUT_HEVC_QPEL_BI_H(64, 16, -64, -128, -64);

#define PUT_HEVC_QPEL_BI_HV(w, x_step, src_step, src2_step, dst_step)   \
void ff_hevc_put_hevc_qpel_bi_hv##w##_8_mmi(uint8_t *_dst,              \
                                            ptrdiff_t _dststride,       \
                                            const uint8_t *_src,        \
                                            ptrdiff_t _srcstride,       \
                                            const int16_t *src2, int height, \
                                            intptr_t mx, intptr_t my,   \
                                            int width)                  \
{                                                                       \
    int x, y;                                                           \
    const int8_t *filter;                                               \
    pixel *src = (pixel*)_src;                                          \
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);                   \
    pixel *dst          = (pixel *)_dst;                                \
    ptrdiff_t dststride = _dststride / sizeof(pixel);                   \
    int16_t tmp_array[(MAX_PB_SIZE + QPEL_EXTRA) * MAX_PB_SIZE];        \
    int16_t *tmp = tmp_array;                                           \
    double ftmp[20];                                                    \
    uint64_t rtmp[1];                                                   \
    union av_intfloat64 shift;                                          \
    union av_intfloat64 offset;                                         \
    DECLARE_VAR_ALL64;                                                  \
    DECLARE_VAR_LOW32;                                                  \
    shift.i = 7;                                                        \
    offset.i = 64;                                                      \
                                                                        \
    src   -= (QPEL_EXTRA_BEFORE * srcstride + 3);                       \
    filter = ff_hevc_qpel_filters[mx];                                  \
    x = width >> 2;                                                     \
    y = height + QPEL_EXTRA;                                            \
    __asm__ volatile(                                                   \
        MMI_LDC1(%[ftmp1], %[filter], 0x00)                             \
        "li           %[rtmp0],      0x08                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"   \
        "punpckhbh    %[ftmp2],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "punpcklbh    %[ftmp1],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "psrah        %[ftmp1],      %[ftmp1],      %[ftmp0]    \n\t"   \
        "psrah        %[ftmp2],      %[ftmp2],      %[ftmp0]    \n\t"   \
        "pxor         %[ftmp0],      %[ftmp0],      %[ftmp0]    \n\t"   \
                                                                        \
        "1:                                                     \n\t"   \
        "2:                                                     \n\t"   \
        MMI_ULDC1(%[ftmp3], %[src], 0x00)                               \
        MMI_ULDC1(%[ftmp4], %[src], 0x01)                               \
        MMI_ULDC1(%[ftmp5], %[src], 0x02)                               \
        MMI_ULDC1(%[ftmp6], %[src], 0x03)                               \
        "punpcklbh    %[ftmp7],      %[ftmp3],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp3],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp3],      %[ftmp7],      %[ftmp8]    \n\t"   \
        "punpcklbh    %[ftmp7],      %[ftmp4],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp4],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp4],      %[ftmp7],      %[ftmp8]    \n\t"   \
        "punpcklbh    %[ftmp7],      %[ftmp5],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp5],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp5],      %[ftmp7],      %[ftmp8]    \n\t"   \
        "punpcklbh    %[ftmp7],      %[ftmp6],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp6],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp6],      %[ftmp7],      %[ftmp8]    \n\t"   \
        TRANSPOSE_4H(%[ftmp3], %[ftmp4], %[ftmp5], %[ftmp6],            \
                     %[ftmp7], %[ftmp8], %[ftmp9], %[ftmp10])           \
        "paddh        %[ftmp3],      %[ftmp3],      %[ftmp4]    \n\t"   \
        "paddh        %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"   \
        "paddh        %[ftmp3],      %[ftmp3],      %[ftmp5]    \n\t"   \
        MMI_USDC1(%[ftmp3], %[tmp], 0x00)                               \
                                                                        \
        "daddi        %[x],          %[x],         -0x01        \n\t"   \
        PTR_ADDIU    "%[src],        %[src],        0x04        \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x08        \n\t"   \
        "bnez         %[x],          2b                         \n\t"   \
                                                                        \
        "daddi        %[y],          %[y],         -0x01        \n\t"   \
        "li           %[x],        " #x_step "                  \n\t"   \
        PTR_ADDIU    "%[src],        %[src],      " #src_step " \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],     " #src2_step " \n\t"   \
        PTR_ADDU     "%[src],        %[src],        %[stride]   \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        "bnez         %[y],          1b                         \n\t"   \
        : RESTRICT_ASM_ALL64                                            \
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                 \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                 \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                 \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                 \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                 \
          [ftmp10]"=&f"(ftmp[10]), [rtmp0]"=&r"(rtmp[0]),               \
          [src]"+&r"(src), [tmp]"+&r"(tmp), [y]"+&r"(y),                \
          [x]"+&r"(x)                                                   \
        : [filter]"r"(filter), [stride]"r"(srcstride)                   \
        : "memory"                                                      \
    );                                                                  \
                                                                        \
    tmp    = tmp_array;                                                 \
    filter = ff_hevc_qpel_filters[my];                                  \
    x = width >> 2;                                                     \
    y = height;                                                         \
    __asm__ volatile(                                                   \
        MMI_LDC1(%[ftmp1], %[filter], 0x00)                             \
        "li           %[rtmp0],      0x08                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"   \
        "punpckhbh    %[ftmp2],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "punpcklbh    %[ftmp1],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "psrah        %[ftmp1],      %[ftmp1],      %[ftmp0]    \n\t"   \
        "psrah        %[ftmp2],      %[ftmp2],      %[ftmp0]    \n\t"   \
        "li           %[rtmp0],      0x06                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"   \
        "punpcklwd    %[offset],     %[offset],     %[offset]   \n\t"   \
                                                                        \
        "1:                                                     \n\t"   \
        "li           %[x],        " #x_step "                  \n\t"   \
        "2:                                                     \n\t"   \
        MMI_ULDC1(%[ftmp3], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp4], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp5], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp6], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp7], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp8], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp9], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp10], %[tmp], 0x00)                              \
        PTR_ADDIU    "%[tmp],        %[tmp],        -0x380      \n\t"   \
        TRANSPOSE_4H(%[ftmp3], %[ftmp4], %[ftmp5], %[ftmp6],            \
                     %[ftmp11], %[ftmp12], %[ftmp13], %[ftmp14])        \
        TRANSPOSE_4H(%[ftmp7], %[ftmp8], %[ftmp9], %[ftmp10],           \
                     %[ftmp11], %[ftmp12], %[ftmp13], %[ftmp14])        \
        "pmaddhw      %[ftmp11],     %[ftmp3],      %[ftmp1]    \n\t"   \
        "pmaddhw      %[ftmp12],     %[ftmp7],      %[ftmp2]    \n\t"   \
        "pmaddhw      %[ftmp13],     %[ftmp4],      %[ftmp1]    \n\t"   \
        "pmaddhw      %[ftmp14],     %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddw        %[ftmp11],     %[ftmp11],     %[ftmp12]   \n\t"   \
        "paddw        %[ftmp13],     %[ftmp13],     %[ftmp14]   \n\t"   \
        TRANSPOSE_2W(%[ftmp11], %[ftmp13], %[ftmp3], %[ftmp4])          \
        "paddw        %[ftmp3],      %[ftmp3],      %[ftmp4]    \n\t"   \
        "psraw        %[ftmp3],      %[ftmp3],      %[ftmp0]    \n\t"   \
        "pmaddhw      %[ftmp11],     %[ftmp5],      %[ftmp1]    \n\t"   \
        "pmaddhw      %[ftmp12],     %[ftmp9],      %[ftmp2]    \n\t"   \
        "pmaddhw      %[ftmp13],     %[ftmp6],      %[ftmp1]    \n\t"   \
        "pmaddhw      %[ftmp14],     %[ftmp10],     %[ftmp2]    \n\t"   \
        "paddw        %[ftmp11],     %[ftmp11],     %[ftmp12]   \n\t"   \
        "paddw        %[ftmp13],     %[ftmp13],     %[ftmp14]   \n\t"   \
        TRANSPOSE_2W(%[ftmp11], %[ftmp13], %[ftmp5], %[ftmp6])          \
        "paddw        %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"   \
        "psraw        %[ftmp5],      %[ftmp5],      %[ftmp0]    \n\t"   \
        "packsswh     %[ftmp3],      %[ftmp3],      %[ftmp5]    \n\t"   \
        MMI_ULDC1(%[ftmp4], %[src2], 0x00)                              \
        "pxor         %[ftmp7],      %[ftmp7],      %[ftmp7]    \n\t"   \
        "li           %[rtmp0],      0x10                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp8]                   \n\t"   \
        "punpcklhw    %[ftmp5],      %[ftmp7],      %[ftmp3]    \n\t"   \
        "punpckhhw    %[ftmp6],      %[ftmp7],      %[ftmp3]    \n\t"   \
        "punpckhhw    %[ftmp3],      %[ftmp7],      %[ftmp4]    \n\t"   \
        "punpcklhw    %[ftmp4],      %[ftmp7],      %[ftmp4]    \n\t"   \
        "psraw        %[ftmp5],      %[ftmp5],      %[ftmp8]    \n\t"   \
        "psraw        %[ftmp6],      %[ftmp6],      %[ftmp8]    \n\t"   \
        "psraw        %[ftmp3],      %[ftmp3],      %[ftmp8]    \n\t"   \
        "psraw        %[ftmp4],      %[ftmp4],      %[ftmp8]    \n\t"   \
        "paddw        %[ftmp5],      %[ftmp5],      %[ftmp4]    \n\t"   \
        "paddw        %[ftmp6],      %[ftmp6],      %[ftmp3]    \n\t"   \
        "paddw        %[ftmp5],      %[ftmp5],      %[offset]   \n\t"   \
        "paddw        %[ftmp6],      %[ftmp6],      %[offset]   \n\t"   \
        "psraw        %[ftmp5],      %[ftmp5],      %[shift]    \n\t"   \
        "psraw        %[ftmp6],      %[ftmp6],      %[shift]    \n\t"   \
        "packsswh     %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"   \
        "pcmpgth      %[ftmp7],      %[ftmp5],      %[ftmp7]    \n\t"   \
        "pand         %[ftmp3],      %[ftmp5],      %[ftmp7]    \n\t"   \
        "packushb     %[ftmp3],      %[ftmp3],      %[ftmp3]    \n\t"   \
        MMI_USWC1(%[ftmp3], %[dst], 0x00)                               \
                                                                        \
        "daddi        %[x],          %[x],         -0x01        \n\t"   \
        PTR_ADDIU    "%[src2],       %[src2],       0x08        \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x08        \n\t"   \
        PTR_ADDIU    "%[dst],        %[dst],        0x04        \n\t"   \
        "bnez         %[x],          2b                         \n\t"   \
                                                                        \
        "daddi        %[y],          %[y],         -0x01        \n\t"   \
        PTR_ADDIU    "%[src2],       %[src2],    " #src2_step " \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],     " #src2_step " \n\t"   \
        PTR_ADDIU    "%[dst],        %[dst],     " #dst_step "  \n\t"   \
        PTR_ADDIU    "%[src2],       %[src2],       0x80        \n\t"   \
        PTR_ADDU     "%[dst],        %[dst],        %[stride]   \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        "bnez         %[y],          1b                         \n\t"   \
        : RESTRICT_ASM_ALL64 RESTRICT_ASM_LOW32                         \
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                 \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                 \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                 \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                 \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                 \
          [ftmp10]"=&f"(ftmp[10]), [ftmp11]"=&f"(ftmp[11]),             \
          [ftmp12]"=&f"(ftmp[12]), [ftmp13]"=&f"(ftmp[13]),             \
          [ftmp14]"=&f"(ftmp[14]), [src2]"+&r"(src2),                   \
          [dst]"+&r"(dst), [tmp]"+&r"(tmp), [y]"+&r"(y), [x]"=&r"(x),   \
          [offset]"+&f"(offset.f), [rtmp0]"=&r"(rtmp[0])                \
        : [filter]"r"(filter), [stride]"r"(dststride),                  \
          [shift]"f"(shift.f)                                           \
        : "memory"                                                      \
    );                                                                  \
}

PUT_HEVC_QPEL_BI_HV(4, 1, -4, -8, -4);
PUT_HEVC_QPEL_BI_HV(8, 2, -8, -16, -8);
PUT_HEVC_QPEL_BI_HV(12, 3, -12, -24, -12);
PUT_HEVC_QPEL_BI_HV(16, 4, -16, -32, -16);
PUT_HEVC_QPEL_BI_HV(24, 6, -24, -48, -24);
PUT_HEVC_QPEL_BI_HV(32, 8, -32, -64, -32);
PUT_HEVC_QPEL_BI_HV(48, 12, -48, -96, -48);
PUT_HEVC_QPEL_BI_HV(64, 16, -64, -128, -64);

#define PUT_HEVC_EPEL_BI_HV(w, x_step, src_step, src2_step, dst_step)   \
void ff_hevc_put_hevc_epel_bi_hv##w##_8_mmi(uint8_t *_dst,              \
                                            ptrdiff_t _dststride,       \
                                            const uint8_t *_src,        \
                                            ptrdiff_t _srcstride,       \
                                            const int16_t *src2, int height, \
                                            intptr_t mx, intptr_t my,   \
                                            int width)                  \
{                                                                       \
    int x, y;                                                           \
    pixel *src = (pixel *)_src;                                         \
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);                   \
    pixel *dst          = (pixel *)_dst;                                \
    ptrdiff_t dststride = _dststride / sizeof(pixel);                   \
    const int8_t *filter = ff_hevc_epel_filters[mx];                    \
    int16_t tmp_array[(MAX_PB_SIZE + EPEL_EXTRA) * MAX_PB_SIZE];        \
    int16_t *tmp = tmp_array;                                           \
    double  ftmp[12];                                                   \
    uint64_t rtmp[1];                                                   \
    union av_intfloat64 shift;                                          \
    union av_intfloat64 offset;                                         \
    DECLARE_VAR_ALL64;                                                  \
    DECLARE_VAR_LOW32;                                                  \
    shift.i = 7;                                                        \
    offset.i = 64;                                                      \
                                                                        \
    src -= (EPEL_EXTRA_BEFORE * srcstride + 1);                         \
    x = width >> 2;                                                     \
    y = height + EPEL_EXTRA;                                            \
    __asm__ volatile(                                                   \
        MMI_LWC1(%[ftmp1], %[filter], 0x00)                             \
        "li           %[rtmp0],      0x08                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"   \
        "punpcklbh    %[ftmp1],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "psrah        %[ftmp1],      %[ftmp1],      %[ftmp0]    \n\t"   \
        "pxor         %[ftmp0],      %[ftmp0],      %[ftmp0]    \n\t"   \
                                                                        \
        "1:                                                     \n\t"   \
        "2:                                                     \n\t"   \
        MMI_ULWC1(%[ftmp2], %[src], 0x00)                               \
        MMI_ULWC1(%[ftmp3], %[src], 0x01)                               \
        MMI_ULWC1(%[ftmp4], %[src], 0x02)                               \
        MMI_ULWC1(%[ftmp5], %[src], 0x03)                               \
        "punpcklbh    %[ftmp2],      %[ftmp2],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp2],      %[ftmp2],      %[ftmp1]    \n\t"   \
        "punpcklbh    %[ftmp3],      %[ftmp3],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp3],      %[ftmp3],      %[ftmp1]    \n\t"   \
        "punpcklbh    %[ftmp4],      %[ftmp4],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp4],      %[ftmp4],      %[ftmp1]    \n\t"   \
        "punpcklbh    %[ftmp5],      %[ftmp5],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp5],      %[ftmp5],      %[ftmp1]    \n\t"   \
        TRANSPOSE_4H(%[ftmp2], %[ftmp3], %[ftmp4], %[ftmp5],            \
                     %[ftmp6], %[ftmp7], %[ftmp8], %[ftmp9])            \
        "paddh        %[ftmp2],      %[ftmp2],      %[ftmp3]    \n\t"   \
        "paddh        %[ftmp4],      %[ftmp4],      %[ftmp5]    \n\t"   \
        "paddh        %[ftmp2],      %[ftmp2],      %[ftmp4]    \n\t"   \
        MMI_USDC1(%[ftmp2], %[tmp], 0x00)                               \
                                                                        \
        "daddi        %[x],          %[x],         -0x01        \n\t"   \
        PTR_ADDIU    "%[src],        %[src],        0x04        \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x08        \n\t"   \
        "bnez         %[x],          2b                         \n\t"   \
                                                                        \
        "daddi        %[y],          %[y],         -0x01        \n\t"   \
        "li           %[x],        " #x_step "                  \n\t"   \
        PTR_ADDIU    "%[src],        %[src],      " #src_step " \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],     " #src2_step " \n\t"   \
        PTR_ADDU     "%[src],        %[src],        %[stride]   \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        "bnez         %[y],          1b                         \n\t"   \
        : RESTRICT_ASM_ALL64                                            \
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                 \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                 \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                 \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                 \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                 \
          [rtmp0]"=&r"(rtmp[0]),                                        \
          [src]"+&r"(src), [tmp]"+&r"(tmp), [y]"+&r"(y),                \
          [x]"+&r"(x)                                                   \
        : [filter]"r"(filter), [stride]"r"(srcstride)                   \
        : "memory"                                                      \
    );                                                                  \
                                                                        \
    tmp      = tmp_array;                                               \
    filter = ff_hevc_epel_filters[my];                                  \
    x = width >> 2;                                                     \
    y = height;                                                         \
    __asm__ volatile(                                                   \
        MMI_LWC1(%[ftmp1], %[filter], 0x00)                             \
        "li           %[rtmp0],      0x08                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"   \
        "punpcklbh    %[ftmp1],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "psrah        %[ftmp1],      %[ftmp1],      %[ftmp0]    \n\t"   \
        "li           %[rtmp0],      0x06                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"   \
        "punpcklwd    %[offset],     %[offset],     %[offset]   \n\t"   \
        "pxor         %[ftmp2],      %[ftmp2],      %[ftmp2]    \n\t"   \
                                                                        \
        "1:                                                     \n\t"   \
        "li           %[x],        " #x_step "                  \n\t"   \
        "2:                                                     \n\t"   \
        MMI_ULDC1(%[ftmp3], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp4], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp5], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp6], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],       -0x180       \n\t"   \
        TRANSPOSE_4H(%[ftmp3], %[ftmp4], %[ftmp5], %[ftmp6],            \
                     %[ftmp7], %[ftmp8], %[ftmp9], %[ftmp10])           \
        "pmaddhw      %[ftmp7],      %[ftmp3],      %[ftmp1]    \n\t"   \
        "pmaddhw      %[ftmp8],      %[ftmp4],      %[ftmp1]    \n\t"   \
        TRANSPOSE_2W(%[ftmp7], %[ftmp8], %[ftmp3], %[ftmp4])            \
        "paddw        %[ftmp3],      %[ftmp3],      %[ftmp4]    \n\t"   \
        "psraw        %[ftmp3],      %[ftmp3],      %[ftmp0]    \n\t"   \
        "pmaddhw      %[ftmp7],      %[ftmp5],      %[ftmp1]    \n\t"   \
        "pmaddhw      %[ftmp8],      %[ftmp6],      %[ftmp1]    \n\t"   \
        TRANSPOSE_2W(%[ftmp7], %[ftmp8], %[ftmp5], %[ftmp6])            \
        "paddw        %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"   \
        "psraw        %[ftmp5],      %[ftmp5],      %[ftmp0]    \n\t"   \
        "packsswh     %[ftmp3],      %[ftmp3],      %[ftmp5]    \n\t"   \
        MMI_ULDC1(%[ftmp4], %[src2], 0x00)                               \
        "li           %[rtmp0],      0x10                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp8]                   \n\t"   \
        "punpcklhw    %[ftmp5],      %[ftmp2],      %[ftmp3]    \n\t"   \
        "punpckhhw    %[ftmp6],      %[ftmp2],      %[ftmp3]    \n\t"   \
        "punpckhhw    %[ftmp3],      %[ftmp2],      %[ftmp4]    \n\t"   \
        "punpcklhw    %[ftmp4],      %[ftmp2],      %[ftmp4]    \n\t"   \
        "psraw        %[ftmp5],      %[ftmp5],      %[ftmp8]    \n\t"   \
        "psraw        %[ftmp6],      %[ftmp6],      %[ftmp8]    \n\t"   \
        "psraw        %[ftmp3],      %[ftmp3],      %[ftmp8]    \n\t"   \
        "psraw        %[ftmp4],      %[ftmp4],      %[ftmp8]    \n\t"   \
        "paddw        %[ftmp5],      %[ftmp5],      %[ftmp4]    \n\t"   \
        "paddw        %[ftmp6],      %[ftmp6],      %[ftmp3]    \n\t"   \
        "paddw        %[ftmp5],      %[ftmp5],      %[offset]   \n\t"   \
        "paddw        %[ftmp6],      %[ftmp6],      %[offset]   \n\t"   \
        "psraw        %[ftmp5],      %[ftmp5],      %[shift]    \n\t"   \
        "psraw        %[ftmp6],      %[ftmp6],      %[shift]    \n\t"   \
        "packsswh     %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"   \
        "pcmpgth      %[ftmp7],      %[ftmp5],      %[ftmp2]    \n\t"   \
        "pand         %[ftmp3],      %[ftmp5],      %[ftmp7]    \n\t"   \
        "packushb     %[ftmp3],      %[ftmp3],      %[ftmp3]    \n\t"   \
        MMI_USWC1(%[ftmp3], %[dst], 0x0)                                \
                                                                        \
        "daddi        %[x],          %[x],         -0x01        \n\t"   \
        PTR_ADDIU    "%[src2],       %[src2],       0x08        \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x08        \n\t"   \
        PTR_ADDIU    "%[dst],        %[dst],        0x04        \n\t"   \
        "bnez         %[x],          2b                         \n\t"   \
                                                                        \
        "daddi        %[y],          %[y],         -0x01        \n\t"   \
        PTR_ADDIU    "%[src2],       %[src2],    " #src2_step " \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],     " #src2_step " \n\t"   \
        PTR_ADDIU    "%[dst],        %[dst],     " #dst_step "  \n\t"   \
        PTR_ADDIU    "%[src2],       %[src2],       0x80        \n\t"   \
        PTR_ADDU     "%[dst],        %[dst],        %[stride]   \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        "bnez         %[y],          1b                         \n\t"   \
        : RESTRICT_ASM_LOW32 RESTRICT_ASM_ALL64                         \
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                 \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                 \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                 \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                 \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                 \
          [ftmp10]"=&f"(ftmp[10]), [src2]"+&r"(src2),                   \
          [dst]"+&r"(dst), [tmp]"+&r"(tmp), [y]"+&r"(y), [x]"=&r"(x),   \
          [offset]"+&f"(offset.f), [rtmp0]"=&r"(rtmp[0])                \
        : [filter]"r"(filter), [stride]"r"(dststride),                  \
          [shift]"f"(shift.f)                                           \
        : "memory"                                                      \
    );                                                                  \
}

PUT_HEVC_EPEL_BI_HV(4, 1, -4, -8, -4);
PUT_HEVC_EPEL_BI_HV(8, 2, -8, -16, -8);
PUT_HEVC_EPEL_BI_HV(12, 3, -12, -24, -12);
PUT_HEVC_EPEL_BI_HV(16, 4, -16, -32, -16);
PUT_HEVC_EPEL_BI_HV(24, 6, -24, -48, -24);
PUT_HEVC_EPEL_BI_HV(32, 8, -32, -64, -32);

#define PUT_HEVC_PEL_BI_PIXELS(w, x_step, src_step, dst_step, src2_step)  \
void ff_hevc_put_hevc_pel_bi_pixels##w##_8_mmi(uint8_t *_dst,             \
                                               ptrdiff_t _dststride,      \
                                               const uint8_t *_src,       \
                                               ptrdiff_t _srcstride,      \
                                               const int16_t *src2, int height, \
                                               intptr_t mx, intptr_t my,  \
                                               int width)                 \
{                                                                         \
    int x, y;                                                             \
    pixel *src          = (pixel *)_src;                                  \
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);                     \
    pixel *dst          = (pixel *)_dst;                                  \
    ptrdiff_t dststride = _dststride / sizeof(pixel);                     \
    double  ftmp[12];                                                     \
    uint64_t rtmp[1];                                                     \
    union av_intfloat64 shift;                                            \
    DECLARE_VAR_ALL64;                                                    \
    shift.i = 7;                                                          \
                                                                          \
    y = height;                                                           \
    x = width >> 3;                                                       \
    __asm__ volatile(                                                     \
        "pxor         %[ftmp0],      %[ftmp0],      %[ftmp0]    \n\t"     \
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
        MMI_ULDC1(%[ftmp5], %[src], 0x00)                                 \
        MMI_ULDC1(%[ftmp2], %[src2], 0x00)                                \
        MMI_ULDC1(%[ftmp3], %[src2], 0x08)                                \
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
        "pand         %[ftmp2],      %[ftmp2],      %[ftmp3]    \n\t"     \
        "pand         %[ftmp4],      %[ftmp4],      %[ftmp5]    \n\t"     \
        "packushb     %[ftmp2],      %[ftmp2],      %[ftmp4]    \n\t"     \
        MMI_USDC1(%[ftmp2], %[dst], 0x0)                                  \
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
        : RESTRICT_ASM_ALL64                                              \
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                   \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                   \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                   \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                   \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                   \
          [ftmp10]"=&f"(ftmp[10]), [offset]"=&f"(ftmp[11]),               \
          [src2]"+&r"(src2), [dst]"+&r"(dst), [src]"+&r"(src),            \
          [x]"+&r"(x), [y]"+&r"(y), [rtmp0]"=&r"(rtmp[0])                 \
        : [dststride]"r"(dststride), [shift]"f"(shift.f),                 \
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

#define PUT_HEVC_QPEL_UNI_HV(w, x_step, src_step, dst_step, tmp_step)   \
void ff_hevc_put_hevc_qpel_uni_hv##w##_8_mmi(uint8_t *_dst,             \
                                             ptrdiff_t _dststride,      \
                                             const uint8_t *_src,       \
                                             ptrdiff_t _srcstride,      \
                                             int height,                \
                                             intptr_t mx, intptr_t my,  \
                                             int width)                 \
{                                                                       \
    int x, y;                                                           \
    const int8_t *filter;                                               \
    pixel *src = (pixel*)_src;                                          \
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);                   \
    pixel *dst          = (pixel *)_dst;                                \
    ptrdiff_t dststride = _dststride / sizeof(pixel);                   \
    int16_t tmp_array[(MAX_PB_SIZE + QPEL_EXTRA) * MAX_PB_SIZE];        \
    int16_t *tmp = tmp_array;                                           \
    double ftmp[20];                                                    \
    uint64_t rtmp[1];                                                   \
    union av_intfloat64 shift;                                          \
    union av_intfloat64 offset;                                         \
    DECLARE_VAR_ALL64;                                                  \
    DECLARE_VAR_LOW32;                                                  \
    shift.i = 6;                                                        \
    offset.i = 32;                                                      \
                                                                        \
    src   -= (QPEL_EXTRA_BEFORE * srcstride + 3);                       \
    filter = ff_hevc_qpel_filters[mx];                                  \
    x = width >> 2;                                                     \
    y = height + QPEL_EXTRA;                                            \
    __asm__ volatile(                                                   \
        MMI_LDC1(%[ftmp1], %[filter], 0x00)                             \
        "li           %[rtmp0],      0x08                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"   \
        "punpckhbh    %[ftmp2],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "punpcklbh    %[ftmp1],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "psrah        %[ftmp1],      %[ftmp1],      %[ftmp0]    \n\t"   \
        "psrah        %[ftmp2],      %[ftmp2],      %[ftmp0]    \n\t"   \
        "pxor         %[ftmp0],      %[ftmp0],      %[ftmp0]    \n\t"   \
                                                                        \
        "1:                                                     \n\t"   \
        "2:                                                     \n\t"   \
        MMI_ULDC1(%[ftmp3], %[src], 0x00)                               \
        MMI_ULDC1(%[ftmp4], %[src], 0x01)                               \
        MMI_ULDC1(%[ftmp5], %[src], 0x02)                               \
        MMI_ULDC1(%[ftmp6], %[src], 0x03)                               \
        "punpcklbh    %[ftmp7],      %[ftmp3],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp3],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp3],      %[ftmp7],      %[ftmp8]    \n\t"   \
        "punpcklbh    %[ftmp7],      %[ftmp4],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp4],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp4],      %[ftmp7],      %[ftmp8]    \n\t"   \
        "punpcklbh    %[ftmp7],      %[ftmp5],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp5],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp5],      %[ftmp7],      %[ftmp8]    \n\t"   \
        "punpcklbh    %[ftmp7],      %[ftmp6],      %[ftmp0]    \n\t"   \
        "punpckhbh    %[ftmp8],      %[ftmp6],      %[ftmp0]    \n\t"   \
        "pmullh       %[ftmp7],      %[ftmp7],      %[ftmp1]    \n\t"   \
        "pmullh       %[ftmp8],      %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddh        %[ftmp6],      %[ftmp7],      %[ftmp8]    \n\t"   \
        TRANSPOSE_4H(%[ftmp3], %[ftmp4], %[ftmp5], %[ftmp6],            \
                     %[ftmp7], %[ftmp8], %[ftmp9], %[ftmp10])           \
        "paddh        %[ftmp3],      %[ftmp3],      %[ftmp4]    \n\t"   \
        "paddh        %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"   \
        "paddh        %[ftmp3],      %[ftmp3],      %[ftmp5]    \n\t"   \
        MMI_USDC1(%[ftmp3], %[tmp], 0x0)                                \
                                                                        \
        "daddi        %[x],          %[x],         -0x01        \n\t"   \
        PTR_ADDIU    "%[src],        %[src],        0x04        \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x08        \n\t"   \
        "bnez         %[x],          2b                         \n\t"   \
                                                                        \
        "daddi        %[y],          %[y],         -0x01        \n\t"   \
        "li           %[x],        " #x_step "                  \n\t"   \
        PTR_ADDIU    "%[src],        %[src],      " #src_step " \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],      " #tmp_step " \n\t"   \
        PTR_ADDU     "%[src],        %[src],        %[stride]   \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        "bnez         %[y],          1b                         \n\t"   \
        : RESTRICT_ASM_ALL64                                            \
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                 \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                 \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                 \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                 \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                 \
          [ftmp10]"=&f"(ftmp[10]), [rtmp0]"=&r"(rtmp[0]),               \
          [src]"+&r"(src), [tmp]"+&r"(tmp), [y]"+&r"(y),                \
          [x]"+&r"(x)                                                   \
        : [filter]"r"(filter), [stride]"r"(srcstride)                   \
        : "memory"                                                      \
    );                                                                  \
                                                                        \
    tmp    = tmp_array;                                                 \
    filter = ff_hevc_qpel_filters[my];                                  \
    x = width >> 2;                                                     \
    y = height;                                                         \
    __asm__ volatile(                                                   \
        MMI_LDC1(%[ftmp1], %[filter], 0x00)                             \
        "li           %[rtmp0],      0x08                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"   \
        "punpckhbh    %[ftmp2],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "punpcklbh    %[ftmp1],      %[ftmp0],      %[ftmp1]    \n\t"   \
        "psrah        %[ftmp1],      %[ftmp1],      %[ftmp0]    \n\t"   \
        "psrah        %[ftmp2],      %[ftmp2],      %[ftmp0]    \n\t"   \
        "li           %[rtmp0],      0x06                       \n\t"   \
        "dmtc1        %[rtmp0],      %[ftmp0]                   \n\t"   \
        "punpcklhw    %[offset],     %[offset],     %[offset]   \n\t"   \
        "punpcklwd    %[offset],     %[offset],     %[offset]   \n\t"   \
                                                                        \
        "1:                                                     \n\t"   \
        "li           %[x],        " #x_step "                  \n\t"   \
        "2:                                                     \n\t"   \
        MMI_ULDC1(%[ftmp3], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp4], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp5], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp6], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp7], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp8], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp9], %[tmp], 0x00)                               \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        MMI_ULDC1(%[ftmp10], %[tmp], 0x00)                              \
        PTR_ADDIU    "%[tmp],        %[tmp],        -0x380      \n\t"   \
        TRANSPOSE_4H(%[ftmp3], %[ftmp4], %[ftmp5], %[ftmp6],            \
                     %[ftmp11], %[ftmp12], %[ftmp13], %[ftmp14])        \
        TRANSPOSE_4H(%[ftmp7], %[ftmp8], %[ftmp9], %[ftmp10],           \
                     %[ftmp11], %[ftmp12], %[ftmp13], %[ftmp14])        \
        "pmaddhw      %[ftmp11],     %[ftmp3],      %[ftmp1]    \n\t"   \
        "pmaddhw      %[ftmp12],     %[ftmp7],      %[ftmp2]    \n\t"   \
        "pmaddhw      %[ftmp13],     %[ftmp4],      %[ftmp1]    \n\t"   \
        "pmaddhw      %[ftmp14],     %[ftmp8],      %[ftmp2]    \n\t"   \
        "paddw        %[ftmp11],     %[ftmp11],     %[ftmp12]   \n\t"   \
        "paddw        %[ftmp13],     %[ftmp13],     %[ftmp14]   \n\t"   \
        TRANSPOSE_2W(%[ftmp11], %[ftmp13], %[ftmp3], %[ftmp4])          \
        "paddw        %[ftmp3],      %[ftmp3],      %[ftmp4]    \n\t"   \
        "psraw        %[ftmp3],      %[ftmp3],      %[ftmp0]    \n\t"   \
        "pmaddhw      %[ftmp11],     %[ftmp5],      %[ftmp1]    \n\t"   \
        "pmaddhw      %[ftmp12],     %[ftmp9],      %[ftmp2]    \n\t"   \
        "pmaddhw      %[ftmp13],     %[ftmp6],      %[ftmp1]    \n\t"   \
        "pmaddhw      %[ftmp14],     %[ftmp10],     %[ftmp2]    \n\t"   \
        "paddw        %[ftmp11],     %[ftmp11],     %[ftmp12]   \n\t"   \
        "paddw        %[ftmp13],     %[ftmp13],     %[ftmp14]   \n\t"   \
        TRANSPOSE_2W(%[ftmp11], %[ftmp13], %[ftmp5], %[ftmp6])          \
        "paddw        %[ftmp5],      %[ftmp5],      %[ftmp6]    \n\t"   \
        "psraw        %[ftmp5],      %[ftmp5],      %[ftmp0]    \n\t"   \
        "packsswh     %[ftmp3],      %[ftmp3],      %[ftmp5]    \n\t"   \
        "paddh        %[ftmp3],      %[ftmp3],      %[offset]   \n\t"   \
        "psrah        %[ftmp3],      %[ftmp3],      %[shift]    \n\t"   \
        "pxor         %[ftmp7],      %[ftmp7],      %[ftmp7]    \n\t"   \
        "pcmpgth      %[ftmp7],      %[ftmp3],      %[ftmp7]    \n\t"   \
        "pand         %[ftmp3],      %[ftmp3],      %[ftmp7]    \n\t"   \
        "packushb     %[ftmp3],      %[ftmp3],      %[ftmp3]    \n\t"   \
        MMI_USWC1(%[ftmp3], %[dst], 0x00)                               \
                                                                        \
        "daddi        %[x],          %[x],         -0x01        \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x08        \n\t"   \
        PTR_ADDIU    "%[dst],        %[dst],        0x04        \n\t"   \
        "bnez         %[x],          2b                         \n\t"   \
                                                                        \
        "daddi        %[y],          %[y],         -0x01        \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],     " #tmp_step "  \n\t"   \
        PTR_ADDIU    "%[dst],        %[dst],     " #dst_step "  \n\t"   \
        PTR_ADDU     "%[dst],        %[dst],        %[stride]   \n\t"   \
        PTR_ADDIU    "%[tmp],        %[tmp],        0x80        \n\t"   \
        "bnez         %[y],          1b                         \n\t"   \
        : RESTRICT_ASM_ALL64 RESTRICT_ASM_LOW32                         \
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]),                 \
          [ftmp2]"=&f"(ftmp[2]), [ftmp3]"=&f"(ftmp[3]),                 \
          [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),                 \
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]),                 \
          [ftmp8]"=&f"(ftmp[8]), [ftmp9]"=&f"(ftmp[9]),                 \
          [ftmp10]"=&f"(ftmp[10]), [ftmp11]"=&f"(ftmp[11]),             \
          [ftmp12]"=&f"(ftmp[12]), [ftmp13]"=&f"(ftmp[13]),             \
          [ftmp14]"=&f"(ftmp[14]),                                      \
          [dst]"+&r"(dst), [tmp]"+&r"(tmp), [y]"+&r"(y), [x]"=&r"(x),   \
          [offset]"+&f"(offset.f), [rtmp0]"=&r"(rtmp[0])                \
        : [filter]"r"(filter), [stride]"r"(dststride),                  \
          [shift]"f"(shift.f)                                           \
        : "memory"                                                      \
    );                                                                  \
}

PUT_HEVC_QPEL_UNI_HV(4, 1, -4, -4, -8);
PUT_HEVC_QPEL_UNI_HV(8, 2, -8, -8, -16);
PUT_HEVC_QPEL_UNI_HV(12, 3, -12, -12, -24);
PUT_HEVC_QPEL_UNI_HV(16, 4, -16, -16, -32);
PUT_HEVC_QPEL_UNI_HV(24, 6, -24, -24, -48);
PUT_HEVC_QPEL_UNI_HV(32, 8, -32, -32, -64);
PUT_HEVC_QPEL_UNI_HV(48, 12, -48, -48, -96);
PUT_HEVC_QPEL_UNI_HV(64, 16, -64, -64, -128);
