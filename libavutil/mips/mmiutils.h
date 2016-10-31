/*
 * Loongson SIMD utils
 *
 * Copyright (c) 2016 Loongson Technology Corporation Limited
 * Copyright (c) 2016 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#ifndef AVUTIL_MIPS_MMIUTILS_H
#define AVUTIL_MIPS_MMIUTILS_H

#include "config.h"
#include "libavutil/mips/asmdefs.h"

#if HAVE_LOONGSON2

#define DECLARE_VAR_LOW32       int32_t low32
#define RESTRICT_ASM_LOW32      [low32]"=&r"(low32),
#define DECLARE_VAR_ALL64       int64_t all64
#define RESTRICT_ASM_ALL64      [all64]"=&r"(all64),
#define DECLARE_VAR_ADDRT       mips_reg addrt
#define RESTRICT_ASM_ADDRT      [addrt]"=&r"(addrt),

#define MMI_LWX(reg, addr, stride, bias)                                    \
    PTR_ADDU    "%[addrt],  "#addr",    "#stride"                   \n\t"   \
    "lw         "#reg",     "#bias"(%[addrt])                       \n\t"

#define MMI_SWX(reg, addr, stride, bias)                                    \
    PTR_ADDU    "%[addrt],  "#addr",    "#stride"                   \n\t"   \
    "sw         "#reg",     "#bias"(%[addrt])                       \n\t"

#define MMI_LDX(reg, addr, stride, bias)                                    \
    PTR_ADDU    "%[addrt],  "#addr",    "#stride"                   \n\t"   \
    "ld         "#reg",     "#bias"(%[addrt])                       \n\t"

#define MMI_SDX(reg, addr, stride, bias)                                    \
    PTR_ADDU    "%[addrt],  "#addr",    "#stride"                   \n\t"   \
    "sd         "#reg",     "#bias"(%[addrt])                       \n\t"

#define MMI_LWC1(fp, addr, bias)                                            \
    "lwc1       "#fp",      "#bias"("#addr")                        \n\t"

#define MMI_ULWC1(fp, addr, bias)                                           \
    "ulw        %[low32],   "#bias"("#addr")                        \n\t"   \
    "mtc1       %[low32],   "#fp"                                   \n\t"

#define MMI_LWXC1(fp, addr, stride, bias)                                   \
    PTR_ADDU    "%[addrt],  "#addr",    "#stride"                   \n\t"   \
    MMI_LWC1(fp, %[addrt], bias)

#define MMI_SWC1(fp, addr, bias)                                            \
    "swc1       "#fp",      "#bias"("#addr")                        \n\t"

#define MMI_USWC1(fp, addr, bias)                                           \
    "mfc1       %[low32],   "#fp"                                   \n\t"   \
    "usw        %[low32],   "#bias"("#addr")                        \n\t"

#define MMI_SWXC1(fp, addr, stride, bias)                                   \
    PTR_ADDU    "%[addrt],  "#addr",    "#stride"                   \n\t"   \
    MMI_SWC1(fp, %[addrt], bias)

#define MMI_LDC1(fp, addr, bias)                                            \
    "ldc1       "#fp",      "#bias"("#addr")                        \n\t"

#define MMI_ULDC1(fp, addr, bias)                                           \
    "uld        %[all64],   "#bias"("#addr")                        \n\t"   \
    "dmtc1      %[all64],   "#fp"                                   \n\t"

#define MMI_LDXC1(fp, addr, stride, bias)                                   \
    PTR_ADDU    "%[addrt],  "#addr",    "#stride"                   \n\t"   \
    MMI_LDC1(fp, %[addrt], bias)

#define MMI_SDC1(fp, addr, bias)                                            \
    "sdc1       "#fp",      "#bias"("#addr")                        \n\t"

#define MMI_USDC1(fp, addr, bias)                                           \
    "dmfc1      %[all64],   "#fp"                                   \n\t"   \
    "usd        %[all64],   "#bias"("#addr")                        \n\t"

#define MMI_SDXC1(fp, addr, stride, bias)                                   \
    PTR_ADDU    "%[addrt],  "#addr",    "#stride"                   \n\t"   \
    MMI_SDC1(fp, %[addrt], bias)

#define MMI_LQ(reg1, reg2, addr, bias)                                      \
    "ld         "#reg1",    "#bias"("#addr")                        \n\t"   \
    "ld         "#reg2",  8+"#bias"("#addr")                        \n\t"

#define MMI_SQ(reg1, reg2, addr, bias)                                      \
    "sd         "#reg1",    "#bias"("#addr")                        \n\t"   \
    "sd         "#reg2",  8+"#bias"("#addr")                        \n\t"

#define MMI_LQC1(fp1, fp2, addr, bias)                                      \
    "ldc1       "#fp1",     "#bias"("#addr")                        \n\t"   \
    "ldc1       "#fp2",   8+"#bias"("#addr")                        \n\t"

#define MMI_SQC1(fp1, fp2, addr, bias)                                      \
    "sdc1       "#fp1",     "#bias"("#addr")                        \n\t"   \
    "sdc1       "#fp2",   8+"#bias"("#addr")                        \n\t"

#elif HAVE_LOONGSON3 /* !HAVE_LOONGSON2 */

