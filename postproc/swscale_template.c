
// Software scaling and colorspace conversion routines for MPlayer

// Orginal C implementation by A'rpi/ESP-team <arpi@thot.banki.hu>
// current version mostly by Michael Niedermayer (michaelni@gmx.at)
// the parts written by michael are under GNU GPL

#undef MOVNTQ
#undef PAVGB
#undef PREFETCH
#undef PREFETCHW
#undef EMMS
#undef SFENCE

#ifdef HAVE_3DNOW
/* On K6 femms is faster of emms. On K7 femms is directly mapped on emms. */
#define EMMS     "femms"
#else
#define EMMS     "emms"
#endif

#ifdef HAVE_3DNOW
#define PREFETCH  "prefetch"
#define PREFETCHW "prefetchw"
#elif defined ( HAVE_MMX2 )
#define PREFETCH "prefetchnta"
#define PREFETCHW "prefetcht0"
#else
#define PREFETCH "/nop"
#define PREFETCHW "/nop"
#endif

#ifdef HAVE_MMX2
#define SFENCE "sfence"
#else
#define SFENCE "/nop"
#endif

#ifdef HAVE_MMX2
#define PAVGB(a,b) "pavgb " #a ", " #b " \n\t"
#elif defined (HAVE_3DNOW)
#define PAVGB(a,b) "pavgusb " #a ", " #b " \n\t"
#endif

#ifdef HAVE_MMX2
#define MOVNTQ(a,b) "movntq " #a ", " #b " \n\t"
#else
#define MOVNTQ(a,b) "movq " #a ", " #b " \n\t"
#endif


#define YSCALEYUV2YV12X(x) \
			"xorl %%eax, %%eax		\n\t"\
			"pxor %%mm3, %%mm3		\n\t"\
			"pxor %%mm4, %%mm4		\n\t"\
			"movl %0, %%edx			\n\t"\
			".balign 16			\n\t" /* FIXME Unroll? */\
			"1:				\n\t"\
			"movl (%1, %%edx, 4), %%esi	\n\t"\
			"movq (%2, %%edx, 8), %%mm0	\n\t" /* filterCoeff */\
			"movq " #x "(%%esi, %%eax, 2), %%mm2	\n\t" /* srcData */\
			"movq 8+" #x "(%%esi, %%eax, 2), %%mm5	\n\t" /* srcData */\
			"pmulhw %%mm0, %%mm2		\n\t"\
			"pmulhw %%mm0, %%mm5		\n\t"\
			"paddw %%mm2, %%mm3		\n\t"\
			"paddw %%mm5, %%mm4		\n\t"\
			"addl $1, %%edx			\n\t"\
			" jnz 1b			\n\t"\
			"psraw $3, %%mm3		\n\t"\
			"psraw $3, %%mm4		\n\t"\
			"packuswb %%mm4, %%mm3		\n\t"\
			MOVNTQ(%%mm3, (%3, %%eax))\
			"addl $8, %%eax			\n\t"\
			"cmpl %4, %%eax			\n\t"\
			"pxor %%mm3, %%mm3		\n\t"\
			"pxor %%mm4, %%mm4		\n\t"\
			"movl %0, %%edx			\n\t"\
			"jb 1b				\n\t"

#define YSCALEYUV2YV121 \
			"movl %2, %%eax			\n\t"\
			".balign 16			\n\t" /* FIXME Unroll? */\
			"1:				\n\t"\
			"movq (%0, %%eax, 2), %%mm0	\n\t"\
			"movq 8(%0, %%eax, 2), %%mm1	\n\t"\
			"psraw $7, %%mm0		\n\t"\
			"psraw $7, %%mm1		\n\t"\
			"packuswb %%mm1, %%mm0		\n\t"\
			MOVNTQ(%%mm0, (%1, %%eax))\
			"addl $8, %%eax			\n\t"\
			"jnc 1b				\n\t"

/*
			:: "m" (-lumFilterSize), "m" (-chrFilterSize),
			   "m" (lumMmxFilter+lumFilterSize*4), "m" (chrMmxFilter+chrFilterSize*4),
			   "r" (dest), "m" (dstW),
			   "m" (lumSrc+lumFilterSize), "m" (chrSrc+chrFilterSize)
			: "%eax", "%ebx", "%ecx", "%edx", "%esi"
*/
#define YSCALEYUV2RGBX \
		"xorl %%eax, %%eax		\n\t"\
		".balign 16			\n\t"\
		"1:				\n\t"\
		"movl %1, %%edx			\n\t" /* -chrFilterSize */\
		"movl %3, %%ebx			\n\t" /* chrMmxFilter+lumFilterSize */\
		"movl %7, %%ecx			\n\t" /* chrSrc+lumFilterSize */\
		"pxor %%mm3, %%mm3		\n\t"\
		"pxor %%mm4, %%mm4		\n\t"\
		"2:				\n\t"\
		"movl (%%ecx, %%edx, 4), %%esi	\n\t"\
		"movq (%%ebx, %%edx, 8), %%mm0	\n\t" /* filterCoeff */\
		"movq (%%esi, %%eax), %%mm2	\n\t" /* UsrcData */\
		"movq 4096(%%esi, %%eax), %%mm5	\n\t" /* VsrcData */\
		"pmulhw %%mm0, %%mm2		\n\t"\
		"pmulhw %%mm0, %%mm5		\n\t"\
		"paddw %%mm2, %%mm3		\n\t"\
		"paddw %%mm5, %%mm4		\n\t"\
		"addl $1, %%edx			\n\t"\
		" jnz 2b			\n\t"\
\
		"movl %0, %%edx			\n\t" /* -lumFilterSize */\
		"movl %2, %%ebx			\n\t" /* lumMmxFilter+lumFilterSize */\
		"movl %6, %%ecx			\n\t" /* lumSrc+lumFilterSize */\
		"pxor %%mm1, %%mm1		\n\t"\
		"pxor %%mm7, %%mm7		\n\t"\
		"2:				\n\t"\
		"movl (%%ecx, %%edx, 4), %%esi	\n\t"\
		"movq (%%ebx, %%edx, 8), %%mm0	\n\t" /* filterCoeff */\
		"movq (%%esi, %%eax, 2), %%mm2	\n\t" /* Y1srcData */\
		"movq 8(%%esi, %%eax, 2), %%mm5	\n\t" /* Y2srcData */\
		"pmulhw %%mm0, %%mm2		\n\t"\
		"pmulhw %%mm0, %%mm5		\n\t"\
		"paddw %%mm2, %%mm1		\n\t"\
		"paddw %%mm5, %%mm7		\n\t"\
		"addl $1, %%edx			\n\t"\
		" jnz 2b			\n\t"\
\
		"psubw w400, %%mm3		\n\t" /* (U-128)8*/\
		"psubw w400, %%mm4		\n\t" /* (V-128)8*/\
		"movq %%mm3, %%mm2		\n\t" /* (U-128)8*/\
		"movq %%mm4, %%mm5		\n\t" /* (V-128)8*/\
		"pmulhw ugCoeff, %%mm3		\n\t"\
		"pmulhw vgCoeff, %%mm4		\n\t"\
	/* mm2=(U-128)8, mm3=ug, mm4=vg mm5=(V-128)8 */\
		"pmulhw ubCoeff, %%mm2		\n\t"\
		"pmulhw vrCoeff, %%mm5		\n\t"\
		"psubw w80, %%mm1		\n\t" /* 8(Y-16)*/\
		"psubw w80, %%mm7		\n\t" /* 8(Y-16)*/\
		"pmulhw yCoeff, %%mm1		\n\t"\
		"pmulhw yCoeff, %%mm7		\n\t"\
	/* mm1= Y1, mm2=ub, mm3=ug, mm4=vg mm5=vr, mm7=Y2 */\
		"paddw %%mm3, %%mm4		\n\t"\
		"movq %%mm2, %%mm0		\n\t"\
		"movq %%mm5, %%mm6		\n\t"\
		"movq %%mm4, %%mm3		\n\t"\
		"punpcklwd %%mm2, %%mm2		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm4, %%mm4		\n\t"\
		"paddw %%mm1, %%mm2		\n\t"\
		"paddw %%mm1, %%mm5		\n\t"\
		"paddw %%mm1, %%mm4		\n\t"\
		"punpckhwd %%mm0, %%mm0		\n\t"\
		"punpckhwd %%mm6, %%mm6		\n\t"\
		"punpckhwd %%mm3, %%mm3		\n\t"\
		"paddw %%mm7, %%mm0		\n\t"\
		"paddw %%mm7, %%mm6		\n\t"\
		"paddw %%mm7, %%mm3		\n\t"\
		/* mm0=B1, mm2=B2, mm3=G2, mm4=G1, mm5=R1, mm6=R2 */\
		"packuswb %%mm0, %%mm2		\n\t"\
		"packuswb %%mm6, %%mm5		\n\t"\
		"packuswb %%mm3, %%mm4		\n\t"\
		"pxor %%mm7, %%mm7		\n\t"

#define FULL_YSCALEYUV2RGB \
		"pxor %%mm7, %%mm7		\n\t"\
		"movd %6, %%mm6			\n\t" /*yalpha1*/\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"movd %7, %%mm5			\n\t" /*uvalpha1*/\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"xorl %%eax, %%eax		\n\t"\
		".balign 16			\n\t"\
		"1:				\n\t"\
		"movq (%0, %%eax, 2), %%mm0	\n\t" /*buf0[eax]*/\
		"movq (%1, %%eax, 2), %%mm1	\n\t" /*buf1[eax]*/\
		"movq (%2, %%eax,2), %%mm2	\n\t" /* uvbuf0[eax]*/\
		"movq (%3, %%eax,2), %%mm3	\n\t" /* uvbuf1[eax]*/\
		"psubw %%mm1, %%mm0		\n\t" /* buf0[eax] - buf1[eax]*/\
		"psubw %%mm3, %%mm2		\n\t" /* uvbuf0[eax] - uvbuf1[eax]*/\
		"pmulhw %%mm6, %%mm0		\n\t" /* (buf0[eax] - buf1[eax])yalpha1>>16*/\
		"pmulhw %%mm5, %%mm2		\n\t" /* (uvbuf0[eax] - uvbuf1[eax])uvalpha1>>16*/\
		"psraw $4, %%mm1		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"movq 4096(%2, %%eax,2), %%mm4	\n\t" /* uvbuf0[eax+2048]*/\
		"psraw $4, %%mm3		\n\t" /* uvbuf0[eax] - uvbuf1[eax] >>4*/\
		"paddw %%mm0, %%mm1		\n\t" /* buf0[eax]yalpha1 + buf1[eax](1-yalpha1) >>16*/\
		"movq 4096(%3, %%eax,2), %%mm0	\n\t" /* uvbuf1[eax+2048]*/\
		"paddw %%mm2, %%mm3		\n\t" /* uvbuf0[eax]uvalpha1 - uvbuf1[eax](1-uvalpha1)*/\
		"psubw %%mm0, %%mm4		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048]*/\
		"psubw w80, %%mm1		\n\t" /* 8(Y-16)*/\
		"psubw w400, %%mm3		\n\t" /* 8(U-128)*/\
		"pmulhw yCoeff, %%mm1		\n\t"\
\
\
		"pmulhw %%mm5, %%mm4		\n\t" /* (uvbuf0[eax+2048] - uvbuf1[eax+2048])uvalpha1>>16*/\
		"movq %%mm3, %%mm2		\n\t" /* (U-128)8*/\
		"pmulhw ubCoeff, %%mm3		\n\t"\
		"psraw $4, %%mm0		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048] >>4*/\
		"pmulhw ugCoeff, %%mm2		\n\t"\
		"paddw %%mm4, %%mm0		\n\t" /* uvbuf0[eax+2048]uvalpha1 - uvbuf1[eax+2048](1-uvalpha1)*/\
		"psubw w400, %%mm0		\n\t" /* (V-128)8*/\
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

