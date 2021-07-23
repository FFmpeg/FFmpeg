/*
 * Copyright (c) 2018 gxw <guxiwei-hf@loongson.cn>
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

#include "vp3dsp_mips.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mips/mmiutils.h"
#include "libavutil/common.h"
#include "libavcodec/rnd_avg.h"

#define LOAD_CONST(dst, value)                        \
    "li     %[tmp1],      "#value"              \n\t" \
    "dmtc1  %[tmp1],      "#dst"                \n\t" \
    "pshufh "#dst",       "#dst",     %[ftmp10] \n\t"

static void idct_row_mmi(int16_t *input)
{
    double ftmp[23];
    uint64_t tmp[2];
    __asm__ volatile (
        "pxor       %[ftmp10],      %[ftmp10],        %[ftmp10] \n\t"
        LOAD_CONST(%[csth_1], 1)
        "li         %[tmp0],        0x02                        \n\t"
        "1:                                                     \n\t"
        /* Load input */
        "ldc1       %[ftmp0],       0x00(%[input])              \n\t"
        "ldc1       %[ftmp1],       0x10(%[input])              \n\t"
        "ldc1       %[ftmp2],       0x20(%[input])              \n\t"
        "ldc1       %[ftmp3],       0x30(%[input])              \n\t"
        "ldc1       %[ftmp4],       0x40(%[input])              \n\t"
        "ldc1       %[ftmp5],       0x50(%[input])              \n\t"
        "ldc1       %[ftmp6],       0x60(%[input])              \n\t"
        "ldc1       %[ftmp7],       0x70(%[input])              \n\t"
        LOAD_CONST(%[ftmp8], 64277)
        LOAD_CONST(%[ftmp9], 12785)
        "pmulhh     %[A],           %[ftmp9],         %[ftmp7]  \n\t"
        "pcmpgth    %[C],           %[ftmp10],        %[ftmp1]  \n\t"
        "por        %[mask],        %[C],             %[csth_1] \n\t"
        "pmullh     %[B],           %[ftmp1],         %[mask]   \n\t"
        "pmulhuh    %[B],           %[ftmp8],         %[B]      \n\t"
        "pmullh     %[B],           %[B],             %[mask]   \n\t"
        "paddh      %[A],           %[A],             %[B]      \n\t"
        "paddh      %[A],           %[A],             %[C]      \n\t"
        "pcmpgth    %[D],           %[ftmp10],        %[ftmp7]  \n\t"
        "por        %[mask],        %[D],             %[csth_1] \n\t"
        "pmullh     %[ftmp7],       %[ftmp7],         %[mask]   \n\t"
        "pmulhuh    %[B],           %[ftmp8],         %[ftmp7]  \n\t"
        "pmullh     %[B],           %[B],             %[mask]   \n\t"
        "pmulhh     %[C],           %[ftmp9],         %[ftmp1]  \n\t"
        "psubh      %[B],           %[C],             %[B]      \n\t"
        "psubh      %[B],           %[B],             %[D]      \n\t"

        LOAD_CONST(%[ftmp8], 54491)
        LOAD_CONST(%[ftmp9], 36410)
        "pcmpgth    %[Ad],          %[ftmp10],        %[ftmp5]  \n\t"
        "por        %[mask],        %[Ad],            %[csth_1] \n\t"
        "pmullh     %[ftmp1],       %[ftmp5],         %[mask]   \n\t"
        "pmulhuh    %[C],           %[ftmp9],         %[ftmp1]  \n\t"
        "pmullh     %[C],           %[C],             %[mask]   \n\t"
        "pcmpgth    %[Bd],          %[ftmp10],        %[ftmp3]  \n\t"
        "por        %[mask],        %[Bd],            %[csth_1] \n\t"
        "pmullh     %[D],           %[ftmp3],         %[mask]   \n\t"
        "pmulhuh    %[D],           %[ftmp8],         %[D]      \n\t"
        "pmullh     %[D],           %[D],             %[mask]   \n\t"
        "paddh      %[C],           %[C],             %[D]      \n\t"
        "paddh      %[C],           %[C],             %[Ad]     \n\t"
        "paddh      %[C],           %[C],             %[Bd]     \n\t"
        "pcmpgth    %[Bd],          %[ftmp10],        %[ftmp3]  \n\t"
        "por        %[mask],        %[Bd],            %[csth_1] \n\t"
        "pmullh     %[ftmp1],       %[ftmp3],         %[mask]   \n\t"
        "pmulhuh    %[D],           %[ftmp9],         %[ftmp1]  \n\t"
        "pmullh     %[D],           %[D],             %[mask]   \n\t"
        "pcmpgth    %[Ed],          %[ftmp10],        %[ftmp5]  \n\t"
        "por        %[mask],        %[Ed],            %[csth_1] \n\t"
        "pmullh     %[Ad],          %[ftmp5],         %[mask]   \n\t"
        "pmulhuh    %[Ad],          %[ftmp8],         %[Ad]     \n\t"
        "pmullh     %[Ad],          %[Ad],            %[mask]   \n\t"
        "psubh      %[D],           %[Ad],            %[D]      \n\t"
        "paddh      %[D],           %[D],             %[Ed]     \n\t"
        "psubh      %[D],           %[D],             %[Bd]     \n\t"

        LOAD_CONST(%[ftmp8], 46341)
        "psubh      %[Ad],          %[A],             %[C]      \n\t"
        "pcmpgth    %[Bd],          %[ftmp10],        %[Ad]     \n\t"
        "por        %[mask],        %[Bd],            %[csth_1] \n\t"
        "pmullh     %[Ad],          %[Ad],            %[mask]   \n\t"
        "pmulhuh    %[Ad],          %[ftmp8],         %[Ad]     \n\t"
        "pmullh     %[Ad],          %[Ad],            %[mask]   \n\t"
        "paddh      %[Ad],          %[Ad],            %[Bd]     \n\t"
        "psubh      %[Bd],          %[B],             %[D]      \n\t"
        "pcmpgth    %[Cd],          %[ftmp10],        %[Bd]     \n\t"
        "por        %[mask],        %[Cd],            %[csth_1] \n\t"
        "pmullh     %[Bd],          %[Bd],            %[mask]   \n\t"
        "pmulhuh    %[Bd],          %[ftmp8],         %[Bd]     \n\t"
        "pmullh     %[Bd],          %[Bd],            %[mask]   \n\t"
        "paddh      %[Bd],          %[Bd],            %[Cd]     \n\t"
        "paddh      %[Cd],          %[A],             %[C]      \n\t"
        "paddh      %[Dd],          %[B],             %[D]      \n\t"
        "paddh      %[A],           %[ftmp0],         %[ftmp4]  \n\t"
        "pcmpgth    %[B],           %[ftmp10],        %[A]      \n\t"
        "por        %[mask],        %[B],             %[csth_1] \n\t"
        "pmullh     %[A],           %[A],             %[mask]   \n\t"
        "pmulhuh    %[A],           %[ftmp8],         %[A]      \n\t"
        "pmullh     %[A],           %[A],             %[mask]   \n\t"
        "paddh      %[A],           %[A],             %[B]      \n\t"
        "psubh      %[B],           %[ftmp0],         %[ftmp4]  \n\t"
        "pcmpgth    %[C],           %[ftmp10],        %[B]      \n\t"
        "por        %[mask],        %[C],             %[csth_1] \n\t"
        "pmullh     %[B],           %[B],             %[mask]   \n\t"
        "pmulhuh    %[B],           %[ftmp8],         %[B]      \n\t"
        "pmullh     %[B],           %[B],             %[mask]   \n\t"
        "paddh      %[B],           %[B],             %[C]      \n\t"

        LOAD_CONST(%[ftmp8], 60547)
        LOAD_CONST(%[ftmp9], 25080)
        "pmulhh     %[C],           %[ftmp9],         %[ftmp6]  \n\t"
        "pcmpgth    %[D],           %[ftmp10],        %[ftmp2]  \n\t"
        "por        %[mask],        %[D],             %[csth_1] \n\t"
        "pmullh     %[Ed],          %[ftmp2],         %[mask]   \n\t"
        "pmulhuh    %[Ed],          %[ftmp8],         %[Ed]     \n\t"
        "pmullh     %[Ed],          %[Ed],            %[mask]   \n\t"
        "paddh      %[C],           %[C],             %[Ed]     \n\t"
        "paddh      %[C],           %[C],             %[D]      \n\t"
        "pcmpgth    %[Ed],          %[ftmp10],        %[ftmp6]  \n\t"
        "por        %[mask],        %[Ed],            %[csth_1] \n\t"
        "pmullh     %[ftmp6],       %[ftmp6],         %[mask]   \n\t"
        "pmulhuh    %[D],           %[ftmp8],         %[ftmp6]  \n\t"
        "pmullh     %[D],           %[D],             %[mask]   \n\t"
        "pmulhh     %[Gd],          %[ftmp9],         %[ftmp2]  \n\t"
        "psubh      %[D],           %[Gd],            %[D]      \n\t"
        "psubh      %[D],           %[D],             %[Ed]     \n\t"
        "psubh      %[Ed],          %[A],             %[C]      \n\t"
        "paddh      %[Gd],          %[A],             %[C]      \n\t"
        "paddh      %[A],           %[B],             %[Ad]     \n\t"
        "psubh      %[C],           %[B],             %[Ad]     \n\t"
        "psubh      %[B],           %[Bd],            %[D]      \n\t"
        "paddh      %[D],           %[Bd],            %[D]      \n\t"
        /* Final sequence of operations over-write original inputs */
        "paddh      %[ftmp0],       %[Gd],            %[Cd]     \n\t"
        "paddh      %[ftmp1],       %[A],             %[D]      \n\t"
        "psubh      %[ftmp2],       %[A],             %[D]      \n\t"
        "paddh      %[ftmp3],       %[Ed],            %[Dd]     \n\t"
        "psubh      %[ftmp4],       %[Ed],            %[Dd]     \n\t"
        "paddh      %[ftmp5],       %[C],             %[B]      \n\t"
        "psubh      %[ftmp6],       %[C],             %[B]      \n\t"
        "psubh      %[ftmp7],       %[Gd],            %[Cd]     \n\t"
        "sdc1       %[ftmp0],       0x00(%[input])              \n\t"
        "sdc1       %[ftmp1],       0x10(%[input])              \n\t"
        "sdc1       %[ftmp2],       0x20(%[input])              \n\t"
        "sdc1       %[ftmp3],       0x30(%[input])              \n\t"
        "sdc1       %[ftmp4],       0x40(%[input])              \n\t"
        "sdc1       %[ftmp5],       0x50(%[input])              \n\t"
        "sdc1       %[ftmp6],       0x60(%[input])              \n\t"
        "sdc1       %[ftmp7],       0x70(%[input])              \n\t"
        PTR_ADDU   "%[tmp0],        %[tmp0],          -0x01     \n\t"
        PTR_ADDIU  "%[input],       %[input],         0x08      \n\t"
        "bnez       %[tmp0],        1b                          \n\t"
        : [input]"+&r"(input), [tmp0]"=&r"(tmp[0]), [tmp1]"=&r"(tmp[1]),
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]), [ftmp2]"=&f"(ftmp[2]),
          [ftmp3]"=&f"(ftmp[3]), [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]), [ftmp8]"=&f"(ftmp[8]),
          [ftmp9]"=&f"(ftmp[9]), [ftmp10]"=&f"(ftmp[10]), [mask]"=&f"(ftmp[11]),
          [A]"=&f"(ftmp[12]), [B]"=&f"(ftmp[13]), [C]"=&f"(ftmp[14]),
          [D]"=&f"(ftmp[15]), [Ad]"=&f"(ftmp[16]), [Bd]"=&f"(ftmp[17]),
          [Cd]"=&f"(ftmp[18]), [Dd]"=&f"(ftmp[19]), [Ed]"=&f"(ftmp[20]),
          [Gd]"=&f"(ftmp[21]), [csth_1]"=&f"(ftmp[22])
        :
        : "memory"
    );
}

