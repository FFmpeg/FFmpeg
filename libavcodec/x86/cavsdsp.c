/*
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder.
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
 *
 * MMX-optimized DSP functions, based on H.264 optimizations by
 * Michael Niedermayer and Loren Merritt
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
#include "libavutil/cpu.h"
#include "libavutil/mem_internal.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/cavsdsp.h"
#include "libavcodec/idctdsp.h"
#include "constants.h"
#include "fpel.h"
#include "idctdsp.h"
#include "config.h"


#if HAVE_SSE2_EXTERNAL

void ff_cavs_idct8_sse2(int16_t *out, const int16_t *in);

static void cavs_idct8_add_sse2(uint8_t *dst, int16_t *block, ptrdiff_t stride)
{
    LOCAL_ALIGNED(16, int16_t, b2, [64]);
    ff_cavs_idct8_sse2(b2, block);
    ff_add_pixels_clamped_sse2(b2, dst, stride);
}

#endif /* HAVE_SSE2_EXTERNAL */

#if HAVE_MMXEXT_INLINE

DECLARE_ASM_CONST(8, uint64_t, pw_42) = 0x002A002A002A002AULL;
DECLARE_ASM_CONST(8, uint64_t, pw_96) = 0x0060006000600060ULL;

/*****************************************************************************
 *
 * motion compensation
 *
 ****************************************************************************/

/* vertical filter [-1 -2 96 42 -7  0]  */
#define QPEL_CAVSV1(A,B,C,D,E,F,OP,ADD, MUL1, MUL2) \
        "movd (%0), "#F"            \n\t"\
        "movq "#C", %%mm6           \n\t"\
        "pmullw "MANGLE(MUL1)", %%mm6\n\t"\
        "movq "#D", %%mm7           \n\t"\
        "pmullw "MANGLE(MUL2)", %%mm7\n\t"\
        "psllw $3, "#E"             \n\t"\
        "psubw "#E", %%mm6          \n\t"\
        "psraw $3, "#E"             \n\t"\
        "paddw %%mm7, %%mm6         \n\t"\
        "paddw "#E", %%mm6          \n\t"\
        "paddw "#B", "#B"           \n\t"\
        "pxor %%mm7, %%mm7          \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%mm7, "#F"      \n\t"\
        "psubw "#B", %%mm6          \n\t"\
        "psraw $1, "#B"             \n\t"\
        "psubw "#A", %%mm6          \n\t"\
        "paddw "MANGLE(ADD)", %%mm6 \n\t"\
        "psraw $7, %%mm6            \n\t"\
        "packuswb %%mm6, %%mm6      \n\t"\
        OP(%%mm6, (%1), A, d)            \
        "add %3, %1                 \n\t"

/* vertical filter [ 0 -1  5  5 -1  0]  */
#define QPEL_CAVSV2(A,B,C,D,E,F,OP,ADD, MUL1, MUL2) \
        "movd (%0), "#F"            \n\t"\
        "movq "#C", %%mm6           \n\t"\
        "paddw "#D", %%mm6          \n\t"\
        "pmullw "MANGLE(MUL1)", %%mm6\n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%mm7, "#F"      \n\t"\
        "psubw "#B", %%mm6          \n\t"\
        "psubw "#E", %%mm6          \n\t"\
        "paddw "MANGLE(ADD)", %%mm6 \n\t"\
        "psraw $3, %%mm6            \n\t"\
        "packuswb %%mm6, %%mm6      \n\t"\
        OP(%%mm6, (%1), A, d)            \
        "add %3, %1                 \n\t"

/* vertical filter [ 0 -7 42 96 -2 -1]  */
#define QPEL_CAVSV3(A,B,C,D,E,F,OP,ADD, MUL1, MUL2) \
        "movd (%0), "#F"            \n\t"\
        "movq "#C", %%mm6           \n\t"\
        "pmullw "MANGLE(MUL2)", %%mm6\n\t"\
        "movq "#D", %%mm7           \n\t"\
        "pmullw "MANGLE(MUL1)", %%mm7\n\t"\
        "psllw $3, "#B"             \n\t"\
        "psubw "#B", %%mm6          \n\t"\
        "psraw $3, "#B"             \n\t"\
        "paddw %%mm7, %%mm6         \n\t"\
        "paddw "#B", %%mm6          \n\t"\
        "paddw "#E", "#E"           \n\t"\
        "pxor %%mm7, %%mm7          \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%mm7, "#F"      \n\t"\
        "psubw "#E", %%mm6          \n\t"\
        "psraw $1, "#E"             \n\t"\
        "psubw "#F", %%mm6          \n\t"\
        "paddw "MANGLE(ADD)", %%mm6 \n\t"\
        "psraw $7, %%mm6            \n\t"\
        "packuswb %%mm6, %%mm6      \n\t"\
        OP(%%mm6, (%1), A, d)            \
        "add %3, %1                 \n\t"


