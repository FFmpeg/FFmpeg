
/*
 * yuv2rgb_mmx.c, Software YUV to RGB coverter with Intel MMX "technology"
 *
 * Copyright (C) 2000, Silicon Integrated System Corp.
 * All Rights Reserved.
 *
 * Author: Olie Lho <ollie@sis.com.tw>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video decoder
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Make; see the file COPYING. If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * 15,24 bpp and dithering from Michael Niedermayer (michaelni@gmx.at)
 * MMX/MMX2 Template stuff from Michael Niedermayer (needed for fast movntq support)
 */

#undef MOVNTQ
#undef EMMS
#undef SFENCE

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
#define SFENCE "/nop"
#endif

#define YUV2RGB \
		     /* Do the multiply part of the conversion for even and odd pixels,
			register usage:
			mm0 -> Cblue, mm1 -> Cred, mm2 -> Cgreen even pixels,
			mm3 -> Cblue, mm4 -> Cred, mm5 -> Cgreen odd pixels,
			mm6 -> Y even, mm7 -> Y odd */\
		     /* convert the chroma part */\
		     "punpcklbw %%mm4, %%mm0;" /* scatter 4 Cb 00 u3 00 u2 00 u1 00 u0 */ \
		     "punpcklbw %%mm4, %%mm1;" /* scatter 4 Cr 00 v3 00 v2 00 v1 00 v0 */ \
\
		     "psubsw "MANGLE(mmx_80w)", %%mm0;" /* Cb -= 128 */ \
		     "psubsw "MANGLE(mmx_80w)", %%mm1;" /* Cr -= 128 */ \
\
		     "psllw $3, %%mm0;" /* Promote precision */ \
		     "psllw $3, %%mm1;" /* Promote precision */ \
\
		     "movq %%mm0, %%mm2;" /* Copy 4 Cb 00 u3 00 u2 00 u1 00 u0 */ \
		     "movq %%mm1, %%mm3;" /* Copy 4 Cr 00 v3 00 v2 00 v1 00 v0 */ \
\
		     "pmulhw "MANGLE(mmx_U_green)", %%mm2;" /* Mul Cb with green coeff -> Cb green */ \
		     "pmulhw "MANGLE(mmx_V_green)", %%mm3;" /* Mul Cr with green coeff -> Cr green */ \
\
		     "pmulhw "MANGLE(mmx_U_blue)", %%mm0;" /* Mul Cb -> Cblue 00 b3 00 b2 00 b1 00 b0 */\
		     "pmulhw "MANGLE(mmx_V_red)", %%mm1;" /* Mul Cr -> Cred 00 r3 00 r2 00 r1 00 r0 */\
\
		     "paddsw %%mm3, %%mm2;" /* Cb green + Cr green -> Cgreen */\
\
		     /* convert the luma part */\
		     "psubusb "MANGLE(mmx_10w)", %%mm6;" /* Y -= 16 */\
\
		     "movq %%mm6, %%mm7;" /* Copy 8 Y Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */\
		     "pand "MANGLE(mmx_00ffw)", %%mm6;" /* get Y even 00 Y6 00 Y4 00 Y2 00 Y0 */\
\
		     "psrlw $8, %%mm7;" /* get Y odd 00 Y7 00 Y5 00 Y3 00 Y1 */\
\
		     "psllw $3, %%mm6;" /* Promote precision */\
		     "psllw $3, %%mm7;" /* Promote precision */\
\
		     "pmulhw "MANGLE(mmx_Y_coeff)", %%mm6;" /* Mul 4 Y even 00 y6 00 y4 00 y2 00 y0 */\
		     "pmulhw "MANGLE(mmx_Y_coeff)", %%mm7;" /* Mul 4 Y odd 00 y7 00 y5 00 y3 00 y1 */\
