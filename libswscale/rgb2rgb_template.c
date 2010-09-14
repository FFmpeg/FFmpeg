/*
 * software RGB to RGB converter
 * pluralize by software PAL8 to RGB converter
 *              software YUV to YUV converter
 *              software YUV to RGB converter
 * Written by Nick Kurshev.
 * palette & YUV & runtime CPU stuff by Michael (michaelni@gmx.at)
 * lot of big-endian byte order fixes by Alex Beregszaszi
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

#include <stddef.h>

#undef PREFETCH
#undef MOVNTQ
#undef EMMS
#undef SFENCE
#undef MMREG_SIZE
#undef PAVGB

#if COMPILE_TEMPLATE_SSE2
#define MMREG_SIZE 16
#else
#define MMREG_SIZE 8
#endif

#if COMPILE_TEMPLATE_AMD3DNOW
#define PREFETCH  "prefetch"
#define PAVGB     "pavgusb"
#elif COMPILE_TEMPLATE_MMX2
#define PREFETCH "prefetchnta"
#define PAVGB     "pavgb"
#else
#define PREFETCH  " # nop"
#endif

#if COMPILE_TEMPLATE_AMD3DNOW
/* On K6 femms is faster than emms. On K7 femms is directly mapped to emms. */
#define EMMS     "femms"
#else
#define EMMS     "emms"
#endif

#if COMPILE_TEMPLATE_MMX2
#define MOVNTQ "movntq"
#define SFENCE "sfence"
#else
#define MOVNTQ "movq"
#define SFENCE " # nop"
#endif

static inline void RENAME(rgb24tobgr32)(const uint8_t *src, uint8_t *dst, long src_size)
{
    uint8_t *dest = dst;
    const uint8_t *s = src;
    const uint8_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint8_t *mm_end;
#endif
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*s):"memory");
    mm_end = end - 23;
    __asm__ volatile("movq        %0, %%mm7"::"m"(mask32a):"memory");
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movd          %1, %%mm0    \n\t"
            "punpckldq    3%1, %%mm0    \n\t"
            "movd         6%1, %%mm1    \n\t"
            "punpckldq    9%1, %%mm1    \n\t"
            "movd        12%1, %%mm2    \n\t"
            "punpckldq   15%1, %%mm2    \n\t"
            "movd        18%1, %%mm3    \n\t"
            "punpckldq   21%1, %%mm3    \n\t"
            "por        %%mm7, %%mm0    \n\t"
            "por        %%mm7, %%mm1    \n\t"
            "por        %%mm7, %%mm2    \n\t"
            "por        %%mm7, %%mm3    \n\t"
            MOVNTQ"     %%mm0,   %0     \n\t"
            MOVNTQ"     %%mm1,  8%0     \n\t"
            MOVNTQ"     %%mm2, 16%0     \n\t"
            MOVNTQ"     %%mm3, 24%0"
            :"=m"(*dest)
            :"m"(*s)
            :"memory");
        dest += 32;
        s += 24;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
#if HAVE_BIGENDIAN
        /* RGB24 (= R,G,B) -> RGB32 (= A,B,G,R) */
        *dest++ = 255;
        *dest++ = s[2];
        *dest++ = s[1];
        *dest++ = s[0];
        s+=3;
#else
        *dest++ = *s++;
        *dest++ = *s++;
        *dest++ = *s++;
        *dest++ = 255;
#endif
    }
}

#define STORE_BGR24_MMX \
            "psrlq         $8, %%mm2    \n\t" \
            "psrlq         $8, %%mm3    \n\t" \
            "psrlq         $8, %%mm6    \n\t" \
            "psrlq         $8, %%mm7    \n\t" \
            "pand "MANGLE(mask24l)", %%mm0\n\t" \
            "pand "MANGLE(mask24l)", %%mm1\n\t" \
            "pand "MANGLE(mask24l)", %%mm4\n\t" \
            "pand "MANGLE(mask24l)", %%mm5\n\t" \
            "pand "MANGLE(mask24h)", %%mm2\n\t" \
            "pand "MANGLE(mask24h)", %%mm3\n\t" \
            "pand "MANGLE(mask24h)", %%mm6\n\t" \
            "pand "MANGLE(mask24h)", %%mm7\n\t" \
            "por        %%mm2, %%mm0    \n\t" \
            "por        %%mm3, %%mm1    \n\t" \
            "por        %%mm6, %%mm4    \n\t" \
            "por        %%mm7, %%mm5    \n\t" \
 \
            "movq       %%mm1, %%mm2    \n\t" \
            "movq       %%mm4, %%mm3    \n\t" \
            "psllq        $48, %%mm2    \n\t" \
            "psllq        $32, %%mm3    \n\t" \
            "pand "MANGLE(mask24hh)", %%mm2\n\t" \
            "pand "MANGLE(mask24hhh)", %%mm3\n\t" \
            "por        %%mm2, %%mm0    \n\t" \
            "psrlq        $16, %%mm1    \n\t" \
            "psrlq        $32, %%mm4    \n\t" \
            "psllq        $16, %%mm5    \n\t" \
            "por        %%mm3, %%mm1    \n\t" \
            "pand  "MANGLE(mask24hhhh)", %%mm5\n\t" \
            "por        %%mm5, %%mm4    \n\t" \
 \
            MOVNTQ"     %%mm0,   %0     \n\t" \
            MOVNTQ"     %%mm1,  8%0     \n\t" \
            MOVNTQ"     %%mm4, 16%0"


static inline void RENAME(rgb32tobgr24)(const uint8_t *src, uint8_t *dst, long src_size)
{
    uint8_t *dest = dst;
    const uint8_t *s = src;
    const uint8_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint8_t *mm_end;
#endif
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*s):"memory");
    mm_end = end - 31;
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movq          %1, %%mm0    \n\t"
            "movq         8%1, %%mm1    \n\t"
            "movq        16%1, %%mm4    \n\t"
            "movq        24%1, %%mm5    \n\t"
            "movq       %%mm0, %%mm2    \n\t"
            "movq       %%mm1, %%mm3    \n\t"
            "movq       %%mm4, %%mm6    \n\t"
            "movq       %%mm5, %%mm7    \n\t"
            STORE_BGR24_MMX
            :"=m"(*dest)
            :"m"(*s)
            :"memory");
        dest += 24;
        s += 32;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
#if HAVE_BIGENDIAN
        /* RGB32 (= A,B,G,R) -> RGB24 (= R,G,B) */
        s++;
        dest[2] = *s++;
        dest[1] = *s++;
        dest[0] = *s++;
        dest += 3;
#else
        *dest++ = *s++;
        *dest++ = *s++;
        *dest++ = *s++;
        s++;
#endif
    }
}

/*
 original by Strepto/Astral
 ported to gcc & bugfixed: A'rpi
 MMX2, 3DNOW optimization by Nick Kurshev
 32-bit C version, and and&add trick by Michael Niedermayer
*/
static inline void RENAME(rgb15to16)(const uint8_t *src, uint8_t *dst, long src_size)
{
    register const uint8_t* s=src;
    register uint8_t* d=dst;
    register const uint8_t *end;
    const uint8_t *mm_end;
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*s));
    __asm__ volatile("movq        %0, %%mm4"::"m"(mask15s));
    mm_end = end - 15;
    while (s<mm_end) {
        __asm__ volatile(
            PREFETCH"  32%1         \n\t"
            "movq        %1, %%mm0  \n\t"
            "movq       8%1, %%mm2  \n\t"
            "movq     %%mm0, %%mm1  \n\t"
            "movq     %%mm2, %%mm3  \n\t"
            "pand     %%mm4, %%mm0  \n\t"
            "pand     %%mm4, %%mm2  \n\t"
            "paddw    %%mm1, %%mm0  \n\t"
            "paddw    %%mm3, %%mm2  \n\t"
            MOVNTQ"   %%mm0,  %0    \n\t"
            MOVNTQ"   %%mm2, 8%0"
            :"=m"(*d)
            :"m"(*s)
        );
        d+=16;
        s+=16;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    mm_end = end - 3;
    while (s < mm_end) {
        register unsigned x= *((const uint32_t *)s);
        *((uint32_t *)d) = (x&0x7FFF7FFF) + (x&0x7FE07FE0);
        d+=4;
        s+=4;
    }
    if (s < end) {
        register unsigned short x= *((const uint16_t *)s);
        *((uint16_t *)d) = (x&0x7FFF) + (x&0x7FE0);
    }
}

static inline void RENAME(rgb16to15)(const uint8_t *src, uint8_t *dst, long src_size)
{
    register const uint8_t* s=src;
    register uint8_t* d=dst;
    register const uint8_t *end;
    const uint8_t *mm_end;
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*s));
    __asm__ volatile("movq        %0, %%mm7"::"m"(mask15rg));
    __asm__ volatile("movq        %0, %%mm6"::"m"(mask15b));
    mm_end = end - 15;
    while (s<mm_end) {
        __asm__ volatile(
            PREFETCH"  32%1         \n\t"
            "movq        %1, %%mm0  \n\t"
            "movq       8%1, %%mm2  \n\t"
            "movq     %%mm0, %%mm1  \n\t"
            "movq     %%mm2, %%mm3  \n\t"
            "psrlq       $1, %%mm0  \n\t"
            "psrlq       $1, %%mm2  \n\t"
            "pand     %%mm7, %%mm0  \n\t"
            "pand     %%mm7, %%mm2  \n\t"
            "pand     %%mm6, %%mm1  \n\t"
            "pand     %%mm6, %%mm3  \n\t"
            "por      %%mm1, %%mm0  \n\t"
            "por      %%mm3, %%mm2  \n\t"
            MOVNTQ"   %%mm0,  %0    \n\t"
            MOVNTQ"   %%mm2, 8%0"
            :"=m"(*d)
            :"m"(*s)
        );
        d+=16;
        s+=16;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    mm_end = end - 3;
    while (s < mm_end) {
        register uint32_t x= *((const uint32_t*)s);
        *((uint32_t *)d) = ((x>>1)&0x7FE07FE0) | (x&0x001F001F);
        s+=4;
        d+=4;
    }
    if (s < end) {
        register uint16_t x= *((const uint16_t*)s);
        *((uint16_t *)d) = ((x>>1)&0x7FE0) | (x&0x001F);
    }
}

static inline void RENAME(rgb32to16)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint8_t *mm_end;
#endif
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    mm_end = end - 15;
#if 1 //is faster only if multiplies are reasonably fast (FIXME figure out on which CPUs this is faster, on Athlon it is slightly faster)
    __asm__ volatile(
        "movq           %3, %%mm5   \n\t"
        "movq           %4, %%mm6   \n\t"
        "movq           %5, %%mm7   \n\t"
        "jmp 2f                     \n\t"
        ASMALIGN(4)
        "1:                         \n\t"
        PREFETCH"   32(%1)          \n\t"
        "movd         (%1), %%mm0   \n\t"
        "movd        4(%1), %%mm3   \n\t"
        "punpckldq   8(%1), %%mm0   \n\t"
        "punpckldq  12(%1), %%mm3   \n\t"
        "movq        %%mm0, %%mm1   \n\t"
        "movq        %%mm3, %%mm4   \n\t"
        "pand        %%mm6, %%mm0   \n\t"
        "pand        %%mm6, %%mm3   \n\t"
        "pmaddwd     %%mm7, %%mm0   \n\t"
        "pmaddwd     %%mm7, %%mm3   \n\t"
        "pand        %%mm5, %%mm1   \n\t"
        "pand        %%mm5, %%mm4   \n\t"
        "por         %%mm1, %%mm0   \n\t"
        "por         %%mm4, %%mm3   \n\t"
        "psrld          $5, %%mm0   \n\t"
        "pslld         $11, %%mm3   \n\t"
        "por         %%mm3, %%mm0   \n\t"
        MOVNTQ"      %%mm0, (%0)    \n\t"
        "add           $16,  %1     \n\t"
        "add            $8,  %0     \n\t"
        "2:                         \n\t"
        "cmp            %2,  %1     \n\t"
        " jb            1b          \n\t"
        : "+r" (d), "+r"(s)
        : "r" (mm_end), "m" (mask3216g), "m" (mask3216br), "m" (mul3216)
    );
#else
    __asm__ volatile(PREFETCH"    %0"::"m"(*src):"memory");
    __asm__ volatile(
        "movq    %0, %%mm7    \n\t"
        "movq    %1, %%mm6    \n\t"
        ::"m"(red_16mask),"m"(green_16mask));
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movd          %1, %%mm0    \n\t"
            "movd         4%1, %%mm3    \n\t"
            "punpckldq    8%1, %%mm0    \n\t"
            "punpckldq   12%1, %%mm3    \n\t"
            "movq       %%mm0, %%mm1    \n\t"
            "movq       %%mm0, %%mm2    \n\t"
            "movq       %%mm3, %%mm4    \n\t"
            "movq       %%mm3, %%mm5    \n\t"
            "psrlq         $3, %%mm0    \n\t"
            "psrlq         $3, %%mm3    \n\t"
            "pand          %2, %%mm0    \n\t"
            "pand          %2, %%mm3    \n\t"
            "psrlq         $5, %%mm1    \n\t"
            "psrlq         $5, %%mm4    \n\t"
            "pand       %%mm6, %%mm1    \n\t"
            "pand       %%mm6, %%mm4    \n\t"
            "psrlq         $8, %%mm2    \n\t"
            "psrlq         $8, %%mm5    \n\t"
            "pand       %%mm7, %%mm2    \n\t"
            "pand       %%mm7, %%mm5    \n\t"
            "por        %%mm1, %%mm0    \n\t"
            "por        %%mm4, %%mm3    \n\t"
            "por        %%mm2, %%mm0    \n\t"
            "por        %%mm5, %%mm3    \n\t"
            "psllq        $16, %%mm3    \n\t"
            "por        %%mm3, %%mm0    \n\t"
            MOVNTQ"     %%mm0, %0       \n\t"
            :"=m"(*d):"m"(*s),"m"(blue_16mask):"memory");
        d += 4;
        s += 16;
    }
#endif
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        register int rgb = *(const uint32_t*)s; s += 4;
        *d++ = ((rgb&0xFF)>>3) + ((rgb&0xFC00)>>5) + ((rgb&0xF80000)>>8);
    }
}

static inline void RENAME(rgb32tobgr16)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint8_t *mm_end;
#endif
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*src):"memory");
    __asm__ volatile(
        "movq          %0, %%mm7    \n\t"
        "movq          %1, %%mm6    \n\t"
        ::"m"(red_16mask),"m"(green_16mask));
    mm_end = end - 15;
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movd          %1, %%mm0    \n\t"
            "movd         4%1, %%mm3    \n\t"
            "punpckldq    8%1, %%mm0    \n\t"
            "punpckldq   12%1, %%mm3    \n\t"
            "movq       %%mm0, %%mm1    \n\t"
            "movq       %%mm0, %%mm2    \n\t"
            "movq       %%mm3, %%mm4    \n\t"
            "movq       %%mm3, %%mm5    \n\t"
            "psllq         $8, %%mm0    \n\t"
            "psllq         $8, %%mm3    \n\t"
            "pand       %%mm7, %%mm0    \n\t"
            "pand       %%mm7, %%mm3    \n\t"
            "psrlq         $5, %%mm1    \n\t"
            "psrlq         $5, %%mm4    \n\t"
            "pand       %%mm6, %%mm1    \n\t"
            "pand       %%mm6, %%mm4    \n\t"
            "psrlq        $19, %%mm2    \n\t"
            "psrlq        $19, %%mm5    \n\t"
            "pand          %2, %%mm2    \n\t"
            "pand          %2, %%mm5    \n\t"
            "por        %%mm1, %%mm0    \n\t"
            "por        %%mm4, %%mm3    \n\t"
            "por        %%mm2, %%mm0    \n\t"
            "por        %%mm5, %%mm3    \n\t"
            "psllq        $16, %%mm3    \n\t"
            "por        %%mm3, %%mm0    \n\t"
            MOVNTQ"     %%mm0, %0       \n\t"
            :"=m"(*d):"m"(*s),"m"(blue_16mask):"memory");
        d += 4;
        s += 16;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        register int rgb = *(const uint32_t*)s; s += 4;
        *d++ = ((rgb&0xF8)<<8) + ((rgb&0xFC00)>>5) + ((rgb&0xF80000)>>19);
    }
}

