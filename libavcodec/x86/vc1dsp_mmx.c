/*
 * VC-1 and WMV3 - DSP functions MMX-optimized
 * Copyright (c) 2007 Christophe GISQUET <christophe.gisquet@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "libavutil/cpu.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/vc1dsp.h"
#include "constants.h"
#include "dsputil_x86.h"
#include "vc1dsp.h"

#if HAVE_INLINE_ASM

#define OP_PUT(S,D)
#define OP_AVG(S,D) "pavgb " #S ", " #D " \n\t"

/** Add rounder from mm7 to mm3 and pack result at destination */
#define NORMALIZE_MMX(SHIFT)                                    \
     "paddw     %%mm7, %%mm3           \n\t" /* +bias-r */      \
     "paddw     %%mm7, %%mm4           \n\t" /* +bias-r */      \
     "psraw     "SHIFT", %%mm3         \n\t"                    \
     "psraw     "SHIFT", %%mm4         \n\t"

#define TRANSFER_DO_PACK(OP)                    \
     "packuswb  %%mm4, %%mm3           \n\t"    \
     OP((%2), %%mm3)                            \
     "movq      %%mm3, (%2)            \n\t"

#define TRANSFER_DONT_PACK(OP)                  \
     OP(0(%2), %%mm3)                           \
     OP(8(%2), %%mm4)                           \
     "movq      %%mm3, 0(%2)           \n\t"    \
     "movq      %%mm4, 8(%2)           \n\t"

/** @see MSPEL_FILTER13_CORE for use as UNPACK macro */
#define DO_UNPACK(reg)  "punpcklbw %%mm0, " reg "\n\t"
#define DONT_UNPACK(reg)

/** Compute the rounder 32-r or 8-r and unpacks it to mm7 */
#define LOAD_ROUNDER_MMX(ROUND)                 \
     "movd      "ROUND", %%mm7         \n\t"    \
     "punpcklwd %%mm7, %%mm7           \n\t"    \
     "punpckldq %%mm7, %%mm7           \n\t"

#define SHIFT2_LINE(OFF, R0,R1,R2,R3)           \
    "paddw     %%mm"#R2", %%mm"#R1"    \n\t"    \
    "movd      (%0,%3), %%mm"#R0"      \n\t"    \
    "pmullw    %%mm6, %%mm"#R1"        \n\t"    \
    "punpcklbw %%mm0, %%mm"#R0"        \n\t"    \
    "movd      (%0,%2), %%mm"#R3"      \n\t"    \
    "psubw     %%mm"#R0", %%mm"#R1"    \n\t"    \
    "punpcklbw %%mm0, %%mm"#R3"        \n\t"    \
    "paddw     %%mm7, %%mm"#R1"        \n\t"    \
    "psubw     %%mm"#R3", %%mm"#R1"    \n\t"    \
    "psraw     %4, %%mm"#R1"           \n\t"    \
    "movq      %%mm"#R1", "#OFF"(%1)   \n\t"    \
    "add       %2, %0                  \n\t"

/** Sacrifying mm6 allows to pipeline loads from src */
static void vc1_put_ver_16b_shift2_mmx(int16_t *dst,
                                       const uint8_t *src, x86_reg stride,
                                       int rnd, int64_t shift)
{
    __asm__ volatile(
        "mov       $3, %%"REG_c"           \n\t"
        LOAD_ROUNDER_MMX("%5")
        "movq      "MANGLE(ff_pw_9)", %%mm6 \n\t"
        "1:                                \n\t"
        "movd      (%0), %%mm2             \n\t"
        "add       %2, %0                  \n\t"
        "movd      (%0), %%mm3             \n\t"
        "punpcklbw %%mm0, %%mm2            \n\t"
        "punpcklbw %%mm0, %%mm3            \n\t"
        SHIFT2_LINE(  0, 1, 2, 3, 4)
        SHIFT2_LINE( 24, 2, 3, 4, 1)
        SHIFT2_LINE( 48, 3, 4, 1, 2)
        SHIFT2_LINE( 72, 4, 1, 2, 3)
        SHIFT2_LINE( 96, 1, 2, 3, 4)
        SHIFT2_LINE(120, 2, 3, 4, 1)
        SHIFT2_LINE(144, 3, 4, 1, 2)
        SHIFT2_LINE(168, 4, 1, 2, 3)
        "sub       %6, %0                  \n\t"
        "add       $8, %1                  \n\t"
        "dec       %%"REG_c"               \n\t"
        "jnz 1b                            \n\t"
        : "+r"(src), "+r"(dst)
        : "r"(stride), "r"(-2*stride),
          "m"(shift), "m"(rnd), "r"(9*stride-4)
        : "%"REG_c, "memory"
    );
}

/**
 * Data is already unpacked, so some operations can directly be made from
 * memory.
 */