\
		     /* Do the addition part of the conversion for even and odd pixels,
			register usage:
			mm0 -> Cblue, mm1 -> Cred, mm2 -> Cgreen even pixels,
			mm3 -> Cblue, mm4 -> Cred, mm5 -> Cgreen odd pixels,
			mm6 -> Y even, mm7 -> Y odd */\
		     "movq %%mm0, %%mm3;" /* Copy Cblue */\
		     "movq %%mm1, %%mm4;" /* Copy Cred */\
		     "movq %%mm2, %%mm5;" /* Copy Cgreen */\
\
		     "paddsw %%mm6, %%mm0;" /* Y even + Cblue 00 B6 00 B4 00 B2 00 B0 */\
		     "paddsw %%mm7, %%mm3;" /* Y odd + Cblue 00 B7 00 B5 00 B3 00 B1 */\
\
		     "paddsw %%mm6, %%mm1;" /* Y even + Cred 00 R6 00 R4 00 R2 00 R0 */\
		     "paddsw %%mm7, %%mm4;" /* Y odd + Cred 00 R7 00 R5 00 R3 00 R1 */\
\
		     "paddsw %%mm6, %%mm2;" /* Y even + Cgreen 00 G6 00 G4 00 G2 00 G0 */\
		     "paddsw %%mm7, %%mm5;" /* Y odd + Cgreen 00 G7 00 G5 00 G3 00 G1 */\
\
		     /* Limit RGB even to 0..255 */\
		     "packuswb %%mm0, %%mm0;" /* B6 B4 B2 B0  B6 B4 B2 B0 */\
		     "packuswb %%mm1, %%mm1;" /* R6 R4 R2 R0  R6 R4 R2 R0 */\
		     "packuswb %%mm2, %%mm2;" /* G6 G4 G2 G0  G6 G4 G2 G0 */\
\
		     /* Limit RGB odd to 0..255 */\
		     "packuswb %%mm3, %%mm3;" /* B7 B5 B3 B1  B7 B5 B3 B1 */\
		     "packuswb %%mm4, %%mm4;" /* R7 R5 R3 R1  R7 R5 R3 R1 */\
		     "packuswb %%mm5, %%mm5;" /* G7 G5 G3 G1  G7 G5 G3 G1 */\
\
		     /* Interleave RGB even and odd */\
		     "punpcklbw %%mm3, %%mm0;" /* B7 B6 B5 B4 B3 B2 B1 B0 */\
		     "punpcklbw %%mm4, %%mm1;" /* R7 R6 R5 R4 R3 R2 R1 R0 */\
		     "punpcklbw %%mm5, %%mm2;" /* G7 G6 G5 G4 G3 G2 G1 G0 */\