static void idct_column_true_mmi(uint8_t *dst, int stride, int16_t *input)
{
    uint8_t temp_value[8];
    double ftmp[23];
    uint64_t tmp[2];
    for (int i = 0; i < 8; ++i)
        temp_value[i] = av_clip_uint8(128 + ((46341 * input[i << 3] + (8 << 16)) >> 20));
    __asm__ volatile (
        "pxor       %[ftmp10],      %[ftmp10],          %[ftmp10] \n\t"
        "li         %[tmp0],        0x02                          \n\t"
        "1:                                                       \n\t"
        "ldc1       %[ftmp0],       0x00(%[input])                \n\t"
        "ldc1       %[ftmp4],       0x08(%[input])                \n\t"
        "ldc1       %[ftmp1],       0x10(%[input])                \n\t"
        "ldc1       %[ftmp5],       0x18(%[input])                \n\t"
        "ldc1       %[ftmp2],       0x20(%[input])                \n\t"
        "ldc1       %[ftmp6],       0x28(%[input])                \n\t"
        "ldc1       %[ftmp3],       0x30(%[input])                \n\t"
        "ldc1       %[ftmp7],       0x38(%[input])                \n\t"
        TRANSPOSE_4H(%[ftmp0], %[ftmp1], %[ftmp2], %[ftmp3],
                     %[A], %[B], %[C], %[D])
        TRANSPOSE_4H(%[ftmp4], %[ftmp5], %[ftmp6], %[ftmp7],
                     %[A], %[B], %[C], %[D])
        LOAD_CONST(%[ftmp8], 64277)
        LOAD_CONST(%[ftmp9], 12785)
        LOAD_CONST(%[Gd], 1)
        "pmulhh     %[A],           %[ftmp9],           %[ftmp7]  \n\t"
        "pcmpgth    %[C],           %[ftmp10],          %[ftmp1]  \n\t"
        "por        %[mask],        %[C],               %[Gd]     \n\t"
        "pmullh     %[B],           %[ftmp1],           %[mask]   \n\t"
        "pmulhuh    %[B],           %[ftmp8],           %[B]      \n\t"
        "pmullh     %[B],           %[B],               %[mask]   \n\t"
        "paddh      %[A],           %[A],               %[B]      \n\t"
        "paddh      %[A],           %[A],               %[C]      \n\t"
        "pcmpgth    %[D],           %[ftmp10],          %[ftmp7]  \n\t"
        "por        %[mask],        %[D],               %[Gd]     \n\t"
        "pmullh     %[Ad],          %[ftmp7],           %[mask]   \n\t"
        "pmulhuh    %[B],           %[ftmp8],           %[Ad]     \n\t"
        "pmullh     %[B],           %[B],               %[mask]   \n\t"
        "pmulhh     %[C],           %[ftmp9],           %[ftmp1]  \n\t"
        "psubh      %[B],           %[C],               %[B]      \n\t"
        "psubh      %[B],           %[B],               %[D]      \n\t"

        LOAD_CONST(%[ftmp8], 54491)
        LOAD_CONST(%[ftmp9], 36410)
        "pcmpgth    %[Ad],          %[ftmp10],          %[ftmp5]  \n\t"
        "por        %[mask],        %[Ad],              %[Gd]     \n\t"
        "pmullh     %[Cd],          %[ftmp5],           %[mask]   \n\t"
        "pmulhuh    %[C],           %[ftmp9],           %[Cd]     \n\t"
        "pmullh     %[C],           %[C],               %[mask]   \n\t"
        "pcmpgth    %[Bd],          %[ftmp10],          %[ftmp3]  \n\t"
        "por        %[mask],        %[Bd],              %[Gd]     \n\t"
        "pmullh     %[D],           %[ftmp3],           %[mask]   \n\t"
        "pmulhuh    %[D],           %[ftmp8],           %[D]      \n\t"
        "pmullh     %[D],           %[D],               %[mask]   \n\t"
        "paddh      %[C],           %[C],               %[D]      \n\t"
        "paddh      %[C],           %[C],               %[Ad]     \n\t"
        "paddh      %[C],           %[C],               %[Bd]     \n\t"
        "pcmpgth    %[Bd],          %[ftmp10],          %[ftmp3]  \n\t"
        "por        %[mask],        %[Bd],              %[Gd]     \n\t"
        "pmullh     %[Cd],          %[ftmp3],           %[mask]   \n\t"
        "pmulhuh    %[D],           %[ftmp9],           %[Cd]     \n\t"
        "pmullh     %[D],           %[D],               %[mask]   \n\t"
        "pcmpgth    %[Ed],          %[ftmp10],          %[ftmp5]  \n\t"
        "por        %[mask],        %[Ed],              %[Gd]     \n\t"
        "pmullh     %[Ad],          %[ftmp5],           %[mask]   \n\t"
        "pmulhuh    %[Ad],          %[ftmp8],           %[Ad]     \n\t"
        "pmullh     %[Ad],          %[Ad],              %[mask]   \n\t"
        "psubh      %[D],           %[Ad],              %[D]      \n\t"
        "paddh      %[D],           %[D],               %[Ed]     \n\t"
        "psubh      %[D],           %[D],               %[Bd]     \n\t"

        LOAD_CONST(%[ftmp8], 46341)
        "psubh      %[Ad],          %[A],             %[C]        \n\t"
        "pcmpgth    %[Bd],          %[ftmp10],        %[Ad]       \n\t"
        "por        %[mask],        %[Bd],            %[Gd]       \n\t"
        "pmullh     %[Ad],          %[Ad],            %[mask]     \n\t"
        "pmulhuh    %[Ad],          %[ftmp8],         %[Ad]       \n\t"
        "pmullh     %[Ad],          %[Ad],            %[mask]     \n\t"
        "paddh      %[Ad],          %[Ad],            %[Bd]       \n\t"
        "psubh      %[Bd],          %[B],             %[D]        \n\t"
        "pcmpgth    %[Cd],          %[ftmp10],        %[Bd]       \n\t"
        "por        %[mask],        %[Cd],            %[Gd]       \n\t"
        "pmullh     %[Bd],          %[Bd],            %[mask]     \n\t"
        "pmulhuh    %[Bd],          %[ftmp8],         %[Bd]       \n\t"
        "pmullh     %[Bd],          %[Bd],            %[mask]     \n\t"
        "paddh      %[Bd],          %[Bd],            %[Cd]       \n\t"
        "paddh      %[Cd],          %[A],             %[C]        \n\t"
        "paddh      %[Dd],          %[B],             %[D]        \n\t"

        LOAD_CONST(%[Ed], 2056)
        "paddh      %[A],           %[ftmp0],         %[ftmp4]    \n\t"
        "pcmpgth    %[B],           %[ftmp10],        %[A]        \n\t"
        "por        %[mask],        %[B],             %[Gd]       \n\t"
        "pmullh     %[A],           %[A],             %[mask]     \n\t"
        "pmulhuh    %[A],           %[ftmp8],         %[A]        \n\t"
        "pmullh     %[A],           %[A],             %[mask]     \n\t"
        "paddh      %[A],           %[A],             %[B]        \n\t"
        "paddh      %[A],           %[A],             %[Ed]       \n\t"
        "psubh      %[B],           %[ftmp0],         %[ftmp4]    \n\t"
        "pcmpgth    %[C],           %[ftmp10],        %[B]        \n\t"
        "por        %[mask],        %[C],             %[Gd]       \n\t"
        "pmullh     %[B],           %[B],             %[mask]     \n\t"
        "pmulhuh    %[B],           %[ftmp8],         %[B]        \n\t"
        "pmullh     %[B],           %[B],             %[mask]     \n\t"
        "paddh      %[B],           %[B],             %[C]        \n\t"
        "paddh      %[B],           %[B],             %[Ed]       \n\t"

        LOAD_CONST(%[ftmp8], 60547)
        LOAD_CONST(%[ftmp9], 25080)
        "pmulhh     %[C],           %[ftmp9],         %[ftmp6]    \n\t"
        "pcmpgth    %[D],           %[ftmp10],        %[ftmp2]    \n\t"
        "por        %[mask],        %[D],             %[Gd]       \n\t"
        "pmullh     %[Ed],          %[ftmp2],         %[mask]     \n\t"
        "pmulhuh    %[Ed],          %[ftmp8],         %[Ed]       \n\t"
        "pmullh     %[Ed],          %[Ed],            %[mask]     \n\t"
        "paddh      %[C],           %[C],             %[Ed]       \n\t"
        "paddh      %[C],           %[C],             %[D]        \n\t"
        "pcmpgth    %[Ed],          %[ftmp10],        %[ftmp6]    \n\t"
        "por        %[mask],        %[Ed],            %[Gd]       \n\t"
        "pmullh     %[D],           %[ftmp6],         %[mask]     \n\t"
        "pmulhuh    %[D],           %[ftmp8],         %[D]        \n\t"
        "pmullh     %[D],           %[D],             %[mask]     \n\t"
        "pmulhh     %[Gd],          %[ftmp9],         %[ftmp2]    \n\t"
        "psubh      %[D],           %[Gd],            %[D]        \n\t"
        "psubh      %[D],           %[D],             %[Ed]       \n\t"
        "psubh      %[Ed],          %[A],             %[C]        \n\t"
        "paddh      %[Gd],          %[A],             %[C]        \n\t"
        "paddh      %[A],           %[B],             %[Ad]       \n\t"
        "psubh      %[C],           %[B],             %[Ad]       \n\t"
        "psubh      %[B],           %[Bd],            %[D]        \n\t"
        "paddh      %[D],           %[Bd],            %[D]        \n\t"
        "por        %[mask],        %[ftmp1],         %[ftmp2]    \n\t"
        "por        %[mask],        %[mask],          %[ftmp3]    \n\t"
        "por        %[mask],        %[mask],          %[ftmp4]    \n\t"
        "por        %[mask],        %[mask],          %[ftmp5]    \n\t"
        "por        %[mask],        %[mask],          %[ftmp6]    \n\t"
        "por        %[mask],        %[mask],          %[ftmp7]    \n\t"
        "pcmpeqh    %[mask],        %[mask],          %[ftmp10]   \n\t"
        "packushb   %[mask],        %[mask],          %[ftmp10]   \n\t"
        "li         %[tmp1],        0x04                          \n\t"
        "dmtc1      %[tmp1],        %[ftmp8]                      \n\t"
        "paddh      %[ftmp0],       %[Gd],            %[Cd]       \n\t"
        "psrah      %[ftmp0],       %[ftmp0],         %[ftmp8]    \n\t"
        "paddh      %[ftmp1],       %[A],             %[D]        \n\t"
        "psrah      %[ftmp1],       %[ftmp1],         %[ftmp8]    \n\t"
        "psubh      %[ftmp2],       %[A],             %[D]        \n\t"
        "psrah      %[ftmp2],       %[ftmp2],         %[ftmp8]    \n\t"
        "paddh      %[ftmp3],       %[Ed],            %[Dd]       \n\t"
        "psrah      %[ftmp3],       %[ftmp3],         %[ftmp8]    \n\t"
        "psubh      %[ftmp4],       %[Ed],            %[Dd]       \n\t"
        "psrah      %[ftmp4],       %[ftmp4],         %[ftmp8]    \n\t"
        "paddh      %[ftmp5],       %[C],             %[B]        \n\t"
        "psrah      %[ftmp5],       %[ftmp5],         %[ftmp8]    \n\t"
        "psubh      %[ftmp6],       %[C],             %[B]        \n\t"
        "psrah      %[ftmp6],       %[ftmp6],         %[ftmp8]    \n\t"
        "psubh      %[ftmp7],       %[Gd],            %[Cd]       \n\t"
        "psrah      %[ftmp7],       %[ftmp7],         %[ftmp8]    \n\t"
        "pmaxsh     %[ftmp0],       %[ftmp0],         %[ftmp10]   \n\t"
        "packushb   %[ftmp0],       %[ftmp0],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp1],       %[ftmp1],         %[ftmp10]   \n\t"
        "packushb   %[ftmp1],       %[ftmp1],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp2],       %[ftmp2],         %[ftmp10]   \n\t"
        "packushb   %[ftmp2],       %[ftmp2],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp3],       %[ftmp3],         %[ftmp10]   \n\t"
        "packushb   %[ftmp3],       %[ftmp3],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp4],       %[ftmp4],         %[ftmp10]   \n\t"
        "packushb   %[ftmp4],       %[ftmp4],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp5],       %[ftmp5],         %[ftmp10]   \n\t"
        "packushb   %[ftmp5],       %[ftmp5],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp6],       %[ftmp6],         %[ftmp10]   \n\t"
        "packushb   %[ftmp6],       %[ftmp6],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp7],       %[ftmp7],         %[ftmp10]   \n\t"
        "packushb   %[ftmp7],       %[ftmp7],         %[ftmp10]   \n\t"

        "lwc1       %[Ed],          0x00(%[temp_value])           \n\t"
        "pand       %[Ed],          %[Ed],            %[mask]     \n\t"
        "paddb      %[ftmp0],       %[ftmp0],         %[Ed]       \n\t"
        "paddb      %[ftmp1],       %[ftmp1],         %[Ed]       \n\t"
        "paddb      %[ftmp2],       %[ftmp2],         %[Ed]       \n\t"
        "paddb      %[ftmp3],       %[ftmp3],         %[Ed]       \n\t"
        "paddb      %[ftmp4],       %[ftmp4],         %[Ed]       \n\t"
        "paddb      %[ftmp5],       %[ftmp5],         %[Ed]       \n\t"
        "paddb      %[ftmp6],       %[ftmp6],         %[Ed]       \n\t"
        "paddb      %[ftmp7],       %[ftmp7],         %[Ed]       \n\t"
        "swc1       %[ftmp0],       0x00(%[dst])                  \n\t"
        PTR_ADDU   "%[tmp1],        %[dst],           %[stride]   \n\t"
        "swc1       %[ftmp1],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp2],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp3],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp4],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp5],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp6],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp7],       0x00(%[tmp1])                 \n\t"
        PTR_ADDIU  "%[dst],         %[dst],           0x04        \n\t"
        PTR_ADDIU  "%[input],       %[input],         0x40        \n\t"
        PTR_ADDIU  "%[temp_value],  %[temp_value],    0x04        \n\t"
        PTR_ADDIU  "%[tmp0],        %[tmp0],          -0x01       \n\t"
        "bnez       %[tmp0],        1b                            \n\t"
        : [dst]"+&r"(dst), [tmp0]"=&r"(tmp[0]), [tmp1]"=&r"(tmp[1]),
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]), [ftmp2]"=&f"(ftmp[2]),
          [ftmp3]"=&f"(ftmp[3]), [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]), [ftmp8]"=&f"(ftmp[8]),
          [ftmp9]"=&f"(ftmp[9]), [ftmp10]"=&f"(ftmp[10]), [mask]"=&f"(ftmp[11]),
          [A]"=&f"(ftmp[12]), [B]"=&f"(ftmp[13]), [C]"=&f"(ftmp[14]),
          [D]"=&f"(ftmp[15]), [Ad]"=&f"(ftmp[16]), [Bd]"=&f"(ftmp[17]),
          [Cd]"=&f"(ftmp[18]), [Dd]"=&f"(ftmp[19]), [Ed]"=&f"(ftmp[20]),
          [Gd]"=&f"(ftmp[21]), [input]"+&r"(input)
        : [stride]"r"(stride), [temp_value]"r"(temp_value)
        : "memory"
    );
}