#define VC1_HOR_16b_SHIFT2(OP, OPNAME)\
static void OPNAME ## vc1_hor_16b_shift2_mmx(uint8_t *dst, x86_reg stride,\
                                             const int16_t *src, int rnd)\
{\
    int h = 8;\
\
    src -= 1;\
    rnd -= (-1+9+9-1)*1024; /* Add -1024 bias */\
    __asm__ volatile(\
        LOAD_ROUNDER_MMX("%4")\
        "movq      "MANGLE(ff_pw_128)", %%mm6\n\t"\
        "movq      "MANGLE(ff_pw_9)", %%mm5 \n\t"\
        "1:                                \n\t"\
        "movq      2*0+0(%1), %%mm1        \n\t"\
        "movq      2*0+8(%1), %%mm2        \n\t"\
        "movq      2*1+0(%1), %%mm3        \n\t"\
        "movq      2*1+8(%1), %%mm4        \n\t"\
        "paddw     2*3+0(%1), %%mm1        \n\t"\
        "paddw     2*3+8(%1), %%mm2        \n\t"\
        "paddw     2*2+0(%1), %%mm3        \n\t"\
        "paddw     2*2+8(%1), %%mm4        \n\t"\
        "pmullw    %%mm5, %%mm3            \n\t"\
        "pmullw    %%mm5, %%mm4            \n\t"\
        "psubw     %%mm1, %%mm3            \n\t"\
        "psubw     %%mm2, %%mm4            \n\t"\
        NORMALIZE_MMX("$7")\
        /* Remove bias */\
        "paddw     %%mm6, %%mm3            \n\t"\
        "paddw     %%mm6, %%mm4            \n\t"\
        TRANSFER_DO_PACK(OP)\
        "add       $24, %1                 \n\t"\
        "add       %3, %2                  \n\t"\
        "decl      %0                      \n\t"\
        "jnz 1b                            \n\t"\
        : "+r"(h), "+r" (src),  "+r" (dst)\
        : "r"(stride), "m"(rnd)\
        : "memory"\
    );\
}

VC1_HOR_16b_SHIFT2(OP_PUT, put_)
VC1_HOR_16b_SHIFT2(OP_AVG, avg_)


/**
 * Purely vertical or horizontal 1/2 shift interpolation.
 * Sacrify mm6 for *9 factor.
 */
#define VC1_SHIFT2(OP, OPNAME)\
static void OPNAME ## vc1_shift2_mmx(uint8_t *dst, const uint8_t *src,\
                                     x86_reg stride, int rnd, x86_reg offset)\
{\
    rnd = 8-rnd;\
    __asm__ volatile(\
        "mov       $8, %%"REG_c"           \n\t"\
        LOAD_ROUNDER_MMX("%5")\
        "movq      "MANGLE(ff_pw_9)", %%mm6\n\t"\
        "1:                                \n\t"\
        "movd      0(%0   ), %%mm3         \n\t"\
        "movd      4(%0   ), %%mm4         \n\t"\
        "movd      0(%0,%2), %%mm1         \n\t"\
        "movd      4(%0,%2), %%mm2         \n\t"\
        "add       %2, %0                  \n\t"\
        "punpcklbw %%mm0, %%mm3            \n\t"\
        "punpcklbw %%mm0, %%mm4            \n\t"\
        "punpcklbw %%mm0, %%mm1            \n\t"\
        "punpcklbw %%mm0, %%mm2            \n\t"\
        "paddw     %%mm1, %%mm3            \n\t"\
        "paddw     %%mm2, %%mm4            \n\t"\
        "movd      0(%0,%3), %%mm1         \n\t"\
        "movd      4(%0,%3), %%mm2         \n\t"\
        "pmullw    %%mm6, %%mm3            \n\t" /* 0,9,9,0*/\
        "pmullw    %%mm6, %%mm4            \n\t" /* 0,9,9,0*/\
        "punpcklbw %%mm0, %%mm1            \n\t"\
        "punpcklbw %%mm0, %%mm2            \n\t"\
        "psubw     %%mm1, %%mm3            \n\t" /*-1,9,9,0*/\
        "psubw     %%mm2, %%mm4            \n\t" /*-1,9,9,0*/\
        "movd      0(%0,%2), %%mm1         \n\t"\
        "movd      4(%0,%2), %%mm2         \n\t"\
        "punpcklbw %%mm0, %%mm1            \n\t"\
        "punpcklbw %%mm0, %%mm2            \n\t"\
        "psubw     %%mm1, %%mm3            \n\t" /*-1,9,9,-1*/\
        "psubw     %%mm2, %%mm4            \n\t" /*-1,9,9,-1*/\
        NORMALIZE_MMX("$4")\
        "packuswb  %%mm4, %%mm3            \n\t"\
        OP((%1), %%mm3)\
        "movq      %%mm3, (%1)             \n\t"\
        "add       %6, %0                  \n\t"\
        "add       %4, %1                  \n\t"\
        "dec       %%"REG_c"               \n\t"\
        "jnz 1b                            \n\t"\
        : "+r"(src),  "+r"(dst)\
        : "r"(offset), "r"(-2*offset), "g"(stride), "m"(rnd),\
          "g"(stride-offset)\
        : "%"REG_c, "memory"\
    );\
}

VC1_SHIFT2(OP_PUT, put_)
VC1_SHIFT2(OP_AVG, avg_)