static inline void RENAME(yuv420_rgb16) (uint8_t * image, uint8_t * py,
			      uint8_t * pu, uint8_t * pv,
			      int h_size, int v_size,
			      int rgb_stride, int y_stride, int uv_stride)
{
    int even = 1;
    int x, y;

    __asm__ __volatile__ ("pxor %mm4, %mm4;" /* zero mm4 */ );

    for (y = v_size; --y >= 0; ) {
	uint8_t *_image = image;
	uint8_t *_py = py;
	uint8_t *_pu = pu;
	uint8_t *_pv = pv;
	int internal_h_size= h_size;
	int aligned_h_size= (h_size+7)&~7;

	if(rgb_stride >= aligned_h_size*2) internal_h_size= aligned_h_size;

	b5Dither= dither8[y&1];
	g6Dither= dither4[y&1];
	g5Dither= dither8[y&1];
	r5Dither= dither8[(y+1)&1];

	/* load data for start of next scan line */
	__asm__ __volatile__ (
		 "movd (%1), %%mm0;" /* Load 4 Cb 00 00 00 00 u3 u2 u1 u0 */
		 "movd (%2), %%mm1;" /* Load 4 Cr 00 00 00 00 v3 v2 v1 v0 */
		 "movq (%0), %%mm6;" /* Load 8  Y Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */

		 : : "r" (_py), "r" (_pu), "r" (_pv));

	for (x = internal_h_size >> 3; --x >= 0; ) {
	    /* this mmx assembly code deals with SINGLE scan line at a time, it convert 8
	       pixels in each iteration */

	    __asm__ __volatile__ (
/* no speed diference on my p3@500 with prefetch,
 * if it is faster for anyone with -benchmark then tell me
			PREFETCH" 64(%0) \n\t"
			PREFETCH" 64(%1) \n\t"
			PREFETCH" 64(%2) \n\t"
*/
YUV2RGB

#ifdef DITHER1XBPP
			"paddusb "MANGLE(b5Dither)", %%mm0;"
			"paddusb "MANGLE(g6Dither)", %%mm2;"
			"paddusb "MANGLE(r5Dither)", %%mm1;"
#endif
		     /* mask unneeded bits off */
		     "pand "MANGLE(mmx_redmask)", %%mm0;" /* b7b6b5b4 b3_0_0_0 b7b6b5b4 b3_0_0_0 */
		     "pand "MANGLE(mmx_grnmask)", %%mm2;" /* g7g6g5g4 g3g2_0_0 g7g6g5g4 g3g2_0_0 */
		     "pand "MANGLE(mmx_redmask)", %%mm1;" /* r7r6r5r4 r3_0_0_0 r7r6r5r4 r3_0_0_0 */

		     "psrlw $3,%%mm0;" /* 0_0_0_b7 b6b5b4b3 0_0_0_b7 b6b5b4b3 */
		     "pxor %%mm4, %%mm4;" /* zero mm4 */

		     "movq %%mm0, %%mm5;" /* Copy B7-B0 */
		     "movq %%mm2, %%mm7;" /* Copy G7-G0 */

		     /* convert rgb24 plane to rgb16 pack for pixel 0-3 */
		     "punpcklbw %%mm4, %%mm2;" /* 0_0_0_0 0_0_0_0 g7g6g5g4 g3g2_0_0 */
		     "punpcklbw %%mm1, %%mm0;" /* r7r6r5r4 r3_0_0_0 0_0_0_b7 b6b5b4b3 */

		     "psllw $3, %%mm2;" /* 0_0_0_0 0_g7g6g5 g4g3g2_0 0_0_0_0 */
		     "por %%mm2, %%mm0;" /* r7r6r5r4 r3g7g6g5 g4g3g2b7 b6b5b4b3 */

		     "movq 8 (%0), %%mm6;" /* Load 8 Y Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */
		     MOVNTQ " %%mm0, (%3);" /* store pixel 0-3 */

		     /* convert rgb24 plane to rgb16 pack for pixel 0-3 */
		     "punpckhbw %%mm4, %%mm7;" /* 0_0_0_0 0_0_0_0 g7g6g5g4 g3g2_0_0 */
		     "punpckhbw %%mm1, %%mm5;" /* r7r6r5r4 r3_0_0_0 0_0_0_b7 b6b5b4b3 */

		     "psllw $3, %%mm7;" /* 0_0_0_0 0_g7g6g5 g4g3g2_0 0_0_0_0 */
		     "movd 4 (%1), %%mm0;" /* Load 4 Cb 00 00 00 00 u3 u2 u1 u0 */

		     "por %%mm7, %%mm5;" /* r7r6r5r4 r3g7g6g5 g4g3g2b7 b6b5b4b3 */
		     "movd 4 (%2), %%mm1;" /* Load 4 Cr 00 00 00 00 v3 v2 v1 v0 */

		     MOVNTQ " %%mm5, 8 (%3);" /* store pixel 4-7 */
		     : : "r" (_py), "r" (_pu), "r" (_pv), "r" (_image));

	    _py += 8;
	    _pu += 4;
	    _pv += 4;
	    _image += 16;
	}

	if (!even) {
	    pu += uv_stride;
	    pv += uv_stride;
	}

	py += y_stride;
	image += rgb_stride;

	even = (!even);
    }

    __asm__ __volatile__ (EMMS);
}

