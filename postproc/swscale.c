
// Software scaling and colorspace conversion routines for MPlayer

// Orginal C implementation by A'rpi/ESP-team <arpi@thot.banki.hu>
// current version mostly by Michael Niedermayer (michaelni@gmx.at)
// the parts written by michael are under GNU GPL

#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "../config.h"
#include "../mangle.h"
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include "swscale.h"
#include "../cpudetect.h"
#undef MOVNTQ
#undef PAVGB

//#undef HAVE_MMX2
//#undef HAVE_MMX
//#undef ARCH_X86
#define DITHER1XBPP
int fullUVIpol=0;
//disables the unscaled height version
int allwaysIpol=0;

#define RET 0xC3 //near return opcode

//#define ASSERT(x) if(!(x)) { printf("ASSERT " #x " failed\n"); *((int*)0)=0; }
#define ASSERT(x) ;

extern int verbose; // defined in mplayer.c
/*
NOTES

known BUGS with known cause (no bugreports please!, but patches are welcome :) )
horizontal fast_bilinear MMX2 scaler reads 1-7 samples too much (might cause a sig11)

Supported output formats BGR15 BGR16 BGR24 BGR32 YV12
BGR15 & BGR16 MMX verions support dithering
Special versions: fast Y 1:1 scaling (no interpolation in y direction)

TODO
more intelligent missalignment avoidance for the horizontal scaler
dither in C
change the distance of the u & v buffer
Move static / global vars into a struct so multiple scalers can be used
write special vertical cubic upscale version
Optimize C code (yv12 / minmax)
dstStride[3]
*/

#define ABS(a) ((a) > 0 ? (a) : (-(a)))
#define MIN(a,b) ((a) > (b) ? (b) : (a))
#define MAX(a,b) ((a) < (b) ? (b) : (a))

#ifdef ARCH_X86
#define CAN_COMPILE_X86_ASM
#endif

#ifdef CAN_COMPILE_X86_ASM
static uint64_t __attribute__((aligned(8))) yCoeff=    0x2568256825682568LL;
static uint64_t __attribute__((aligned(8))) vrCoeff=   0x3343334333433343LL;
static uint64_t __attribute__((aligned(8))) ubCoeff=   0x40cf40cf40cf40cfLL;
static uint64_t __attribute__((aligned(8))) vgCoeff=   0xE5E2E5E2E5E2E5E2LL;
static uint64_t __attribute__((aligned(8))) ugCoeff=   0xF36EF36EF36EF36ELL;
static uint64_t __attribute__((aligned(8))) bF8=       0xF8F8F8F8F8F8F8F8LL;
static uint64_t __attribute__((aligned(8))) bFC=       0xFCFCFCFCFCFCFCFCLL;
static uint64_t __attribute__((aligned(8))) w400=      0x0400040004000400LL;
static uint64_t __attribute__((aligned(8))) w80=       0x0080008000800080LL;
static uint64_t __attribute__((aligned(8))) w10=       0x0010001000100010LL;
static uint64_t __attribute__((aligned(8))) w02=       0x0002000200020002LL;
static uint64_t __attribute__((aligned(8))) bm00001111=0x00000000FFFFFFFFLL;
static uint64_t __attribute__((aligned(8))) bm00000111=0x0000000000FFFFFFLL;
static uint64_t __attribute__((aligned(8))) bm11111000=0xFFFFFFFFFF000000LL;

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

static uint64_t __attribute__((aligned(8))) b16Mask=   0x001F001F001F001FLL;
static uint64_t __attribute__((aligned(8))) g16Mask=   0x07E007E007E007E0LL;
static uint64_t __attribute__((aligned(8))) r16Mask=   0xF800F800F800F800LL;
static uint64_t __attribute__((aligned(8))) b15Mask=   0x001F001F001F001FLL;
static uint64_t __attribute__((aligned(8))) g15Mask=   0x03E003E003E003E0LL;
static uint64_t __attribute__((aligned(8))) r15Mask=   0x7C007C007C007C00LL;

static uint64_t __attribute__((aligned(8))) M24A=   0x00FF0000FF0000FFLL;
static uint64_t __attribute__((aligned(8))) M24B=   0xFF0000FF0000FF00LL;
static uint64_t __attribute__((aligned(8))) M24C=   0x0000FF0000FF0000LL;