/**
 * Core of the 1/4 and 3/4 shift bicubic interpolation.
 *
 * @param UNPACK  Macro unpacking arguments from 8 to 16bits (can be empty).
 * @param MOVQ    "movd 1" or "movq 2", if data read is already unpacked.
 * @param A1      Address of 1st tap (beware of unpacked/packed).
 * @param A2      Address of 2nd tap
 * @param A3      Address of 3rd tap
 * @param A4      Address of 4th tap
 */
#define MSPEL_FILTER13_CORE(UNPACK, MOVQ, A1, A2, A3, A4)       \
     MOVQ "*0+"A1", %%mm1       \n\t"                           \
     MOVQ "*4+"A1", %%mm2       \n\t"                           \
     UNPACK("%%mm1")                                            \
     UNPACK("%%mm2")                                            \
     "pmullw    "MANGLE(ff_pw_3)", %%mm1\n\t"                   \
     "pmullw    "MANGLE(ff_pw_3)", %%mm2\n\t"                   \
     MOVQ "*0+"A2", %%mm3       \n\t"                           \
     MOVQ "*4+"A2", %%mm4       \n\t"                           \
     UNPACK("%%mm3")                                            \
     UNPACK("%%mm4")                                            \
     "pmullw    %%mm6, %%mm3    \n\t" /* *18 */                 \
     "pmullw    %%mm6, %%mm4    \n\t" /* *18 */                 \
     "psubw     %%mm1, %%mm3    \n\t" /* 18,-3 */               \
     "psubw     %%mm2, %%mm4    \n\t" /* 18,-3 */               \
     MOVQ "*0+"A4", %%mm1       \n\t"                           \
     MOVQ "*4+"A4", %%mm2       \n\t"                           \
     UNPACK("%%mm1")                                            \
     UNPACK("%%mm2")                                            \
     "psllw     $2, %%mm1       \n\t" /* 4* */                  \
     "psllw     $2, %%mm2       \n\t" /* 4* */                  \
     "psubw     %%mm1, %%mm3    \n\t" /* -4,18,-3 */            \
     "psubw     %%mm2, %%mm4    \n\t" /* -4,18,-3 */            \
     MOVQ "*0+"A3", %%mm1       \n\t"                           \
     MOVQ "*4+"A3", %%mm2       \n\t"                           \
     UNPACK("%%mm1")                                            \
     UNPACK("%%mm2")                                            \
     "pmullw    %%mm5, %%mm1    \n\t" /* *53 */                 \
     "pmullw    %%mm5, %%mm2    \n\t" /* *53 */                 \
     "paddw     %%mm1, %%mm3    \n\t" /* 4,53,18,-3 */          \
     "paddw     %%mm2, %%mm4    \n\t" /* 4,53,18,-3 */

/**
 * Macro to build the vertical 16bits version of vc1_put_shift[13].
 * Here, offset=src_stride. Parameters passed A1 to A4 must use
 * %3 (src_stride) and %4 (3*src_stride).
 *
 * @param  NAME   Either 1 or 3
 * @see MSPEL_FILTER13_CORE for information on A1->A4
 */
#define MSPEL_FILTER13_VER_16B(NAME, A1, A2, A3, A4)                    \
static void                                                             \
vc1_put_ver_16b_ ## NAME ## _mmx(int16_t *dst, const uint8_t *src,      \
                                 x86_reg src_stride,                   \
                                 int rnd, int64_t shift)                \
{                                                                       \
    int h = 8;                                                          \
    src -= src_stride;                                                  \
    __asm__ volatile(                                                       \
        LOAD_ROUNDER_MMX("%5")                                          \
        "movq      "MANGLE(ff_pw_53)", %%mm5\n\t"                       \
        "movq      "MANGLE(ff_pw_18)", %%mm6\n\t"                       \
        ".p2align 3                \n\t"                                \
        "1:                        \n\t"                                \
        MSPEL_FILTER13_CORE(DO_UNPACK, "movd  1", A1, A2, A3, A4)       \
        NORMALIZE_MMX("%6")                                             \
        TRANSFER_DONT_PACK(OP_PUT)                                      \
        /* Last 3 (in fact 4) bytes on the line */                      \
        "movd      8+"A1", %%mm1   \n\t"                                \
        DO_UNPACK("%%mm1")                                              \
        "movq      %%mm1, %%mm3    \n\t"                                \
        "paddw     %%mm1, %%mm1    \n\t"                                \
        "paddw     %%mm3, %%mm1    \n\t" /* 3* */                       \
        "movd      8+"A2", %%mm3   \n\t"                                \
        DO_UNPACK("%%mm3")                                              \
        "pmullw    %%mm6, %%mm3    \n\t" /* *18 */                      \
        "psubw     %%mm1, %%mm3    \n\t" /*18,-3 */                     \
        "movd      8+"A3", %%mm1   \n\t"                                \
        DO_UNPACK("%%mm1")                                              \
        "pmullw    %%mm5, %%mm1    \n\t" /* *53 */                      \
        "paddw     %%mm1, %%mm3    \n\t" /*53,18,-3 */                  \
        "movd      8+"A4", %%mm1   \n\t"                                \
        DO_UNPACK("%%mm1")                                              \
        "psllw     $2, %%mm1       \n\t" /* 4* */                       \
        "psubw     %%mm1, %%mm3    \n\t"                                \
        "paddw     %%mm7, %%mm3    \n\t"                                \
        "psraw     %6, %%mm3       \n\t"                                \
        "movq      %%mm3, 16(%2)   \n\t"                                \
        "add       %3, %1          \n\t"                                \
        "add       $24, %2         \n\t"                                \
        "decl      %0              \n\t"                                \
        "jnz 1b                    \n\t"                                \
        : "+r"(h), "+r" (src),  "+r" (dst)                              \
        : "r"(src_stride), "r"(3*src_stride),                           \
          "m"(rnd), "m"(shift)                                          \
        : "memory"                                                      \
    );                                                                  \
}

