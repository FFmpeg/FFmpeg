#include <inttypes.h>
#include "../config.h"
#include "rgb2rgb.h"
#include "mmx.h"

#ifdef HAVE_3DNOW
#define PREFETCH "prefetch"
#define PREFETCHW "prefetchw"
#elif defined ( HAVE_MMX2 )
#define PREFETCH "prefetchnta"
#define PREFETCHW "prefetcht0"
#endif

#ifdef HAVE_3DNOW
#define EMMS "femms"
#else
#define EMMS "emms"
#endif

#ifdef HAVE_MMX2
#define MOVNTQ "movntq"
#else
#define MOVNTQ "movq"
#endif

#ifdef HAVE_MMX2
#define SFENCE "sfence"
#endif

void rgb24to32(uint8_t *src,uint8_t *dst,uint32_t src_size)
{
  uint8_t *dest = dst;
  uint8_t *s = src;
  uint8_t *end;
#ifdef HAVE_MMX
  const uint64_t mask32 = 0x00FFFFFF00FFFFFFULL;
  uint8_t *mm_end;
#endif
  end = s + src_size;
#ifdef HAVE_MMX
#ifdef PREFETCH
  __asm __volatile(
    PREFETCH" %0\n\t"
    ::"m"(*s):"memory");
#endif
  mm_end = (uint8_t*)((((unsigned long)end)/16)*16);
  __asm __volatile("movq %0, %%mm7"::"m"(mask32):"memory");
  while(s < mm_end)
  {
#ifdef PREFETCH
    __asm __volatile(
	PREFETCH" 32%0\n\t"
	::"m"(*s):"memory");
#endif
    __asm __volatile(
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
#ifdef SFENCE
  __asm __volatile(SFENCE:::"memory");
#endif
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

/* TODO: MMX optimization */
void rgb32to24(uint8_t *src,uint8_t *dst,uint32_t src_size)
{
  uint8_t *dest = dst;
  uint8_t *s = src;
  uint8_t *end;
  end = s + src_size;
  while(s < end)
  {
    *dest++ = *s++;
    *dest++ = *s++;
    *dest++ = *s++;
    s++;
  }
}

/* Original by Strepto/Astral
 ported to gcc & bugfixed : A'rpi */
void rgb15to16(uint8_t *src,uint8_t *dst,uint32_t src_size)
{
#ifdef HAVE_MMX
  static uint64_t mask_b  = 0x001F001F001F001FLL; // 00000000 00011111  xxB
  static uint64_t mask_rg = 0x7FE07FE07FE07FE0LL; // 01111111 11100000  RGx
  register char* s=src+src_size;
  register char* d=dst+src_size;
  register int offs=-src_size;
  movq_m2r (mask_b,  mm4);
  movq_m2r (mask_rg, mm5);
  while(offs<0){
    movq_m2r (*(s+offs), mm0);
    movq_r2r (mm0, mm1);

    movq_m2r (*(s+8+offs), mm2);
    movq_r2r (mm2, mm3);
    
    pand_r2r (mm4, mm0);
    pand_r2r (mm5, mm1);
    
    psllq_i2r(1,mm1);
    pand_r2r (mm4, mm2);

    pand_r2r (mm5, mm3);
    por_r2r  (mm1, mm0);

    psllq_i2r(1,mm3);
    movq_r2m (mm0,*(d+offs));

    por_r2r  (mm3,mm2);
    movq_r2m (mm2,*(d+8+offs));

    offs+=16;
  }
  emms();
#else
   uint16_t *s1=( uint16_t * )src;
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
#endif
}