static inline void RENAME(rgb32to15)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint8_t *mm_end;
#endif
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    mm_end = end - 15;
#if 1 //is faster only if multiplies are reasonably fast (FIXME figure out on which CPUs this is faster, on Athlon it is slightly faster)
    __asm__ volatile(
        "movq           %3, %%mm5   \n\t"
        "movq           %4, %%mm6   \n\t"
        "movq           %5, %%mm7   \n\t"
        "jmp            2f          \n\t"
        ASMALIGN(4)
        "1:                         \n\t"
        PREFETCH"   32(%1)          \n\t"
        "movd         (%1), %%mm0   \n\t"
        "movd        4(%1), %%mm3   \n\t"
        "punpckldq   8(%1), %%mm0   \n\t"
        "punpckldq  12(%1), %%mm3   \n\t"
        "movq        %%mm0, %%mm1   \n\t"
        "movq        %%mm3, %%mm4   \n\t"
        "pand        %%mm6, %%mm0   \n\t"
        "pand        %%mm6, %%mm3   \n\t"
        "pmaddwd     %%mm7, %%mm0   \n\t"
        "pmaddwd     %%mm7, %%mm3   \n\t"
        "pand        %%mm5, %%mm1   \n\t"
        "pand        %%mm5, %%mm4   \n\t"
        "por         %%mm1, %%mm0   \n\t"
        "por         %%mm4, %%mm3   \n\t"
        "psrld          $6, %%mm0   \n\t"
        "pslld         $10, %%mm3   \n\t"
        "por         %%mm3, %%mm0   \n\t"
        MOVNTQ"      %%mm0, (%0)    \n\t"
        "add           $16,  %1     \n\t"
        "add            $8,  %0     \n\t"
        "2:                         \n\t"
        "cmp            %2,  %1     \n\t"
        " jb            1b          \n\t"
        : "+r" (d), "+r"(s)
        : "r" (mm_end), "m" (mask3215g), "m" (mask3216br), "m" (mul3215)
    );
#else
    __asm__ volatile(PREFETCH"    %0"::"m"(*src):"memory");
    __asm__ volatile(
        "movq          %0, %%mm7    \n\t"
        "movq          %1, %%mm6    \n\t"
        ::"m"(red_15mask),"m"(green_15mask));
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movd          %1, %%mm0    \n\t"
            "movd         4%1, %%mm3    \n\t"
            "punpckldq    8%1, %%mm0    \n\t"
            "punpckldq   12%1, %%mm3    \n\t"
            "movq       %%mm0, %%mm1    \n\t"
            "movq       %%mm0, %%mm2    \n\t"
            "movq       %%mm3, %%mm4    \n\t"
            "movq       %%mm3, %%mm5    \n\t"
            "psrlq         $3, %%mm0    \n\t"
            "psrlq         $3, %%mm3    \n\t"
            "pand          %2, %%mm0    \n\t"
            "pand          %2, %%mm3    \n\t"
            "psrlq         $6, %%mm1    \n\t"
            "psrlq         $6, %%mm4    \n\t"
            "pand       %%mm6, %%mm1    \n\t"
            "pand       %%mm6, %%mm4    \n\t"
            "psrlq         $9, %%mm2    \n\t"
            "psrlq         $9, %%mm5    \n\t"
            "pand       %%mm7, %%mm2    \n\t"
            "pand       %%mm7, %%mm5    \n\t"
            "por        %%mm1, %%mm0    \n\t"
            "por        %%mm4, %%mm3    \n\t"
            "por        %%mm2, %%mm0    \n\t"
            "por        %%mm5, %%mm3    \n\t"
            "psllq        $16, %%mm3    \n\t"
            "por        %%mm3, %%mm0    \n\t"
            MOVNTQ"     %%mm0, %0       \n\t"
            :"=m"(*d):"m"(*s),"m"(blue_15mask):"memory");
        d += 4;
        s += 16;
    }
#endif
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        register int rgb = *(const uint32_t*)s; s += 4;
        *d++ = ((rgb&0xFF)>>3) + ((rgb&0xF800)>>6) + ((rgb&0xF80000)>>9);
    }
}

static inline void RENAME(rgb32tobgr15)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint8_t *mm_end;
#endif
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*src):"memory");
    __asm__ volatile(
        "movq          %0, %%mm7    \n\t"
        "movq          %1, %%mm6    \n\t"
        ::"m"(red_15mask),"m"(green_15mask));
    mm_end = end - 15;
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movd          %1, %%mm0    \n\t"
            "movd         4%1, %%mm3    \n\t"
            "punpckldq    8%1, %%mm0    \n\t"
            "punpckldq   12%1, %%mm3    \n\t"
            "movq       %%mm0, %%mm1    \n\t"
            "movq       %%mm0, %%mm2    \n\t"
            "movq       %%mm3, %%mm4    \n\t"
            "movq       %%mm3, %%mm5    \n\t"
            "psllq         $7, %%mm0    \n\t"
            "psllq         $7, %%mm3    \n\t"
            "pand       %%mm7, %%mm0    \n\t"
            "pand       %%mm7, %%mm3    \n\t"
            "psrlq         $6, %%mm1    \n\t"
            "psrlq         $6, %%mm4    \n\t"
            "pand       %%mm6, %%mm1    \n\t"
            "pand       %%mm6, %%mm4    \n\t"
            "psrlq        $19, %%mm2    \n\t"
            "psrlq        $19, %%mm5    \n\t"
            "pand          %2, %%mm2    \n\t"
            "pand          %2, %%mm5    \n\t"
            "por        %%mm1, %%mm0    \n\t"
            "por        %%mm4, %%mm3    \n\t"
            "por        %%mm2, %%mm0    \n\t"
            "por        %%mm5, %%mm3    \n\t"
            "psllq        $16, %%mm3    \n\t"
            "por        %%mm3, %%mm0    \n\t"
            MOVNTQ"     %%mm0, %0       \n\t"
            :"=m"(*d):"m"(*s),"m"(blue_15mask):"memory");
        d += 4;
        s += 16;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        register int rgb = *(const uint32_t*)s; s += 4;
        *d++ = ((rgb&0xF8)<<7) + ((rgb&0xF800)>>6) + ((rgb&0xF80000)>>19);
    }
}

static inline void RENAME(rgb24tobgr16)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint8_t *mm_end;
#endif
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*src):"memory");
    __asm__ volatile(
        "movq         %0, %%mm7     \n\t"
        "movq         %1, %%mm6     \n\t"
        ::"m"(red_16mask),"m"(green_16mask));
    mm_end = end - 11;
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movd          %1, %%mm0    \n\t"
            "movd         3%1, %%mm3    \n\t"
            "punpckldq    6%1, %%mm0    \n\t"
            "punpckldq    9%1, %%mm3    \n\t"
            "movq       %%mm0, %%mm1    \n\t"
            "movq       %%mm0, %%mm2    \n\t"
            "movq       %%mm3, %%mm4    \n\t"
            "movq       %%mm3, %%mm5    \n\t"
            "psrlq         $3, %%mm0    \n\t"
            "psrlq         $3, %%mm3    \n\t"
            "pand          %2, %%mm0    \n\t"
            "pand          %2, %%mm3    \n\t"
            "psrlq         $5, %%mm1    \n\t"
            "psrlq         $5, %%mm4    \n\t"
            "pand       %%mm6, %%mm1    \n\t"
            "pand       %%mm6, %%mm4    \n\t"
            "psrlq         $8, %%mm2    \n\t"
            "psrlq         $8, %%mm5    \n\t"
            "pand       %%mm7, %%mm2    \n\t"
            "pand       %%mm7, %%mm5    \n\t"
            "por        %%mm1, %%mm0    \n\t"
            "por        %%mm4, %%mm3    \n\t"
            "por        %%mm2, %%mm0    \n\t"
            "por        %%mm5, %%mm3    \n\t"
            "psllq        $16, %%mm3    \n\t"
            "por        %%mm3, %%mm0    \n\t"
            MOVNTQ"     %%mm0, %0       \n\t"
            :"=m"(*d):"m"(*s),"m"(blue_16mask):"memory");
        d += 4;
        s += 12;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        const int b = *s++;
        const int g = *s++;
        const int r = *s++;
        *d++ = (b>>3) | ((g&0xFC)<<3) | ((r&0xF8)<<8);
    }
}

static inline void RENAME(rgb24to16)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint8_t *mm_end;
#endif
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*src):"memory");
    __asm__ volatile(
        "movq         %0, %%mm7     \n\t"
        "movq         %1, %%mm6     \n\t"
        ::"m"(red_16mask),"m"(green_16mask));
    mm_end = end - 15;
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movd          %1, %%mm0    \n\t"
            "movd         3%1, %%mm3    \n\t"
            "punpckldq    6%1, %%mm0    \n\t"
            "punpckldq    9%1, %%mm3    \n\t"
            "movq       %%mm0, %%mm1    \n\t"
            "movq       %%mm0, %%mm2    \n\t"
            "movq       %%mm3, %%mm4    \n\t"
            "movq       %%mm3, %%mm5    \n\t"
            "psllq         $8, %%mm0    \n\t"
            "psllq         $8, %%mm3    \n\t"
            "pand       %%mm7, %%mm0    \n\t"
            "pand       %%mm7, %%mm3    \n\t"
            "psrlq         $5, %%mm1    \n\t"
            "psrlq         $5, %%mm4    \n\t"
            "pand       %%mm6, %%mm1    \n\t"
            "pand       %%mm6, %%mm4    \n\t"
            "psrlq        $19, %%mm2    \n\t"
            "psrlq        $19, %%mm5    \n\t"
            "pand          %2, %%mm2    \n\t"
            "pand          %2, %%mm5    \n\t"
            "por        %%mm1, %%mm0    \n\t"
            "por        %%mm4, %%mm3    \n\t"
            "por        %%mm2, %%mm0    \n\t"
            "por        %%mm5, %%mm3    \n\t"
            "psllq        $16, %%mm3    \n\t"
            "por        %%mm3, %%mm0    \n\t"
            MOVNTQ"     %%mm0, %0       \n\t"
            :"=m"(*d):"m"(*s),"m"(blue_16mask):"memory");
        d += 4;
        s += 12;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        const int r = *s++;
        const int g = *s++;
        const int b = *s++;
        *d++ = (b>>3) | ((g&0xFC)<<3) | ((r&0xF8)<<8);
    }
}

static inline void RENAME(rgb24tobgr15)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint8_t *mm_end;
#endif
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*src):"memory");
    __asm__ volatile(
        "movq          %0, %%mm7    \n\t"
        "movq          %1, %%mm6    \n\t"
        ::"m"(red_15mask),"m"(green_15mask));
    mm_end = end - 11;
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movd          %1, %%mm0    \n\t"
            "movd         3%1, %%mm3    \n\t"
            "punpckldq    6%1, %%mm0    \n\t"
            "punpckldq    9%1, %%mm3    \n\t"
            "movq       %%mm0, %%mm1    \n\t"
            "movq       %%mm0, %%mm2    \n\t"
            "movq       %%mm3, %%mm4    \n\t"
            "movq       %%mm3, %%mm5    \n\t"
            "psrlq         $3, %%mm0    \n\t"
            "psrlq         $3, %%mm3    \n\t"
            "pand          %2, %%mm0    \n\t"
            "pand          %2, %%mm3    \n\t"
            "psrlq         $6, %%mm1    \n\t"
            "psrlq         $6, %%mm4    \n\t"
            "pand       %%mm6, %%mm1    \n\t"
            "pand       %%mm6, %%mm4    \n\t"
            "psrlq         $9, %%mm2    \n\t"
            "psrlq         $9, %%mm5    \n\t"
            "pand       %%mm7, %%mm2    \n\t"
            "pand       %%mm7, %%mm5    \n\t"
            "por        %%mm1, %%mm0    \n\t"
            "por        %%mm4, %%mm3    \n\t"
            "por        %%mm2, %%mm0    \n\t"
            "por        %%mm5, %%mm3    \n\t"
            "psllq        $16, %%mm3    \n\t"
            "por        %%mm3, %%mm0    \n\t"
            MOVNTQ"     %%mm0, %0       \n\t"
            :"=m"(*d):"m"(*s),"m"(blue_15mask):"memory");
        d += 4;
        s += 12;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        const int b = *s++;
        const int g = *s++;
        const int r = *s++;
        *d++ = (b>>3) | ((g&0xF8)<<2) | ((r&0xF8)<<7);
    }
}

static inline void RENAME(rgb24to15)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint8_t *s = src;
    const uint8_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint8_t *mm_end;
#endif
    uint16_t *d = (uint16_t *)dst;
    end = s + src_size;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*src):"memory");
    __asm__ volatile(
        "movq         %0, %%mm7     \n\t"
        "movq         %1, %%mm6     \n\t"
        ::"m"(red_15mask),"m"(green_15mask));
    mm_end = end - 15;
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"   32%1            \n\t"
            "movd         %1, %%mm0     \n\t"
            "movd        3%1, %%mm3     \n\t"
            "punpckldq   6%1, %%mm0     \n\t"
            "punpckldq   9%1, %%mm3     \n\t"
            "movq      %%mm0, %%mm1     \n\t"
            "movq      %%mm0, %%mm2     \n\t"
            "movq      %%mm3, %%mm4     \n\t"
            "movq      %%mm3, %%mm5     \n\t"
            "psllq        $7, %%mm0     \n\t"
            "psllq        $7, %%mm3     \n\t"
            "pand      %%mm7, %%mm0     \n\t"
            "pand      %%mm7, %%mm3     \n\t"
            "psrlq        $6, %%mm1     \n\t"
            "psrlq        $6, %%mm4     \n\t"
            "pand      %%mm6, %%mm1     \n\t"
            "pand      %%mm6, %%mm4     \n\t"
            "psrlq       $19, %%mm2     \n\t"
            "psrlq       $19, %%mm5     \n\t"
            "pand         %2, %%mm2     \n\t"
            "pand         %2, %%mm5     \n\t"
            "por       %%mm1, %%mm0     \n\t"
            "por       %%mm4, %%mm3     \n\t"
            "por       %%mm2, %%mm0     \n\t"
            "por       %%mm5, %%mm3     \n\t"
            "psllq       $16, %%mm3     \n\t"
            "por       %%mm3, %%mm0     \n\t"
            MOVNTQ"    %%mm0, %0        \n\t"
            :"=m"(*d):"m"(*s),"m"(blue_15mask):"memory");
        d += 4;
        s += 12;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        const int r = *s++;
        const int g = *s++;
        const int b = *s++;
        *d++ = (b>>3) | ((g&0xF8)<<2) | ((r&0xF8)<<7);
    }
}

