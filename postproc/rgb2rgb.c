/*
 *
 *  rgb2rgb.c, Software RGB to RGB convertor
 *  pluralize by Software PAL8 to RGB convertor
 *               Software YUV to YUV convertor
 *               Software YUV to RGB convertor
 *  Written by Nick Kurshev.
 *  palette & yuv & runtime cpu stuff by Michael (michaelni@gmx.at) (under GPL)
 */
#include <inttypes.h>
#include "../config.h"
#include "rgb2rgb.h"
#include "../cpudetect.h"
#include "../mangle.h"

#ifdef ARCH_X86
#define CAN_COMPILE_X86_ASM
#endif

#define FAST_BGR2YV12 // use 7 bit coeffs instead of 15bit

#ifdef CAN_COMPILE_X86_ASM
static const uint64_t mmx_null  __attribute__((aligned(8))) = 0x0000000000000000ULL;
static const uint64_t mmx_one   __attribute__((aligned(8))) = 0xFFFFFFFFFFFFFFFFULL;
static const uint64_t mask32b  __attribute__((aligned(8))) = 0x000000FF000000FFULL;
static const uint64_t mask32g  __attribute__((aligned(8))) = 0x0000FF000000FF00ULL;
static const uint64_t mask32r  __attribute__((aligned(8))) = 0x00FF000000FF0000ULL;
static const uint64_t mask32   __attribute__((aligned(8))) = 0x00FFFFFF00FFFFFFULL;
static const uint64_t mask24b  __attribute__((aligned(8))) = 0x00FF0000FF0000FFULL;
static const uint64_t mask24g  __attribute__((aligned(8))) = 0xFF0000FF0000FF00ULL;
static const uint64_t mask24r  __attribute__((aligned(8))) = 0x0000FF0000FF0000ULL;
static const uint64_t mask24l  __attribute__((aligned(8))) = 0x0000000000FFFFFFULL;
static const uint64_t mask24h  __attribute__((aligned(8))) = 0x0000FFFFFF000000ULL;
static const uint64_t mask24hh  __attribute__((aligned(8))) = 0xffff000000000000ULL;
static const uint64_t mask24hhh  __attribute__((aligned(8))) = 0xffffffff00000000ULL;
static const uint64_t mask24hhhh  __attribute__((aligned(8))) = 0xffffffffffff0000ULL;
static const uint64_t mask15b  __attribute__((aligned(8))) = 0x001F001F001F001FULL; /* 00000000 00011111  xxB */
static const uint64_t mask15rg __attribute__((aligned(8))) = 0x7FE07FE07FE07FE0ULL; /* 01111111 11100000  RGx */
static const uint64_t mask15s  __attribute__((aligned(8))) = 0xFFE0FFE0FFE0FFE0ULL;
static const uint64_t mask15g  __attribute__((aligned(8))) = 0x03E003E003E003E0ULL;
static const uint64_t mask15r  __attribute__((aligned(8))) = 0x7C007C007C007C00ULL;
#define mask16b mask15b
static const uint64_t mask16g  __attribute__((aligned(8))) = 0x07E007E007E007E0ULL;
static const uint64_t mask16r  __attribute__((aligned(8))) = 0xF800F800F800F800ULL;
static const uint64_t red_16mask  __attribute__((aligned(8))) = 0x0000f8000000f800ULL;
static const uint64_t green_16mask __attribute__((aligned(8)))= 0x000007e0000007e0ULL;
static const uint64_t blue_16mask __attribute__((aligned(8))) = 0x0000001f0000001fULL;
static const uint64_t red_15mask  __attribute__((aligned(8))) = 0x00007c000000f800ULL;
static const uint64_t green_15mask __attribute__((aligned(8)))= 0x000003e0000007e0ULL;
static const uint64_t blue_15mask __attribute__((aligned(8))) = 0x0000001f0000001fULL;

