/*
 * XVID MPEG-4 VIDEO CODEC
 * - SSE2 inverse discrete cosine transform -
 *
 * Copyright(C) 2003 Pascal Massimino <skal@planet-d.net>
 *
 * Conversion to gcc syntax with modifications
 * by Alexander Strange <astrange@ithinksw.com>
 *
 * Originally from dct/x86_asm/fdct_sse2_skal.asm in Xvid.
 *
 * This file is part of FFmpeg.
 *
 * Vertical pass is an implementation of the scheme:
 *  Loeffler C., Ligtenberg A., and Moschytz C.S.:
 *  Practical Fast 1D DCT Algorithm with Eleven Multiplications,
 *  Proc. ICASSP 1989, 988-991.
 *
 * Horizontal pass is a double 4x4 vector/matrix multiplication,
 * (see also Intel's Application Note 922:
 *  http://developer.intel.com/vtune/cbts/strmsimd/922down.htm
 *  Copyright (C) 1999 Intel Corporation)
 *
 * More details at http://skal.planet-d.net/coding/dct.html
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavcodec/dsputil.h"
#include "idct_xvid.h"
#include "dsputil_mmx.h"

/*!
 * @file libavcodec/x86/idct_sse2_xvid.c
 * @brief SSE2 idct compatible with xvidmmx
 */

#define X8(x)     x,x,x,x,x,x,x,x

#define ROW_SHIFT 11
#define COL_SHIFT 6

DECLARE_ASM_CONST(16, int16_t, tan1)[] = {X8(13036)}; // tan( pi/16)
DECLARE_ASM_CONST(16, int16_t, tan2)[] = {X8(27146)}; // tan(2pi/16) = sqrt(2)-1
DECLARE_ASM_CONST(16, int16_t, tan3)[] = {X8(43790)}; // tan(3pi/16)-1
DECLARE_ASM_CONST(16, int16_t, sqrt2)[]= {X8(23170)}; // 0.5/sqrt(2)
DECLARE_ASM_CONST(8,  uint8_t, m127)[] = {X8(127)};

DECLARE_ASM_CONST(16, int16_t, iTab1)[] = {
 0x4000, 0x539f, 0xc000, 0xac61, 0x4000, 0xdd5d, 0x4000, 0xdd5d,
 0x4000, 0x22a3, 0x4000, 0x22a3, 0xc000, 0x539f, 0x4000, 0xac61,
 0x3249, 0x11a8, 0x4b42, 0xee58, 0x11a8, 0x4b42, 0x11a8, 0xcdb7,
 0x58c5, 0x4b42, 0xa73b, 0xcdb7, 0x3249, 0xa73b, 0x4b42, 0xa73b
};

DECLARE_ASM_CONST(16, int16_t, iTab2)[] = {
 0x58c5, 0x73fc, 0xa73b, 0x8c04, 0x58c5, 0xcff5, 0x58c5, 0xcff5,
 0x58c5, 0x300b, 0x58c5, 0x300b, 0xa73b, 0x73fc, 0x58c5, 0x8c04,
 0x45bf, 0x187e, 0x6862, 0xe782, 0x187e, 0x6862, 0x187e, 0xba41,
 0x7b21, 0x6862, 0x84df, 0xba41, 0x45bf, 0x84df, 0x6862, 0x84df
};

DECLARE_ASM_CONST(16, int16_t, iTab3)[] = {
 0x539f, 0x6d41, 0xac61, 0x92bf, 0x539f, 0xd2bf, 0x539f, 0xd2bf,
 0x539f, 0x2d41, 0x539f, 0x2d41, 0xac61, 0x6d41, 0x539f, 0x92bf,
 0x41b3, 0x1712, 0x6254, 0xe8ee, 0x1712, 0x6254, 0x1712, 0xbe4d,
 0x73fc, 0x6254, 0x8c04, 0xbe4d, 0x41b3, 0x8c04, 0x6254, 0x8c04
};

DECLARE_ASM_CONST(16, int16_t, iTab4)[] = {
 0x4b42, 0x6254, 0xb4be, 0x9dac, 0x4b42, 0xd746, 0x4b42, 0xd746,
 0x4b42, 0x28ba, 0x4b42, 0x28ba, 0xb4be, 0x6254, 0x4b42, 0x9dac,
 0x3b21, 0x14c3, 0x587e, 0xeb3d, 0x14c3, 0x587e, 0x14c3, 0xc4df,
 0x6862, 0x587e, 0x979e, 0xc4df, 0x3b21, 0x979e, 0x587e, 0x979e
};

