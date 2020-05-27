/*
 * Loongson SIMD optimized simple idct
 *
 * Copyright (c) 2015 Loongson Technology Corporation Limited
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
 *                    Zhang Shuangshuang <zhangshuangshuang@ict.ac.cn>
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

#include "idctdsp_mips.h"
#include "constants.h"
#include "libavutil/mips/asmdefs.h"
#include "libavutil/mips/mmiutils.h"
#include "libavutil/mem_internal.h"

#define W1  22725  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W2  21407  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W3  19266  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W4  16383  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W5  12873  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W6  8867   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W7  4520   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5

#define ROW_SHIFT 11
#define COL_SHIFT 20
#define DC_SHIFT 3

DECLARE_ALIGNED(16, const int16_t, W_arr)[46] = {
    W4,  W2,  W4,  W6,
    W1,  W3,  W5,  W7,
    W4,  W6, -W4, -W2,
    W3, -W7, -W1, -W5,
    W4, -W6, -W4,  W2,
    W5, -W1,  W7,  W3,
    W4, -W2,  W4, -W6,
    W7, -W5,  W3, -W1,
    1024, 0,  1024, 0, //ff_p32_1024 = 0x0000040000000400ULL
    0,   -1,  -1,  -1, //mask = 0xffffffffffff0000ULL
    32,  32,  32,  32  //ff_p16_32 = 0x0020002000200020ULL
};

void ff_simple_idct_8_mmi(int16_t *block)
{
    BACKUP_REG
    __asm__ volatile (

#define IDCT_ROW_COND_DC(src1, src2)                                  \
        "dmfc1        $11,      "#src1"                         \n\t" \
        "dmfc1        $12,      "#src2"                         \n\t" \
        "and          $11,       $11,       $9                  \n\t" \
        "or           $10,       $11,       $12                 \n\t" \
        "beqz         $10,       1f                             \n\t" \
                                                                      \
        "punpcklhw    $f30,     "#src1",   "#src2"              \n\t" \
        "punpckhhw    $f31,     "#src1",   "#src2"              \n\t" \
        /* s6, s4, s2, s0 */                                          \
        "punpcklhw   "#src1",    $f30,      $f31                \n\t" \
        /* s7, s5, s3, s1 */                                          \
        "punpckhhw   "#src2",    $f30,      $f31                \n\t" \
                                                                      \
        "pmaddhw      $f30,     "#src1",    $f18                \n\t" \
        "pmaddhw      $f31,     "#src2",    $f19                \n\t" \
        "paddw        $f28,      $f30,      $f31                \n\t" \
        "psubw        $f29,      $f30,      $f31                \n\t" \
        "punpcklwd    $f30,      $f28,      $f29                \n\t" \
        "punpckhwd    $f31,      $f28,      $f29                \n\t" \
        "paddw        $f26,      $f30,      $f31                \n\t" \
        "paddw        $f26,      $f26,      $f16                \n\t" \
        /* $f26: src[7], src[0] */                                    \
        "psraw        $f26,      $f26,      $f17                \n\t" \
                                                                      \
        "pmaddhw      $f30,     "#src1",    $f20                \n\t" \
        "pmaddhw      $f31,     "#src2",    $f21                \n\t" \
        "paddw        $f28,      $f30,      $f31                \n\t" \
        "psubw        $f29,      $f30,      $f31                \n\t" \
        "punpcklwd    $f30,      $f28,      $f29                \n\t" \
        "punpckhwd    $f31,      $f28,      $f29                \n\t" \
        "paddw        $f27,      $f30,      $f31                \n\t" \
        "paddw        $f27,      $f27,      $f16                \n\t" \
        /* $f27: src[6], src[1] */                                    \
        "psraw        $f27,      $f27,      $f17                \n\t" \
                                                                      \
        "pmaddhw      $f30,     "#src1",    $f22                \n\t" \
        "pmaddhw      $f31,     "#src2",    $f23                \n\t" \
        "paddw        $f28,      $f30,      $f31                \n\t" \
        "psubw        $f29,      $f30,      $f31                \n\t" \
        "punpcklwd    $f30,      $f28,      $f29                \n\t" \
        "punpckhwd    $f31,      $f28,      $f29                \n\t" \
        "paddw        $f28,      $f30,      $f31                \n\t" \
        "paddw        $f28,      $f28,      $f16                \n\t" \
        /* $f28: src[5], src[2] */                                    \
        "psraw        $f28,      $f28,      $f17                \n\t" \
                                                                      \
        "pmaddhw      $f30,     "#src1",    $f24                \n\t" \
        "pmaddhw      $f31,     "#src2",    $f25                \n\t" \
        "paddw       "#src1",    $f30,      $f31                \n\t" \
        "psubw       "#src2",    $f30,      $f31                \n\t" \
        "punpcklwd    $f30,     "#src1",   "#src2"              \n\t" \
        "punpckhwd    $f31,     "#src1",   "#src2"              \n\t" \
        "paddw        $f29,      $f30,      $f31                \n\t" \
        "paddw        $f29,      $f29,      $f16                \n\t" \
        /* $f29: src[4], src[3] */                                    \
        "psraw        $f29,      $f29,      $f17                \n\t" \
                                                                      \
        "punpcklhw   "#src1",    $f26,      $f27                \n\t" \
        "punpckhhw    $f30,      $f27,      $f26                \n\t" \
        "punpcklhw    $f31,      $f28,      $f29                \n\t" \
        "punpckhhw   "#src2",    $f29,      $f28                \n\t" \
        /* src[3], src[2], src[1], src[0] */                          \
        "punpcklwd   "#src1",   "#src1",    $f31                \n\t" \
        /* src[7], src[6], src[5], src[4] */                          \
        "punpcklwd   "#src2",   "#src2",    $f30                \n\t" \
        "j                       2f                             \n\t" \
                                                                      \
        "1:                                                     \n\t" \
        "li           $10,       3                              \n\t" \
        "dmtc1        $10,       $f30                           \n\t" \
        "psllh        $f28,     "#src1",    $f30                \n\t" \
        "dmtc1        $9,        $f31                           \n\t" \
        "punpcklhw    $f29,      $f28,      $f28                \n\t" \
        "and          $f29,      $f29,      $f31                \n\t" \
        "paddw        $f28,      $f28,      $f29                \n\t" \
        "punpcklwd   "#src1",    $f28,      $f28                \n\t" \
        "punpcklwd   "#src2",    $f28,      $f28                \n\t" \
        "2:                                                     \n\t" \

        /* idctRowCondDC row0~8 */

        /* load W */
        "gslqc1       $f19,      $f18,      0x00(%[w_arr])      \n\t"
        "gslqc1       $f21,      $f20,      0x10(%[w_arr])      \n\t"
        "gslqc1       $f23,      $f22,      0x20(%[w_arr])      \n\t"
        "gslqc1       $f25,      $f24,      0x30(%[w_arr])      \n\t"
        "gslqc1       $f17,      $f16,      0x40(%[w_arr])      \n\t"
        /* load source in block */
        "gslqc1       $f1,       $f0,       0x00(%[block])      \n\t"
        "gslqc1       $f3,       $f2,       0x10(%[block])      \n\t"
        "gslqc1       $f5,       $f4,       0x20(%[block])      \n\t"
        "gslqc1       $f7,       $f6,       0x30(%[block])      \n\t"
        "gslqc1       $f9,       $f8,       0x40(%[block])      \n\t"
        "gslqc1       $f11,      $f10,      0x50(%[block])      \n\t"
        "gslqc1       $f13,      $f12,      0x60(%[block])      \n\t"
        "gslqc1       $f15,      $f14,      0x70(%[block])      \n\t"

        /* $9: mask ; $f17: ROW_SHIFT */
        "dmfc1        $9,        $f17                           \n\t"
        "li           $10,       11                             \n\t"
        "mtc1         $10,       $f17                           \n\t"
        IDCT_ROW_COND_DC($f0,$f1)
        IDCT_ROW_COND_DC($f2,$f3)
        IDCT_ROW_COND_DC($f4,$f5)
        IDCT_ROW_COND_DC($f6,$f7)
        IDCT_ROW_COND_DC($f8,$f9)
        IDCT_ROW_COND_DC($f10,$f11)
        IDCT_ROW_COND_DC($f12,$f13)
        IDCT_ROW_COND_DC($f14,$f15)