/*
  I use less accurate approximation here by simply left-shifting the input
  value and filling the low order bits with zeroes. This method improves PNG
  compression but this scheme cannot reproduce white exactly, since it does
  not generate an all-ones maximum value; the net effect is to darken the
  image slightly.

  The better method should be "left bit replication":

   4 3 2 1 0
   ---------
   1 1 0 1 1

   7 6 5 4 3  2 1 0
   ----------------
   1 1 0 1 1  1 1 0
   |=======|  |===|
       |      leftmost bits repeated to fill open bits
       |
   original bits
*/
static inline void RENAME(rgb15tobgr24)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint16_t *mm_end;
#endif
    uint8_t *d = dst;
    const uint16_t *s = (const uint16_t*)src;
    end = s + src_size/2;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*s):"memory");
    mm_end = end - 7;
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movq          %1, %%mm0    \n\t"
            "movq          %1, %%mm1    \n\t"
            "movq          %1, %%mm2    \n\t"
            "pand          %2, %%mm0    \n\t"
            "pand          %3, %%mm1    \n\t"
            "pand          %4, %%mm2    \n\t"
            "psllq         $3, %%mm0    \n\t"
            "psrlq         $2, %%mm1    \n\t"
            "psrlq         $7, %%mm2    \n\t"
            "movq       %%mm0, %%mm3    \n\t"
            "movq       %%mm1, %%mm4    \n\t"
            "movq       %%mm2, %%mm5    \n\t"
            "punpcklwd     %5, %%mm0    \n\t"
            "punpcklwd     %5, %%mm1    \n\t"
            "punpcklwd     %5, %%mm2    \n\t"
            "punpckhwd     %5, %%mm3    \n\t"
            "punpckhwd     %5, %%mm4    \n\t"
            "punpckhwd     %5, %%mm5    \n\t"
            "psllq         $8, %%mm1    \n\t"
            "psllq        $16, %%mm2    \n\t"
            "por        %%mm1, %%mm0    \n\t"
            "por        %%mm2, %%mm0    \n\t"
            "psllq         $8, %%mm4    \n\t"
            "psllq        $16, %%mm5    \n\t"
            "por        %%mm4, %%mm3    \n\t"
            "por        %%mm5, %%mm3    \n\t"

            "movq       %%mm0, %%mm6    \n\t"
            "movq       %%mm3, %%mm7    \n\t"

            "movq         8%1, %%mm0    \n\t"
            "movq         8%1, %%mm1    \n\t"
            "movq         8%1, %%mm2    \n\t"
            "pand          %2, %%mm0    \n\t"
            "pand          %3, %%mm1    \n\t"
            "pand          %4, %%mm2    \n\t"
            "psllq         $3, %%mm0    \n\t"
            "psrlq         $2, %%mm1    \n\t"
            "psrlq         $7, %%mm2    \n\t"
            "movq       %%mm0, %%mm3    \n\t"
            "movq       %%mm1, %%mm4    \n\t"
            "movq       %%mm2, %%mm5    \n\t"
            "punpcklwd     %5, %%mm0    \n\t"
            "punpcklwd     %5, %%mm1    \n\t"
            "punpcklwd     %5, %%mm2    \n\t"
            "punpckhwd     %5, %%mm3    \n\t"
            "punpckhwd     %5, %%mm4    \n\t"
            "punpckhwd     %5, %%mm5    \n\t"
            "psllq         $8, %%mm1    \n\t"
            "psllq        $16, %%mm2    \n\t"
            "por        %%mm1, %%mm0    \n\t"
            "por        %%mm2, %%mm0    \n\t"
            "psllq         $8, %%mm4    \n\t"
            "psllq        $16, %%mm5    \n\t"
            "por        %%mm4, %%mm3    \n\t"
            "por        %%mm5, %%mm3    \n\t"

            :"=m"(*d)
            :"m"(*s),"m"(mask15b),"m"(mask15g),"m"(mask15r), "m"(mmx_null)
            :"memory");
        /* borrowed 32 to 24 */
        __asm__ volatile(
            "movq       %%mm0, %%mm4    \n\t"
            "movq       %%mm3, %%mm5    \n\t"
            "movq       %%mm6, %%mm0    \n\t"
            "movq       %%mm7, %%mm1    \n\t"

            "movq       %%mm4, %%mm6    \n\t"
            "movq       %%mm5, %%mm7    \n\t"
            "movq       %%mm0, %%mm2    \n\t"
            "movq       %%mm1, %%mm3    \n\t"

            STORE_BGR24_MMX

            :"=m"(*d)
            :"m"(*s)
            :"memory");
        d += 24;
        s += 8;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        register uint16_t bgr;
        bgr = *s++;
        *d++ = (bgr&0x1F)<<3;
        *d++ = (bgr&0x3E0)>>2;
        *d++ = (bgr&0x7C00)>>7;
    }
}

static inline void RENAME(rgb16tobgr24)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint16_t *mm_end;
#endif
    uint8_t *d = (uint8_t *)dst;
    const uint16_t *s = (const uint16_t *)src;
    end = s + src_size/2;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*s):"memory");
    mm_end = end - 7;
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movq          %1, %%mm0    \n\t"
            "movq          %1, %%mm1    \n\t"
            "movq          %1, %%mm2    \n\t"
            "pand          %2, %%mm0    \n\t"
            "pand          %3, %%mm1    \n\t"
            "pand          %4, %%mm2    \n\t"
            "psllq         $3, %%mm0    \n\t"
            "psrlq         $3, %%mm1    \n\t"
            "psrlq         $8, %%mm2    \n\t"
            "movq       %%mm0, %%mm3    \n\t"
            "movq       %%mm1, %%mm4    \n\t"
            "movq       %%mm2, %%mm5    \n\t"
            "punpcklwd     %5, %%mm0    \n\t"
            "punpcklwd     %5, %%mm1    \n\t"
            "punpcklwd     %5, %%mm2    \n\t"
            "punpckhwd     %5, %%mm3    \n\t"
            "punpckhwd     %5, %%mm4    \n\t"
            "punpckhwd     %5, %%mm5    \n\t"
            "psllq         $8, %%mm1    \n\t"
            "psllq        $16, %%mm2    \n\t"
            "por        %%mm1, %%mm0    \n\t"
            "por        %%mm2, %%mm0    \n\t"
            "psllq         $8, %%mm4    \n\t"
            "psllq        $16, %%mm5    \n\t"
            "por        %%mm4, %%mm3    \n\t"
            "por        %%mm5, %%mm3    \n\t"

            "movq       %%mm0, %%mm6    \n\t"
            "movq       %%mm3, %%mm7    \n\t"

            "movq         8%1, %%mm0    \n\t"
            "movq         8%1, %%mm1    \n\t"
            "movq         8%1, %%mm2    \n\t"
            "pand          %2, %%mm0    \n\t"
            "pand          %3, %%mm1    \n\t"
            "pand          %4, %%mm2    \n\t"
            "psllq         $3, %%mm0    \n\t"
            "psrlq         $3, %%mm1    \n\t"
            "psrlq         $8, %%mm2    \n\t"
            "movq       %%mm0, %%mm3    \n\t"
            "movq       %%mm1, %%mm4    \n\t"
            "movq       %%mm2, %%mm5    \n\t"
            "punpcklwd     %5, %%mm0    \n\t"
            "punpcklwd     %5, %%mm1    \n\t"
            "punpcklwd     %5, %%mm2    \n\t"
            "punpckhwd     %5, %%mm3    \n\t"
            "punpckhwd     %5, %%mm4    \n\t"
            "punpckhwd     %5, %%mm5    \n\t"
            "psllq         $8, %%mm1    \n\t"
            "psllq        $16, %%mm2    \n\t"
            "por        %%mm1, %%mm0    \n\t"
            "por        %%mm2, %%mm0    \n\t"
            "psllq         $8, %%mm4    \n\t"
            "psllq        $16, %%mm5    \n\t"
            "por        %%mm4, %%mm3    \n\t"
            "por        %%mm5, %%mm3    \n\t"
            :"=m"(*d)
            :"m"(*s),"m"(mask16b),"m"(mask16g),"m"(mask16r),"m"(mmx_null)
            :"memory");
        /* borrowed 32 to 24 */
        __asm__ volatile(
            "movq       %%mm0, %%mm4    \n\t"
            "movq       %%mm3, %%mm5    \n\t"
            "movq       %%mm6, %%mm0    \n\t"
            "movq       %%mm7, %%mm1    \n\t"

            "movq       %%mm4, %%mm6    \n\t"
            "movq       %%mm5, %%mm7    \n\t"
            "movq       %%mm0, %%mm2    \n\t"
            "movq       %%mm1, %%mm3    \n\t"

            STORE_BGR24_MMX

            :"=m"(*d)
            :"m"(*s)
            :"memory");
        d += 24;
        s += 8;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        register uint16_t bgr;
        bgr = *s++;
        *d++ = (bgr&0x1F)<<3;
        *d++ = (bgr&0x7E0)>>3;
        *d++ = (bgr&0xF800)>>8;
    }
}

/*
 * mm0 = 00 B3 00 B2 00 B1 00 B0
 * mm1 = 00 G3 00 G2 00 G1 00 G0
 * mm2 = 00 R3 00 R2 00 R1 00 R0
 * mm6 = FF FF FF FF FF FF FF FF
 * mm7 = 00 00 00 00 00 00 00 00
 */
#define PACK_RGB32 \
    "packuswb   %%mm7, %%mm0    \n\t" /* 00 00 00 00 B3 B2 B1 B0 */ \
    "packuswb   %%mm7, %%mm1    \n\t" /* 00 00 00 00 G3 G2 G1 G0 */ \
    "packuswb   %%mm7, %%mm2    \n\t" /* 00 00 00 00 R3 R2 R1 R0 */ \
    "punpcklbw  %%mm1, %%mm0    \n\t" /* G3 B3 G2 B2 G1 B1 G0 B0 */ \
    "punpcklbw  %%mm6, %%mm2    \n\t" /* FF R3 FF R2 FF R1 FF R0 */ \
    "movq       %%mm0, %%mm3    \n\t"                               \
    "punpcklwd  %%mm2, %%mm0    \n\t" /* FF R1 G1 B1 FF R0 G0 B0 */ \
    "punpckhwd  %%mm2, %%mm3    \n\t" /* FF R3 G3 B3 FF R2 G2 B2 */ \
    MOVNTQ"     %%mm0,  %0      \n\t"                               \
    MOVNTQ"     %%mm3, 8%0      \n\t"                               \

static inline void RENAME(rgb15to32)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint16_t *mm_end;
#endif
    uint8_t *d = dst;
    const uint16_t *s = (const uint16_t *)src;
    end = s + src_size/2;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*s):"memory");
    __asm__ volatile("pxor    %%mm7,%%mm7    \n\t":::"memory");
    __asm__ volatile("pcmpeqd %%mm6,%%mm6    \n\t":::"memory");
    mm_end = end - 3;
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movq          %1, %%mm0    \n\t"
            "movq          %1, %%mm1    \n\t"
            "movq          %1, %%mm2    \n\t"
            "pand          %2, %%mm0    \n\t"
            "pand          %3, %%mm1    \n\t"
            "pand          %4, %%mm2    \n\t"
            "psllq         $3, %%mm0    \n\t"
            "psrlq         $2, %%mm1    \n\t"
            "psrlq         $7, %%mm2    \n\t"
            PACK_RGB32
            :"=m"(*d)
            :"m"(*s),"m"(mask15b),"m"(mask15g),"m"(mask15r)
            :"memory");
        d += 16;
        s += 4;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        register uint16_t bgr;
        bgr = *s++;
#if HAVE_BIGENDIAN
        *d++ = 255;
        *d++ = (bgr&0x7C00)>>7;
        *d++ = (bgr&0x3E0)>>2;
        *d++ = (bgr&0x1F)<<3;
#else
        *d++ = (bgr&0x1F)<<3;
        *d++ = (bgr&0x3E0)>>2;
        *d++ = (bgr&0x7C00)>>7;
        *d++ = 255;
#endif
    }
}

static inline void RENAME(rgb16to32)(const uint8_t *src, uint8_t *dst, long src_size)
{
    const uint16_t *end;
#if COMPILE_TEMPLATE_MMX
    const uint16_t *mm_end;
#endif
    uint8_t *d = dst;
    const uint16_t *s = (const uint16_t*)src;
    end = s + src_size/2;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(PREFETCH"    %0"::"m"(*s):"memory");
    __asm__ volatile("pxor    %%mm7,%%mm7    \n\t":::"memory");
    __asm__ volatile("pcmpeqd %%mm6,%%mm6    \n\t":::"memory");
    mm_end = end - 3;
    while (s < mm_end) {
        __asm__ volatile(
            PREFETCH"    32%1           \n\t"
            "movq          %1, %%mm0    \n\t"
            "movq          %1, %%mm1    \n\t"
            "movq          %1, %%mm2    \n\t"
            "pand          %2, %%mm0    \n\t"
            "pand          %3, %%mm1    \n\t"
            "pand          %4, %%mm2    \n\t"
            "psllq         $3, %%mm0    \n\t"
            "psrlq         $3, %%mm1    \n\t"
            "psrlq         $8, %%mm2    \n\t"
            PACK_RGB32
            :"=m"(*d)
            :"m"(*s),"m"(mask16b),"m"(mask16g),"m"(mask16r)
            :"memory");
        d += 16;
        s += 4;
    }
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    while (s < end) {
        register uint16_t bgr;
        bgr = *s++;
#if HAVE_BIGENDIAN
        *d++ = 255;
        *d++ = (bgr&0xF800)>>8;
        *d++ = (bgr&0x7E0)>>3;
        *d++ = (bgr&0x1F)<<3;
#else
        *d++ = (bgr&0x1F)<<3;
        *d++ = (bgr&0x7E0)>>3;
        *d++ = (bgr&0xF800)>>8;
        *d++ = 255;
#endif
    }
}

static inline void RENAME(shuffle_bytes_2103)(const uint8_t *src, uint8_t *dst, long src_size)
{
    x86_reg idx = 15 - src_size;
    const uint8_t *s = src-idx;
    uint8_t *d = dst-idx;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(
        "test          %0, %0           \n\t"
        "jns           2f               \n\t"
        PREFETCH"       (%1, %0)        \n\t"
        "movq          %3, %%mm7        \n\t"
        "pxor          %4, %%mm7        \n\t"
        "movq       %%mm7, %%mm6        \n\t"
        "pxor          %5, %%mm7        \n\t"
        ASMALIGN(4)
        "1:                             \n\t"
        PREFETCH"     32(%1, %0)        \n\t"
        "movq           (%1, %0), %%mm0 \n\t"
        "movq          8(%1, %0), %%mm1 \n\t"
# if COMPILE_TEMPLATE_MMX2
        "pshufw      $177, %%mm0, %%mm3 \n\t"
        "pshufw      $177, %%mm1, %%mm5 \n\t"
        "pand       %%mm7, %%mm0        \n\t"
        "pand       %%mm6, %%mm3        \n\t"
        "pand       %%mm7, %%mm1        \n\t"
        "pand       %%mm6, %%mm5        \n\t"
        "por        %%mm3, %%mm0        \n\t"
        "por        %%mm5, %%mm1        \n\t"
# else
        "movq       %%mm0, %%mm2        \n\t"
        "movq       %%mm1, %%mm4        \n\t"
        "pand       %%mm7, %%mm0        \n\t"
        "pand       %%mm6, %%mm2        \n\t"
        "pand       %%mm7, %%mm1        \n\t"
        "pand       %%mm6, %%mm4        \n\t"
        "movq       %%mm2, %%mm3        \n\t"
        "movq       %%mm4, %%mm5        \n\t"
        "pslld        $16, %%mm2        \n\t"
        "psrld        $16, %%mm3        \n\t"
        "pslld        $16, %%mm4        \n\t"
        "psrld        $16, %%mm5        \n\t"
        "por        %%mm2, %%mm0        \n\t"
        "por        %%mm4, %%mm1        \n\t"
        "por        %%mm3, %%mm0        \n\t"
        "por        %%mm5, %%mm1        \n\t"
# endif
        MOVNTQ"     %%mm0,  (%2, %0)    \n\t"
        MOVNTQ"     %%mm1, 8(%2, %0)    \n\t"
        "add          $16, %0           \n\t"
        "js            1b               \n\t"
        SFENCE"                         \n\t"
        EMMS"                           \n\t"
        "2:                             \n\t"
        : "+&r"(idx)
        : "r" (s), "r" (d), "m" (mask32b), "m" (mask32r), "m" (mmx_one)
        : "memory");
#endif
    for (; idx<15; idx+=4) {
        register int v = *(const uint32_t *)&s[idx], g = v & 0xff00ff00;
        v &= 0xff00ff;
        *(uint32_t *)&d[idx] = (v>>16) + g + (v<<16);
    }
}

