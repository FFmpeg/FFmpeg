/*
 *
 *  rgb2rgb.c, Software RGB to RGB convertor
 *  pluralize by Software PAL8 to RGB convertor
 *               Software YUV to YUV convertor
 *               Software YUV to RGB convertor
 *  Written by Nick Kurshev.
 *  palette & YUV & runtime CPU stuff by Michael (michaelni@gmx.at)
 *  lot of big-endian byteorder fixes by Alex Beregszaszi
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * 
 * the C code (not assembly, mmx, ...) of this file can be used
 * under the LGPL license too
 */

#include <stddef.h>
#include <inttypes.h> /* for __WORDSIZE */

#ifndef __WORDSIZE
// #warning You have misconfigured system and probably will lose performance!
#define __WORDSIZE MP_WORDSIZE
#endif

#undef PREFETCH
#undef MOVNTQ
#undef EMMS
#undef SFENCE
#undef MMREG_SIZE
#undef PREFETCHW
#undef PAVGB

#ifdef HAVE_SSE2
#define MMREG_SIZE 16
#else
#define MMREG_SIZE 8
#endif

#ifdef HAVE_3DNOW
#define PREFETCH  "prefetch"
#define PREFETCHW "prefetchw"
#define PAVGB	  "pavgusb"
#elif defined ( HAVE_MMX2 )
#define PREFETCH "prefetchnta"
#define PREFETCHW "prefetcht0"
#define PAVGB	  "pavgb"
#else
#ifdef __APPLE__
#define PREFETCH "#"
#define PREFETCHW "#"
#else
#define PREFETCH  " # nop"
#define PREFETCHW " # nop"
#endif
#endif

#ifdef HAVE_3DNOW
/* On K6 femms is faster of emms. On K7 femms is directly mapped on emms. */
#define EMMS     "femms"
#else
#define EMMS     "emms"
#endif

#ifdef HAVE_MMX2
#define MOVNTQ "movntq"
#define SFENCE "sfence"
#else
#define MOVNTQ "movq"
#define SFENCE " # nop"
#endif

static inline void RENAME(rgb24to32)(const uint8_t *src,uint8_t *dst,long src_size)
{
  uint8_t *dest = dst;
  const uint8_t *s = src;
  const uint8_t *end;
#ifdef HAVE_MMX
  const uint8_t *mm_end;
#endif
  end = s + src_size;
#ifdef HAVE_MMX
  __asm __volatile(PREFETCH"	%0"::"m"(*s):"memory");
  mm_end = end - 23;
  __asm __volatile("movq	%0, %%mm7"::"m"(mask32):"memory");
  while(s < mm_end)
  {
    __asm __volatile(
	PREFETCH"	32%1\n\t"
	"movd	%1, %%mm0\n\t"
	"punpckldq 3%1, %%mm0\n\t"
	"movd	6%1, %%mm1\n\t"
	"punpckldq 9%1, %%mm1\n\t"
	"movd	12%1, %%mm2\n\t"
	"punpckldq 15%1, %%mm2\n\t"
	"movd	18%1, %%mm3\n\t"
	"punpckldq 21%1, %%mm3\n\t"
	"pand	%%mm7, %%mm0\n\t"
	"pand	%%mm7, %%mm1\n\t"
	"pand	%%mm7, %%mm2\n\t"
	"pand	%%mm7, %%mm3\n\t"
	MOVNTQ"	%%mm0, %0\n\t"
	MOVNTQ"	%%mm1, 8%0\n\t"
	MOVNTQ"	%%mm2, 16%0\n\t"
	MOVNTQ"	%%mm3, 24%0"
	:"=m"(*dest)
	:"m"(*s)
	:"memory");
    dest += 32;
    s += 24;
  }
  __asm __volatile(SFENCE:::"memory");
  __asm __volatile(EMMS:::"memory");
#endif
  while(s < end)
  {
#ifdef WORDS_BIGENDIAN
    /* RGB24 (= R,G,B) -> RGB32 (= A,B,G,R) */
    *dest++ = 0;
    *dest++ = s[2];
    *dest++ = s[1];
    *dest++ = s[0];
    s+=3;
#else
    *dest++ = *s++;
    *dest++ = *s++;
    *dest++ = *s++;
    *dest++ = 0;
#endif
  }
}

static inline void RENAME(rgb32to24)(const uint8_t *src,uint8_t *dst,long src_size)
{
  uint8_t *dest = dst;
  const uint8_t *s = src;
  const uint8_t *end;
#ifdef HAVE_MMX
  const uint8_t *mm_end;
#endif
  end = s + src_size;
#ifdef HAVE_MMX
  __asm __volatile(PREFETCH"	%0"::"m"(*s):"memory");
  mm_end = end - 31;
  while(s < mm_end)
  {
    __asm __volatile(
	PREFETCH"	32%1\n\t"
	"movq	%1, %%mm0\n\t"
	"movq	8%1, %%mm1\n\t"
	"movq	16%1, %%mm4\n\t"
	"movq	24%1, %%mm5\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"movq	%%mm4, %%mm6\n\t"
	"movq	%%mm5, %%mm7\n\t"
	"psrlq	$8, %%mm2\n\t"
	"psrlq	$8, %%mm3\n\t"
	"psrlq	$8, %%mm6\n\t"
	"psrlq	$8, %%mm7\n\t"
	"pand	%2, %%mm0\n\t"
	"pand	%2, %%mm1\n\t"
	"pand	%2, %%mm4\n\t"
	"pand	%2, %%mm5\n\t"
	"pand	%3, %%mm2\n\t"
	"pand	%3, %%mm3\n\t"
	"pand	%3, %%mm6\n\t"
	"pand	%3, %%mm7\n\t"
	"por	%%mm2, %%mm0\n\t"
	"por	%%mm3, %%mm1\n\t"
	"por	%%mm6, %%mm4\n\t"
	"por	%%mm7, %%mm5\n\t"

	"movq	%%mm1, %%mm2\n\t"
	"movq	%%mm4, %%mm3\n\t"
	"psllq	$48, %%mm2\n\t"
	"psllq	$32, %%mm3\n\t"
	"pand	%4, %%mm2\n\t"
	"pand	%5, %%mm3\n\t"
	"por	%%mm2, %%mm0\n\t"
	"psrlq	$16, %%mm1\n\t"
	"psrlq	$32, %%mm4\n\t"
	"psllq	$16, %%mm5\n\t"
	"por	%%mm3, %%mm1\n\t"
	"pand	%6, %%mm5\n\t"
	"por	%%mm5, %%mm4\n\t"

	MOVNTQ"	%%mm0, %0\n\t"
	MOVNTQ"	%%mm1, 8%0\n\t"
	MOVNTQ"	%%mm4, 16%0"
	:"=m"(*dest)
	:"m"(*s),"m"(mask24l),
	 "m"(mask24h),"m"(mask24hh),"m"(mask24hhh),"m"(mask24hhhh)
	:"memory");
    dest += 24;
    s += 32;
  }
  __asm __volatile(SFENCE:::"memory");
  __asm __volatile(EMMS:::"memory");
#endif
  while(s < end)
  {
#ifdef WORDS_BIGENDIAN
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
 Original by Strepto/Astral
 ported to gcc & bugfixed : A'rpi
 MMX2, 3DNOW optimization by Nick Kurshev
 32bit c version, and and&add trick by Michael Niedermayer
*/
static inline void RENAME(rgb15to16)(const uint8_t *src,uint8_t *dst,long src_size)
{
  register const uint8_t* s=src;
  register uint8_t* d=dst;
  register const uint8_t *end;
  const uint8_t *mm_end;
  end = s + src_size;
#ifdef HAVE_MMX
  __asm __volatile(PREFETCH"	%0"::"m"(*s));
  __asm __volatile("movq	%0, %%mm4"::"m"(mask15s));
  mm_end = end - 15;
  while(s<mm_end)
  {
	__asm __volatile(
		PREFETCH"	32%1\n\t"
		"movq	%1, %%mm0\n\t"
		"movq	8%1, %%mm2\n\t"
		"movq	%%mm0, %%mm1\n\t"
		"movq	%%mm2, %%mm3\n\t"
		"pand	%%mm4, %%mm0\n\t"
		"pand	%%mm4, %%mm2\n\t"
		"paddw	%%mm1, %%mm0\n\t"
		"paddw	%%mm3, %%mm2\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		MOVNTQ"	%%mm2, 8%0"
		:"=m"(*d)
		:"m"(*s)
		);
	d+=16;
	s+=16;
  }
  __asm __volatile(SFENCE:::"memory");
  __asm __volatile(EMMS:::"memory");
#endif
    mm_end = end - 3;
    while(s < mm_end)
    {
	register unsigned x= *((uint32_t *)s);
	*((uint32_t *)d) = (x&0x7FFF7FFF) + (x&0x7FE07FE0);
	d+=4;
	s+=4;
    }
    if(s < end)
    {
	register unsigned short x= *((uint16_t *)s);
	*((uint16_t *)d) = (x&0x7FFF) + (x&0x7FE0);
    }
}

static inline void RENAME(rgb16to15)(const uint8_t *src,uint8_t *dst,long src_size)
{
  register const uint8_t* s=src;
  register uint8_t* d=dst;
  register const uint8_t *end;
  const uint8_t *mm_end;
  end = s + src_size;
#ifdef HAVE_MMX
  __asm __volatile(PREFETCH"	%0"::"m"(*s));
  __asm __volatile("movq	%0, %%mm7"::"m"(mask15rg));
  __asm __volatile("movq	%0, %%mm6"::"m"(mask15b));
  mm_end = end - 15;
  while(s<mm_end)
  {
	__asm __volatile(
		PREFETCH"	32%1\n\t"
		"movq	%1, %%mm0\n\t"
		"movq	8%1, %%mm2\n\t"
		"movq	%%mm0, %%mm1\n\t"
		"movq	%%mm2, %%mm3\n\t"
		"psrlq	$1, %%mm0\n\t"
		"psrlq	$1, %%mm2\n\t"
		"pand	%%mm7, %%mm0\n\t"
		"pand	%%mm7, %%mm2\n\t"
		"pand	%%mm6, %%mm1\n\t"
		"pand	%%mm6, %%mm3\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm3, %%mm2\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		MOVNTQ"	%%mm2, 8%0"
		:"=m"(*d)
		:"m"(*s)
		);
	d+=16;
	s+=16;
  }
  __asm __volatile(SFENCE:::"memory");
  __asm __volatile(EMMS:::"memory");
#endif
    mm_end = end - 3;
    while(s < mm_end)
    {
	register uint32_t x= *((uint32_t *)s);
	*((uint32_t *)d) = ((x>>1)&0x7FE07FE0) | (x&0x001F001F);
	s+=4;
	d+=4;
    }
    if(s < end)
    {
	register uint16_t x= *((uint16_t *)s);
	*((uint16_t *)d) = ((x>>1)&0x7FE0) | (x&0x001F);
	s+=2;
	d+=2;
    }
}

static inline void RENAME(rgb32to16)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint8_t *s = src;
	const uint8_t *end;
#ifdef HAVE_MMX
	const uint8_t *mm_end;
#endif
	uint16_t *d = (uint16_t *)dst;
	end = s + src_size;
#ifdef HAVE_MMX
	mm_end = end - 15;
#if 1 //is faster only if multiplies are reasonable fast (FIXME figure out on which cpus this is faster, on Athlon its slightly faster)
	asm volatile(
		"movq %3, %%mm5			\n\t"
		"movq %4, %%mm6			\n\t"
		"movq %5, %%mm7			\n\t"
		ASMALIGN(4)
		"1:				\n\t"
		PREFETCH" 32(%1)		\n\t"
		"movd	(%1), %%mm0		\n\t"
		"movd	4(%1), %%mm3		\n\t"
		"punpckldq 8(%1), %%mm0		\n\t"
		"punpckldq 12(%1), %%mm3	\n\t"
		"movq %%mm0, %%mm1		\n\t"
		"movq %%mm3, %%mm4		\n\t"
		"pand %%mm6, %%mm0		\n\t"
		"pand %%mm6, %%mm3		\n\t"
		"pmaddwd %%mm7, %%mm0		\n\t"
		"pmaddwd %%mm7, %%mm3		\n\t"
		"pand %%mm5, %%mm1		\n\t"
		"pand %%mm5, %%mm4		\n\t"
		"por %%mm1, %%mm0		\n\t"	
		"por %%mm4, %%mm3		\n\t"
		"psrld $5, %%mm0		\n\t"
		"pslld $11, %%mm3		\n\t"
		"por %%mm3, %%mm0		\n\t"
		MOVNTQ"	%%mm0, (%0)		\n\t"
		"add $16, %1			\n\t"
		"add $8, %0			\n\t"
		"cmp %2, %1			\n\t"
		" jb 1b				\n\t"
		: "+r" (d), "+r"(s)
		: "r" (mm_end), "m" (mask3216g), "m" (mask3216br), "m" (mul3216)
	);
#else
	__asm __volatile(PREFETCH"	%0"::"m"(*src):"memory");
	__asm __volatile(
	    "movq	%0, %%mm7\n\t"
	    "movq	%1, %%mm6\n\t"
	    ::"m"(red_16mask),"m"(green_16mask));
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movd	%1, %%mm0\n\t"
		"movd	4%1, %%mm3\n\t"
		"punpckldq 8%1, %%mm0\n\t"
		"punpckldq 12%1, %%mm3\n\t"
		"movq	%%mm0, %%mm1\n\t"
		"movq	%%mm0, %%mm2\n\t"
		"movq	%%mm3, %%mm4\n\t"
		"movq	%%mm3, %%mm5\n\t"
		"psrlq	$3, %%mm0\n\t"
		"psrlq	$3, %%mm3\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%2, %%mm3\n\t"
		"psrlq	$5, %%mm1\n\t"
		"psrlq	$5, %%mm4\n\t"
		"pand	%%mm6, %%mm1\n\t"
		"pand	%%mm6, %%mm4\n\t"
		"psrlq	$8, %%mm2\n\t"
		"psrlq	$8, %%mm5\n\t"
		"pand	%%mm7, %%mm2\n\t"
		"pand	%%mm7, %%mm5\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm2, %%mm0\n\t"
		"por	%%mm5, %%mm3\n\t"
		"psllq	$16, %%mm3\n\t"
		"por	%%mm3, %%mm0\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		:"=m"(*d):"m"(*s),"m"(blue_16mask):"memory");
		d += 4;
		s += 16;
	}
#endif
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
		register int rgb = *(uint32_t*)s; s += 4;
		*d++ = ((rgb&0xFF)>>3) + ((rgb&0xFC00)>>5) + ((rgb&0xF80000)>>8);
	}
}