#define DECLARE_VAR_ALL64
#define RESTRICT_ASM_ALL64
#define DECLARE_VAR_ADDRT
#define RESTRICT_ASM_ADDRT

#define MMI_LWX(reg, addr, stride, bias)                                    \
    "gslwx      "#reg",     "#bias"("#addr", "#stride")             \n\t"

#define MMI_SWX(reg, addr, stride, bias)                                    \
    "gsswx      "#reg",     "#bias"("#addr", "#stride")             \n\t"

#define MMI_LDX(reg, addr, stride, bias)                                    \
    "gsldx      "#reg",     "#bias"("#addr", "#stride")             \n\t"

#define MMI_SDX(reg, addr, stride, bias)                                    \
    "gssdx      "#reg",     "#bias"("#addr", "#stride")             \n\t"

#define MMI_LWC1(fp, addr, bias)                                            \
    "lwc1       "#fp",      "#bias"("#addr")                        \n\t"

#if _MIPS_SIM == _ABIO32 /* workaround for 3A2000 gslwlc1 bug */

#define DECLARE_VAR_LOW32       int32_t low32
#define RESTRICT_ASM_LOW32      [low32]"=&r"(low32),

#define MMI_ULWC1(fp, addr, bias)                                           \
    "ulw        %[low32],   "#bias"("#addr")                        \n\t"   \
    "mtc1       %[low32],   "#fp"                                   \n\t"

#else /* _MIPS_SIM != _ABIO32 */

#define DECLARE_VAR_LOW32
#define RESTRICT_ASM_LOW32

#define MMI_ULWC1(fp, addr, bias)                                           \
    "gslwlc1    "#fp",    3+"#bias"("#addr")                        \n\t"   \
    "gslwrc1    "#fp",      "#bias"("#addr")                        \n\t"

#endif /* _MIPS_SIM != _ABIO32 */

#define MMI_LWXC1(fp, addr, stride, bias)                                   \
    "gslwxc1    "#fp",      "#bias"("#addr", "#stride")             \n\t"

#define MMI_SWC1(fp, addr, bias)                                            \
    "swc1       "#fp",      "#bias"("#addr")                        \n\t"

#define MMI_USWC1(fp, addr, bias)                                           \
    "gsswlc1    "#fp",    3+"#bias"("#addr")                        \n\t"   \
    "gsswrc1    "#fp",      "#bias"("#addr")                        \n\t"

#define MMI_SWXC1(fp, addr, stride, bias)                                   \
    "gsswxc1    "#fp",      "#bias"("#addr", "#stride")             \n\t"

#define MMI_LDC1(fp, addr, bias)                                            \
    "ldc1       "#fp",      "#bias"("#addr")                        \n\t"

#define MMI_ULDC1(fp, addr, bias)                                           \
    "gsldlc1    "#fp",    7+"#bias"("#addr")                        \n\t"   \
    "gsldrc1    "#fp",      "#bias"("#addr")                        \n\t"

#define MMI_LDXC1(fp, addr, stride, bias)                                   \
    "gsldxc1    "#fp",      "#bias"("#addr", "#stride")             \n\t"