/**
 * Macro to build the horizontal 16bits version of vc1_put_shift[13].
 * Here, offset=16bits, so parameters passed A1 to A4 should be simple.
 *
 * @param  NAME   Either 1 or 3
 * @see MSPEL_FILTER13_CORE for information on A1->A4
 */
#define MSPEL_FILTER13_HOR_16B(NAME, A1, A2, A3, A4, OP, OPNAME)        \
static void                                                             \
OPNAME ## vc1_hor_16b_ ## NAME ## _mmx(uint8_t *dst, x86_reg stride,    \
                                 const int16_t *src, int rnd)           \
{                                                                       \
    int h = 8;                                                          \
    src -= 1;                                                           \
    rnd -= (-4+58+13-3)*256; /* Add -256 bias */                        \
    __asm__ volatile(                                                       \
        LOAD_ROUNDER_MMX("%4")                                          \
        "movq      "MANGLE(ff_pw_18)", %%mm6   \n\t"                    \
        "movq      "MANGLE(ff_pw_53)", %%mm5   \n\t"                    \
        ".p2align 3                \n\t"                                \
        "1:                        \n\t"                                \
        MSPEL_FILTER13_CORE(DONT_UNPACK, "movq 2", A1, A2, A3, A4)      \
        NORMALIZE_MMX("$7")                                             \
        /* Remove bias */                                               \
        "paddw     "MANGLE(ff_pw_128)", %%mm3  \n\t"                    \
        "paddw     "MANGLE(ff_pw_128)", %%mm4  \n\t"                    \
        TRANSFER_DO_PACK(OP)                                            \
        "add       $24, %1         \n\t"                                \
        "add       %3, %2          \n\t"                                \
        "decl      %0              \n\t"                                \
        "jnz 1b                    \n\t"                                \
        : "+r"(h), "+r" (src),  "+r" (dst)                              \
        : "r"(stride), "m"(rnd)                                         \
        : "memory"                                                      \
    );                                                                  \
}

/**
 * Macro to build the 8bits, any direction, version of vc1_put_shift[13].
 * Here, offset=src_stride. Parameters passed A1 to A4 must use
 * %3 (offset) and %4 (3*offset).
 *
 * @param  NAME   Either 1 or 3
 * @see MSPEL_FILTER13_CORE for information on A1->A4
 */
#define MSPEL_FILTER13_8B(NAME, A1, A2, A3, A4, OP, OPNAME)             \
static void                                                             \
OPNAME ## vc1_## NAME ## _mmx(uint8_t *dst, const uint8_t *src,         \
                        x86_reg stride, int rnd, x86_reg offset)      \
{                                                                       \
    int h = 8;                                                          \
    src -= offset;                                                      \
    rnd = 32-rnd;                                                       \
    __asm__ volatile (                                                      \
        LOAD_ROUNDER_MMX("%6")                                          \
        "movq      "MANGLE(ff_pw_53)", %%mm5       \n\t"                \
        "movq      "MANGLE(ff_pw_18)", %%mm6       \n\t"                \
        ".p2align 3                \n\t"                                \
        "1:                        \n\t"                                \
        MSPEL_FILTER13_CORE(DO_UNPACK, "movd   1", A1, A2, A3, A4)      \
        NORMALIZE_MMX("$6")                                             \
        TRANSFER_DO_PACK(OP)                                            \
        "add       %5, %1          \n\t"                                \
        "add       %5, %2          \n\t"                                \
        "decl      %0              \n\t"                                \
        "jnz 1b                    \n\t"                                \
        : "+r"(h), "+r" (src),  "+r" (dst)                              \
        : "r"(offset), "r"(3*offset), "g"(stride), "m"(rnd)             \
        : "memory"                                                      \
    );                                                                  \
}

/** 1/4 shift bicubic interpolation */
MSPEL_FILTER13_8B     (shift1, "0(%1,%4  )", "0(%1,%3,2)", "0(%1,%3  )", "0(%1     )", OP_PUT, put_)
MSPEL_FILTER13_8B     (shift1, "0(%1,%4  )", "0(%1,%3,2)", "0(%1,%3  )", "0(%1     )", OP_AVG, avg_)
MSPEL_FILTER13_VER_16B(shift1, "0(%1,%4  )", "0(%1,%3,2)", "0(%1,%3  )", "0(%1     )")
MSPEL_FILTER13_HOR_16B(shift1, "2*3(%1)", "2*2(%1)", "2*1(%1)", "2*0(%1)", OP_PUT, put_)
MSPEL_FILTER13_HOR_16B(shift1, "2*3(%1)", "2*2(%1)", "2*1(%1)", "2*0(%1)", OP_AVG, avg_)

