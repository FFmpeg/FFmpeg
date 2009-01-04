/*
 * Copyright (c) 2005 Zoltan Hidvegi <hzoli -a- hzoli -d- com>,
 *                    Loren Merritt
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

/**
 * MMX optimized version of (put|avg)_h264_chroma_mc8.
 * H264_CHROMA_MC8_TMPL must be defined to the desired function name
 * H264_CHROMA_OP must be defined to empty for put and pavgb/pavgusb for avg
 * H264_CHROMA_MC8_MV0 must be defined to a (put|avg)_pixels8 function
 */
static void H264_CHROMA_MC8_TMPL(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y, const uint64_t *rnd_reg)
{
    DECLARE_ALIGNED_8(uint64_t, AA);
    DECLARE_ALIGNED_8(uint64_t, DD);
    int i;

    if(y==0 && x==0) {
        /* no filter needed */
        H264_CHROMA_MC8_MV0(dst, src, stride, h);
        return;
    }

    assert(x<8 && y<8 && x>=0 && y>=0);

    if(y==0 || x==0)
    {
        /* 1 dimensional filter only */
        const int dxy = x ? 1 : stride;

        __asm__ volatile(
            "movd %0, %%mm5\n\t"
            "movq %1, %%mm4\n\t"
            "movq %2, %%mm6\n\t"         /* mm6 = rnd >> 3 */
            "punpcklwd %%mm5, %%mm5\n\t"
            "punpckldq %%mm5, %%mm5\n\t" /* mm5 = B = x */
            "pxor %%mm7, %%mm7\n\t"
            "psubw %%mm5, %%mm4\n\t"     /* mm4 = A = 8-x */
            :: "rm"(x+y), "m"(ff_pw_8), "m"(*(rnd_reg+1)));

        for(i=0; i<h; i++) {
            __asm__ volatile(
                /* mm0 = src[0..7], mm1 = src[1..8] */
                "movq %0, %%mm0\n\t"
                "movq %1, %%mm2\n\t"
                :: "m"(src[0]), "m"(src[dxy]));

            __asm__ volatile(
                /* [mm0,mm1] = A * src[0..7] */
                /* [mm2,mm3] = B * src[1..8] */
                "movq %%mm0, %%mm1\n\t"
                "movq %%mm2, %%mm3\n\t"
                "punpcklbw %%mm7, %%mm0\n\t"
                "punpckhbw %%mm7, %%mm1\n\t"
                "punpcklbw %%mm7, %%mm2\n\t"
                "punpckhbw %%mm7, %%mm3\n\t"
                "pmullw %%mm4, %%mm0\n\t"
                "pmullw %%mm4, %%mm1\n\t"
                "pmullw %%mm5, %%mm2\n\t"
                "pmullw %%mm5, %%mm3\n\t"

                /* dst[0..7] = (A * src[0..7] + B * src[1..8] + (rnd >> 3)) >> 3 */
                "paddw %%mm6, %%mm0\n\t"
                "paddw %%mm6, %%mm1\n\t"
                "paddw %%mm2, %%mm0\n\t"
                "paddw %%mm3, %%mm1\n\t"
                "psrlw $3, %%mm0\n\t"
                "psrlw $3, %%mm1\n\t"
                "packuswb %%mm1, %%mm0\n\t"
                H264_CHROMA_OP(%0, %%mm0)
                "movq %%mm0, %0\n\t"
                : "=m" (dst[0]));

            src += stride;
            dst += stride;
        }
        return;
    }

    /* general case, bilinear */
    __asm__ volatile("movd %2, %%mm4\n\t"
                 "movd %3, %%mm6\n\t"
                 "punpcklwd %%mm4, %%mm4\n\t"
                 "punpcklwd %%mm6, %%mm6\n\t"
                 "punpckldq %%mm4, %%mm4\n\t" /* mm4 = x words */
                 "punpckldq %%mm6, %%mm6\n\t" /* mm6 = y words */
                 "movq %%mm4, %%mm5\n\t"
                 "pmullw %%mm6, %%mm4\n\t"    /* mm4 = x * y */
                 "psllw $3, %%mm5\n\t"
                 "psllw $3, %%mm6\n\t"
                 "movq %%mm5, %%mm7\n\t"
                 "paddw %%mm6, %%mm7\n\t"
                 "movq %%mm4, %1\n\t"         /* DD = x * y */
                 "psubw %%mm4, %%mm5\n\t"     /* mm5 = B = 8x - xy */
                 "psubw %%mm4, %%mm6\n\t"     /* mm6 = C = 8y - xy */
                 "paddw %4, %%mm4\n\t"
                 "psubw %%mm7, %%mm4\n\t"     /* mm4 = A = xy - (8x+8y) + 64 */
                 "pxor %%mm7, %%mm7\n\t"
                 "movq %%mm4, %0\n\t"
                 : "=m" (AA), "=m" (DD) : "rm" (x), "rm" (y), "m" (ff_pw_64));

    __asm__ volatile(
        /* mm0 = src[0..7], mm1 = src[1..8] */
        "movq %0, %%mm0\n\t"
        "movq %1, %%mm1\n\t"
        : : "m" (src[0]), "m" (src[1]));

    for(i=0; i<h; i++) {
        src += stride;

        __asm__ volatile(
            /* mm2 = A * src[0..3] + B * src[1..4] */
            /* mm3 = A * src[4..7] + B * src[5..8] */
            "movq %%mm0, %%mm2\n\t"
            "movq %%mm1, %%mm3\n\t"
            "punpckhbw %%mm7, %%mm0\n\t"
            "punpcklbw %%mm7, %%mm1\n\t"
            "punpcklbw %%mm7, %%mm2\n\t"
            "punpckhbw %%mm7, %%mm3\n\t"
            "pmullw %0, %%mm0\n\t"
            "pmullw %0, %%mm2\n\t"
            "pmullw %%mm5, %%mm1\n\t"
            "pmullw %%mm5, %%mm3\n\t"
            "paddw %%mm1, %%mm2\n\t"
            "paddw %%mm0, %%mm3\n\t"
            : : "m" (AA));

        __asm__ volatile(
            /* [mm2,mm3] += C * src[0..7] */
            "movq %0, %%mm0\n\t"
            "movq %%mm0, %%mm1\n\t"
            "punpcklbw %%mm7, %%mm0\n\t"
            "punpckhbw %%mm7, %%mm1\n\t"
            "pmullw %%mm6, %%mm0\n\t"
            "pmullw %%mm6, %%mm1\n\t"
            "paddw %%mm0, %%mm2\n\t"
            "paddw %%mm1, %%mm3\n\t"
            : : "m" (src[0]));

        __asm__ volatile(
            /* [mm2,mm3] += D * src[1..8] */
            "movq %1, %%mm1\n\t"
            "movq %%mm1, %%mm0\n\t"
            "movq %%mm1, %%mm4\n\t"
            "punpcklbw %%mm7, %%mm0\n\t"
            "punpckhbw %%mm7, %%mm4\n\t"
            "pmullw %2, %%mm0\n\t"
            "pmullw %2, %%mm4\n\t"
            "paddw %%mm0, %%mm2\n\t"
            "paddw %%mm4, %%mm3\n\t"
            "movq %0, %%mm0\n\t"
            : : "m" (src[0]), "m" (src[1]), "m" (DD));

        __asm__ volatile(
            /* dst[0..7] = ([mm2,mm3] + rnd) >> 6 */
            "paddw %1, %%mm2\n\t"
            "paddw %1, %%mm3\n\t"
            "psrlw $6, %%mm2\n\t"
            "psrlw $6, %%mm3\n\t"
            "packuswb %%mm3, %%mm2\n\t"
            H264_CHROMA_OP(%0, %%mm2)
            "movq %%mm2, %0\n\t"
            : "=m" (dst[0]) : "m" (*rnd_reg));
        dst+= stride;
    }
}

