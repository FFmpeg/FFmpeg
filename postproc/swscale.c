
// Software scaling and colorspace conversion routines for MPlayer

// Orginal C implementation by A'rpi/ESP-team <arpi@thot.banki.hu>
// current version mostly by Michael Niedermayer (michaelni@gmx.at)

#include <inttypes.h>
#include "../config.h"

//#undef HAVE_MMX2
//#undef HAVE_MMX
//#undef ARCH_X86
#define DITHER16BPP
#define ALT_ERROR

#define RET 0xC3 //near return opcode
/*
NOTES

known BUGS with known cause (no bugreports please!)
line at the right (c,asm and mmx2)
code reads 1 sample too much (might cause a sig11)

TODO
check alignment off everything
*/

static uint64_t yCoeff=    0x2568256825682568LL;
static uint64_t ubCoeff=   0x3343334333433343LL;
static uint64_t vrCoeff=   0x40cf40cf40cf40cfLL;
static uint64_t ugCoeff=   0xE5E2E5E2E5E2E5E2LL;
static uint64_t vgCoeff=   0xF36EF36EF36EF36ELL;
static uint64_t w80=       0x0080008000800080LL;
static uint64_t w10=       0x0010001000100010LL;
static uint64_t bm00000111=0x0000000000FFFFFFLL;
static uint64_t bm11111000=0xFFFFFFFFFF000000LL;

static uint64_t b16Dither= 0x0004000400040004LL;
static uint64_t b16Dither1=0x0004000400040004LL;
static uint64_t b16Dither2=0x0602060206020602LL;
static uint64_t g16Dither= 0x0002000200020002LL;
static uint64_t g16Dither1=0x0002000200020002LL;
static uint64_t g16Dither2=0x0301030103010301LL;

static uint64_t b16Mask=   0x001F001F001F001FLL;
static uint64_t g16Mask=   0x07E007E007E007E0LL;
static uint64_t r16Mask=   0xF800F800F800F800LL;
static uint64_t temp0;


// temporary storage for 4 yuv lines:
// 16bit for now (mmx likes it more compact)
static uint16_t pix_buf_y[4][2048];
static uint16_t pix_buf_uv[2][2048*2];

// clipping helper table for C implementations:
static unsigned char clip_table[768];

// yuv->rgb conversion tables:
static    int yuvtab_2568[256];
static    int yuvtab_3343[256];
static    int yuvtab_0c92[256];
static    int yuvtab_1a1e[256];
static    int yuvtab_40cf[256];


static uint8_t funnyYCode[10000];
static uint8_t funnyUVCode[10000];