/** 3/4 shift bicubic interpolation */
MSPEL_FILTER13_8B     (shift3, "0(%1     )", "0(%1,%3  )", "0(%1,%3,2)", "0(%1,%4  )", OP_PUT, put_)
MSPEL_FILTER13_8B     (shift3, "0(%1     )", "0(%1,%3  )", "0(%1,%3,2)", "0(%1,%4  )", OP_AVG, avg_)
MSPEL_FILTER13_VER_16B(shift3, "0(%1     )", "0(%1,%3  )", "0(%1,%3,2)", "0(%1,%4  )")
MSPEL_FILTER13_HOR_16B(shift3, "2*0(%1)", "2*1(%1)", "2*2(%1)", "2*3(%1)", OP_PUT, put_)
MSPEL_FILTER13_HOR_16B(shift3, "2*0(%1)", "2*1(%1)", "2*2(%1)", "2*3(%1)", OP_AVG, avg_)

typedef void (*vc1_mspel_mc_filter_ver_16bits)(int16_t *dst, const uint8_t *src, x86_reg src_stride, int rnd, int64_t shift);
typedef void (*vc1_mspel_mc_filter_hor_16bits)(uint8_t *dst, x86_reg dst_stride, const int16_t *src, int rnd);
typedef void (*vc1_mspel_mc_filter_8bits)(uint8_t *dst, const uint8_t *src, x86_reg stride, int rnd, x86_reg offset);

/**
 * Interpolate fractional pel values by applying proper vertical then
 * horizontal filter.
 *
 * @param  dst     Destination buffer for interpolated pels.
 * @param  src     Source buffer.
 * @param  stride  Stride for both src and dst buffers.
 * @param  hmode   Horizontal filter (expressed in quarter pixels shift).
 * @param  hmode   Vertical filter.
 * @param  rnd     Rounding bias.
 */
#define VC1_MSPEL_MC(OP)\
static void OP ## vc1_mspel_mc(uint8_t *dst, const uint8_t *src, int stride,\
                               int hmode, int vmode, int rnd)\
{\
    static const vc1_mspel_mc_filter_ver_16bits vc1_put_shift_ver_16bits[] =\
         { NULL, vc1_put_ver_16b_shift1_mmx, vc1_put_ver_16b_shift2_mmx, vc1_put_ver_16b_shift3_mmx };\
    static const vc1_mspel_mc_filter_hor_16bits vc1_put_shift_hor_16bits[] =\
         { NULL, OP ## vc1_hor_16b_shift1_mmx, OP ## vc1_hor_16b_shift2_mmx, OP ## vc1_hor_16b_shift3_mmx };\
    static const vc1_mspel_mc_filter_8bits vc1_put_shift_8bits[] =\
         { NULL, OP ## vc1_shift1_mmx, OP ## vc1_shift2_mmx, OP ## vc1_shift3_mmx };\
\
    __asm__ volatile(\
        "pxor %%mm0, %%mm0         \n\t"\
        ::: "memory"\
    );\
\
    if (vmode) { /* Vertical filter to apply */\
        if (hmode) { /* Horizontal filter to apply, output to tmp */\
            static const int shift_value[] = { 0, 5, 1, 5 };\
            int              shift = (shift_value[hmode]+shift_value[vmode])>>1;\
            int              r;\
            DECLARE_ALIGNED(16, int16_t, tmp)[12*8];\
\
            r = (1<<(shift-1)) + rnd-1;\
            vc1_put_shift_ver_16bits[vmode](tmp, src-1, stride, r, shift);\
\
            vc1_put_shift_hor_16bits[hmode](dst, stride, tmp+1, 64-rnd);\
            return;\
        }\
        else { /* No horizontal filter, output 8 lines to dst */\
            vc1_put_shift_8bits[vmode](dst, src, stride, 1-rnd, stride);\
            return;\
        }\
    }\
\
    /* Horizontal mode with no vertical mode */\
    vc1_put_shift_8bits[hmode](dst, src, stride, rnd, 1);\
}

VC1_MSPEL_MC(put_)
VC1_MSPEL_MC(avg_)

/** Macro to ease bicubic filter interpolation functions declarations */
#define DECLARE_FUNCTION(a, b)                                          \
static void put_vc1_mspel_mc ## a ## b ## _mmx(uint8_t *dst,            \
                                               const uint8_t *src,      \
                                               ptrdiff_t stride,        \
                                               int rnd)                 \
{                                                                       \
     put_vc1_mspel_mc(dst, src, stride, a, b, rnd);                     \
}\
static void avg_vc1_mspel_mc ## a ## b ## _mmxext(uint8_t *dst,         \
                                                  const uint8_t *src,   \
                                                  ptrdiff_t stride,     \
                                                  int rnd)              \
{                                                                       \
     avg_vc1_mspel_mc(dst, src, stride, a, b, rnd);                     \
}