static uint64_t __attribute__((aligned(8))) temp0;
static uint64_t __attribute__((aligned(8))) asm_yalpha1;
static uint64_t __attribute__((aligned(8))) asm_uvalpha1;

static int16_t __attribute__((aligned(8))) *lumPixBuf[2000];
static int16_t __attribute__((aligned(8))) *chrPixBuf[2000];
static int16_t __attribute__((aligned(8))) hLumFilter[8000];
static int16_t __attribute__((aligned(8))) hLumFilterPos[2000];
static int16_t __attribute__((aligned(8))) hChrFilter[8000];
static int16_t __attribute__((aligned(8))) hChrFilterPos[2000];
static int16_t __attribute__((aligned(8))) vLumFilter[8000];
static int16_t __attribute__((aligned(8))) vLumFilterPos[2000];
static int16_t __attribute__((aligned(8))) vChrFilter[8000];
static int16_t __attribute__((aligned(8))) vChrFilterPos[2000];

// Contain simply the values from v(Lum|Chr)Filter just nicely packed for mmx
//FIXME these are very likely too small / 8000 caused problems with 480x480
static int16_t __attribute__((aligned(8))) lumMmxFilter[16000];
static int16_t __attribute__((aligned(8))) chrMmxFilter[16000];
#else
static int16_t *lumPixBuf[2000];
static int16_t *chrPixBuf[2000];
static int16_t hLumFilter[8000];
static int16_t hLumFilterPos[2000];
static int16_t hChrFilter[8000];
static int16_t hChrFilterPos[2000];
static int16_t vLumFilter[8000];
static int16_t vLumFilterPos[2000];
static int16_t vChrFilter[8000];
static int16_t vChrFilterPos[2000];
//FIXME just dummy vars
static int16_t lumMmxFilter[1];
static int16_t chrMmxFilter[1];
#endif

// clipping helper table for C implementations:
static unsigned char clip_table[768];

static unsigned short clip_table16b[768];
static unsigned short clip_table16g[768];
static unsigned short clip_table16r[768];
static unsigned short clip_table15b[768];
static unsigned short clip_table15g[768];
static unsigned short clip_table15r[768];

// yuv->rgb conversion tables:
static    int yuvtab_2568[256];
static    int yuvtab_3343[256];
static    int yuvtab_0c92[256];
static    int yuvtab_1a1e[256];
static    int yuvtab_40cf[256];
// Needed for cubic scaler to catch overflows
static    int clip_yuvtab_2568[768];
static    int clip_yuvtab_3343[768];
static    int clip_yuvtab_0c92[768];
static    int clip_yuvtab_1a1e[768];
static    int clip_yuvtab_40cf[768];

static int hLumFilterSize=0;
static int hChrFilterSize=0;
static int vLumFilterSize=0;
static int vChrFilterSize=0;
static int vLumBufSize=0;
static int vChrBufSize=0;

int sws_flags=0;

#ifdef CAN_COMPILE_X86_ASM
static uint8_t funnyYCode[10000];
static uint8_t funnyUVCode[10000];
#endif

static int canMMX2BeUsed=0;

#ifdef CAN_COMPILE_X86_ASM
void in_asm_used_var_warning_killer()
{
 volatile int i= yCoeff+vrCoeff+ubCoeff+vgCoeff+ugCoeff+bF8+bFC+w400+w80+w10+
 bm00001111+bm00000111+bm11111000+b16Mask+g16Mask+r16Mask+b15Mask+g15Mask+r15Mask+temp0+asm_yalpha1+ asm_uvalpha1+
 M24A+M24B+M24C+w02 + funnyYCode[0]+ funnyUVCode[0]+b5Dither+g5Dither+r5Dither+g6Dither+dither4[0]+dither8[0];
 if(i) i=0;
}
#endif