static inline void RENAME(rgb24tobgr24)(const uint8_t *src, uint8_t *dst, long src_size)
{
    unsigned i;
#if COMPILE_TEMPLATE_MMX
    x86_reg mmx_size= 23 - src_size;
    __asm__ volatile (
        "test             %%"REG_a", %%"REG_a"          \n\t"
        "jns                     2f                     \n\t"
        "movq     "MANGLE(mask24r)", %%mm5              \n\t"
        "movq     "MANGLE(mask24g)", %%mm6              \n\t"
        "movq     "MANGLE(mask24b)", %%mm7              \n\t"
        ASMALIGN(4)
        "1:                                             \n\t"
        PREFETCH" 32(%1, %%"REG_a")                     \n\t"
        "movq       (%1, %%"REG_a"), %%mm0              \n\t" // BGR BGR BG
        "movq       (%1, %%"REG_a"), %%mm1              \n\t" // BGR BGR BG
        "movq      2(%1, %%"REG_a"), %%mm2              \n\t" // R BGR BGR B
        "psllq                  $16, %%mm0              \n\t" // 00 BGR BGR
        "pand                 %%mm5, %%mm0              \n\t"
        "pand                 %%mm6, %%mm1              \n\t"
        "pand                 %%mm7, %%mm2              \n\t"
        "por                  %%mm0, %%mm1              \n\t"
        "por                  %%mm2, %%mm1              \n\t"
        "movq      6(%1, %%"REG_a"), %%mm0              \n\t" // BGR BGR BG
        MOVNTQ"               %%mm1,   (%2, %%"REG_a")  \n\t" // RGB RGB RG
        "movq      8(%1, %%"REG_a"), %%mm1              \n\t" // R BGR BGR B
        "movq     10(%1, %%"REG_a"), %%mm2              \n\t" // GR BGR BGR
        "pand                 %%mm7, %%mm0              \n\t"
        "pand                 %%mm5, %%mm1              \n\t"
        "pand                 %%mm6, %%mm2              \n\t"
        "por                  %%mm0, %%mm1              \n\t"
        "por                  %%mm2, %%mm1              \n\t"
        "movq     14(%1, %%"REG_a"), %%mm0              \n\t" // R BGR BGR B
        MOVNTQ"               %%mm1,  8(%2, %%"REG_a")  \n\t" // B RGB RGB R
        "movq     16(%1, %%"REG_a"), %%mm1              \n\t" // GR BGR BGR
        "movq     18(%1, %%"REG_a"), %%mm2              \n\t" // BGR BGR BG
        "pand                 %%mm6, %%mm0              \n\t"
        "pand                 %%mm7, %%mm1              \n\t"
        "pand                 %%mm5, %%mm2              \n\t"
        "por                  %%mm0, %%mm1              \n\t"
        "por                  %%mm2, %%mm1              \n\t"
        MOVNTQ"               %%mm1, 16(%2, %%"REG_a")  \n\t"
        "add                    $24, %%"REG_a"          \n\t"
        " js                     1b                     \n\t"
        "2:                                             \n\t"
        : "+a" (mmx_size)
        : "r" (src-mmx_size), "r"(dst-mmx_size)
    );

    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");

    if (mmx_size==23) return; //finished, was multiple of 8

    src+= src_size;
    dst+= src_size;
    src_size= 23-mmx_size;
    src-= src_size;
    dst-= src_size;
#endif
    for (i=0; i<src_size; i+=3) {
        register uint8_t x;
        x          = src[i + 2];
        dst[i + 1] = src[i + 1];
        dst[i + 2] = src[i + 0];
        dst[i + 0] = x;
    }
}

static inline void RENAME(yuvPlanartoyuy2)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
                                           long width, long height,
                                           long lumStride, long chromStride, long dstStride, long vertLumPerChroma)
{
    long y;
    const x86_reg chromWidth= width>>1;
    for (y=0; y<height; y++) {
#if COMPILE_TEMPLATE_MMX
        //FIXME handle 2 lines at once (fewer prefetches, reuse some chroma, but very likely memory-limited anyway)
        __asm__ volatile(
            "xor                 %%"REG_a", %%"REG_a"   \n\t"
            ASMALIGN(4)
            "1:                                         \n\t"
            PREFETCH"    32(%1, %%"REG_a", 2)           \n\t"
            PREFETCH"    32(%2, %%"REG_a")              \n\t"
            PREFETCH"    32(%3, %%"REG_a")              \n\t"
            "movq          (%2, %%"REG_a"), %%mm0       \n\t" // U(0)
            "movq                    %%mm0, %%mm2       \n\t" // U(0)
            "movq          (%3, %%"REG_a"), %%mm1       \n\t" // V(0)
            "punpcklbw               %%mm1, %%mm0       \n\t" // UVUV UVUV(0)
            "punpckhbw               %%mm1, %%mm2       \n\t" // UVUV UVUV(8)

            "movq        (%1, %%"REG_a",2), %%mm3       \n\t" // Y(0)
            "movq       8(%1, %%"REG_a",2), %%mm5       \n\t" // Y(8)
            "movq                    %%mm3, %%mm4       \n\t" // Y(0)
            "movq                    %%mm5, %%mm6       \n\t" // Y(8)
            "punpcklbw               %%mm0, %%mm3       \n\t" // YUYV YUYV(0)
            "punpckhbw               %%mm0, %%mm4       \n\t" // YUYV YUYV(4)
            "punpcklbw               %%mm2, %%mm5       \n\t" // YUYV YUYV(8)
            "punpckhbw               %%mm2, %%mm6       \n\t" // YUYV YUYV(12)

            MOVNTQ"                  %%mm3,   (%0, %%"REG_a", 4)    \n\t"
            MOVNTQ"                  %%mm4,  8(%0, %%"REG_a", 4)    \n\t"
            MOVNTQ"                  %%mm5, 16(%0, %%"REG_a", 4)    \n\t"
            MOVNTQ"                  %%mm6, 24(%0, %%"REG_a", 4)    \n\t"

            "add                        $8, %%"REG_a"   \n\t"
            "cmp                        %4, %%"REG_a"   \n\t"
            " jb                        1b              \n\t"
            ::"r"(dst), "r"(ysrc), "r"(usrc), "r"(vsrc), "g" (chromWidth)
            : "%"REG_a
        );
#else

#if ARCH_ALPHA && HAVE_MVI
#define pl2yuy2(n)                  \
    y1 = yc[n];                     \
    y2 = yc2[n];                    \
    u = uc[n];                      \
    v = vc[n];                      \
    __asm__("unpkbw %1, %0" : "=r"(y1) : "r"(y1));  \
    __asm__("unpkbw %1, %0" : "=r"(y2) : "r"(y2));  \
    __asm__("unpkbl %1, %0" : "=r"(u) : "r"(u));    \
    __asm__("unpkbl %1, %0" : "=r"(v) : "r"(v));    \
    yuv1 = (u << 8) + (v << 24);                \
    yuv2 = yuv1 + y2;               \
    yuv1 += y1;                     \
    qdst[n]  = yuv1;                \
    qdst2[n] = yuv2;

        int i;
        uint64_t *qdst = (uint64_t *) dst;
        uint64_t *qdst2 = (uint64_t *) (dst + dstStride);
        const uint32_t *yc = (uint32_t *) ysrc;
        const uint32_t *yc2 = (uint32_t *) (ysrc + lumStride);
        const uint16_t *uc = (uint16_t*) usrc, *vc = (uint16_t*) vsrc;
        for (i = 0; i < chromWidth; i += 8) {
            uint64_t y1, y2, yuv1, yuv2;
            uint64_t u, v;
            /* Prefetch */
            __asm__("ldq $31,64(%0)" :: "r"(yc));
            __asm__("ldq $31,64(%0)" :: "r"(yc2));
            __asm__("ldq $31,64(%0)" :: "r"(uc));
            __asm__("ldq $31,64(%0)" :: "r"(vc));

            pl2yuy2(0);
            pl2yuy2(1);
            pl2yuy2(2);
            pl2yuy2(3);

            yc    += 4;
            yc2   += 4;
            uc    += 4;
            vc    += 4;
            qdst  += 4;
            qdst2 += 4;
        }
        y++;
        ysrc += lumStride;
        dst += dstStride;

#elif HAVE_FAST_64BIT
        int i;
        uint64_t *ldst = (uint64_t *) dst;
        const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
        for (i = 0; i < chromWidth; i += 2) {
            uint64_t k, l;
            k = yc[0] + (uc[0] << 8) +
                (yc[1] << 16) + (vc[0] << 24);
            l = yc[2] + (uc[1] << 8) +
                (yc[3] << 16) + (vc[1] << 24);
            *ldst++ = k + (l << 32);
            yc += 4;
            uc += 2;
            vc += 2;
        }

#else
        int i, *idst = (int32_t *) dst;
        const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
        for (i = 0; i < chromWidth; i++) {
#if HAVE_BIGENDIAN
            *idst++ = (yc[0] << 24)+ (uc[0] << 16) +
                (yc[1] << 8) + (vc[0] << 0);
#else
            *idst++ = yc[0] + (uc[0] << 8) +
                (yc[1] << 16) + (vc[0] << 24);
#endif
            yc += 2;
            uc++;
            vc++;
        }
#endif
#endif
        if ((y&(vertLumPerChroma-1)) == vertLumPerChroma-1) {
            usrc += chromStride;
            vsrc += chromStride;
        }
        ysrc += lumStride;
        dst  += dstStride;
    }
#if COMPILE_TEMPLATE_MMX
    __asm__(EMMS"       \n\t"
            SFENCE"     \n\t"
            :::"memory");
#endif
}

/**
 * Height should be a multiple of 2 and width should be a multiple of 16.
 * (If this is a problem for anyone then tell me, and I will fix it.)
 */
static inline void RENAME(yv12toyuy2)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
                                      long width, long height,
                                      long lumStride, long chromStride, long dstStride)
{
    //FIXME interpolate chroma
    RENAME(yuvPlanartoyuy2)(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride, 2);
}

static inline void RENAME(yuvPlanartouyvy)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
                                           long width, long height,
                                           long lumStride, long chromStride, long dstStride, long vertLumPerChroma)
{
    long y;
    const x86_reg chromWidth= width>>1;
    for (y=0; y<height; y++) {
#if COMPILE_TEMPLATE_MMX
        //FIXME handle 2 lines at once (fewer prefetches, reuse some chroma, but very likely memory-limited anyway)
        __asm__ volatile(
            "xor                %%"REG_a", %%"REG_a"    \n\t"
            ASMALIGN(4)
            "1:                                         \n\t"
            PREFETCH"   32(%1, %%"REG_a", 2)            \n\t"
            PREFETCH"   32(%2, %%"REG_a")               \n\t"
            PREFETCH"   32(%3, %%"REG_a")               \n\t"
            "movq         (%2, %%"REG_a"), %%mm0        \n\t" // U(0)
            "movq                   %%mm0, %%mm2        \n\t" // U(0)
            "movq         (%3, %%"REG_a"), %%mm1        \n\t" // V(0)
            "punpcklbw              %%mm1, %%mm0        \n\t" // UVUV UVUV(0)
            "punpckhbw              %%mm1, %%mm2        \n\t" // UVUV UVUV(8)

            "movq       (%1, %%"REG_a",2), %%mm3        \n\t" // Y(0)
            "movq      8(%1, %%"REG_a",2), %%mm5        \n\t" // Y(8)
            "movq                   %%mm0, %%mm4        \n\t" // Y(0)
            "movq                   %%mm2, %%mm6        \n\t" // Y(8)
            "punpcklbw              %%mm3, %%mm0        \n\t" // YUYV YUYV(0)
            "punpckhbw              %%mm3, %%mm4        \n\t" // YUYV YUYV(4)
            "punpcklbw              %%mm5, %%mm2        \n\t" // YUYV YUYV(8)
            "punpckhbw              %%mm5, %%mm6        \n\t" // YUYV YUYV(12)

            MOVNTQ"                 %%mm0,   (%0, %%"REG_a", 4)     \n\t"
            MOVNTQ"                 %%mm4,  8(%0, %%"REG_a", 4)     \n\t"
            MOVNTQ"                 %%mm2, 16(%0, %%"REG_a", 4)     \n\t"
            MOVNTQ"                 %%mm6, 24(%0, %%"REG_a", 4)     \n\t"

            "add                       $8, %%"REG_a"    \n\t"
            "cmp                       %4, %%"REG_a"    \n\t"
            " jb                       1b               \n\t"
            ::"r"(dst), "r"(ysrc), "r"(usrc), "r"(vsrc), "g" (chromWidth)
            : "%"REG_a
        );
#else
//FIXME adapt the Alpha ASM code from yv12->yuy2

#if HAVE_FAST_64BIT
        int i;
        uint64_t *ldst = (uint64_t *) dst;
        const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
        for (i = 0; i < chromWidth; i += 2) {
            uint64_t k, l;
            k = uc[0] + (yc[0] << 8) +
                (vc[0] << 16) + (yc[1] << 24);
            l = uc[1] + (yc[2] << 8) +
                (vc[1] << 16) + (yc[3] << 24);
            *ldst++ = k + (l << 32);
            yc += 4;
            uc += 2;
            vc += 2;
        }

#else
        int i, *idst = (int32_t *) dst;
        const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
        for (i = 0; i < chromWidth; i++) {
#if HAVE_BIGENDIAN
            *idst++ = (uc[0] << 24)+ (yc[0] << 16) +
                (vc[0] << 8) + (yc[1] << 0);
#else
            *idst++ = uc[0] + (yc[0] << 8) +
               (vc[0] << 16) + (yc[1] << 24);
#endif
            yc += 2;
            uc++;
            vc++;
        }
#endif
#endif
        if ((y&(vertLumPerChroma-1)) == vertLumPerChroma-1) {
            usrc += chromStride;
            vsrc += chromStride;
        }
        ysrc += lumStride;
        dst += dstStride;
    }
#if COMPILE_TEMPLATE_MMX
    __asm__(EMMS"       \n\t"
            SFENCE"     \n\t"
            :::"memory");
#endif
}

/**
 * Height should be a multiple of 2 and width should be a multiple of 16
 * (If this is a problem for anyone then tell me, and I will fix it.)
 */
static inline void RENAME(yv12touyvy)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
                                      long width, long height,
                                      long lumStride, long chromStride, long dstStride)
{
    //FIXME interpolate chroma
    RENAME(yuvPlanartouyvy)(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride, 2);
}

/**
 * Width should be a multiple of 16.
 */
static inline void RENAME(yuv422ptouyvy)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
                                         long width, long height,
                                         long lumStride, long chromStride, long dstStride)
{
    RENAME(yuvPlanartouyvy)(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride, 1);
}

/**
 * Width should be a multiple of 16.
 */
static inline void RENAME(yuv422ptoyuy2)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
                                         long width, long height,
                                         long lumStride, long chromStride, long dstStride)
{
    RENAME(yuvPlanartoyuy2)(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride, 1);
}

/**
 * Height should be a multiple of 2 and width should be a multiple of 16.
 * (If this is a problem for anyone then tell me, and I will fix it.)
 */