DECLARE_ASM_CONST(16, int32_t, walkenIdctRounders)[] = {
 65536, 65536, 65536, 65536,
  3597,  3597,  3597,  3597,
  2260,  2260,  2260,  2260,
  1203,  1203,  1203,  1203,
   120,   120,   120,   120,
   512,   512,   512,   512
};

// Temporary storage before the column pass
#define ROW1 "%%xmm6"
#define ROW3 "%%xmm4"
#define ROW5 "%%xmm5"
#define ROW7 "%%xmm7"

#define CLEAR_ODD(r) "pxor  "r","r" \n\t"
#define PUT_ODD(dst) "pshufhw  $0x1B, %%xmm2, "dst"   \n\t"

#if ARCH_X86_64

# define ROW0 "%%xmm8"
# define REG0 ROW0
# define ROW2 "%%xmm9"
# define REG2 ROW2
# define ROW4 "%%xmm10"
# define REG4 ROW4
# define ROW6 "%%xmm11"
# define REG6 ROW6
# define CLEAR_EVEN(r) CLEAR_ODD(r)
# define PUT_EVEN(dst) PUT_ODD(dst)
# define XMMS "%%xmm12"
# define MOV_32_ONLY "#"
# define SREG2 REG2
# define TAN3 "%%xmm13"
# define TAN1 "%%xmm14"

#else

# define ROW0 "(%0)"
# define REG0 "%%xmm4"
# define ROW2 "2*16(%0)"
# define REG2 "%%xmm4"
# define ROW4 "4*16(%0)"
# define REG4 "%%xmm6"
# define ROW6 "6*16(%0)"
# define REG6 "%%xmm6"
# define CLEAR_EVEN(r)
# define PUT_EVEN(dst) \
    "pshufhw  $0x1B, %%xmm2, %%xmm2   \n\t" \
    "movdqa          %%xmm2, "dst"    \n\t"
# define XMMS "%%xmm2"
# define MOV_32_ONLY "movdqa "
# define SREG2 "%%xmm7"
# define TAN3 "%%xmm0"
# define TAN1 "%%xmm2"

#endif

#define ROUND(x) "paddd   "MANGLE(x)

#define JZ(reg, to)                         \
    "testl     "reg","reg"            \n\t" \
    "jz        "to"                   \n\t"

#define JNZ(reg, to)                        \
    "testl     "reg","reg"            \n\t" \
    "jnz       "to"                   \n\t"

#define TEST_ONE_ROW(src, reg, clear)       \
    clear                                   \
    "movq     "src", %%mm1            \n\t" \
    "por    8+"src", %%mm1            \n\t" \
    "paddusb  %%mm0, %%mm1            \n\t" \
    "pmovmskb %%mm1, "reg"            \n\t"

#define TEST_TWO_ROWS(row1, row2, reg1, reg2, clear1, clear2) \
    clear1                                  \
    clear2                                  \
    "movq     "row1", %%mm1           \n\t" \
    "por    8+"row1", %%mm1           \n\t" \
    "movq     "row2", %%mm2           \n\t" \
    "por    8+"row2", %%mm2           \n\t" \
    "paddusb   %%mm0, %%mm1           \n\t" \
    "paddusb   %%mm0, %%mm2           \n\t" \
    "pmovmskb  %%mm1, "reg1"          \n\t" \
    "pmovmskb  %%mm2, "reg2"          \n\t"

///IDCT pass on rows.
#define iMTX_MULT(src, table, rounder, put) \
    "movdqa        "src", %%xmm3      \n\t" \
    "movdqa       %%xmm3, %%xmm0      \n\t" \
    "pshufd   $0x11, %%xmm3, %%xmm1   \n\t" /* 4602 */ \
    "punpcklqdq   %%xmm0, %%xmm0      \n\t" /* 0246 */ \
    "pmaddwd     "table", %%xmm0      \n\t" \
    "pmaddwd  16+"table", %%xmm1      \n\t" \
    "pshufd   $0xBB, %%xmm3, %%xmm2   \n\t" /* 5713 */ \
    "punpckhqdq   %%xmm3, %%xmm3      \n\t" /* 1357 */ \
    "pmaddwd  32+"table", %%xmm2      \n\t" \
    "pmaddwd  48+"table", %%xmm3      \n\t" \
    "paddd        %%xmm1, %%xmm0      \n\t" \
    "paddd        %%xmm3, %%xmm2      \n\t" \
    rounder",     %%xmm0              \n\t" \
    "movdqa       %%xmm2, %%xmm3      \n\t" \
    "paddd        %%xmm0, %%xmm2      \n\t" \
    "psubd        %%xmm3, %%xmm0      \n\t" \
    "psrad           $11, %%xmm2      \n\t" \
    "psrad           $11, %%xmm0      \n\t" \
    "packssdw     %%xmm0, %%xmm2      \n\t" \
    put                                     \
    "1:                               \n\t"