static inline void yuv2yuvXinC(int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
				    int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
				    uint8_t *dest, uint8_t *uDest, uint8_t *vDest, int dstW)
{
	//FIXME Optimize (just quickly writen not opti..)
	int i;
	for(i=0; i<dstW; i++)
	{
		int val=0;
		int j;
		for(j=0; j<lumFilterSize; j++)
			val += lumSrc[j][i] * lumFilter[j];

		dest[i]= MIN(MAX(val>>19, 0), 255);
	}

	if(uDest != NULL)
		for(i=0; i<(dstW>>1); i++)
		{
			int u=0;
			int v=0;
			int j;
			for(j=0; j<chrFilterSize; j++)
			{
				u += chrSrc[j][i] * chrFilter[j];
				v += chrSrc[j][i + 2048] * chrFilter[j];
			}

			uDest[i]= MIN(MAX(u>>19, 0), 255);
			vDest[i]= MIN(MAX(v>>19, 0), 255);
		}
}

static inline void yuv2rgbXinC(int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
				    int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
				    uint8_t *dest, int dstW, int dstbpp)
{
	if(dstbpp==32)
	{
		int i;
		for(i=0; i<(dstW>>1); i++){
			int j;
			int Y1=0;
			int Y2=0;
			int U=0;
			int V=0;
			int Cb, Cr, Cg;
			for(j=0; j<lumFilterSize; j++)
			{
				Y1 += lumSrc[j][2*i] * lumFilter[j];
				Y2 += lumSrc[j][2*i+1] * lumFilter[j];
			}
			for(j=0; j<chrFilterSize; j++)
			{
				U += chrSrc[j][i] * chrFilter[j];
				V += chrSrc[j][i+2048] * chrFilter[j];
			}
			Y1= clip_yuvtab_2568[ (Y1>>19) + 256 ];
			Y2= clip_yuvtab_2568[ (Y2>>19) + 256 ];
			U >>= 19;
			V >>= 19;

			Cb= clip_yuvtab_40cf[U+ 256];
			Cg= clip_yuvtab_1a1e[V+ 256] + yuvtab_0c92[U+ 256];
			Cr= clip_yuvtab_3343[V+ 256];

			dest[8*i+0]=clip_table[((Y1 + Cb) >>13)];
			dest[8*i+1]=clip_table[((Y1 + Cg) >>13)];
			dest[8*i+2]=clip_table[((Y1 + Cr) >>13)];

			dest[8*i+4]=clip_table[((Y2 + Cb) >>13)];
			dest[8*i+5]=clip_table[((Y2 + Cg) >>13)];
			dest[8*i+6]=clip_table[((Y2 + Cr) >>13)];
		}
	}
	else if(dstbpp==24)
	{
		int i;
		for(i=0; i<(dstW>>1); i++){
			int j;
			int Y1=0;
			int Y2=0;
			int U=0;
			int V=0;
			int Cb, Cr, Cg;
			for(j=0; j<lumFilterSize; j++)
			{
				Y1 += lumSrc[j][2*i] * lumFilter[j];
				Y2 += lumSrc[j][2*i+1] * lumFilter[j];
			}
			for(j=0; j<chrFilterSize; j++)
			{
				U += chrSrc[j][i] * chrFilter[j];
				V += chrSrc[j][i+2048] * chrFilter[j];
			}
			Y1= clip_yuvtab_2568[ (Y1>>19) + 256 ];
			Y2= clip_yuvtab_2568[ (Y2>>19) + 256 ];
			U >>= 19;
			V >>= 19;

			Cb= clip_yuvtab_40cf[U+ 256];
			Cg= clip_yuvtab_1a1e[V+ 256] + yuvtab_0c92[U+ 256];
			Cr= clip_yuvtab_3343[V+ 256];

			dest[0]=clip_table[((Y1 + Cb) >>13)];
			dest[1]=clip_table[((Y1 + Cg) >>13)];
			dest[2]=clip_table[((Y1 + Cr) >>13)];

			dest[3]=clip_table[((Y2 + Cb) >>13)];
			dest[4]=clip_table[((Y2 + Cg) >>13)];
			dest[5]=clip_table[((Y2 + Cr) >>13)];
			dest+=6;
		}
	}
	else if(dstbpp==16)
	{
		int i;
		for(i=0; i<(dstW>>1); i++){
			int j;
			int Y1=0;
			int Y2=0;
			int U=0;
			int V=0;
			int Cb, Cr, Cg;
			for(j=0; j<lumFilterSize; j++)
			{
				Y1 += lumSrc[j][2*i] * lumFilter[j];
				Y2 += lumSrc[j][2*i+1] * lumFilter[j];
			}
			for(j=0; j<chrFilterSize; j++)
			{
				U += chrSrc[j][i] * chrFilter[j];
				V += chrSrc[j][i+2048] * chrFilter[j];
			}
			Y1= clip_yuvtab_2568[ (Y1>>19) + 256 ];
			Y2= clip_yuvtab_2568[ (Y2>>19) + 256 ];
			U >>= 19;
			V >>= 19;

			Cb= clip_yuvtab_40cf[U+ 256];
			Cg= clip_yuvtab_1a1e[V+ 256] + yuvtab_0c92[U+ 256];
			Cr= clip_yuvtab_3343[V+ 256];

			((uint16_t*)dest)[2*i] =
				clip_table16b[(Y1 + Cb) >>13] |
				clip_table16g[(Y1 + Cg) >>13] |
				clip_table16r[(Y1 + Cr) >>13];

			((uint16_t*)dest)[2*i+1] =
				clip_table16b[(Y2 + Cb) >>13] |
				clip_table16g[(Y2 + Cg) >>13] |
				clip_table16r[(Y2 + Cr) >>13];
		}
	}
	else if(dstbpp==15)
	{
		int i;
		for(i=0; i<(dstW>>1); i++){
			int j;
			int Y1=0;
			int Y2=0;
			int U=0;
			int V=0;
			int Cb, Cr, Cg;
			for(j=0; j<lumFilterSize; j++)
			{
				Y1 += lumSrc[j][2*i] * lumFilter[j];
				Y2 += lumSrc[j][2*i+1] * lumFilter[j];
			}
			for(j=0; j<chrFilterSize; j++)
			{
				U += chrSrc[j][i] * chrFilter[j];
				V += chrSrc[j][i+2048] * chrFilter[j];
			}
			Y1= clip_yuvtab_2568[ (Y1>>19) + 256 ];
			Y2= clip_yuvtab_2568[ (Y2>>19) + 256 ];
			U >>= 19;
			V >>= 19;

			Cb= clip_yuvtab_40cf[U+ 256];
			Cg= clip_yuvtab_1a1e[V+ 256] + yuvtab_0c92[U+ 256];
			Cr= clip_yuvtab_3343[V+ 256];

			((uint16_t*)dest)[2*i] =
				clip_table15b[(Y1 + Cb) >>13] |
				clip_table15g[(Y1 + Cg) >>13] |
				clip_table15r[(Y1 + Cr) >>13];

			((uint16_t*)dest)[2*i+1] =
				clip_table15b[(Y2 + Cb) >>13] |
				clip_table15g[(Y2 + Cg) >>13] |
				clip_table15r[(Y2 + Cr) >>13];
		}
	}
}