static inline void RENAME(yuy2toyv12)(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                                      long width, long height,
                                      long lumStride, long chromStride, long srcStride)
{
    long y;
    const x86_reg chromWidth= width>>1;
    for (y=0; y<height; y+=2) {
#if COMPILE_TEMPLATE_MMX
        __asm__ volatile(
            "xor                 %%"REG_a", %%"REG_a"   \n\t"
            "pcmpeqw                 %%mm7, %%mm7       \n\t"
            "psrlw                      $8, %%mm7       \n\t" // FF,00,FF,00...
            ASMALIGN(4)
            "1:                \n\t"
            PREFETCH" 64(%0, %%"REG_a", 4)              \n\t"
            "movq       (%0, %%"REG_a", 4), %%mm0       \n\t" // YUYV YUYV(0)
            "movq      8(%0, %%"REG_a", 4), %%mm1       \n\t" // YUYV YUYV(4)
            "movq                    %%mm0, %%mm2       \n\t" // YUYV YUYV(0)
            "movq                    %%mm1, %%mm3       \n\t" // YUYV YUYV(4)
            "psrlw                      $8, %%mm0       \n\t" // U0V0 U0V0(0)
            "psrlw                      $8, %%mm1       \n\t" // U0V0 U0V0(4)
            "pand                    %%mm7, %%mm2       \n\t" // Y0Y0 Y0Y0(0)
            "pand                    %%mm7, %%mm3       \n\t" // Y0Y0 Y0Y0(4)
            "packuswb                %%mm1, %%mm0       \n\t" // UVUV UVUV(0)
            "packuswb                %%mm3, %%mm2       \n\t" // YYYY YYYY(0)

            MOVNTQ"                  %%mm2, (%1, %%"REG_a", 2)  \n\t"

            "movq     16(%0, %%"REG_a", 4), %%mm1       \n\t" // YUYV YUYV(8)
            "movq     24(%0, %%"REG_a", 4), %%mm2       \n\t" // YUYV YUYV(12)
            "movq                    %%mm1, %%mm3       \n\t" // YUYV YUYV(8)
            "movq                    %%mm2, %%mm4       \n\t" // YUYV YUYV(12)
            "psrlw                      $8, %%mm1       \n\t" // U0V0 U0V0(8)
            "psrlw                      $8, %%mm2       \n\t" // U0V0 U0V0(12)
            "pand                    %%mm7, %%mm3       \n\t" // Y0Y0 Y0Y0(8)
            "pand                    %%mm7, %%mm4       \n\t" // Y0Y0 Y0Y0(12)
            "packuswb                %%mm2, %%mm1       \n\t" // UVUV UVUV(8)
            "packuswb                %%mm4, %%mm3       \n\t" // YYYY YYYY(8)

            MOVNTQ"                  %%mm3, 8(%1, %%"REG_a", 2) \n\t"

            "movq                    %%mm0, %%mm2       \n\t" // UVUV UVUV(0)
            "movq                    %%mm1, %%mm3       \n\t" // UVUV UVUV(8)
            "psrlw                      $8, %%mm0       \n\t" // V0V0 V0V0(0)
            "psrlw                      $8, %%mm1       \n\t" // V0V0 V0V0(8)
            "pand                    %%mm7, %%mm2       \n\t" // U0U0 U0U0(0)
            "pand                    %%mm7, %%mm3       \n\t" // U0U0 U0U0(8)
            "packuswb                %%mm1, %%mm0       \n\t" // VVVV VVVV(0)
            "packuswb                %%mm3, %%mm2       \n\t" // UUUU UUUU(0)

            MOVNTQ"                  %%mm0, (%3, %%"REG_a")     \n\t"
            MOVNTQ"                  %%mm2, (%2, %%"REG_a")     \n\t"

            "add                        $8, %%"REG_a"   \n\t"
            "cmp                        %4, %%"REG_a"   \n\t"
            " jb                        1b              \n\t"
            ::"r"(src), "r"(ydst), "r"(udst), "r"(vdst), "g" (chromWidth)
            : "memory", "%"REG_a
        );

        ydst += lumStride;
        src  += srcStride;

        __asm__ volatile(
            "xor                 %%"REG_a", %%"REG_a"   \n\t"
            ASMALIGN(4)
            "1:                                         \n\t"
            PREFETCH" 64(%0, %%"REG_a", 4)              \n\t"
            "movq       (%0, %%"REG_a", 4), %%mm0       \n\t" // YUYV YUYV(0)
            "movq      8(%0, %%"REG_a", 4), %%mm1       \n\t" // YUYV YUYV(4)
            "movq     16(%0, %%"REG_a", 4), %%mm2       \n\t" // YUYV YUYV(8)
            "movq     24(%0, %%"REG_a", 4), %%mm3       \n\t" // YUYV YUYV(12)
            "pand                    %%mm7, %%mm0       \n\t" // Y0Y0 Y0Y0(0)
            "pand                    %%mm7, %%mm1       \n\t" // Y0Y0 Y0Y0(4)
            "pand                    %%mm7, %%mm2       \n\t" // Y0Y0 Y0Y0(8)
            "pand                    %%mm7, %%mm3       \n\t" // Y0Y0 Y0Y0(12)
            "packuswb                %%mm1, %%mm0       \n\t" // YYYY YYYY(0)
            "packuswb                %%mm3, %%mm2       \n\t" // YYYY YYYY(8)

            MOVNTQ"                  %%mm0,  (%1, %%"REG_a", 2) \n\t"
            MOVNTQ"                  %%mm2, 8(%1, %%"REG_a", 2) \n\t"

            "add                        $8, %%"REG_a"   \n\t"
            "cmp                        %4, %%"REG_a"   \n\t"
            " jb                        1b              \n\t"

            ::"r"(src), "r"(ydst), "r"(udst), "r"(vdst), "g" (chromWidth)
            : "memory", "%"REG_a
        );
#else
        long i;
        for (i=0; i<chromWidth; i++) {
            ydst[2*i+0]     = src[4*i+0];
            udst[i]     = src[4*i+1];
            ydst[2*i+1]     = src[4*i+2];
            vdst[i]     = src[4*i+3];
        }
        ydst += lumStride;
        src  += srcStride;

        for (i=0; i<chromWidth; i++) {
            ydst[2*i+0]     = src[4*i+0];
            ydst[2*i+1]     = src[4*i+2];
        }
#endif
        udst += chromStride;
        vdst += chromStride;
        ydst += lumStride;
        src  += srcStride;
    }
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(EMMS"       \n\t"
                     SFENCE"     \n\t"
                     :::"memory");
#endif
}

static inline void RENAME(planar2x)(const uint8_t *src, uint8_t *dst, long srcWidth, long srcHeight, long srcStride, long dstStride)
{
    long x,y;

    dst[0]= src[0];

    // first line
    for (x=0; x<srcWidth-1; x++) {
        dst[2*x+1]= (3*src[x] +   src[x+1])>>2;
        dst[2*x+2]= (  src[x] + 3*src[x+1])>>2;
    }
    dst[2*srcWidth-1]= src[srcWidth-1];

    dst+= dstStride;

    for (y=1; y<srcHeight; y++) {
#if COMPILE_TEMPLATE_MMX2 || COMPILE_TEMPLATE_AMD3DNOW
        const x86_reg mmxSize= srcWidth&~15;
        __asm__ volatile(
            "mov           %4, %%"REG_a"            \n\t"
            "movq        "MANGLE(mmx_ff)", %%mm0    \n\t"
            "movq         (%0, %%"REG_a"), %%mm4    \n\t"
            "movq                   %%mm4, %%mm2    \n\t"
            "psllq                     $8, %%mm4    \n\t"
            "pand                   %%mm0, %%mm2    \n\t"
            "por                    %%mm2, %%mm4    \n\t"
            "movq         (%1, %%"REG_a"), %%mm5    \n\t"
            "movq                   %%mm5, %%mm3    \n\t"
            "psllq                     $8, %%mm5    \n\t"
            "pand                   %%mm0, %%mm3    \n\t"
            "por                    %%mm3, %%mm5    \n\t"
            "1:                                     \n\t"
            "movq         (%0, %%"REG_a"), %%mm0    \n\t"
            "movq         (%1, %%"REG_a"), %%mm1    \n\t"
            "movq        1(%0, %%"REG_a"), %%mm2    \n\t"
            "movq        1(%1, %%"REG_a"), %%mm3    \n\t"
            PAVGB"                  %%mm0, %%mm5    \n\t"
            PAVGB"                  %%mm0, %%mm3    \n\t"
            PAVGB"                  %%mm0, %%mm5    \n\t"
            PAVGB"                  %%mm0, %%mm3    \n\t"
            PAVGB"                  %%mm1, %%mm4    \n\t"
            PAVGB"                  %%mm1, %%mm2    \n\t"
            PAVGB"                  %%mm1, %%mm4    \n\t"
            PAVGB"                  %%mm1, %%mm2    \n\t"
            "movq                   %%mm5, %%mm7    \n\t"
            "movq                   %%mm4, %%mm6    \n\t"
            "punpcklbw              %%mm3, %%mm5    \n\t"
            "punpckhbw              %%mm3, %%mm7    \n\t"
            "punpcklbw              %%mm2, %%mm4    \n\t"
            "punpckhbw              %%mm2, %%mm6    \n\t"
#if 1
            MOVNTQ"                 %%mm5,  (%2, %%"REG_a", 2)  \n\t"
            MOVNTQ"                 %%mm7, 8(%2, %%"REG_a", 2)  \n\t"
            MOVNTQ"                 %%mm4,  (%3, %%"REG_a", 2)  \n\t"
            MOVNTQ"                 %%mm6, 8(%3, %%"REG_a", 2)  \n\t"
#else
            "movq                   %%mm5,  (%2, %%"REG_a", 2)  \n\t"
            "movq                   %%mm7, 8(%2, %%"REG_a", 2)  \n\t"
            "movq                   %%mm4,  (%3, %%"REG_a", 2)  \n\t"
            "movq                   %%mm6, 8(%3, %%"REG_a", 2)  \n\t"
#endif
            "add                       $8, %%"REG_a"            \n\t"
            "movq       -1(%0, %%"REG_a"), %%mm4    \n\t"
            "movq       -1(%1, %%"REG_a"), %%mm5    \n\t"
            " js                       1b                       \n\t"
            :: "r" (src + mmxSize  ), "r" (src + srcStride + mmxSize  ),
               "r" (dst + mmxSize*2), "r" (dst + dstStride + mmxSize*2),
               "g" (-mmxSize)
            : "%"REG_a
        );
#else
        const x86_reg mmxSize=1;

        dst[0        ]= (3*src[0] +   src[srcStride])>>2;
        dst[dstStride]= (  src[0] + 3*src[srcStride])>>2;
#endif

        for (x=mmxSize-1; x<srcWidth-1; x++) {
            dst[2*x          +1]= (3*src[x+0] +   src[x+srcStride+1])>>2;
            dst[2*x+dstStride+2]= (  src[x+0] + 3*src[x+srcStride+1])>>2;
            dst[2*x+dstStride+1]= (  src[x+1] + 3*src[x+srcStride  ])>>2;
            dst[2*x          +2]= (3*src[x+1] +   src[x+srcStride  ])>>2;
        }
        dst[srcWidth*2 -1            ]= (3*src[srcWidth-1] +   src[srcWidth-1 + srcStride])>>2;
        dst[srcWidth*2 -1 + dstStride]= (  src[srcWidth-1] + 3*src[srcWidth-1 + srcStride])>>2;

        dst+=dstStride*2;
        src+=srcStride;
    }

    // last line
#if 1
    dst[0]= src[0];

    for (x=0; x<srcWidth-1; x++) {
        dst[2*x+1]= (3*src[x] +   src[x+1])>>2;
        dst[2*x+2]= (  src[x] + 3*src[x+1])>>2;
    }
    dst[2*srcWidth-1]= src[srcWidth-1];
#else
    for (x=0; x<srcWidth; x++) {
        dst[2*x+0]=
        dst[2*x+1]= src[x];
    }
#endif

#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(EMMS"       \n\t"
                     SFENCE"     \n\t"
                     :::"memory");
#endif
}

/**
 * Height should be a multiple of 2 and width should be a multiple of 16.
 * (If this is a problem for anyone then tell me, and I will fix it.)
 * Chrominance data is only taken from every second line, others are ignored.
 * FIXME: Write HQ version.
 */
static inline void RENAME(uyvytoyv12)(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                                      long width, long height,
                                      long lumStride, long chromStride, long srcStride)
{
    long y;
    const x86_reg chromWidth= width>>1;
    for (y=0; y<height; y+=2) {
#if COMPILE_TEMPLATE_MMX
        __asm__ volatile(
            "xor                 %%"REG_a", %%"REG_a"   \n\t"
            "pcmpeqw             %%mm7, %%mm7   \n\t"
            "psrlw                  $8, %%mm7   \n\t" // FF,00,FF,00...
            ASMALIGN(4)
            "1:                                 \n\t"
            PREFETCH" 64(%0, %%"REG_a", 4)          \n\t"
            "movq       (%0, %%"REG_a", 4), %%mm0   \n\t" // UYVY UYVY(0)
            "movq      8(%0, %%"REG_a", 4), %%mm1   \n\t" // UYVY UYVY(4)
            "movq                %%mm0, %%mm2   \n\t" // UYVY UYVY(0)
            "movq                %%mm1, %%mm3   \n\t" // UYVY UYVY(4)
            "pand                %%mm7, %%mm0   \n\t" // U0V0 U0V0(0)
            "pand                %%mm7, %%mm1   \n\t" // U0V0 U0V0(4)
            "psrlw                  $8, %%mm2   \n\t" // Y0Y0 Y0Y0(0)
            "psrlw                  $8, %%mm3   \n\t" // Y0Y0 Y0Y0(4)
            "packuswb            %%mm1, %%mm0   \n\t" // UVUV UVUV(0)
            "packuswb            %%mm3, %%mm2   \n\t" // YYYY YYYY(0)

            MOVNTQ"              %%mm2,  (%1, %%"REG_a", 2) \n\t"

            "movq     16(%0, %%"REG_a", 4), %%mm1   \n\t" // UYVY UYVY(8)
            "movq     24(%0, %%"REG_a", 4), %%mm2   \n\t" // UYVY UYVY(12)
            "movq                %%mm1, %%mm3   \n\t" // UYVY UYVY(8)
            "movq                %%mm2, %%mm4   \n\t" // UYVY UYVY(12)
            "pand                %%mm7, %%mm1   \n\t" // U0V0 U0V0(8)
            "pand                %%mm7, %%mm2   \n\t" // U0V0 U0V0(12)
            "psrlw                  $8, %%mm3   \n\t" // Y0Y0 Y0Y0(8)
            "psrlw                  $8, %%mm4   \n\t" // Y0Y0 Y0Y0(12)
            "packuswb            %%mm2, %%mm1   \n\t" // UVUV UVUV(8)
            "packuswb            %%mm4, %%mm3   \n\t" // YYYY YYYY(8)

            MOVNTQ"              %%mm3, 8(%1, %%"REG_a", 2) \n\t"

            "movq                %%mm0, %%mm2   \n\t" // UVUV UVUV(0)
            "movq                %%mm1, %%mm3   \n\t" // UVUV UVUV(8)
            "psrlw                  $8, %%mm0   \n\t" // V0V0 V0V0(0)
            "psrlw                  $8, %%mm1   \n\t" // V0V0 V0V0(8)
            "pand                %%mm7, %%mm2   \n\t" // U0U0 U0U0(0)
            "pand                %%mm7, %%mm3   \n\t" // U0U0 U0U0(8)
            "packuswb            %%mm1, %%mm0   \n\t" // VVVV VVVV(0)
            "packuswb            %%mm3, %%mm2   \n\t" // UUUU UUUU(0)

            MOVNTQ"              %%mm0, (%3, %%"REG_a") \n\t"
            MOVNTQ"              %%mm2, (%2, %%"REG_a") \n\t"

            "add                    $8, %%"REG_a"   \n\t"
            "cmp                    %4, %%"REG_a"   \n\t"
            " jb                    1b          \n\t"
            ::"r"(src), "r"(ydst), "r"(udst), "r"(vdst), "g" (chromWidth)
            : "memory", "%"REG_a
        );

        ydst += lumStride;
        src  += srcStride;

        __asm__ volatile(
            "xor                 %%"REG_a", %%"REG_a"   \n\t"
            ASMALIGN(4)
            "1:                                 \n\t"
            PREFETCH" 64(%0, %%"REG_a", 4)          \n\t"
            "movq       (%0, %%"REG_a", 4), %%mm0   \n\t" // YUYV YUYV(0)
            "movq      8(%0, %%"REG_a", 4), %%mm1   \n\t" // YUYV YUYV(4)
            "movq     16(%0, %%"REG_a", 4), %%mm2   \n\t" // YUYV YUYV(8)
            "movq     24(%0, %%"REG_a", 4), %%mm3   \n\t" // YUYV YUYV(12)
            "psrlw                  $8, %%mm0   \n\t" // Y0Y0 Y0Y0(0)
            "psrlw                  $8, %%mm1   \n\t" // Y0Y0 Y0Y0(4)
            "psrlw                  $8, %%mm2   \n\t" // Y0Y0 Y0Y0(8)
            "psrlw                  $8, %%mm3   \n\t" // Y0Y0 Y0Y0(12)
            "packuswb            %%mm1, %%mm0   \n\t" // YYYY YYYY(0)
            "packuswb            %%mm3, %%mm2   \n\t" // YYYY YYYY(8)

            MOVNTQ"              %%mm0,  (%1, %%"REG_a", 2) \n\t"
            MOVNTQ"              %%mm2, 8(%1, %%"REG_a", 2) \n\t"

            "add                    $8, %%"REG_a"   \n\t"
            "cmp                    %4, %%"REG_a"   \n\t"
            " jb                    1b          \n\t"

            ::"r"(src), "r"(ydst), "r"(udst), "r"(vdst), "g" (chromWidth)
            : "memory", "%"REG_a
        );
#else
        long i;
        for (i=0; i<chromWidth; i++) {
            udst[i]     = src[4*i+0];
            ydst[2*i+0] = src[4*i+1];
            vdst[i]     = src[4*i+2];
            ydst[2*i+1] = src[4*i+3];
        }
        ydst += lumStride;
        src  += srcStride;

        for (i=0; i<chromWidth; i++) {
            ydst[2*i+0] = src[4*i+1];
            ydst[2*i+1] = src[4*i+3];
        }
#endif
        udst += chromStride;
        vdst += chromStride;
        ydst += lumStride;
        src  += srcStride;
    }
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(EMMS"       \n\t"
                     SFENCE"     \n\t"
                     :::"memory");
#endif
}

