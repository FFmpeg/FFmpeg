/*
 * MMX optimized PNG utils
 * Copyright (c) 2008 Loren Merritt
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
 *
 */

#include "libavutil/cpu.h"
#include "libavutil/x86_cpu.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/png.h"
#include "dsputil_mmx.h"

//#undef NDEBUG
//#include <assert.h>

static void add_bytes_l2_mmx(uint8_t *dst, uint8_t *src1, uint8_t *src2, int w)
{
    x86_reg i=0;
    __asm__ volatile(
        "jmp 2f                         \n\t"
        "1:                             \n\t"
        "movq   (%2, %0), %%mm0         \n\t"
        "movq  8(%2, %0), %%mm1         \n\t"
        "paddb  (%3, %0), %%mm0         \n\t"
        "paddb 8(%3, %0), %%mm1         \n\t"
        "movq %%mm0,  (%1, %0)          \n\t"
        "movq %%mm1, 8(%1, %0)          \n\t"
        "add $16, %0                    \n\t"
        "2:                             \n\t"
        "cmp %4, %0                     \n\t"
        " js 1b                         \n\t"
        : "+r" (i)
        : "r"(dst), "r"(src1), "r"(src2), "r"((x86_reg)w-15)
    );
    for(; i<w; i++)
        dst[i] = src1[i] + src2[i];
}

#define PAETH(cpu, abs3)\
static void add_paeth_prediction_##cpu(uint8_t *dst, uint8_t *src, uint8_t *top, int w, int bpp)\
{\
    x86_reg i, end;\
    if(bpp>4) add_paeth_prediction_##cpu(dst+bpp/2, src+bpp/2, top+bpp/2, w-bpp/2, -bpp);\
    if(bpp<0) bpp=-bpp;\
    i= -bpp;\
    end = w-3;\
    __asm__ volatile(\
        "pxor      %%mm7, %%mm7 \n"\
        "movd    (%1,%0), %%mm0 \n"\
        "movd    (%2,%0), %%mm1 \n"\
        "punpcklbw %%mm7, %%mm0 \n"\
        "punpcklbw %%mm7, %%mm1 \n"\
        "add       %4, %0 \n"\
        "1: \n"\
        "movq      %%mm1, %%mm2 \n"\
        "movd    (%2,%0), %%mm1 \n"\
        "movq      %%mm2, %%mm3 \n"\
        "punpcklbw %%mm7, %%mm1 \n"\
        "movq      %%mm2, %%mm4 \n"\
        "psubw     %%mm1, %%mm3 \n"\
        "psubw     %%mm0, %%mm4 \n"\
        "movq      %%mm3, %%mm5 \n"\
        "paddw     %%mm4, %%mm5 \n"\
        abs3\
        "movq      %%mm4, %%mm6 \n"\
        "pminsw    %%mm5, %%mm6 \n"\
        "pcmpgtw   %%mm6, %%mm3 \n"\
        "pcmpgtw   %%mm5, %%mm4 \n"\
        "movq      %%mm4, %%mm6 \n"\
        "pand      %%mm3, %%mm4 \n"\
        "pandn     %%mm3, %%mm6 \n"\
        "pandn     %%mm0, %%mm3 \n"\
        "movd    (%3,%0), %%mm0 \n"\
        "pand      %%mm1, %%mm6 \n"\
        "pand      %%mm4, %%mm2 \n"\
        "punpcklbw %%mm7, %%mm0 \n"\
        "paddw     %%mm6, %%mm0 \n"\
        "paddw     %%mm2, %%mm3 \n"\
        "paddw     %%mm3, %%mm0 \n"\
        "pand      %6   , %%mm0 \n"\
        "movq      %%mm0, %%mm3 \n"\
        "packuswb  %%mm3, %%mm3 \n"\
        "movd      %%mm3, (%1,%0) \n"\
        "add       %4, %0 \n"\
        "cmp       %5, %0 \n"\
        "jle 1b \n"\
        :"+r"(i)\
        :"r"(dst), "r"(top), "r"(src), "r"((x86_reg)bpp), "g"(end),\
         "m"(ff_pw_255)\
        :"memory"\
    );\
}

#define ABS3_MMX2\
        "psubw     %%mm5, %%mm7 \n"\
        "pmaxsw    %%mm7, %%mm5 \n"\
        "pxor      %%mm6, %%mm6 \n"\
        "pxor      %%mm7, %%mm7 \n"\
        "psubw     %%mm3, %%mm6 \n"\
        "psubw     %%mm4, %%mm7 \n"\
        "pmaxsw    %%mm6, %%mm3 \n"\
        "pmaxsw    %%mm7, %%mm4 \n"\
        "pxor      %%mm7, %%mm7 \n"

#define ABS3_SSSE3\
        "pabsw     %%mm3, %%mm3 \n"\
        "pabsw     %%mm4, %%mm4 \n"\
        "pabsw     %%mm5, %%mm5 \n"

PAETH(mmx2, ABS3_MMX2)
#if HAVE_SSSE3
PAETH(ssse3, ABS3_SSSE3)
#endif

void ff_png_init_mmx(PNGDecContext *s)
{
    int mm_flags = av_get_cpu_flags();

    if (mm_flags & AV_CPU_FLAG_MMX2) {
        s->add_bytes_l2 = add_bytes_l2_mmx;
        s->add_paeth_prediction = add_paeth_prediction_mmx2;
#if HAVE_SSSE3
        if (mm_flags & AV_CPU_FLAG_SSSE3)
            s->add_paeth_prediction = add_paeth_prediction_ssse3;
#endif
    }
}
