
// Software scaling and colorspace conversion routines for MPlayer

// Orginal C implementation by A'rpi/ESP-team <arpi@thot.banki.hu>
// current version mostly by Michael Niedermayer (michaelni@gmx.at)
// the parts written by michael are under GNU GPL

#include <inttypes.h>
#include <string.h>
#include "../config.h"
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
/*
NOTES

known BUGS with known cause (no bugreports please!, but patches are welcome :) )
horizontal MMX2 scaler reads 1-7 samples too much (might cause a sig11)

Supported output formats BGR15 BGR16 BGR24 BGR32
BGR15 & BGR16 MMX verions support dithering
Special versions: fast Y 1:1 scaling (no interpolation in y direction)

TODO
more intelligent missalignment avoidance for the horizontal scaler
bicubic scaler
dither in C
change the distance of the u & v buffer
how to differenciate between x86 an C at runtime ?! (using C for now)
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

// temporary storage for 4 yuv lines:
// 16bit for now (mmx likes it more compact)
static uint16_t __attribute__((aligned(8))) pix_buf_y[4][2048];
static uint16_t __attribute__((aligned(8))) pix_buf_uv[2][2048*2];
#else
static uint16_t pix_buf_y[4][2048];
static uint16_t pix_buf_uv[2][2048*2];
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

#ifdef CAN_COMPILE_X86_ASM
static uint8_t funnyYCode[10000];
static uint8_t funnyUVCode[10000];
#endif

static int canMMX2BeUsed=0;

#ifdef CAN_COMPILE_X86_ASM
void in_asm_used_var_warning_killer()
{
 int i= yCoeff+vrCoeff+ubCoeff+vgCoeff+ugCoeff+bF8+bFC+w400+w80+w10+
 bm00001111+bm00000111+bm11111000+b16Mask+g16Mask+r16Mask+b15Mask+g15Mask+r15Mask+temp0+asm_yalpha1+ asm_uvalpha1+
 M24A+M24B+M24C;
 if(i) i=0;
}
#endif

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
// *** Designed to upscale, but may work for downscale too.
// s_xinc = (src_width << 16) / dst_width
// s_yinc = (src_height << 16) / dst_height
// switching the cpu type during a sliced drawing can have bad effects, like sig11
void SwScale_YV12slice(unsigned char* srcptr[],int stride[], int y, int h,
			     uint8_t* dstptr[], int dststride, int dstw, int dstbpp,
			     unsigned int s_xinc,unsigned int s_yinc){

// scaling factors:
//static int s_yinc=(vo_dga_src_height<<16)/vo_dga_vp_height;
//static int s_xinc=(vo_dga_src_width<<8)/vo_dga_vp_width;
#ifdef RUNTIME_CPUDETECT
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		SwScale_YV12slice_MMX2(srcptr, stride, y, h, dstptr, dststride, dstw, dstbpp, s_xinc, s_yinc);
	else if(gCpuCaps.has3DNow)
		SwScale_YV12slice_3DNow(srcptr, stride, y, h, dstptr, dststride, dstw, dstbpp, s_xinc, s_yinc);
	else if(gCpuCaps.hasMMX)
		SwScale_YV12slice_MMX(srcptr, stride, y, h, dstptr, dststride, dstw, dstbpp, s_xinc, s_yinc);
	else
		SwScale_YV12slice_C(srcptr, stride, y, h, dstptr, dststride, dstw, dstbpp, s_xinc, s_yinc);
#else
		SwScale_YV12slice_C(srcptr, stride, y, h, dstptr, dststride, dstw, dstbpp, s_xinc, s_yinc);
#endif
#else //RUNTIME_CPUDETECT
#ifdef HAVE_MMX2
		SwScale_YV12slice_MMX2(srcptr, stride, y, h, dstptr, dststride, dstw, dstbpp, s_xinc, s_yinc);
#elif defined (HAVE_3DNOW)
		SwScale_YV12slice_3DNow(srcptr, stride, y, h, dstptr, dststride, dstw, dstbpp, s_xinc, s_yinc);
#elif defined (HAVE_MMX)
		SwScale_YV12slice_MMX(srcptr, stride, y, h, dstptr, dststride, dstw, dstbpp, s_xinc, s_yinc);
#else
		SwScale_YV12slice_C(srcptr, stride, y, h, dstptr, dststride, dstw, dstbpp, s_xinc, s_yinc);
#endif
#endif //!RUNTIME_CPUDETECT

}

void SwScale_Init(){
    // generating tables:
    int i;
    for(i=0;i<256;i++){
        clip_table[i]=0;
        clip_table[i+256]=i;
        clip_table[i+512]=255;
	yuvtab_2568[i]=(0x2568*(i-16))+(256<<13);
	yuvtab_3343[i]=0x3343*(i-128);
	yuvtab_0c92[i]=-0x0c92*(i-128);
	yuvtab_1a1e[i]=-0x1a1e*(i-128);
	yuvtab_40cf[i]=0x40cf*(i-128);
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

