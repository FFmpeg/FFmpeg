/*
 *
 *  rgb2rgb.c, Software RGB to RGB convertor
 *  Written by Nick Kurshev.
 *  palette stuff & yuv stuff by Michael
 */
#include <inttypes.h>
#include "../config.h"
#include "rgb2rgb.h"
#include "../mmx_defs.h"

#ifdef HAVE_MMX
static const uint64_t mask32   __attribute__((aligned(8))) = 0x00FFFFFF00FFFFFFULL;
static const uint64_t mask24l  __attribute__((aligned(8))) = 0x0000000000FFFFFFULL;
static const uint64_t mask24h  __attribute__((aligned(8))) = 0x0000FFFFFF000000ULL;
static const uint64_t mask15b  __attribute__((aligned(8))) = 0x001F001F001F001FULL; /* 00000000 00011111  xxB */
static const uint64_t mask15rg __attribute__((aligned(8))) = 0x7FE07FE07FE07FE0ULL; /* 01111111 11100000  RGx */
static const uint64_t mask15s  __attribute__((aligned(8))) = 0xFFE0FFE0FFE0FFE0ULL;
#endif

void rgb24to32(const uint8_t *src,uint8_t *dst,unsigned src_size)
{
  uint8_t *dest = dst;
  const uint8_t *s = src;
  const uint8_t *end;
#ifdef HAVE_MMX
  uint8_t *mm_end;
#endif
  end = s + src_size;
#ifdef HAVE_MMX
  __asm __volatile(PREFETCH"	%0"::"m"(*s):"memory");
  mm_end = (uint8_t*)((((unsigned long)end)/(MMREG_SIZE*2))*(MMREG_SIZE*2));
  __asm __volatile("movq	%0, %%mm7"::"m"(mask32):"memory");
  if(mm_end == end) mm_end -= MMREG_SIZE*2;
  while(s < mm_end)
  {
    __asm __volatile(
	PREFETCH"	32%1\n\t"
	"movd	%1, %%mm0\n\t"
	"movd	3%1, %%mm1\n\t"
	"movd	6%1, %%mm2\n\t"
	"movd	9%1, %%mm3\n\t"
	"punpckldq %%mm1, %%mm0\n\t"
	"punpckldq %%mm3, %%mm2\n\t"
	"pand	%%mm7, %%mm0\n\t"
	"pand	%%mm7, %%mm2\n\t"
	MOVNTQ"	%%mm0, %0\n\t"
	MOVNTQ"	%%mm2, 8%0"
	:"=m"(*dest)
	:"m"(*s)
	:"memory");
    dest += 16;
    s += 12;
  }
  __asm __volatile(SFENCE:::"memory");
  __asm __volatile(EMMS:::"memory");
#endif
  while(s < end)
  {
    *dest++ = *s++;
    *dest++ = *s++;
    *dest++ = *s++;
    *dest++ = 0;
  }
}

void rgb32to24(const uint8_t *src,uint8_t *dst,unsigned src_size)
{
  uint8_t *dest = dst;
  const uint8_t *s = src;
  const uint8_t *end;
#ifdef HAVE_MMX
  uint8_t *mm_end;
#endif
  end = s + src_size;
#ifdef HAVE_MMX
  __asm __volatile(PREFETCH"	%0"::"m"(*s):"memory");
  mm_end = (uint8_t*)((((unsigned long)end)/(MMREG_SIZE*2))*(MMREG_SIZE*2));
  __asm __volatile(
	"movq	%0, %%mm7\n\t"
	"movq	%1, %%mm6"
	::"m"(mask24l),"m"(mask24h):"memory");
  if(mm_end == end) mm_end -= MMREG_SIZE*2;
  while(s < mm_end)
  {
    __asm __volatile(
	PREFETCH"	32%1\n\t"
	"movq	%1, %%mm0\n\t"
	"movq	8%1, %%mm1\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"psrlq	$8, %%mm2\n\t"
	"psrlq	$8, %%mm3\n\t"
	"pand	%%mm7, %%mm0\n\t"
	"pand	%%mm7, %%mm1\n\t"
	"pand	%%mm6, %%mm2\n\t"
	"pand	%%mm6, %%mm3\n\t"
	"por	%%mm2, %%mm0\n\t"
	"por	%%mm3, %%mm1\n\t"
	MOVNTQ"	%%mm0, %0\n\t"
	MOVNTQ"	%%mm1, 6%0"
	:"=m"(*dest)
	:"m"(*s)
	:"memory");
    dest += 12;
    s += 16;
  }
  __asm __volatile(SFENCE:::"memory");
  __asm __volatile(EMMS:::"memory");
#endif
  while(s < end)
  {
    *dest++ = *s++;
    *dest++ = *s++;
    *dest++ = *s++;
    s++;
  }
}