#define YSCALEYUV2RGB \
		"movd %6, %%mm6			\n\t" /*yalpha1*/\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"movq %%mm6, asm_yalpha1	\n\t"\
		"movd %7, %%mm5			\n\t" /*uvalpha1*/\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"movq %%mm5, asm_uvalpha1	\n\t"\
		"xorl %%eax, %%eax		\n\t"\
		".balign 16			\n\t"\
		"1:				\n\t"\
		"movq (%2, %%eax), %%mm2	\n\t" /* uvbuf0[eax]*/\
		"movq (%3, %%eax), %%mm3	\n\t" /* uvbuf1[eax]*/\
		"movq 4096(%2, %%eax), %%mm5	\n\t" /* uvbuf0[eax+2048]*/\
		"movq 4096(%3, %%eax), %%mm4	\n\t" /* uvbuf1[eax+2048]*/\
		"psubw %%mm3, %%mm2		\n\t" /* uvbuf0[eax] - uvbuf1[eax]*/\
		"psubw %%mm4, %%mm5		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048]*/\
		"movq asm_uvalpha1, %%mm0	\n\t"\
		"pmulhw %%mm0, %%mm2		\n\t" /* (uvbuf0[eax] - uvbuf1[eax])uvalpha1>>16*/\
		"pmulhw %%mm0, %%mm5		\n\t" /* (uvbuf0[eax+2048] - uvbuf1[eax+2048])uvalpha1>>16*/\
		"psraw $4, %%mm3		\n\t" /* uvbuf0[eax] - uvbuf1[eax] >>4*/\
		"psraw $4, %%mm4		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048] >>4*/\
		"paddw %%mm2, %%mm3		\n\t" /* uvbuf0[eax]uvalpha1 - uvbuf1[eax](1-uvalpha1)*/\
		"paddw %%mm5, %%mm4		\n\t" /* uvbuf0[eax+2048]uvalpha1 - uvbuf1[eax+2048](1-uvalpha1)*/\
		"psubw w400, %%mm3		\n\t" /* (U-128)8*/\
		"psubw w400, %%mm4		\n\t" /* (V-128)8*/\
		"movq %%mm3, %%mm2		\n\t" /* (U-128)8*/\
		"movq %%mm4, %%mm5		\n\t" /* (V-128)8*/\
		"pmulhw ugCoeff, %%mm3		\n\t"\
		"pmulhw vgCoeff, %%mm4		\n\t"\
	/* mm2=(U-128)8, mm3=ug, mm4=vg mm5=(V-128)8 */\
		"movq (%0, %%eax, 2), %%mm0	\n\t" /*buf0[eax]*/\
		"movq (%1, %%eax, 2), %%mm1	\n\t" /*buf1[eax]*/\
		"movq 8(%0, %%eax, 2), %%mm6	\n\t" /*buf0[eax]*/\
		"movq 8(%1, %%eax, 2), %%mm7	\n\t" /*buf1[eax]*/\
		"psubw %%mm1, %%mm0		\n\t" /* buf0[eax] - buf1[eax]*/\
		"psubw %%mm7, %%mm6		\n\t" /* buf0[eax] - buf1[eax]*/\
		"pmulhw asm_yalpha1, %%mm0	\n\t" /* (buf0[eax] - buf1[eax])yalpha1>>16*/\
		"pmulhw asm_yalpha1, %%mm6	\n\t" /* (buf0[eax] - buf1[eax])yalpha1>>16*/\
		"psraw $4, %%mm1		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"psraw $4, %%mm7		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"paddw %%mm0, %%mm1		\n\t" /* buf0[eax]yalpha1 + buf1[eax](1-yalpha1) >>16*/\
		"paddw %%mm6, %%mm7		\n\t" /* buf0[eax]yalpha1 + buf1[eax](1-yalpha1) >>16*/\
		"pmulhw ubCoeff, %%mm2		\n\t"\
		"pmulhw vrCoeff, %%mm5		\n\t"\
		"psubw w80, %%mm1		\n\t" /* 8(Y-16)*/\
		"psubw w80, %%mm7		\n\t" /* 8(Y-16)*/\
		"pmulhw yCoeff, %%mm1		\n\t"\
		"pmulhw yCoeff, %%mm7		\n\t"\
	/* mm1= Y1, mm2=ub, mm3=ug, mm4=vg mm5=vr, mm7=Y2 */\
		"paddw %%mm3, %%mm4		\n\t"\
		"movq %%mm2, %%mm0		\n\t"\
		"movq %%mm5, %%mm6		\n\t"\
		"movq %%mm4, %%mm3		\n\t"\
		"punpcklwd %%mm2, %%mm2		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm4, %%mm4		\n\t"\
		"paddw %%mm1, %%mm2		\n\t"\
		"paddw %%mm1, %%mm5		\n\t"\
		"paddw %%mm1, %%mm4		\n\t"\
		"punpckhwd %%mm0, %%mm0		\n\t"\
		"punpckhwd %%mm6, %%mm6		\n\t"\
		"punpckhwd %%mm3, %%mm3		\n\t"\
		"paddw %%mm7, %%mm0		\n\t"\
		"paddw %%mm7, %%mm6		\n\t"\
		"paddw %%mm7, %%mm3		\n\t"\
		/* mm0=B1, mm2=B2, mm3=G2, mm4=G1, mm5=R1, mm6=R2 */\
		"packuswb %%mm0, %%mm2		\n\t"\
		"packuswb %%mm6, %%mm5		\n\t"\
		"packuswb %%mm3, %%mm4		\n\t"\
		"pxor %%mm7, %%mm7		\n\t"

#define YSCALEYUV2RGB1 \
		"xorl %%eax, %%eax		\n\t"\
		".balign 16			\n\t"\
		"1:				\n\t"\
		"movq (%2, %%eax), %%mm3	\n\t" /* uvbuf0[eax]*/\
		"movq 4096(%2, %%eax), %%mm4	\n\t" /* uvbuf0[eax+2048]*/\
		"psraw $4, %%mm3		\n\t" /* uvbuf0[eax] - uvbuf1[eax] >>4*/\
		"psraw $4, %%mm4		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048] >>4*/\
		"psubw w400, %%mm3		\n\t" /* (U-128)8*/\
		"psubw w400, %%mm4		\n\t" /* (V-128)8*/\
		"movq %%mm3, %%mm2		\n\t" /* (U-128)8*/\
		"movq %%mm4, %%mm5		\n\t" /* (V-128)8*/\
		"pmulhw ugCoeff, %%mm3		\n\t"\
		"pmulhw vgCoeff, %%mm4		\n\t"\
	/* mm2=(U-128)8, mm3=ug, mm4=vg mm5=(V-128)8 */\
		"movq (%0, %%eax, 2), %%mm1	\n\t" /*buf0[eax]*/\
		"movq 8(%0, %%eax, 2), %%mm7	\n\t" /*buf0[eax]*/\
		"psraw $4, %%mm1		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"psraw $4, %%mm7		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"pmulhw ubCoeff, %%mm2		\n\t"\
		"pmulhw vrCoeff, %%mm5		\n\t"\
		"psubw w80, %%mm1		\n\t" /* 8(Y-16)*/\
		"psubw w80, %%mm7		\n\t" /* 8(Y-16)*/\
		"pmulhw yCoeff, %%mm1		\n\t"\
		"pmulhw yCoeff, %%mm7		\n\t"\
	/* mm1= Y1, mm2=ub, mm3=ug, mm4=vg mm5=vr, mm7=Y2 */\
		"paddw %%mm3, %%mm4		\n\t"\
		"movq %%mm2, %%mm0		\n\t"\
		"movq %%mm5, %%mm6		\n\t"\
		"movq %%mm4, %%mm3		\n\t"\
		"punpcklwd %%mm2, %%mm2		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm4, %%mm4		\n\t"\
		"paddw %%mm1, %%mm2		\n\t"\
		"paddw %%mm1, %%mm5		\n\t"\
		"paddw %%mm1, %%mm4		\n\t"\
		"punpckhwd %%mm0, %%mm0		\n\t"\
		"punpckhwd %%mm6, %%mm6		\n\t"\
		"punpckhwd %%mm3, %%mm3		\n\t"\
		"paddw %%mm7, %%mm0		\n\t"\
		"paddw %%mm7, %%mm6		\n\t"\
		"paddw %%mm7, %%mm3		\n\t"\
		/* mm0=B1, mm2=B2, mm3=G2, mm4=G1, mm5=R1, mm6=R2 */\
		"packuswb %%mm0, %%mm2		\n\t"\
		"packuswb %%mm6, %%mm5		\n\t"\
		"packuswb %%mm3, %%mm4		\n\t"\
		"pxor %%mm7, %%mm7		\n\t"

// do vertical chrominance interpolation
#define YSCALEYUV2RGB1b \
		"xorl %%eax, %%eax		\n\t"\
		".balign 16			\n\t"\
		"1:				\n\t"\
		"movq (%2, %%eax), %%mm2	\n\t" /* uvbuf0[eax]*/\
		"movq (%3, %%eax), %%mm3	\n\t" /* uvbuf1[eax]*/\
		"movq 4096(%2, %%eax), %%mm5	\n\t" /* uvbuf0[eax+2048]*/\
		"movq 4096(%3, %%eax), %%mm4	\n\t" /* uvbuf1[eax+2048]*/\
		"paddw %%mm2, %%mm3		\n\t" /* uvbuf0[eax] + uvbuf1[eax]*/\
		"paddw %%mm5, %%mm4		\n\t" /* uvbuf0[eax+2048] + uvbuf1[eax+2048]*/\
		"psrlw $5, %%mm3		\n\t" /*FIXME might overflow*/\
		"psrlw $5, %%mm4		\n\t" /*FIXME might overflow*/\
		"psubw w400, %%mm3		\n\t" /* (U-128)8*/\
		"psubw w400, %%mm4		\n\t" /* (V-128)8*/\
		"movq %%mm3, %%mm2		\n\t" /* (U-128)8*/\
		"movq %%mm4, %%mm5		\n\t" /* (V-128)8*/\
		"pmulhw ugCoeff, %%mm3		\n\t"\
		"pmulhw vgCoeff, %%mm4		\n\t"\
	/* mm2=(U-128)8, mm3=ug, mm4=vg mm5=(V-128)8 */\
		"movq (%0, %%eax, 2), %%mm1	\n\t" /*buf0[eax]*/\
		"movq 8(%0, %%eax, 2), %%mm7	\n\t" /*buf0[eax]*/\
		"psraw $4, %%mm1		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"psraw $4, %%mm7		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"pmulhw ubCoeff, %%mm2		\n\t"\
		"pmulhw vrCoeff, %%mm5		\n\t"\
		"psubw w80, %%mm1		\n\t" /* 8(Y-16)*/\
		"psubw w80, %%mm7		\n\t" /* 8(Y-16)*/\
		"pmulhw yCoeff, %%mm1		\n\t"\
		"pmulhw yCoeff, %%mm7		\n\t"\
	/* mm1= Y1, mm2=ub, mm3=ug, mm4=vg mm5=vr, mm7=Y2 */\
		"paddw %%mm3, %%mm4		\n\t"\
		"movq %%mm2, %%mm0		\n\t"\
		"movq %%mm5, %%mm6		\n\t"\
		"movq %%mm4, %%mm3		\n\t"\
		"punpcklwd %%mm2, %%mm2		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm4, %%mm4		\n\t"\
		"paddw %%mm1, %%mm2		\n\t"\
		"paddw %%mm1, %%mm5		\n\t"\
		"paddw %%mm1, %%mm4		\n\t"\
		"punpckhwd %%mm0, %%mm0		\n\t"\
		"punpckhwd %%mm6, %%mm6		\n\t"\
		"punpckhwd %%mm3, %%mm3		\n\t"\
		"paddw %%mm7, %%mm0		\n\t"\
		"paddw %%mm7, %%mm6		\n\t"\
		"paddw %%mm7, %%mm3		\n\t"\
		/* mm0=B1, mm2=B2, mm3=G2, mm4=G1, mm5=R1, mm6=R2 */\
		"packuswb %%mm0, %%mm2		\n\t"\
		"packuswb %%mm6, %%mm5		\n\t"\
		"packuswb %%mm3, %%mm4		\n\t"\
		"pxor %%mm7, %%mm7		\n\t"

#define WRITEBGR32 \
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */\
			"movq %%mm2, %%mm1		\n\t" /* B */\
			"movq %%mm5, %%mm6		\n\t" /* R */\
			"punpcklbw %%mm4, %%mm2		\n\t" /* GBGBGBGB 0 */\
			"punpcklbw %%mm7, %%mm5		\n\t" /* 0R0R0R0R 0 */\
			"punpckhbw %%mm4, %%mm1		\n\t" /* GBGBGBGB 2 */\
			"punpckhbw %%mm7, %%mm6		\n\t" /* 0R0R0R0R 2 */\
			"movq %%mm2, %%mm0		\n\t" /* GBGBGBGB 0 */\
			"movq %%mm1, %%mm3		\n\t" /* GBGBGBGB 2 */\
			"punpcklwd %%mm5, %%mm0		\n\t" /* 0RGB0RGB 0 */\
			"punpckhwd %%mm5, %%mm2		\n\t" /* 0RGB0RGB 1 */\
			"punpcklwd %%mm6, %%mm1		\n\t" /* 0RGB0RGB 2 */\
			"punpckhwd %%mm6, %%mm3		\n\t" /* 0RGB0RGB 3 */\
\
			MOVNTQ(%%mm0, (%4, %%eax, 4))\
			MOVNTQ(%%mm2, 8(%4, %%eax, 4))\
			MOVNTQ(%%mm1, 16(%4, %%eax, 4))\
			MOVNTQ(%%mm3, 24(%4, %%eax, 4))\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#define WRITEBGR16 \
			"pand bF8, %%mm2		\n\t" /* B */\
			"pand bFC, %%mm4		\n\t" /* G */\
			"pand bF8, %%mm5		\n\t" /* R */\
			"psrlq $3, %%mm2		\n\t"\
\
			"movq %%mm2, %%mm1		\n\t"\
			"movq %%mm4, %%mm3		\n\t"\
\
			"punpcklbw %%mm7, %%mm3		\n\t"\
			"punpcklbw %%mm5, %%mm2		\n\t"\
			"punpckhbw %%mm7, %%mm4		\n\t"\
			"punpckhbw %%mm5, %%mm1		\n\t"\