// *** bilinear scaling and yuv->rgb conversion of yv12 slices:
// *** Note: it's called multiple times while decoding a frame, first time y==0
// *** Designed to upscale, but may work for downscale too.
// s_xinc = (src_width << 16) / dst_width
// s_yinc = (src_height << 16) / dst_height
void SwScale_YV12slice_brg24(unsigned char* srcptr[],int stride[], int y, int h,
			     unsigned char* dstptr, int dststride, int dstw, int dstbpp,
			     unsigned int s_xinc,unsigned int s_yinc){

// scaling factors:
//static int s_yinc=(vo_dga_src_height<<16)/vo_dga_vp_height;
//static int s_xinc=(vo_dga_src_width<<8)/vo_dga_vp_width;

unsigned int s_xinc2;

static int s_srcypos; // points to the dst Pixels center in the source (0 is the center of pixel 0,0 in src)
static int s_ypos;

// last horzontally interpolated lines, used to avoid unnecessary calculations
static int s_last_ypos;
static int s_last_y1pos;

static int static_dstw;

#ifdef HAVE_MMX2
// used to detect a horizontal size change
static int old_dstw= -1;
static int old_s_xinc= -1;

// difference between the requested xinc and the required one for the mmx2 routine
static int s_xinc_diff=0;
static int s_xinc2_diff=0;
#endif
int canMMX2BeUsed;

// we need that precission at least for the mmx2 code
//s_xinc*= 256;
s_xinc2=s_xinc>>1;
canMMX2BeUsed= (s_xinc <= 0x10000 && (dstw&31)==0) ? 1 : 0;

#ifdef HAVE_MMX2
	if(canMMX2BeUsed)
	{
		s_xinc+= s_xinc_diff;
		s_xinc2+= s_xinc2_diff;
	}
#endif

  // force calculation of the horizontal interpolation of the first line
  s_last_ypos=-99;
  s_last_y1pos=-99;

  if(y==0){
      s_srcypos= s_yinc/2 - 0x8000;
      s_ypos=0;
#ifdef HAVE_MMX2
// cant downscale !!!
	if((old_s_xinc != s_xinc || old_dstw!=dstw) && canMMX2BeUsed)
	{
		uint8_t *fragment;
		int imm8OfPShufW1;
		int imm8OfPShufW2;
		int fragmentLength;

		int xpos, xx, xalpha, i;

		old_s_xinc= s_xinc;
		old_dstw= dstw;

		static_dstw= dstw;

		// create an optimized horizontal scaling routine

		//code fragment

		asm volatile(
			"jmp 9f				\n\t"
		// Begin
			"0:				\n\t"
			"movq (%%esi), %%mm0		\n\t" //FIXME Alignment
			"movq %%mm0, %%mm1		\n\t"
			"psrlq $8, %%mm0		\n\t"
			"punpcklbw %%mm7, %%mm1	\n\t"
			"movq %%mm2, %%mm3		\n\t"
			"punpcklbw %%mm7, %%mm0	\n\t"
			"addw %%bx, %%cx		\n\t" //2*xalpha += (4*s_xinc)&0xFFFF
			"pshufw $0xFF, %%mm1, %%mm1	\n\t"
			"1:				\n\t"
			"adcl %%edx, %%esi		\n\t" //xx+= (4*s_xinc)>>16 + carry
			"pshufw $0xFF, %%mm0, %%mm0	\n\t"
			"2:				\n\t"
			"psrlw $9, %%mm3		\n\t"
			"psubw %%mm1, %%mm0		\n\t"
			"pmullw %%mm3, %%mm0		\n\t"
			"paddw %%mm6, %%mm2		\n\t" // 2*alpha += xpos&0xFFFF
			"psllw $7, %%mm1		\n\t"
			"paddw %%mm1, %%mm0		\n\t"

			"movq %%mm0, (%%edi, %%eax)	\n\t"

			"addl $8, %%eax			\n\t"
		// End
			"9:				\n\t"
//		"int $3\n\t"
			"leal 0b, %0			\n\t"
			"leal 1b, %1			\n\t"
			"leal 2b, %2			\n\t"
			"decl %1			\n\t"
			"decl %2			\n\t"
			"subl %0, %1			\n\t"
			"subl %0, %2			\n\t"
			"leal 9b, %3			\n\t"
			"subl %0, %3			\n\t"
			:"=r" (fragment), "=r" (imm8OfPShufW1), "=r" (imm8OfPShufW2),
			 "=r" (fragmentLength)
		);

		xpos= xx=xalpha= 0;

		/* choose xinc so that all 8 parts fit exactly
		   Note: we cannot use just 1 part because it would not fit in the code cache */
		s_xinc2_diff= -((((s_xinc2*(dstw/8))&0xFFFF))/(dstw/8))+10;
//		s_xinc_diff= -((((s_xinc*(dstw/8))&0xFFFF))/(dstw/8));
#ifdef ALT_ERROR
		s_xinc2_diff+= ((0x10000/(dstw/8)));
#endif
		s_xinc_diff= s_xinc2_diff*2;

		s_xinc2+= s_xinc2_diff;
		s_xinc+= s_xinc_diff;

		old_s_xinc= s_xinc;

		for(i=0; i<dstw/8; i++)
		{
			int xx=xpos>>16;

			if((i&3) == 0)
			{
				int a=0;
				int b=((xpos+s_xinc)>>16) - xx;
				int c=((xpos+s_xinc*2)>>16) - xx;
				int d=((xpos+s_xinc*3)>>16) - xx;

				memcpy(funnyYCode + fragmentLength*i/4, fragment, fragmentLength);

				funnyYCode[fragmentLength*i/4 + imm8OfPShufW1]=
				funnyYCode[fragmentLength*i/4 + imm8OfPShufW2]=
					a | (b<<2) | (c<<4) | (d<<6);

				funnyYCode[fragmentLength*(i+4)/4]= RET;
			}
			xpos+=s_xinc;
		}

		xpos= xx=xalpha= 0;
		//FIXME choose size and or xinc so that they fit exactly
		for(i=0; i<dstw/8; i++)
		{
			int xx=xpos>>16;

			if((i&3) == 0)
			{
				int a=0;
				int b=((xpos+s_xinc2)>>16) - xx;
				int c=((xpos+s_xinc2*2)>>16) - xx;
				int d=((xpos+s_xinc2*3)>>16) - xx;

				memcpy(funnyUVCode + fragmentLength*i/4, fragment, fragmentLength);

				funnyUVCode[fragmentLength*i/4 + imm8OfPShufW1]=
				funnyUVCode[fragmentLength*i/4 + imm8OfPShufW2]=
					a | (b<<2) | (c<<4) | (d<<6);

				funnyUVCode[fragmentLength*(i+4)/4]= RET;
			}
			xpos+=s_xinc2;
		}
//		funnyCode[0]= RET;
	}

#endif // HAVE_MMX2
  } // reset counters


  while(1){
    unsigned char *dest=dstptr+dststride*s_ypos;
    int y0=(s_srcypos + 0xFFFF)>>16;  // first luminance source line number below the dst line
	// points to the dst Pixels center in the source (0 is the center of pixel 0,0 in src)
    int srcuvpos= s_srcypos + s_yinc/2 - 0x8000;
    int y1=(srcuvpos + 0x1FFFF)>>17; // first chrominance source line number below the dst line
    int yalpha=((s_srcypos-1)&0xFFFF)>>7;
    int yalpha1=yalpha^511;
    int uvalpha=((srcuvpos-1)&0x1FFFF)>>8;
    int uvalpha1=uvalpha^511;
    uint16_t *buf0=pix_buf_y[y0&1];		// top line of the interpolated slice
    uint16_t *buf1=pix_buf_y[((y0+1)&1)];	// bottom line of the interpolated slice
    uint16_t *uvbuf0=pix_buf_uv[y1&1];		// top line of the interpolated slice
    uint16_t *uvbuf1=pix_buf_uv[(y1+1)&1];	// bottom line of the interpolated slice
    int i;

    // if this is before the first line than use only the first src line
    if(y0==0) buf0= buf1;
    if(y1==0) uvbuf0= uvbuf1; // yes we do have to check this, its not the same as y0==0

    if(y0>=y+h) break; // FIXME wrong, skips last lines, but they are dupliactes anyway

    // if this is after the last line than use only the last src line
    if(y0>=y+h)
    {
	buf1= buf0;
	s_last_ypos=y0;
    }
    if(y1>=(y+h)/2)
    {
	uvbuf1= uvbuf0;
	s_last_y1pos=y1;
    }


    s_ypos++; s_srcypos+=s_yinc;

    //only interpolate the src line horizontally if we didnt do it allready
    if(s_last_ypos!=y0){
      unsigned char *src=srcptr[0]+(y0-y)*stride[0];
      unsigned int xpos=0;
      s_last_ypos=y0;
      // *** horizontal scale Y line to temp buffer
#ifdef ARCH_X86

#ifdef HAVE_MMX2
	if(canMMX2BeUsed)
	{
		asm volatile(
			"pxor %%mm7, %%mm7		\n\t"
			"pxor %%mm2, %%mm2		\n\t" // 2*xalpha
			"movd %5, %%mm6			\n\t" // s_xinc&0xFFFF
			"punpcklwd %%mm6, %%mm6		\n\t"
			"punpcklwd %%mm6, %%mm6		\n\t"
			"movq %%mm6, %%mm2		\n\t"
			"psllq $16, %%mm2		\n\t"
			"paddw %%mm6, %%mm2		\n\t"
			"psllq $16, %%mm2		\n\t"
			"paddw %%mm6, %%mm2		\n\t"
			"psllq $16, %%mm2		\n\t" //0,t,2t,3t		t=s_xinc&0xFF
			"movq %%mm2, temp0		\n\t"
			"movd %4, %%mm6			\n\t" //(s_xinc*4)&0xFFFF
			"punpcklwd %%mm6, %%mm6		\n\t"
			"punpcklwd %%mm6, %%mm6		\n\t"
			"xorl %%eax, %%eax		\n\t" // i
			"movl %0, %%esi			\n\t" // src
			"movl %1, %%edi			\n\t" // buf1
			"movl %3, %%edx			\n\t" // (s_xinc*4)>>16
			"xorl %%ecx, %%ecx		\n\t"
			"xorl %%ebx, %%ebx		\n\t"
			"movw %4, %%bx			\n\t" // (s_xinc*4)&0xFFFF
	//	"int $3\n\t"
			"call funnyYCode			\n\t"
			"movq temp0, %%mm2		\n\t"
			"xorl %%ecx, %%ecx		\n\t"
			"call funnyYCode			\n\t"
			"movq temp0, %%mm2		\n\t"
			"xorl %%ecx, %%ecx		\n\t"
			"call funnyYCode			\n\t"
			"movq temp0, %%mm2		\n\t"
			"xorl %%ecx, %%ecx		\n\t"
			"call funnyYCode			\n\t"
			"movq temp0, %%mm2		\n\t"
			"xorl %%ecx, %%ecx		\n\t"
			"call funnyYCode			\n\t"
			"movq temp0, %%mm2		\n\t"
			"xorl %%ecx, %%ecx		\n\t"
			"call funnyYCode			\n\t"
			"movq temp0, %%mm2		\n\t"
			"xorl %%ecx, %%ecx		\n\t"
			"call funnyYCode			\n\t"
			"movq temp0, %%mm2		\n\t"
			"xorl %%ecx, %%ecx		\n\t"
			"call funnyYCode			\n\t"
			:: "m" (src), "m" (buf1), "m" (dstw), "m" ((s_xinc*4)>>16),
			"m" ((s_xinc*4)&0xFFFF), "m" (s_xinc&0xFFFF)
			: "%eax", "%ebx", "%ecx", "%edx", "%esi", "%edi"
		);
	}
	else
	{
#endif
	//NO MMX just normal asm ... FIXME try/write funny MMX2 variant
	//FIXME add prefetch
	asm volatile(
		"xorl %%eax, %%eax		\n\t" // i
		"xorl %%ebx, %%ebx		\n\t" // xx
		"xorl %%ecx, %%ecx		\n\t" // 2*xalpha
		"1:				\n\t"
		"movzbl  (%0, %%ebx), %%edi	\n\t" //src[xx]
		"movzbl 1(%0, %%ebx), %%esi	\n\t" //src[xx+1]
		"subl %%edi, %%esi		\n\t" //src[xx+1] - src[xx]
		"imull %%ecx, %%esi		\n\t" //(src[xx+1] - src[xx])*2*xalpha
		"shll $16, %%edi		\n\t"
		"addl %%edi, %%esi		\n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
		"movl %1, %%edi			\n\t"
		"shrl $9, %%esi			\n\t"
		"movw %%si, (%%edi, %%eax, 2)	\n\t"
		"addw %4, %%cx			\n\t" //2*xalpha += s_xinc&0xFF
		"adcl %3, %%ebx			\n\t" //xx+= s_xinc>>8 + carry

		"movzbl (%0, %%ebx), %%edi	\n\t" //src[xx]
		"movzbl 1(%0, %%ebx), %%esi	\n\t" //src[xx+1]
		"subl %%edi, %%esi		\n\t" //src[xx+1] - src[xx]
		"imull %%ecx, %%esi		\n\t" //(src[xx+1] - src[xx])*2*xalpha
		"shll $16, %%edi		\n\t"
		"addl %%edi, %%esi		\n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
		"movl %1, %%edi			\n\t"
		"shrl $9, %%esi			\n\t"
		"movw %%si, 2(%%edi, %%eax, 2)	\n\t"
		"addw %4, %%cx			\n\t" //2*xalpha += s_xinc&0xFF
		"adcl %3, %%ebx			\n\t" //xx+= s_xinc>>8 + carry


		"addl $2, %%eax			\n\t"
		"cmpl %2, %%eax			\n\t"
		" jb 1b				\n\t"


		:: "r" (src), "m" (buf1), "m" (dstw), "m" (s_xinc>>16), "m" (s_xinc&0xFFFF)
		: "%eax", "%ebx", "%ecx", "%edi", "%esi"
		);
#ifdef HAVE_MMX2
	} //if MMX2 cant be used
#endif
#else
      for(i=0;i<dstw;i++){
	register unsigned int xx=xpos>>16;
        register unsigned int xalpha=(xpos&0xFFFF)>>9;
	buf1[i]=(src[xx]*(xalpha^127)+src[xx+1]*xalpha);
	xpos+=s_xinc;
      }
#endif
    }
      // *** horizontal scale U and V lines to temp buffer
    if(s_last_y1pos!=y1){
        unsigned char *src1=srcptr[1]+(y1-y/2)*stride[1];
        unsigned char *src2=srcptr[2]+(y1-y/2)*stride[2];
        int xpos=0;
	s_last_y1pos= y1;
#ifdef ARCH_X86
#ifdef HAVE_MMX2
	if(canMMX2BeUsed)
	{
		asm volatile(
		"pxor %%mm7, %%mm7		\n\t"
		"pxor %%mm2, %%mm2		\n\t" // 2*xalpha
		"movd %5, %%mm6			\n\t" // s_xinc&0xFFFF
		"punpcklwd %%mm6, %%mm6		\n\t"
		"punpcklwd %%mm6, %%mm6		\n\t"
		"movq %%mm6, %%mm2		\n\t"
		"psllq $16, %%mm2		\n\t"
		"paddw %%mm6, %%mm2		\n\t"
		"psllq $16, %%mm2		\n\t"
		"paddw %%mm6, %%mm2		\n\t"
		"psllq $16, %%mm2		\n\t" //0,t,2t,3t		t=s_xinc&0xFFFF
		"movq %%mm2, temp0		\n\t"
		"movd %4, %%mm6			\n\t" //(s_xinc*4)&0xFFFF
		"punpcklwd %%mm6, %%mm6		\n\t"
		"punpcklwd %%mm6, %%mm6		\n\t"
		"xorl %%eax, %%eax		\n\t" // i
		"movl %0, %%esi			\n\t" // src
		"movl %1, %%edi			\n\t" // buf1
		"movl %3, %%edx			\n\t" // (s_xinc*4)>>16
		"xorl %%ecx, %%ecx		\n\t"
		"xorl %%ebx, %%ebx		\n\t"
		"movw %4, %%bx			\n\t" // (s_xinc*4)&0xFFFF

//	"int $3\n\t"
#define FUNNYUVCODE \
		"call funnyUVCode		\n\t"\
		"movq temp0, %%mm2		\n\t"\
		"xorl %%ecx, %%ecx		\n\t"

FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE

FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE



		"xorl %%eax, %%eax		\n\t" // i
		"movl %6, %%esi			\n\t" // src
		"movl %1, %%edi			\n\t" // buf1
		"addl $4096, %%edi		\n\t"

FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE

FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE

		:: "m" (src1), "m" (uvbuf1), "m" (dstw), "m" ((s_xinc2*4)>>16),
		  "m" ((s_xinc2*4)&0xFFFF), "m" (s_xinc2&0xFFFF), "m" (src2)
		: "%eax", "%ebx", "%ecx", "%edx", "%esi", "%edi"
	);
	}
	else
	{
#endif
	asm volatile(
		"xorl %%eax, %%eax		\n\t" // i
		"xorl %%ebx, %%ebx		\n\t" // xx
		"xorl %%ecx, %%ecx		\n\t" // 2*xalpha
		"1:				\n\t"
		"movl %0, %%esi			\n\t"
		"movzbl  (%%esi, %%ebx), %%edi	\n\t" //src[xx]
		"movzbl 1(%%esi, %%ebx), %%esi	\n\t" //src[xx+1]
		"subl %%edi, %%esi		\n\t" //src[xx+1] - src[xx]
		"imull %%ecx, %%esi		\n\t" //(src[xx+1] - src[xx])*2*xalpha
		"shll $16, %%edi		\n\t"
		"addl %%edi, %%esi		\n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
		"movl %1, %%edi			\n\t"
		"shrl $9, %%esi			\n\t"
		"movw %%si, (%%edi, %%eax, 2)	\n\t"

		"movzbl  (%5, %%ebx), %%edi	\n\t" //src[xx]
		"movzbl 1(%5, %%ebx), %%esi	\n\t" //src[xx+1]
		"subl %%edi, %%esi		\n\t" //src[xx+1] - src[xx]
		"imull %%ecx, %%esi		\n\t" //(src[xx+1] - src[xx])*2*xalpha
		"shll $16, %%edi		\n\t"
		"addl %%edi, %%esi		\n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
		"movl %1, %%edi			\n\t"
		"shrl $9, %%esi			\n\t"
		"movw %%si, 4096(%%edi, %%eax, 2)\n\t"

		"addw %4, %%cx			\n\t" //2*xalpha += s_xinc&0xFF
		"adcl %3, %%ebx			\n\t" //xx+= s_xinc>>8 + carry
		"addl $1, %%eax			\n\t"
		"cmpl %2, %%eax			\n\t"
		" jb 1b				\n\t"


		:: "m" (src1), "m" (uvbuf1), "m" (dstw), "m" (s_xinc2>>16), "m" (s_xinc2&0xFFFF),
		"r" (src2)
		: "%eax", "%ebx", "%ecx", "%edi", "%esi"
		);
#ifdef HAVE_MMX2
	} //if MMX2 cant be used
#endif
#else
      for(i=0;i<dstw;i++){
	  register unsigned int xx=xpos>>16;
          register unsigned int xalpha=(xpos&0xFFFF)>>9;
	  uvbuf1[i]=(src1[xx]*(xalpha^127)+src1[xx+1]*xalpha);
	  uvbuf1[i+2048]=(src2[xx]*(xalpha^127)+src2[xx+1]*xalpha);
	  xpos+=s_xinc2;
      }
#endif
    }


    // Note1: this code can be resticted to n*8 (or n*16) width lines to simplify optimization...
    // Re: Note1: ok n*4 for now
    // Note2: instead of using lookup tabs, mmx version could do the multiply...
    // Re: Note2: yep
    // Note3: maybe we should make separated 15/16, 24 and 32bpp version of this:
    // Re: done (32 & 16) and 16 has dithering :) but 16 is untested
#ifdef HAVE_MMX
	//FIXME write lq version with less uv ...
	//FIXME reorder / optimize
	if(dstbpp == 32)
	{
		asm volatile(

#define YSCALEYUV2RGB \
		"pxor %%mm7, %%mm7		\n\t"\
		"movd %6, %%mm6			\n\t" /*yalpha1*/\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"movd %7, %%mm5			\n\t" /*uvalpha1*/\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"xorl %%eax, %%eax		\n\t"\
		"1:				\n\t"\
		"movq (%0, %%eax, 2), %%mm0	\n\t" /*buf0[eax]*/\
		"movq (%1, %%eax, 2), %%mm1	\n\t" /*buf1[eax]*/\
		"movq (%2, %%eax,2), %%mm2	\n\t" /* uvbuf0[eax]*/\
		"movq (%3, %%eax,2), %%mm3	\n\t" /* uvbuf1[eax]*/\
		"psubw %%mm1, %%mm0		\n\t" /* buf0[eax] - buf1[eax]*/\
		"psubw %%mm3, %%mm2		\n\t" /* uvbuf0[eax] - uvbuf1[eax]*/\
		"pmulhw %%mm6, %%mm0		\n\t" /* (buf0[eax] - buf1[eax])yalpha1>>16*/\
		"pmulhw %%mm5, %%mm2		\n\t" /* (uvbuf0[eax] - uvbuf1[eax])uvalpha1>>16*/\
		"psraw $7, %%mm1		\n\t" /* buf0[eax] - buf1[eax] >>7*/\
		"movq 4096(%2, %%eax,2), %%mm4	\n\t" /* uvbuf0[eax+2048]*/\
		"psraw $7, %%mm3		\n\t" /* uvbuf0[eax] - uvbuf1[eax] >>7*/\
		"paddw %%mm0, %%mm1		\n\t" /* buf0[eax]yalpha1 + buf1[eax](1-yalpha1) >>16*/\
		"movq 4096(%3, %%eax,2), %%mm0	\n\t" /* uvbuf1[eax+2048]*/\
		"paddw %%mm2, %%mm3		\n\t" /* uvbuf0[eax]uvalpha1 - uvbuf1[eax](1-uvalpha1)*/\
		"psubw %%mm0, %%mm4		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048]*/\
		"psubw w10, %%mm1		\n\t" /* Y-16*/\
		"psubw w80, %%mm3		\n\t" /* (U-128)*/\
		"psllw $3, %%mm1		\n\t" /* (y-16)*8*/\
		"psllw $3, %%mm3		\n\t" /*(U-128)8*/\
		"pmulhw yCoeff, %%mm1		\n\t"\
\
\
		"pmulhw %%mm5, %%mm4		\n\t" /* (uvbuf0[eax+2048] - uvbuf1[eax+2048])uvalpha1>>16*/\
		"movq %%mm3, %%mm2		\n\t" /* (U-128)8*/\
		"pmulhw ubCoeff, %%mm3		\n\t"\
		"psraw $7, %%mm0		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048] >>7*/\
		"pmulhw ugCoeff, %%mm2		\n\t"\
		"paddw %%mm4, %%mm0		\n\t" /* uvbuf0[eax+2048]uvalpha1 - uvbuf1[eax+2048](1-uvalpha1)*/\
		"psubw w80, %%mm0		\n\t" /* (V-128)*/\
		"psllw $3, %%mm0		\n\t" /* (V-128)8*/\
\
\
		"movq %%mm0, %%mm4		\n\t" /* (V-128)8*/\
		"pmulhw vrCoeff, %%mm0		\n\t"\
		"pmulhw vgCoeff, %%mm4		\n\t"\
		"paddw %%mm1, %%mm3		\n\t" /* B*/\
		"paddw %%mm1, %%mm0		\n\t" /* R*/\
		"packuswb %%mm3, %%mm3		\n\t"\
\
		"packuswb %%mm0, %%mm0		\n\t"\
		"paddw %%mm4, %%mm2		\n\t"\
		"paddw %%mm2, %%mm1		\n\t" /* G*/\
\
		"packuswb %%mm1, %%mm1		\n\t"

YSCALEYUV2RGB
		"punpcklbw %%mm1, %%mm3		\n\t" // BGBGBGBG
		"punpcklbw %%mm7, %%mm0		\n\t" // R0R0R0R0

		"movq %%mm3, %%mm1		\n\t"
		"punpcklwd %%mm0, %%mm3		\n\t" // BGR0BGR0
		"punpckhwd %%mm0, %%mm1		\n\t" // BGR0BGR0
#ifdef HAVE_MMX2
		"movntq %%mm3, (%4, %%eax, 4)	\n\t"
		"movntq %%mm1, 8(%4, %%eax, 4)	\n\t"
#else
		"movq %%mm3, (%4, %%eax, 4)	\n\t"
		"movq %%mm1, 8(%4, %%eax, 4)	\n\t"
#endif
		"addl $4, %%eax			\n\t"
		"cmpl %5, %%eax			\n\t"
		" jb 1b				\n\t"


		:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
		"m" (yalpha1), "m" (uvalpha1)
		: "%eax"
		);
	}
	else if(dstbpp==24)
	{
		asm volatile(

YSCALEYUV2RGB

							// lsb ... msb
		"punpcklbw %%mm1, %%mm3		\n\t" // BGBGBGBG
		"punpcklbw %%mm7, %%mm0		\n\t" // R0R0R0R0

		"movq %%mm3, %%mm1		\n\t"
		"punpcklwd %%mm0, %%mm3		\n\t" // BGR0BGR0
		"punpckhwd %%mm0, %%mm1		\n\t" // BGR0BGR0

		"movq %%mm3, %%mm2		\n\t" // BGR0BGR0
		"psrlq $8, %%mm3		\n\t" // GR0BGR00
		"pand bm00000111, %%mm2		\n\t" // BGR00000
		"pand bm11111000, %%mm3		\n\t" // 000BGR00
		"por %%mm2, %%mm3		\n\t" // BGRBGR00
		"movq %%mm1, %%mm2		\n\t"
		"psllq $48, %%mm1		\n\t" // 000000BG
		"por %%mm1, %%mm3		\n\t" // BGRBGRBG

		"movq %%mm2, %%mm1		\n\t" // BGR0BGR0
		"psrld $16, %%mm2		\n\t" // R000R000
		"psrlq $24, %%mm1		\n\t" // 0BGR0000
		"por %%mm2, %%mm1		\n\t" // RBGRR000

		"movl %4, %%ebx			\n\t"
		"addl %%eax, %%ebx		\n\t"
#ifdef HAVE_MMX2
		//FIXME Alignment
		"movntq %%mm3, (%%ebx, %%eax, 2)\n\t"
		"movntq %%mm1, 8(%%ebx, %%eax, 2)\n\t"
#else
		"movd %%mm3, (%%ebx, %%eax, 2)	\n\t"
		"psrlq $32, %%mm3		\n\t"
		"movd %%mm3, 4(%%ebx, %%eax, 2)	\n\t"
		"movd %%mm1, 8(%%ebx, %%eax, 2)	\n\t"
#endif
		"addl $4, %%eax			\n\t"
		"cmpl %5, %%eax			\n\t"
		" jb 1b				\n\t"

		:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "m" (dest), "m" (dstw),
		"m" (yalpha1), "m" (uvalpha1)
		: "%eax", "%ebx"
		);
	}
	else if(dstbpp==16)
	{
		asm volatile(

YSCALEYUV2RGB
#ifdef DITHER16BPP
		"paddusb g16Dither, %%mm1	\n\t"
		"paddusb b16Dither, %%mm0	\n\t"
		"paddusb b16Dither, %%mm3	\n\t"
#endif
		"punpcklbw %%mm7, %%mm1		\n\t" // 0G0G0G0G
		"punpcklbw %%mm7, %%mm3		\n\t" // 0B0B0B0B
		"punpcklbw %%mm7, %%mm0		\n\t" // 0R0R0R0R

		"psrlw $3, %%mm3		\n\t"
		"psllw $3, %%mm1		\n\t"
		"psllw $8, %%mm0		\n\t"
		"pand g16Mask, %%mm1		\n\t"
		"pand r16Mask, %%mm0		\n\t"

		"por %%mm3, %%mm1		\n\t"
		"por %%mm1, %%mm0		\n\t"
#ifdef HAVE_MMX2
		"movntq %%mm0, (%4, %%eax, 2)	\n\t"
#else
		"movq %%mm0, (%4, %%eax, 2)	\n\t"
#endif
		"addl $4, %%eax			\n\t"
		"cmpl %5, %%eax			\n\t"
		" jb 1b				\n\t"

		:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
		"m" (yalpha1), "m" (uvalpha1)
		: "%eax"
		);
	}
#else
	if(dstbpp==32 || dstbpp==24)
	{
		for(i=0;i<dstw;i++){
			// vertical linear interpolation && yuv2rgb in a single step:
			int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>16)];
			int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>16);
			int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>16);
			dest[0]=clip_table[((Y + yuvtab_3343[U]) >>13)];
			dest[1]=clip_table[((Y + yuvtab_0c92[V] + yuvtab_1a1e[U]) >>13)];
			dest[2]=clip_table[((Y + yuvtab_40cf[V]) >>13)];
			dest+=dstbpp>>3;
		}
	}
	else if(dstbpp==16)
	{
		for(i=0;i<dstw;i++){
			// vertical linear interpolation && yuv2rgb in a single step:
			int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>16)];
			int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>16);
			int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>16);

			((uint16_t*)dest)[0] =
				(clip_table[((Y + yuvtab_3343[U]) >>13)]>>3) |
				(clip_table[((Y + yuvtab_0c92[V] + yuvtab_1a1e[U]) >>13)]<<3)&0x07E0 |
				(clip_table[((Y + yuvtab_40cf[V]) >>13)]<<8)&0xF800;
			dest+=2;
		}
	}
	else if(dstbpp==15) //15bit FIXME how do i figure out if its 15 or 16?
	{
		for(i=0;i<dstw;i++){
			// vertical linear interpolation && yuv2rgb in a single step:
			int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>16)];
			int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>16);
			int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>16);

			((uint16_t*)dest)[0] =
				(clip_table[((Y + yuvtab_3343[U]) >>13)]>>3) |
				(clip_table[((Y + yuvtab_0c92[V] + yuvtab_1a1e[U]) >>13)]<<2)&0x03E0 |
				(clip_table[((Y + yuvtab_40cf[V]) >>13)]<<7)&0x7C00;
			dest+=2;
		}
	}
#endif

	b16Dither= b16Dither1;
	b16Dither1= b16Dither2;
	b16Dither2= b16Dither;

	g16Dither= g16Dither1;
	g16Dither1= g16Dither2;
	g16Dither2= g16Dither;
  }

#ifdef HAVE_3DNOW
	asm volatile("femms");
#elif defined (HAVE_MMX)
	asm volatile("emms");
#endif
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

}
