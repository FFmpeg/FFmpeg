/*
 * Copyright (C) 2001-2003 Michael Niedermayer <michaelni@gmx.at>
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

#include "../swscale_internal.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"

#define RET 0xC3 // near return opcode for x86
#define PREFETCH "prefetchnta"

#if HAVE_INLINE_ASM
av_cold int ff_init_hscaler_mmxext(int dstW, int xInc, uint8_t *filterCode,
                                       int16_t *filter, int32_t *filterPos,
                                       int numSplits)
{
    uint8_t *fragmentA;
    x86_reg imm8OfPShufW1A;
    x86_reg imm8OfPShufW2A;
    x86_reg fragmentLengthA;
    uint8_t *fragmentB;
    x86_reg imm8OfPShufW1B;
    x86_reg imm8OfPShufW2B;
    x86_reg fragmentLengthB;
    int fragmentPos;

    int xpos, i;

    // create an optimized horizontal scaling routine
    /* This scaler is made of runtime-generated MMXEXT code using specially tuned
     * pshufw instructions. For every four output pixels, if four input pixels
     * are enough for the fast bilinear scaling, then a chunk of fragmentB is
     * used. If five input pixels are needed, then a chunk of fragmentA is used.
     */

    // code fragment

    __asm__ volatile (
        "jmp                         9f                 \n\t"
        // Begin
        "0:                                             \n\t"
        "movq    (%%"REG_d", %%"REG_a"), %%mm3          \n\t"
        "movd    (%%"REG_c", %%"REG_S"), %%mm0          \n\t"
        "movd   1(%%"REG_c", %%"REG_S"), %%mm1          \n\t"
        "punpcklbw                %%mm7, %%mm1          \n\t"
        "punpcklbw                %%mm7, %%mm0          \n\t"
        "pshufw                   $0xFF, %%mm1, %%mm1   \n\t"
        "1:                                             \n\t"
        "pshufw                   $0xFF, %%mm0, %%mm0   \n\t"
        "2:                                             \n\t"
        "psubw                    %%mm1, %%mm0          \n\t"
        "movl   8(%%"REG_b", %%"REG_a"), %%esi          \n\t"
        "pmullw                   %%mm3, %%mm0          \n\t"
        "psllw                       $7, %%mm1          \n\t"
        "paddw                    %%mm1, %%mm0          \n\t"

        "movq                     %%mm0, (%%"REG_D", %%"REG_a") \n\t"

        "add                         $8, %%"REG_a"      \n\t"
        // End
        "9:                                             \n\t"
        "lea       " LOCAL_MANGLE(0b) ", %0             \n\t"
        "lea       " LOCAL_MANGLE(1b) ", %1             \n\t"
        "lea       " LOCAL_MANGLE(2b) ", %2             \n\t"
        "dec                         %1                 \n\t"
        "dec                         %2                 \n\t"
        "sub                         %0, %1             \n\t"
        "sub                         %0, %2             \n\t"
        "lea       " LOCAL_MANGLE(9b) ", %3             \n\t"
        "sub                         %0, %3             \n\t"


        : "=r" (fragmentA), "=r" (imm8OfPShufW1A), "=r" (imm8OfPShufW2A),
          "=r" (fragmentLengthA)
        );

    __asm__ volatile (
        "jmp                         9f                 \n\t"
        // Begin
        "0:                                             \n\t"
        "movq    (%%"REG_d", %%"REG_a"), %%mm3          \n\t"
        "movd    (%%"REG_c", %%"REG_S"), %%mm0          \n\t"
        "punpcklbw                %%mm7, %%mm0          \n\t"
        "pshufw                   $0xFF, %%mm0, %%mm1   \n\t"
        "1:                                             \n\t"
        "pshufw                   $0xFF, %%mm0, %%mm0   \n\t"
        "2:                                             \n\t"
        "psubw                    %%mm1, %%mm0          \n\t"
        "movl   8(%%"REG_b", %%"REG_a"), %%esi          \n\t"
        "pmullw                   %%mm3, %%mm0          \n\t"
        "psllw                       $7, %%mm1          \n\t"
        "paddw                    %%mm1, %%mm0          \n\t"

        "movq                     %%mm0, (%%"REG_D", %%"REG_a") \n\t"

        "add                         $8, %%"REG_a"      \n\t"
        // End
        "9:                                             \n\t"
        "lea       " LOCAL_MANGLE(0b) ", %0             \n\t"
        "lea       " LOCAL_MANGLE(1b) ", %1             \n\t"
        "lea       " LOCAL_MANGLE(2b) ", %2             \n\t"
        "dec                         %1                 \n\t"
        "dec                         %2                 \n\t"
        "sub                         %0, %1             \n\t"
        "sub                         %0, %2             \n\t"
        "lea       " LOCAL_MANGLE(9b) ", %3             \n\t"
        "sub                         %0, %3             \n\t"


        : "=r" (fragmentB), "=r" (imm8OfPShufW1B), "=r" (imm8OfPShufW2B),
          "=r" (fragmentLengthB)
        );

    xpos        = 0; // lumXInc/2 - 0x8000; // difference between pixel centers
    fragmentPos = 0;

    for (i = 0; i < dstW / numSplits; i++) {
        int xx = xpos >> 16;

        if ((i & 3) == 0) {
            int a                  = 0;
            int b                  = ((xpos + xInc) >> 16) - xx;
            int c                  = ((xpos + xInc * 2) >> 16) - xx;
            int d                  = ((xpos + xInc * 3) >> 16) - xx;
            int inc                = (d + 1 < 4);
            uint8_t *fragment      = inc ? fragmentB : fragmentA;
            x86_reg imm8OfPShufW1  = inc ? imm8OfPShufW1B : imm8OfPShufW1A;
            x86_reg imm8OfPShufW2  = inc ? imm8OfPShufW2B : imm8OfPShufW2A;
            x86_reg fragmentLength = inc ? fragmentLengthB : fragmentLengthA;
            int maxShift           = 3 - (d + inc);
            int shift              = 0;

            if (filterCode) {
                filter[i]        = ((xpos              & 0xFFFF) ^ 0xFFFF) >> 9;
                filter[i + 1]    = (((xpos + xInc)     & 0xFFFF) ^ 0xFFFF) >> 9;
                filter[i + 2]    = (((xpos + xInc * 2) & 0xFFFF) ^ 0xFFFF) >> 9;
                filter[i + 3]    = (((xpos + xInc * 3) & 0xFFFF) ^ 0xFFFF) >> 9;
                filterPos[i / 2] = xx;

                memcpy(filterCode + fragmentPos, fragment, fragmentLength);

                filterCode[fragmentPos + imm8OfPShufW1] =  (a + inc)       |
                                                          ((b + inc) << 2) |
                                                          ((c + inc) << 4) |
                                                          ((d + inc) << 6);
                filterCode[fragmentPos + imm8OfPShufW2] =  a | (b << 2) |
                                                               (c << 4) |
                                                               (d << 6);

                if (i + 4 - inc >= dstW)
                    shift = maxShift;               // avoid overread
                else if ((filterPos[i / 2] & 3) <= maxShift)
                    shift = filterPos[i / 2] & 3;   // align

                if (shift && i >= shift) {
                    filterCode[fragmentPos + imm8OfPShufW1] += 0x55 * shift;
                    filterCode[fragmentPos + imm8OfPShufW2] += 0x55 * shift;
                    filterPos[i / 2]                        -= shift;
                }
            }

            fragmentPos += fragmentLength;

            if (filterCode)
                filterCode[fragmentPos] = RET;
        }
        xpos += xInc;
    }
    if (filterCode)
        filterPos[((i / 2) + 1) & (~1)] = xpos >> 16;  // needed to jump to the next part

    return fragmentPos + 1;
}