\
			"psllq $3, %%mm3		\n\t"\
			"psllq $3, %%mm4		\n\t"\
\
			"por %%mm3, %%mm2		\n\t"\
			"por %%mm4, %%mm1		\n\t"\
\
			MOVNTQ(%%mm2, (%4, %%eax, 2))\
			MOVNTQ(%%mm1, 8(%4, %%eax, 2))\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#define WRITEBGR15 \
			"pand bF8, %%mm2		\n\t" /* B */\
			"pand bF8, %%mm4		\n\t" /* G */\
			"pand bF8, %%mm5		\n\t" /* R */\
			"psrlq $3, %%mm2		\n\t"\
			"psrlq $1, %%mm5		\n\t"\
\
			"movq %%mm2, %%mm1		\n\t"\
			"movq %%mm4, %%mm3		\n\t"\
\
			"punpcklbw %%mm7, %%mm3		\n\t"\
			"punpcklbw %%mm5, %%mm2		\n\t"\
			"punpckhbw %%mm7, %%mm4		\n\t"\
			"punpckhbw %%mm5, %%mm1		\n\t"\
\
			"psllq $2, %%mm3		\n\t"\
			"psllq $2, %%mm4		\n\t"\
\
			"por %%mm3, %%mm2		\n\t"\
			"por %%mm4, %%mm1		\n\t"\
\
			MOVNTQ(%%mm2, (%4, %%eax, 2))\
			MOVNTQ(%%mm1, 8(%4, %%eax, 2))\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#define WRITEBGR24OLD \
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */\
			"movq %%mm2, %%mm1		\n\t" /* B */\
			"movq %%mm5, %%mm6		\n\t" /* R */\
			"punpcklbw %%mm4, %%mm2		\n\t" /* GBGBGBGB 0 */\
			"punpcklbw %%mm7, %%mm5		\n\t" /* 0R0R0R0R 0 */\
			"punpckhbw %%mm4, %%mm1		\n\t" /* GBGBGBGB 2 */\
			"punpckhbw %%mm7, %%mm6		\n\t" /* 0R0R0R0R 2 */\
			"movq %%mm2, %%mm0		\n\t" /* GBGBGBGB 0 */\
			"movq %%mm1, %%mm3		\n\t" /* GBGBGBGB 2 */\
			"punpcklwd %%mm5, %%mm0		\n\t" /* 0RGB0RGB 0 */\
			"punpckhwd %%mm5, %%mm2		\n\t" /* 0RGB0RGB 1 */\
			"punpcklwd %%mm6, %%mm1		\n\t" /* 0RGB0RGB 2 */\
			"punpckhwd %%mm6, %%mm3		\n\t" /* 0RGB0RGB 3 */\
\
			"movq %%mm0, %%mm4		\n\t" /* 0RGB0RGB 0 */\
			"psrlq $8, %%mm0		\n\t" /* 00RGB0RG 0 */\
			"pand bm00000111, %%mm4		\n\t" /* 00000RGB 0 */\
			"pand bm11111000, %%mm0		\n\t" /* 00RGB000 0.5 */\
			"por %%mm4, %%mm0		\n\t" /* 00RGBRGB 0 */\
			"movq %%mm2, %%mm4		\n\t" /* 0RGB0RGB 1 */\
			"psllq $48, %%mm2		\n\t" /* GB000000 1 */\
			"por %%mm2, %%mm0		\n\t" /* GBRGBRGB 0 */\
\
			"movq %%mm4, %%mm2		\n\t" /* 0RGB0RGB 1 */\
			"psrld $16, %%mm4		\n\t" /* 000R000R 1 */\
			"psrlq $24, %%mm2		\n\t" /* 0000RGB0 1.5 */\
			"por %%mm4, %%mm2		\n\t" /* 000RRGBR 1 */\
			"pand bm00001111, %%mm2		\n\t" /* 0000RGBR 1 */\
			"movq %%mm1, %%mm4		\n\t" /* 0RGB0RGB 2 */\
			"psrlq $8, %%mm1		\n\t" /* 00RGB0RG 2 */\
			"pand bm00000111, %%mm4		\n\t" /* 00000RGB 2 */\
			"pand bm11111000, %%mm1		\n\t" /* 00RGB000 2.5 */\
			"por %%mm4, %%mm1		\n\t" /* 00RGBRGB 2 */\
			"movq %%mm1, %%mm4		\n\t" /* 00RGBRGB 2 */\
			"psllq $32, %%mm1		\n\t" /* BRGB0000 2 */\
			"por %%mm1, %%mm2		\n\t" /* BRGBRGBR 1 */\
\
			"psrlq $32, %%mm4		\n\t" /* 000000RG 2.5 */\
			"movq %%mm3, %%mm5		\n\t" /* 0RGB0RGB 3 */\
			"psrlq $8, %%mm3		\n\t" /* 00RGB0RG 3 */\
			"pand bm00000111, %%mm5		\n\t" /* 00000RGB 3 */\
			"pand bm11111000, %%mm3		\n\t" /* 00RGB000 3.5 */\
			"por %%mm5, %%mm3		\n\t" /* 00RGBRGB 3 */\
			"psllq $16, %%mm3		\n\t" /* RGBRGB00 3 */\
			"por %%mm4, %%mm3		\n\t" /* RGBRGBRG 2.5 */\
\
			MOVNTQ(%%mm0, (%%ebx))\
			MOVNTQ(%%mm2, 8(%%ebx))\
			MOVNTQ(%%mm3, 16(%%ebx))\
			"addl $24, %%ebx		\n\t"\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#define WRITEBGR24MMX \
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */\
			"movq %%mm2, %%mm1		\n\t" /* B */\
			"movq %%mm5, %%mm6		\n\t" /* R */\
			"punpcklbw %%mm4, %%mm2		\n\t" /* GBGBGBGB 0 */\
			"punpcklbw %%mm7, %%mm5		\n\t" /* 0R0R0R0R 0 */\
			"punpckhbw %%mm4, %%mm1		\n\t" /* GBGBGBGB 2 */\
			"punpckhbw %%mm7, %%mm6		\n\t" /* 0R0R0R0R 2 */\
			"movq %%mm2, %%mm0		\n\t" /* GBGBGBGB 0 */\
			"movq %%mm1, %%mm3		\n\t" /* GBGBGBGB 2 */\
			"punpcklwd %%mm5, %%mm0		\n\t" /* 0RGB0RGB 0 */\
			"punpckhwd %%mm5, %%mm2		\n\t" /* 0RGB0RGB 1 */\
			"punpcklwd %%mm6, %%mm1		\n\t" /* 0RGB0RGB 2 */\
			"punpckhwd %%mm6, %%mm3		\n\t" /* 0RGB0RGB 3 */\
\
			"movq %%mm0, %%mm4		\n\t" /* 0RGB0RGB 0 */\
			"movq %%mm2, %%mm6		\n\t" /* 0RGB0RGB 1 */\
			"movq %%mm1, %%mm5		\n\t" /* 0RGB0RGB 2 */\
			"movq %%mm3, %%mm7		\n\t" /* 0RGB0RGB 3 */\
\
			"psllq $40, %%mm0		\n\t" /* RGB00000 0 */\
			"psllq $40, %%mm2		\n\t" /* RGB00000 1 */\
			"psllq $40, %%mm1		\n\t" /* RGB00000 2 */\
			"psllq $40, %%mm3		\n\t" /* RGB00000 3 */\
\
			"punpckhdq %%mm4, %%mm0		\n\t" /* 0RGBRGB0 0 */\
			"punpckhdq %%mm6, %%mm2		\n\t" /* 0RGBRGB0 1 */\
			"punpckhdq %%mm5, %%mm1		\n\t" /* 0RGBRGB0 2 */\
			"punpckhdq %%mm7, %%mm3		\n\t" /* 0RGBRGB0 3 */\
\
			"psrlq $8, %%mm0		\n\t" /* 00RGBRGB 0 */\
			"movq %%mm2, %%mm6		\n\t" /* 0RGBRGB0 1 */\
			"psllq $40, %%mm2		\n\t" /* GB000000 1 */\
			"por %%mm2, %%mm0		\n\t" /* GBRGBRGB 0 */\
			MOVNTQ(%%mm0, (%%ebx))\
\
			"psrlq $24, %%mm6		\n\t" /* 0000RGBR 1 */\
			"movq %%mm1, %%mm5		\n\t" /* 0RGBRGB0 2 */\
			"psllq $24, %%mm1		\n\t" /* BRGB0000 2 */\
			"por %%mm1, %%mm6		\n\t" /* BRGBRGBR 1 */\
			MOVNTQ(%%mm6, 8(%%ebx))\
\
			"psrlq $40, %%mm5		\n\t" /* 000000RG 2 */\
			"psllq $8, %%mm3		\n\t" /* RGBRGB00 3 */\
			"por %%mm3, %%mm5		\n\t" /* RGBRGBRG 2 */\
			MOVNTQ(%%mm5, 16(%%ebx))\
\
			"addl $24, %%ebx		\n\t"\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#define WRITEBGR24MMX2 \
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */\
			"movq M24A, %%mm0		\n\t"\
			"movq M24C, %%mm7		\n\t"\
			"pshufw $0x50, %%mm2, %%mm1	\n\t" /* B3 B2 B3 B2  B1 B0 B1 B0 */\
			"pshufw $0x50, %%mm4, %%mm3	\n\t" /* G3 G2 G3 G2  G1 G0 G1 G0 */\
			"pshufw $0x00, %%mm5, %%mm6	\n\t" /* R1 R0 R1 R0  R1 R0 R1 R0 */\
\
			"pand %%mm0, %%mm1		\n\t" /*    B2        B1       B0 */\
			"pand %%mm0, %%mm3		\n\t" /*    G2        G1       G0 */\
			"pand %%mm7, %%mm6		\n\t" /*       R1        R0       */\
\
			"psllq $8, %%mm3		\n\t" /* G2        G1       G0    */\
			"por %%mm1, %%mm6		\n\t"\
			"por %%mm3, %%mm6		\n\t"\
			MOVNTQ(%%mm6, (%%ebx))\
\
			"psrlq $8, %%mm4		\n\t" /* 00 G7 G6 G5  G4 G3 G2 G1 */\
			"pshufw $0xA5, %%mm2, %%mm1	\n\t" /* B5 B4 B5 B4  B3 B2 B3 B2 */\
			"pshufw $0x55, %%mm4, %%mm3	\n\t" /* G4 G3 G4 G3  G4 G3 G4 G3 */\
			"pshufw $0xA5, %%mm5, %%mm6	\n\t" /* R5 R4 R5 R4  R3 R2 R3 R2 */\
\
			"pand M24B, %%mm1		\n\t" /* B5       B4        B3    */\
			"pand %%mm7, %%mm3		\n\t" /*       G4        G3       */\
			"pand %%mm0, %%mm6		\n\t" /*    R4        R3       R2 */\
\
			"por %%mm1, %%mm3		\n\t" /* B5    G4 B4     G3 B3    */\
			"por %%mm3, %%mm6		\n\t"\
			MOVNTQ(%%mm6, 8(%%ebx))\
\
			"pshufw $0xFF, %%mm2, %%mm1	\n\t" /* B7 B6 B7 B6  B7 B6 B6 B7 */\
			"pshufw $0xFA, %%mm4, %%mm3	\n\t" /* 00 G7 00 G7  G6 G5 G6 G5 */\
			"pshufw $0xFA, %%mm5, %%mm6	\n\t" /* R7 R6 R7 R6  R5 R4 R5 R4 */\
\
			"pand %%mm7, %%mm1		\n\t" /*       B7        B6       */\
			"pand %%mm0, %%mm3		\n\t" /*    G7        G6       G5 */\
			"pand M24B, %%mm6		\n\t" /* R7       R6        R5    */\
\
			"por %%mm1, %%mm3		\n\t"\
			"por %%mm3, %%mm6		\n\t"\
			MOVNTQ(%%mm6, 16(%%ebx))\
\
			"addl $24, %%ebx		\n\t"\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#ifdef HAVE_MMX2
#undef WRITEBGR24
#define WRITEBGR24 WRITEBGR24MMX2
#else
#undef WRITEBGR24
#define WRITEBGR24 WRITEBGR24MMX
#endif

static inline void RENAME(yuv2yuvX)(int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
				    int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
				    uint8_t *dest, uint8_t *uDest, uint8_t *vDest, int dstW,
				    int16_t * lumMmxFilter, int16_t * chrMmxFilter)
{
#ifdef HAVE_MMX
	if(uDest != NULL)
	{
		asm volatile(
				YSCALEYUV2YV12X(0)
				:: "m" (-chrFilterSize), "r" (chrSrc+chrFilterSize),
				"r" (chrMmxFilter+chrFilterSize*4), "r" (uDest), "m" (dstW>>1)
				: "%eax", "%edx", "%esi"
			);

		asm volatile(
				YSCALEYUV2YV12X(4096)
				:: "m" (-chrFilterSize), "r" (chrSrc+chrFilterSize),
				"r" (chrMmxFilter+chrFilterSize*4), "r" (vDest), "m" (dstW>>1)
				: "%eax", "%edx", "%esi"
			);
	}

	asm volatile(
			YSCALEYUV2YV12X(0)
			:: "m" (-lumFilterSize), "r" (lumSrc+lumFilterSize),
			   "r" (lumMmxFilter+lumFilterSize*4), "r" (dest), "m" (dstW)
			: "%eax", "%edx", "%esi"
		);
#else
yuv2yuvXinC(lumFilter, lumSrc, lumFilterSize,
	    chrFilter, chrSrc, chrFilterSize,
	    dest, uDest, vDest, dstW);
#endif
}