#ifdef FAST_BGR2YV12
static const uint64_t bgr2YCoeff  __attribute__((aligned(8))) = 0x000000210041000DULL;
static const uint64_t bgr2UCoeff  __attribute__((aligned(8))) = 0x0000FFEEFFDC0038ULL;
static const uint64_t bgr2VCoeff  __attribute__((aligned(8))) = 0x00000038FFD2FFF8ULL;
#else
static const uint64_t bgr2YCoeff  __attribute__((aligned(8))) = 0x000020E540830C8BULL;
static const uint64_t bgr2UCoeff  __attribute__((aligned(8))) = 0x0000ED0FDAC23831ULL;
static const uint64_t bgr2VCoeff  __attribute__((aligned(8))) = 0x00003831D0E6F6EAULL;
#endif
static const uint64_t bgr2YOffset __attribute__((aligned(8))) = 0x1010101010101010ULL;
static const uint64_t bgr2UVOffset __attribute__((aligned(8)))= 0x8080808080808080ULL;
static const uint64_t w1111       __attribute__((aligned(8))) = 0x0001000100010001ULL;

#if 0
static volatile uint64_t __attribute__((aligned(8))) b5Dither;
static volatile uint64_t __attribute__((aligned(8))) g5Dither;
static volatile uint64_t __attribute__((aligned(8))) g6Dither;
static volatile uint64_t __attribute__((aligned(8))) r5Dither;

static uint64_t __attribute__((aligned(8))) dither4[2]={
	0x0103010301030103LL,
	0x0200020002000200LL,};

static uint64_t __attribute__((aligned(8))) dither8[2]={
	0x0602060206020602LL,
	0x0004000400040004LL,};
#endif
#endif

#define RGB2YUV_SHIFT 8
#define BY ((int)( 0.098*(1<<RGB2YUV_SHIFT)+0.5))
#define BV ((int)(-0.071*(1<<RGB2YUV_SHIFT)+0.5))
#define BU ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define GY ((int)( 0.504*(1<<RGB2YUV_SHIFT)+0.5))
#define GV ((int)(-0.368*(1<<RGB2YUV_SHIFT)+0.5))
#define GU ((int)(-0.291*(1<<RGB2YUV_SHIFT)+0.5))
#define RY ((int)( 0.257*(1<<RGB2YUV_SHIFT)+0.5))
#define RV ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define RU ((int)(-0.148*(1<<RGB2YUV_SHIFT)+0.5))

//Note: we have C, MMX, MMX2, 3DNOW version therse no 3DNOW+MMX2 one
//Plain C versions
#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#undef ARCH_X86
#undef HAVE_SSE2
#define RENAME(a) a ## _C
#include "rgb2rgb_template.c"

#ifdef CAN_COMPILE_X86_ASM

//MMX versions
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#undef HAVE_SSE2
#define ARCH_X86
#define RENAME(a) a ## _MMX
#include "rgb2rgb_template.c"

//MMX2 versions
#undef RENAME
#define HAVE_MMX
#define HAVE_MMX2
#undef HAVE_3DNOW
#undef HAVE_SSE2
#define ARCH_X86
#define RENAME(a) a ## _MMX2
#include "rgb2rgb_template.c"

//3DNOW versions
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#define HAVE_3DNOW
#undef HAVE_SSE2
#define ARCH_X86
#define RENAME(a) a ## _3DNow
#include "rgb2rgb_template.c"

#endif //CAN_COMPILE_X86_ASM

void rgb24to32(const uint8_t *src,uint8_t *dst,unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb24to32_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb24to32_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb24to32_MMX(src, dst, src_size);
	else
#endif
		rgb24to32_C(src, dst, src_size);
}

void rgb15to24(const uint8_t *src,uint8_t *dst,unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb15to24_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb15to24_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb15to24_MMX(src, dst, src_size);
	else
#endif
		rgb15to24_C(src, dst, src_size);
}

void rgb16to24(const uint8_t *src,uint8_t *dst,unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb16to24_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb16to24_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb16to24_MMX(src, dst, src_size);
	else
#endif
		rgb16to24_C(src, dst, src_size);
}