void ff_hyscale_fast_mmxext(SwsContext *c, int16_t *dst,
                                 int dstWidth, const uint8_t *src,
                                 int srcW, int xInc)
{
    int32_t *filterPos = c->hLumFilterPos;
    int16_t *filter    = c->hLumFilter;
    void    *mmxextFilterCode = c->lumMmxextFilterCode;
    int i;
#if ARCH_X86_64
    uint64_t retsave;
#else
#if defined(PIC)
    uint64_t ebxsave;
#endif
#endif

    __asm__ volatile(
#if ARCH_X86_64
        "mov               -8(%%rsp), %%"REG_a" \n\t"
        "mov               %%"REG_a", %5        \n\t"  // retsave
#else
#if defined(PIC)
        "mov               %%"REG_b", %5        \n\t"  // ebxsave
#endif
#endif
        "pxor                  %%mm7, %%mm7     \n\t"
        "mov                      %0, %%"REG_c" \n\t"
        "mov                      %1, %%"REG_D" \n\t"
        "mov                      %2, %%"REG_d" \n\t"
        "mov                      %3, %%"REG_b" \n\t"
        "xor               %%"REG_a", %%"REG_a" \n\t" // i
        PREFETCH"        (%%"REG_c")            \n\t"
        PREFETCH"      32(%%"REG_c")            \n\t"
        PREFETCH"      64(%%"REG_c")            \n\t"

#if ARCH_X86_64
#define CALL_MMXEXT_FILTER_CODE \
        "movl            (%%"REG_b"), %%esi     \n\t"\
        "call                    *%4            \n\t"\
        "movl (%%"REG_b", %%"REG_a"), %%esi     \n\t"\
        "add               %%"REG_S", %%"REG_c" \n\t"\
        "add               %%"REG_a", %%"REG_D" \n\t"\
        "xor               %%"REG_a", %%"REG_a" \n\t"\

#else
#define CALL_MMXEXT_FILTER_CODE \
        "movl (%%"REG_b"), %%esi        \n\t"\
        "call         *%4                       \n\t"\
        "addl (%%"REG_b", %%"REG_a"), %%"REG_c" \n\t"\
        "add               %%"REG_a", %%"REG_D" \n\t"\
        "xor               %%"REG_a", %%"REG_a" \n\t"\

#endif /* ARCH_X86_64 */

        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE

#if ARCH_X86_64
        "mov                      %5, %%"REG_a" \n\t"
        "mov               %%"REG_a", -8(%%rsp) \n\t"
#else
#if defined(PIC)
        "mov                      %5, %%"REG_b" \n\t"
#endif
#endif
        :: "m" (src), "m" (dst), "m" (filter), "m" (filterPos),
           "m" (mmxextFilterCode)
#if ARCH_X86_64
          ,"m"(retsave)
#else
#if defined(PIC)
          ,"m" (ebxsave)
#endif
#endif
        : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S, "%"REG_D
#if ARCH_X86_64 || !defined(PIC)
         ,"%"REG_b
#endif
    );

    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--)
        dst[i] = src[srcW-1]*128;
}