static inline void RENAME(rgb32tobgr16)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint8_t *s = src;
	const uint8_t *end;
#ifdef HAVE_MMX
	const uint8_t *mm_end;
#endif
	uint16_t *d = (uint16_t *)dst;
	end = s + src_size;
#ifdef HAVE_MMX
	__asm __volatile(PREFETCH"	%0"::"m"(*src):"memory");
	__asm __volatile(
	    "movq	%0, %%mm7\n\t"
	    "movq	%1, %%mm6\n\t"
	    ::"m"(red_16mask),"m"(green_16mask));
	mm_end = end - 15;
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movd	%1, %%mm0\n\t"
		"movd	4%1, %%mm3\n\t"
		"punpckldq 8%1, %%mm0\n\t"
		"punpckldq 12%1, %%mm3\n\t"
		"movq	%%mm0, %%mm1\n\t"
		"movq	%%mm0, %%mm2\n\t"
		"movq	%%mm3, %%mm4\n\t"
		"movq	%%mm3, %%mm5\n\t"
		"psllq	$8, %%mm0\n\t"
		"psllq	$8, %%mm3\n\t"
		"pand	%%mm7, %%mm0\n\t"
		"pand	%%mm7, %%mm3\n\t"
		"psrlq	$5, %%mm1\n\t"
		"psrlq	$5, %%mm4\n\t"
		"pand	%%mm6, %%mm1\n\t"
		"pand	%%mm6, %%mm4\n\t"
		"psrlq	$19, %%mm2\n\t"
		"psrlq	$19, %%mm5\n\t"
		"pand	%2, %%mm2\n\t"
		"pand	%2, %%mm5\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm2, %%mm0\n\t"
		"por	%%mm5, %%mm3\n\t"
		"psllq	$16, %%mm3\n\t"
		"por	%%mm3, %%mm0\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		:"=m"(*d):"m"(*s),"m"(blue_16mask):"memory");
		d += 4;
		s += 16;
	}
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
		register int rgb = *(uint32_t*)s; s += 4;
		*d++ = ((rgb&0xF8)<<8) + ((rgb&0xFC00)>>5) + ((rgb&0xF80000)>>19);
	}
}

static inline void RENAME(rgb32to15)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint8_t *s = src;
	const uint8_t *end;
#ifdef HAVE_MMX
	const uint8_t *mm_end;
#endif
	uint16_t *d = (uint16_t *)dst;
	end = s + src_size;
#ifdef HAVE_MMX
	mm_end = end - 15;
#if 1 //is faster only if multiplies are reasonable fast (FIXME figure out on which cpus this is faster, on Athlon its slightly faster)
	asm volatile(
		"movq %3, %%mm5			\n\t"
		"movq %4, %%mm6			\n\t"
		"movq %5, %%mm7			\n\t"
		ASMALIGN(4)
		"1:				\n\t"
		PREFETCH" 32(%1)		\n\t"
		"movd	(%1), %%mm0		\n\t"
		"movd	4(%1), %%mm3		\n\t"
		"punpckldq 8(%1), %%mm0		\n\t"
		"punpckldq 12(%1), %%mm3	\n\t"
		"movq %%mm0, %%mm1		\n\t"
		"movq %%mm3, %%mm4		\n\t"
		"pand %%mm6, %%mm0		\n\t"
		"pand %%mm6, %%mm3		\n\t"
		"pmaddwd %%mm7, %%mm0		\n\t"
		"pmaddwd %%mm7, %%mm3		\n\t"
		"pand %%mm5, %%mm1		\n\t"
		"pand %%mm5, %%mm4		\n\t"
		"por %%mm1, %%mm0		\n\t"	
		"por %%mm4, %%mm3		\n\t"
		"psrld $6, %%mm0		\n\t"
		"pslld $10, %%mm3		\n\t"
		"por %%mm3, %%mm0		\n\t"
		MOVNTQ"	%%mm0, (%0)		\n\t"
		"add $16, %1			\n\t"
		"add $8, %0			\n\t"
		"cmp %2, %1			\n\t"
		" jb 1b				\n\t"
		: "+r" (d), "+r"(s)
		: "r" (mm_end), "m" (mask3215g), "m" (mask3216br), "m" (mul3215)
	);
#else
	__asm __volatile(PREFETCH"	%0"::"m"(*src):"memory");
	__asm __volatile(
	    "movq	%0, %%mm7\n\t"
	    "movq	%1, %%mm6\n\t"
	    ::"m"(red_15mask),"m"(green_15mask));
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movd	%1, %%mm0\n\t"
		"movd	4%1, %%mm3\n\t"
		"punpckldq 8%1, %%mm0\n\t"
		"punpckldq 12%1, %%mm3\n\t"
		"movq	%%mm0, %%mm1\n\t"
		"movq	%%mm0, %%mm2\n\t"
		"movq	%%mm3, %%mm4\n\t"
		"movq	%%mm3, %%mm5\n\t"
		"psrlq	$3, %%mm0\n\t"
		"psrlq	$3, %%mm3\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%2, %%mm3\n\t"
		"psrlq	$6, %%mm1\n\t"
		"psrlq	$6, %%mm4\n\t"
		"pand	%%mm6, %%mm1\n\t"
		"pand	%%mm6, %%mm4\n\t"
		"psrlq	$9, %%mm2\n\t"
		"psrlq	$9, %%mm5\n\t"
		"pand	%%mm7, %%mm2\n\t"
		"pand	%%mm7, %%mm5\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm2, %%mm0\n\t"
		"por	%%mm5, %%mm3\n\t"
		"psllq	$16, %%mm3\n\t"
		"por	%%mm3, %%mm0\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		:"=m"(*d):"m"(*s),"m"(blue_15mask):"memory");
		d += 4;
		s += 16;
	}
#endif
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
		register int rgb = *(uint32_t*)s; s += 4;
		*d++ = ((rgb&0xFF)>>3) + ((rgb&0xF800)>>6) + ((rgb&0xF80000)>>9);
	}
}

static inline void RENAME(rgb32tobgr15)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint8_t *s = src;
	const uint8_t *end;
#ifdef HAVE_MMX
	const uint8_t *mm_end;
#endif
	uint16_t *d = (uint16_t *)dst;
	end = s + src_size;
#ifdef HAVE_MMX
	__asm __volatile(PREFETCH"	%0"::"m"(*src):"memory");
	__asm __volatile(
	    "movq	%0, %%mm7\n\t"
	    "movq	%1, %%mm6\n\t"
	    ::"m"(red_15mask),"m"(green_15mask));
	mm_end = end - 15;
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movd	%1, %%mm0\n\t"
		"movd	4%1, %%mm3\n\t"
		"punpckldq 8%1, %%mm0\n\t"
		"punpckldq 12%1, %%mm3\n\t"
		"movq	%%mm0, %%mm1\n\t"
		"movq	%%mm0, %%mm2\n\t"
		"movq	%%mm3, %%mm4\n\t"
		"movq	%%mm3, %%mm5\n\t"
		"psllq	$7, %%mm0\n\t"
		"psllq	$7, %%mm3\n\t"
		"pand	%%mm7, %%mm0\n\t"
		"pand	%%mm7, %%mm3\n\t"
		"psrlq	$6, %%mm1\n\t"
		"psrlq	$6, %%mm4\n\t"
		"pand	%%mm6, %%mm1\n\t"
		"pand	%%mm6, %%mm4\n\t"
		"psrlq	$19, %%mm2\n\t"
		"psrlq	$19, %%mm5\n\t"
		"pand	%2, %%mm2\n\t"
		"pand	%2, %%mm5\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm2, %%mm0\n\t"
		"por	%%mm5, %%mm3\n\t"
		"psllq	$16, %%mm3\n\t"
		"por	%%mm3, %%mm0\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		:"=m"(*d):"m"(*s),"m"(blue_15mask):"memory");
		d += 4;
		s += 16;
	}
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
		register int rgb = *(uint32_t*)s; s += 4;
		*d++ = ((rgb&0xF8)<<7) + ((rgb&0xF800)>>6) + ((rgb&0xF80000)>>19);
	}
}

static inline void RENAME(rgb24to16)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint8_t *s = src;
	const uint8_t *end;
#ifdef HAVE_MMX
	const uint8_t *mm_end;
#endif
	uint16_t *d = (uint16_t *)dst;
	end = s + src_size;
#ifdef HAVE_MMX
	__asm __volatile(PREFETCH"	%0"::"m"(*src):"memory");
	__asm __volatile(
	    "movq	%0, %%mm7\n\t"
	    "movq	%1, %%mm6\n\t"
	    ::"m"(red_16mask),"m"(green_16mask));
	mm_end = end - 11;
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movd	%1, %%mm0\n\t"
		"movd	3%1, %%mm3\n\t"
		"punpckldq 6%1, %%mm0\n\t"
		"punpckldq 9%1, %%mm3\n\t"
		"movq	%%mm0, %%mm1\n\t"
		"movq	%%mm0, %%mm2\n\t"
		"movq	%%mm3, %%mm4\n\t"
		"movq	%%mm3, %%mm5\n\t"
		"psrlq	$3, %%mm0\n\t"
		"psrlq	$3, %%mm3\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%2, %%mm3\n\t"
		"psrlq	$5, %%mm1\n\t"
		"psrlq	$5, %%mm4\n\t"
		"pand	%%mm6, %%mm1\n\t"
		"pand	%%mm6, %%mm4\n\t"
		"psrlq	$8, %%mm2\n\t"
		"psrlq	$8, %%mm5\n\t"
		"pand	%%mm7, %%mm2\n\t"
		"pand	%%mm7, %%mm5\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm2, %%mm0\n\t"
		"por	%%mm5, %%mm3\n\t"
		"psllq	$16, %%mm3\n\t"
		"por	%%mm3, %%mm0\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		:"=m"(*d):"m"(*s),"m"(blue_16mask):"memory");
		d += 4;
		s += 12;
	}
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
		const int b= *s++;
		const int g= *s++;
		const int r= *s++;
		*d++ = (b>>3) | ((g&0xFC)<<3) | ((r&0xF8)<<8);
	}
}

static inline void RENAME(rgb24tobgr16)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint8_t *s = src;
	const uint8_t *end;
#ifdef HAVE_MMX
	const uint8_t *mm_end;
#endif
	uint16_t *d = (uint16_t *)dst;
	end = s + src_size;
#ifdef HAVE_MMX
	__asm __volatile(PREFETCH"	%0"::"m"(*src):"memory");
	__asm __volatile(
	    "movq	%0, %%mm7\n\t"
	    "movq	%1, %%mm6\n\t"
	    ::"m"(red_16mask),"m"(green_16mask));
	mm_end = end - 15;
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movd	%1, %%mm0\n\t"
		"movd	3%1, %%mm3\n\t"
		"punpckldq 6%1, %%mm0\n\t"
		"punpckldq 9%1, %%mm3\n\t"
		"movq	%%mm0, %%mm1\n\t"
		"movq	%%mm0, %%mm2\n\t"
		"movq	%%mm3, %%mm4\n\t"
		"movq	%%mm3, %%mm5\n\t"
		"psllq	$8, %%mm0\n\t"
		"psllq	$8, %%mm3\n\t"
		"pand	%%mm7, %%mm0\n\t"
		"pand	%%mm7, %%mm3\n\t"
		"psrlq	$5, %%mm1\n\t"
		"psrlq	$5, %%mm4\n\t"
		"pand	%%mm6, %%mm1\n\t"
		"pand	%%mm6, %%mm4\n\t"
		"psrlq	$19, %%mm2\n\t"
		"psrlq	$19, %%mm5\n\t"
		"pand	%2, %%mm2\n\t"
		"pand	%2, %%mm5\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm2, %%mm0\n\t"
		"por	%%mm5, %%mm3\n\t"
		"psllq	$16, %%mm3\n\t"
		"por	%%mm3, %%mm0\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		:"=m"(*d):"m"(*s),"m"(blue_16mask):"memory");
		d += 4;
		s += 12;
	}
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
		const int r= *s++;
		const int g= *s++;
		const int b= *s++;
		*d++ = (b>>3) | ((g&0xFC)<<3) | ((r&0xF8)<<8);
	}
}