void rgb15to32(const uint8_t *src,uint8_t *dst,unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb15to32_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb15to32_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb15to32_MMX(src, dst, src_size);
	else
#endif
		rgb15to32_C(src, dst, src_size);
}

void rgb16to32(const uint8_t *src,uint8_t *dst,unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb16to32_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb16to32_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb16to32_MMX(src, dst, src_size);
	else
#endif
		rgb16to32_C(src, dst, src_size);
}

void rgb32to24(const uint8_t *src,uint8_t *dst,unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb32to24_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb32to24_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb32to24_MMX(src, dst, src_size);
	else
#endif
		rgb32to24_C(src, dst, src_size);
}

/*
 Original by Strepto/Astral
 ported to gcc & bugfixed : A'rpi
 MMX2, 3DNOW optimization by Nick Kurshev
 32bit c version, and and&add trick by Michael Niedermayer
*/
void rgb15to16(const uint8_t *src,uint8_t *dst,unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb15to16_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb15to16_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb15to16_MMX(src, dst, src_size);
	else
#endif
		rgb15to16_C(src, dst, src_size);
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

void bgr24torgb24(const uint8_t *src, uint8_t *dst, unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		bgr24torgb24_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		bgr24torgb24_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		bgr24torgb24_MMX(src, dst, src_size);
	else
		bgr24torgb24_C(src, dst, src_size);
#else
		bgr24torgb24_C(src, dst, src_size);
#endif
}

void rgb32to16(const uint8_t *src, uint8_t *dst, unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb32to16_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb32to16_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb32to16_MMX(src, dst, src_size);
	else
#endif
		rgb32to16_C(src, dst, src_size);
}

void rgb32to15(const uint8_t *src, uint8_t *dst, unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb32to15_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb32to15_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb32to15_MMX(src, dst, src_size);
	else
#endif
		rgb32to15_C(src, dst, src_size);
}

void rgb24to16(const uint8_t *src, uint8_t *dst, unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb24to16_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb24to16_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb24to16_MMX(src, dst, src_size);
	else
#endif
		rgb24to16_C(src, dst, src_size);
}

void rgb24to15(const uint8_t *src, uint8_t *dst, unsigned src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb24to15_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb24to15_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb24to15_MMX(src, dst, src_size);
	else
#endif
		rgb24to15_C(src, dst, src_size);
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

void rgb32tobgr32(const uint8_t *src, uint8_t *dst, unsigned int src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb32tobgr32_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb32tobgr32_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb32tobgr32_MMX(src, dst, src_size);
	else
#endif
		rgb32tobgr32_C(src, dst, src_size);
}

void rgb24tobgr24(const uint8_t *src, uint8_t *dst, unsigned int src_size)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb24tobgr24_MMX2(src, dst, src_size);
	else if(gCpuCaps.has3DNow)
		rgb24tobgr24_3DNow(src, dst, src_size);
	else if(gCpuCaps.hasMMX)
		rgb24tobgr24_MMX(src, dst, src_size);
	else
#endif
		rgb24tobgr24_C(src, dst, src_size);
}

/**
 *
 * height should be a multiple of 2 and width should be a multiple of 16 (if this is a
 * problem for anyone then tell me, and ill fix it)
 */
void yv12toyuy2(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
	unsigned int width, unsigned int height,
	unsigned int lumStride, unsigned int chromStride, unsigned int dstStride)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		yv12toyuy2_MMX2(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride);
	else if(gCpuCaps.has3DNow)
		yv12toyuy2_3DNow(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride);
	else if(gCpuCaps.hasMMX)
		yv12toyuy2_MMX(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride);
	else
#endif
		yv12toyuy2_C(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride);
}

/**
 *
 * width should be a multiple of 16
 */
void yuv422ptoyuy2(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
	unsigned int width, unsigned int height,
	unsigned int lumStride, unsigned int chromStride, unsigned int dstStride)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		yuv422ptoyuy2_MMX2(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride);
	else if(gCpuCaps.has3DNow)
		yuv422ptoyuy2_3DNow(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride);
	else if(gCpuCaps.hasMMX)
		yuv422ptoyuy2_MMX(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride);
	else