#define QPEL_CAVSVNUM(VOP,OP,ADD,MUL1,MUL2)\
    int w= 2;\
    src -= 2*srcStride;\
    \
    while(w--){\
      __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movd (%0), %%mm0           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm1           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm2           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm3           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm4           \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpcklbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm4     \n\t"\
        VOP(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP, ADD, MUL1, MUL2)\
        VOP(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP, ADD, MUL1, MUL2)\
        VOP(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP, ADD, MUL1, MUL2)\
        VOP(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP, ADD, MUL1, MUL2)\
        VOP(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, OP, ADD, MUL1, MUL2)\
        VOP(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, OP, ADD, MUL1, MUL2)\
        VOP(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP, ADD, MUL1, MUL2)\
        VOP(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP, ADD, MUL1, MUL2)\
        \
        : "+a"(src), "+c"(dst)\
        : "S"((x86_reg)srcStride), "r"((x86_reg)dstStride)\
          NAMED_CONSTRAINTS_ADD(ADD,MUL1,MUL2)\
        : "memory"\
     );\
     if(h==16){\
        __asm__ volatile(\
            VOP(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP, ADD, MUL1, MUL2)\
            VOP(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP, ADD, MUL1, MUL2)\
            VOP(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, OP, ADD, MUL1, MUL2)\
            VOP(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, OP, ADD, MUL1, MUL2)\
            VOP(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP, ADD, MUL1, MUL2)\
            VOP(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP, ADD, MUL1, MUL2)\
            VOP(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP, ADD, MUL1, MUL2)\
            VOP(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP, ADD, MUL1, MUL2)\
            \
           : "+a"(src), "+c"(dst)\
           : "S"((x86_reg)srcStride), "r"((x86_reg)dstStride)\
             NAMED_CONSTRAINTS_ADD(ADD,MUL1,MUL2)\
           : "memory"\
        );\
     }\
     src += 4-(h+5)*srcStride;\
     dst += 4-h*dstStride;\
   }

#define QPEL_CAVS(OPNAME, OP, MMX)\
static void OPNAME ## cavs_qpel8_h_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{\
    int h=8;\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movq "MANGLE(ff_pw_5)", %%mm6\n\t"\
        "1:                         \n\t"\
        "movq    (%0), %%mm0        \n\t"\
        "movq   1(%0), %%mm2        \n\t"\
        "movq %%mm0, %%mm1          \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpckhbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm3, %%mm1         \n\t"\
        "pmullw %%mm6, %%mm0        \n\t"\
        "pmullw %%mm6, %%mm1        \n\t"\
        "movq   -1(%0), %%mm2       \n\t"\
        "movq    2(%0), %%mm4       \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "movq %%mm4, %%mm5          \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm4     \n\t"\
        "punpckhbw %%mm7, %%mm5     \n\t"\
        "paddw %%mm4, %%mm2         \n\t"\
        "paddw %%mm3, %%mm5         \n\t"\
        "psubw %%mm2, %%mm0         \n\t"\
        "psubw %%mm5, %%mm1         \n\t"\
        "movq "MANGLE(ff_pw_4)", %%mm5\n\t"\
        "paddw %%mm5, %%mm0         \n\t"\
        "paddw %%mm5, %%mm1         \n\t"\
        "psraw $3, %%mm0            \n\t"\
        "psraw $3, %%mm1            \n\t"\
        "packuswb %%mm1, %%mm0      \n\t"\
        OP(%%mm0, (%1),%%mm5, q)         \
        "add %3, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(src), "+c"(dst), "+m"(h)\
        : "d"((x86_reg)srcStride), "S"((x86_reg)dstStride)\
          NAMED_CONSTRAINTS_ADD(ff_pw_4,ff_pw_5)\
        : "memory"\
    );\
}\
\
static inline void OPNAME ## cavs_qpel8or16_v1_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride, int h)\
{                                                                       \
  QPEL_CAVSVNUM(QPEL_CAVSV1,OP,ff_pw_64,pw_96,pw_42)      \
}\
\
static inline void OPNAME ## cavs_qpel8or16_v2_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride, int h)\
{                                                                       \
  QPEL_CAVSVNUM(QPEL_CAVSV2,OP,ff_pw_4,ff_pw_5,pw_42)        \
}\
\
static inline void OPNAME ## cavs_qpel8or16_v3_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride, int h)\
{                                                                       \
  QPEL_CAVSVNUM(QPEL_CAVSV3,OP,ff_pw_64,pw_96,pw_42)      \
}\
\
static void OPNAME ## cavs_qpel8_v1_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v1_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static void OPNAME ## cavs_qpel16_v1_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v1_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## cavs_qpel8or16_v1_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static void OPNAME ## cavs_qpel8_v2_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v2_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static void OPNAME ## cavs_qpel16_v2_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v2_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## cavs_qpel8or16_v2_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static void OPNAME ## cavs_qpel8_v3_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v3_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static void OPNAME ## cavs_qpel16_v3_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v3_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## cavs_qpel8or16_v3_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static void OPNAME ## cavs_qpel16_h_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8_h_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## cavs_qpel8_h_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## cavs_qpel8_h_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## cavs_qpel8_h_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\

#define CAVS_MC(OPNAME, SIZE, MMX) \
static void OPNAME ## cavs_qpel ## SIZE ## _mc20_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_qpel ## SIZE ## _h_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc01_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_qpel ## SIZE ## _v1_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc02_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_qpel ## SIZE ## _v2_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc03_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_qpel ## SIZE ## _v3_ ## MMX(dst, src, stride, stride);\
}\