static void idct_column_false_mmi(uint8_t *dst, int stride, int16_t *input)
{
    int16_t temp_value[8];
    double ftmp[23];
    uint64_t tmp[2];
    for (int i = 0; i < 8; ++i)
        temp_value[i] = (46341 * input[i << 3] + (8 << 16)) >> 20;
    __asm__ volatile (
        "pxor       %[ftmp10],      %[ftmp10],          %[ftmp10] \n\t"
        "li         %[tmp0],        0x02                          \n\t"
        "1:                                                       \n\t"
        "ldc1       %[ftmp0],       0x00(%[input])                \n\t"
        "ldc1       %[ftmp4],       0x08(%[input])                \n\t"
        "ldc1       %[ftmp1],       0x10(%[input])                \n\t"
        "ldc1       %[ftmp5],       0x18(%[input])                \n\t"
        "ldc1       %[ftmp2],       0x20(%[input])                \n\t"
        "ldc1       %[ftmp6],       0x28(%[input])                \n\t"
        "ldc1       %[ftmp3],       0x30(%[input])                \n\t"
        "ldc1       %[ftmp7],       0x38(%[input])                \n\t"
        TRANSPOSE_4H(%[ftmp0], %[ftmp1], %[ftmp2], %[ftmp3],
                     %[A], %[B], %[C], %[D])
        TRANSPOSE_4H(%[ftmp4], %[ftmp5], %[ftmp6], %[ftmp7],
                     %[A], %[B], %[C], %[D])
        LOAD_CONST(%[ftmp8], 64277)
        LOAD_CONST(%[ftmp9], 12785)
        LOAD_CONST(%[Gd], 1)
        "pmulhh     %[A],           %[ftmp9],           %[ftmp7]  \n\t"
        "pcmpgth    %[C],           %[ftmp10],          %[ftmp1]  \n\t"
        "por        %[mask],        %[C],               %[Gd]     \n\t"
        "pmullh     %[B],           %[ftmp1],           %[mask]   \n\t"
        "pmulhuh    %[B],           %[ftmp8],           %[B]      \n\t"
        "pmullh     %[B],           %[B],               %[mask]   \n\t"
        "paddh      %[A],           %[A],               %[B]      \n\t"
        "paddh      %[A],           %[A],               %[C]      \n\t"
        "pcmpgth    %[D],           %[ftmp10],          %[ftmp7]  \n\t"
        "por        %[mask],        %[D],               %[Gd]     \n\t"
        "pmullh     %[Ad],          %[ftmp7],           %[mask]   \n\t"
        "pmulhuh    %[B],           %[ftmp8],           %[Ad]     \n\t"
        "pmullh     %[B],           %[B],               %[mask]   \n\t"
        "pmulhh     %[C],           %[ftmp9],           %[ftmp1]  \n\t"
        "psubh      %[B],           %[C],               %[B]      \n\t"
        "psubh      %[B],           %[B],               %[D]      \n\t"

        LOAD_CONST(%[ftmp8], 54491)
        LOAD_CONST(%[ftmp9], 36410)
        "pcmpgth    %[Ad],          %[ftmp10],          %[ftmp5]  \n\t"
        "por        %[mask],        %[Ad],              %[Gd]     \n\t"
        "pmullh     %[Cd],          %[ftmp5],           %[mask]   \n\t"
        "pmulhuh    %[C],           %[ftmp9],           %[Cd]     \n\t"
        "pmullh     %[C],           %[C],               %[mask]   \n\t"
        "pcmpgth    %[Bd],          %[ftmp10],          %[ftmp3]  \n\t"
        "por        %[mask],        %[Bd],              %[Gd]     \n\t"
        "pmullh     %[D],           %[ftmp3],           %[mask]   \n\t"
        "pmulhuh    %[D],           %[ftmp8],           %[D]      \n\t"
        "pmullh     %[D],           %[D],               %[mask]   \n\t"
        "paddh      %[C],           %[C],               %[D]      \n\t"
        "paddh      %[C],           %[C],               %[Ad]     \n\t"
        "paddh      %[C],           %[C],               %[Bd]     \n\t"
        "pcmpgth    %[Bd],          %[ftmp10],          %[ftmp3]  \n\t"
        "por        %[mask],        %[Bd],              %[Gd]     \n\t"
        "pmullh     %[Cd],          %[ftmp3],           %[mask]   \n\t"
        "pmulhuh    %[D],           %[ftmp9],           %[Cd]     \n\t"
        "pmullh     %[D],           %[D],               %[mask]   \n\t"
        "pcmpgth    %[Ed],          %[ftmp10],          %[ftmp5]  \n\t"
        "por        %[mask],        %[Ed],              %[Gd]     \n\t"
        "pmullh     %[Ad],          %[ftmp5],           %[mask]   \n\t"
        "pmulhuh    %[Ad],          %[ftmp8],           %[Ad]     \n\t"
        "pmullh     %[Ad],          %[Ad],              %[mask]   \n\t"
        "psubh      %[D],           %[Ad],              %[D]      \n\t"
        "paddh      %[D],           %[D],               %[Ed]     \n\t"
        "psubh      %[D],           %[D],               %[Bd]     \n\t"

        LOAD_CONST(%[ftmp8], 46341)
        "psubh      %[Ad],          %[A],             %[C]        \n\t"
        "pcmpgth    %[Bd],          %[ftmp10],        %[Ad]       \n\t"
        "por        %[mask],        %[Bd],            %[Gd]       \n\t"
        "pmullh     %[Ad],          %[Ad],            %[mask]     \n\t"
        "pmulhuh    %[Ad],          %[ftmp8],         %[Ad]       \n\t"
        "pmullh     %[Ad],          %[Ad],            %[mask]     \n\t"
        "paddh      %[Ad],          %[Ad],            %[Bd]       \n\t"
        "psubh      %[Bd],          %[B],             %[D]        \n\t"
        "pcmpgth    %[Cd],          %[ftmp10],        %[Bd]       \n\t"
        "por        %[mask],        %[Cd],            %[Gd]       \n\t"
        "pmullh     %[Bd],          %[Bd],            %[mask]     \n\t"
        "pmulhuh    %[Bd],          %[ftmp8],         %[Bd]       \n\t"
        "pmullh     %[Bd],          %[Bd],            %[mask]     \n\t"
        "paddh      %[Bd],          %[Bd],            %[Cd]       \n\t"
        "paddh      %[Cd],          %[A],             %[C]        \n\t"
        "paddh      %[Dd],          %[B],             %[D]        \n\t"

        LOAD_CONST(%[Ed], 8)
        "paddh      %[A],           %[ftmp0],         %[ftmp4]    \n\t"
        "pcmpgth    %[B],           %[ftmp10],        %[A]        \n\t"
        "por        %[mask],        %[B],             %[Gd]       \n\t"
        "pmullh     %[A],           %[A],             %[mask]     \n\t"
        "pmulhuh    %[A],           %[ftmp8],         %[A]        \n\t"
        "pmullh     %[A],           %[A],             %[mask]     \n\t"
        "paddh      %[A],           %[A],             %[B]        \n\t"
        "paddh      %[A],           %[A],             %[Ed]       \n\t"
        "psubh      %[B],           %[ftmp0],         %[ftmp4]    \n\t"
        "pcmpgth    %[C],           %[ftmp10],        %[B]        \n\t"
        "por        %[mask],        %[C],             %[Gd]       \n\t"
        "pmullh     %[B],           %[B],             %[mask]     \n\t"
        "pmulhuh    %[B],           %[ftmp8],         %[B]        \n\t"
        "pmullh     %[B],           %[B],             %[mask]     \n\t"
        "paddh      %[B],           %[B],             %[C]        \n\t"
        "paddh      %[B],           %[B],             %[Ed]       \n\t"

        LOAD_CONST(%[ftmp8], 60547)
        LOAD_CONST(%[ftmp9], 25080)
        "pmulhh     %[C],           %[ftmp9],         %[ftmp6]    \n\t"
        "pcmpgth    %[D],           %[ftmp10],        %[ftmp2]    \n\t"
        "por        %[mask],        %[D],             %[Gd]       \n\t"
        "pmullh     %[Ed],          %[ftmp2],         %[mask]     \n\t"
        "pmulhuh    %[Ed],          %[ftmp8],         %[Ed]       \n\t"
        "pmullh     %[Ed],          %[Ed],            %[mask]     \n\t"
        "paddh      %[C],           %[C],             %[Ed]       \n\t"
        "paddh      %[C],           %[C],             %[D]        \n\t"
        "pcmpgth    %[Ed],          %[ftmp10],        %[ftmp6]    \n\t"
        "por        %[mask],        %[Ed],            %[Gd]       \n\t"
        "pmullh     %[D],           %[ftmp6],         %[mask]     \n\t"
        "pmulhuh    %[D],           %[ftmp8],         %[D]        \n\t"
        "pmullh     %[D],           %[D],             %[mask]     \n\t"
        "pmulhh     %[Gd],          %[ftmp9],         %[ftmp2]    \n\t"
        "psubh      %[D],           %[Gd],            %[D]        \n\t"
        "psubh      %[D],           %[D],             %[Ed]       \n\t"
        "psubh      %[Ed],          %[A],             %[C]        \n\t"
        "paddh      %[Gd],          %[A],             %[C]        \n\t"
        "paddh      %[A],           %[B],             %[Ad]       \n\t"
        "psubh      %[C],           %[B],             %[Ad]       \n\t"
        "psubh      %[B],           %[Bd],            %[D]        \n\t"
        "paddh      %[D],           %[Bd],            %[D]        \n\t"
        "por        %[mask],        %[ftmp1],         %[ftmp2]    \n\t"
        "por        %[mask],        %[mask],          %[ftmp3]    \n\t"
        "por        %[mask],        %[mask],          %[ftmp4]    \n\t"
        "por        %[mask],        %[mask],          %[ftmp5]    \n\t"
        "por        %[mask],        %[mask],          %[ftmp6]    \n\t"
        "por        %[mask],        %[mask],          %[ftmp7]    \n\t"
        "pcmpeqh    %[mask],        %[mask],          %[ftmp10]   \n\t"
        "li         %[tmp1],        0x04                          \n\t"
        "dmtc1      %[tmp1],        %[ftmp8]                      \n\t"
        "paddh      %[ftmp0],       %[Gd],            %[Cd]       \n\t"
        "psrah      %[ftmp0],       %[ftmp0],         %[ftmp8]    \n\t"
        "paddh      %[ftmp1],       %[A],             %[D]        \n\t"
        "psrah      %[ftmp1],       %[ftmp1],         %[ftmp8]    \n\t"
        "psubh      %[ftmp2],       %[A],             %[D]        \n\t"
        "psrah      %[ftmp2],       %[ftmp2],         %[ftmp8]    \n\t"
        "paddh      %[ftmp3],       %[Ed],            %[Dd]       \n\t"
        "psrah      %[ftmp3],       %[ftmp3],         %[ftmp8]    \n\t"
        "psubh      %[ftmp4],       %[Ed],            %[Dd]       \n\t"
        "psrah      %[ftmp4],       %[ftmp4],         %[ftmp8]    \n\t"
        "paddh      %[ftmp5],       %[C],             %[B]        \n\t"
        "psrah      %[ftmp5],       %[ftmp5],         %[ftmp8]    \n\t"
        "psubh      %[ftmp6],       %[C],             %[B]        \n\t"
        "psrah      %[ftmp6],       %[ftmp6],         %[ftmp8]    \n\t"
        "psubh      %[ftmp7],       %[Gd],            %[Cd]       \n\t"
        "psrah      %[ftmp7],       %[ftmp7],         %[ftmp8]    \n\t"

        /* Load from dst */
        "lwc1       %[A],           0x00(%[dst])                  \n\t"
        PTR_ADDU   "%[tmp1],        %[dst],           %[stride]   \n\t"
        "lwc1       %[B],           0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "lwc1       %[C],           0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "lwc1       %[D],           0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "lwc1       %[Ad],          0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "lwc1       %[Bd],          0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "lwc1       %[Cd],          0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "lwc1       %[Dd],          0x00(%[tmp1])                 \n\t"
        "punpcklbh  %[A],           %[A],             %[ftmp10]   \n\t"
        "punpcklbh  %[B],           %[B],             %[ftmp10]   \n\t"
        "punpcklbh  %[C],           %[C],             %[ftmp10]   \n\t"
        "punpcklbh  %[D],           %[D],             %[ftmp10]   \n\t"
        "punpcklbh  %[Ad],          %[Ad],            %[ftmp10]   \n\t"
        "punpcklbh  %[Bd],          %[Bd],            %[ftmp10]   \n\t"
        "punpcklbh  %[Cd],          %[Cd],            %[ftmp10]   \n\t"
        "punpcklbh  %[Dd],          %[Dd],            %[ftmp10]   \n\t"
        "ldc1       %[Ed],          0x00(%[temp_value])           \n\t"
        "pand       %[Ed],          %[Ed],            %[mask]     \n\t"
        "pnor       %[mask],        %[mask],          %[mask]     \n\t"
        "pand       %[ftmp0],       %[ftmp0],         %[mask]     \n\t"
        "pand       %[ftmp1],       %[ftmp1],         %[mask]     \n\t"
        "pand       %[ftmp2],       %[ftmp2],         %[mask]     \n\t"
        "pand       %[ftmp3],       %[ftmp3],         %[mask]     \n\t"
        "pand       %[ftmp4],       %[ftmp4],         %[mask]     \n\t"
        "pand       %[ftmp5],       %[ftmp5],         %[mask]     \n\t"
        "pand       %[ftmp6],       %[ftmp6],         %[mask]     \n\t"
        "pand       %[ftmp7],       %[ftmp7],         %[mask]     \n\t"
        "paddh      %[ftmp0],       %[ftmp0],         %[A]        \n\t"
        "paddh      %[ftmp1],       %[ftmp1],         %[B]        \n\t"
        "paddh      %[ftmp2],       %[ftmp2],         %[C]        \n\t"
        "paddh      %[ftmp3],       %[ftmp3],         %[D]        \n\t"
        "paddh      %[ftmp4],       %[ftmp4],         %[Ad]       \n\t"
        "paddh      %[ftmp5],       %[ftmp5],         %[Bd]       \n\t"
        "paddh      %[ftmp6],       %[ftmp6],         %[Cd]       \n\t"
        "paddh      %[ftmp7],       %[ftmp7],         %[Dd]       \n\t"
        "paddh      %[ftmp0],       %[ftmp0],         %[Ed]       \n\t"
        "paddh      %[ftmp1],       %[ftmp1],         %[Ed]       \n\t"
        "paddh      %[ftmp2],       %[ftmp2],         %[Ed]       \n\t"
        "paddh      %[ftmp3],       %[ftmp3],         %[Ed]       \n\t"
        "paddh      %[ftmp4],       %[ftmp4],         %[Ed]       \n\t"
        "paddh      %[ftmp5],       %[ftmp5],         %[Ed]       \n\t"
        "paddh      %[ftmp6],       %[ftmp6],         %[Ed]       \n\t"
        "paddh      %[ftmp7],       %[ftmp7],         %[Ed]       \n\t"
        "pmaxsh     %[ftmp0],       %[ftmp0],         %[ftmp10]   \n\t"
        "packushb   %[ftmp0],       %[ftmp0],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp1],       %[ftmp1],         %[ftmp10]   \n\t"
        "packushb   %[ftmp1],       %[ftmp1],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp2],       %[ftmp2],         %[ftmp10]   \n\t"
        "packushb   %[ftmp2],       %[ftmp2],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp3],       %[ftmp3],         %[ftmp10]   \n\t"
        "packushb   %[ftmp3],       %[ftmp3],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp4],       %[ftmp4],         %[ftmp10]   \n\t"
        "packushb   %[ftmp4],       %[ftmp4],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp5],       %[ftmp5],         %[ftmp10]   \n\t"
        "packushb   %[ftmp5],       %[ftmp5],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp6],       %[ftmp6],         %[ftmp10]   \n\t"
        "packushb   %[ftmp6],       %[ftmp6],         %[ftmp10]   \n\t"
        "pmaxsh     %[ftmp7],       %[ftmp7],         %[ftmp10]   \n\t"
        "packushb   %[ftmp7],       %[ftmp7],         %[ftmp10]   \n\t"
        "swc1       %[ftmp0],       0x00(%[dst])                  \n\t"
        PTR_ADDU   "%[tmp1],        %[dst],           %[stride]   \n\t"
        "swc1       %[ftmp1],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp2],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp3],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp4],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp5],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp6],       0x00(%[tmp1])                 \n\t"
        PTR_ADDU   "%[tmp1],        %[tmp1],          %[stride]   \n\t"
        "swc1       %[ftmp7],       0x00(%[tmp1])                 \n\t"
        PTR_ADDIU  "%[dst],         %[dst],           0x04        \n\t"
        PTR_ADDIU  "%[input],       %[input],         0x40        \n\t"
        PTR_ADDIU  "%[temp_value],  %[temp_value],    0x08        \n\t"
        PTR_ADDIU  "%[tmp0],        %[tmp0],          -0x01       \n\t"
        "bnez       %[tmp0],        1b                            \n\t"
        : [dst]"+&r"(dst), [tmp0]"=&r"(tmp[0]), [tmp1]"=&r"(tmp[1]),
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]), [ftmp2]"=&f"(ftmp[2]),
          [ftmp3]"=&f"(ftmp[3]), [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]), [ftmp7]"=&f"(ftmp[7]), [ftmp8]"=&f"(ftmp[8]),
          [ftmp9]"=&f"(ftmp[9]), [ftmp10]"=&f"(ftmp[10]), [mask]"=&f"(ftmp[11]),
          [A]"=&f"(ftmp[12]), [B]"=&f"(ftmp[13]), [C]"=&f"(ftmp[14]),
          [D]"=&f"(ftmp[15]), [Ad]"=&f"(ftmp[16]), [Bd]"=&f"(ftmp[17]),
          [Cd]"=&f"(ftmp[18]), [Dd]"=&f"(ftmp[19]), [Ed]"=&f"(ftmp[20]),
          [Gd]"=&f"(ftmp[21]), [input]"+&r"(input)
        : [stride]"r"(stride), [temp_value]"r"(temp_value)
        : "memory"
    );
}
static void idct_mmi(uint8_t *dst, int stride, int16_t *input, int type)
{
    idct_row_mmi(input);
    if (type == 1)
        idct_column_true_mmi(dst, stride, input);
    else
        idct_column_false_mmi(dst, stride, input);
}