static inline void RENAME(yuv420_rgb15) (uint8_t * image, uint8_t * py,
			      uint8_t * pu, uint8_t * pv,
			      int h_size, int v_size,
			      int rgb_stride, int y_stride, int uv_stride)
{
    int even = 1;
    int x, y;

    __asm__ __volatile__ ("pxor %mm4, %mm4;" /* zero mm4 */ );

    for (y = v_size; --y >= 0; ) {
	uint8_t *_image = image;
	uint8_t *_py = py;
	uint8_t *_pu = pu;
	uint8_t *_pv = pv;
	int internal_h_size= h_size;
	int aligned_h_size= (h_size+7)&~7;

	if(rgb_stride >= aligned_h_size*2) internal_h_size= aligned_h_size;

	b5Dither= dither8[y&1];
	g6Dither= dither4[y&1];
	g5Dither= dither8[y&1];
	r5Dither= dither8[(y+1)&1];

	/* load data for start of next scan line */
	__asm__ __volatile__ (
		 "movd (%1), %%mm0;" /* Load 4 Cb 00 00 00 00 u3 u2 u1 u0 */
		 "movd (%2), %%mm1;" /* Load 4 Cr 00 00 00 00 v3 v2 v1 v0 */
		 "movq (%0), %%mm6;" /* Load 8  Y Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */

		 : : "r" (_py), "r" (_pu), "r" (_pv));

	for (x = internal_h_size >> 3; --x >= 0; ) {
	    /* this mmx assembly code deals with SINGLE scan line at a time, it convert 8
	       pixels in each iteration */

	    __asm__ __volatile__ (
YUV2RGB

#ifdef DITHER1XBPP
			"paddusb "MANGLE(b5Dither)", %%mm0	\n\t"
			"paddusb "MANGLE(g5Dither)", %%mm2	\n\t"
			"paddusb "MANGLE(r5Dither)", %%mm1	\n\t"
#endif

		     /* mask unneeded bits off */
		     "pand "MANGLE(mmx_redmask)", %%mm0;" /* b7b6b5b4 b3_0_0_0 b7b6b5b4 b3_0_0_0 */
		     "pand "MANGLE(mmx_redmask)", %%mm2;" /* g7g6g5g4 g3_0_0_0 g7g6g5g4 g3_0_0_0 */
		     "pand "MANGLE(mmx_redmask)", %%mm1;" /* r7r6r5r4 r3_0_0_0 r7r6r5r4 r3_0_0_0 */

		     "psrlw $3,%%mm0;" /* 0_0_0_b7 b6b5b4b3 0_0_0_b7 b6b5b4b3 */
		     "psrlw $1,%%mm1;"            /* 0_r7r6r5  r4r3_0_0 0_r7r6r5 r4r3_0_0 */
		     "pxor %%mm4, %%mm4;" /* zero mm4 */

		     "movq %%mm0, %%mm5;" /* Copy B7-B0 */
		     "movq %%mm2, %%mm7;" /* Copy G7-G0 */

		     /* convert rgb24 plane to rgb16 pack for pixel 0-3 */
		     "punpcklbw %%mm4, %%mm2;" /* 0_0_0_0 0_0_0_0 g7g6g5g4 g3_0_0_0 */
		     "punpcklbw %%mm1, %%mm0;" /* r7r6r5r4 r3_0_0_0 0_0_0_b7 b6b5b4b3 */

		     "psllw $2, %%mm2;" /* 0_0_0_0 0_0_g7g6 g5g4g3_0 0_0_0_0 */
		     "por %%mm2, %%mm0;" /* 0_r7r6r5 r4r3g7g6 g5g4g3b7 b6b5b4b3 */

		     "movq 8 (%0), %%mm6;" /* Load 8 Y Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */
		     MOVNTQ " %%mm0, (%3);" /* store pixel 0-3 */

		     /* convert rgb24 plane to rgb16 pack for pixel 0-3 */
		     "punpckhbw %%mm4, %%mm7;" /* 0_0_0_0 0_0_0_0 0_g7g6g5 g4g3_0_0 */
		     "punpckhbw %%mm1, %%mm5;" /* r7r6r5r4 r3_0_0_0 0_0_0_b7 b6b5b4b3 */

		     "psllw $2, %%mm7;" /* 0_0_0_0 0_0_g7g6 g5g4g3_0 0_0_0_0 */
		     "movd 4 (%1), %%mm0;" /* Load 4 Cb 00 00 00 00 u3 u2 u1 u0 */

		     "por %%mm7, %%mm5;" /* 0_r7r6r5 r4r3g7g6 g5g4g3b7 b6b5b4b3 */
		     "movd 4 (%2), %%mm1;" /* Load 4 Cr 00 00 00 00 v3 v2 v1 v0 */

		     MOVNTQ " %%mm5, 8 (%3);" /* store pixel 4-7 */
		     : : "r" (_py), "r" (_pu), "r" (_pv), "r" (_image));

	    _py += 8;
	    _pu += 4;
	    _pv += 4;
	    _image += 16;
	}

	if (!even) {
	    pu += uv_stride;
	    pv += uv_stride;
	}

	py += y_stride;
	image += rgb_stride;

	even = (!even);
    }

    __asm__ __volatile__ (EMMS);
}

