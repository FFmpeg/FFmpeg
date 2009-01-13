/*
 * CPU detection code, extracted from mmx.h
 * (c)1997-99 by H. Dietz and R. Fisher
 * Converted to C and improved by Fabrice Bellard.
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

#include <stdlib.h>
#include "libavutil/x86_cpu.h"
#include "libavcodec/dsputil.h"

#undef printf

/* ebx saving is necessary for PIC. gcc seems unable to see it alone */
#define cpuid(index,eax,ebx,ecx,edx)\
    __asm__ volatile\
        ("mov %%"REG_b", %%"REG_S"\n\t"\
         "cpuid\n\t"\
         "xchg %%"REG_b", %%"REG_S\
         : "=a" (eax), "=S" (ebx),\
           "=c" (ecx), "=d" (edx)\
         : "0" (index));

/* Function to test if multimedia instructions are supported...  */
int mm_support(void)
{
    int rval = 0;
    int eax, ebx, ecx, edx;
    int max_std_level, max_ext_level, std_caps=0, ext_caps=0;
    x86_reg a, c;

#if ARCH_X86_64
#define PUSHF "pushfq\n\t"
#define POPF "popfq\n\t"
#else
#define PUSHF "pushfl\n\t"
#define POPF "popfl\n\t"
#endif
    __asm__ volatile (
        /* See if CPUID instruction is supported ... */
        /* ... Get copies of EFLAGS into eax and ecx */
        PUSHF
        "pop %0\n\t"
        "mov %0, %1\n\t"

        /* ... Toggle the ID bit in one copy and store */
        /*     to the EFLAGS reg */
        "xor $0x200000, %0\n\t"
        "push %0\n\t"
        POPF

        /* ... Get the (hopefully modified) EFLAGS */
        PUSHF
        "pop %0\n\t"
        : "=a" (a), "=c" (c)
        :
        : "cc"
        );

    if (a == c)
        return 0; /* CPUID not supported */

    cpuid(0, max_std_level, ebx, ecx, edx);

    if(max_std_level >= 1){
        cpuid(1, eax, ebx, ecx, std_caps);
        if (std_caps & (1<<23))
            rval |= FF_MM_MMX;
        if (std_caps & (1<<25))
            rval |= FF_MM_MMXEXT
#if HAVE_SSE
                  | FF_MM_SSE;
        if (std_caps & (1<<26))
            rval |= FF_MM_SSE2;
        if (ecx & 1)
            rval |= FF_MM_SSE3;
        if (ecx & 0x00000200 )
            rval |= FF_MM_SSSE3
#endif
                  ;
    }

    cpuid(0x80000000, max_ext_level, ebx, ecx, edx);

    if(max_ext_level >= 0x80000001){
        cpuid(0x80000001, eax, ebx, ecx, ext_caps);
        if (ext_caps & (1<<31))
            rval |= FF_MM_3DNOW;
        if (ext_caps & (1<<30))
            rval |= FF_MM_3DNOWEXT;
        if (ext_caps & (1<<23))
            rval |= FF_MM_MMX;
        if (ext_caps & (1<<22))
            rval |= FF_MM_MMXEXT;
    }

#if 0
    av_log(NULL, AV_LOG_DEBUG, "%s%s%s%s%s%s%s%s\n",
        (rval&FF_MM_MMX) ? "MMX ":"",
        (rval&FF_MM_MMXEXT) ? "MMX2 ":"",
        (rval&FF_MM_SSE) ? "SSE ":"",
        (rval&FF_MM_SSE2) ? "SSE2 ":"",
        (rval&FF_MM_SSE3) ? "SSE3 ":"",
        (rval&FF_MM_SSSE3) ? "SSSE3 ":"",
        (rval&FF_MM_3DNOW) ? "3DNow ":"",
        (rval&FF_MM_3DNOWEXT) ? "3DNowExt ":"");
#endif
    return rval;
}

#ifdef TEST
int main ( void )
{
    int mm_flags;
    mm_flags = mm_support();
    printf("mm_support = 0x%08X\n",mm_flags);
    return 0;
}
#endif
