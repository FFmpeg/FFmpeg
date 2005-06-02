/*
 * Copyright (c) 2005 Zoltan Hidvegi <hzoli -a- hzoli -d- com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * MMX optimized version of (put|avg)_h264_chroma_mc8.
 * H264_CHROMA_MC8_TMPL must be defined to the desired function name and
 * H264_CHROMA_OP must be defined to empty for put and pavgb/pavgusb for avg.
 */
static void H264_CHROMA_MC8_TMPL(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    uint64_t AA __align8;
    uint64_t DD __align8;
    unsigned long srcos = (long)src & 7;
    uint64_t sh1 __align8 = srcos * 8;
    uint64_t sh2 __align8 = 56 - sh1;
    int i;

    assert(x<8 && y<8 && x>=0 && y>=0);

    asm volatile("movd %1, %%mm4\n\t"
                 "movd %2, %%mm6\n\t"
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
                 "movq %%mm4, %0\n\t"         /* DD = x * y */
                 "psubw %%mm4, %%mm5\n\t"     /* mm5 = B = 8x - xy */
                 "psubw %%mm4, %%mm6\n\t"     /* mm6 = C = 8y - xy */
                 "paddw %3, %%mm4\n\t"
                 "psubw %%mm7, %%mm4\n\t"     /* mm4 = A = xy - (8x+8y) + 64 */
                 "pxor %%mm7, %%mm7\n\t"
                 : "=m" (DD) : "rm" (x), "rm" (y), "m" (ff_pw_64));

    asm volatile("movq %%mm4, %0" : "=m" (AA));

    src -= srcos;
    asm volatile(
        /* mm0 = src[0..7], mm1 = src[1..8] */
        "movq %0, %%mm1\n\t"
        "movq %1, %%mm0\n\t"
        "psrlq %2, %%mm1\n\t"
        "psllq %3, %%mm0\n\t"
        "movq %%mm0, %%mm4\n\t"
        "psllq $8, %%mm0\n\t"
        "por %%mm1, %%mm0\n\t"
        "psrlq $8, %%mm1\n\t"
        "por %%mm4, %%mm1\n\t"
        : : "m" (src[0]), "m" (src[8]), "m" (sh1), "m" (sh2));

    for(i=0; i<h; i++) {
        asm volatile(
            /* [mm2,mm3] = A * src[0..7] */
            "movq %%mm0, %%mm2\n\t"
            "punpcklbw %%mm7, %%mm2\n\t"
            "pmullw %0, %%mm2\n\t"
            "movq %%mm0, %%mm3\n\t"
            "punpckhbw %%mm7, %%mm3\n\t"
            "pmullw %0, %%mm3\n\t"

            /* [mm2,mm3] += B * src[1..8] */
            "movq %%mm1, %%mm0\n\t"
            "punpcklbw %%mm7, %%mm0\n\t"
            "pmullw %%mm5, %%mm0\n\t"
            "punpckhbw %%mm7, %%mm1\n\t"
            "pmullw %%mm5, %%mm1\n\t"
            "paddw %%mm0, %%mm2\n\t"
            "paddw %%mm1, %%mm3\n\t"
            : : "m" (AA));

        src += stride;
        asm volatile(
            /* mm0 = src[0..7], mm1 = src[1..8] */
            "movq %0, %%mm1\n\t"
            "movq %1, %%mm0\n\t"
            "psrlq %2, %%mm1\n\t"
            "psllq %3, %%mm0\n\t"
            "movq %%mm0, %%mm4\n\t"
            "psllq $8, %%mm0\n\t"
            "por %%mm1, %%mm0\n\t"
            "psrlq $8, %%mm1\n\t"
            "por %%mm4, %%mm1\n\t"
            : : "m" (src[0]), "m" (src[8]), "m" (sh1), "m" (sh2));

        asm volatile(
            /* [mm2,mm3] += C *  src[0..7] */
            "movq %mm0, %mm4\n\t"
            "punpcklbw %mm7, %mm4\n\t"
            "pmullw %mm6, %mm4\n\t"
            "paddw %mm4, %mm2\n\t"
            "movq %mm0, %mm4\n\t"
            "punpckhbw %mm7, %mm4\n\t"
            "pmullw %mm6, %mm4\n\t"
            "paddw %mm4, %mm3\n\t");

        asm volatile(
            /* [mm2,mm3] += D *  src[1..8] */
            "movq %%mm1, %%mm4\n\t"
            "punpcklbw %%mm7, %%mm4\n\t"
            "pmullw %0, %%mm4\n\t"
            "paddw %%mm4, %%mm2\n\t"
            "movq %%mm1, %%mm4\n\t"
            "punpckhbw %%mm7, %%mm4\n\t"
            "pmullw %0, %%mm4\n\t"
            "paddw %%mm4, %%mm3\n\t"
            : : "m" (DD));

        asm volatile(
            /* dst[0..7] = pack(([mm2,mm3] + 32) >> 6) */
            "paddw %1, %%mm2\n\t"
            "paddw %1, %%mm3\n\t"
            "psrlw $6, %%mm2\n\t"
            "psrlw $6, %%mm3\n\t"
            "packuswb %%mm3, %%mm2\n\t"
            H264_CHROMA_OP(%0, %%mm2)
            "movq %%mm2, %0\n\t"
            : "=m" (dst[0]) : "m" (ff_pw_32));
        dst+= stride;
    }
}