static inline void RENAME(yuv420_rgb24) (uint8_t * image, uint8_t * py,
			      uint8_t * pu, uint8_t * pv,
			      int h_size, int v_size,
			      int rgb_stride, int y_stride, int uv_stride)
{
    int even = 1;
    int x, y;

    __asm__ __volatile__ ("pxor %mm4, %mm4;" /* zero mm4 */ );

    for (y = v_size; --y >= 0; ) {
	uint8_t *_image = image;
	uint8_t *_py = py;
	uint8_t *_pu = pu;
	uint8_t *_pv = pv;
	int internal_h_size= h_size;
	int aligned_h_size= (h_size+7)&~7;

	if(rgb_stride >= aligned_h_size*3) internal_h_size= aligned_h_size;

	/* load data for start of next scan line */
	__asm__ __volatile__ (
		 "movd (%1), %%mm0;" /* Load 4 Cb 00 00 00 00 u3 u2 u1 u0 */
		 "movd (%2), %%mm1;" /* Load 4 Cr 00 00 00 00 v3 v2 v1 v0 */
		 "movq (%0), %%mm6;" /* Load 8  Y Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */

		 : : "r" (_py), "r" (_pu), "r" (_pv));

	for (x = internal_h_size >> 3; --x >= 0; ) {
	    /* this mmx assembly code deals with SINGLE scan line at a time, it convert 8
	       pixels in each iteration */

	    __asm__ __volatile__ (
YUV2RGB
	/* mm0=B, %%mm2=G, %%mm1=R */
#ifdef HAVE_MMX2
			"movq "MANGLE(M24A)", %%mm4	\n\t"
			"movq "MANGLE(M24C)", %%mm7	\n\t"
			"pshufw $0x50, %%mm0, %%mm5	\n\t" /* B3 B2 B3 B2  B1 B0 B1 B0 */
			"pshufw $0x50, %%mm2, %%mm3	\n\t" /* G3 G2 G3 G2  G1 G0 G1 G0 */
			"pshufw $0x00, %%mm1, %%mm6	\n\t" /* R1 R0 R1 R0  R1 R0 R1 R0 */

			"pand %%mm4, %%mm5		\n\t" /*    B2        B1       B0 */
			"pand %%mm4, %%mm3		\n\t" /*    G2        G1       G0 */
			"pand %%mm7, %%mm6		\n\t" /*       R1        R0       */

			"psllq $8, %%mm3		\n\t" /* G2        G1       G0    */
			"por %%mm5, %%mm6		\n\t"
			"por %%mm3, %%mm6		\n\t"
			MOVNTQ" %%mm6, (%3)		\n\t"

			"psrlq $8, %%mm2		\n\t" /* 00 G7 G6 G5  G4 G3 G2 G1 */
			"pshufw $0xA5, %%mm0, %%mm5	\n\t" /* B5 B4 B5 B4  B3 B2 B3 B2 */
			"pshufw $0x55, %%mm2, %%mm3	\n\t" /* G4 G3 G4 G3  G4 G3 G4 G3 */
			"pshufw $0xA5, %%mm1, %%mm6	\n\t" /* R5 R4 R5 R4  R3 R2 R3 R2 */

			"pand "MANGLE(M24B)", %%mm5	\n\t" /* B5       B4        B3    */
			"pand %%mm7, %%mm3		\n\t" /*       G4        G3       */
			"pand %%mm4, %%mm6		\n\t" /*    R4        R3       R2 */

			"por %%mm5, %%mm3		\n\t" /* B5    G4 B4     G3 B3    */
			"por %%mm3, %%mm6		\n\t"
			MOVNTQ" %%mm6, 8(%3)		\n\t"

			"pshufw $0xFF, %%mm0, %%mm5	\n\t" /* B7 B6 B7 B6  B7 B6 B6 B7 */
			"pshufw $0xFA, %%mm2, %%mm3	\n\t" /* 00 G7 00 G7  G6 G5 G6 G5 */
			"pshufw $0xFA, %%mm1, %%mm6	\n\t" /* R7 R6 R7 R6  R5 R4 R5 R4 */
			"movd 4 (%1), %%mm0;" /* Load 4 Cb 00 00 00 00 u3 u2 u1 u0 */

			"pand %%mm7, %%mm5		\n\t" /*       B7        B6       */
			"pand %%mm4, %%mm3		\n\t" /*    G7        G6       G5 */
			"pand "MANGLE(M24B)", %%mm6	\n\t" /* R7       R6        R5    */
			"movd 4 (%2), %%mm1;" /* Load 4 Cr 00 00 00 00 v3 v2 v1 v0 */
\
			"por %%mm5, %%mm3		\n\t"
			"por %%mm3, %%mm6		\n\t"
			MOVNTQ" %%mm6, 16(%3)		\n\t"
			"movq 8 (%0), %%mm6;" /* Load 8 Y Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */
			"pxor %%mm4, %%mm4		\n\t"

#else

			"pxor %%mm4, %%mm4		\n\t"
			"movq %%mm0, %%mm5		\n\t" /* B */
			"movq %%mm1, %%mm6		\n\t" /* R */
			"punpcklbw %%mm2, %%mm0		\n\t" /* GBGBGBGB 0 */
			"punpcklbw %%mm4, %%mm1		\n\t" /* 0R0R0R0R 0 */
			"punpckhbw %%mm2, %%mm5		\n\t" /* GBGBGBGB 2 */
			"punpckhbw %%mm4, %%mm6		\n\t" /* 0R0R0R0R 2 */
			"movq %%mm0, %%mm7		\n\t" /* GBGBGBGB 0 */
			"movq %%mm5, %%mm3		\n\t" /* GBGBGBGB 2 */
			"punpcklwd %%mm1, %%mm7		\n\t" /* 0RGB0RGB 0 */
			"punpckhwd %%mm1, %%mm0		\n\t" /* 0RGB0RGB 1 */
			"punpcklwd %%mm6, %%mm5		\n\t" /* 0RGB0RGB 2 */
			"punpckhwd %%mm6, %%mm3		\n\t" /* 0RGB0RGB 3 */

			"movq %%mm7, %%mm2		\n\t" /* 0RGB0RGB 0 */
			"movq %%mm0, %%mm6		\n\t" /* 0RGB0RGB 1 */
			"movq %%mm5, %%mm1		\n\t" /* 0RGB0RGB 2 */
			"movq %%mm3, %%mm4		\n\t" /* 0RGB0RGB 3 */

			"psllq $40, %%mm7		\n\t" /* RGB00000 0 */
			"psllq $40, %%mm0		\n\t" /* RGB00000 1 */
			"psllq $40, %%mm5		\n\t" /* RGB00000 2 */
			"psllq $40, %%mm3		\n\t" /* RGB00000 3 */

			"punpckhdq %%mm2, %%mm7		\n\t" /* 0RGBRGB0 0 */
			"punpckhdq %%mm6, %%mm0		\n\t" /* 0RGBRGB0 1 */
			"punpckhdq %%mm1, %%mm5		\n\t" /* 0RGBRGB0 2 */
			"punpckhdq %%mm4, %%mm3		\n\t" /* 0RGBRGB0 3 */

			"psrlq $8, %%mm7		\n\t" /* 00RGBRGB 0 */
			"movq %%mm0, %%mm6		\n\t" /* 0RGBRGB0 1 */
			"psllq $40, %%mm0		\n\t" /* GB000000 1 */
			"por %%mm0, %%mm7		\n\t" /* GBRGBRGB 0 */
			MOVNTQ" %%mm7, (%3)		\n\t"

			"movd 4 (%1), %%mm0;" /* Load 4 Cb 00 00 00 00 u3 u2 u1 u0 */

			"psrlq $24, %%mm6		\n\t" /* 0000RGBR 1 */
			"movq %%mm5, %%mm1		\n\t" /* 0RGBRGB0 2 */
			"psllq $24, %%mm5		\n\t" /* BRGB0000 2 */
			"por %%mm5, %%mm6		\n\t" /* BRGBRGBR 1 */
			MOVNTQ" %%mm6, 8(%3)		\n\t"

			"movq 8 (%0), %%mm6;" /* Load 8 Y Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */

			"psrlq $40, %%mm1		\n\t" /* 000000RG 2 */
			"psllq $8, %%mm3		\n\t" /* RGBRGB00 3 */
			"por %%mm3, %%mm1		\n\t" /* RGBRGBRG 2 */
			MOVNTQ" %%mm1, 16(%3)		\n\t"

			"movd 4 (%2), %%mm1;" /* Load 4 Cr 00 00 00 00 v3 v2 v1 v0 */
			"pxor %%mm4, %%mm4		\n\t"
#endif

		     : : "r" (_py), "r" (_pu), "r" (_pv), "r" (_image));

	    _py += 8;
	    _pu += 4;
	    _pv += 4;
	    _image += 24;
	}

	if (!even) {
	    pu += uv_stride;
	    pv += uv_stride;
	}

	py += y_stride;
	image += rgb_stride;

	even = (!even);
    }

    __asm__ __volatile__ (EMMS);
}