/**
 * Height should be a multiple of 2 and width should be a multiple of 2.
 * (If this is a problem for anyone then tell me, and I will fix it.)
 * Chrominance data is only taken from every second line,
 * others are ignored in the C version.
 * FIXME: Write HQ version.
 */
static inline void RENAME(rgb24toyv12)(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                                       long width, long height,
                                       long lumStride, long chromStride, long srcStride)
{
    long y;
    const x86_reg chromWidth= width>>1;
#if COMPILE_TEMPLATE_MMX
    for (y=0; y<height-2; y+=2) {
        long i;
        for (i=0; i<2; i++) {
            __asm__ volatile(
                "mov                        %2, %%"REG_a"   \n\t"
                "movq  "MANGLE(ff_bgr2YCoeff)", %%mm6       \n\t"
                "movq       "MANGLE(ff_w1111)", %%mm5       \n\t"
                "pxor                    %%mm7, %%mm7       \n\t"
                "lea (%%"REG_a", %%"REG_a", 2), %%"REG_d"   \n\t"
                ASMALIGN(4)
                "1:                                         \n\t"
                PREFETCH"    64(%0, %%"REG_d")              \n\t"
                "movd          (%0, %%"REG_d"), %%mm0       \n\t"
                "movd         3(%0, %%"REG_d"), %%mm1       \n\t"
                "punpcklbw               %%mm7, %%mm0       \n\t"
                "punpcklbw               %%mm7, %%mm1       \n\t"
                "movd         6(%0, %%"REG_d"), %%mm2       \n\t"
                "movd         9(%0, %%"REG_d"), %%mm3       \n\t"
                "punpcklbw               %%mm7, %%mm2       \n\t"
                "punpcklbw               %%mm7, %%mm3       \n\t"
                "pmaddwd                 %%mm6, %%mm0       \n\t"
                "pmaddwd                 %%mm6, %%mm1       \n\t"
                "pmaddwd                 %%mm6, %%mm2       \n\t"
                "pmaddwd                 %%mm6, %%mm3       \n\t"
#ifndef FAST_BGR2YV12
                "psrad                      $8, %%mm0       \n\t"
                "psrad                      $8, %%mm1       \n\t"
                "psrad                      $8, %%mm2       \n\t"
                "psrad                      $8, %%mm3       \n\t"
#endif
                "packssdw                %%mm1, %%mm0       \n\t"
                "packssdw                %%mm3, %%mm2       \n\t"
                "pmaddwd                 %%mm5, %%mm0       \n\t"
                "pmaddwd                 %%mm5, %%mm2       \n\t"
                "packssdw                %%mm2, %%mm0       \n\t"
                "psraw                      $7, %%mm0       \n\t"

                "movd        12(%0, %%"REG_d"), %%mm4       \n\t"
                "movd        15(%0, %%"REG_d"), %%mm1       \n\t"
                "punpcklbw               %%mm7, %%mm4       \n\t"
                "punpcklbw               %%mm7, %%mm1       \n\t"
                "movd        18(%0, %%"REG_d"), %%mm2       \n\t"
                "movd        21(%0, %%"REG_d"), %%mm3       \n\t"
                "punpcklbw               %%mm7, %%mm2       \n\t"
                "punpcklbw               %%mm7, %%mm3       \n\t"
                "pmaddwd                 %%mm6, %%mm4       \n\t"
                "pmaddwd                 %%mm6, %%mm1       \n\t"
                "pmaddwd                 %%mm6, %%mm2       \n\t"
                "pmaddwd                 %%mm6, %%mm3       \n\t"
#ifndef FAST_BGR2YV12
                "psrad                      $8, %%mm4       \n\t"
                "psrad                      $8, %%mm1       \n\t"
                "psrad                      $8, %%mm2       \n\t"
                "psrad                      $8, %%mm3       \n\t"
#endif
                "packssdw                %%mm1, %%mm4       \n\t"
                "packssdw                %%mm3, %%mm2       \n\t"
                "pmaddwd                 %%mm5, %%mm4       \n\t"
                "pmaddwd                 %%mm5, %%mm2       \n\t"
                "add                       $24, %%"REG_d"   \n\t"
                "packssdw                %%mm2, %%mm4       \n\t"
                "psraw                      $7, %%mm4       \n\t"

                "packuswb                %%mm4, %%mm0       \n\t"
                "paddusb "MANGLE(ff_bgr2YOffset)", %%mm0    \n\t"

                MOVNTQ"                  %%mm0, (%1, %%"REG_a") \n\t"
                "add                        $8,      %%"REG_a"  \n\t"
                " js                        1b                  \n\t"
                : : "r" (src+width*3), "r" (ydst+width), "g" ((x86_reg)-width)
                : "%"REG_a, "%"REG_d
            );
            ydst += lumStride;
            src  += srcStride;
        }
        src -= srcStride*2;
        __asm__ volatile(
            "mov                        %4, %%"REG_a"   \n\t"
            "movq       "MANGLE(ff_w1111)", %%mm5       \n\t"
            "movq  "MANGLE(ff_bgr2UCoeff)", %%mm6       \n\t"
            "pxor                    %%mm7, %%mm7       \n\t"
            "lea (%%"REG_a", %%"REG_a", 2), %%"REG_d"   \n\t"
            "add                 %%"REG_d", %%"REG_d"   \n\t"
            ASMALIGN(4)
            "1:                                         \n\t"
            PREFETCH"    64(%0, %%"REG_d")              \n\t"
            PREFETCH"    64(%1, %%"REG_d")              \n\t"
#if COMPILE_TEMPLATE_MMX2 || COMPILE_TEMPLATE_AMD3DNOW
            "movq          (%0, %%"REG_d"), %%mm0       \n\t"
            "movq          (%1, %%"REG_d"), %%mm1       \n\t"
            "movq         6(%0, %%"REG_d"), %%mm2       \n\t"
            "movq         6(%1, %%"REG_d"), %%mm3       \n\t"
            PAVGB"                   %%mm1, %%mm0       \n\t"
            PAVGB"                   %%mm3, %%mm2       \n\t"
            "movq                    %%mm0, %%mm1       \n\t"
            "movq                    %%mm2, %%mm3       \n\t"
            "psrlq                     $24, %%mm0       \n\t"
            "psrlq                     $24, %%mm2       \n\t"
            PAVGB"                   %%mm1, %%mm0       \n\t"
            PAVGB"                   %%mm3, %%mm2       \n\t"
            "punpcklbw               %%mm7, %%mm0       \n\t"
            "punpcklbw               %%mm7, %%mm2       \n\t"
#else
            "movd          (%0, %%"REG_d"), %%mm0       \n\t"
            "movd          (%1, %%"REG_d"), %%mm1       \n\t"
            "movd         3(%0, %%"REG_d"), %%mm2       \n\t"
            "movd         3(%1, %%"REG_d"), %%mm3       \n\t"
            "punpcklbw               %%mm7, %%mm0       \n\t"
            "punpcklbw               %%mm7, %%mm1       \n\t"
            "punpcklbw               %%mm7, %%mm2       \n\t"
            "punpcklbw               %%mm7, %%mm3       \n\t"
            "paddw                   %%mm1, %%mm0       \n\t"
            "paddw                   %%mm3, %%mm2       \n\t"
            "paddw                   %%mm2, %%mm0       \n\t"
            "movd         6(%0, %%"REG_d"), %%mm4       \n\t"
            "movd         6(%1, %%"REG_d"), %%mm1       \n\t"
            "movd         9(%0, %%"REG_d"), %%mm2       \n\t"
            "movd         9(%1, %%"REG_d"), %%mm3       \n\t"
            "punpcklbw               %%mm7, %%mm4       \n\t"
            "punpcklbw               %%mm7, %%mm1       \n\t"
            "punpcklbw               %%mm7, %%mm2       \n\t"
            "punpcklbw               %%mm7, %%mm3       \n\t"
            "paddw                   %%mm1, %%mm4       \n\t"
            "paddw                   %%mm3, %%mm2       \n\t"
            "paddw                   %%mm4, %%mm2       \n\t"
            "psrlw                      $2, %%mm0       \n\t"
            "psrlw                      $2, %%mm2       \n\t"
#endif
            "movq  "MANGLE(ff_bgr2VCoeff)", %%mm1       \n\t"
            "movq  "MANGLE(ff_bgr2VCoeff)", %%mm3       \n\t"

            "pmaddwd                 %%mm0, %%mm1       \n\t"
            "pmaddwd                 %%mm2, %%mm3       \n\t"
            "pmaddwd                 %%mm6, %%mm0       \n\t"
            "pmaddwd                 %%mm6, %%mm2       \n\t"
#ifndef FAST_BGR2YV12
            "psrad                      $8, %%mm0       \n\t"
            "psrad                      $8, %%mm1       \n\t"
            "psrad                      $8, %%mm2       \n\t"
            "psrad                      $8, %%mm3       \n\t"
#endif
            "packssdw                %%mm2, %%mm0       \n\t"
            "packssdw                %%mm3, %%mm1       \n\t"
            "pmaddwd                 %%mm5, %%mm0       \n\t"
            "pmaddwd                 %%mm5, %%mm1       \n\t"
            "packssdw                %%mm1, %%mm0       \n\t" // V1 V0 U1 U0
            "psraw                      $7, %%mm0       \n\t"

#if COMPILE_TEMPLATE_MMX2 || COMPILE_TEMPLATE_AMD3DNOW
            "movq        12(%0, %%"REG_d"), %%mm4       \n\t"
            "movq        12(%1, %%"REG_d"), %%mm1       \n\t"
            "movq        18(%0, %%"REG_d"), %%mm2       \n\t"
            "movq        18(%1, %%"REG_d"), %%mm3       \n\t"
            PAVGB"                   %%mm1, %%mm4       \n\t"
            PAVGB"                   %%mm3, %%mm2       \n\t"
            "movq                    %%mm4, %%mm1       \n\t"
            "movq                    %%mm2, %%mm3       \n\t"
            "psrlq                     $24, %%mm4       \n\t"
            "psrlq                     $24, %%mm2       \n\t"
            PAVGB"                   %%mm1, %%mm4       \n\t"
            PAVGB"                   %%mm3, %%mm2       \n\t"
            "punpcklbw               %%mm7, %%mm4       \n\t"
            "punpcklbw               %%mm7, %%mm2       \n\t"
#else
            "movd        12(%0, %%"REG_d"), %%mm4       \n\t"
            "movd        12(%1, %%"REG_d"), %%mm1       \n\t"
            "movd        15(%0, %%"REG_d"), %%mm2       \n\t"
            "movd        15(%1, %%"REG_d"), %%mm3       \n\t"
            "punpcklbw               %%mm7, %%mm4       \n\t"
            "punpcklbw               %%mm7, %%mm1       \n\t"
            "punpcklbw               %%mm7, %%mm2       \n\t"
            "punpcklbw               %%mm7, %%mm3       \n\t"
            "paddw                   %%mm1, %%mm4       \n\t"
            "paddw                   %%mm3, %%mm2       \n\t"
            "paddw                   %%mm2, %%mm4       \n\t"
            "movd        18(%0, %%"REG_d"), %%mm5       \n\t"
            "movd        18(%1, %%"REG_d"), %%mm1       \n\t"
            "movd        21(%0, %%"REG_d"), %%mm2       \n\t"
            "movd        21(%1, %%"REG_d"), %%mm3       \n\t"
            "punpcklbw               %%mm7, %%mm5       \n\t"
            "punpcklbw               %%mm7, %%mm1       \n\t"
            "punpcklbw               %%mm7, %%mm2       \n\t"
            "punpcklbw               %%mm7, %%mm3       \n\t"
            "paddw                   %%mm1, %%mm5       \n\t"
            "paddw                   %%mm3, %%mm2       \n\t"
            "paddw                   %%mm5, %%mm2       \n\t"
            "movq       "MANGLE(ff_w1111)", %%mm5       \n\t"
            "psrlw                      $2, %%mm4       \n\t"
            "psrlw                      $2, %%mm2       \n\t"
#endif
            "movq  "MANGLE(ff_bgr2VCoeff)", %%mm1       \n\t"
            "movq  "MANGLE(ff_bgr2VCoeff)", %%mm3       \n\t"

            "pmaddwd                 %%mm4, %%mm1       \n\t"
            "pmaddwd                 %%mm2, %%mm3       \n\t"
            "pmaddwd                 %%mm6, %%mm4       \n\t"
            "pmaddwd                 %%mm6, %%mm2       \n\t"
#ifndef FAST_BGR2YV12
            "psrad                      $8, %%mm4       \n\t"
            "psrad                      $8, %%mm1       \n\t"
            "psrad                      $8, %%mm2       \n\t"
            "psrad                      $8, %%mm3       \n\t"
#endif
            "packssdw                %%mm2, %%mm4       \n\t"
            "packssdw                %%mm3, %%mm1       \n\t"
            "pmaddwd                 %%mm5, %%mm4       \n\t"
            "pmaddwd                 %%mm5, %%mm1       \n\t"
            "add                       $24, %%"REG_d"   \n\t"
            "packssdw                %%mm1, %%mm4       \n\t" // V3 V2 U3 U2
            "psraw                      $7, %%mm4       \n\t"

            "movq                    %%mm0, %%mm1           \n\t"
            "punpckldq               %%mm4, %%mm0           \n\t"
            "punpckhdq               %%mm4, %%mm1           \n\t"
            "packsswb                %%mm1, %%mm0           \n\t"
            "paddb "MANGLE(ff_bgr2UVOffset)", %%mm0         \n\t"
            "movd                    %%mm0, (%2, %%"REG_a") \n\t"
            "punpckhdq               %%mm0, %%mm0           \n\t"
            "movd                    %%mm0, (%3, %%"REG_a") \n\t"
            "add                        $4, %%"REG_a"       \n\t"
            " js                        1b                  \n\t"
            : : "r" (src+chromWidth*6), "r" (src+srcStride+chromWidth*6), "r" (udst+chromWidth), "r" (vdst+chromWidth), "g" (-chromWidth)
            : "%"REG_a, "%"REG_d
        );

        udst += chromStride;
        vdst += chromStride;
        src  += srcStride*2;
    }

    __asm__ volatile(EMMS"       \n\t"
                     SFENCE"     \n\t"
                     :::"memory");
#else
    y=0;
#endif
    for (; y<height; y+=2) {
        long i;
        for (i=0; i<chromWidth; i++) {
            unsigned int b = src[6*i+0];
            unsigned int g = src[6*i+1];
            unsigned int r = src[6*i+2];

            unsigned int Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;
            unsigned int V  =  ((RV*r + GV*g + BV*b)>>RGB2YUV_SHIFT) + 128;
            unsigned int U  =  ((RU*r + GU*g + BU*b)>>RGB2YUV_SHIFT) + 128;

            udst[i]     = U;
            vdst[i]     = V;
            ydst[2*i]   = Y;

            b = src[6*i+3];
            g = src[6*i+4];
            r = src[6*i+5];

            Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;
            ydst[2*i+1]     = Y;
        }
        ydst += lumStride;
        src  += srcStride;

        for (i=0; i<chromWidth; i++) {
            unsigned int b = src[6*i+0];
            unsigned int g = src[6*i+1];
            unsigned int r = src[6*i+2];

            unsigned int Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;

            ydst[2*i]     = Y;

            b = src[6*i+3];
            g = src[6*i+4];
            r = src[6*i+5];

            Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;
            ydst[2*i+1]     = Y;
        }
        udst += chromStride;
        vdst += chromStride;
        ydst += lumStride;
        src  += srcStride;
    }
}