/*
 Original by Strepto/Astral
 ported to gcc & bugfixed : A'rpi
 MMX2, 3DNOW optimization by Nick Kurshev
 32bit c version, and and&add trick by Michael Niedermayer
*/
void rgb15to16(const uint8_t *src,uint8_t *dst,unsigned src_size)
{
#ifdef HAVE_MMX
  register const char* s=src+src_size;
  register char* d=dst+src_size;
  register int offs=-src_size;
  __asm __volatile(PREFETCH"	%0"::"m"(*(s+offs)));
  __asm __volatile(
	"movq	%0, %%mm4\n\t"
	::"m"(mask15s));
  while(offs<0)
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
		:"=m"(*(d+offs))
		:"m"(*(s+offs))
		);
	offs+=16;
  }
  __asm __volatile(SFENCE:::"memory");
  __asm __volatile(EMMS:::"memory");
#else
#if 0
   const uint16_t *s1=( uint16_t * )src;
   uint16_t *d1=( uint16_t * )dst;
   uint16_t *e=((uint8_t *)s1)+src_size;
   while( s1<e ){
     register int x=*( s1++ );
     /* rrrrrggggggbbbbb
        0rrrrrgggggbbbbb
        0111 1111 1110 0000=0x7FE0
        00000000000001 1111=0x001F */
     *( d1++ )=( x&0x001F )|( ( x&0x7FE0 )<<1 );
   }
#else
	const unsigned *s1=( unsigned * )src;
	unsigned *d1=( unsigned * )dst;
	int i;
	int size= src_size>>2;
	for(i=0; i<size; i++)
	{
		register int x= s1[i];
//		d1[i] = x + (x&0x7FE07FE0); //faster but need msbit =0 which might not allways be true
		d1[i] = (x&0x7FFF7FFF) + (x&0x7FE07FE0);

	}
#endif
#endif
}

/**
 * Pallete is assumed to contain bgr32
 */
void palette8torgb32(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette)
{
	unsigned i;
	for(i=0; i<num_pixels; i++)
		((unsigned *)dst)[i] = ((unsigned *)palette)[ src[i] ];
}

/**
 * Pallete is assumed to contain bgr32
 */
void palette8torgb24(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette)
{
	unsigned i;
/*
	writes 1 byte o much and might cause alignment issues on some architectures?
	for(i=0; i<num_pixels; i++)
		((unsigned *)(&dst[i*3])) = ((unsigned *)palette)[ src[i] ];
*/
	for(i=0; i<num_pixels; i++)
	{
		//FIXME slow?
		dst[0]= palette[ src[i]*4+0 ];
		dst[1]= palette[ src[i]*4+1 ];
		dst[2]= palette[ src[i]*4+2 ];
		dst+= 3;
	}
}