#define iLLM_HEAD                           \
    "movdqa   "MANGLE(tan3)", "TAN3"  \n\t" \
    "movdqa   "MANGLE(tan1)", "TAN1"  \n\t" \

///IDCT pass on columns.
#define iLLM_PASS(dct)                      \
    "movdqa   "TAN3", %%xmm1          \n\t" \
    "movdqa   "TAN1", %%xmm3          \n\t" \
    "pmulhw   %%xmm4, "TAN3"          \n\t" \
    "pmulhw   %%xmm5, %%xmm1          \n\t" \
    "paddsw   %%xmm4, "TAN3"          \n\t" \
    "paddsw   %%xmm5, %%xmm1          \n\t" \
    "psubsw   %%xmm5, "TAN3"          \n\t" \
    "paddsw   %%xmm4, %%xmm1          \n\t" \
    "pmulhw   %%xmm7, %%xmm3          \n\t" \
    "pmulhw   %%xmm6, "TAN1"          \n\t" \
    "paddsw   %%xmm6, %%xmm3          \n\t" \
    "psubsw   %%xmm7, "TAN1"          \n\t" \
    "movdqa   %%xmm3, %%xmm7          \n\t" \
    "movdqa   "TAN1", %%xmm6          \n\t" \
    "psubsw   %%xmm1, %%xmm3          \n\t" \
    "psubsw   "TAN3", "TAN1"          \n\t" \
    "paddsw   %%xmm7, %%xmm1          \n\t" \
    "paddsw   %%xmm6, "TAN3"          \n\t" \
    "movdqa   %%xmm3, %%xmm6          \n\t" \
    "psubsw   "TAN3", %%xmm3          \n\t" \
    "paddsw   %%xmm6, "TAN3"          \n\t" \
    "movdqa   "MANGLE(sqrt2)", %%xmm4 \n\t" \
    "pmulhw   %%xmm4, %%xmm3          \n\t" \
    "pmulhw   %%xmm4, "TAN3"          \n\t" \
    "paddsw   "TAN3", "TAN3"          \n\t" \
    "paddsw   %%xmm3, %%xmm3          \n\t" \
    "movdqa   "MANGLE(tan2)", %%xmm7  \n\t" \
    MOV_32_ONLY ROW2", "REG2"         \n\t" \
    MOV_32_ONLY ROW6", "REG6"         \n\t" \
    "movdqa   %%xmm7, %%xmm5          \n\t" \
    "pmulhw   "REG6", %%xmm7          \n\t" \
    "pmulhw   "REG2", %%xmm5          \n\t" \
    "paddsw   "REG2", %%xmm7          \n\t" \
    "psubsw   "REG6", %%xmm5          \n\t" \
    MOV_32_ONLY ROW0", "REG0"         \n\t" \
    MOV_32_ONLY ROW4", "REG4"         \n\t" \
    MOV_32_ONLY"  "TAN1", (%0)        \n\t" \
    "movdqa   "REG0", "XMMS"          \n\t" \
    "psubsw   "REG4", "REG0"          \n\t" \
    "paddsw   "XMMS", "REG4"          \n\t" \
    "movdqa   "REG4", "XMMS"          \n\t" \
    "psubsw   %%xmm7, "REG4"          \n\t" \
    "paddsw   "XMMS", %%xmm7          \n\t" \
    "movdqa   "REG0", "XMMS"          \n\t" \
    "psubsw   %%xmm5, "REG0"          \n\t" \
    "paddsw   "XMMS", %%xmm5          \n\t" \
    "movdqa   %%xmm5, "XMMS"          \n\t" \
    "psubsw   "TAN3", %%xmm5          \n\t" \
    "paddsw   "XMMS", "TAN3"          \n\t" \
    "movdqa   "REG0", "XMMS"          \n\t" \
    "psubsw   %%xmm3, "REG0"          \n\t" \
    "paddsw   "XMMS", %%xmm3          \n\t" \
    MOV_32_ONLY"  (%0), "TAN1"        \n\t" \
    "psraw        $6, %%xmm5          \n\t" \
    "psraw        $6, "REG0"          \n\t" \
    "psraw        $6, "TAN3"          \n\t" \
    "psraw        $6, %%xmm3          \n\t" \
    "movdqa   "TAN3", 1*16("dct")     \n\t" \
    "movdqa   %%xmm3, 2*16("dct")     \n\t" \
    "movdqa   "REG0", 5*16("dct")     \n\t" \
    "movdqa   %%xmm5, 6*16("dct")     \n\t" \
    "movdqa   %%xmm7, %%xmm0          \n\t" \
    "movdqa   "REG4", %%xmm4          \n\t" \
    "psubsw   %%xmm1, %%xmm7          \n\t" \
    "psubsw   "TAN1", "REG4"          \n\t" \
    "paddsw   %%xmm0, %%xmm1          \n\t" \
    "paddsw   %%xmm4, "TAN1"          \n\t" \
    "psraw        $6, %%xmm1          \n\t" \
    "psraw        $6, %%xmm7          \n\t" \
    "psraw        $6, "TAN1"          \n\t" \
    "psraw        $6, "REG4"          \n\t" \
    "movdqa   %%xmm1, ("dct")         \n\t" \
    "movdqa   "TAN1", 3*16("dct")     \n\t" \
    "movdqa   "REG4", 4*16("dct")     \n\t" \
    "movdqa   %%xmm7, 7*16("dct")     \n\t"