void ff_hcscale_fast_mmxext(SwsContext *c, int16_t *dst1, int16_t *dst2,
                                 int dstWidth, const uint8_t *src1,
                                 const uint8_t *src2, int srcW, int xInc)
{
    int32_t *filterPos = c->hChrFilterPos;
    int16_t *filter    = c->hChrFilter;
    void    *mmxextFilterCode = c->chrMmxextFilterCode;
    int i;
#if ARCH_X86_64
    DECLARE_ALIGNED(8, uint64_t, retsave);
#else
#if defined(PIC)
    DECLARE_ALIGNED(8, uint64_t, ebxsave);
#endif
#endif
    __asm__ volatile(
#if ARCH_X86_64
        "mov          -8(%%rsp), %%"REG_a"  \n\t"
        "mov          %%"REG_a", %7         \n\t"  // retsave
#else
#if defined(PIC)
        "mov          %%"REG_b", %7         \n\t"  // ebxsave
#endif
#endif
        "pxor             %%mm7, %%mm7      \n\t"
        "mov                 %0, %%"REG_c"  \n\t"
        "mov                 %1, %%"REG_D"  \n\t"
        "mov                 %2, %%"REG_d"  \n\t"
        "mov                 %3, %%"REG_b"  \n\t"
        "xor          %%"REG_a", %%"REG_a"  \n\t" // i
        PREFETCH"   (%%"REG_c")             \n\t"
        PREFETCH" 32(%%"REG_c")             \n\t"
        PREFETCH" 64(%%"REG_c")             \n\t"

        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE
        "xor          %%"REG_a", %%"REG_a"  \n\t" // i
        "mov                 %5, %%"REG_c"  \n\t" // src2
        "mov                 %6, %%"REG_D"  \n\t" // dst2
        PREFETCH"   (%%"REG_c")             \n\t"
        PREFETCH" 32(%%"REG_c")             \n\t"
        PREFETCH" 64(%%"REG_c")             \n\t"

        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE
        CALL_MMXEXT_FILTER_CODE

#if ARCH_X86_64
        "mov                 %7, %%"REG_a"  \n\t"
        "mov          %%"REG_a", -8(%%rsp)  \n\t"
#else
#if defined(PIC)
        "mov %7, %%"REG_b"    \n\t"
#endif
#endif
        :: "m" (src1), "m" (dst1), "m" (filter), "m" (filterPos),
           "m" (mmxextFilterCode), "m" (src2), "m"(dst2)
#if ARCH_X86_64
          ,"m"(retsave)
#else
#if defined(PIC)
          ,"m" (ebxsave)
#endif
#endif
        : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S, "%"REG_D
#if ARCH_X86_64 || !defined(PIC)
         ,"%"REG_b
#endif
    );

    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--) {
        dst1[i] = src1[srcW-1]*128;
        dst2[i] = src2[srcW-1]*128;
    }
}
#endif //HAVE_INLINE_ASM