void rgb32to16(const uint8_t *src, uint8_t *dst, unsigned src_size)
{
	unsigned j,i,num_pixels=src_size/4;
	uint16_t *d = (uint16_t *)dst;
	for(i=0,j=0; j<num_pixels; i+=4,j++)
	{
		const int b= src[i+0];
		const int g= src[i+1];
		const int r= src[i+2];

		d[j]= (b>>3) | ((g&0xFC)<<3) | ((r&0xF8)<<8);
	}
}

void rgb32to15(const uint8_t *src, uint8_t *dst, unsigned src_size)
{
	unsigned j,i,num_pixels=src_size/4;
	uint16_t *d = (uint16_t *)dst;
	for(i=0,j=0; j<num_pixels; i+=4,j++)
	{
		const int b= src[i+0];
		const int g= src[i+1];
		const int r= src[i+2];

		d[j]= (b>>3) | ((g&0xF8)<<2) | ((r&0xF8)<<7);
	}
}

void rgb24to16(const uint8_t *src, uint8_t *dst, unsigned src_size)
{
	unsigned j,i,num_pixels=src_size/3;
	uint16_t *d = (uint16_t *)dst;
	for(i=0,j=0; j<num_pixels; i+=3,j++)
	{
		const int b= src[i+0];
		const int g= src[i+1];
		const int r= src[i+2];

		d[j]= (b>>3) | ((g&0xFC)<<3) | ((r&0xF8)<<8);
	}
}

void rgb24to15(const uint8_t *src, uint8_t *dst, unsigned src_size)
{
	unsigned j,i,num_pixels=src_size/3;
	uint16_t *d = (uint16_t *)dst;
	for(i=0,j=0; j<num_pixels; i+=3,j++)
	{
		const int b= src[i+0];
		const int g= src[i+1];
		const int r= src[i+2];

		d[j]= (b>>3) | ((g&0xF8)<<2) | ((r&0xF8)<<7);
	}
}

/**
 * Palette is assumed to contain bgr16, see rgb32to16 to convert the palette
 */
void palette8torgb16(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette)
{
	unsigned i;
	for(i=0; i<num_pixels; i++)
		((uint16_t *)dst)[i] = ((uint16_t *)palette)[ src[i] ];
}

/**
 * Pallete is assumed to contain bgr15, see rgb32to15 to convert the palette
 */
void palette8torgb15(const uint8_t *src, uint8_t *dst, unsigned num_pixels, const uint8_t *palette)
{
	unsigned i;
	for(i=0; i<num_pixels; i++)
		((uint16_t *)dst)[i] = ((uint16_t *)palette)[ src[i] ];
}
/**
 *
 * num_pixels must be a multiple of 16 for the MMX version
 */
void yv12toyuy2(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst, unsigned num_pixels)
{
#ifdef HAVE_MMX
	asm volatile(
		"xorl %%eax, %%eax		\n\t"
		"1:				\n\t"
		PREFETCH" 32(%1, %%eax, 2)	\n\t"
		PREFETCH" 32(%2, %%eax)		\n\t"
		PREFETCH" 32(%3, %%eax)		\n\t"
		"movq (%2, %%eax), %%mm0	\n\t" // U(0)
		"movq %%mm0, %%mm2		\n\t" // U(0)
		"movq (%3, %%eax), %%mm1	\n\t" // V(0)
		"punpcklbw %%mm1, %%mm0		\n\t" // UVUV UVUV(0)
		"punpckhbw %%mm1, %%mm2		\n\t" // UVUV UVUV(8)

		"movq (%1, %%eax,2), %%mm3	\n\t" // Y(0)
		"movq 8(%1, %%eax,2), %%mm5	\n\t" // Y(8)
		"movq %%mm3, %%mm4		\n\t" // Y(0)
		"movq %%mm5, %%mm6		\n\t" // Y(8)
		"punpcklbw %%mm0, %%mm3		\n\t" // YUYV YUYV(0)
		"punpckhbw %%mm0, %%mm4		\n\t" // YUYV YUYV(4)
		"punpcklbw %%mm2, %%mm5		\n\t" // YUYV YUYV(8)
		"punpckhbw %%mm2, %%mm6		\n\t" // YUYV YUYV(12)

		MOVNTQ" %%mm3, (%0, %%eax, 4)	\n\t"
		MOVNTQ" %%mm4, 8(%0, %%eax, 4)	\n\t"
		MOVNTQ" %%mm5, 16(%0, %%eax, 4)	\n\t"
		MOVNTQ" %%mm6, 24(%0, %%eax, 4)	\n\t"

		"addl $8, %%eax			\n\t"
		"cmpl %4, %%eax			\n\t"
		" jb 1b				\n\t"
		EMMS" \n\t"
		SFENCE
		::"r"(dst), "r"(ysrc), "r"(usrc), "r"(vsrc), "r" (num_pixels>>1)
		: "memory", "%eax"
	);

#else
	int i;
	num_pixels>>=1;
	for(i=0; i<num_pixels; i++)
	{
		dst[4*i+0] = ysrc[2*i+0];
		dst[4*i+1] = usrc[i];
		dst[4*i+2] = ysrc[2*i+1];
		dst[4*i+3] = vsrc[i];
	}
#endif
}