///IDCT pass on columns, assuming rows 4-7 are zero.
#define iLLM_PASS_SPARSE(dct)               \
    "pmulhw   %%xmm4, "TAN3"          \n\t" \
    "paddsw   %%xmm4, "TAN3"          \n\t" \
    "movdqa   %%xmm6, %%xmm3          \n\t" \
    "pmulhw   %%xmm6, "TAN1"          \n\t" \
    "movdqa   %%xmm4, %%xmm1          \n\t" \
    "psubsw   %%xmm1, %%xmm3          \n\t" \
    "paddsw   %%xmm6, %%xmm1          \n\t" \
    "movdqa   "TAN1", %%xmm6          \n\t" \
    "psubsw   "TAN3", "TAN1"          \n\t" \
    "paddsw   %%xmm6, "TAN3"          \n\t" \
    "movdqa   %%xmm3, %%xmm6          \n\t" \
    "psubsw   "TAN3", %%xmm3          \n\t" \
    "paddsw   %%xmm6, "TAN3"          \n\t" \
    "movdqa   "MANGLE(sqrt2)", %%xmm4 \n\t" \
    "pmulhw   %%xmm4, %%xmm3          \n\t" \
    "pmulhw   %%xmm4, "TAN3"          \n\t" \
    "paddsw   "TAN3", "TAN3"          \n\t" \
    "paddsw   %%xmm3, %%xmm3          \n\t" \
    "movdqa   "MANGLE(tan2)", %%xmm5  \n\t" \
    MOV_32_ONLY ROW2", "SREG2"        \n\t" \
    "pmulhw   "SREG2", %%xmm5         \n\t" \
    MOV_32_ONLY ROW0", "REG0"         \n\t" \
    "movdqa   "REG0", %%xmm6          \n\t" \
    "psubsw   "SREG2", %%xmm6         \n\t" \
    "paddsw   "REG0", "SREG2"         \n\t" \
    MOV_32_ONLY"  "TAN1", (%0)        \n\t" \
    "movdqa   "REG0", "XMMS"          \n\t" \
    "psubsw   %%xmm5, "REG0"          \n\t" \
    "paddsw   "XMMS", %%xmm5          \n\t" \
    "movdqa   %%xmm5, "XMMS"          \n\t" \
    "psubsw   "TAN3", %%xmm5          \n\t" \
    "paddsw   "XMMS", "TAN3"          \n\t" \
    "movdqa   "REG0", "XMMS"          \n\t" \
    "psubsw   %%xmm3, "REG0"          \n\t" \
    "paddsw   "XMMS", %%xmm3          \n\t" \
    MOV_32_ONLY"  (%0), "TAN1"        \n\t" \
    "psraw        $6, %%xmm5          \n\t" \
    "psraw        $6, "REG0"          \n\t" \
    "psraw        $6, "TAN3"          \n\t" \
    "psraw        $6, %%xmm3          \n\t" \
    "movdqa   "TAN3", 1*16("dct")     \n\t" \
    "movdqa   %%xmm3, 2*16("dct")     \n\t" \
    "movdqa   "REG0", 5*16("dct")     \n\t" \
    "movdqa   %%xmm5, 6*16("dct")     \n\t" \
    "movdqa   "SREG2", %%xmm0         \n\t" \
    "movdqa   %%xmm6, %%xmm4          \n\t" \
    "psubsw   %%xmm1, "SREG2"         \n\t" \
    "psubsw   "TAN1", %%xmm6          \n\t" \
    "paddsw   %%xmm0, %%xmm1          \n\t" \
    "paddsw   %%xmm4, "TAN1"          \n\t" \
    "psraw        $6, %%xmm1          \n\t" \
    "psraw        $6, "SREG2"         \n\t" \
    "psraw        $6, "TAN1"          \n\t" \
    "psraw        $6, %%xmm6          \n\t" \
    "movdqa   %%xmm1, ("dct")         \n\t" \
    "movdqa   "TAN1", 3*16("dct")     \n\t" \
    "movdqa   %%xmm6, 4*16("dct")     \n\t" \
    "movdqa   "SREG2", 7*16("dct")    \n\t"