static void RENAME(interleaveBytes)(const uint8_t *src1, const uint8_t *src2, uint8_t *dest,
                             long width, long height, long src1Stride,
                             long src2Stride, long dstStride)
{
    long h;

    for (h=0; h < height; h++) {
        long w;

#if COMPILE_TEMPLATE_MMX
#if COMPILE_TEMPLATE_SSE2
        __asm__(
            "xor              %%"REG_a", %%"REG_a"  \n\t"
            "1:                                     \n\t"
            PREFETCH" 64(%1, %%"REG_a")             \n\t"
            PREFETCH" 64(%2, %%"REG_a")             \n\t"
            "movdqa     (%1, %%"REG_a"), %%xmm0     \n\t"
            "movdqa     (%1, %%"REG_a"), %%xmm1     \n\t"
            "movdqa     (%2, %%"REG_a"), %%xmm2     \n\t"
            "punpcklbw           %%xmm2, %%xmm0     \n\t"
            "punpckhbw           %%xmm2, %%xmm1     \n\t"
            "movntdq             %%xmm0,   (%0, %%"REG_a", 2)   \n\t"
            "movntdq             %%xmm1, 16(%0, %%"REG_a", 2)   \n\t"
            "add                    $16, %%"REG_a"  \n\t"
            "cmp                     %3, %%"REG_a"  \n\t"
            " jb                     1b             \n\t"
            ::"r"(dest), "r"(src1), "r"(src2), "r" ((x86_reg)width-15)
            : "memory", "%"REG_a""
        );
#else
        __asm__(
            "xor %%"REG_a", %%"REG_a"               \n\t"
            "1:                                     \n\t"
            PREFETCH" 64(%1, %%"REG_a")             \n\t"
            PREFETCH" 64(%2, %%"REG_a")             \n\t"
            "movq       (%1, %%"REG_a"), %%mm0      \n\t"
            "movq      8(%1, %%"REG_a"), %%mm2      \n\t"
            "movq                 %%mm0, %%mm1      \n\t"
            "movq                 %%mm2, %%mm3      \n\t"
            "movq       (%2, %%"REG_a"), %%mm4      \n\t"
            "movq      8(%2, %%"REG_a"), %%mm5      \n\t"
            "punpcklbw            %%mm4, %%mm0      \n\t"
            "punpckhbw            %%mm4, %%mm1      \n\t"
            "punpcklbw            %%mm5, %%mm2      \n\t"
            "punpckhbw            %%mm5, %%mm3      \n\t"
            MOVNTQ"               %%mm0,   (%0, %%"REG_a", 2)   \n\t"
            MOVNTQ"               %%mm1,  8(%0, %%"REG_a", 2)   \n\t"
            MOVNTQ"               %%mm2, 16(%0, %%"REG_a", 2)   \n\t"
            MOVNTQ"               %%mm3, 24(%0, %%"REG_a", 2)   \n\t"
            "add                    $16, %%"REG_a"  \n\t"
            "cmp                     %3, %%"REG_a"  \n\t"
            " jb                     1b             \n\t"
            ::"r"(dest), "r"(src1), "r"(src2), "r" ((x86_reg)width-15)
            : "memory", "%"REG_a
        );
#endif
        for (w= (width&(~15)); w < width; w++) {
            dest[2*w+0] = src1[w];
            dest[2*w+1] = src2[w];
        }
#else
        for (w=0; w < width; w++) {
            dest[2*w+0] = src1[w];
            dest[2*w+1] = src2[w];
        }
#endif
        dest += dstStride;
        src1 += src1Stride;
        src2 += src2Stride;
    }
#if COMPILE_TEMPLATE_MMX
    __asm__(
            EMMS"       \n\t"
            SFENCE"     \n\t"
            ::: "memory"
            );
#endif
}

static inline void RENAME(vu9_to_vu12)(const uint8_t *src1, const uint8_t *src2,
                                       uint8_t *dst1, uint8_t *dst2,
                                       long width, long height,
                                       long srcStride1, long srcStride2,
                                       long dstStride1, long dstStride2)
{
    x86_reg y;
    long x,w,h;
    w=width/2; h=height/2;
#if COMPILE_TEMPLATE_MMX
    __asm__ volatile(
        PREFETCH" %0    \n\t"
        PREFETCH" %1    \n\t"
        ::"m"(*(src1+srcStride1)),"m"(*(src2+srcStride2)):"memory");
#endif
    for (y=0;y<h;y++) {
        const uint8_t* s1=src1+srcStride1*(y>>1);
        uint8_t* d=dst1+dstStride1*y;
        x=0;
#if COMPILE_TEMPLATE_MMX
        for (;x<w-31;x+=32) {
            __asm__ volatile(
                PREFETCH"   32%1        \n\t"
                "movq         %1, %%mm0 \n\t"
                "movq        8%1, %%mm2 \n\t"
                "movq       16%1, %%mm4 \n\t"
                "movq       24%1, %%mm6 \n\t"
                "movq      %%mm0, %%mm1 \n\t"
                "movq      %%mm2, %%mm3 \n\t"
                "movq      %%mm4, %%mm5 \n\t"
                "movq      %%mm6, %%mm7 \n\t"
                "punpcklbw %%mm0, %%mm0 \n\t"
                "punpckhbw %%mm1, %%mm1 \n\t"
                "punpcklbw %%mm2, %%mm2 \n\t"
                "punpckhbw %%mm3, %%mm3 \n\t"
                "punpcklbw %%mm4, %%mm4 \n\t"
                "punpckhbw %%mm5, %%mm5 \n\t"
                "punpcklbw %%mm6, %%mm6 \n\t"
                "punpckhbw %%mm7, %%mm7 \n\t"
                MOVNTQ"    %%mm0,   %0  \n\t"
                MOVNTQ"    %%mm1,  8%0  \n\t"
                MOVNTQ"    %%mm2, 16%0  \n\t"
                MOVNTQ"    %%mm3, 24%0  \n\t"
                MOVNTQ"    %%mm4, 32%0  \n\t"
                MOVNTQ"    %%mm5, 40%0  \n\t"
                MOVNTQ"    %%mm6, 48%0  \n\t"
                MOVNTQ"    %%mm7, 56%0"
                :"=m"(d[2*x])
                :"m"(s1[x])
                :"memory");
        }
#endif
        for (;x<w;x++) d[2*x]=d[2*x+1]=s1[x];
    }
    for (y=0;y<h;y++) {
        const uint8_t* s2=src2+srcStride2*(y>>1);
        uint8_t* d=dst2+dstStride2*y;
        x=0;
#if COMPILE_TEMPLATE_MMX
        for (;x<w-31;x+=32) {
            __asm__ volatile(
                PREFETCH"   32%1        \n\t"
                "movq         %1, %%mm0 \n\t"
                "movq        8%1, %%mm2 \n\t"
                "movq       16%1, %%mm4 \n\t"
                "movq       24%1, %%mm6 \n\t"
                "movq      %%mm0, %%mm1 \n\t"
                "movq      %%mm2, %%mm3 \n\t"
                "movq      %%mm4, %%mm5 \n\t"
                "movq      %%mm6, %%mm7 \n\t"
                "punpcklbw %%mm0, %%mm0 \n\t"
                "punpckhbw %%mm1, %%mm1 \n\t"
                "punpcklbw %%mm2, %%mm2 \n\t"
                "punpckhbw %%mm3, %%mm3 \n\t"
                "punpcklbw %%mm4, %%mm4 \n\t"
                "punpckhbw %%mm5, %%mm5 \n\t"
                "punpcklbw %%mm6, %%mm6 \n\t"
                "punpckhbw %%mm7, %%mm7 \n\t"
                MOVNTQ"    %%mm0,   %0  \n\t"
                MOVNTQ"    %%mm1,  8%0  \n\t"
                MOVNTQ"    %%mm2, 16%0  \n\t"
                MOVNTQ"    %%mm3, 24%0  \n\t"
                MOVNTQ"    %%mm4, 32%0  \n\t"
                MOVNTQ"    %%mm5, 40%0  \n\t"
                MOVNTQ"    %%mm6, 48%0  \n\t"
                MOVNTQ"    %%mm7, 56%0"
                :"=m"(d[2*x])
                :"m"(s2[x])
                :"memory");
        }
#endif
        for (;x<w;x++) d[2*x]=d[2*x+1]=s2[x];
    }
#if COMPILE_TEMPLATE_MMX
    __asm__(
            EMMS"       \n\t"
            SFENCE"     \n\t"
            ::: "memory"
        );
#endif
}

static inline void RENAME(yvu9_to_yuy2)(const uint8_t *src1, const uint8_t *src2, const uint8_t *src3,
                                        uint8_t *dst,
                                        long width, long height,
                                        long srcStride1, long srcStride2,
                                        long srcStride3, long dstStride)
{
    x86_reg x;
    long y,w,h;
    w=width/2; h=height;
    for (y=0;y<h;y++) {
        const uint8_t* yp=src1+srcStride1*y;
        const uint8_t* up=src2+srcStride2*(y>>2);
        const uint8_t* vp=src3+srcStride3*(y>>2);
        uint8_t* d=dst+dstStride*y;
        x=0;
#if COMPILE_TEMPLATE_MMX
        for (;x<w-7;x+=8) {
            __asm__ volatile(
                PREFETCH"   32(%1, %0)          \n\t"
                PREFETCH"   32(%2, %0)          \n\t"
                PREFETCH"   32(%3, %0)          \n\t"
                "movq      (%1, %0, 4), %%mm0   \n\t" /* Y0Y1Y2Y3Y4Y5Y6Y7 */
                "movq         (%2, %0), %%mm1   \n\t" /* U0U1U2U3U4U5U6U7 */
                "movq         (%3, %0), %%mm2   \n\t" /* V0V1V2V3V4V5V6V7 */
                "movq            %%mm0, %%mm3   \n\t" /* Y0Y1Y2Y3Y4Y5Y6Y7 */
                "movq            %%mm1, %%mm4   \n\t" /* U0U1U2U3U4U5U6U7 */
                "movq            %%mm2, %%mm5   \n\t" /* V0V1V2V3V4V5V6V7 */
                "punpcklbw       %%mm1, %%mm1   \n\t" /* U0U0 U1U1 U2U2 U3U3 */
                "punpcklbw       %%mm2, %%mm2   \n\t" /* V0V0 V1V1 V2V2 V3V3 */
                "punpckhbw       %%mm4, %%mm4   \n\t" /* U4U4 U5U5 U6U6 U7U7 */
                "punpckhbw       %%mm5, %%mm5   \n\t" /* V4V4 V5V5 V6V6 V7V7 */

                "movq            %%mm1, %%mm6   \n\t"
                "punpcklbw       %%mm2, %%mm1   \n\t" /* U0V0 U0V0 U1V1 U1V1*/
                "punpcklbw       %%mm1, %%mm0   \n\t" /* Y0U0 Y1V0 Y2U0 Y3V0*/
                "punpckhbw       %%mm1, %%mm3   \n\t" /* Y4U1 Y5V1 Y6U1 Y7V1*/
                MOVNTQ"          %%mm0,  (%4, %0, 8)    \n\t"
                MOVNTQ"          %%mm3, 8(%4, %0, 8)    \n\t"

                "punpckhbw       %%mm2, %%mm6   \n\t" /* U2V2 U2V2 U3V3 U3V3*/
                "movq     8(%1, %0, 4), %%mm0   \n\t"
                "movq            %%mm0, %%mm3   \n\t"
                "punpcklbw       %%mm6, %%mm0   \n\t" /* Y U2 Y V2 Y U2 Y V2*/
                "punpckhbw       %%mm6, %%mm3   \n\t" /* Y U3 Y V3 Y U3 Y V3*/
                MOVNTQ"          %%mm0, 16(%4, %0, 8)   \n\t"
                MOVNTQ"          %%mm3, 24(%4, %0, 8)   \n\t"

                "movq            %%mm4, %%mm6   \n\t"
                "movq    16(%1, %0, 4), %%mm0   \n\t"
                "movq            %%mm0, %%mm3   \n\t"
                "punpcklbw       %%mm5, %%mm4   \n\t"
                "punpcklbw       %%mm4, %%mm0   \n\t" /* Y U4 Y V4 Y U4 Y V4*/
                "punpckhbw       %%mm4, %%mm3   \n\t" /* Y U5 Y V5 Y U5 Y V5*/
                MOVNTQ"          %%mm0, 32(%4, %0, 8)   \n\t"
                MOVNTQ"          %%mm3, 40(%4, %0, 8)   \n\t"

                "punpckhbw       %%mm5, %%mm6   \n\t"
                "movq    24(%1, %0, 4), %%mm0   \n\t"
                "movq            %%mm0, %%mm3   \n\t"
                "punpcklbw       %%mm6, %%mm0   \n\t" /* Y U6 Y V6 Y U6 Y V6*/
                "punpckhbw       %%mm6, %%mm3   \n\t" /* Y U7 Y V7 Y U7 Y V7*/
                MOVNTQ"          %%mm0, 48(%4, %0, 8)   \n\t"
                MOVNTQ"          %%mm3, 56(%4, %0, 8)   \n\t"

                : "+r" (x)
                : "r"(yp), "r" (up), "r"(vp), "r"(d)
                :"memory");
        }
#endif
        for (; x<w; x++) {
            const long x2 = x<<2;
            d[8*x+0] = yp[x2];
            d[8*x+1] = up[x];
            d[8*x+2] = yp[x2+1];
            d[8*x+3] = vp[x];
            d[8*x+4] = yp[x2+2];
            d[8*x+5] = up[x];
            d[8*x+6] = yp[x2+3];
            d[8*x+7] = vp[x];
        }
    }
#if COMPILE_TEMPLATE_MMX
    __asm__(
            EMMS"       \n\t"
            SFENCE"     \n\t"
            ::: "memory"
        );
#endif
}