void ff_vp3_idct_put_mmi(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    idct_mmi(dest, line_size, block, 1);
    memset(block, 0, sizeof(*block) << 6);
}

void ff_vp3_idct_add_mmi(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    idct_mmi(dest, line_size, block, 2);
    memset(block, 0, sizeof(*block) << 6);
}
void ff_vp3_idct_dc_add_mmi(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int dc = (block[0] + 15) >> 5;

    double ftmp[7];
    uint64_t tmp;
    __asm__ volatile (
        "pxor       %[ftmp0],     %[ftmp0],           %[ftmp0]      \n\t"
        "mtc1       %[dc],        %[ftmp5]                          \n\t"
        "pshufh     %[ftmp5],     %[ftmp5],           %[ftmp0]      \n\t"
        "li         %[tmp0],      0x08                              \n\t"
        "1:                                                         \n\t"
        "ldc1       %[ftmp1],     0x00(%[dest])                     \n\t"
        "punpcklbh  %[ftmp2],     %[ftmp1],           %[ftmp0]      \n\t"
        "punpckhbh  %[ftmp3],     %[ftmp1],           %[ftmp0]      \n\t"
        "paddh      %[ftmp4],     %[ftmp2],           %[ftmp5]      \n\t"
        "paddh      %[ftmp6],     %[ftmp3],           %[ftmp5]      \n\t"
        "packushb   %[ftmp4],     %[ftmp4],           %[ftmp0]      \n\t"
        "packushb   %[ftmp6],     %[ftmp6],           %[ftmp0]      \n\t"
        "swc1       %[ftmp4],     0x00(%[dest])                     \n\t"
        "swc1       %[ftmp6],     0x04(%[dest])                     \n\t"
        PTR_ADDU   "%[dest],      %[dest],            %[line_size]  \n\t"
        PTR_ADDIU  "%[tmp0],      %[tmp0],            -0x01         \n\t"
        "bnez       %[tmp0],      1b                                \n\t"
        : [dest]"+&r"(dest), [block]"+&r"(block), [tmp0]"=&r"(tmp),
          [ftmp0]"=&f"(ftmp[0]), [ftmp1]"=&f"(ftmp[1]), [ftmp2]"=&f"(ftmp[2]),
          [ftmp3]"=&f"(ftmp[3]), [ftmp4]"=&f"(ftmp[4]), [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6])
        : [line_size]"r"(line_size), [dc]"r"(dc)
        : "memory"
    );
    block[0] = 0;
}