DECLARE_FUNCTION(0, 1)
DECLARE_FUNCTION(0, 2)
DECLARE_FUNCTION(0, 3)

DECLARE_FUNCTION(1, 0)
DECLARE_FUNCTION(1, 1)
DECLARE_FUNCTION(1, 2)
DECLARE_FUNCTION(1, 3)

DECLARE_FUNCTION(2, 0)
DECLARE_FUNCTION(2, 1)
DECLARE_FUNCTION(2, 2)
DECLARE_FUNCTION(2, 3)

DECLARE_FUNCTION(3, 0)
DECLARE_FUNCTION(3, 1)
DECLARE_FUNCTION(3, 2)
DECLARE_FUNCTION(3, 3)

static void vc1_inv_trans_4x4_dc_mmxext(uint8_t *dest, int linesize,
                                        int16_t *block)
{
    int dc = block[0];
    dc = (17 * dc +  4) >> 3;
    dc = (17 * dc + 64) >> 7;
    __asm__ volatile(
        "movd          %0, %%mm0 \n\t"
        "pshufw $0, %%mm0, %%mm0 \n\t"
        "pxor       %%mm1, %%mm1 \n\t"
        "psubw      %%mm0, %%mm1 \n\t"
        "packuswb   %%mm0, %%mm0 \n\t"
        "packuswb   %%mm1, %%mm1 \n\t"
        ::"r"(dc)
    );
    __asm__ volatile(
        "movd          %0, %%mm2 \n\t"
        "movd          %1, %%mm3 \n\t"
        "movd          %2, %%mm4 \n\t"
        "movd          %3, %%mm5 \n\t"
        "paddusb    %%mm0, %%mm2 \n\t"
        "paddusb    %%mm0, %%mm3 \n\t"
        "paddusb    %%mm0, %%mm4 \n\t"
        "paddusb    %%mm0, %%mm5 \n\t"
        "psubusb    %%mm1, %%mm2 \n\t"
        "psubusb    %%mm1, %%mm3 \n\t"
        "psubusb    %%mm1, %%mm4 \n\t"
        "psubusb    %%mm1, %%mm5 \n\t"
        "movd       %%mm2, %0    \n\t"
        "movd       %%mm3, %1    \n\t"
        "movd       %%mm4, %2    \n\t"
        "movd       %%mm5, %3    \n\t"
        :"+m"(*(uint32_t*)(dest+0*linesize)),
         "+m"(*(uint32_t*)(dest+1*linesize)),
         "+m"(*(uint32_t*)(dest+2*linesize)),
         "+m"(*(uint32_t*)(dest+3*linesize))
    );
}

static void vc1_inv_trans_4x8_dc_mmxext(uint8_t *dest, int linesize,
                                        int16_t *block)
{
    int dc = block[0];
    dc = (17 * dc +  4) >> 3;
    dc = (12 * dc + 64) >> 7;
    __asm__ volatile(
        "movd          %0, %%mm0 \n\t"
        "pshufw $0, %%mm0, %%mm0 \n\t"
        "pxor       %%mm1, %%mm1 \n\t"
        "psubw      %%mm0, %%mm1 \n\t"
        "packuswb   %%mm0, %%mm0 \n\t"
        "packuswb   %%mm1, %%mm1 \n\t"
        ::"r"(dc)
    );
    __asm__ volatile(
        "movd          %0, %%mm2 \n\t"
        "movd          %1, %%mm3 \n\t"
        "movd          %2, %%mm4 \n\t"
        "movd          %3, %%mm5 \n\t"
        "paddusb    %%mm0, %%mm2 \n\t"
        "paddusb    %%mm0, %%mm3 \n\t"
        "paddusb    %%mm0, %%mm4 \n\t"
        "paddusb    %%mm0, %%mm5 \n\t"
        "psubusb    %%mm1, %%mm2 \n\t"
        "psubusb    %%mm1, %%mm3 \n\t"
        "psubusb    %%mm1, %%mm4 \n\t"
        "psubusb    %%mm1, %%mm5 \n\t"
        "movd       %%mm2, %0    \n\t"
        "movd       %%mm3, %1    \n\t"
        "movd       %%mm4, %2    \n\t"
        "movd       %%mm5, %3    \n\t"
        :"+m"(*(uint32_t*)(dest+0*linesize)),
         "+m"(*(uint32_t*)(dest+1*linesize)),
         "+m"(*(uint32_t*)(dest+2*linesize)),
         "+m"(*(uint32_t*)(dest+3*linesize))
    );
    dest += 4*linesize;
    __asm__ volatile(
        "movd          %0, %%mm2 \n\t"
        "movd          %1, %%mm3 \n\t"
        "movd          %2, %%mm4 \n\t"
        "movd          %3, %%mm5 \n\t"
        "paddusb    %%mm0, %%mm2 \n\t"
        "paddusb    %%mm0, %%mm3 \n\t"
        "paddusb    %%mm0, %%mm4 \n\t"
        "paddusb    %%mm0, %%mm5 \n\t"
        "psubusb    %%mm1, %%mm2 \n\t"
        "psubusb    %%mm1, %%mm3 \n\t"
        "psubusb    %%mm1, %%mm4 \n\t"
        "psubusb    %%mm1, %%mm5 \n\t"
        "movd       %%mm2, %0    \n\t"
        "movd       %%mm3, %1    \n\t"
        "movd       %%mm4, %2    \n\t"
        "movd       %%mm5, %3    \n\t"
        :"+m"(*(uint32_t*)(dest+0*linesize)),
         "+m"(*(uint32_t*)(dest+1*linesize)),
         "+m"(*(uint32_t*)(dest+2*linesize)),
         "+m"(*(uint32_t*)(dest+3*linesize))
    );
}