static inline void RENAME(rgb24to15)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint8_t *s = src;
	const uint8_t *end;
#ifdef HAVE_MMX
	const uint8_t *mm_end;
#endif
	uint16_t *d = (uint16_t *)dst;
	end = s + src_size;
#ifdef HAVE_MMX
	__asm __volatile(PREFETCH"	%0"::"m"(*src):"memory");
	__asm __volatile(
	    "movq	%0, %%mm7\n\t"
	    "movq	%1, %%mm6\n\t"
	    ::"m"(red_15mask),"m"(green_15mask));
	mm_end = end - 11;
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movd	%1, %%mm0\n\t"
		"movd	3%1, %%mm3\n\t"
		"punpckldq 6%1, %%mm0\n\t"
		"punpckldq 9%1, %%mm3\n\t"
		"movq	%%mm0, %%mm1\n\t"
		"movq	%%mm0, %%mm2\n\t"
		"movq	%%mm3, %%mm4\n\t"
		"movq	%%mm3, %%mm5\n\t"
		"psrlq	$3, %%mm0\n\t"
		"psrlq	$3, %%mm3\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%2, %%mm3\n\t"
		"psrlq	$6, %%mm1\n\t"
		"psrlq	$6, %%mm4\n\t"
		"pand	%%mm6, %%mm1\n\t"
		"pand	%%mm6, %%mm4\n\t"
		"psrlq	$9, %%mm2\n\t"
		"psrlq	$9, %%mm5\n\t"
		"pand	%%mm7, %%mm2\n\t"
		"pand	%%mm7, %%mm5\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm2, %%mm0\n\t"
		"por	%%mm5, %%mm3\n\t"
		"psllq	$16, %%mm3\n\t"
		"por	%%mm3, %%mm0\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		:"=m"(*d):"m"(*s),"m"(blue_15mask):"memory");
		d += 4;
		s += 12;
	}
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
		const int b= *s++;
		const int g= *s++;
		const int r= *s++;
		*d++ = (b>>3) | ((g&0xF8)<<2) | ((r&0xF8)<<7);
	}
}

static inline void RENAME(rgb24tobgr15)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint8_t *s = src;
	const uint8_t *end;
#ifdef HAVE_MMX
	const uint8_t *mm_end;
#endif
	uint16_t *d = (uint16_t *)dst;
	end = s + src_size;
#ifdef HAVE_MMX
	__asm __volatile(PREFETCH"	%0"::"m"(*src):"memory");
	__asm __volatile(
	    "movq	%0, %%mm7\n\t"
	    "movq	%1, %%mm6\n\t"
	    ::"m"(red_15mask),"m"(green_15mask));
	mm_end = end - 15;
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movd	%1, %%mm0\n\t"
		"movd	3%1, %%mm3\n\t"
		"punpckldq 6%1, %%mm0\n\t"
		"punpckldq 9%1, %%mm3\n\t"
		"movq	%%mm0, %%mm1\n\t"
		"movq	%%mm0, %%mm2\n\t"
		"movq	%%mm3, %%mm4\n\t"
		"movq	%%mm3, %%mm5\n\t"
		"psllq	$7, %%mm0\n\t"
		"psllq	$7, %%mm3\n\t"
		"pand	%%mm7, %%mm0\n\t"
		"pand	%%mm7, %%mm3\n\t"
		"psrlq	$6, %%mm1\n\t"
		"psrlq	$6, %%mm4\n\t"
		"pand	%%mm6, %%mm1\n\t"
		"pand	%%mm6, %%mm4\n\t"
		"psrlq	$19, %%mm2\n\t"
		"psrlq	$19, %%mm5\n\t"
		"pand	%2, %%mm2\n\t"
		"pand	%2, %%mm5\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm2, %%mm0\n\t"
		"por	%%mm5, %%mm3\n\t"
		"psllq	$16, %%mm3\n\t"
		"por	%%mm3, %%mm0\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		:"=m"(*d):"m"(*s),"m"(blue_15mask):"memory");
		d += 4;
		s += 12;
	}
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
		const int r= *s++;
		const int g= *s++;
		const int b= *s++;
		*d++ = (b>>3) | ((g&0xF8)<<2) | ((r&0xF8)<<7);
	}
}

/*
  I use here less accurate approximation by simply
 left-shifting the input
  value and filling the low order bits with
 zeroes. This method improves png's
  compression but this scheme cannot reproduce white exactly, since it does not
  generate an all-ones maximum value; the net effect is to darken the
  image slightly.

  The better method should be "left bit replication":

   4 3 2 1 0
   ---------
   1 1 0 1 1

   7 6 5 4 3  2 1 0
   ----------------
   1 1 0 1 1  1 1 0
   |=======|  |===|
       |      Leftmost Bits Repeated to Fill Open Bits
       |
   Original Bits
*/
static inline void RENAME(rgb15to24)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint16_t *end;
#ifdef HAVE_MMX
	const uint16_t *mm_end;
#endif
	uint8_t *d = (uint8_t *)dst;
	const uint16_t *s = (uint16_t *)src;
	end = s + src_size/2;
#ifdef HAVE_MMX
	__asm __volatile(PREFETCH"	%0"::"m"(*s):"memory");
	mm_end = end - 7;
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movq	%1, %%mm0\n\t"
		"movq	%1, %%mm1\n\t"
		"movq	%1, %%mm2\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%3, %%mm1\n\t"
		"pand	%4, %%mm2\n\t"
		"psllq	$3, %%mm0\n\t"
		"psrlq	$2, %%mm1\n\t"
		"psrlq	$7, %%mm2\n\t"
		"movq	%%mm0, %%mm3\n\t"
		"movq	%%mm1, %%mm4\n\t"
		"movq	%%mm2, %%mm5\n\t"
		"punpcklwd %5, %%mm0\n\t"
		"punpcklwd %5, %%mm1\n\t"
		"punpcklwd %5, %%mm2\n\t"
		"punpckhwd %5, %%mm3\n\t"
		"punpckhwd %5, %%mm4\n\t"
		"punpckhwd %5, %%mm5\n\t"
		"psllq	$8, %%mm1\n\t"
		"psllq	$16, %%mm2\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm2, %%mm0\n\t"
		"psllq	$8, %%mm4\n\t"
		"psllq	$16, %%mm5\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm5, %%mm3\n\t"

		"movq	%%mm0, %%mm6\n\t"
		"movq	%%mm3, %%mm7\n\t"
		
		"movq	8%1, %%mm0\n\t"
		"movq	8%1, %%mm1\n\t"
		"movq	8%1, %%mm2\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%3, %%mm1\n\t"
		"pand	%4, %%mm2\n\t"
		"psllq	$3, %%mm0\n\t"
		"psrlq	$2, %%mm1\n\t"
		"psrlq	$7, %%mm2\n\t"
		"movq	%%mm0, %%mm3\n\t"
		"movq	%%mm1, %%mm4\n\t"
		"movq	%%mm2, %%mm5\n\t"
		"punpcklwd %5, %%mm0\n\t"
		"punpcklwd %5, %%mm1\n\t"
		"punpcklwd %5, %%mm2\n\t"
		"punpckhwd %5, %%mm3\n\t"
		"punpckhwd %5, %%mm4\n\t"
		"punpckhwd %5, %%mm5\n\t"
		"psllq	$8, %%mm1\n\t"
		"psllq	$16, %%mm2\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm2, %%mm0\n\t"
		"psllq	$8, %%mm4\n\t"
		"psllq	$16, %%mm5\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm5, %%mm3\n\t"

		:"=m"(*d)
		:"m"(*s),"m"(mask15b),"m"(mask15g),"m"(mask15r), "m"(mmx_null)
		:"memory");
	    /* Borrowed 32 to 24 */
	    __asm __volatile(
		"movq	%%mm0, %%mm4\n\t"
		"movq	%%mm3, %%mm5\n\t"
		"movq	%%mm6, %%mm0\n\t"
		"movq	%%mm7, %%mm1\n\t"
		
		"movq	%%mm4, %%mm6\n\t"
		"movq	%%mm5, %%mm7\n\t"
		"movq	%%mm0, %%mm2\n\t"
		"movq	%%mm1, %%mm3\n\t"

		"psrlq	$8, %%mm2\n\t"
		"psrlq	$8, %%mm3\n\t"
		"psrlq	$8, %%mm6\n\t"
		"psrlq	$8, %%mm7\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%2, %%mm1\n\t"
		"pand	%2, %%mm4\n\t"
		"pand	%2, %%mm5\n\t"
		"pand	%3, %%mm2\n\t"
		"pand	%3, %%mm3\n\t"
		"pand	%3, %%mm6\n\t"
		"pand	%3, %%mm7\n\t"
		"por	%%mm2, %%mm0\n\t"
		"por	%%mm3, %%mm1\n\t"
		"por	%%mm6, %%mm4\n\t"
		"por	%%mm7, %%mm5\n\t"

		"movq	%%mm1, %%mm2\n\t"
		"movq	%%mm4, %%mm3\n\t"
		"psllq	$48, %%mm2\n\t"
		"psllq	$32, %%mm3\n\t"
		"pand	%4, %%mm2\n\t"
		"pand	%5, %%mm3\n\t"
		"por	%%mm2, %%mm0\n\t"
		"psrlq	$16, %%mm1\n\t"
		"psrlq	$32, %%mm4\n\t"
		"psllq	$16, %%mm5\n\t"
		"por	%%mm3, %%mm1\n\t"
		"pand	%6, %%mm5\n\t"
		"por	%%mm5, %%mm4\n\t"

		MOVNTQ"	%%mm0, %0\n\t"
		MOVNTQ"	%%mm1, 8%0\n\t"
		MOVNTQ"	%%mm4, 16%0"

		:"=m"(*d)
		:"m"(*s),"m"(mask24l),"m"(mask24h),"m"(mask24hh),"m"(mask24hhh),"m"(mask24hhhh)
		:"memory");
		d += 24;
		s += 8;
	}
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
		register uint16_t bgr;
		bgr = *s++;
		*d++ = (bgr&0x1F)<<3;
		*d++ = (bgr&0x3E0)>>2;
		*d++ = (bgr&0x7C00)>>7;
	}
}

static inline void RENAME(rgb16to24)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint16_t *end;
#ifdef HAVE_MMX
	const uint16_t *mm_end;
#endif
	uint8_t *d = (uint8_t *)dst;
	const uint16_t *s = (const uint16_t *)src;
	end = s + src_size/2;