static inline void RENAME(yuv420_argb32) (uint8_t * image, uint8_t * py,
			       uint8_t * pu, uint8_t * pv,
			       int h_size, int v_size,
			       int rgb_stride, int y_stride, int uv_stride)
{
    int even = 1;
    int x, y;

    __asm__ __volatile__ ("pxor %mm4, %mm4;" /* zero mm4 */ );

    for (y = v_size; --y >= 0; ) {
	uint8_t *_image = image;
	uint8_t *_py = py;
	uint8_t *_pu = pu;
	uint8_t *_pv = pv;
	int internal_h_size= h_size;
	int aligned_h_size= (h_size+7)&~7;

	if(rgb_stride >= aligned_h_size*4) internal_h_size= aligned_h_size;

	/* load data for start of next scan line */
	__asm__ __volatile__ 
	    (
	     "movd (%1), %%mm0;" /* Load 4 Cb 00 00 00 00 u3 u2 u1 u0 */ 
	     "movd (%2), %%mm1;" /* Load 4 Cr 00 00 00 00 v3 v2 v1 v0 */ 
	     "movq (%0), %%mm6;" /* Load 8  Y Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */ 
	     : : "r" (_py), "r" (_pu), "r" (_pv)
	     );

	for (x = internal_h_size >> 3; --x >= 0; ) {
	    /* this mmx assembly code deals with SINGLE scan line at a time, it convert 8
	       pixels in each iteration */
	    __asm__ __volatile__ (
YUV2RGB
		     /* convert RGB plane to RGB packed format,
			mm0 -> B, mm1 -> R, mm2 -> G, mm3 -> 0,
			mm4 -> GB, mm5 -> AR pixel 4-7,
			mm6 -> GB, mm7 -> AR pixel 0-3 */
		     "pxor %%mm3, %%mm3;" /* zero mm3 */

		     "movq %%mm0, %%mm6;" /* B7 B6 B5 B4 B3 B2 B1 B0 */
		     "movq %%mm1, %%mm7;" /* R7 R6 R5 R4 R3 R2 R1 R0 */

		     "movq %%mm0, %%mm4;" /* B7 B6 B5 B4 B3 B2 B1 B0 */
		     "movq %%mm1, %%mm5;" /* R7 R6 R5 R4 R3 R2 R1 R0 */

		     "punpcklbw %%mm2, %%mm6;" /* G3 B3 G2 B2 G1 B1 G0 B0 */
		     "punpcklbw %%mm3, %%mm7;" /* 00 R3 00 R2 00 R1 00 R0 */

		     "punpcklwd %%mm7, %%mm6;" /* 00 R1 B1 G1 00 R0 B0 G0 */
		     MOVNTQ " %%mm6, (%3);" /* Store ARGB1 ARGB0 */

		     "movq %%mm0, %%mm6;" /* B7 B6 B5 B4 B3 B2 B1 B0 */
		     "punpcklbw %%mm2, %%mm6;" /* G3 B3 G2 B2 G1 B1 G0 B0 */

		     "punpckhwd %%mm7, %%mm6;" /* 00 R3 G3 B3 00 R2 B3 G2 */
		     MOVNTQ " %%mm6, 8 (%3);" /* Store ARGB3 ARGB2 */

		     "punpckhbw %%mm2, %%mm4;" /* G7 B7 G6 B6 G5 B5 G4 B4 */
		     "punpckhbw %%mm3, %%mm5;" /* 00 R7 00 R6 00 R5 00 R4 */

		     "punpcklwd %%mm5, %%mm4;" /* 00 R5 B5 G5 00 R4 B4 G4 */
		     MOVNTQ " %%mm4, 16 (%3);" /* Store ARGB5 ARGB4 */

		     "movq %%mm0, %%mm4;" /* B7 B6 B5 B4 B3 B2 B1 B0 */
		     "punpckhbw %%mm2, %%mm4;" /* G7 B7 G6 B6 G5 B5 G4 B4 */

		     "punpckhwd %%mm5, %%mm4;" /* 00 R7 G7 B7 00 R6 B6 G6 */
		     MOVNTQ " %%mm4, 24 (%3);" /* Store ARGB7 ARGB6 */

		     "movd 4 (%1), %%mm0;" /* Load 4 Cb 00 00 00 00 u3 u2 u1 u0 */
		     "movd 4 (%2), %%mm1;" /* Load 4 Cr 00 00 00 00 v3 v2 v1 v0 */

		     "pxor %%mm4, %%mm4;" /* zero mm4 */
		     "movq 8 (%0), %%mm6;" /* Load 8 Y Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */

		     : : "r" (_py), "r" (_pu), "r" (_pv), "r" (_image));

	    _py += 8;
	    _pu += 4;
	    _pv += 4;
	    _image += 32;
	}

	if (!even) {
	    pu += uv_stride;
	    pv += uv_stride;
	}

	py += y_stride;
	image += rgb_stride;

	even = (!even);
    }

    __asm__ __volatile__ (EMMS);
}

yuv2rgb_fun RENAME(yuv2rgb_init) (int bpp, int mode)
{
    if (bpp == 15 && mode == MODE_RGB) return RENAME(yuv420_rgb15);
    if (bpp == 16 && mode == MODE_RGB) return RENAME(yuv420_rgb16);
    if (bpp == 24 && mode == MODE_RGB) return RENAME(yuv420_rgb24);
    if (bpp == 32 && mode == MODE_RGB) return RENAME(yuv420_argb32);
    return NULL; // Fallback to C.
}