void yuy2toyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst, unsigned num_pixels)
{
#ifdef HAVE_MMX
	asm volatile(
		"xorl %%eax, %%eax		\n\t"
		"pcmpeqw %%mm7, %%mm7		\n\t"
		"psrlw $8, %%mm7		\n\t" // FF,00,FF,00...
		"1:				\n\t"
		PREFETCH" 64(%0, %%eax, 4)	\n\t"
		"movq (%0, %%eax, 4), %%mm0	\n\t" // YUYV YUYV(0)
		"movq 8(%0, %%eax, 4), %%mm1	\n\t" // YUYV YUYV(4)
		"movq %%mm0, %%mm2		\n\t" // YUYV YUYV(0)
		"movq %%mm1, %%mm3		\n\t" // YUYV YUYV(4)
		"psrlw $8, %%mm0		\n\t" // U0V0 U0V0(0)
		"psrlw $8, %%mm1		\n\t" // U0V0 U0V0(4)
		"pand %%mm7, %%mm2		\n\t" // Y0Y0 Y0Y0(0)
		"pand %%mm7, %%mm3		\n\t" // Y0Y0 Y0Y0(4)
		"packuswb %%mm1, %%mm0		\n\t" // UVUV UVUV(0)
		"packuswb %%mm3, %%mm2		\n\t" // YYYY YYYY(0)

		MOVNTQ" %%mm2, (%1, %%eax, 2)	\n\t"

		"movq 16(%0, %%eax, 4), %%mm1	\n\t" // YUYV YUYV(8)
		"movq 24(%0, %%eax, 4), %%mm2	\n\t" // YUYV YUYV(12)
		"movq %%mm1, %%mm3		\n\t" // YUYV YUYV(8)
		"movq %%mm2, %%mm4		\n\t" // YUYV YUYV(12)
		"psrlw $8, %%mm1		\n\t" // U0V0 U0V0(8)
		"psrlw $8, %%mm2		\n\t" // U0V0 U0V0(12)
		"pand %%mm7, %%mm3		\n\t" // Y0Y0 Y0Y0(8)
		"pand %%mm7, %%mm4		\n\t" // Y0Y0 Y0Y0(12)
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
		EMMS" \n\t"
		SFENCE
		::"r"(src), "r"(ydst), "r"(udst), "r"(vdst), "r" (num_pixels>>1)
		: "memory", "%eax"
	);
#else
	int i;
	num_pixels>>=1;
	for(i=0; i<num_pixels; i++)
	{
		 ydst[2*i+0] 	= src[4*i+0];
		 udst[i] 	= src[4*i+1];
		 ydst[2*i+1] 	= src[4*i+2];
		 vdst[i] 	= src[4*i+3];
	}
#endif
}