#ifdef HAVE_MMX
	__asm __volatile(PREFETCH"	%0"::"m"(*s):"memory");
	mm_end = end - 7;
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movq	%1, %%mm0\n\t"
		"movq	%1, %%mm1\n\t"
		"movq	%1, %%mm2\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%3, %%mm1\n\t"
		"pand	%4, %%mm2\n\t"
		"psllq	$3, %%mm0\n\t"
		"psrlq	$3, %%mm1\n\t"
		"psrlq	$8, %%mm2\n\t"
		"movq	%%mm0, %%mm3\n\t"
		"movq	%%mm1, %%mm4\n\t"
		"movq	%%mm2, %%mm5\n\t"
		"punpcklwd %5, %%mm0\n\t"
		"punpcklwd %5, %%mm1\n\t"
		"punpcklwd %5, %%mm2\n\t"
		"punpckhwd %5, %%mm3\n\t"
		"punpckhwd %5, %%mm4\n\t"
		"punpckhwd %5, %%mm5\n\t"
		"psllq	$8, %%mm1\n\t"
		"psllq	$16, %%mm2\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm2, %%mm0\n\t"
		"psllq	$8, %%mm4\n\t"
		"psllq	$16, %%mm5\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm5, %%mm3\n\t"
		
		"movq	%%mm0, %%mm6\n\t"
		"movq	%%mm3, %%mm7\n\t"

		"movq	8%1, %%mm0\n\t"
		"movq	8%1, %%mm1\n\t"
		"movq	8%1, %%mm2\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%3, %%mm1\n\t"
		"pand	%4, %%mm2\n\t"
		"psllq	$3, %%mm0\n\t"
		"psrlq	$3, %%mm1\n\t"
		"psrlq	$8, %%mm2\n\t"
		"movq	%%mm0, %%mm3\n\t"
		"movq	%%mm1, %%mm4\n\t"
		"movq	%%mm2, %%mm5\n\t"
		"punpcklwd %5, %%mm0\n\t"
		"punpcklwd %5, %%mm1\n\t"
		"punpcklwd %5, %%mm2\n\t"
		"punpckhwd %5, %%mm3\n\t"
		"punpckhwd %5, %%mm4\n\t"
		"punpckhwd %5, %%mm5\n\t"
		"psllq	$8, %%mm1\n\t"
		"psllq	$16, %%mm2\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm2, %%mm0\n\t"
		"psllq	$8, %%mm4\n\t"
		"psllq	$16, %%mm5\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm5, %%mm3\n\t"
		:"=m"(*d)
		:"m"(*s),"m"(mask16b),"m"(mask16g),"m"(mask16r),"m"(mmx_null)		
		:"memory");
	    /* Borrowed 32 to 24 */
	    __asm __volatile(
		"movq	%%mm0, %%mm4\n\t"
		"movq	%%mm3, %%mm5\n\t"
		"movq	%%mm6, %%mm0\n\t"
		"movq	%%mm7, %%mm1\n\t"
		
		"movq	%%mm4, %%mm6\n\t"
		"movq	%%mm5, %%mm7\n\t"
		"movq	%%mm0, %%mm2\n\t"
		"movq	%%mm1, %%mm3\n\t"

		"psrlq	$8, %%mm2\n\t"
		"psrlq	$8, %%mm3\n\t"
		"psrlq	$8, %%mm6\n\t"
		"psrlq	$8, %%mm7\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%2, %%mm1\n\t"
		"pand	%2, %%mm4\n\t"
		"pand	%2, %%mm5\n\t"
		"pand	%3, %%mm2\n\t"
		"pand	%3, %%mm3\n\t"
		"pand	%3, %%mm6\n\t"
		"pand	%3, %%mm7\n\t"
		"por	%%mm2, %%mm0\n\t"
		"por	%%mm3, %%mm1\n\t"
		"por	%%mm6, %%mm4\n\t"
		"por	%%mm7, %%mm5\n\t"

		"movq	%%mm1, %%mm2\n\t"
		"movq	%%mm4, %%mm3\n\t"
		"psllq	$48, %%mm2\n\t"
		"psllq	$32, %%mm3\n\t"
		"pand	%4, %%mm2\n\t"
		"pand	%5, %%mm3\n\t"
		"por	%%mm2, %%mm0\n\t"
		"psrlq	$16, %%mm1\n\t"
		"psrlq	$32, %%mm4\n\t"
		"psllq	$16, %%mm5\n\t"
		"por	%%mm3, %%mm1\n\t"
		"pand	%6, %%mm5\n\t"
		"por	%%mm5, %%mm4\n\t"

		MOVNTQ"	%%mm0, %0\n\t"
		MOVNTQ"	%%mm1, 8%0\n\t"
		MOVNTQ"	%%mm4, 16%0"

		:"=m"(*d)
		:"m"(*s),"m"(mask24l),"m"(mask24h),"m"(mask24hh),"m"(mask24hhh),"m"(mask24hhhh)
		:"memory");
		d += 24;
		s += 8;
	}
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
		register uint16_t bgr;
		bgr = *s++;
		*d++ = (bgr&0x1F)<<3;
		*d++ = (bgr&0x7E0)>>3;
		*d++ = (bgr&0xF800)>>8;
	}
}

static inline void RENAME(rgb15to32)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint16_t *end;
#ifdef HAVE_MMX
	const uint16_t *mm_end;
#endif
	uint8_t *d = (uint8_t *)dst;
	const uint16_t *s = (const uint16_t *)src;
	end = s + src_size/2;
#ifdef HAVE_MMX
	__asm __volatile(PREFETCH"	%0"::"m"(*s):"memory");
	__asm __volatile("pxor	%%mm7,%%mm7\n\t":::"memory");
	mm_end = end - 3;
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movq	%1, %%mm0\n\t"
		"movq	%1, %%mm1\n\t"
		"movq	%1, %%mm2\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%3, %%mm1\n\t"
		"pand	%4, %%mm2\n\t"
		"psllq	$3, %%mm0\n\t"
		"psrlq	$2, %%mm1\n\t"
		"psrlq	$7, %%mm2\n\t"
		"movq	%%mm0, %%mm3\n\t"
		"movq	%%mm1, %%mm4\n\t"
		"movq	%%mm2, %%mm5\n\t"
		"punpcklwd %%mm7, %%mm0\n\t"
		"punpcklwd %%mm7, %%mm1\n\t"
		"punpcklwd %%mm7, %%mm2\n\t"
		"punpckhwd %%mm7, %%mm3\n\t"
		"punpckhwd %%mm7, %%mm4\n\t"
		"punpckhwd %%mm7, %%mm5\n\t"
		"psllq	$8, %%mm1\n\t"
		"psllq	$16, %%mm2\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm2, %%mm0\n\t"
		"psllq	$8, %%mm4\n\t"
		"psllq	$16, %%mm5\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm5, %%mm3\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		MOVNTQ"	%%mm3, 8%0\n\t"
		:"=m"(*d)
		:"m"(*s),"m"(mask15b),"m"(mask15g),"m"(mask15r)
		:"memory");
		d += 16;
		s += 4;
	}
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
#if 0 //slightly slower on athlon
		int bgr= *s++;
		*((uint32_t*)d)++ = ((bgr&0x1F)<<3) + ((bgr&0x3E0)<<6) + ((bgr&0x7C00)<<9);
#else
		register uint16_t bgr;
		bgr = *s++;
#ifdef WORDS_BIGENDIAN
		*d++ = 0;
		*d++ = (bgr&0x7C00)>>7;
		*d++ = (bgr&0x3E0)>>2;
		*d++ = (bgr&0x1F)<<3;
#else
		*d++ = (bgr&0x1F)<<3;
		*d++ = (bgr&0x3E0)>>2;
		*d++ = (bgr&0x7C00)>>7;
		*d++ = 0;
#endif

#endif
	}
}

static inline void RENAME(rgb16to32)(const uint8_t *src, uint8_t *dst, long src_size)
{
	const uint16_t *end;
#ifdef HAVE_MMX
	const uint16_t *mm_end;
#endif
	uint8_t *d = (uint8_t *)dst;
	const uint16_t *s = (uint16_t *)src;
	end = s + src_size/2;
#ifdef HAVE_MMX
	__asm __volatile(PREFETCH"	%0"::"m"(*s):"memory");
	__asm __volatile("pxor	%%mm7,%%mm7\n\t":::"memory");
	mm_end = end - 3;
	while(s < mm_end)
	{
	    __asm __volatile(
		PREFETCH" 32%1\n\t"
		"movq	%1, %%mm0\n\t"
		"movq	%1, %%mm1\n\t"
		"movq	%1, %%mm2\n\t"
		"pand	%2, %%mm0\n\t"
		"pand	%3, %%mm1\n\t"
		"pand	%4, %%mm2\n\t"
		"psllq	$3, %%mm0\n\t"
		"psrlq	$3, %%mm1\n\t"
		"psrlq	$8, %%mm2\n\t"
		"movq	%%mm0, %%mm3\n\t"
		"movq	%%mm1, %%mm4\n\t"
		"movq	%%mm2, %%mm5\n\t"
		"punpcklwd %%mm7, %%mm0\n\t"
		"punpcklwd %%mm7, %%mm1\n\t"
		"punpcklwd %%mm7, %%mm2\n\t"
		"punpckhwd %%mm7, %%mm3\n\t"
		"punpckhwd %%mm7, %%mm4\n\t"
		"punpckhwd %%mm7, %%mm5\n\t"
		"psllq	$8, %%mm1\n\t"
		"psllq	$16, %%mm2\n\t"
		"por	%%mm1, %%mm0\n\t"
		"por	%%mm2, %%mm0\n\t"
		"psllq	$8, %%mm4\n\t"
		"psllq	$16, %%mm5\n\t"
		"por	%%mm4, %%mm3\n\t"
		"por	%%mm5, %%mm3\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		MOVNTQ"	%%mm3, 8%0\n\t"
		:"=m"(*d)
		:"m"(*s),"m"(mask16b),"m"(mask16g),"m"(mask16r)
		:"memory");
		d += 16;
		s += 4;
	}
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	while(s < end)
	{
		register uint16_t bgr;
		bgr = *s++;
#ifdef WORDS_BIGENDIAN
		*d++ = 0;
		*d++ = (bgr&0xF800)>>8;
		*d++ = (bgr&0x7E0)>>3;
		*d++ = (bgr&0x1F)<<3;
#else
		*d++ = (bgr&0x1F)<<3;
		*d++ = (bgr&0x7E0)>>3;
		*d++ = (bgr&0xF800)>>8;
		*d++ = 0;
#endif
	}
}

static inline void RENAME(rgb32tobgr32)(const uint8_t *src, uint8_t *dst, long src_size)
{
#ifdef HAVE_MMX
/* TODO: unroll this loop */
	asm volatile (
		"xor %%"REG_a", %%"REG_a"	\n\t"
		ASMALIGN(4)
		"1:				\n\t"
		PREFETCH" 32(%0, %%"REG_a")	\n\t"
		"movq (%0, %%"REG_a"), %%mm0	\n\t"
		"movq %%mm0, %%mm1		\n\t"
		"movq %%mm0, %%mm2		\n\t"
		"pslld $16, %%mm0		\n\t"
		"psrld $16, %%mm1		\n\t"
		"pand "MANGLE(mask32r)", %%mm0	\n\t"
		"pand "MANGLE(mask32g)", %%mm2	\n\t"
		"pand "MANGLE(mask32b)", %%mm1	\n\t"
		"por %%mm0, %%mm2		\n\t"
		"por %%mm1, %%mm2		\n\t"
		MOVNTQ" %%mm2, (%1, %%"REG_a")	\n\t"
		"add $8, %%"REG_a"		\n\t"
		"cmp %2, %%"REG_a"		\n\t"
		" jb 1b				\n\t"
		:: "r" (src), "r"(dst), "r" (src_size-7)
		: "%"REG_a
	);

	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#else
	unsigned i;
	unsigned num_pixels = src_size >> 2;
	for(i=0; i<num_pixels; i++)
	{
#ifdef WORDS_BIGENDIAN  
	  dst[4*i + 1] = src[4*i + 3];
	  dst[4*i + 2] = src[4*i + 2];
	  dst[4*i + 3] = src[4*i + 1];
#else
	  dst[4*i + 0] = src[4*i + 2];
	  dst[4*i + 1] = src[4*i + 1];
	  dst[4*i + 2] = src[4*i + 0];
#endif
	}
#endif
}