static void vc1_inv_trans_8x4_dc_mmxext(uint8_t *dest, int linesize,
                                        int16_t *block)
{
    int dc = block[0];
    dc = ( 3 * dc +  1) >> 1;
    dc = (17 * dc + 64) >> 7;
    __asm__ volatile(
        "movd          %0, %%mm0 \n\t"
        "pshufw $0, %%mm0, %%mm0 \n\t"
        "pxor       %%mm1, %%mm1 \n\t"
        "psubw      %%mm0, %%mm1 \n\t"
        "packuswb   %%mm0, %%mm0 \n\t"
        "packuswb   %%mm1, %%mm1 \n\t"
        ::"r"(dc)
    );
    __asm__ volatile(
        "movq          %0, %%mm2 \n\t"
        "movq          %1, %%mm3 \n\t"
        "movq          %2, %%mm4 \n\t"
        "movq          %3, %%mm5 \n\t"
        "paddusb    %%mm0, %%mm2 \n\t"
        "paddusb    %%mm0, %%mm3 \n\t"
        "paddusb    %%mm0, %%mm4 \n\t"
        "paddusb    %%mm0, %%mm5 \n\t"
        "psubusb    %%mm1, %%mm2 \n\t"
        "psubusb    %%mm1, %%mm3 \n\t"
        "psubusb    %%mm1, %%mm4 \n\t"
        "psubusb    %%mm1, %%mm5 \n\t"
        "movq       %%mm2, %0    \n\t"
        "movq       %%mm3, %1    \n\t"
        "movq       %%mm4, %2    \n\t"
        "movq       %%mm5, %3    \n\t"
        :"+m"(*(uint32_t*)(dest+0*linesize)),
         "+m"(*(uint32_t*)(dest+1*linesize)),
         "+m"(*(uint32_t*)(dest+2*linesize)),
         "+m"(*(uint32_t*)(dest+3*linesize))
    );
}

static void vc1_inv_trans_8x8_dc_mmxext(uint8_t *dest, int linesize,
                                        int16_t *block)
{
    int dc = block[0];
    dc = (3 * dc +  1) >> 1;
    dc = (3 * dc + 16) >> 5;
    __asm__ volatile(
        "movd          %0, %%mm0 \n\t"
        "pshufw $0, %%mm0, %%mm0 \n\t"
        "pxor       %%mm1, %%mm1 \n\t"
        "psubw      %%mm0, %%mm1 \n\t"
        "packuswb   %%mm0, %%mm0 \n\t"
        "packuswb   %%mm1, %%mm1 \n\t"
        ::"r"(dc)
    );
    __asm__ volatile(
        "movq          %0, %%mm2 \n\t"
        "movq          %1, %%mm3 \n\t"
        "movq          %2, %%mm4 \n\t"
        "movq          %3, %%mm5 \n\t"
        "paddusb    %%mm0, %%mm2 \n\t"
        "paddusb    %%mm0, %%mm3 \n\t"
        "paddusb    %%mm0, %%mm4 \n\t"
        "paddusb    %%mm0, %%mm5 \n\t"
        "psubusb    %%mm1, %%mm2 \n\t"
        "psubusb    %%mm1, %%mm3 \n\t"
        "psubusb    %%mm1, %%mm4 \n\t"
        "psubusb    %%mm1, %%mm5 \n\t"
        "movq       %%mm2, %0    \n\t"
        "movq       %%mm3, %1    \n\t"
        "movq       %%mm4, %2    \n\t"
        "movq       %%mm5, %3    \n\t"
        :"+m"(*(uint32_t*)(dest+0*linesize)),
         "+m"(*(uint32_t*)(dest+1*linesize)),
         "+m"(*(uint32_t*)(dest+2*linesize)),
         "+m"(*(uint32_t*)(dest+3*linesize))
    );
    dest += 4*linesize;
    __asm__ volatile(
        "movq          %0, %%mm2 \n\t"
        "movq          %1, %%mm3 \n\t"
        "movq          %2, %%mm4 \n\t"
        "movq          %3, %%mm5 \n\t"
        "paddusb    %%mm0, %%mm2 \n\t"
        "paddusb    %%mm0, %%mm3 \n\t"
        "paddusb    %%mm0, %%mm4 \n\t"
        "paddusb    %%mm0, %%mm5 \n\t"
        "psubusb    %%mm1, %%mm2 \n\t"
        "psubusb    %%mm1, %%mm3 \n\t"
        "psubusb    %%mm1, %%mm4 \n\t"
        "psubusb    %%mm1, %%mm5 \n\t"
        "movq       %%mm2, %0    \n\t"
        "movq       %%mm3, %1    \n\t"
        "movq       %%mm4, %2    \n\t"
        "movq       %%mm5, %3    \n\t"
        :"+m"(*(uint32_t*)(dest+0*linesize)),
         "+m"(*(uint32_t*)(dest+1*linesize)),
         "+m"(*(uint32_t*)(dest+2*linesize)),
         "+m"(*(uint32_t*)(dest+3*linesize))
    );
}