void ff_put_no_rnd_pixels_l2_mmi(uint8_t *dst, const uint8_t *src1,
                                 const uint8_t *src2, ptrdiff_t stride, int h)
{
    if (h == 8) {
        double ftmp[6];
        uint64_t tmp[2];
        DECLARE_VAR_ALL64;

        __asm__ volatile (
            "li          %[tmp0],        0x08                            \n\t"
            "li          %[tmp1],        0xfefefefe                      \n\t"
            "dmtc1       %[tmp1],        %[ftmp4]                        \n\t"
            "punpcklwd   %[ftmp4],       %[ftmp4],             %[ftmp4]  \n\t"
            "li          %[tmp1],        0x01                            \n\t"
            "dmtc1       %[tmp1],        %[ftmp5]                        \n\t"
            "1:                                                          \n\t"
            MMI_ULDC1(%[ftmp1], %[src1], 0x0)
            MMI_ULDC1(%[ftmp2], %[src2], 0x0)
            "pxor        %[ftmp3],       %[ftmp1],             %[ftmp2]  \n\t"
            "pand        %[ftmp3],       %[ftmp3],             %[ftmp4]  \n\t"
            "psrlw       %[ftmp3],       %[ftmp3],             %[ftmp5]  \n\t"
            "pand        %[ftmp6],       %[ftmp1],             %[ftmp2]  \n\t"
            "paddw       %[ftmp3],       %[ftmp3],             %[ftmp6]  \n\t"
            "sdc1        %[ftmp3],       0x00(%[dst])                    \n\t"
            PTR_ADDU    "%[src1],        %[src1],              %[stride] \n\t"
            PTR_ADDU    "%[src2],        %[src2],              %[stride] \n\t"
            PTR_ADDU    "%[dst],         %[dst],               %[stride] \n\t"
            PTR_ADDIU   "%[tmp0],        %[tmp0],              -0x01     \n\t"
            "bnez        %[tmp0],        1b                              \n\t"
            : RESTRICT_ASM_ALL64
              [dst]"+&r"(dst), [src1]"+&r"(src1), [src2]"+&r"(src2),
              [ftmp1]"=&f"(ftmp[0]), [ftmp2]"=&f"(ftmp[1]), [ftmp3]"=&f"(ftmp[2]),
              [ftmp4]"=&f"(ftmp[3]), [ftmp5]"=&f"(ftmp[4]), [ftmp6]"=&f"(ftmp[5]),
              [tmp0]"=&r"(tmp[0]), [tmp1]"=&r"(tmp[1])
            : [stride]"r"(stride)
            : "memory"
        );
    } else {
        int i;

        for (i = 0; i < h; i++) {
            uint32_t a, b;

            a = AV_RN32(&src1[i * stride]);
            b = AV_RN32(&src2[i * stride]);
            AV_WN32A(&dst[i * stride], no_rnd_avg32(a, b));
            a = AV_RN32(&src1[i * stride + 4]);
            b = AV_RN32(&src2[i * stride + 4]);
            AV_WN32A(&dst[i * stride + 4], no_rnd_avg32(a, b));
        }
    }
}