//Note: we have C, X86, MMX, MMX2, 3DNOW version therse no 3DNOW+MMX2 one
//Plain C versions
#if !defined (HAVE_MMX) || defined (RUNTIME_CPUDETECT)
#define COMPILE_C
#endif

#ifdef CAN_COMPILE_X86_ASM

#if (defined (HAVE_MMX) && !defined (HAVE_3DNOW) && !defined (HAVE_MMX2)) || defined (RUNTIME_CPUDETECT)
#define COMPILE_MMX
#endif

#if defined (HAVE_MMX2) || defined (RUNTIME_CPUDETECT)
#define COMPILE_MMX2
#endif

#if (defined (HAVE_3DNOW) && !defined (HAVE_MMX2)) || defined (RUNTIME_CPUDETECT)
#define COMPILE_3DNOW
#endif
#endif //CAN_COMPILE_X86_ASM

#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#undef ARCH_X86

#ifdef COMPILE_C
#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#undef ARCH_X86
#define RENAME(a) a ## _C
#include "swscale_template.c"
#endif

#ifdef CAN_COMPILE_X86_ASM

//X86 versions
/*
#undef RENAME
#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#define ARCH_X86
#define RENAME(a) a ## _X86
#include "swscale_template.c"
*/
//MMX versions
#ifdef COMPILE_MMX
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#define ARCH_X86
#define RENAME(a) a ## _MMX
#include "swscale_template.c"
#endif