static inline void RENAME(rgb24tobgr24)(const uint8_t *src, uint8_t *dst, long src_size)
{
	unsigned i;
#ifdef HAVE_MMX
	long mmx_size= 23 - src_size;
	asm volatile (
		"movq "MANGLE(mask24r)", %%mm5	\n\t"
		"movq "MANGLE(mask24g)", %%mm6	\n\t"
		"movq "MANGLE(mask24b)", %%mm7	\n\t"
		ASMALIGN(4)
		"1:				\n\t"
		PREFETCH" 32(%1, %%"REG_a")	\n\t"
		"movq   (%1, %%"REG_a"), %%mm0	\n\t" // BGR BGR BG
		"movq   (%1, %%"REG_a"), %%mm1	\n\t" // BGR BGR BG
		"movq  2(%1, %%"REG_a"), %%mm2	\n\t" // R BGR BGR B
		"psllq $16, %%mm0		\n\t" // 00 BGR BGR
		"pand %%mm5, %%mm0		\n\t"
		"pand %%mm6, %%mm1		\n\t"
		"pand %%mm7, %%mm2		\n\t"
		"por %%mm0, %%mm1		\n\t"
		"por %%mm2, %%mm1		\n\t"                
		"movq  6(%1, %%"REG_a"), %%mm0	\n\t" // BGR BGR BG
		MOVNTQ" %%mm1,   (%2, %%"REG_a")\n\t" // RGB RGB RG
		"movq  8(%1, %%"REG_a"), %%mm1	\n\t" // R BGR BGR B
		"movq 10(%1, %%"REG_a"), %%mm2	\n\t" // GR BGR BGR
		"pand %%mm7, %%mm0		\n\t"
		"pand %%mm5, %%mm1		\n\t"
		"pand %%mm6, %%mm2		\n\t"
		"por %%mm0, %%mm1		\n\t"
		"por %%mm2, %%mm1		\n\t"                
		"movq 14(%1, %%"REG_a"), %%mm0	\n\t" // R BGR BGR B
		MOVNTQ" %%mm1,  8(%2, %%"REG_a")\n\t" // B RGB RGB R
		"movq 16(%1, %%"REG_a"), %%mm1	\n\t" // GR BGR BGR
		"movq 18(%1, %%"REG_a"), %%mm2	\n\t" // BGR BGR BG
		"pand %%mm6, %%mm0		\n\t"
		"pand %%mm7, %%mm1		\n\t"
		"pand %%mm5, %%mm2		\n\t"
		"por %%mm0, %%mm1		\n\t"
		"por %%mm2, %%mm1		\n\t"                
		MOVNTQ" %%mm1, 16(%2, %%"REG_a")\n\t"
		"add $24, %%"REG_a"		\n\t"
		" js 1b				\n\t"
		: "+a" (mmx_size)
		: "r" (src-mmx_size), "r"(dst-mmx_size)
	);

	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");

	if(mmx_size==23) return; //finihsed, was multiple of 8

	src+= src_size;
	dst+= src_size;
	src_size= 23-mmx_size;
	src-= src_size;
	dst-= src_size;
#endif
	for(i=0; i<src_size; i+=3)
	{
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
	const long chromWidth= width>>1;
	for(y=0; y<height; y++)
	{
#ifdef HAVE_MMX
//FIXME handle 2 lines a once (fewer prefetch, reuse some chrom, but very likely limited by mem anyway)
		asm volatile(
			"xor %%"REG_a", %%"REG_a"	\n\t"
			ASMALIGN(4)
			"1:				\n\t"
			PREFETCH" 32(%1, %%"REG_a", 2)	\n\t"
			PREFETCH" 32(%2, %%"REG_a")	\n\t"
			PREFETCH" 32(%3, %%"REG_a")	\n\t"
			"movq (%2, %%"REG_a"), %%mm0	\n\t" // U(0)
			"movq %%mm0, %%mm2		\n\t" // U(0)
			"movq (%3, %%"REG_a"), %%mm1	\n\t" // V(0)
			"punpcklbw %%mm1, %%mm0		\n\t" // UVUV UVUV(0)
			"punpckhbw %%mm1, %%mm2		\n\t" // UVUV UVUV(8)

			"movq (%1, %%"REG_a",2), %%mm3	\n\t" // Y(0)
			"movq 8(%1, %%"REG_a",2), %%mm5	\n\t" // Y(8)
			"movq %%mm3, %%mm4		\n\t" // Y(0)
			"movq %%mm5, %%mm6		\n\t" // Y(8)
			"punpcklbw %%mm0, %%mm3		\n\t" // YUYV YUYV(0)
			"punpckhbw %%mm0, %%mm4		\n\t" // YUYV YUYV(4)
			"punpcklbw %%mm2, %%mm5		\n\t" // YUYV YUYV(8)
			"punpckhbw %%mm2, %%mm6		\n\t" // YUYV YUYV(12)

			MOVNTQ" %%mm3, (%0, %%"REG_a", 4)\n\t"
			MOVNTQ" %%mm4, 8(%0, %%"REG_a", 4)\n\t"
			MOVNTQ" %%mm5, 16(%0, %%"REG_a", 4)\n\t"
			MOVNTQ" %%mm6, 24(%0, %%"REG_a", 4)\n\t"

			"add $8, %%"REG_a"		\n\t"
			"cmp %4, %%"REG_a"		\n\t"
			" jb 1b				\n\t"
			::"r"(dst), "r"(ysrc), "r"(usrc), "r"(vsrc), "g" (chromWidth)
			: "%"REG_a
		);
#else

#if defined ARCH_ALPHA && defined HAVE_MVI
#define pl2yuy2(n)					\
	y1 = yc[n];					\
	y2 = yc2[n];					\
	u = uc[n];					\
	v = vc[n];					\
	asm("unpkbw %1, %0" : "=r"(y1) : "r"(y1));	\
	asm("unpkbw %1, %0" : "=r"(y2) : "r"(y2));	\
	asm("unpkbl %1, %0" : "=r"(u) : "r"(u));	\
	asm("unpkbl %1, %0" : "=r"(v) : "r"(v));	\
	yuv1 = (u << 8) + (v << 24);			\
	yuv2 = yuv1 + y2;				\
	yuv1 += y1;					\
	qdst[n] = yuv1;					\
	qdst2[n] = yuv2;

		int i;
		uint64_t *qdst = (uint64_t *) dst;
		uint64_t *qdst2 = (uint64_t *) (dst + dstStride);
		const uint32_t *yc = (uint32_t *) ysrc;
		const uint32_t *yc2 = (uint32_t *) (ysrc + lumStride);
		const uint16_t *uc = (uint16_t*) usrc, *vc = (uint16_t*) vsrc;
		for(i = 0; i < chromWidth; i += 8){
			uint64_t y1, y2, yuv1, yuv2;
			uint64_t u, v;
			/* Prefetch */
			asm("ldq $31,64(%0)" :: "r"(yc));
			asm("ldq $31,64(%0)" :: "r"(yc2));
			asm("ldq $31,64(%0)" :: "r"(uc));
			asm("ldq $31,64(%0)" :: "r"(vc));

			pl2yuy2(0);
			pl2yuy2(1);
			pl2yuy2(2);
			pl2yuy2(3);

			yc += 4;
			yc2 += 4;
			uc += 4;
			vc += 4;
			qdst += 4;
			qdst2 += 4;
		}
		y++;
		ysrc += lumStride;
		dst += dstStride;

#elif __WORDSIZE >= 64
		int i;
		uint64_t *ldst = (uint64_t *) dst;
		const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
		for(i = 0; i < chromWidth; i += 2){
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
		for(i = 0; i < chromWidth; i++){
#ifdef WORDS_BIGENDIAN
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
		if((y&(vertLumPerChroma-1))==(vertLumPerChroma-1) )
		{
			usrc += chromStride;
			vsrc += chromStride;
		}
		ysrc += lumStride;
		dst += dstStride;
	}
#ifdef HAVE_MMX
asm(    EMMS" \n\t"
        SFENCE" \n\t"
        :::"memory");
#endif
}

/**
 *
 * height should be a multiple of 2 and width should be a multiple of 16 (if this is a
 * problem for anyone then tell me, and ill fix it)
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
	const long chromWidth= width>>1;
	for(y=0; y<height; y++)
	{
#ifdef HAVE_MMX
//FIXME handle 2 lines a once (fewer prefetch, reuse some chrom, but very likely limited by mem anyway)
		asm volatile(
			"xor %%"REG_a", %%"REG_a"	\n\t"
			ASMALIGN(4)
			"1:				\n\t"
			PREFETCH" 32(%1, %%"REG_a", 2)	\n\t"
			PREFETCH" 32(%2, %%"REG_a")	\n\t"
			PREFETCH" 32(%3, %%"REG_a")	\n\t"
			"movq (%2, %%"REG_a"), %%mm0	\n\t" // U(0)
			"movq %%mm0, %%mm2		\n\t" // U(0)
			"movq (%3, %%"REG_a"), %%mm1	\n\t" // V(0)
			"punpcklbw %%mm1, %%mm0		\n\t" // UVUV UVUV(0)
			"punpckhbw %%mm1, %%mm2		\n\t" // UVUV UVUV(8)

			"movq (%1, %%"REG_a",2), %%mm3	\n\t" // Y(0)
			"movq 8(%1, %%"REG_a",2), %%mm5	\n\t" // Y(8)
			"movq %%mm0, %%mm4		\n\t" // Y(0)
			"movq %%mm2, %%mm6		\n\t" // Y(8)
			"punpcklbw %%mm3, %%mm0		\n\t" // YUYV YUYV(0)
			"punpckhbw %%mm3, %%mm4		\n\t" // YUYV YUYV(4)
			"punpcklbw %%mm5, %%mm2		\n\t" // YUYV YUYV(8)
			"punpckhbw %%mm5, %%mm6		\n\t" // YUYV YUYV(12)

			MOVNTQ" %%mm0, (%0, %%"REG_a", 4)\n\t"
			MOVNTQ" %%mm4, 8(%0, %%"REG_a", 4)\n\t"
			MOVNTQ" %%mm2, 16(%0, %%"REG_a", 4)\n\t"
			MOVNTQ" %%mm6, 24(%0, %%"REG_a", 4)\n\t"

			"add $8, %%"REG_a"		\n\t"
			"cmp %4, %%"REG_a"		\n\t"
			" jb 1b				\n\t"
			::"r"(dst), "r"(ysrc), "r"(usrc), "r"(vsrc), "g" (chromWidth)
			: "%"REG_a
		);
#else
//FIXME adapt the alpha asm code from yv12->yuy2

#if __WORDSIZE >= 64
		int i;
		uint64_t *ldst = (uint64_t *) dst;
		const uint8_t *yc = ysrc, *uc = usrc, *vc = vsrc;
		for(i = 0; i < chromWidth; i += 2){
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
		for(i = 0; i < chromWidth; i++){
#ifdef WORDS_BIGENDIAN
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
		if((y&(vertLumPerChroma-1))==(vertLumPerChroma-1) )
		{
			usrc += chromStride;
			vsrc += chromStride;
		}
		ysrc += lumStride;
		dst += dstStride;
	}
#ifdef HAVE_MMX
asm(    EMMS" \n\t"
        SFENCE" \n\t"
        :::"memory");
#endif
}

/**
 *
 * height should be a multiple of 2 and width should be a multiple of 16 (if this is a
 * problem for anyone then tell me, and ill fix it)
 */
static inline void RENAME(yv12touyvy)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
	long width, long height,
	long lumStride, long chromStride, long dstStride)
{
	//FIXME interpolate chroma
	RENAME(yuvPlanartouyvy)(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride, 2);
}

/**
 *
 * width should be a multiple of 16
 */
static inline void RENAME(yuv422ptoyuy2)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
	long width, long height,
	long lumStride, long chromStride, long dstStride)
{
	RENAME(yuvPlanartoyuy2)(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride, 1);
}

/**
 *
 * height should be a multiple of 2 and width should be a multiple of 16 (if this is a
 * problem for anyone then tell me, and ill fix it)
 */
static inline void RENAME(yuy2toyv12)(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	long width, long height,
	long lumStride, long chromStride, long srcStride)
{
	long y;
	const long chromWidth= width>>1;
	for(y=0; y<height; y+=2)
	{
#ifdef HAVE_MMX
		asm volatile(
			"xor %%"REG_a", %%"REG_a"	\n\t"
			"pcmpeqw %%mm7, %%mm7		\n\t"
			"psrlw $8, %%mm7		\n\t" // FF,00,FF,00...
			ASMALIGN(4)
			"1:				\n\t"
			PREFETCH" 64(%0, %%"REG_a", 4)	\n\t"
			"movq (%0, %%"REG_a", 4), %%mm0	\n\t" // YUYV YUYV(0)
			"movq 8(%0, %%"REG_a", 4), %%mm1\n\t" // YUYV YUYV(4)
			"movq %%mm0, %%mm2		\n\t" // YUYV YUYV(0)
			"movq %%mm1, %%mm3		\n\t" // YUYV YUYV(4)
			"psrlw $8, %%mm0		\n\t" // U0V0 U0V0(0)
			"psrlw $8, %%mm1		\n\t" // U0V0 U0V0(4)
			"pand %%mm7, %%mm2		\n\t" // Y0Y0 Y0Y0(0)
			"pand %%mm7, %%mm3		\n\t" // Y0Y0 Y0Y0(4)
			"packuswb %%mm1, %%mm0		\n\t" // UVUV UVUV(0)
			"packuswb %%mm3, %%mm2		\n\t" // YYYY YYYY(0)

			MOVNTQ" %%mm2, (%1, %%"REG_a", 2)\n\t"

			"movq 16(%0, %%"REG_a", 4), %%mm1\n\t" // YUYV YUYV(8)
			"movq 24(%0, %%"REG_a", 4), %%mm2\n\t" // YUYV YUYV(12)
			"movq %%mm1, %%mm3		\n\t" // YUYV YUYV(8)
			"movq %%mm2, %%mm4		\n\t" // YUYV YUYV(12)
			"psrlw $8, %%mm1		\n\t" // U0V0 U0V0(8)
			"psrlw $8, %%mm2		\n\t" // U0V0 U0V0(12)
			"pand %%mm7, %%mm3		\n\t" // Y0Y0 Y0Y0(8)
			"pand %%mm7, %%mm4		\n\t" // Y0Y0 Y0Y0(12)
			"packuswb %%mm2, %%mm1		\n\t" // UVUV UVUV(8)
			"packuswb %%mm4, %%mm3		\n\t" // YYYY YYYY(8)

			MOVNTQ" %%mm3, 8(%1, %%"REG_a", 2)\n\t"

			"movq %%mm0, %%mm2		\n\t" // UVUV UVUV(0)
			"movq %%mm1, %%mm3		\n\t" // UVUV UVUV(8)
			"psrlw $8, %%mm0		\n\t" // V0V0 V0V0(0)
			"psrlw $8, %%mm1		\n\t" // V0V0 V0V0(8)
			"pand %%mm7, %%mm2		\n\t" // U0U0 U0U0(0)
			"pand %%mm7, %%mm3		\n\t" // U0U0 U0U0(8)
			"packuswb %%mm1, %%mm0		\n\t" // VVVV VVVV(0)
			"packuswb %%mm3, %%mm2		\n\t" // UUUU UUUU(0)

			MOVNTQ" %%mm0, (%3, %%"REG_a")	\n\t"
			MOVNTQ" %%mm2, (%2, %%"REG_a")	\n\t"

			"add $8, %%"REG_a"		\n\t"
			"cmp %4, %%"REG_a"		\n\t"
			" jb 1b				\n\t"
			::"r"(src), "r"(ydst), "r"(udst), "r"(vdst), "g" (chromWidth)
			: "memory", "%"REG_a
		);

		ydst += lumStride;
		src  += srcStride;

		asm volatile(
			"xor %%"REG_a", %%"REG_a"	\n\t"
			ASMALIGN(4)
			"1:				\n\t"
			PREFETCH" 64(%0, %%"REG_a", 4)	\n\t"
			"movq (%0, %%"REG_a", 4), %%mm0	\n\t" // YUYV YUYV(0)
			"movq 8(%0, %%"REG_a", 4), %%mm1\n\t" // YUYV YUYV(4)
			"movq 16(%0, %%"REG_a", 4), %%mm2\n\t" // YUYV YUYV(8)
			"movq 24(%0, %%"REG_a", 4), %%mm3\n\t" // YUYV YUYV(12)
			"pand %%mm7, %%mm0		\n\t" // Y0Y0 Y0Y0(0)
			"pand %%mm7, %%mm1		\n\t" // Y0Y0 Y0Y0(4)
			"pand %%mm7, %%mm2		\n\t" // Y0Y0 Y0Y0(8)
			"pand %%mm7, %%mm3		\n\t" // Y0Y0 Y0Y0(12)
			"packuswb %%mm1, %%mm0		\n\t" // YYYY YYYY(0)
			"packuswb %%mm3, %%mm2		\n\t" // YYYY YYYY(8)

			MOVNTQ" %%mm0, (%1, %%"REG_a", 2)\n\t"
			MOVNTQ" %%mm2, 8(%1, %%"REG_a", 2)\n\t"

			"add $8, %%"REG_a"		\n\t"
			"cmp %4, %%"REG_a"		\n\t"
			" jb 1b				\n\t"

			::"r"(src), "r"(ydst), "r"(udst), "r"(vdst), "g" (chromWidth)
			: "memory", "%"REG_a
		);
#else
		long i;
		for(i=0; i<chromWidth; i++)
		{
			ydst[2*i+0] 	= src[4*i+0];
			udst[i] 	= src[4*i+1];
			ydst[2*i+1] 	= src[4*i+2];
			vdst[i] 	= src[4*i+3];
		}
		ydst += lumStride;
		src  += srcStride;

		for(i=0; i<chromWidth; i++)
		{
			ydst[2*i+0] 	= src[4*i+0];
			ydst[2*i+1] 	= src[4*i+2];
		}
#endif
		udst += chromStride;
		vdst += chromStride;
		ydst += lumStride;
		src  += srcStride;
	}
#ifdef HAVE_MMX
asm volatile(   EMMS" \n\t"
        	SFENCE" \n\t"
        	:::"memory");
#endif
}

static inline void RENAME(yvu9toyv12)(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc,
	uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	long width, long height, long lumStride, long chromStride)
{
	/* Y Plane */
	memcpy(ydst, ysrc, width*height);

	/* XXX: implement upscaling for U,V */
}

static inline void RENAME(planar2x)(const uint8_t *src, uint8_t *dst, long srcWidth, long srcHeight, long srcStride, long dstStride)
{
	long x,y;
	
	dst[0]= src[0];
        
	// first line
	for(x=0; x<srcWidth-1; x++){
		dst[2*x+1]= (3*src[x] +   src[x+1])>>2;
		dst[2*x+2]= (  src[x] + 3*src[x+1])>>2;
	}
	dst[2*srcWidth-1]= src[srcWidth-1];
	
        dst+= dstStride;

	for(y=1; y<srcHeight; y++){
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
		const long mmxSize= srcWidth&~15;
		asm volatile(
			"mov %4, %%"REG_a"		\n\t"
			"1:				\n\t"
			"movq (%0, %%"REG_a"), %%mm0	\n\t"
			"movq (%1, %%"REG_a"), %%mm1	\n\t"
			"movq 1(%0, %%"REG_a"), %%mm2	\n\t"
			"movq 1(%1, %%"REG_a"), %%mm3	\n\t"
			"movq -1(%0, %%"REG_a"), %%mm4	\n\t"
			"movq -1(%1, %%"REG_a"), %%mm5	\n\t"
			PAVGB" %%mm0, %%mm5		\n\t"
			PAVGB" %%mm0, %%mm3		\n\t"
			PAVGB" %%mm0, %%mm5		\n\t"
			PAVGB" %%mm0, %%mm3		\n\t"
			PAVGB" %%mm1, %%mm4		\n\t"
			PAVGB" %%mm1, %%mm2		\n\t"
			PAVGB" %%mm1, %%mm4		\n\t"
			PAVGB" %%mm1, %%mm2		\n\t"
			"movq %%mm5, %%mm7		\n\t"
			"movq %%mm4, %%mm6		\n\t"
			"punpcklbw %%mm3, %%mm5		\n\t"
			"punpckhbw %%mm3, %%mm7		\n\t"
			"punpcklbw %%mm2, %%mm4		\n\t"
			"punpckhbw %%mm2, %%mm6		\n\t"
#if 1
			MOVNTQ" %%mm5, (%2, %%"REG_a", 2)\n\t"
			MOVNTQ" %%mm7, 8(%2, %%"REG_a", 2)\n\t"
			MOVNTQ" %%mm4, (%3, %%"REG_a", 2)\n\t"
			MOVNTQ" %%mm6, 8(%3, %%"REG_a", 2)\n\t"
#else
			"movq %%mm5, (%2, %%"REG_a", 2)	\n\t"
			"movq %%mm7, 8(%2, %%"REG_a", 2)\n\t"
			"movq %%mm4, (%3, %%"REG_a", 2)	\n\t"
			"movq %%mm6, 8(%3, %%"REG_a", 2)\n\t"
#endif
			"add $8, %%"REG_a"		\n\t"
			" js 1b				\n\t"
			:: "r" (src + mmxSize  ), "r" (src + srcStride + mmxSize  ),
			   "r" (dst + mmxSize*2), "r" (dst + dstStride + mmxSize*2),
			   "g" (-mmxSize)
			: "%"REG_a

		);
#else
		const long mmxSize=1;
#endif
		dst[0        ]= (3*src[0] +   src[srcStride])>>2;
		dst[dstStride]= (  src[0] + 3*src[srcStride])>>2;

		for(x=mmxSize-1; x<srcWidth-1; x++){
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
        
	for(x=0; x<srcWidth-1; x++){
		dst[2*x+1]= (3*src[x] +   src[x+1])>>2;
		dst[2*x+2]= (  src[x] + 3*src[x+1])>>2;
	}
	dst[2*srcWidth-1]= src[srcWidth-1];
#else
	for(x=0; x<srcWidth; x++){
		dst[2*x+0]=
		dst[2*x+1]= src[x];
	}
#endif

#ifdef HAVE_MMX
asm volatile(   EMMS" \n\t"
        	SFENCE" \n\t"
        	:::"memory");
#endif
}

/**
 *
 * height should be a multiple of 2 and width should be a multiple of 16 (if this is a
 * problem for anyone then tell me, and ill fix it)
 * chrominance data is only taken from every secound line others are ignored FIXME write HQ version
 */
static inline void RENAME(uyvytoyv12)(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	long width, long height,
	long lumStride, long chromStride, long srcStride)
{
	long y;
	const long chromWidth= width>>1;
	for(y=0; y<height; y+=2)
	{
#ifdef HAVE_MMX
		asm volatile(
			"xorl %%eax, %%eax		\n\t"
			"pcmpeqw %%mm7, %%mm7		\n\t"
			"psrlw $8, %%mm7		\n\t" // FF,00,FF,00...
			ASMALIGN(4)
			"1:				\n\t"
			PREFETCH" 64(%0, %%eax, 4)	\n\t"
			"movq (%0, %%eax, 4), %%mm0	\n\t" // UYVY UYVY(0)
			"movq 8(%0, %%eax, 4), %%mm1	\n\t" // UYVY UYVY(4)
			"movq %%mm0, %%mm2		\n\t" // UYVY UYVY(0)
			"movq %%mm1, %%mm3		\n\t" // UYVY UYVY(4)
			"pand %%mm7, %%mm0		\n\t" // U0V0 U0V0(0)
			"pand %%mm7, %%mm1		\n\t" // U0V0 U0V0(4)
			"psrlw $8, %%mm2		\n\t" // Y0Y0 Y0Y0(0)
			"psrlw $8, %%mm3		\n\t" // Y0Y0 Y0Y0(4)
			"packuswb %%mm1, %%mm0		\n\t" // UVUV UVUV(0)
			"packuswb %%mm3, %%mm2		\n\t" // YYYY YYYY(0)

			MOVNTQ" %%mm2, (%1, %%eax, 2)	\n\t"

			"movq 16(%0, %%eax, 4), %%mm1	\n\t" // UYVY UYVY(8)
			"movq 24(%0, %%eax, 4), %%mm2	\n\t" // UYVY UYVY(12)
			"movq %%mm1, %%mm3		\n\t" // UYVY UYVY(8)
			"movq %%mm2, %%mm4		\n\t" // UYVY UYVY(12)
			"pand %%mm7, %%mm1		\n\t" // U0V0 U0V0(8)
			"pand %%mm7, %%mm2		\n\t" // U0V0 U0V0(12)
			"psrlw $8, %%mm3		\n\t" // Y0Y0 Y0Y0(8)
			"psrlw $8, %%mm4		\n\t" // Y0Y0 Y0Y0(12)
			"packuswb %%mm2, %%mm1		\n\t" // UVUV UVUV(8)
			"packuswb %%mm4, %%mm3		\n\t" // YYYY YYYY(8)

			MOVNTQ" %%mm3, 8(%1, %%eax, 2)	\n\t"

			"movq %%mm0, %%mm2		\n\t" // UVUV UVUV(0)
			"movq %%mm1, %%mm3		\n\t" // UVUV UVUV(8)
			"psrlw $8, %%mm0		\n\t" // V0V0 V0V0(0)
			"psrlw $8, %%mm1		\n\t" // V0V0 V0V0(8)
			"pand %%mm7, %%mm2		\n\t" // U0U0 U0U0(0)
			"pand %%mm7, %%mm3		\n\t" // U0U0 U0U0(8)
			"packuswb %%mm1, %%mm0		\n\t" // VVVV VVVV(0)
			"packuswb %%mm3, %%mm2		\n\t" // UUUU UUUU(0)

			MOVNTQ" %%mm0, (%3, %%eax)	\n\t"
			MOVNTQ" %%mm2, (%2, %%eax)	\n\t"

			"addl $8, %%eax			\n\t"
			"cmpl %4, %%eax			\n\t"
			" jb 1b				\n\t"
			::"r"(src), "r"(ydst), "r"(udst), "r"(vdst), "g" (chromWidth)
			: "memory", "%eax"
		);

		ydst += lumStride;
		src  += srcStride;

		asm volatile(
			"xorl %%eax, %%eax		\n\t"
			ASMALIGN(4)
			"1:				\n\t"
			PREFETCH" 64(%0, %%eax, 4)	\n\t"
			"movq (%0, %%eax, 4), %%mm0	\n\t" // YUYV YUYV(0)
			"movq 8(%0, %%eax, 4), %%mm1	\n\t" // YUYV YUYV(4)
			"movq 16(%0, %%eax, 4), %%mm2	\n\t" // YUYV YUYV(8)
			"movq 24(%0, %%eax, 4), %%mm3	\n\t" // YUYV YUYV(12)
			"psrlw $8, %%mm0		\n\t" // Y0Y0 Y0Y0(0)
			"psrlw $8, %%mm1		\n\t" // Y0Y0 Y0Y0(4)
			"psrlw $8, %%mm2		\n\t" // Y0Y0 Y0Y0(8)
			"psrlw $8, %%mm3		\n\t" // Y0Y0 Y0Y0(12)
			"packuswb %%mm1, %%mm0		\n\t" // YYYY YYYY(0)
			"packuswb %%mm3, %%mm2		\n\t" // YYYY YYYY(8)

			MOVNTQ" %%mm0, (%1, %%eax, 2)	\n\t"
			MOVNTQ" %%mm2, 8(%1, %%eax, 2)	\n\t"

			"addl $8, %%eax			\n\t"
			"cmpl %4, %%eax			\n\t"
			" jb 1b				\n\t"

			::"r"(src), "r"(ydst), "r"(udst), "r"(vdst), "g" (chromWidth)
			: "memory", "%eax"
		);
#else
		long i;
		for(i=0; i<chromWidth; i++)
		{
			udst[i] 	= src[4*i+0];
			ydst[2*i+0] 	= src[4*i+1];
			vdst[i] 	= src[4*i+2];
			ydst[2*i+1] 	= src[4*i+3];
		}
		ydst += lumStride;
		src  += srcStride;

		for(i=0; i<chromWidth; i++)
		{
			ydst[2*i+0] 	= src[4*i+1];
			ydst[2*i+1] 	= src[4*i+3];
		}
#endif
		udst += chromStride;
		vdst += chromStride;
		ydst += lumStride;
		src  += srcStride;
	}
#ifdef HAVE_MMX
asm volatile(   EMMS" \n\t"
        	SFENCE" \n\t"
        	:::"memory");
#endif
}

/**
 *
 * height should be a multiple of 2 and width should be a multiple of 2 (if this is a
 * problem for anyone then tell me, and ill fix it)
 * chrominance data is only taken from every secound line others are ignored in the C version FIXME write HQ version
 */
static inline void RENAME(rgb24toyv12)(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	long width, long height,
	long lumStride, long chromStride, long srcStride)
{
	long y;
	const long chromWidth= width>>1;
#ifdef HAVE_MMX
	for(y=0; y<height-2; y+=2)
	{
		long i;
		for(i=0; i<2; i++)
		{
			asm volatile(
				"mov %2, %%"REG_a"		\n\t"
				"movq "MANGLE(bgr2YCoeff)", %%mm6		\n\t"
				"movq "MANGLE(w1111)", %%mm5		\n\t"
				"pxor %%mm7, %%mm7		\n\t"
				"lea (%%"REG_a", %%"REG_a", 2), %%"REG_d"\n\t"
				ASMALIGN(4)
				"1:				\n\t"
				PREFETCH" 64(%0, %%"REG_d")	\n\t"
				"movd (%0, %%"REG_d"), %%mm0	\n\t"
				"movd 3(%0, %%"REG_d"), %%mm1	\n\t"
				"punpcklbw %%mm7, %%mm0		\n\t"
				"punpcklbw %%mm7, %%mm1		\n\t"
				"movd 6(%0, %%"REG_d"), %%mm2	\n\t"
				"movd 9(%0, %%"REG_d"), %%mm3	\n\t"
				"punpcklbw %%mm7, %%mm2		\n\t"
				"punpcklbw %%mm7, %%mm3		\n\t"
				"pmaddwd %%mm6, %%mm0		\n\t"
				"pmaddwd %%mm6, %%mm1		\n\t"
				"pmaddwd %%mm6, %%mm2		\n\t"
				"pmaddwd %%mm6, %%mm3		\n\t"
#ifndef FAST_BGR2YV12
				"psrad $8, %%mm0		\n\t"
				"psrad $8, %%mm1		\n\t"
				"psrad $8, %%mm2		\n\t"
				"psrad $8, %%mm3		\n\t"
#endif
				"packssdw %%mm1, %%mm0		\n\t"
				"packssdw %%mm3, %%mm2		\n\t"
				"pmaddwd %%mm5, %%mm0		\n\t"
				"pmaddwd %%mm5, %%mm2		\n\t"
				"packssdw %%mm2, %%mm0		\n\t"
				"psraw $7, %%mm0		\n\t"

				"movd 12(%0, %%"REG_d"), %%mm4	\n\t"
				"movd 15(%0, %%"REG_d"), %%mm1	\n\t"
				"punpcklbw %%mm7, %%mm4		\n\t"
				"punpcklbw %%mm7, %%mm1		\n\t"
				"movd 18(%0, %%"REG_d"), %%mm2	\n\t"
				"movd 21(%0, %%"REG_d"), %%mm3	\n\t"
				"punpcklbw %%mm7, %%mm2		\n\t"
				"punpcklbw %%mm7, %%mm3		\n\t"
				"pmaddwd %%mm6, %%mm4		\n\t"
				"pmaddwd %%mm6, %%mm1		\n\t"
				"pmaddwd %%mm6, %%mm2		\n\t"
				"pmaddwd %%mm6, %%mm3		\n\t"
#ifndef FAST_BGR2YV12
				"psrad $8, %%mm4		\n\t"
				"psrad $8, %%mm1		\n\t"
				"psrad $8, %%mm2		\n\t"
				"psrad $8, %%mm3		\n\t"
#endif
				"packssdw %%mm1, %%mm4		\n\t"
				"packssdw %%mm3, %%mm2		\n\t"
				"pmaddwd %%mm5, %%mm4		\n\t"
				"pmaddwd %%mm5, %%mm2		\n\t"
				"add $24, %%"REG_d"		\n\t"
				"packssdw %%mm2, %%mm4		\n\t"
				"psraw $7, %%mm4		\n\t"

				"packuswb %%mm4, %%mm0		\n\t"
				"paddusb "MANGLE(bgr2YOffset)", %%mm0	\n\t"

				MOVNTQ" %%mm0, (%1, %%"REG_a")	\n\t"
				"add $8, %%"REG_a"		\n\t"
				" js 1b				\n\t"
				: : "r" (src+width*3), "r" (ydst+width), "g" (-width)
				: "%"REG_a, "%"REG_d
			);
			ydst += lumStride;
			src  += srcStride;
		}
		src -= srcStride*2;
		asm volatile(
			"mov %4, %%"REG_a"		\n\t"
			"movq "MANGLE(w1111)", %%mm5		\n\t"
			"movq "MANGLE(bgr2UCoeff)", %%mm6		\n\t"
			"pxor %%mm7, %%mm7		\n\t"
			"lea (%%"REG_a", %%"REG_a", 2), %%"REG_d"\n\t"
			"add %%"REG_d", %%"REG_d"	\n\t"
			ASMALIGN(4)
			"1:				\n\t"
			PREFETCH" 64(%0, %%"REG_d")	\n\t"
			PREFETCH" 64(%1, %%"REG_d")	\n\t"
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
			"movq (%0, %%"REG_d"), %%mm0	\n\t"
			"movq (%1, %%"REG_d"), %%mm1	\n\t"
			"movq 6(%0, %%"REG_d"), %%mm2	\n\t"
			"movq 6(%1, %%"REG_d"), %%mm3	\n\t"
			PAVGB" %%mm1, %%mm0		\n\t"
			PAVGB" %%mm3, %%mm2		\n\t"
			"movq %%mm0, %%mm1		\n\t"
			"movq %%mm2, %%mm3		\n\t"
			"psrlq $24, %%mm0		\n\t"
			"psrlq $24, %%mm2		\n\t"
			PAVGB" %%mm1, %%mm0		\n\t"
			PAVGB" %%mm3, %%mm2		\n\t"
			"punpcklbw %%mm7, %%mm0		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
#else
			"movd (%0, %%"REG_d"), %%mm0	\n\t"
			"movd (%1, %%"REG_d"), %%mm1	\n\t"
			"movd 3(%0, %%"REG_d"), %%mm2	\n\t"
			"movd 3(%1, %%"REG_d"), %%mm3	\n\t"
			"punpcklbw %%mm7, %%mm0		\n\t"
			"punpcklbw %%mm7, %%mm1		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"punpcklbw %%mm7, %%mm3		\n\t"
			"paddw %%mm1, %%mm0		\n\t"
			"paddw %%mm3, %%mm2		\n\t"
			"paddw %%mm2, %%mm0		\n\t"
			"movd 6(%0, %%"REG_d"), %%mm4	\n\t"
			"movd 6(%1, %%"REG_d"), %%mm1	\n\t"
			"movd 9(%0, %%"REG_d"), %%mm2	\n\t"
			"movd 9(%1, %%"REG_d"), %%mm3	\n\t"
			"punpcklbw %%mm7, %%mm4		\n\t"
			"punpcklbw %%mm7, %%mm1		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"punpcklbw %%mm7, %%mm3		\n\t"
			"paddw %%mm1, %%mm4		\n\t"
			"paddw %%mm3, %%mm2		\n\t"
			"paddw %%mm4, %%mm2		\n\t"
			"psrlw $2, %%mm0		\n\t"
			"psrlw $2, %%mm2		\n\t"
#endif
			"movq "MANGLE(bgr2VCoeff)", %%mm1		\n\t"
			"movq "MANGLE(bgr2VCoeff)", %%mm3		\n\t"

			"pmaddwd %%mm0, %%mm1		\n\t"
			"pmaddwd %%mm2, %%mm3		\n\t"
			"pmaddwd %%mm6, %%mm0		\n\t"
			"pmaddwd %%mm6, %%mm2		\n\t"
#ifndef FAST_BGR2YV12
			"psrad $8, %%mm0		\n\t"
			"psrad $8, %%mm1		\n\t"
			"psrad $8, %%mm2		\n\t"
			"psrad $8, %%mm3		\n\t"
#endif
			"packssdw %%mm2, %%mm0		\n\t"
			"packssdw %%mm3, %%mm1		\n\t"
			"pmaddwd %%mm5, %%mm0		\n\t"
			"pmaddwd %%mm5, %%mm1		\n\t"
			"packssdw %%mm1, %%mm0		\n\t" // V1 V0 U1 U0
			"psraw $7, %%mm0		\n\t"

#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
			"movq 12(%0, %%"REG_d"), %%mm4	\n\t"
			"movq 12(%1, %%"REG_d"), %%mm1	\n\t"
			"movq 18(%0, %%"REG_d"), %%mm2	\n\t"
			"movq 18(%1, %%"REG_d"), %%mm3	\n\t"
			PAVGB" %%mm1, %%mm4		\n\t"
			PAVGB" %%mm3, %%mm2		\n\t"
			"movq %%mm4, %%mm1		\n\t"
			"movq %%mm2, %%mm3		\n\t"
			"psrlq $24, %%mm4		\n\t"
			"psrlq $24, %%mm2		\n\t"
			PAVGB" %%mm1, %%mm4		\n\t"
			PAVGB" %%mm3, %%mm2		\n\t"
			"punpcklbw %%mm7, %%mm4		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
#else
			"movd 12(%0, %%"REG_d"), %%mm4	\n\t"
			"movd 12(%1, %%"REG_d"), %%mm1	\n\t"
			"movd 15(%0, %%"REG_d"), %%mm2	\n\t"
			"movd 15(%1, %%"REG_d"), %%mm3	\n\t"
			"punpcklbw %%mm7, %%mm4		\n\t"
			"punpcklbw %%mm7, %%mm1		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"punpcklbw %%mm7, %%mm3		\n\t"
			"paddw %%mm1, %%mm4		\n\t"
			"paddw %%mm3, %%mm2		\n\t"
			"paddw %%mm2, %%mm4		\n\t"
			"movd 18(%0, %%"REG_d"), %%mm5	\n\t"
			"movd 18(%1, %%"REG_d"), %%mm1	\n\t"
			"movd 21(%0, %%"REG_d"), %%mm2	\n\t"
			"movd 21(%1, %%"REG_d"), %%mm3	\n\t"
			"punpcklbw %%mm7, %%mm5		\n\t"
			"punpcklbw %%mm7, %%mm1		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"punpcklbw %%mm7, %%mm3		\n\t"
			"paddw %%mm1, %%mm5		\n\t"
			"paddw %%mm3, %%mm2		\n\t"
			"paddw %%mm5, %%mm2		\n\t"
			"movq "MANGLE(w1111)", %%mm5		\n\t"
			"psrlw $2, %%mm4		\n\t"
			"psrlw $2, %%mm2		\n\t"
#endif
			"movq "MANGLE(bgr2VCoeff)", %%mm1		\n\t"
			"movq "MANGLE(bgr2VCoeff)", %%mm3		\n\t"

			"pmaddwd %%mm4, %%mm1		\n\t"
			"pmaddwd %%mm2, %%mm3		\n\t"
			"pmaddwd %%mm6, %%mm4		\n\t"
			"pmaddwd %%mm6, %%mm2		\n\t"
#ifndef FAST_BGR2YV12
			"psrad $8, %%mm4		\n\t"
			"psrad $8, %%mm1		\n\t"
			"psrad $8, %%mm2		\n\t"
			"psrad $8, %%mm3		\n\t"
#endif
			"packssdw %%mm2, %%mm4		\n\t"
			"packssdw %%mm3, %%mm1		\n\t"
			"pmaddwd %%mm5, %%mm4		\n\t"
			"pmaddwd %%mm5, %%mm1		\n\t"
			"add $24, %%"REG_d"		\n\t"
			"packssdw %%mm1, %%mm4		\n\t" // V3 V2 U3 U2
			"psraw $7, %%mm4		\n\t"

			"movq %%mm0, %%mm1		\n\t"
			"punpckldq %%mm4, %%mm0		\n\t"
			"punpckhdq %%mm4, %%mm1		\n\t"
			"packsswb %%mm1, %%mm0		\n\t"
			"paddb "MANGLE(bgr2UVOffset)", %%mm0	\n\t"
			"movd %%mm0, (%2, %%"REG_a")	\n\t"
			"punpckhdq %%mm0, %%mm0		\n\t"
			"movd %%mm0, (%3, %%"REG_a")	\n\t"
			"add $4, %%"REG_a"		\n\t"
			" js 1b				\n\t"
			: : "r" (src+chromWidth*6), "r" (src+srcStride+chromWidth*6), "r" (udst+chromWidth), "r" (vdst+chromWidth), "g" (-chromWidth)
			: "%"REG_a, "%"REG_d
		);

		udst += chromStride;
		vdst += chromStride;
		src  += srcStride*2;
	}

	asm volatile(   EMMS" \n\t"
			SFENCE" \n\t"
			:::"memory");
#else
	y=0;
#endif
	for(; y<height; y+=2)
	{
		long i;
		for(i=0; i<chromWidth; i++)
		{
			unsigned int b= src[6*i+0];
			unsigned int g= src[6*i+1];
			unsigned int r= src[6*i+2];

			unsigned int Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;
			unsigned int V  =  ((RV*r + GV*g + BV*b)>>RGB2YUV_SHIFT) + 128;
			unsigned int U  =  ((RU*r + GU*g + BU*b)>>RGB2YUV_SHIFT) + 128;

			udst[i] 	= U;
			vdst[i] 	= V;
			ydst[2*i] 	= Y;

			b= src[6*i+3];
			g= src[6*i+4];
			r= src[6*i+5];

			Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;
			ydst[2*i+1] 	= Y;
		}
		ydst += lumStride;
		src  += srcStride;

		for(i=0; i<chromWidth; i++)
		{
			unsigned int b= src[6*i+0];
			unsigned int g= src[6*i+1];
			unsigned int r= src[6*i+2];

			unsigned int Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;

			ydst[2*i] 	= Y;

			b= src[6*i+3];
			g= src[6*i+4];
			r= src[6*i+5];

			Y  =  ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;
			ydst[2*i+1] 	= Y;
		}
		udst += chromStride;
		vdst += chromStride;
		ydst += lumStride;
		src  += srcStride;
	}
}

void RENAME(interleaveBytes)(uint8_t *src1, uint8_t *src2, uint8_t *dest,
			    long width, long height, long src1Stride,
			    long src2Stride, long dstStride){
	long h;

	for(h=0; h < height; h++)
	{
		long w;

#ifdef HAVE_MMX
#ifdef HAVE_SSE2
		asm(
			"xor %%"REG_a", %%"REG_a"	\n\t"
			"1:				\n\t"
			PREFETCH" 64(%1, %%"REG_a")	\n\t"
			PREFETCH" 64(%2, %%"REG_a")	\n\t"
			"movdqa (%1, %%"REG_a"), %%xmm0	\n\t"
			"movdqa (%1, %%"REG_a"), %%xmm1	\n\t"
			"movdqa (%2, %%"REG_a"), %%xmm2	\n\t"
			"punpcklbw %%xmm2, %%xmm0	\n\t"
			"punpckhbw %%xmm2, %%xmm1	\n\t"
			"movntdq %%xmm0, (%0, %%"REG_a", 2)\n\t"
			"movntdq %%xmm1, 16(%0, %%"REG_a", 2)\n\t"
			"add $16, %%"REG_a"		\n\t"
			"cmp %3, %%"REG_a"		\n\t"
			" jb 1b				\n\t"
			::"r"(dest), "r"(src1), "r"(src2), "r" (width-15)
			: "memory", "%"REG_a""
		);
#else
		asm(
			"xor %%"REG_a", %%"REG_a"	\n\t"
			"1:				\n\t"
			PREFETCH" 64(%1, %%"REG_a")	\n\t"
			PREFETCH" 64(%2, %%"REG_a")	\n\t"
			"movq (%1, %%"REG_a"), %%mm0	\n\t"
			"movq 8(%1, %%"REG_a"), %%mm2	\n\t"
			"movq %%mm0, %%mm1		\n\t"
			"movq %%mm2, %%mm3		\n\t"
			"movq (%2, %%"REG_a"), %%mm4	\n\t"
			"movq 8(%2, %%"REG_a"), %%mm5	\n\t"
			"punpcklbw %%mm4, %%mm0		\n\t"
			"punpckhbw %%mm4, %%mm1		\n\t"
			"punpcklbw %%mm5, %%mm2		\n\t"
			"punpckhbw %%mm5, %%mm3		\n\t"
			MOVNTQ" %%mm0, (%0, %%"REG_a", 2)\n\t"
			MOVNTQ" %%mm1, 8(%0, %%"REG_a", 2)\n\t"
			MOVNTQ" %%mm2, 16(%0, %%"REG_a", 2)\n\t"
			MOVNTQ" %%mm3, 24(%0, %%"REG_a", 2)\n\t"
			"add $16, %%"REG_a"		\n\t"
			"cmp %3, %%"REG_a"		\n\t"
			" jb 1b				\n\t"
			::"r"(dest), "r"(src1), "r"(src2), "r" (width-15)
			: "memory", "%"REG_a
		);
#endif
		for(w= (width&(~15)); w < width; w++)
		{
			dest[2*w+0] = src1[w];
			dest[2*w+1] = src2[w];
		}
#else
		for(w=0; w < width; w++)
		{
			dest[2*w+0] = src1[w];
			dest[2*w+1] = src2[w];
		}
#endif
		dest += dstStride;
                src1 += src1Stride;
                src2 += src2Stride;
	}
#ifdef HAVE_MMX
	asm(
		EMMS" \n\t"
		SFENCE" \n\t"
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
    long y,x,w,h;
    w=width/2; h=height/2;
#ifdef HAVE_MMX
    asm volatile(
	PREFETCH" %0\n\t"
	PREFETCH" %1\n\t"
	::"m"(*(src1+srcStride1)),"m"(*(src2+srcStride2)):"memory");
#endif
    for(y=0;y<h;y++){
	const uint8_t* s1=src1+srcStride1*(y>>1);
	uint8_t* d=dst1+dstStride1*y;
	x=0;
#ifdef HAVE_MMX
	for(;x<w-31;x+=32)
	{
	    asm volatile(
		PREFETCH" 32%1\n\t"
	        "movq	%1, %%mm0\n\t"
	        "movq	8%1, %%mm2\n\t"
	        "movq	16%1, %%mm4\n\t"
	        "movq	24%1, %%mm6\n\t"
	        "movq	%%mm0, %%mm1\n\t"
	        "movq	%%mm2, %%mm3\n\t"
	        "movq	%%mm4, %%mm5\n\t"
	        "movq	%%mm6, %%mm7\n\t"
		"punpcklbw %%mm0, %%mm0\n\t"
		"punpckhbw %%mm1, %%mm1\n\t"
		"punpcklbw %%mm2, %%mm2\n\t"
		"punpckhbw %%mm3, %%mm3\n\t"
		"punpcklbw %%mm4, %%mm4\n\t"
		"punpckhbw %%mm5, %%mm5\n\t"
		"punpcklbw %%mm6, %%mm6\n\t"
		"punpckhbw %%mm7, %%mm7\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		MOVNTQ"	%%mm1, 8%0\n\t"
		MOVNTQ"	%%mm2, 16%0\n\t"
		MOVNTQ"	%%mm3, 24%0\n\t"
		MOVNTQ"	%%mm4, 32%0\n\t"
		MOVNTQ"	%%mm5, 40%0\n\t"
		MOVNTQ"	%%mm6, 48%0\n\t"
		MOVNTQ"	%%mm7, 56%0"
		:"=m"(d[2*x])
		:"m"(s1[x])
		:"memory");
	}
#endif
	for(;x<w;x++) d[2*x]=d[2*x+1]=s1[x];
    }
    for(y=0;y<h;y++){
	const uint8_t* s2=src2+srcStride2*(y>>1);
	uint8_t* d=dst2+dstStride2*y;
	x=0;
#ifdef HAVE_MMX
	for(;x<w-31;x+=32)
	{
	    asm volatile(
		PREFETCH" 32%1\n\t"
	        "movq	%1, %%mm0\n\t"
	        "movq	8%1, %%mm2\n\t"
	        "movq	16%1, %%mm4\n\t"
	        "movq	24%1, %%mm6\n\t"
	        "movq	%%mm0, %%mm1\n\t"
	        "movq	%%mm2, %%mm3\n\t"
	        "movq	%%mm4, %%mm5\n\t"
	        "movq	%%mm6, %%mm7\n\t"
		"punpcklbw %%mm0, %%mm0\n\t"
		"punpckhbw %%mm1, %%mm1\n\t"
		"punpcklbw %%mm2, %%mm2\n\t"
		"punpckhbw %%mm3, %%mm3\n\t"
		"punpcklbw %%mm4, %%mm4\n\t"
		"punpckhbw %%mm5, %%mm5\n\t"
		"punpcklbw %%mm6, %%mm6\n\t"
		"punpckhbw %%mm7, %%mm7\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		MOVNTQ"	%%mm1, 8%0\n\t"
		MOVNTQ"	%%mm2, 16%0\n\t"
		MOVNTQ"	%%mm3, 24%0\n\t"
		MOVNTQ"	%%mm4, 32%0\n\t"
		MOVNTQ"	%%mm5, 40%0\n\t"
		MOVNTQ"	%%mm6, 48%0\n\t"
		MOVNTQ"	%%mm7, 56%0"
		:"=m"(d[2*x])
		:"m"(s2[x])
		:"memory");
	}
#endif
	for(;x<w;x++) d[2*x]=d[2*x+1]=s2[x];
    }
#ifdef HAVE_MMX
	asm(
		EMMS" \n\t"
		SFENCE" \n\t"
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
    long y,x,w,h;
    w=width/2; h=height;
    for(y=0;y<h;y++){
	const uint8_t* yp=src1+srcStride1*y;
	const uint8_t* up=src2+srcStride2*(y>>2);
	const uint8_t* vp=src3+srcStride3*(y>>2);
	uint8_t* d=dst+dstStride*y;
	x=0;
#ifdef HAVE_MMX
	for(;x<w-7;x+=8)
	{
	    asm volatile(
		PREFETCH" 32(%1, %0)\n\t"
		PREFETCH" 32(%2, %0)\n\t"
		PREFETCH" 32(%3, %0)\n\t"
		"movq	(%1, %0, 4), %%mm0\n\t"       /* Y0Y1Y2Y3Y4Y5Y6Y7 */
		"movq	(%2, %0), %%mm1\n\t"       /* U0U1U2U3U4U5U6U7 */
		"movq	(%3, %0), %%mm2\n\t"	     /* V0V1V2V3V4V5V6V7 */
		"movq	%%mm0, %%mm3\n\t"    /* Y0Y1Y2Y3Y4Y5Y6Y7 */
		"movq	%%mm1, %%mm4\n\t"    /* U0U1U2U3U4U5U6U7 */
		"movq	%%mm2, %%mm5\n\t"    /* V0V1V2V3V4V5V6V7 */
		"punpcklbw %%mm1, %%mm1\n\t" /* U0U0 U1U1 U2U2 U3U3 */
		"punpcklbw %%mm2, %%mm2\n\t" /* V0V0 V1V1 V2V2 V3V3 */
		"punpckhbw %%mm4, %%mm4\n\t" /* U4U4 U5U5 U6U6 U7U7 */
		"punpckhbw %%mm5, %%mm5\n\t" /* V4V4 V5V5 V6V6 V7V7 */

		"movq	%%mm1, %%mm6\n\t"
		"punpcklbw %%mm2, %%mm1\n\t" /* U0V0 U0V0 U1V1 U1V1*/
		"punpcklbw %%mm1, %%mm0\n\t" /* Y0U0 Y1V0 Y2U0 Y3V0*/
		"punpckhbw %%mm1, %%mm3\n\t" /* Y4U1 Y5V1 Y6U1 Y7V1*/
		MOVNTQ"	%%mm0, (%4, %0, 8)\n\t"
		MOVNTQ"	%%mm3, 8(%4, %0, 8)\n\t"
		
		"punpckhbw %%mm2, %%mm6\n\t" /* U2V2 U2V2 U3V3 U3V3*/
		"movq	8(%1, %0, 4), %%mm0\n\t"
		"movq	%%mm0, %%mm3\n\t"
		"punpcklbw %%mm6, %%mm0\n\t" /* Y U2 Y V2 Y U2 Y V2*/
		"punpckhbw %%mm6, %%mm3\n\t" /* Y U3 Y V3 Y U3 Y V3*/
		MOVNTQ"	%%mm0, 16(%4, %0, 8)\n\t"
		MOVNTQ"	%%mm3, 24(%4, %0, 8)\n\t"

		"movq	%%mm4, %%mm6\n\t"
		"movq	16(%1, %0, 4), %%mm0\n\t"
		"movq	%%mm0, %%mm3\n\t"
		"punpcklbw %%mm5, %%mm4\n\t"
		"punpcklbw %%mm4, %%mm0\n\t" /* Y U4 Y V4 Y U4 Y V4*/
		"punpckhbw %%mm4, %%mm3\n\t" /* Y U5 Y V5 Y U5 Y V5*/
		MOVNTQ"	%%mm0, 32(%4, %0, 8)\n\t"
		MOVNTQ"	%%mm3, 40(%4, %0, 8)\n\t"
		
		"punpckhbw %%mm5, %%mm6\n\t"
		"movq	24(%1, %0, 4), %%mm0\n\t"
		"movq	%%mm0, %%mm3\n\t"
		"punpcklbw %%mm6, %%mm0\n\t" /* Y U6 Y V6 Y U6 Y V6*/
		"punpckhbw %%mm6, %%mm3\n\t" /* Y U7 Y V7 Y U7 Y V7*/
		MOVNTQ"	%%mm0, 48(%4, %0, 8)\n\t"
		MOVNTQ"	%%mm3, 56(%4, %0, 8)\n\t"

		: "+r" (x)
                : "r"(yp), "r" (up), "r"(vp), "r"(d)
		:"memory");
	}
#endif
	for(; x<w; x++)
	{
	    const long x2= x<<2;
	    d[8*x+0]=yp[x2];
	    d[8*x+1]=up[x];
	    d[8*x+2]=yp[x2+1];
	    d[8*x+3]=vp[x];
	    d[8*x+4]=yp[x2+2];
	    d[8*x+5]=up[x];
	    d[8*x+6]=yp[x2+3];
	    d[8*x+7]=vp[x];
	}
    }
#ifdef HAVE_MMX
	asm(
		EMMS" \n\t"
		SFENCE" \n\t"
		::: "memory"
		);
#endif
}