static inline void RENAME(yuv2yuv1)(int16_t *lumSrc, int16_t *chrSrc,
				    uint8_t *dest, uint8_t *uDest, uint8_t *vDest, int dstW)
{
#ifdef HAVE_MMX
	if(uDest != NULL)
	{
		asm volatile(
				YSCALEYUV2YV121
				:: "r" (chrSrc + (dstW>>1)), "r" (uDest + (dstW>>1)),
				"g" (-(dstW>>1))
				: "%eax"
			);

		asm volatile(
				YSCALEYUV2YV121
				:: "r" (chrSrc + 2048 + (dstW>>1)), "r" (vDest + (dstW>>1)),
				"g" (-(dstW>>1))
				: "%eax"
			);
	}

	asm volatile(
		YSCALEYUV2YV121
		:: "r" (lumSrc + dstW), "r" (dest + dstW),
		"g" (-dstW)
		: "%eax"
	);
#else
	//FIXME Optimize (just quickly writen not opti..)
	//FIXME replace MINMAX with LUTs
	int i;
	for(i=0; i<dstW; i++)
	{
		int val= lumSrc[i]>>7;

		dest[i]= MIN(MAX(val>>19, 0), 255);
	}

	if(uDest != NULL)
		for(i=0; i<(dstW>>1); i++)
		{
			int u=chrSrc[i]>>7;
			int v=chrSrc[i + 2048]>>7;

			uDest[i]= MIN(MAX(u>>19, 0), 255);
			vDest[i]= MIN(MAX(v>>19, 0), 255);
		}
#endif
}


/**
 * vertical scale YV12 to RGB
 */
static inline void RENAME(yuv2rgbX)(int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
				    int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
			    uint8_t *dest, int dstW, int dstbpp, int16_t * lumMmxFilter, int16_t * chrMmxFilter)
{
	if(fullUVIpol)
	{
//FIXME
	}//FULL_UV_IPOL
	else
	{
#ifdef HAVE_MMX
		if(dstbpp == 32) //FIXME untested
		{
			asm volatile(
				YSCALEYUV2RGBX
				WRITEBGR32

			:: "m" (-lumFilterSize), "m" (-chrFilterSize),
			   "m" (lumMmxFilter+lumFilterSize*4), "m" (chrMmxFilter+chrFilterSize*4),
			   "r" (dest), "m" (dstW),
			   "m" (lumSrc+lumFilterSize), "m" (chrSrc+chrFilterSize)
			: "%eax", "%ebx", "%ecx", "%edx", "%esi"
			);
		}
		else if(dstbpp==24) //FIXME untested
		{
			asm volatile(
				YSCALEYUV2RGBX
				"leal (%%eax, %%eax, 2), %%ebx	\n\t" //FIXME optimize
				"addl %4, %%ebx			\n\t"
				WRITEBGR24

			:: "m" (-lumFilterSize), "m" (-chrFilterSize),
			   "m" (lumMmxFilter+lumFilterSize*4), "m" (chrMmxFilter+chrFilterSize*4),
			   "r" (dest), "m" (dstW),
			   "m" (lumSrc+lumFilterSize), "m" (chrSrc+chrFilterSize)
			: "%eax", "%ebx", "%ecx", "%edx", "%esi"
			);
		}
		else if(dstbpp==15)
		{
			asm volatile(
				YSCALEYUV2RGBX
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g5Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif

				WRITEBGR15

			:: "m" (-lumFilterSize), "m" (-chrFilterSize),
			   "m" (lumMmxFilter+lumFilterSize*4), "m" (chrMmxFilter+chrFilterSize*4),
			   "r" (dest), "m" (dstW),
			   "m" (lumSrc+lumFilterSize), "m" (chrSrc+chrFilterSize)
			: "%eax", "%ebx", "%ecx", "%edx", "%esi"
			);
		}
		else if(dstbpp==16)
		{
			asm volatile(
				YSCALEYUV2RGBX
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g6Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif

				WRITEBGR16

			:: "m" (-lumFilterSize), "m" (-chrFilterSize),
			   "m" (lumMmxFilter+lumFilterSize*4), "m" (chrMmxFilter+chrFilterSize*4),
			   "r" (dest), "m" (dstW),
			   "m" (lumSrc+lumFilterSize), "m" (chrSrc+chrFilterSize)
			: "%eax", "%ebx", "%ecx", "%edx", "%esi"
			);
		}
#else
yuv2rgbXinC(lumFilter, lumSrc, lumFilterSize,
	    chrFilter, chrSrc, chrFilterSize,
	    dest, dstW, dstbpp);

#endif
	} //!FULL_UV_IPOL
}


/**
 * vertical bilinear scale YV12 to RGB
 */