#define MMI_SDC1(fp, addr, bias)                                            \
    "sdc1       "#fp",      "#bias"("#addr")                        \n\t"

#define MMI_USDC1(fp, addr, bias)                                           \
    "gssdlc1    "#fp",    7+"#bias"("#addr")                        \n\t"   \
    "gssdrc1    "#fp",      "#bias"("#addr")                        \n\t"

#define MMI_SDXC1(fp, addr, stride, bias)                                   \
    "gssdxc1    "#fp",      "#bias"("#addr", "#stride")             \n\t"

#define MMI_LQ(reg1, reg2, addr, bias)                                      \
    "gslq       "#reg1",    "#reg2",     "#bias"("#addr")           \n\t"

#define MMI_SQ(reg1, reg2, addr, bias)                                      \
    "gssq       "#reg1",    "#reg2",     "#bias"("#addr")           \n\t"

#define MMI_LQC1(fp1, fp2, addr, bias)                                      \
    "gslqc1     "#fp1",     "#fp2",     "#bias"("#addr")            \n\t"

#define MMI_SQC1(fp1, fp2, addr, bias)                                      \
    "gssqc1     "#fp1",     "#fp2",     "#bias"("#addr")            \n\t"

#endif /* HAVE_LOONGSON2 */

#define TRANSPOSE_4H(m1, m2, m3, m4, t1, t2, t3, t4, t5, r1, zero, shift) \
        "li         "#r1",  0x93                                    \n\t" \
        "xor        "#zero","#zero","#zero"                         \n\t" \
        "mtc1       "#r1",  "#shift"                                \n\t" \
        "punpcklhw  "#t1",  "#m1",  "#zero"                         \n\t" \
        "punpcklhw  "#t5",  "#m2",  "#zero"                         \n\t" \
        "pshufh     "#t5",  "#t5",  "#shift"                        \n\t" \
        "or         "#t1",  "#t1",  "#t5"                           \n\t" \
        "punpckhhw  "#t2",  "#m1",  "#zero"                         \n\t" \
        "punpckhhw  "#t5",  "#m2",  "#zero"                         \n\t" \
        "pshufh     "#t5",  "#t5",  "#shift"                        \n\t" \
        "or         "#t2",  "#t2",  "#t5"                           \n\t" \
        "punpcklhw  "#t3",  "#m3",  "#zero"                         \n\t" \
        "punpcklhw  "#t5",  "#m4",  "#zero"                         \n\t" \
        "pshufh     "#t5",  "#t5",  "#shift"                        \n\t" \
        "or         "#t3",  "#t3",  "#t5"                           \n\t" \
        "punpckhhw  "#t4",  "#m3",  "#zero"                         \n\t" \
        "punpckhhw  "#t5",  "#m4",  "#zero"                         \n\t" \
        "pshufh     "#t5",  "#t5",  "#shift"                        \n\t" \
        "or         "#t4",  "#t4",  "#t5"                           \n\t" \
        "punpcklwd  "#m1",  "#t1",  "#t3"                           \n\t" \
        "punpckhwd  "#m2",  "#t1",  "#t3"                           \n\t" \
        "punpcklwd  "#m3",  "#t2",  "#t4"                           \n\t" \
        "punpckhwd  "#m4",  "#t2",  "#t4"                           \n\t"


#define PSRAH_4_MMI(fp1, fp2, fp3, fp4, shift)                              \
        "psrah      "#fp1",     "#fp1",     "#shift"                \n\t"   \
        "psrah      "#fp2",     "#fp2",     "#shift"                \n\t"   \
        "psrah      "#fp3",     "#fp3",     "#shift"                \n\t"   \
        "psrah      "#fp4",     "#fp4",     "#shift"                \n\t"

#define PSRAH_8_MMI(fp1, fp2, fp3, fp4, fp5, fp6, fp7, fp8, shift)          \
        PSRAH_4_MMI(fp1, fp2, fp3, fp4, shift)                              \
        PSRAH_4_MMI(fp5, fp6, fp7, fp8, shift)


#endif /* AVUTILS_MIPS_MMIUTILS_H */