static void RENAME(extract_even)(const uint8_t *src, uint8_t *dst, x86_reg count)
{
    dst +=   count;
    src += 2*count;
    count= - count;

#if COMPILE_TEMPLATE_MMX
    if(count <= -16) {
        count += 15;
        __asm__ volatile(
            "pcmpeqw       %%mm7, %%mm7        \n\t"
            "psrlw            $8, %%mm7        \n\t"
            "1:                                \n\t"
            "movq -30(%1, %0, 2), %%mm0        \n\t"
            "movq -22(%1, %0, 2), %%mm1        \n\t"
            "movq -14(%1, %0, 2), %%mm2        \n\t"
            "movq  -6(%1, %0, 2), %%mm3        \n\t"
            "pand          %%mm7, %%mm0        \n\t"
            "pand          %%mm7, %%mm1        \n\t"
            "pand          %%mm7, %%mm2        \n\t"
            "pand          %%mm7, %%mm3        \n\t"
            "packuswb      %%mm1, %%mm0        \n\t"
            "packuswb      %%mm3, %%mm2        \n\t"
            MOVNTQ"        %%mm0,-15(%2, %0)   \n\t"
            MOVNTQ"        %%mm2,- 7(%2, %0)   \n\t"
            "add             $16, %0           \n\t"
            " js 1b                            \n\t"
            : "+r"(count)
            : "r"(src), "r"(dst)
        );
        count -= 15;
    }
#endif
    while(count<0) {
        dst[count]= src[2*count];
        count++;
    }
}

static void RENAME(extract_even2)(const uint8_t *src, uint8_t *dst0, uint8_t *dst1, x86_reg count)
{
    dst0+=   count;
    dst1+=   count;
    src += 4*count;
    count= - count;
#if COMPILE_TEMPLATE_MMX
    if(count <= -8) {
        count += 7;
        __asm__ volatile(
            "pcmpeqw       %%mm7, %%mm7        \n\t"
            "psrlw            $8, %%mm7        \n\t"
            "1:                                \n\t"
            "movq -28(%1, %0, 4), %%mm0        \n\t"
            "movq -20(%1, %0, 4), %%mm1        \n\t"
            "movq -12(%1, %0, 4), %%mm2        \n\t"
            "movq  -4(%1, %0, 4), %%mm3        \n\t"
            "pand          %%mm7, %%mm0        \n\t"
            "pand          %%mm7, %%mm1        \n\t"
            "pand          %%mm7, %%mm2        \n\t"
            "pand          %%mm7, %%mm3        \n\t"
            "packuswb      %%mm1, %%mm0        \n\t"
            "packuswb      %%mm3, %%mm2        \n\t"
            "movq          %%mm0, %%mm1        \n\t"
            "movq          %%mm2, %%mm3        \n\t"
            "psrlw            $8, %%mm0        \n\t"
            "psrlw            $8, %%mm2        \n\t"
            "pand          %%mm7, %%mm1        \n\t"
            "pand          %%mm7, %%mm3        \n\t"
            "packuswb      %%mm2, %%mm0        \n\t"
            "packuswb      %%mm3, %%mm1        \n\t"
            MOVNTQ"        %%mm0,- 7(%3, %0)   \n\t"
            MOVNTQ"        %%mm1,- 7(%2, %0)   \n\t"
            "add              $8, %0           \n\t"
            " js 1b                            \n\t"
            : "+r"(count)
            : "r"(src), "r"(dst0), "r"(dst1)
        );
        count -= 7;
    }
#endif
    while(count<0) {
        dst0[count]= src[4*count+0];
        dst1[count]= src[4*count+2];
        count++;
    }
}

static void RENAME(extract_even2avg)(const uint8_t *src0, const uint8_t *src1, uint8_t *dst0, uint8_t *dst1, x86_reg count)
{
    dst0 +=   count;
    dst1 +=   count;
    src0 += 4*count;
    src1 += 4*count;
    count= - count;
#ifdef PAVGB
    if(count <= -8) {
        count += 7;
        __asm__ volatile(
            "pcmpeqw        %%mm7, %%mm7        \n\t"
            "psrlw             $8, %%mm7        \n\t"
            "1:                                \n\t"
            "movq  -28(%1, %0, 4), %%mm0        \n\t"
            "movq  -20(%1, %0, 4), %%mm1        \n\t"
            "movq  -12(%1, %0, 4), %%mm2        \n\t"
            "movq   -4(%1, %0, 4), %%mm3        \n\t"
            PAVGB" -28(%2, %0, 4), %%mm0        \n\t"
            PAVGB" -20(%2, %0, 4), %%mm1        \n\t"
            PAVGB" -12(%2, %0, 4), %%mm2        \n\t"
            PAVGB" - 4(%2, %0, 4), %%mm3        \n\t"
            "pand           %%mm7, %%mm0        \n\t"
            "pand           %%mm7, %%mm1        \n\t"
            "pand           %%mm7, %%mm2        \n\t"
            "pand           %%mm7, %%mm3        \n\t"
            "packuswb       %%mm1, %%mm0        \n\t"
            "packuswb       %%mm3, %%mm2        \n\t"
            "movq           %%mm0, %%mm1        \n\t"
            "movq           %%mm2, %%mm3        \n\t"
            "psrlw             $8, %%mm0        \n\t"
            "psrlw             $8, %%mm2        \n\t"
            "pand           %%mm7, %%mm1        \n\t"
            "pand           %%mm7, %%mm3        \n\t"
            "packuswb       %%mm2, %%mm0        \n\t"
            "packuswb       %%mm3, %%mm1        \n\t"
            MOVNTQ"         %%mm0,- 7(%4, %0)   \n\t"
            MOVNTQ"         %%mm1,- 7(%3, %0)   \n\t"
            "add               $8, %0           \n\t"
            " js 1b                            \n\t"
            : "+r"(count)
            : "r"(src0), "r"(src1), "r"(dst0), "r"(dst1)
        );
        count -= 7;
    }
#endif
    while(count<0) {
        dst0[count]= (src0[4*count+0]+src1[4*count+0])>>1;
        dst1[count]= (src0[4*count+2]+src1[4*count+2])>>1;
        count++;
    }
}

static void RENAME(extract_odd2)(const uint8_t *src, uint8_t *dst0, uint8_t *dst1, x86_reg count)
{
    dst0+=   count;
    dst1+=   count;
    src += 4*count;
    count= - count;
#if COMPILE_TEMPLATE_MMX
    if(count <= -8) {
        count += 7;
        __asm__ volatile(
            "pcmpeqw       %%mm7, %%mm7        \n\t"
            "psrlw            $8, %%mm7        \n\t"
            "1:                                \n\t"
            "movq -28(%1, %0, 4), %%mm0        \n\t"
            "movq -20(%1, %0, 4), %%mm1        \n\t"
            "movq -12(%1, %0, 4), %%mm2        \n\t"
            "movq  -4(%1, %0, 4), %%mm3        \n\t"
            "psrlw            $8, %%mm0        \n\t"
            "psrlw            $8, %%mm1        \n\t"
            "psrlw            $8, %%mm2        \n\t"
            "psrlw            $8, %%mm3        \n\t"
            "packuswb      %%mm1, %%mm0        \n\t"
            "packuswb      %%mm3, %%mm2        \n\t"
            "movq          %%mm0, %%mm1        \n\t"
            "movq          %%mm2, %%mm3        \n\t"
            "psrlw            $8, %%mm0        \n\t"
            "psrlw            $8, %%mm2        \n\t"
            "pand          %%mm7, %%mm1        \n\t"
            "pand          %%mm7, %%mm3        \n\t"
            "packuswb      %%mm2, %%mm0        \n\t"
            "packuswb      %%mm3, %%mm1        \n\t"
            MOVNTQ"        %%mm0,- 7(%3, %0)   \n\t"
            MOVNTQ"        %%mm1,- 7(%2, %0)   \n\t"
            "add              $8, %0           \n\t"
            " js 1b                            \n\t"
            : "+r"(count)
            : "r"(src), "r"(dst0), "r"(dst1)
        );
        count -= 7;
    }
#endif
    src++;
    while(count<0) {
        dst0[count]= src[4*count+0];
        dst1[count]= src[4*count+2];
        count++;
    }
}

static void RENAME(extract_odd2avg)(const uint8_t *src0, const uint8_t *src1, uint8_t *dst0, uint8_t *dst1, x86_reg count)
{
    dst0 +=   count;
    dst1 +=   count;
    src0 += 4*count;
    src1 += 4*count;
    count= - count;
#ifdef PAVGB
    if(count <= -8) {
        count += 7;
        __asm__ volatile(
            "pcmpeqw        %%mm7, %%mm7        \n\t"
            "psrlw             $8, %%mm7        \n\t"
            "1:                                \n\t"
            "movq  -28(%1, %0, 4), %%mm0        \n\t"
            "movq  -20(%1, %0, 4), %%mm1        \n\t"
            "movq  -12(%1, %0, 4), %%mm2        \n\t"
            "movq   -4(%1, %0, 4), %%mm3        \n\t"
            PAVGB" -28(%2, %0, 4), %%mm0        \n\t"
            PAVGB" -20(%2, %0, 4), %%mm1        \n\t"
            PAVGB" -12(%2, %0, 4), %%mm2        \n\t"
            PAVGB" - 4(%2, %0, 4), %%mm3        \n\t"
            "psrlw             $8, %%mm0        \n\t"
            "psrlw             $8, %%mm1        \n\t"
            "psrlw             $8, %%mm2        \n\t"
            "psrlw             $8, %%mm3        \n\t"
            "packuswb       %%mm1, %%mm0        \n\t"
            "packuswb       %%mm3, %%mm2        \n\t"
            "movq           %%mm0, %%mm1        \n\t"
            "movq           %%mm2, %%mm3        \n\t"
            "psrlw             $8, %%mm0        \n\t"
            "psrlw             $8, %%mm2        \n\t"
            "pand           %%mm7, %%mm1        \n\t"
            "pand           %%mm7, %%mm3        \n\t"
            "packuswb       %%mm2, %%mm0        \n\t"
            "packuswb       %%mm3, %%mm1        \n\t"
            MOVNTQ"         %%mm0,- 7(%4, %0)   \n\t"
            MOVNTQ"         %%mm1,- 7(%3, %0)   \n\t"
            "add               $8, %0           \n\t"
            " js 1b                            \n\t"
            : "+r"(count)
            : "r"(src0), "r"(src1), "r"(dst0), "r"(dst1)
        );
        count -= 7;
    }
#endif
    src0++;
    src1++;
    while(count<0) {
        dst0[count]= (src0[4*count+0]+src1[4*count+0])>>1;
        dst1[count]= (src0[4*count+2]+src1[4*count+2])>>1;
        count++;
    }
}

static void RENAME(yuyvtoyuv420)(uint8_t *ydst, uint8_t *udst, uint8_t *vdst, const uint8_t *src,
                                      long width, long height,
                                      long lumStride, long chromStride, long srcStride)
{
    long y;
    const long chromWidth= -((-width)>>1);

    for (y=0; y<height; y++) {
        RENAME(extract_even)(src, ydst, width);
        if(y&1) {
            RENAME(extract_odd2avg)(src-srcStride, src, udst, vdst, chromWidth);
            udst+= chromStride;
            vdst+= chromStride;
        }

        src += srcStride;
        ydst+= lumStride;
    }
#if COMPILE_TEMPLATE_MMX
    __asm__(
            EMMS"       \n\t"
            SFENCE"     \n\t"
            ::: "memory"
        );
#endif
}

static void RENAME(yuyvtoyuv422)(uint8_t *ydst, uint8_t *udst, uint8_t *vdst, const uint8_t *src,
                                      long width, long height,
                                      long lumStride, long chromStride, long srcStride)
{
    long y;
    const long chromWidth= -((-width)>>1);

    for (y=0; y<height; y++) {
        RENAME(extract_even)(src, ydst, width);
        RENAME(extract_odd2)(src, udst, vdst, chromWidth);

        src += srcStride;
        ydst+= lumStride;
        udst+= chromStride;
        vdst+= chromStride;
    }
#if COMPILE_TEMPLATE_MMX
    __asm__(
            EMMS"       \n\t"
            SFENCE"     \n\t"
            ::: "memory"
        );
#endif
}

static void RENAME(uyvytoyuv420)(uint8_t *ydst, uint8_t *udst, uint8_t *vdst, const uint8_t *src,
                                      long width, long height,
                                      long lumStride, long chromStride, long srcStride)
{
    long y;
    const long chromWidth= -((-width)>>1);

    for (y=0; y<height; y++) {
        RENAME(extract_even)(src+1, ydst, width);
        if(y&1) {
            RENAME(extract_even2avg)(src-srcStride, src, udst, vdst, chromWidth);
            udst+= chromStride;
            vdst+= chromStride;
        }

        src += srcStride;
        ydst+= lumStride;
    }
#if COMPILE_TEMPLATE_MMX
    __asm__(
            EMMS"       \n\t"
            SFENCE"     \n\t"
            ::: "memory"
        );
#endif
}

static void RENAME(uyvytoyuv422)(uint8_t *ydst, uint8_t *udst, uint8_t *vdst, const uint8_t *src,
                                      long width, long height,
                                      long lumStride, long chromStride, long srcStride)
{
    long y;
    const long chromWidth= -((-width)>>1);

    for (y=0; y<height; y++) {
        RENAME(extract_even)(src+1, ydst, width);
        RENAME(extract_even2)(src, udst, vdst, chromWidth);

        src += srcStride;
        ydst+= lumStride;
        udst+= chromStride;
        vdst+= chromStride;
    }
#if COMPILE_TEMPLATE_MMX
    __asm__(
            EMMS"       \n\t"
            SFENCE"     \n\t"
            ::: "memory"
        );
#endif
}

static inline void RENAME(rgb2rgb_init)(void)
{
    rgb15to16       = RENAME(rgb15to16);
    rgb15tobgr24    = RENAME(rgb15tobgr24);
    rgb15to32       = RENAME(rgb15to32);
    rgb16tobgr24    = RENAME(rgb16tobgr24);
    rgb16to32       = RENAME(rgb16to32);
    rgb16to15       = RENAME(rgb16to15);
    rgb24tobgr16    = RENAME(rgb24tobgr16);
    rgb24tobgr15    = RENAME(rgb24tobgr15);
    rgb24tobgr32    = RENAME(rgb24tobgr32);
    rgb32to16       = RENAME(rgb32to16);
    rgb32to15       = RENAME(rgb32to15);
    rgb32tobgr24    = RENAME(rgb32tobgr24);
    rgb24to15       = RENAME(rgb24to15);
    rgb24to16       = RENAME(rgb24to16);
    rgb24tobgr24    = RENAME(rgb24tobgr24);
    shuffle_bytes_2103 = RENAME(shuffle_bytes_2103);
    rgb32tobgr16    = RENAME(rgb32tobgr16);
    rgb32tobgr15    = RENAME(rgb32tobgr15);
    yv12toyuy2      = RENAME(yv12toyuy2);
    yv12touyvy      = RENAME(yv12touyvy);
    yuv422ptoyuy2   = RENAME(yuv422ptoyuy2);
    yuv422ptouyvy   = RENAME(yuv422ptouyvy);
    yuy2toyv12      = RENAME(yuy2toyv12);
    planar2x        = RENAME(planar2x);
    rgb24toyv12     = RENAME(rgb24toyv12);
    interleaveBytes = RENAME(interleaveBytes);
    vu9_to_vu12     = RENAME(vu9_to_vu12);
    yvu9_to_yuy2    = RENAME(yvu9_to_yuy2);

    uyvytoyuv420    = RENAME(uyvytoyuv420);
    uyvytoyuv422    = RENAME(uyvytoyuv422);
    yuyvtoyuv420    = RENAME(yuyvtoyuv420);
    yuyvtoyuv422    = RENAME(yuyvtoyuv422);
}