#endif
		yuv422ptoyuy2_C(ysrc, usrc, vsrc, dst, width, height, lumStride, chromStride, dstStride);
}

/**
 *
 * height should be a multiple of 2 and width should be a multiple of 16 (if this is a
 * problem for anyone then tell me, and ill fix it)
 */
void yuy2toyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	unsigned int width, unsigned int height,
	unsigned int lumStride, unsigned int chromStride, unsigned int srcStride)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		yuy2toyv12_MMX2(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
	else if(gCpuCaps.has3DNow)
		yuy2toyv12_3DNow(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
	else if(gCpuCaps.hasMMX)
		yuy2toyv12_MMX(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
	else
#endif
		yuy2toyv12_C(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
}

/**
 *
 * height should be a multiple of 2 and width should be a multiple of 16 (if this is a
 * problem for anyone then tell me, and ill fix it)
 * chrominance data is only taken from every secound line others are ignored FIXME write HQ version
 */
void uyvytoyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	unsigned int width, unsigned int height,
	unsigned int lumStride, unsigned int chromStride, unsigned int srcStride)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		uyvytoyv12_MMX2(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
	else if(gCpuCaps.has3DNow)
		uyvytoyv12_3DNow(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
	else if(gCpuCaps.hasMMX)
		uyvytoyv12_MMX(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
	else
		uyvytoyv12_C(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
#else
		uyvytoyv12_C(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
#endif
}

void yvu9toyv12(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc,
	uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	unsigned int width, unsigned int height,
	unsigned int lumStride, unsigned int chromStride)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		yvu9toyv12_MMX2(ysrc, usrc, vsrc, ydst, udst, vdst, width, height, lumStride, chromStride);
	else if(gCpuCaps.has3DNow)
		yvu9toyv12_3DNow(ysrc, usrc, vsrc, ydst, udst, vdst, width, height, lumStride, chromStride);
	else if(gCpuCaps.hasMMX)
		yvu9toyv12_MMX(ysrc, usrc, vsrc, ydst, udst, vdst, width, height, lumStride, chromStride);
	else
		yvu9toyv12_C(ysrc, usrc, vsrc, ydst, udst, vdst, width, height, lumStride, chromStride);
#else
		yvu9toyv12_C(ysrc, usrc, vsrc, ydst, udst, vdst, width, height, lumStride, chromStride);
#endif
}

/**
 *
 * height should be a multiple of 2 and width should be a multiple of 2 (if this is a
 * problem for anyone then tell me, and ill fix it)
 * chrominance data is only taken from every secound line others are ignored FIXME write HQ version
 */
void rgb24toyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
	unsigned int width, unsigned int height,
	unsigned int lumStride, unsigned int chromStride, unsigned int srcStride)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		rgb24toyv12_MMX2(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
	else if(gCpuCaps.has3DNow)
		rgb24toyv12_3DNow(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
	else if(gCpuCaps.hasMMX)
		rgb24toyv12_MMX(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
	else
#endif
		rgb24toyv12_C(src, ydst, udst, vdst, width,  height, lumStride, chromStride, srcStride);
}

void interleaveBytes(uint8_t *src1, uint8_t *src2, uint8_t *dst,
		     unsigned width, unsigned height, unsigned src1Stride,
		     unsigned src2Stride, unsigned dstStride)
{
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		interleaveBytes_MMX2(src1, src2, dst, width, height, src1Stride, src2Stride, dstStride);
	else if(gCpuCaps.has3DNow)
		interleaveBytes_3DNow(src1, src2, dst, width, height, src1Stride, src2Stride, dstStride);
	else if(gCpuCaps.hasMMX)
		interleaveBytes_MMX(src1, src2, dst, width, height, src1Stride, src2Stride, dstStride);
	else
#endif
		interleaveBytes_C(src1, src2, dst, width, height, src1Stride, src2Stride, dstStride);
}