//MMX2 versions
#ifdef COMPILE_MMX2
#undef RENAME
#define HAVE_MMX
#define HAVE_MMX2
#undef HAVE_3DNOW
#define ARCH_X86
#define RENAME(a) a ## _MMX2
#include "swscale_template.c"
#endif

//3DNOW versions
#ifdef COMPILE_3DNOW
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#define HAVE_3DNOW
#define ARCH_X86
#define RENAME(a) a ## _3DNow
#include "swscale_template.c"
#endif

#endif //CAN_COMPILE_X86_ASM

// minor note: the HAVE_xyz is messed up after that line so dont use it


// *** bilinear scaling and yuv->rgb or yuv->yuv conversion of yv12 slices:
// *** Note: it's called multiple times while decoding a frame, first time y==0
// switching the cpu type during a sliced drawing can have bad effects, like sig11
void SwScale_YV12slice(unsigned char* srcptr[],int stride[], int srcSliceY ,
			     int srcSliceH, uint8_t* dstptr[], int dststride, int dstbpp,
			     int srcW, int srcH, int dstW, int dstH){

#ifdef RUNTIME_CPUDETECT
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		SwScale_YV12slice_MMX2(srcptr, stride, srcSliceY, srcSliceH, dstptr, dststride, dstbpp, srcW, srcH, dstW, dstH);
	else if(gCpuCaps.has3DNow)
		SwScale_YV12slice_3DNow(srcptr, stride, srcSliceY, srcSliceH, dstptr, dststride, dstbpp, srcW, srcH, dstW, dstH);
	else if(gCpuCaps.hasMMX)
		SwScale_YV12slice_MMX(srcptr, stride, srcSliceY, srcSliceH, dstptr, dststride, dstbpp, srcW, srcH, dstW, dstH);
	else
		SwScale_YV12slice_C(srcptr, stride, srcSliceY, srcSliceH, dstptr, dststride, dstbpp, srcW, srcH, dstW, dstH);
#else
		SwScale_YV12slice_C(srcptr, stride, srcSliceY, srcSliceH, dstptr, dststride, dstbpp, srcW, srcH, dstW, dstH);
#endif
#else //RUNTIME_CPUDETECT
#ifdef HAVE_MMX2
		SwScale_YV12slice_MMX2(srcptr, stride, srcSliceY, srcSliceH, dstptr, dststride, dstbpp, srcW, srcH, dstW, dstH);
#elif defined (HAVE_3DNOW)
		SwScale_YV12slice_3DNow(srcptr, stride, srcSliceY, srcSliceH, dstptr, dststride, dstbpp, srcW, srcH, dstW, dstH);
#elif defined (HAVE_MMX)
		SwScale_YV12slice_MMX(srcptr, stride, srcSliceY, srcSliceH, dstptr, dststride, dstbpp, srcW, srcH, dstW, dstH);
#else
		SwScale_YV12slice_C(srcptr, stride, srcSliceY, srcSliceH, dstptr, dststride, dstbpp, srcW, srcH, dstW, dstH);
#endif
#endif //!RUNTIME_CPUDETECT

}

void SwScale_Init(){
    // generating tables:
    int i;
    for(i=0; i<768; i++){
	int c= MIN(MAX(i-256, 0), 255);
	clip_table[i]=c;
	yuvtab_2568[c]= clip_yuvtab_2568[i]=(0x2568*(c-16))+(256<<13);
	yuvtab_3343[c]= clip_yuvtab_3343[i]=0x3343*(c-128);
	yuvtab_0c92[c]= clip_yuvtab_0c92[i]=-0x0c92*(c-128);
	yuvtab_1a1e[c]= clip_yuvtab_1a1e[i]=-0x1a1e*(c-128);
	yuvtab_40cf[c]= clip_yuvtab_40cf[i]=0x40cf*(c-128);
    }

    for(i=0; i<768; i++)
    {
    	int v= clip_table[i];
	clip_table16b[i]= v>>3;
	clip_table16g[i]= (v<<3)&0x07E0;
	clip_table16r[i]= (v<<8)&0xF800;
	clip_table15b[i]= v>>3;
	clip_table15g[i]= (v<<2)&0x03E0;
	clip_table15r[i]= (v<<7)&0x7C00;
    }

}