#define PUT_OP(a,b,temp, size) "mov" #size " " #a ", " #b "    \n\t"
#define AVG_MMXEXT_OP(a, b, temp, size) \
"mov" #size " " #b ", " #temp "   \n\t"\
"pavgb " #temp ", " #a "          \n\t"\
"mov" #size " " #a ", " #b "      \n\t"

#endif /* HAVE_MMXEXT_INLINE */

#if HAVE_MMX_EXTERNAL
static void put_cavs_qpel8_mc00_mmx(uint8_t *dst, const uint8_t *src,
                                    ptrdiff_t stride)
{
    ff_put_pixels8_mmx(dst, src, stride, 8);
}

static void avg_cavs_qpel8_mc00_mmxext(uint8_t *dst, const uint8_t *src,
                                       ptrdiff_t stride)
{
    ff_avg_pixels8_mmxext(dst, src, stride, 8);
}

static void put_cavs_qpel16_mc00_sse2(uint8_t *dst, const uint8_t *src,
                                      ptrdiff_t stride)
{
    ff_put_pixels16_sse2(dst, src, stride, 16);
}

static void avg_cavs_qpel16_mc00_sse2(uint8_t *dst, const uint8_t *src,
                                      ptrdiff_t stride)
{
    ff_avg_pixels16_sse2(dst, src, stride, 16);
}
#endif

static av_cold void cavsdsp_init_mmx(CAVSDSPContext *c)
{
#if HAVE_MMX_EXTERNAL
    c->put_cavs_qpel_pixels_tab[1][0] = put_cavs_qpel8_mc00_mmx;
#endif /* HAVE_MMX_EXTERNAL */
}

#define DSPFUNC(PFX, IDX, NUM, EXT)                                                       \
    c->PFX ## _cavs_qpel_pixels_tab[IDX][ 2] = PFX ## _cavs_qpel ## NUM ## _mc20_ ## EXT; \
    c->PFX ## _cavs_qpel_pixels_tab[IDX][ 4] = PFX ## _cavs_qpel ## NUM ## _mc01_ ## EXT; \
    c->PFX ## _cavs_qpel_pixels_tab[IDX][ 8] = PFX ## _cavs_qpel ## NUM ## _mc02_ ## EXT; \
    c->PFX ## _cavs_qpel_pixels_tab[IDX][12] = PFX ## _cavs_qpel ## NUM ## _mc03_ ## EXT; \

#if HAVE_MMXEXT_INLINE
QPEL_CAVS(put_,        PUT_OP, mmxext)
QPEL_CAVS(avg_, AVG_MMXEXT_OP, mmxext)

CAVS_MC(put_,  8, mmxext)
CAVS_MC(put_, 16, mmxext)
CAVS_MC(avg_,  8, mmxext)
CAVS_MC(avg_, 16, mmxext)
#endif /* HAVE_MMXEXT_INLINE */

av_cold void ff_cavsdsp_init_x86(CAVSDSPContext *c)
{
    av_unused int cpu_flags = av_get_cpu_flags();

    if (X86_MMX(cpu_flags))
        cavsdsp_init_mmx(c);

#if HAVE_MMXEXT_INLINE
    if (INLINE_MMXEXT(cpu_flags)) {
        DSPFUNC(put, 0, 16, mmxext);
        DSPFUNC(put, 1,  8, mmxext);
        DSPFUNC(avg, 0, 16, mmxext);
        DSPFUNC(avg, 1,  8, mmxext);
    }
#endif
#if HAVE_MMX_EXTERNAL
    if (EXTERNAL_MMXEXT(cpu_flags)) {
        c->avg_cavs_qpel_pixels_tab[1][0] = avg_cavs_qpel8_mc00_mmxext;
    }
#endif
#if HAVE_SSE2_EXTERNAL
    if (EXTERNAL_SSE2(cpu_flags)) {
        c->put_cavs_qpel_pixels_tab[0][0] = put_cavs_qpel16_mc00_sse2;
        c->avg_cavs_qpel_pixels_tab[0][0] = avg_cavs_qpel16_mc00_sse2;

        c->cavs_idct8_add = cavs_idct8_add_sse2;
        c->idct_perm      = FF_IDCT_PERM_TRANSPOSE;
    }
#endif
}