#define IDCT_COL_CASE1(src, out1, out2)                               \
        "pmaddhw      $f26,     "#src",     $f18                \n\t" \
        "pmaddhw      $f27,     "#src",     $f20                \n\t" \
        "pmaddhw      $f28,     "#src",     $f22                \n\t" \
        "pmaddhw      $f29,     "#src",     $f24                \n\t" \
                                                                      \
        "punpcklwd    $f30,      $f26,      $f26                \n\t" \
        "punpckhwd    $f31,      $f26,      $f26                \n\t" \
        /* $f26: src[0], src[56] */                                   \
        "paddw        $f26,      $f30,      $f31                \n\t" \
        "punpcklwd    $f30,      $f27,      $f27                \n\t" \
        "punpckhwd    $f31,      $f27,      $f27                \n\t" \
        /* $f27: src[8], src[48] */                                   \
        "paddw        $f27,      $f30,      $f31                \n\t" \
        "punpcklwd    $f30,      $f28,      $f28                \n\t" \
        "punpckhwd    $f31,      $f28,      $f28                \n\t" \
        /* $f28: src[16], src[40] */                                  \
        "paddw        $f28,      $f30,      $f31                \n\t" \
        "punpcklwd    $f30,      $f29,      $f29                \n\t" \
        "punpckhwd    $f31,      $f29,      $f29                \n\t" \
        /* $f29: src[24], src[32] */                                  \
        "paddw        $f29,      $f30,      $f31                \n\t" \
                                                                      \
        /* out1: src[24], src[16], src[8], src[0] */                  \
        /* out2: src[56], src[48], src[40], src[32] */                \
        "punpcklhw    $f30,      $f26,      $f27                \n\t" \
        "punpcklhw    $f31,      $f28,      $f29                \n\t" \
        "punpckhwd   "#out1",    $f30,      $f31                \n\t" \
        "psrah       "#out1",   "#out1",    $f16                \n\t" \
        "punpcklhw    $f30,      $f27,      $f26                \n\t" \
        "punpcklhw    $f31,      $f29,      $f28                \n\t" \
        "punpckhwd   "#out2",    $f31,      $f30                \n\t" \
        "psrah       "#out2",   "#out2",    $f16                \n\t"