static inline void RENAME(yuv2rgb2)(uint16_t *buf0, uint16_t *buf1, uint16_t *uvbuf0, uint16_t *uvbuf1,
			    uint8_t *dest, int dstW, int yalpha, int uvalpha, int dstbpp)
{
	int yalpha1=yalpha^4095;
	int uvalpha1=uvalpha^4095;

	if(fullUVIpol)
	{

#ifdef HAVE_MMX
		if(dstbpp == 32)
		{
			asm volatile(


FULL_YSCALEYUV2RGB
			"punpcklbw %%mm1, %%mm3		\n\t" // BGBGBGBG
			"punpcklbw %%mm7, %%mm0		\n\t" // R0R0R0R0

			"movq %%mm3, %%mm1		\n\t"
			"punpcklwd %%mm0, %%mm3		\n\t" // BGR0BGR0
			"punpckhwd %%mm0, %%mm1		\n\t" // BGR0BGR0

			MOVNTQ(%%mm3, (%4, %%eax, 4))
			MOVNTQ(%%mm1, 8(%4, %%eax, 4))

			"addl $4, %%eax			\n\t"
			"cmpl %5, %%eax			\n\t"
			" jb 1b				\n\t"


			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==24)
		{
			asm volatile(

FULL_YSCALEYUV2RGB

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

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "m" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax", "%ebx"
			);
		}
		else if(dstbpp==15)
		{
			asm volatile(

FULL_YSCALEYUV2RGB
#ifdef DITHER1XBPP
			"paddusb g5Dither, %%mm1	\n\t"
			"paddusb r5Dither, %%mm0	\n\t"
			"paddusb b5Dither, %%mm3	\n\t"
#endif
			"punpcklbw %%mm7, %%mm1		\n\t" // 0G0G0G0G
			"punpcklbw %%mm7, %%mm3		\n\t" // 0B0B0B0B
			"punpcklbw %%mm7, %%mm0		\n\t" // 0R0R0R0R

			"psrlw $3, %%mm3		\n\t"
			"psllw $2, %%mm1		\n\t"
			"psllw $7, %%mm0		\n\t"
			"pand g15Mask, %%mm1		\n\t"
			"pand r15Mask, %%mm0		\n\t"

			"por %%mm3, %%mm1		\n\t"
			"por %%mm1, %%mm0		\n\t"

			MOVNTQ(%%mm0, (%4, %%eax, 2))

			"addl $4, %%eax			\n\t"
			"cmpl %5, %%eax			\n\t"
			" jb 1b				\n\t"

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==16)
		{
			asm volatile(

FULL_YSCALEYUV2RGB
#ifdef DITHER1XBPP
			"paddusb g6Dither, %%mm1	\n\t"
			"paddusb r5Dither, %%mm0	\n\t"
			"paddusb b5Dither, %%mm3	\n\t"
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

			MOVNTQ(%%mm0, (%4, %%eax, 2))

			"addl $4, %%eax			\n\t"
			"cmpl %5, %%eax			\n\t"
			" jb 1b				\n\t"

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
#else
		if(dstbpp==32 || dstbpp==24)
		{
			int i;
			for(i=0;i<dstW;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>19);
				int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>19);
				dest[0]=clip_table[((Y + yuvtab_40cf[U]) >>13)];
				dest[1]=clip_table[((Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13)];
				dest[2]=clip_table[((Y + yuvtab_3343[V]) >>13)];
				dest+=dstbpp>>3;
			}
		}
		else if(dstbpp==16)
		{
			int i;
			for(i=0;i<dstW;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>19);
				int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>19);

				((uint16_t*)dest)[i] =
					clip_table16b[(Y + yuvtab_40cf[U]) >>13] |
					clip_table16g[(Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13] |
					clip_table16r[(Y + yuvtab_3343[V]) >>13];
			}
		}
		else if(dstbpp==15)
		{
			int i;
			for(i=0;i<dstW;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>19);
				int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>19);

				((uint16_t*)dest)[i] =
					clip_table15b[(Y + yuvtab_40cf[U]) >>13] |
					clip_table15g[(Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13] |
					clip_table15r[(Y + yuvtab_3343[V]) >>13];
			}
		}
#endif
	}//FULL_UV_IPOL
	else
	{
#ifdef HAVE_MMX
		if(dstbpp == 32)
		{
			asm volatile(
				YSCALEYUV2RGB
				WRITEBGR32

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==24)
		{
			asm volatile(
				"movl %4, %%ebx			\n\t"
				YSCALEYUV2RGB
				WRITEBGR24

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "m" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax", "%ebx"
			);
		}
		else if(dstbpp==15)
		{
			asm volatile(
				YSCALEYUV2RGB
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g5Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif

				WRITEBGR15

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==16)
		{
			asm volatile(
				YSCALEYUV2RGB
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g6Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif

				WRITEBGR16

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
#else
		if(dstbpp==32)
		{
			int i;
			for(i=0; i<dstW-1; i+=2){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y1=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int Y2=yuvtab_2568[((buf0[i+1]*yalpha1+buf1[i+1]*yalpha)>>19)];
				int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
				int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

				int Cb= yuvtab_40cf[U];
				int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
				int Cr= yuvtab_3343[V];

				dest[4*i+0]=clip_table[((Y1 + Cb) >>13)];
				dest[4*i+1]=clip_table[((Y1 + Cg) >>13)];
				dest[4*i+2]=clip_table[((Y1 + Cr) >>13)];

				dest[4*i+4]=clip_table[((Y2 + Cb) >>13)];
				dest[4*i+5]=clip_table[((Y2 + Cg) >>13)];
				dest[4*i+6]=clip_table[((Y2 + Cr) >>13)];
			}
		}
		else if(dstbpp==24)
		{
			int i;
			for(i=0; i<dstW-1; i+=2){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y1=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int Y2=yuvtab_2568[((buf0[i+1]*yalpha1+buf1[i+1]*yalpha)>>19)];
				int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
				int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

				int Cb= yuvtab_40cf[U];
				int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
				int Cr= yuvtab_3343[V];

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
			for(i=0; i<dstW-1; i+=2){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y1=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int Y2=yuvtab_2568[((buf0[i+1]*yalpha1+buf1[i+1]*yalpha)>>19)];
				int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
				int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

				int Cb= yuvtab_40cf[U];
				int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
				int Cr= yuvtab_3343[V];

				((uint16_t*)dest)[i] =
					clip_table16b[(Y1 + Cb) >>13] |
					clip_table16g[(Y1 + Cg) >>13] |
					clip_table16r[(Y1 + Cr) >>13];

				((uint16_t*)dest)[i+1] =
					clip_table16b[(Y2 + Cb) >>13] |
					clip_table16g[(Y2 + Cg) >>13] |
					clip_table16r[(Y2 + Cr) >>13];
			}
		}
		else if(dstbpp==15)
		{
			int i;
			for(i=0; i<dstW-1; i+=2){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y1=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int Y2=yuvtab_2568[((buf0[i+1]*yalpha1+buf1[i+1]*yalpha)>>19)];
				int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
				int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

				int Cb= yuvtab_40cf[U];
				int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
				int Cr= yuvtab_3343[V];

				((uint16_t*)dest)[i] =
					clip_table15b[(Y1 + Cb) >>13] |
					clip_table15g[(Y1 + Cg) >>13] |
					clip_table15r[(Y1 + Cr) >>13];

				((uint16_t*)dest)[i+1] =
					clip_table15b[(Y2 + Cb) >>13] |
					clip_table15g[(Y2 + Cg) >>13] |
					clip_table15r[(Y2 + Cr) >>13];
			}
		}
#endif
	} //!FULL_UV_IPOL
}

/**
 * YV12 to RGB without scaling or interpolating
 */
static inline void RENAME(yuv2rgb1)(uint16_t *buf0, uint16_t *uvbuf0, uint16_t *uvbuf1,
			    uint8_t *dest, int dstW, int uvalpha, int dstbpp)
{
	int uvalpha1=uvalpha^4095;
	const int yalpha1=0;

	if(fullUVIpol || allwaysIpol)
	{
		RENAME(yuv2rgb2)(buf0, buf0, uvbuf0, uvbuf1, dest, dstW, 0, uvalpha, dstbpp);
		return;
	}

#ifdef HAVE_MMX
	if( uvalpha < 2048 ) // note this is not correct (shifts chrominance by 0.5 pixels) but its a bit faster
	{
		if(dstbpp == 32)
		{
			asm volatile(
				YSCALEYUV2RGB1
				WRITEBGR32
			:: "r" (buf0), "r" (buf0), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==24)
		{
			asm volatile(
				"movl %4, %%ebx			\n\t"
				YSCALEYUV2RGB1
				WRITEBGR24
			:: "r" (buf0), "r" (buf0), "r" (uvbuf0), "r" (uvbuf1), "m" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax", "%ebx"
			);
		}
		else if(dstbpp==15)
		{
			asm volatile(
				YSCALEYUV2RGB1
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g5Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif
				WRITEBGR15
			:: "r" (buf0), "r" (buf0), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==16)
		{
			asm volatile(
				YSCALEYUV2RGB1
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g6Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif

				WRITEBGR16
			:: "r" (buf0), "r" (buf0), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
	}
	else
	{
		if(dstbpp == 32)
		{
			asm volatile(
				YSCALEYUV2RGB1b
				WRITEBGR32
			:: "r" (buf0), "r" (buf0), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==24)
		{
			asm volatile(
				"movl %4, %%ebx			\n\t"
				YSCALEYUV2RGB1b
				WRITEBGR24
			:: "r" (buf0), "r" (buf0), "r" (uvbuf0), "r" (uvbuf1), "m" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax", "%ebx"
			);
		}
		else if(dstbpp==15)
		{
			asm volatile(
				YSCALEYUV2RGB1b
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g5Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif
				WRITEBGR15
			:: "r" (buf0), "r" (buf0), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==16)
		{
			asm volatile(
				YSCALEYUV2RGB1b
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g6Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif

				WRITEBGR16
			:: "r" (buf0), "r" (buf0), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
	}
#else
//FIXME write 2 versions (for even & odd lines)

	if(dstbpp==32)
	{
		int i;
		for(i=0; i<dstW-1; i+=2){
			// vertical linear interpolation && yuv2rgb in a single step:
			int Y1=yuvtab_2568[buf0[i]>>7];
			int Y2=yuvtab_2568[buf0[i+1]>>7];
			int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
			int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

			int Cb= yuvtab_40cf[U];
			int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
			int Cr= yuvtab_3343[V];

			dest[4*i+0]=clip_table[((Y1 + Cb) >>13)];
			dest[4*i+1]=clip_table[((Y1 + Cg) >>13)];
			dest[4*i+2]=clip_table[((Y1 + Cr) >>13)];

			dest[4*i+4]=clip_table[((Y2 + Cb) >>13)];
			dest[4*i+5]=clip_table[((Y2 + Cg) >>13)];
			dest[4*i+6]=clip_table[((Y2 + Cr) >>13)];
		}
	}
	else if(dstbpp==24)
	{
		int i;
		for(i=0; i<dstW-1; i+=2){
			// vertical linear interpolation && yuv2rgb in a single step:
			int Y1=yuvtab_2568[buf0[i]>>7];
			int Y2=yuvtab_2568[buf0[i+1]>>7];
			int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
			int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

			int Cb= yuvtab_40cf[U];
			int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
			int Cr= yuvtab_3343[V];

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
		for(i=0; i<dstW-1; i+=2){
			// vertical linear interpolation && yuv2rgb in a single step:
			int Y1=yuvtab_2568[buf0[i]>>7];
			int Y2=yuvtab_2568[buf0[i+1]>>7];
			int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
			int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

			int Cb= yuvtab_40cf[U];
			int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
			int Cr= yuvtab_3343[V];

			((uint16_t*)dest)[i] =
				clip_table16b[(Y1 + Cb) >>13] |
				clip_table16g[(Y1 + Cg) >>13] |
				clip_table16r[(Y1 + Cr) >>13];

			((uint16_t*)dest)[i+1] =
				clip_table16b[(Y2 + Cb) >>13] |
				clip_table16g[(Y2 + Cg) >>13] |
				clip_table16r[(Y2 + Cr) >>13];
		}
	}
	else if(dstbpp==15)
	{
		int i;
		for(i=0; i<dstW-1; i+=2){
			// vertical linear interpolation && yuv2rgb in a single step:
			int Y1=yuvtab_2568[buf0[i]>>7];
			int Y2=yuvtab_2568[buf0[i+1]>>7];
			int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
			int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

			int Cb= yuvtab_40cf[U];
			int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
			int Cr= yuvtab_3343[V];

			((uint16_t*)dest)[i] =
				clip_table15b[(Y1 + Cb) >>13] |
				clip_table15g[(Y1 + Cg) >>13] |
				clip_table15r[(Y1 + Cr) >>13];

			((uint16_t*)dest)[i+1] =
				clip_table15b[(Y2 + Cb) >>13] |
				clip_table15g[(Y2 + Cg) >>13] |
				clip_table15r[(Y2 + Cr) >>13];
		}
	}
#endif
}

// Bilinear / Bicubic scaling
static inline void RENAME(hScale)(int16_t *dst, int dstW, uint8_t *src, int srcW, int xInc,
				  int16_t *filter, int16_t *filterPos, int filterSize)
{
#ifdef HAVE_MMX
	if(filterSize==4) // allways true for upscaling, sometimes for down too
	{
		int counter= -2*dstW;
		filter-= counter*2;
		filterPos-= counter/2;
		dst-= counter/2;
		asm volatile(
			"pxor %%mm7, %%mm7		\n\t"
			"movq w02, %%mm6		\n\t"
			"pushl %%ebp			\n\t" // we use 7 regs here ...
			"movl %%eax, %%ebp		\n\t"
			".balign 16			\n\t"
			"1:				\n\t"
			"movzwl (%2, %%ebp), %%eax	\n\t"
			"movzwl 2(%2, %%ebp), %%ebx	\n\t"
			"movq (%1, %%ebp, 4), %%mm1	\n\t"
			"movq 8(%1, %%ebp, 4), %%mm3	\n\t"
			"movd (%3, %%eax), %%mm0	\n\t"
			"movd (%3, %%ebx), %%mm2	\n\t"
			"punpcklbw %%mm7, %%mm0		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"pmaddwd %%mm1, %%mm0		\n\t"
			"pmaddwd %%mm2, %%mm3		\n\t"
			"psrad $8, %%mm0		\n\t"
			"psrad $8, %%mm3		\n\t"
			"packssdw %%mm3, %%mm0		\n\t"
			"pmaddwd %%mm6, %%mm0		\n\t"
			"packssdw %%mm0, %%mm0		\n\t"
			"movd %%mm0, (%4, %%ebp)	\n\t"
			"addl $4, %%ebp			\n\t"
			" jnc 1b			\n\t"

			"popl %%ebp			\n\t"
			: "+a" (counter)
			: "c" (filter), "d" (filterPos), "S" (src), "D" (dst)
			: "%ebx"
		);
	}
	else if(filterSize==8)
	{
		int counter= -2*dstW;
		filter-= counter*4;
		filterPos-= counter/2;
		dst-= counter/2;
		asm volatile(
			"pxor %%mm7, %%mm7		\n\t"
			"movq w02, %%mm6		\n\t"
			"pushl %%ebp			\n\t" // we use 7 regs here ...
			"movl %%eax, %%ebp		\n\t"
			".balign 16			\n\t"
			"1:				\n\t"
			"movzwl (%2, %%ebp), %%eax	\n\t"
			"movzwl 2(%2, %%ebp), %%ebx	\n\t"
			"movq (%1, %%ebp, 8), %%mm1	\n\t"
			"movq 16(%1, %%ebp, 8), %%mm3	\n\t"
			"movd (%3, %%eax), %%mm0	\n\t"
			"movd (%3, %%ebx), %%mm2	\n\t"
			"punpcklbw %%mm7, %%mm0		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"pmaddwd %%mm1, %%mm0		\n\t"
			"pmaddwd %%mm2, %%mm3		\n\t"

			"movq 8(%1, %%ebp, 8), %%mm1	\n\t"
			"movq 24(%1, %%ebp, 8), %%mm5	\n\t"
			"movd 4(%3, %%eax), %%mm4	\n\t"
			"movd 4(%3, %%ebx), %%mm2	\n\t"
			"punpcklbw %%mm7, %%mm4		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"pmaddwd %%mm1, %%mm4		\n\t"
			"pmaddwd %%mm2, %%mm5		\n\t"
			"paddd %%mm4, %%mm0		\n\t"
			"paddd %%mm5, %%mm3		\n\t"
						
			"psrad $8, %%mm0		\n\t"
			"psrad $8, %%mm3		\n\t"
			"packssdw %%mm3, %%mm0		\n\t"
			"pmaddwd %%mm6, %%mm0		\n\t"
			"packssdw %%mm0, %%mm0		\n\t"
			"movd %%mm0, (%4, %%ebp)	\n\t"
			"addl $4, %%ebp			\n\t"
			" jnc 1b			\n\t"

			"popl %%ebp			\n\t"
			: "+a" (counter)
			: "c" (filter), "d" (filterPos), "S" (src), "D" (dst)
			: "%ebx"
		);
	}
	else
	{
		int counter= -2*dstW;
//		filter-= counter*filterSize/2;
		filterPos-= counter/2;
		dst-= counter/2;
		asm volatile(
			"pxor %%mm7, %%mm7		\n\t"
			"movq w02, %%mm6		\n\t"
			".balign 16			\n\t"
			"1:				\n\t"
			"movl %2, %%ecx			\n\t"
			"movzwl (%%ecx, %0), %%eax	\n\t"
			"movzwl 2(%%ecx, %0), %%ebx	\n\t"
			"movl %5, %%ecx			\n\t"
			"pxor %%mm4, %%mm4		\n\t"
			"pxor %%mm5, %%mm5		\n\t"
			"2:				\n\t"
			"movq (%1), %%mm1		\n\t"
			"movq (%1, %6), %%mm3		\n\t"
			"movd (%%ecx, %%eax), %%mm0	\n\t"
			"movd (%%ecx, %%ebx), %%mm2	\n\t"
			"punpcklbw %%mm7, %%mm0		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"pmaddwd %%mm1, %%mm0		\n\t"
			"pmaddwd %%mm2, %%mm3		\n\t"
			"paddd %%mm3, %%mm5		\n\t"
			"paddd %%mm0, %%mm4		\n\t"
			"addl $8, %1			\n\t"
			"addl $4, %%ecx			\n\t"
			"cmpl %4, %%ecx			\n\t"
			" jb 2b				\n\t"
			"addl %6, %1			\n\t"
			"psrad $8, %%mm4		\n\t"
			"psrad $8, %%mm5		\n\t"
			"packssdw %%mm5, %%mm4		\n\t"
			"pmaddwd %%mm6, %%mm4		\n\t"
			"packssdw %%mm4, %%mm4		\n\t"
			"movl %3, %%eax			\n\t"
			"movd %%mm4, (%%eax, %0)	\n\t"
			"addl $4, %0			\n\t"
			" jnc 1b			\n\t"

			: "+r" (counter), "+r" (filter)
			: "m" (filterPos), "m" (dst), "m"(src+filterSize),
			  "m" (src), "r" (filterSize*2)
			: "%ebx", "%eax", "%ecx"
		);
	}
#else
	int i;
	for(i=0; i<dstW; i++)
	{
		int j;
		int srcPos= filterPos[i];
		int val=0;
//		printf("filterPos: %d\n", filterPos[i]);
		for(j=0; j<filterSize; j++)
		{
//			printf("filter: %d, src: %d\n", filter[i], src[srcPos + j]);
			val += ((int)src[srcPos + j])*filter[filterSize*i + j];
		}
//		filter += hFilterSize;
		dst[i] = MIN(MAX(0, val>>7), (1<<15)-1); // the cubic equation does overflow ...
//		dst[i] = val>>7;
	}
#endif
}
      // *** horizontal scale Y line to temp buffer
static inline void RENAME(hyscale)(uint16_t *dst, int dstWidth, uint8_t *src, int srcW, int xInc)
{
#ifdef HAVE_MMX
	// use the new MMX scaler if th mmx2 cant be used (its faster than the x86asm one)
    if(sws_flags != SWS_FAST_BILINEAR || (!canMMX2BeUsed))
#else
    if(sws_flags != SWS_FAST_BILINEAR)
#endif
    {
    	RENAME(hScale)(dst, dstWidth, src, srcW, xInc, hLumFilter, hLumFilterPos, hLumFilterSize);
    }
    else // Fast Bilinear upscale / crap downscale
    {
#ifdef ARCH_X86
#ifdef HAVE_MMX2
	int i;
	if(canMMX2BeUsed)
	{
		asm volatile(
			"pxor %%mm7, %%mm7		\n\t"
			"pxor %%mm2, %%mm2		\n\t" // 2*xalpha
			"movd %5, %%mm6			\n\t" // xInc&0xFFFF
			"punpcklwd %%mm6, %%mm6		\n\t"
			"punpcklwd %%mm6, %%mm6		\n\t"
			"movq %%mm6, %%mm2		\n\t"
			"psllq $16, %%mm2		\n\t"
			"paddw %%mm6, %%mm2		\n\t"
			"psllq $16, %%mm2		\n\t"
			"paddw %%mm6, %%mm2		\n\t"
			"psllq $16, %%mm2		\n\t" //0,t,2t,3t		t=xInc&0xFF
			"movq %%mm2, temp0		\n\t"
			"movd %4, %%mm6			\n\t" //(xInc*4)&0xFFFF
			"punpcklwd %%mm6, %%mm6		\n\t"
			"punpcklwd %%mm6, %%mm6		\n\t"
			"xorl %%eax, %%eax		\n\t" // i
			"movl %0, %%esi			\n\t" // src
			"movl %1, %%edi			\n\t" // buf1
			"movl %3, %%edx			\n\t" // (xInc*4)>>16
			"xorl %%ecx, %%ecx		\n\t"
			"xorl %%ebx, %%ebx		\n\t"
			"movw %4, %%bx			\n\t" // (xInc*4)&0xFFFF

#define FUNNY_Y_CODE \
			PREFETCH" 1024(%%esi)		\n\t"\
			PREFETCH" 1056(%%esi)		\n\t"\
			PREFETCH" 1088(%%esi)		\n\t"\
			"call funnyYCode		\n\t"\
			"movq temp0, %%mm2		\n\t"\
			"xorl %%ecx, %%ecx		\n\t"

FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE

			:: "m" (src), "m" (dst), "m" (dstWidth), "m" ((xInc*4)>>16),
			"m" ((xInc*4)&0xFFFF), "m" (xInc&0xFFFF)
			: "%eax", "%ebx", "%ecx", "%edx", "%esi", "%edi"
		);
		for(i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--) dst[i] = src[srcW-1]*128;
	}
	else
	{
#endif
	//NO MMX just normal asm ...
	asm volatile(
		"xorl %%eax, %%eax		\n\t" // i
		"xorl %%ebx, %%ebx		\n\t" // xx
		"xorl %%ecx, %%ecx		\n\t" // 2*xalpha
		".balign 16			\n\t"
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
		"addw %4, %%cx			\n\t" //2*xalpha += xInc&0xFF
		"adcl %3, %%ebx			\n\t" //xx+= xInc>>8 + carry

		"movzbl (%0, %%ebx), %%edi	\n\t" //src[xx]
		"movzbl 1(%0, %%ebx), %%esi	\n\t" //src[xx+1]
		"subl %%edi, %%esi		\n\t" //src[xx+1] - src[xx]
		"imull %%ecx, %%esi		\n\t" //(src[xx+1] - src[xx])*2*xalpha
		"shll $16, %%edi		\n\t"
		"addl %%edi, %%esi		\n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
		"movl %1, %%edi			\n\t"
		"shrl $9, %%esi			\n\t"
		"movw %%si, 2(%%edi, %%eax, 2)	\n\t"
		"addw %4, %%cx			\n\t" //2*xalpha += xInc&0xFF
		"adcl %3, %%ebx			\n\t" //xx+= xInc>>8 + carry


		"addl $2, %%eax			\n\t"
		"cmpl %2, %%eax			\n\t"
		" jb 1b				\n\t"


		:: "r" (src), "m" (dst), "m" (dstWidth), "m" (xInc>>16), "m" (xInc&0xFFFF)
		: "%eax", "%ebx", "%ecx", "%edi", "%esi"
		);
#ifdef HAVE_MMX2
	} //if MMX2 cant be used
#endif
#else
	int i;
	unsigned int xpos=0;
	for(i=0;i<dstWidth;i++)
	{
		register unsigned int xx=xpos>>16;
		register unsigned int xalpha=(xpos&0xFFFF)>>9;
		dst[i]= (src[xx]<<7) + (src[xx+1] - src[xx])*xalpha;
		xpos+=xInc;
	}
#endif
    }
}

inline static void RENAME(hcscale)(uint16_t *dst, int dstWidth,
				uint8_t *src1, uint8_t *src2, int srcW, int xInc)
{
#ifdef HAVE_MMX
	// use the new MMX scaler if th mmx2 cant be used (its faster than the x86asm one)
    if(sws_flags != SWS_FAST_BILINEAR || (!canMMX2BeUsed))
#else
    if(sws_flags != SWS_FAST_BILINEAR)
#endif
    {
    	RENAME(hScale)(dst     , dstWidth, src1, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize);
    	RENAME(hScale)(dst+2048, dstWidth, src2, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize);
    }
    else // Fast Bilinear upscale / crap downscale
    {
#ifdef ARCH_X86
#ifdef HAVE_MMX2
	int i;
	if(canMMX2BeUsed)
	{
		asm volatile(
		"pxor %%mm7, %%mm7		\n\t"
		"pxor %%mm2, %%mm2		\n\t" // 2*xalpha
		"movd %5, %%mm6			\n\t" // xInc&0xFFFF
		"punpcklwd %%mm6, %%mm6		\n\t"
		"punpcklwd %%mm6, %%mm6		\n\t"
		"movq %%mm6, %%mm2		\n\t"
		"psllq $16, %%mm2		\n\t"
		"paddw %%mm6, %%mm2		\n\t"
		"psllq $16, %%mm2		\n\t"
		"paddw %%mm6, %%mm2		\n\t"
		"psllq $16, %%mm2		\n\t" //0,t,2t,3t		t=xInc&0xFFFF
		"movq %%mm2, temp0		\n\t"
		"movd %4, %%mm6			\n\t" //(xInc*4)&0xFFFF
		"punpcklwd %%mm6, %%mm6		\n\t"
		"punpcklwd %%mm6, %%mm6		\n\t"
		"xorl %%eax, %%eax		\n\t" // i
		"movl %0, %%esi			\n\t" // src
		"movl %1, %%edi			\n\t" // buf1
		"movl %3, %%edx			\n\t" // (xInc*4)>>16
		"xorl %%ecx, %%ecx		\n\t"
		"xorl %%ebx, %%ebx		\n\t"
		"movw %4, %%bx			\n\t" // (xInc*4)&0xFFFF

#define FUNNYUVCODE \
			PREFETCH" 1024(%%esi)		\n\t"\
			PREFETCH" 1056(%%esi)		\n\t"\
			PREFETCH" 1088(%%esi)		\n\t"\
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

		:: "m" (src1), "m" (dst), "m" (dstWidth), "m" ((xInc*4)>>16),
		  "m" ((xInc*4)&0xFFFF), "m" (xInc&0xFFFF), "m" (src2)
		: "%eax", "%ebx", "%ecx", "%edx", "%esi", "%edi"
	);
		for(i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--)
		{
//			printf("%d %d %d\n", dstWidth, i, srcW);
			dst[i] = src1[srcW-1]*128;
			dst[i+2048] = src2[srcW-1]*128;
		}
	}
	else
	{
#endif
	asm volatile(
		"xorl %%eax, %%eax		\n\t" // i
		"xorl %%ebx, %%ebx		\n\t" // xx
		"xorl %%ecx, %%ecx		\n\t" // 2*xalpha
		".balign 16			\n\t"
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

		"addw %4, %%cx			\n\t" //2*xalpha += xInc&0xFF
		"adcl %3, %%ebx			\n\t" //xx+= xInc>>8 + carry
		"addl $1, %%eax			\n\t"
		"cmpl %2, %%eax			\n\t"
		" jb 1b				\n\t"

		:: "m" (src1), "m" (dst), "m" (dstWidth), "m" (xInc>>16), "m" (xInc&0xFFFF),
		"r" (src2)
		: "%eax", "%ebx", "%ecx", "%edi", "%esi"
		);
#ifdef HAVE_MMX2
	} //if MMX2 cant be used
#endif
#else
	int i;
	unsigned int xpos=0;
	for(i=0;i<dstWidth;i++)
	{
		register unsigned int xx=xpos>>16;
		register unsigned int xalpha=(xpos&0xFFFF)>>9;
		dst[i]=(src1[xx]*(xalpha^127)+src1[xx+1]*xalpha);
		dst[i+2048]=(src2[xx]*(xalpha^127)+src2[xx+1]*xalpha);
/* slower
	  dst[i]= (src1[xx]<<7) + (src1[xx+1] - src1[xx])*xalpha;
	  dst[i+2048]=(src2[xx]<<7) + (src2[xx+1] - src2[xx])*xalpha;
*/
		xpos+=xInc;
	}
#endif
   }
}

static inline void RENAME(initFilter)(int16_t *dstFilter, int16_t *filterPos, int *filterSize, int xInc,
				      int srcW, int dstW, int filterAlign, int one)
{
	int i;
	double filter[8000];
#ifdef HAVE_MMX
	asm volatile("emms\n\t"::: "memory"); //FIXME this shouldnt be required but it IS (even for non mmx versions)
#endif

	if(ABS(xInc - 0x10000) <10) // unscaled
	{
		int i;
		*filterSize= (1 +(filterAlign-1)) & (~(filterAlign-1)); // 1 or 4 normaly
		for(i=0; i<dstW*(*filterSize); i++) filter[i]=0;

		for(i=0; i<dstW; i++)
		{
			filter[i*(*filterSize)]=1;
			filterPos[i]=i;
		}

	}
	else if(xInc <= (1<<16) || sws_flags==SWS_FAST_BILINEAR) // upscale
	{
		int i;
		int xDstInSrc;
		if(sws_flags==SWS_BICUBIC) *filterSize= 4;
		else			   *filterSize= 2;
//		printf("%d %d %d\n", filterSize, srcW, dstW);
		*filterSize= (*filterSize +(filterAlign-1)) & (~(filterAlign-1));

		xDstInSrc= xInc/2 - 0x8000;
		for(i=0; i<dstW; i++)
		{
			int xx= (xDstInSrc>>16) - (*filterSize>>1) + 1;
			int j;

			filterPos[i]= xx;
			if(sws_flags == SWS_BICUBIC)
			{
				double d= ABS(((xx+1)<<16) - xDstInSrc)/(double)(1<<16);
				double y1,y2,y3,y4;
				double A= -0.75;
					// Equation is from VirtualDub
				y1 = (        +     A*d -       2.0*A*d*d +       A*d*d*d);
				y2 = (+ 1.0             -     (A+3.0)*d*d + (A+2.0)*d*d*d);
				y3 = (        -     A*d + (2.0*A+3.0)*d*d - (A+2.0)*d*d*d);
				y4 = (                  +           A*d*d -       A*d*d*d);

//				printf("%d %d %d \n", coeff, (int)d, xDstInSrc);
				filter[i*(*filterSize) + 0]= y1;
				filter[i*(*filterSize) + 1]= y2;
				filter[i*(*filterSize) + 2]= y3;
				filter[i*(*filterSize) + 3]= y4;
//				printf("%1.3f %d, %d, %d, %d\n",d , y1, y2, y3, y4);
			}
			else
			{
				for(j=0; j<*filterSize; j++)
				{
					double d= ABS((xx<<16) - xDstInSrc)/(double)(1<<16);
					double coeff= 1.0 - d;
					if(coeff<0) coeff=0;
	//				printf("%d %d %d \n", coeff, (int)d, xDstInSrc);
					filter[i*(*filterSize) + j]= coeff;
					xx++;
				}
			}
			xDstInSrc+= xInc;
		}
	}
	else // downscale
	{
		int xDstInSrc;
		if(sws_flags==SWS_BICUBIC) *filterSize= (int)ceil(1 + 4.0*srcW / (double)dstW);
		else			   *filterSize= (int)ceil(1 + 2.0*srcW / (double)dstW);
//		printf("%d %d %d\n", *filterSize, srcW, dstW);
		*filterSize= (*filterSize +(filterAlign-1)) & (~(filterAlign-1));

		xDstInSrc= xInc/2 - 0x8000;
		for(i=0; i<dstW; i++)
		{
			int xx= (int)((double)xDstInSrc/(double)(1<<16) - ((*filterSize)-1)*0.5 + 0.5);
			int j;

			filterPos[i]= xx;
			for(j=0; j<*filterSize; j++)
			{
				double d= ABS((xx<<16) - xDstInSrc)/(double)xInc;
				double coeff;
				if(sws_flags == SWS_BICUBIC)
				{
					double A= -0.75;
//					d*=2;
					// Equation is from VirtualDub
					if(d<1.0)
						coeff = (1.0 - (A+3.0)*d*d + (A+2.0)*d*d*d);
					else if(d<2.0)
						coeff = (-4.0*A + 8.0*A*d - 5.0*A*d*d + A*d*d*d);
					else
						coeff=0.0;
				}
				else
				{
					coeff= 1.0 - d;
					if(coeff<0) coeff=0;
				}
//				if(filterAlign==1) printf("%d %d %d \n", coeff, (int)d, xDstInSrc);
				filter[i*(*filterSize) + j]= coeff;
				xx++;
			}
			xDstInSrc+= xInc;
		}
	}

	//fix borders
	for(i=0; i<dstW; i++)
	{
		int j;
		if(filterPos[i] < 0)
		{
			// Move filter coeffs left to compensate for filterPos
			for(j=1; j<*filterSize; j++)
			{
				int left= MAX(j + filterPos[i], 0);
				filter[i*(*filterSize) + left] += filter[i*(*filterSize) + j];
				filter[i*(*filterSize) + j]=0;
			}
			filterPos[i]= 0;
		}

		if(filterPos[i] + (*filterSize) > srcW)
		{
			int shift= filterPos[i] + (*filterSize) - srcW;
			// Move filter coeffs right to compensate for filterPos
			for(j=(*filterSize)-2; j>=0; j--)
			{
				int right= MIN(j + shift, (*filterSize)-1);
				filter[i*(*filterSize) +right] += filter[i*(*filterSize) +j];
				filter[i*(*filterSize) +j]=0;
			}
			filterPos[i]= srcW - (*filterSize);
		}
	}

	//FIXME try to align filterpos if possible / try to shift filterpos to put zeros at the end
	// and skip these than later

	//Normalize
	for(i=0; i<dstW; i++)
	{
		int j;
		double sum=0;
		double scale= one;
		for(j=0; j<*filterSize; j++)
		{
			sum+= filter[i*(*filterSize) + j];
		}
		scale/= sum;
		for(j=0; j<*filterSize; j++)
		{
			dstFilter[i*(*filterSize) + j]= (int)(filter[i*(*filterSize) + j]*scale);
		}
	}
}

#ifdef HAVE_MMX2
static void initMMX2HScaler(int dstW, int xInc, uint8_t *funnyCode)
{
	uint8_t *fragment;
	int imm8OfPShufW1;
	int imm8OfPShufW2;
	int fragmentLength;

	int xpos, i;

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
		"addw %%bx, %%cx		\n\t" //2*xalpha += (4*lumXInc)&0xFFFF
		"pshufw $0xFF, %%mm1, %%mm1	\n\t"
		"1:				\n\t"
		"adcl %%edx, %%esi		\n\t" //xx+= (4*lumXInc)>>16 + carry
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

	xpos= 0; //lumXInc/2 - 0x8000; // difference between pixel centers

	for(i=0; i<dstW/8; i++)
	{
		int xx=xpos>>16;

		if((i&3) == 0)
		{
			int a=0;
			int b=((xpos+xInc)>>16) - xx;
			int c=((xpos+xInc*2)>>16) - xx;
			int d=((xpos+xInc*3)>>16) - xx;

			memcpy(funnyCode + fragmentLength*i/4, fragment, fragmentLength);

			funnyCode[fragmentLength*i/4 + imm8OfPShufW1]=
			funnyCode[fragmentLength*i/4 + imm8OfPShufW2]=
				a | (b<<2) | (c<<4) | (d<<6);

			// if we dont need to read 8 bytes than dont :), reduces the chance of
			// crossing a cache line
			if(d<3) funnyCode[fragmentLength*i/4 + 1]= 0x6E;

			funnyCode[fragmentLength*(i+4)/4]= RET;
		}
		xpos+=xInc;
	}
/*
	xpos= 0; //chrXInc/2 - 0x10000; // difference between centers of chrom samples
	for(i=0; i<dstUVw/8; i++)
	{
		int xx=xpos>>16;

		if((i&3) == 0)
		{
			int a=0;
			int b=((xpos+chrXInc)>>16) - xx;
			int c=((xpos+chrXInc*2)>>16) - xx;
			int d=((xpos+chrXInc*3)>>16) - xx;

			memcpy(funnyUVCode + fragmentLength*i/4, fragment, fragmentLength);

			funnyUVCode[fragmentLength*i/4 + imm8OfPShufW1]=
			funnyUVCode[fragmentLength*i/4 + imm8OfPShufW2]=
				a | (b<<2) | (c<<4) | (d<<6);

			// if we dont need to read 8 bytes than dont :), reduces the chance of
			// crossing a cache line
			if(d<3) funnyUVCode[fragmentLength*i/4 + 1]= 0x6E;

			funnyUVCode[fragmentLength*(i+4)/4]= RET;
		}
		xpos+=chrXInc;
	}
*/
//	funnyCode[0]= RET;
}
#endif // HAVE_MMX2

static void RENAME(SwScale_YV12slice)(unsigned char* srcptr[],int stride[], int srcSliceY ,
			     int srcSliceH, uint8_t* dstptr[], int dststride, int dstbpp,
			     int srcW, int srcH, int dstW, int dstH){


unsigned int lumXInc= (srcW << 16) / dstW;
unsigned int lumYInc= (srcH << 16) / dstH;
unsigned int chrXInc;
unsigned int chrYInc;

static int dstY;

// used to detect a size change
static int oldDstW= -1;
static int oldSrcW= -1;
static int oldDstH= -1;
static int oldSrcH= -1;
static int oldFlags=-1;

static int lastInLumBuf;
static int lastInChrBuf;

int chrDstW, chrDstH;

static int lumBufIndex=0;
static int chrBufIndex=0;

static int firstTime=1;

const int widthAlign= dstbpp==12 ? 16 : 8;
const int bytespp= (dstbpp+1)/8; //(12->1, 15&16->2, 24->3, 32->4)
const int over= dstbpp==12 ? 	  (((dstW+15)&(~15))) - dststride
				: (((dstW+7)&(~7)))*bytespp - dststride;
if(dststride%widthAlign !=0 )
{
	if(firstTime)
		fprintf(stderr, "SwScaler: Warning: dstStride is not a multiple of %d!\n"
				"SwScaler:          ->cannot do aligned memory acesses anymore\n",
				widthAlign);
}

if(over>0)
{
	if(firstTime)
		fprintf(stderr, "SwScaler: Warning: output width is not a multiple of 8 (16 for YV12)\n"
				"SwScaler:          and dststride is not large enough to handle %d extra bytes\n"
				"SwScaler:          ->using unoptimized C version for last line(s)\n",
				over);
}



//printf("%d %d %d %d\n", srcW, srcH, dstW, dstH);
//printf("%d %d %d %d\n", lumXInc, lumYInc, srcSliceY, srcSliceH);

#ifdef HAVE_MMX2
canMMX2BeUsed= (lumXInc <= 0x10000 && (dstW&31)==0 && (srcW&15)==0) ? 1 : 0;
if(!canMMX2BeUsed && lumXInc <= 0x10000 && (srcW&15)==0 && sws_flags==SWS_FAST_BILINEAR)
{
	if(firstTime)
		fprintf(stderr, "SwScaler: output Width is not a multiple of 32 -> no MMX2 scaler\n");
}
#else
canMMX2BeUsed=0; // should be 0 anyway but ...
#endif

if(firstTime)
{
#if defined (DITHER1XBPP) && defined (HAVE_MMX)
	char *dither= " dithered";
#else
	char *dither= "";
#endif
	if(sws_flags==SWS_FAST_BILINEAR)
		fprintf(stderr, "\nSwScaler: FAST_BILINEAR scaler ");
	else if(sws_flags==SWS_BILINEAR)
		fprintf(stderr, "\nSwScaler: BILINEAR scaler ");
	else if(sws_flags==SWS_BICUBIC)
		fprintf(stderr, "\nSwScaler: BICUBIC scaler ");
	else
		fprintf(stderr, "\nSwScaler: ehh flags invalid?! ");

	if(dstbpp==15)
		fprintf(stderr, "with%s BGR15 output ", dither);
	else if(dstbpp==16)
		fprintf(stderr, "with%s BGR16 output ", dither);
	else if(dstbpp==24)
		fprintf(stderr, "with BGR24 output ");
	else if(dstbpp==32)
		fprintf(stderr, "with BGR32 output ");
	else if(dstbpp==12)
		fprintf(stderr, "with YV12 output ");
	else
		fprintf(stderr, "without output ");

#ifdef HAVE_MMX2
		fprintf(stderr, "using MMX2\n");
#elif defined (HAVE_3DNOW)
		fprintf(stderr, "using 3DNOW\n");
#elif defined (HAVE_MMX)
		fprintf(stderr, "using MMX\n");
#elif defined (ARCH_X86)
		fprintf(stderr, "using X86 ASM\n");
#else
		fprintf(stderr, "using C\n");
#endif
}


// match pixel 0 of the src to pixel 0 of dst and match pixel n-2 of src to pixel n-2 of dst
// n-2 is the last chrominance sample available
// this is not perfect, but noone shuld notice the difference, the more correct variant
// would be like the vertical one, but that would require some special code for the
// first and last pixel
if(sws_flags==SWS_FAST_BILINEAR)
{
	if(canMMX2BeUsed) 	lumXInc+= 20;
#ifndef HAVE_MMX //we dont use the x86asm scaler if mmx is available
	else			lumXInc = ((srcW-2)<<16)/(dstW-2) - 20;
#endif
}

if(fullUVIpol && !(dstbpp==12)) 	chrXInc= lumXInc>>1, chrDstW= dstW;
else					chrXInc= lumXInc,    chrDstW= (dstW+1)>>1;

if(dstbpp==12)	chrYInc= lumYInc,    chrDstH= (dstH+1)>>1;
else		chrYInc= lumYInc>>1, chrDstH= dstH;

  // force calculation of the horizontal interpolation of the first line

  if(srcSliceY ==0){
//	printf("dstW %d, srcw %d, mmx2 %d\n", dstW, srcW, canMMX2BeUsed);
	lumBufIndex=0;
	chrBufIndex=0;
	dstY=0;

	//precalculate horizontal scaler filter coefficients
	if(oldDstW!=dstW || oldSrcW!=srcW || oldFlags!=sws_flags)
	{
#ifdef HAVE_MMX
		const int filterAlign=4;
#else
		const int filterAlign=1;
#endif
		oldDstW= dstW; oldSrcW= srcW; oldFlags= sws_flags;

		RENAME(initFilter)(hLumFilter, hLumFilterPos, &hLumFilterSize, lumXInc,
				srcW   , dstW   , filterAlign, 1<<14);
		RENAME(initFilter)(hChrFilter, hChrFilterPos, &hChrFilterSize, chrXInc,
				(srcW+1)>>1, chrDstW, filterAlign, 1<<14);

#ifdef HAVE_MMX2
// cant downscale !!!
		if(canMMX2BeUsed && sws_flags == SWS_FAST_BILINEAR)
		{
			initMMX2HScaler(dstW   , lumXInc, funnyYCode);
			initMMX2HScaler(chrDstW, chrXInc, funnyUVCode);
		}
#endif
	} // Init Horizontal stuff

	if(oldDstH!=dstH || oldSrcH!=srcH || oldFlags!=sws_flags)
	{
		int i;
		oldDstH= dstH; oldSrcH= srcH; oldFlags= sws_flags; //FIXME swsflags conflict with x check

		// deallocate pixbufs
		for(i=0; i<vLumBufSize; i++) free(lumPixBuf[i]);
		for(i=0; i<vChrBufSize; i++) free(chrPixBuf[i]);

		RENAME(initFilter)(vLumFilter, vLumFilterPos, &vLumFilterSize, lumYInc,
				srcH   , dstH,    1, (1<<12)-4);
		RENAME(initFilter)(vChrFilter, vChrFilterPos, &vChrFilterSize, chrYInc,
				(srcH+1)>>1, chrDstH, 1, (1<<12)-4);

		// Calculate Buffer Sizes so that they wont run out while handling these damn slices
		vLumBufSize= vLumFilterSize; vChrBufSize= vChrFilterSize;
		for(i=0; i<dstH; i++)
		{
			int chrI= i*chrDstH / dstH;
			int nextSlice= MAX(vLumFilterPos[i   ] + vLumFilterSize - 1,
					 ((vChrFilterPos[chrI] + vChrFilterSize - 1)<<1));
			nextSlice&= ~1; // Slices start at even boundaries
			if(vLumFilterPos[i   ] + vLumBufSize < nextSlice)
				vLumBufSize= nextSlice - vLumFilterPos[i   ];
			if(vChrFilterPos[chrI] + vChrBufSize < (nextSlice>>1))
				vChrBufSize= (nextSlice>>1) - vChrFilterPos[chrI];
		}

		// allocate pixbufs (we use dynamic allocation because otherwise we would need to
		// allocate several megabytes to handle all possible cases)
		for(i=0; i<vLumBufSize; i++)
			lumPixBuf[i]= lumPixBuf[i+vLumBufSize]= (uint16_t*)memalign(8, 4000);
		for(i=0; i<vChrBufSize; i++)
			chrPixBuf[i]= chrPixBuf[i+vChrBufSize]= (uint16_t*)memalign(8, 8000);

		//try to avoid drawing green stuff between the right end and the stride end
		for(i=0; i<vLumBufSize; i++) memset(lumPixBuf[i], 0, 4000);
		for(i=0; i<vChrBufSize; i++) memset(chrPixBuf[i], 64, 8000);

		ASSERT(chrDstH<=dstH)
		ASSERT(vLumFilterSize*dstH*4<16000)
		ASSERT(vChrFilterSize*chrDstH*4<16000)
#ifdef HAVE_MMX
		// pack filter data for mmx code
		for(i=0; i<vLumFilterSize*dstH; i++)
			lumMmxFilter[4*i]=lumMmxFilter[4*i+1]=lumMmxFilter[4*i+2]=lumMmxFilter[4*i+3]=
				vLumFilter[i];
		for(i=0; i<vChrFilterSize*chrDstH; i++)
			chrMmxFilter[4*i]=chrMmxFilter[4*i+1]=chrMmxFilter[4*i+2]=chrMmxFilter[4*i+3]=
				vChrFilter[i];
#endif
	}

	if(firstTime && verbose)
	{
#ifdef HAVE_MMX2
		int mmx2=1;
#else
		int mmx2=0;
#endif
#ifdef HAVE_MMX
		int mmx=1;
#else
		int mmx=0;
#endif

#ifdef HAVE_MMX
		if(canMMX2BeUsed && sws_flags==SWS_FAST_BILINEAR)
			printf("SwScaler: using FAST_BILINEAR MMX2 scaler for horizontal scaling\n");
		else
		{
			if(hLumFilterSize==4)
				printf("SwScaler: using 4-tap MMX scaler for horizontal luminance scaling\n");
			else if(hLumFilterSize==8)
				printf("SwScaler: using 8-tap MMX scaler for horizontal luminance scaling\n");
			else
				printf("SwScaler: using n-tap MMX scaler for horizontal luminance scaling\n");

			if(hChrFilterSize==4)
				printf("SwScaler: using 4-tap MMX scaler for horizontal chrominance scaling\n");
			else if(hChrFilterSize==8)
				printf("SwScaler: using 8-tap MMX scaler for horizontal chrominance scaling\n");
			else
				printf("SwScaler: using n-tap MMX scaler for horizontal chrominance scaling\n");
		}
#elif defined (ARCH_X86)
		printf("SwScaler: using X86-Asm scaler for horizontal scaling\n");
#else
		if(sws_flags==SWS_FAST_BILINEAR)
			printf("SwScaler: using FAST_BILINEAR C scaler for horizontal scaling\n");
		else
			printf("SwScaler: using C scaler for horizontal scaling\n");
#endif

		if(dstbpp==12)
		{
			if(vLumFilterSize==1)
				printf("SwScaler: using 1-tap %s \"scaler\" for vertical scaling (YV12)\n", mmx ? "MMX" : "C");
			else
				printf("SwScaler: using n-tap %s scaler for vertical scaling (YV12)\n", mmx ? "MMX" : "C");
		}
		else
		{
			if(vLumFilterSize==1 && vChrFilterSize==2)
				printf("SwScaler: using 1-tap %s \"scaler\" for vertical luminance scaling (BGR)\n"
				       "SwScaler:       2-tap scaler for vertical chrominance scaling (BGR)\n", mmx ? "MMX" : "C");
			else if(vLumFilterSize==2 && vChrFilterSize==2)
				printf("SwScaler: using 2-tap linear %s scaler for vertical scaling (BGR)\n", mmx ? "MMX" : "C");
			else
				printf("SwScaler: using n-tap %s scaler for vertical scaling (BGR)\n", mmx ? "MMX" : "C");
		}

		if(dstbpp==24)
			printf("SwScaler: using %s YV12->BGR24 Converter\n",
				mmx2 ? "MMX2" : (mmx ? "MMX" : "C"));
		else
			printf("SwScaler: using %s YV12->BGR%d Converter\n", mmx ? "MMX" : "C", dstbpp);

		printf("SwScaler: %dx%d -> %dx%d\n", srcW, srcH, dstW, dstH);
	}

	lastInLumBuf= -1;
	lastInChrBuf= -1;
  } // if(firstLine)

	for(;dstY < dstH; dstY++){
		unsigned char *dest =dstptr[0]+dststride*dstY;
		unsigned char *uDest=dstptr[1]+(dststride>>1)*(dstY>>1);
		unsigned char *vDest=dstptr[2]+(dststride>>1)*(dstY>>1);
		const int chrDstY= dstbpp==12 ? (dstY>>1) : dstY;

		const int firstLumSrcY= vLumFilterPos[dstY]; //First line needed as input
		const int firstChrSrcY= vChrFilterPos[chrDstY]; //First line needed as input
		const int lastLumSrcY= firstLumSrcY + vLumFilterSize -1; // Last line needed as input
		const int lastChrSrcY= firstChrSrcY + vChrFilterSize -1; // Last line needed as input

		if(sws_flags == SWS_FAST_BILINEAR)
		{
			//handle holes
			if(firstLumSrcY > lastInLumBuf) lastInLumBuf= firstLumSrcY-1;
			if(firstChrSrcY > lastInChrBuf) lastInChrBuf= firstChrSrcY-1;
		}

		ASSERT(firstLumSrcY >= lastInLumBuf - vLumBufSize + 1)
		ASSERT(firstChrSrcY >= lastInChrBuf - vChrBufSize + 1)

		// Do we have enough lines in this slice to output the dstY line
		if(lastLumSrcY < srcSliceY + srcSliceH && lastChrSrcY < ((srcSliceY + srcSliceH)>>1))
		{
			//Do horizontal scaling
			while(lastInLumBuf < lastLumSrcY)
			{
				uint8_t *src= srcptr[0]+(lastInLumBuf + 1 - srcSliceY)*stride[0];
				lumBufIndex++;
				ASSERT(lumBufIndex < 2*vLumBufSize)
				ASSERT(lastInLumBuf + 1 - srcSliceY < srcSliceH)
				ASSERT(lastInLumBuf + 1 - srcSliceY >= 0)
//				printf("%d %d\n", lumBufIndex, vLumBufSize);
				RENAME(hyscale)(lumPixBuf[ lumBufIndex ], dstW, src, srcW, lumXInc);
				lastInLumBuf++;
			}
			while(lastInChrBuf < lastChrSrcY)
			{
				uint8_t *src1= srcptr[1]+(lastInChrBuf + 1 - (srcSliceY>>1))*stride[1];
				uint8_t *src2= srcptr[2]+(lastInChrBuf + 1 - (srcSliceY>>1))*stride[2];
				chrBufIndex++;
				ASSERT(chrBufIndex < 2*vChrBufSize)
				ASSERT(lastInChrBuf + 1 - (srcSliceY>>1) < (srcSliceH>>1))
				ASSERT(lastInChrBuf + 1 - (srcSliceY>>1) >= 0)
				RENAME(hcscale)(chrPixBuf[ chrBufIndex ], chrDstW, src1, src2, (srcW+1)>>1, chrXInc);
				lastInChrBuf++;
			}
			//wrap buf index around to stay inside the ring buffer
			if(lumBufIndex >= vLumBufSize ) lumBufIndex-= vLumBufSize;
			if(chrBufIndex >= vChrBufSize ) chrBufIndex-= vChrBufSize;
		}
		else // not enough lines left in this slice -> load the rest in the buffer
		{
/*		printf("%d %d Last:%d %d LastInBuf:%d %d Index:%d %d Y:%d FSize: %d %d BSize: %d %d\n",
			firstChrSrcY,firstLumSrcY,lastChrSrcY,lastLumSrcY,
			lastInChrBuf,lastInLumBuf,chrBufIndex,lumBufIndex,dstY,vChrFilterSize,vLumFilterSize,
			vChrBufSize, vLumBufSize);
*/
			//Do horizontal scaling
			while(lastInLumBuf+1 < srcSliceY + srcSliceH)
			{
				uint8_t *src= srcptr[0]+(lastInLumBuf + 1 - srcSliceY)*stride[0];
				lumBufIndex++;
				ASSERT(lumBufIndex < 2*vLumBufSize)
				ASSERT(lastInLumBuf + 1 - srcSliceY < srcSliceH)
				ASSERT(lastInLumBuf + 1 - srcSliceY >= 0)
				RENAME(hyscale)(lumPixBuf[ lumBufIndex ], dstW, src, srcW, lumXInc);
				lastInLumBuf++;
			}
			while(lastInChrBuf+1 < ((srcSliceY + srcSliceH)>>1))
			{
				uint8_t *src1= srcptr[1]+(lastInChrBuf + 1 - (srcSliceY>>1))*stride[1];
				uint8_t *src2= srcptr[2]+(lastInChrBuf + 1 - (srcSliceY>>1))*stride[2];
				chrBufIndex++;
				ASSERT(chrBufIndex < 2*vChrBufSize)
				ASSERT(lastInChrBuf + 1 - (srcSliceY>>1) < (srcSliceH>>1))
				ASSERT(lastInChrBuf + 1 - (srcSliceY>>1) >= 0)
				RENAME(hcscale)(chrPixBuf[ chrBufIndex ], chrDstW, src1, src2, (srcW+1)>>1, chrXInc);
				lastInChrBuf++;
			}
			//wrap buf index around to stay inside the ring buffer
			if(lumBufIndex >= vLumBufSize ) lumBufIndex-= vLumBufSize;
			if(chrBufIndex >= vChrBufSize ) chrBufIndex-= vChrBufSize;
			break; //we cant output a dstY line so lets try with the next slice
		}

#ifdef HAVE_MMX
		b5Dither= dither8[dstY&1];
		g6Dither= dither4[dstY&1];
		g5Dither= dither8[dstY&1];
		r5Dither= dither8[(dstY+1)&1];
#endif
	    if(dstY < dstH-2 || over<=0)
	    {
		if(dstbpp==12) //YV12
		{
			if(dstY&1) uDest=vDest= NULL; //FIXME split functions in lumi / chromi
			if(vLumFilterSize == 1 && vChrFilterSize == 1) // Unscaled YV12
			{
				int16_t *lumBuf = lumPixBuf[0];
				int16_t *chrBuf= chrPixBuf[0];
				RENAME(yuv2yuv1)(lumBuf, chrBuf, dest, uDest, vDest, dstW);
			}
			else //General YV12
			{
				int16_t **lumSrcPtr= lumPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
				int16_t **chrSrcPtr= chrPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
				RENAME(yuv2yuvX)(
					vLumFilter+dstY*vLumFilterSize     , lumSrcPtr, vLumFilterSize,
					vChrFilter+(dstY>>1)*vChrFilterSize, chrSrcPtr, vChrFilterSize,
					dest, uDest, vDest, dstW,
					lumMmxFilter+dstY*vLumFilterSize*4, chrMmxFilter+(dstY>>1)*vChrFilterSize*4);
			}
		}
		else
		{
			int16_t **lumSrcPtr= lumPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
			int16_t **chrSrcPtr= chrPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;

			ASSERT(lumSrcPtr + vLumFilterSize - 1 < lumPixBuf + vLumBufSize*2);
			ASSERT(chrSrcPtr + vChrFilterSize - 1 < chrPixBuf + vChrBufSize*2);
			if(vLumFilterSize == 1 && vChrFilterSize == 2) //Unscaled RGB
			{
				int chrAlpha= vChrFilter[2*dstY+1];

				RENAME(yuv2rgb1)(*lumSrcPtr, *chrSrcPtr, *(chrSrcPtr+1),
						 dest, dstW, chrAlpha, dstbpp);
			}
			else if(vLumFilterSize == 2 && vChrFilterSize == 2) //BiLinear Upscale RGB
			{
				int lumAlpha= vLumFilter[2*dstY+1];
				int chrAlpha= vChrFilter[2*dstY+1];

				RENAME(yuv2rgb2)(*lumSrcPtr, *(lumSrcPtr+1), *chrSrcPtr, *(chrSrcPtr+1),
						 dest, dstW, lumAlpha, chrAlpha, dstbpp);
			}
			else //General RGB
			{
				RENAME(yuv2rgbX)(
					vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
					vChrFilter+dstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
					dest, dstW, dstbpp,
					lumMmxFilter+dstY*vLumFilterSize*4, chrMmxFilter+dstY*vChrFilterSize*4);
			}
		}
            }
	    else // hmm looks like we cant use MMX here without overwriting this arrays tail
	    {
		int16_t **lumSrcPtr= lumPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
		int16_t **chrSrcPtr= chrPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
		if(dstbpp==12) //YV12
		{
			if(dstY&1) uDest=vDest= NULL; //FIXME split functions in lumi / chromi
			yuv2yuvXinC(
				vLumFilter+dstY*vLumFilterSize     , lumSrcPtr, vLumFilterSize,
				vChrFilter+(dstY>>1)*vChrFilterSize, chrSrcPtr, vChrFilterSize,
				dest, uDest, vDest, dstW);
		}
		else
		{
			ASSERT(lumSrcPtr + vLumFilterSize - 1 < lumPixBuf + vLumBufSize*2);
			ASSERT(chrSrcPtr + vChrFilterSize - 1 < chrPixBuf + vChrBufSize*2);
			yuv2rgbXinC(
				vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
				vChrFilter+dstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
				dest, dstW, dstbpp);
		}
	    }
	}

#ifdef HAVE_MMX
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
	firstTime=0;
}