static void put_vc1_mspel_mc00_mmx(uint8_t *dst, const uint8_t *src,
                                   ptrdiff_t stride, int rnd)
{
    ff_put_pixels8_mmx(dst, src, stride, 8);
}

av_cold void ff_vc1dsp_init_mmx(VC1DSPContext *dsp)
{
    dsp->put_vc1_mspel_pixels_tab[ 0] = put_vc1_mspel_mc00_mmx;
    dsp->put_vc1_mspel_pixels_tab[ 4] = put_vc1_mspel_mc01_mmx;
    dsp->put_vc1_mspel_pixels_tab[ 8] = put_vc1_mspel_mc02_mmx;
    dsp->put_vc1_mspel_pixels_tab[12] = put_vc1_mspel_mc03_mmx;

    dsp->put_vc1_mspel_pixels_tab[ 1] = put_vc1_mspel_mc10_mmx;
    dsp->put_vc1_mspel_pixels_tab[ 5] = put_vc1_mspel_mc11_mmx;
    dsp->put_vc1_mspel_pixels_tab[ 9] = put_vc1_mspel_mc12_mmx;
    dsp->put_vc1_mspel_pixels_tab[13] = put_vc1_mspel_mc13_mmx;

    dsp->put_vc1_mspel_pixels_tab[ 2] = put_vc1_mspel_mc20_mmx;
    dsp->put_vc1_mspel_pixels_tab[ 6] = put_vc1_mspel_mc21_mmx;
    dsp->put_vc1_mspel_pixels_tab[10] = put_vc1_mspel_mc22_mmx;
    dsp->put_vc1_mspel_pixels_tab[14] = put_vc1_mspel_mc23_mmx;

    dsp->put_vc1_mspel_pixels_tab[ 3] = put_vc1_mspel_mc30_mmx;
    dsp->put_vc1_mspel_pixels_tab[ 7] = put_vc1_mspel_mc31_mmx;
    dsp->put_vc1_mspel_pixels_tab[11] = put_vc1_mspel_mc32_mmx;
    dsp->put_vc1_mspel_pixels_tab[15] = put_vc1_mspel_mc33_mmx;
}

av_cold void ff_vc1dsp_init_mmxext(VC1DSPContext *dsp)
{
    dsp->avg_vc1_mspel_pixels_tab[ 4] = avg_vc1_mspel_mc01_mmxext;
    dsp->avg_vc1_mspel_pixels_tab[ 8] = avg_vc1_mspel_mc02_mmxext;
    dsp->avg_vc1_mspel_pixels_tab[12] = avg_vc1_mspel_mc03_mmxext;

    dsp->avg_vc1_mspel_pixels_tab[ 1] = avg_vc1_mspel_mc10_mmxext;
    dsp->avg_vc1_mspel_pixels_tab[ 5] = avg_vc1_mspel_mc11_mmxext;
    dsp->avg_vc1_mspel_pixels_tab[ 9] = avg_vc1_mspel_mc12_mmxext;
    dsp->avg_vc1_mspel_pixels_tab[13] = avg_vc1_mspel_mc13_mmxext;

    dsp->avg_vc1_mspel_pixels_tab[ 2] = avg_vc1_mspel_mc20_mmxext;
    dsp->avg_vc1_mspel_pixels_tab[ 6] = avg_vc1_mspel_mc21_mmxext;
    dsp->avg_vc1_mspel_pixels_tab[10] = avg_vc1_mspel_mc22_mmxext;
    dsp->avg_vc1_mspel_pixels_tab[14] = avg_vc1_mspel_mc23_mmxext;

    dsp->avg_vc1_mspel_pixels_tab[ 3] = avg_vc1_mspel_mc30_mmxext;
    dsp->avg_vc1_mspel_pixels_tab[ 7] = avg_vc1_mspel_mc31_mmxext;
    dsp->avg_vc1_mspel_pixels_tab[11] = avg_vc1_mspel_mc32_mmxext;
    dsp->avg_vc1_mspel_pixels_tab[15] = avg_vc1_mspel_mc33_mmxext;

    dsp->vc1_inv_trans_8x8_dc = vc1_inv_trans_8x8_dc_mmxext;
    dsp->vc1_inv_trans_4x8_dc = vc1_inv_trans_4x8_dc_mmxext;
    dsp->vc1_inv_trans_8x4_dc = vc1_inv_trans_8x4_dc_mmxext;
    dsp->vc1_inv_trans_4x4_dc = vc1_inv_trans_4x4_dc_mmxext;
}
#endif /* HAVE_INLINE_ASM */