inline void ff_idct_xvid_sse2(short *block)
{
    __asm__ volatile(
    "movq     "MANGLE(m127)", %%mm0                              \n\t"
    iMTX_MULT("(%0)",     MANGLE(iTab1), ROUND(walkenIdctRounders),      PUT_EVEN(ROW0))
    iMTX_MULT("1*16(%0)", MANGLE(iTab2), ROUND(walkenIdctRounders+1*16), PUT_ODD(ROW1))
    iMTX_MULT("2*16(%0)", MANGLE(iTab3), ROUND(walkenIdctRounders+2*16), PUT_EVEN(ROW2))

    TEST_TWO_ROWS("3*16(%0)", "4*16(%0)", "%%eax", "%%ecx", CLEAR_ODD(ROW3), CLEAR_EVEN(ROW4))
    JZ("%%eax", "1f")
    iMTX_MULT("3*16(%0)", MANGLE(iTab4), ROUND(walkenIdctRounders+3*16), PUT_ODD(ROW3))

    TEST_TWO_ROWS("5*16(%0)", "6*16(%0)", "%%eax", "%%edx", CLEAR_ODD(ROW5), CLEAR_EVEN(ROW6))
    TEST_ONE_ROW("7*16(%0)", "%%esi", CLEAR_ODD(ROW7))
    iLLM_HEAD
    ASMALIGN(4)
    JNZ("%%ecx", "2f")
    JNZ("%%eax", "3f")
    JNZ("%%edx", "4f")
    JNZ("%%esi", "5f")
    iLLM_PASS_SPARSE("%0")
    "jmp 6f                                                      \n\t"
    "2:                                                          \n\t"
    iMTX_MULT("4*16(%0)", MANGLE(iTab1), "#", PUT_EVEN(ROW4))
    "3:                                                          \n\t"
    iMTX_MULT("5*16(%0)", MANGLE(iTab4), ROUND(walkenIdctRounders+4*16), PUT_ODD(ROW5))
    JZ("%%edx", "1f")
    "4:                                                          \n\t"
    iMTX_MULT("6*16(%0)", MANGLE(iTab3), ROUND(walkenIdctRounders+5*16), PUT_EVEN(ROW6))
    JZ("%%esi", "1f")
    "5:                                                          \n\t"
    iMTX_MULT("7*16(%0)", MANGLE(iTab2), ROUND(walkenIdctRounders+5*16), PUT_ODD(ROW7))
#if !ARCH_X86_64
    iLLM_HEAD
#endif
    iLLM_PASS("%0")
    "6:                                                          \n\t"
    : "+r"(block)
    :
    : "%eax", "%ecx", "%edx", "%esi", "memory");
}

void ff_idct_xvid_sse2_put(uint8_t *dest, int line_size, short *block)
{
    ff_idct_xvid_sse2(block);
    put_pixels_clamped_mmx(block, dest, line_size);
}

void ff_idct_xvid_sse2_add(uint8_t *dest, int line_size, short *block)
{
    ff_idct_xvid_sse2(block);
    add_pixels_clamped_mmx(block, dest, line_size);
}