static void H264_CHROMA_MC4_TMPL(uint8_t *dst/*align 4*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y, const uint64_t *rnd_reg)
{
    __asm__ volatile(
        "pxor   %%mm7, %%mm7        \n\t"
        "movd %5, %%mm2             \n\t"
        "movd %6, %%mm3             \n\t"
        "movq "MANGLE(ff_pw_8)", %%mm4\n\t"
        "movq "MANGLE(ff_pw_8)", %%mm5\n\t"
        "punpcklwd %%mm2, %%mm2     \n\t"
        "punpcklwd %%mm3, %%mm3     \n\t"
        "punpcklwd %%mm2, %%mm2     \n\t"
        "punpcklwd %%mm3, %%mm3     \n\t"
        "psubw %%mm2, %%mm4         \n\t"
        "psubw %%mm3, %%mm5         \n\t"

        "movd  (%1), %%mm0          \n\t"
        "movd 1(%1), %%mm6          \n\t"
        "add %3, %1                 \n\t"
        "punpcklbw %%mm7, %%mm0     \n\t"
        "punpcklbw %%mm7, %%mm6     \n\t"
        "pmullw %%mm4, %%mm0        \n\t"
        "pmullw %%mm2, %%mm6        \n\t"
        "paddw %%mm0, %%mm6         \n\t"

        "1:                         \n\t"
        "movd  (%1), %%mm0          \n\t"
        "movd 1(%1), %%mm1          \n\t"
        "add %3, %1                 \n\t"
        "punpcklbw %%mm7, %%mm0     \n\t"
        "punpcklbw %%mm7, %%mm1     \n\t"
        "pmullw %%mm4, %%mm0        \n\t"
        "pmullw %%mm2, %%mm1        \n\t"
        "paddw %%mm0, %%mm1         \n\t"
        "movq %%mm1, %%mm0          \n\t"
        "pmullw %%mm5, %%mm6        \n\t"
        "pmullw %%mm3, %%mm1        \n\t"
        "paddw %4, %%mm6            \n\t"
        "paddw %%mm6, %%mm1         \n\t"
        "psrlw $6, %%mm1            \n\t"
        "packuswb %%mm1, %%mm1      \n\t"
        H264_CHROMA_OP4((%0), %%mm1, %%mm6)
        "movd %%mm1, (%0)           \n\t"
        "add %3, %0                 \n\t"
        "movd  (%1), %%mm6          \n\t"
        "movd 1(%1), %%mm1          \n\t"
        "add %3, %1                 \n\t"
        "punpcklbw %%mm7, %%mm6     \n\t"
        "punpcklbw %%mm7, %%mm1     \n\t"
        "pmullw %%mm4, %%mm6        \n\t"
        "pmullw %%mm2, %%mm1        \n\t"
        "paddw %%mm6, %%mm1         \n\t"
        "movq %%mm1, %%mm6          \n\t"
        "pmullw %%mm5, %%mm0        \n\t"
        "pmullw %%mm3, %%mm1        \n\t"
        "paddw %4, %%mm0            \n\t"
        "paddw %%mm0, %%mm1         \n\t"
        "psrlw $6, %%mm1            \n\t"
        "packuswb %%mm1, %%mm1      \n\t"
        H264_CHROMA_OP4((%0), %%mm1, %%mm0)
        "movd %%mm1, (%0)           \n\t"
        "add %3, %0                 \n\t"
        "sub $2, %2                 \n\t"
        "jnz 1b                     \n\t"
        : "+r"(dst), "+r"(src), "+r"(h)
        : "r"((x86_reg)stride), "m"(*rnd_reg), "m"(x), "m"(y)
    );
}

#ifdef H264_CHROMA_MC2_TMPL
static void H264_CHROMA_MC2_TMPL(uint8_t *dst/*align 2*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    int tmp = ((1<<16)-1)*x + 8;
    int CD= tmp*y;
    int AB= (tmp<<3) - CD;
    __asm__ volatile(
        /* mm5 = {A,B,A,B} */
        /* mm6 = {C,D,C,D} */
        "movd %0, %%mm5\n\t"
        "movd %1, %%mm6\n\t"
        "punpckldq %%mm5, %%mm5\n\t"
        "punpckldq %%mm6, %%mm6\n\t"
        "pxor %%mm7, %%mm7\n\t"
        /* mm0 = src[0,1,1,2] */
        "movd %2, %%mm2\n\t"
        "punpcklbw %%mm7, %%mm2\n\t"
        "pshufw $0x94, %%mm2, %%mm2\n\t"
        :: "r"(AB), "r"(CD), "m"(src[0]));


    __asm__ volatile(
        "1:\n\t"
        "add %4, %1\n\t"
        /* mm1 = A * src[0,1] + B * src[1,2] */
        "movq    %%mm2, %%mm1\n\t"
        "pmaddwd %%mm5, %%mm1\n\t"
        /* mm0 = src[0,1,1,2] */
        "movd (%1), %%mm0\n\t"
        "punpcklbw %%mm7, %%mm0\n\t"
        "pshufw $0x94, %%mm0, %%mm0\n\t"
        /* mm1 += C * src[0,1] + D * src[1,2] */
        "movq    %%mm0, %%mm2\n\t"
        "pmaddwd %%mm6, %%mm0\n\t"
        "paddw      %3, %%mm1\n\t"
        "paddw   %%mm0, %%mm1\n\t"
        /* dst[0,1] = pack((mm1 + 32) >> 6) */
        "psrlw $6, %%mm1\n\t"
        "packssdw %%mm7, %%mm1\n\t"
        "packuswb %%mm7, %%mm1\n\t"
        H264_CHROMA_OP4((%0), %%mm1, %%mm3)
        "movd %%mm1, %%esi\n\t"
        "movw %%si, (%0)\n\t"
        "add %4, %0\n\t"
        "sub $1, %2\n\t"
        "jnz 1b\n\t"
        : "+r" (dst), "+r"(src), "+r"(h)
        : "m" (ff_pw_32), "r"((x86_reg)stride)
        : "%esi");

}
#endif