#define IDCT_COL_CASE2(src1, src2, out1, out2)                        \
        "pmaddhw      $f28,     "#src1",    $f18                \n\t" \
        "pmaddhw      $f29,     "#src2",    $f19                \n\t" \
        "paddw        $f30,      $f28,      $f29                \n\t" \
        "psubw        $f31,      $f28,      $f29                \n\t" \
        "punpcklwd    $f28,      $f30,      $f31                \n\t" \
        "punpckhwd    $f29,      $f30,      $f31                \n\t" \
        "pmaddhw      $f30,     "#src1",    $f20                \n\t" \
        "pmaddhw      $f31,     "#src2",    $f21                \n\t" \
        /* $f26: src[0], src[56] */                                   \
        "paddw        $f26,      $f28,      $f29                \n\t" \
        "paddw        $f28,      $f30,      $f31                \n\t" \
        "psubw        $f29,      $f30,      $f31                \n\t" \
        "punpcklwd    $f30,      $f28,      $f29                \n\t" \
        "punpckhwd    $f31,      $f28,      $f29                \n\t" \
        "pmaddhw      $f28,     "#src1",    $f22                \n\t" \
        "pmaddhw      $f29,     "#src2",    $f23                \n\t" \
        /* $f27: src[8], src[48] */                                   \
        "paddw        $f27,      $f30,      $f31                \n\t" \
        "paddw        $f30,      $f28,      $f29                \n\t" \
        "psubw        $f31,      $f28,      $f29                \n\t" \
        "punpcklwd    $f28,      $f30,      $f31                \n\t" \
        "punpckhwd    $f29,      $f30,      $f31                \n\t" \
        "pmaddhw      $f30,     "#src1",    $f24                \n\t" \
        "pmaddhw      $f31,     "#src2",    $f25                \n\t" \
        /* $f28: src[16], src[40] */                                  \
        "paddw        $f28,      $f28,      $f29                \n\t" \
        "paddw       "#out1",    $f30,      $f31                \n\t" \
        "psubw       "#out2",    $f30,      $f31                \n\t" \
        "punpcklwd    $f30,     "#out1",   "#out2"              \n\t" \
        "punpckhwd    $f31,     "#out1",   "#out2"              \n\t" \
        /* $f29: src[24], src[32] */                                  \
        "paddw        $f29,      $f30,      $f31                \n\t" \
                                                                      \
        /* out1: src[24], src[16], src[8], src[0] */                  \
        /* out2: src[56], src[48], src[40], src[32] */                \
        "punpcklhw   "#out1",    $f26,      $f27                \n\t" \
        "punpckhhw   "#out2",    $f27,      $f26                \n\t" \
        "punpcklhw    $f30,      $f28,      $f29                \n\t" \
        "punpckhhw    $f31,      $f29,      $f28                \n\t" \
        "punpckhwd   "#out1",   "#out1",    $f30                \n\t" \
        "punpckhwd   "#out2",    $f31,     "#out2"              \n\t" \
        "psrah       "#out1",   "#out1",    $f16                \n\t" \
        "psrah       "#out2",   "#out2",    $f16                \n\t"


        /* idctSparseCol col0~3 */

        /* $f17: ff_p16_32; $f16: COL_SHIFT-16 */
        "gsldlc1      $f17,      0x57(%[w_arr])                 \n\t"
        "gsldrc1      $f17,      0x50(%[w_arr])                 \n\t"
        "li           $10,       4                              \n\t"
        "dmtc1        $10,       $f16                           \n\t"
        "paddh        $f0,       $f0,       $f17                \n\t"
        /* Transpose row[0,2,4,6] */
        "punpcklhw    $f26,      $f0,       $f4                 \n\t"
        "punpckhhw    $f27,      $f0,       $f4                 \n\t"
        "punpcklhw    $f28,      $f8,       $f12                \n\t"
        "punpckhhw    $f29,      $f8,       $f12                \n\t"
        "punpcklwd    $f0,       $f26,      $f28                \n\t"
        "punpckhwd    $f4,       $f26,      $f28                \n\t"
        "punpcklwd    $f8,       $f27,      $f29                \n\t"
        "punpckhwd    $f12,      $f27,      $f29                \n\t"

        "or           $f26,      $f2,       $f6                 \n\t"
        "or           $f26,      $f26,      $f10                \n\t"
        "or           $f26,      $f26,      $f14                \n\t"
        "dmfc1        $10,       $f26                           \n\t"
        "bnez         $10,       1f                             \n\t"
        /* case1: In this case, row[1,3,5,7] are all zero */
        /* col0: $f0: col[24,16,8,0]; $f2: col[56,48,40,32] */
        IDCT_COL_CASE1($f0, $f0, $f2)
        /* col1: $f4: col[25,17,9,1]; $f6: col[57,49,41,33] */
        IDCT_COL_CASE1($f4, $f4, $f6)
        /* col2: $f8: col[26,18,10,2]; $f10: col[58,50,42,34] */
        IDCT_COL_CASE1($f8, $f8, $f10)
        /* col3: $f12: col[27,19,11,3]; $f14: col[59,51,43,35] */
        IDCT_COL_CASE1($f12, $f12, $f14)
        "j                                  2f                  \n\t"

        "1:                                                     \n\t"
        /* case2: row[1,3,5,7] are not all zero */
        /* Transpose */
        "punpcklhw    $f26,      $f2,       $f6                 \n\t"
        "punpckhhw    $f27,      $f2,       $f6                 \n\t"
        "punpcklhw    $f28,      $f10,      $f14                \n\t"
        "punpckhhw    $f29,      $f10,      $f14                \n\t"
        "punpcklwd    $f2,       $f26,      $f28                \n\t"
        "punpckhwd    $f6,       $f26,      $f28                \n\t"
        "punpcklwd    $f10,      $f27,      $f29                \n\t"
        "punpckhwd    $f14,      $f27,      $f29                \n\t"

        /* col0: $f0: col[24,16,8,0]; $f2: col[56,48,40,32] */
        IDCT_COL_CASE2($f0, $f2, $f0, $f2)
        /* col1: $f4: col[25,17,9,1]; $f6: col[57,49,41,33] */
        IDCT_COL_CASE2($f4, $f6, $f4, $f6)
        /* col2: $f8: col[26,18,10,2]; $f10: col[58,50,42,34] */
        IDCT_COL_CASE2($f8, $f10, $f8, $f10)
        /* col3: $f12: col[27,19,11,3]; $f14: col[59,51,43,35] */
        IDCT_COL_CASE2($f12, $f14, $f12, $f14)

        "2:                                                     \n\t"
        /* Transpose */
        "punpcklhw    $f26,      $f0,       $f4                 \n\t"
        "punpckhhw    $f27,      $f0,       $f4                 \n\t"
        "punpcklhw    $f28,      $f8,       $f12                \n\t"
        "punpckhhw    $f29,      $f8,       $f12                \n\t"
        "punpcklwd    $f0,       $f26,      $f28                \n\t"
        "punpckhwd    $f4,       $f26,      $f28                \n\t"
        "punpcklwd    $f8,       $f27,      $f29                \n\t"
        "punpckhwd    $f12,      $f27,      $f29                \n\t"
        /* Transpose */
        "punpcklhw    $f26,      $f2,       $f6                 \n\t"
        "punpckhhw    $f27,      $f2,       $f6                 \n\t"
        "punpcklhw    $f28,      $f10,      $f14                \n\t"
        "punpckhhw    $f29,      $f10,      $f14                \n\t"
        "punpcklwd    $f2,       $f26,      $f28                \n\t"
        "punpckhwd    $f6,       $f26,      $f28                \n\t"
        "punpcklwd    $f10,      $f27,      $f29                \n\t"
        "punpckhwd    $f14,      $f27,      $f29                \n\t"

        /* idctSparseCol col4~7 */

        "paddh        $f1,       $f1,       $f17                \n\t"
        /* Transpose */
        "punpcklhw    $f26,      $f1,       $f5                 \n\t"
        "punpckhhw    $f27,      $f1,       $f5                 \n\t"
        "punpcklhw    $f28,      $f9,       $f13                \n\t"
        "punpckhhw    $f29,      $f9,       $f13                \n\t"
        "punpcklwd    $f1,       $f26,      $f28                \n\t"
        "punpckhwd    $f5,       $f26,      $f28                \n\t"
        "punpcklwd    $f9,       $f27,      $f29                \n\t"
        "punpckhwd    $f13,      $f27,      $f29                \n\t"

        "or           $f26,      $f3,       $f7                 \n\t"
        "or           $f26,      $f26,      $f11                \n\t"
        "or           $f26,      $f26,      $f15                \n\t"
        "dmfc1        $10,       $f26                           \n\t"
        "bnez         $10,       1f                             \n\t"
        /* case1: In this case, row[1,3,5,7] are all zero */
        /* col4: $f1: col[24,16,8,0]; $f3: col[56,48,40,32] */
        IDCT_COL_CASE1($f1, $f1, $f3)
        /* col5: $f5: col[25,17,9,1]; $f7: col[57,49,41,33] */
        IDCT_COL_CASE1($f5, $f5, $f7)
        /* col6: $f9: col[26,18,10,2]; $f11: col[58,50,42,34] */
        IDCT_COL_CASE1($f9, $f9, $f11)
        /* col7: $f13: col[27,19,11,3]; $f15: col[59,51,43,35] */
        IDCT_COL_CASE1($f13, $f13, $f15)
        "j                                  2f                  \n\t"

        "1:                                                     \n\t"
        /* case2: row[1,3,5,7] are not all zero */
        /* Transpose */
        "punpcklhw    $f26,      $f3,       $f7                 \n\t"
        "punpckhhw    $f27,      $f3,       $f7                 \n\t"
        "punpcklhw    $f28,      $f11,      $f15                \n\t"
        "punpckhhw    $f29,      $f11,      $f15                \n\t"
        "punpcklwd    $f3,       $f26,      $f28                \n\t"
        "punpckhwd    $f7,       $f26,      $f28                \n\t"
        "punpcklwd    $f11,      $f27,      $f29                \n\t"
        "punpckhwd    $f15,      $f27,      $f29                \n\t"

        /* col4: $f1: col[24,16,8,0]; $f3: col[56,48,40,32] */
        IDCT_COL_CASE2($f1, $f3, $f1, $f3)
        /* col5: $f5: col[25,17,9,1]; $f7: col[57,49,41,33] */
        IDCT_COL_CASE2($f5, $f7, $f5, $f7)
        /* col6: $f9: col[26,18,10,2]; $f11: col[58,50,42,34] */
        IDCT_COL_CASE2($f9, $f11, $f9, $f11)
        /* col7: $f13: col[27,19,11,3]; $f15: col[59,51,43,35] */
        IDCT_COL_CASE2($f13, $f15, $f13, $f15)

        "2:                                                     \n\t"
        /* Transpose */
        "punpcklhw    $f26,      $f1,       $f5                 \n\t"
        "punpckhhw    $f27,      $f1,       $f5                 \n\t"
        "punpcklhw    $f28,      $f9,       $f13                \n\t"
        "punpckhhw    $f29,      $f9,       $f13                \n\t"
        "punpcklwd    $f1,       $f26,      $f28                \n\t"
        "punpckhwd    $f5,       $f26,      $f28                \n\t"
        "punpcklwd    $f9,       $f27,      $f29                \n\t"
        "punpckhwd    $f13,      $f27,      $f29                \n\t"
        /* Transpose */
        "punpcklhw    $f26,      $f3,       $f7                 \n\t"
        "punpckhhw    $f27,      $f3,       $f7                 \n\t"
        "punpcklhw    $f28,      $f11,      $f15                \n\t"
        "punpckhhw    $f29,      $f11,      $f15                \n\t"
        "punpcklwd    $f3,       $f26,      $f28                \n\t"
        "punpckhwd    $f7,       $f26,      $f28                \n\t"
        "punpcklwd    $f11,      $f27,      $f29                \n\t"
        "punpckhwd    $f15,      $f27,      $f29                \n\t"
        /* Store */
        "gssqc1       $f1,       $f0,       0x00(%[block])      \n\t"
        "gssqc1       $f5,       $f4,       0x10(%[block])      \n\t"
        "gssqc1       $f9,       $f8,       0x20(%[block])      \n\t"
        "gssqc1       $f13,      $f12,      0x30(%[block])      \n\t"
        "gssqc1       $f3,       $f2,       0x40(%[block])      \n\t"
        "gssqc1       $f7,       $f6,       0x50(%[block])      \n\t"
        "gssqc1       $f11,      $f10,      0x60(%[block])      \n\t"
        "gssqc1       $f15,      $f14,      0x70(%[block])      \n\t"

        : [block]"+&r"(block)
        : [w_arr]"r"(W_arr)
        : "memory"
    );

    RECOVER_REG
}

void ff_simple_idct_put_8_mmi(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    ff_simple_idct_8_mmi(block);
    ff_put_pixels_clamped_mmi(block, dest, line_size);
}
void ff_simple_idct_add_8_mmi(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    ff_simple_idct_8_mmi(block);
    ff_add_pixels_clamped_mmi(block, dest, line_size);
}
