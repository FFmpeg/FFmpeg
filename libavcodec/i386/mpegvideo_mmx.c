/*
 * The simplest mpeg encoder (well, it was the simplest!)
 * Copyright (c) 2000,2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Optimized for ia32 cpus by Nick Kurshev <nickols_k@mail.ru>
 * h263, mpeg1, mpeg2 dequantizer & draw_edges by Michael Niedermayer <michaelni@gmx.at>
 */

#include "../dsputil.h"
#include "../mpegvideo.h"
#include "../avcodec.h"

extern uint8_t zigzag_direct_noperm[64];
extern uint16_t inv_zigzag_direct16[64];

static const unsigned long long int mm_wabs __attribute__ ((aligned(8))) = 0xffffffffffffffffULL;
static const unsigned long long int mm_wone __attribute__ ((aligned(8))) = 0x0001000100010001ULL;


static void dct_unquantize_h263_mmx(MpegEncContext *s,
                                  DCTELEM *block, int n, int qscale)
{
    int level, qmul, qadd, nCoeffs;

    qmul = qscale << 1;
    qadd = (qscale - 1) | 1;

    assert(s->block_last_index[n]>=0);
        
    if (s->mb_intra) {
        if (!s->h263_aic) {
            if (n < 4)
                level = block[0] * s->y_dc_scale;
            else
                level = block[0] * s->c_dc_scale;
        }else{
            qadd = 0;
            level= block[0];
        }
        nCoeffs=63;
    } else {
	nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];
        level = 0;/* keep gcc quiet */
    }
//printf("%d %d  ", qmul, qadd);
asm volatile(
		"movd %1, %%mm6			\n\t" //qmul
		"packssdw %%mm6, %%mm6		\n\t"
		"packssdw %%mm6, %%mm6		\n\t"
		"movd %2, %%mm5			\n\t" //qadd
		"pxor %%mm7, %%mm7		\n\t"
		"packssdw %%mm5, %%mm5		\n\t"
		"packssdw %%mm5, %%mm5		\n\t"
		"psubw %%mm5, %%mm7		\n\t"
		"pxor %%mm4, %%mm4		\n\t"
		".balign 16\n\t"
		"1:				\n\t"
		"movq (%0, %3), %%mm0		\n\t"
		"movq 8(%0, %3), %%mm1		\n\t"

		"pmullw %%mm6, %%mm0		\n\t"
		"pmullw %%mm6, %%mm1		\n\t"

		"movq (%0, %3), %%mm2		\n\t"
		"movq 8(%0, %3), %%mm3		\n\t"

		"pcmpgtw %%mm4, %%mm2		\n\t" // block[i] < 0 ? -1 : 0
		"pcmpgtw %%mm4, %%mm3		\n\t" // block[i] < 0 ? -1 : 0

		"pxor %%mm2, %%mm0		\n\t"
		"pxor %%mm3, %%mm1		\n\t"

		"paddw %%mm7, %%mm0		\n\t"
		"paddw %%mm7, %%mm1		\n\t"

		"pxor %%mm0, %%mm2		\n\t"
		"pxor %%mm1, %%mm3		\n\t"

		"pcmpeqw %%mm7, %%mm0		\n\t" // block[i] == 0 ? -1 : 0
		"pcmpeqw %%mm7, %%mm1		\n\t" // block[i] == 0 ? -1 : 0

		"pandn %%mm2, %%mm0		\n\t"
		"pandn %%mm3, %%mm1		\n\t"

		"movq %%mm0, (%0, %3)		\n\t"
		"movq %%mm1, 8(%0, %3)		\n\t"

		"addl $16, %3			\n\t"
		"jng 1b				\n\t"
		::"r" (block+nCoeffs), "g"(qmul), "g" (qadd), "r" (2*(-nCoeffs))
		: "memory"
	);
        if(s->mb_intra)
            block[0]= level;
}


/*
  NK:
  Note: looking at PARANOID:
  "enable all paranoid tests for rounding, overflows, etc..."

#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
  We can suppose that result of two multiplications can't be greate of 0xFFFF
  i.e. is 16-bit, so we use here only PMULLW instruction and can avoid
  a complex multiplication.
=====================================================
 Full formula for multiplication of 2 integer numbers
 which are represent as high:low words:
 input: value1 = high1:low1
        value2 = high2:low2
 output: value3 = value1*value2
 value3=high3:low3 (on overflow: modulus 2^32 wrap-around)
 this mean that for 0x123456 * 0x123456 correct result is 0x766cb0ce4
 but this algorithm will compute only 0x66cb0ce4
 this limited by 16-bit size of operands
 ---------------------------------
 tlow1 = high1*low2
 tlow2 = high2*low1
 tlow1 = tlow1 + tlow2
 high3:low3 = low1*low2
 high3 += tlow1
*/
static void dct_unquantize_mpeg1_mmx(MpegEncContext *s,
                                     DCTELEM *block, int n, int qscale)
{
    int nCoeffs;
    const uint16_t *quant_matrix;

    assert(s->block_last_index[n]>=0);

    nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ]+1;

    if (s->mb_intra) {
        int block0;
        if (n < 4) 
            block0 = block[0] * s->y_dc_scale;
        else
            block0 = block[0] * s->c_dc_scale;
        /* XXX: only mpeg1 */
        quant_matrix = s->intra_matrix;
asm volatile(
		"pcmpeqw %%mm7, %%mm7		\n\t"
		"psrlw $15, %%mm7		\n\t"
		"movd %2, %%mm6			\n\t"
		"packssdw %%mm6, %%mm6		\n\t"
		"packssdw %%mm6, %%mm6		\n\t"
                "movl %3, %%eax			\n\t"
		".balign 16\n\t"
		"1:				\n\t"
		"movq (%0, %%eax), %%mm0	\n\t"
		"movq 8(%0, %%eax), %%mm1	\n\t"
		"movq (%1, %%eax), %%mm4	\n\t"
		"movq 8(%1, %%eax), %%mm5	\n\t"
		"pmullw %%mm6, %%mm4		\n\t" // q=qscale*quant_matrix[i]
		"pmullw %%mm6, %%mm5		\n\t" // q=qscale*quant_matrix[i]
		"pxor %%mm2, %%mm2		\n\t"
		"pxor %%mm3, %%mm3		\n\t"
		"pcmpgtw %%mm0, %%mm2		\n\t" // block[i] < 0 ? -1 : 0
		"pcmpgtw %%mm1, %%mm3		\n\t" // block[i] < 0 ? -1 : 0
		"pxor %%mm2, %%mm0		\n\t"
		"pxor %%mm3, %%mm1		\n\t"
		"psubw %%mm2, %%mm0		\n\t" // abs(block[i])
		"psubw %%mm3, %%mm1		\n\t" // abs(block[i])
		"pmullw %%mm4, %%mm0		\n\t" // abs(block[i])*q
		"pmullw %%mm5, %%mm1		\n\t" // abs(block[i])*q
		"pxor %%mm4, %%mm4		\n\t"
		"pxor %%mm5, %%mm5		\n\t" // FIXME slow
		"pcmpeqw (%0, %%eax), %%mm4	\n\t" // block[i] == 0 ? -1 : 0
		"pcmpeqw 8(%0, %%eax), %%mm5	\n\t" // block[i] == 0 ? -1 : 0
		"psraw $3, %%mm0		\n\t"
		"psraw $3, %%mm1		\n\t"
		"psubw %%mm7, %%mm0		\n\t"
		"psubw %%mm7, %%mm1		\n\t"
		"por %%mm7, %%mm0		\n\t"
		"por %%mm7, %%mm1		\n\t"
		"pxor %%mm2, %%mm0		\n\t"
		"pxor %%mm3, %%mm1		\n\t"
		"psubw %%mm2, %%mm0		\n\t"
		"psubw %%mm3, %%mm1		\n\t"
		"pandn %%mm0, %%mm4		\n\t"
		"pandn %%mm1, %%mm5		\n\t"
		"movq %%mm4, (%0, %%eax)	\n\t"
		"movq %%mm5, 8(%0, %%eax)	\n\t"

		"addl $16, %%eax		\n\t"
		"js 1b				\n\t"
		::"r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "g" (qscale), "g" (-2*nCoeffs)
		: "%eax", "memory"
	);    
        block[0]= block0;

        } else {
        quant_matrix = s->inter_matrix;
asm volatile(
		"pcmpeqw %%mm7, %%mm7		\n\t"
		"psrlw $15, %%mm7		\n\t"
		"movd %2, %%mm6			\n\t"
		"packssdw %%mm6, %%mm6		\n\t"
		"packssdw %%mm6, %%mm6		\n\t"
                "movl %3, %%eax			\n\t"
		".balign 16\n\t"
		"1:				\n\t"
		"movq (%0, %%eax), %%mm0	\n\t"
		"movq 8(%0, %%eax), %%mm1	\n\t"
		"movq (%1, %%eax), %%mm4	\n\t"
		"movq 8(%1, %%eax), %%mm5	\n\t"
		"pmullw %%mm6, %%mm4		\n\t" // q=qscale*quant_matrix[i]
		"pmullw %%mm6, %%mm5		\n\t" // q=qscale*quant_matrix[i]
		"pxor %%mm2, %%mm2		\n\t"
		"pxor %%mm3, %%mm3		\n\t"
		"pcmpgtw %%mm0, %%mm2		\n\t" // block[i] < 0 ? -1 : 0
		"pcmpgtw %%mm1, %%mm3		\n\t" // block[i] < 0 ? -1 : 0
		"pxor %%mm2, %%mm0		\n\t"
		"pxor %%mm3, %%mm1		\n\t"
		"psubw %%mm2, %%mm0		\n\t" // abs(block[i])
		"psubw %%mm3, %%mm1		\n\t" // abs(block[i])
		"paddw %%mm0, %%mm0		\n\t" // abs(block[i])*2
		"paddw %%mm1, %%mm1		\n\t" // abs(block[i])*2
		"paddw %%mm7, %%mm0		\n\t" // abs(block[i])*2 + 1
		"paddw %%mm7, %%mm1		\n\t" // abs(block[i])*2 + 1
		"pmullw %%mm4, %%mm0		\n\t" // (abs(block[i])*2 + 1)*q
		"pmullw %%mm5, %%mm1		\n\t" // (abs(block[i])*2 + 1)*q
		"pxor %%mm4, %%mm4		\n\t"
		"pxor %%mm5, %%mm5		\n\t" // FIXME slow
		"pcmpeqw (%0, %%eax), %%mm4	\n\t" // block[i] == 0 ? -1 : 0
		"pcmpeqw 8(%0, %%eax), %%mm5	\n\t" // block[i] == 0 ? -1 : 0
		"psraw $4, %%mm0		\n\t"
		"psraw $4, %%mm1		\n\t"
		"psubw %%mm7, %%mm0		\n\t"
		"psubw %%mm7, %%mm1		\n\t"
		"por %%mm7, %%mm0		\n\t"
		"por %%mm7, %%mm1		\n\t"
		"pxor %%mm2, %%mm0		\n\t"
		"pxor %%mm3, %%mm1		\n\t"
		"psubw %%mm2, %%mm0		\n\t"
		"psubw %%mm3, %%mm1		\n\t"
		"pandn %%mm0, %%mm4		\n\t"
		"pandn %%mm1, %%mm5		\n\t"
		"movq %%mm4, (%0, %%eax)	\n\t"
		"movq %%mm5, 8(%0, %%eax)	\n\t"

		"addl $16, %%eax		\n\t"
		"js 1b				\n\t"
		::"r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "g" (qscale), "g" (-2*nCoeffs)
		: "%eax", "memory"
	);
    }

}

static void dct_unquantize_mpeg2_mmx(MpegEncContext *s,
                                     DCTELEM *block, int n, int qscale)
{
    int nCoeffs;
    const uint16_t *quant_matrix;
    
    assert(s->block_last_index[n]>=0);

    if(s->alternate_scan) nCoeffs= 63; //FIXME
    else nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ];

    if (s->mb_intra) {
        int block0;
        if (n < 4) 
            block0 = block[0] * s->y_dc_scale;
        else
            block0 = block[0] * s->c_dc_scale;
        quant_matrix = s->intra_matrix;
asm volatile(
		"pcmpeqw %%mm7, %%mm7		\n\t"
		"psrlw $15, %%mm7		\n\t"
		"movd %2, %%mm6			\n\t"
		"packssdw %%mm6, %%mm6		\n\t"
		"packssdw %%mm6, %%mm6		\n\t"
                "movl %3, %%eax			\n\t"
		".balign 16\n\t"
		"1:				\n\t"
		"movq (%0, %%eax), %%mm0	\n\t"
		"movq 8(%0, %%eax), %%mm1	\n\t"
		"movq (%1, %%eax), %%mm4	\n\t"
		"movq 8(%1, %%eax), %%mm5	\n\t"
		"pmullw %%mm6, %%mm4		\n\t" // q=qscale*quant_matrix[i]
		"pmullw %%mm6, %%mm5		\n\t" // q=qscale*quant_matrix[i]
		"pxor %%mm2, %%mm2		\n\t"
		"pxor %%mm3, %%mm3		\n\t"
		"pcmpgtw %%mm0, %%mm2		\n\t" // block[i] < 0 ? -1 : 0
		"pcmpgtw %%mm1, %%mm3		\n\t" // block[i] < 0 ? -1 : 0
		"pxor %%mm2, %%mm0		\n\t"
		"pxor %%mm3, %%mm1		\n\t"
		"psubw %%mm2, %%mm0		\n\t" // abs(block[i])
		"psubw %%mm3, %%mm1		\n\t" // abs(block[i])
		"pmullw %%mm4, %%mm0		\n\t" // abs(block[i])*q
		"pmullw %%mm5, %%mm1		\n\t" // abs(block[i])*q
		"pxor %%mm4, %%mm4		\n\t"
		"pxor %%mm5, %%mm5		\n\t" // FIXME slow
		"pcmpeqw (%0, %%eax), %%mm4	\n\t" // block[i] == 0 ? -1 : 0
		"pcmpeqw 8(%0, %%eax), %%mm5	\n\t" // block[i] == 0 ? -1 : 0
		"psraw $3, %%mm0		\n\t"
		"psraw $3, %%mm1		\n\t"
		"pxor %%mm2, %%mm0		\n\t"
		"pxor %%mm3, %%mm1		\n\t"
		"psubw %%mm2, %%mm0		\n\t"
		"psubw %%mm3, %%mm1		\n\t"
		"pandn %%mm0, %%mm4		\n\t"
		"pandn %%mm1, %%mm5		\n\t"
		"movq %%mm4, (%0, %%eax)	\n\t"
		"movq %%mm5, 8(%0, %%eax)	\n\t"

		"addl $16, %%eax		\n\t"
		"jng 1b				\n\t"
		::"r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "g" (qscale), "g" (-2*nCoeffs)
		: "%eax", "memory"
	);    
        block[0]= block0;
        //Note, we dont do mismatch control for intra as errors cannot accumulate

    } else {
        quant_matrix = s->inter_matrix;
asm volatile(
		"pcmpeqw %%mm7, %%mm7		\n\t"
                "psrlq $48, %%mm7		\n\t"
		"movd %2, %%mm6			\n\t"
		"packssdw %%mm6, %%mm6		\n\t"
		"packssdw %%mm6, %%mm6		\n\t"
                "movl %3, %%eax			\n\t"
		".balign 16\n\t"
		"1:				\n\t"
		"movq (%0, %%eax), %%mm0	\n\t"
		"movq 8(%0, %%eax), %%mm1	\n\t"
		"movq (%1, %%eax), %%mm4	\n\t"
		"movq 8(%1, %%eax), %%mm5	\n\t"
		"pmullw %%mm6, %%mm4		\n\t" // q=qscale*quant_matrix[i]
		"pmullw %%mm6, %%mm5		\n\t" // q=qscale*quant_matrix[i]
		"pxor %%mm2, %%mm2		\n\t"
		"pxor %%mm3, %%mm3		\n\t"
		"pcmpgtw %%mm0, %%mm2		\n\t" // block[i] < 0 ? -1 : 0
		"pcmpgtw %%mm1, %%mm3		\n\t" // block[i] < 0 ? -1 : 0
		"pxor %%mm2, %%mm0		\n\t"
		"pxor %%mm3, %%mm1		\n\t"
		"psubw %%mm2, %%mm0		\n\t" // abs(block[i])
		"psubw %%mm3, %%mm1		\n\t" // abs(block[i])
		"paddw %%mm0, %%mm0		\n\t" // abs(block[i])*2
		"paddw %%mm1, %%mm1		\n\t" // abs(block[i])*2
		"pmullw %%mm4, %%mm0		\n\t" // abs(block[i])*2*q
		"pmullw %%mm5, %%mm1		\n\t" // abs(block[i])*2*q
		"paddw %%mm4, %%mm0		\n\t" // (abs(block[i])*2 + 1)*q
		"paddw %%mm5, %%mm1		\n\t" // (abs(block[i])*2 + 1)*q
		"pxor %%mm4, %%mm4		\n\t"
		"pxor %%mm5, %%mm5		\n\t" // FIXME slow
		"pcmpeqw (%0, %%eax), %%mm4	\n\t" // block[i] == 0 ? -1 : 0
		"pcmpeqw 8(%0, %%eax), %%mm5	\n\t" // block[i] == 0 ? -1 : 0
		"psrlw $4, %%mm0		\n\t"
		"psrlw $4, %%mm1		\n\t"
		"pxor %%mm2, %%mm0		\n\t"
		"pxor %%mm3, %%mm1		\n\t"
		"psubw %%mm2, %%mm0		\n\t"
		"psubw %%mm3, %%mm1		\n\t"
		"pandn %%mm0, %%mm4		\n\t"
		"pandn %%mm1, %%mm5		\n\t"
                "pxor %%mm4, %%mm7		\n\t"
                "pxor %%mm5, %%mm7		\n\t"
		"movq %%mm4, (%0, %%eax)	\n\t"
		"movq %%mm5, 8(%0, %%eax)	\n\t"

		"addl $16, %%eax		\n\t"
		"jng 1b				\n\t"
                "movd 124(%0, %3), %%mm0	\n\t"
                "movq %%mm7, %%mm6		\n\t"
                "psrlq $32, %%mm7		\n\t"
                "pxor %%mm6, %%mm7		\n\t"
                "movq %%mm7, %%mm6		\n\t"
                "psrlq $16, %%mm7		\n\t"
                "pxor %%mm6, %%mm7		\n\t"
                "pslld $31, %%mm7		\n\t"
                "psrlq $15, %%mm7		\n\t"
                "pxor %%mm7, %%mm0		\n\t"
                "movd %%mm0, 124(%0, %3)	\n\t"
                
		::"r" (block+nCoeffs), "r"(quant_matrix+nCoeffs), "g" (qscale), "r" (-2*nCoeffs)
		: "%eax", "memory"
	);
    }
}

/* draw the edges of width 'w' of an image of size width, height 
   this mmx version can only handle w==8 || w==16 */
static void draw_edges_mmx(uint8_t *buf, int wrap, int width, int height, int w)
{
    uint8_t *ptr, *last_line;
    int i;

    last_line = buf + (height - 1) * wrap;
    /* left and right */
    ptr = buf;
    if(w==8)
    {
	asm volatile(
		"1:				\n\t"
		"movd (%0), %%mm0		\n\t"
		"punpcklbw %%mm0, %%mm0		\n\t" 
		"punpcklwd %%mm0, %%mm0		\n\t"
		"punpckldq %%mm0, %%mm0		\n\t"
		"movq %%mm0, -8(%0)		\n\t"
		"movq -8(%0, %2), %%mm1		\n\t"
		"punpckhbw %%mm1, %%mm1		\n\t"
		"punpckhwd %%mm1, %%mm1		\n\t"
		"punpckhdq %%mm1, %%mm1		\n\t"
		"movq %%mm1, (%0, %2)		\n\t"
		"addl %1, %0			\n\t"
		"cmpl %3, %0			\n\t"
		" jb 1b				\n\t"
		: "+r" (ptr)
		: "r" (wrap), "r" (width), "r" (ptr + wrap*height)
	);
    }
    else
    {
	asm volatile(
		"1:				\n\t"
		"movd (%0), %%mm0		\n\t"
		"punpcklbw %%mm0, %%mm0		\n\t" 
		"punpcklwd %%mm0, %%mm0		\n\t"
		"punpckldq %%mm0, %%mm0		\n\t"
		"movq %%mm0, -8(%0)		\n\t"
		"movq %%mm0, -16(%0)		\n\t"
		"movq -8(%0, %2), %%mm1		\n\t"
		"punpckhbw %%mm1, %%mm1		\n\t"
		"punpckhwd %%mm1, %%mm1		\n\t"
		"punpckhdq %%mm1, %%mm1		\n\t"
		"movq %%mm1, (%0, %2)		\n\t"
		"movq %%mm1, 8(%0, %2)		\n\t"
		"addl %1, %0			\n\t"
		"cmpl %3, %0			\n\t"
		" jb 1b				\n\t"		
		: "+r" (ptr)
		: "r" (wrap), "r" (width), "r" (ptr + wrap*height)
	);
    }
    
    for(i=0;i<w;i+=4) {
        /* top and bottom (and hopefully also the corners) */
	ptr= buf - (i + 1) * wrap - w;
	asm volatile(
		"1:				\n\t"
		"movq (%1, %0), %%mm0		\n\t"
		"movq %%mm0, (%0)		\n\t"
		"movq %%mm0, (%0, %2)		\n\t"
		"movq %%mm0, (%0, %2, 2)	\n\t"
		"movq %%mm0, (%0, %3)		\n\t"
		"addl $8, %0			\n\t"
		"cmpl %4, %0			\n\t"
		" jb 1b				\n\t"
		: "+r" (ptr)
		: "r" ((int)buf - (int)ptr - w), "r" (-wrap), "r" (-wrap*3), "r" (ptr+width+2*w)
	);
	ptr= last_line + (i + 1) * wrap - w;
	asm volatile(
		"1:				\n\t"
		"movq (%1, %0), %%mm0		\n\t"
		"movq %%mm0, (%0)		\n\t"
		"movq %%mm0, (%0, %2)		\n\t"
		"movq %%mm0, (%0, %2, 2)	\n\t"
		"movq %%mm0, (%0, %3)		\n\t"
		"addl $8, %0			\n\t"
		"cmpl %4, %0			\n\t"
		" jb 1b				\n\t"
		: "+r" (ptr)
		: "r" ((int)last_line - (int)ptr - w), "r" (wrap), "r" (wrap*3), "r" (ptr+width+2*w)
	);
    }
}

#undef HAVE_MMX2
#define RENAME(a) a ## _MMX
#include "mpegvideo_mmx_template.c"

#define HAVE_MMX2
#undef RENAME
#define RENAME(a) a ## _MMX2
#include "mpegvideo_mmx_template.c"

void MPV_common_init_mmx(MpegEncContext *s)
{
    if (mm_flags & MM_MMX) {
        const int dct_algo = s->avctx->dct_algo;
        
        s->dct_unquantize_h263 = dct_unquantize_h263_mmx;
        s->dct_unquantize_mpeg1 = dct_unquantize_mpeg1_mmx;
        s->dct_unquantize_mpeg2 = dct_unquantize_mpeg2_mmx;

        draw_edges = draw_edges_mmx;

        if(dct_algo==FF_DCT_AUTO || dct_algo==FF_DCT_MMX){
            if(mm_flags & MM_MMXEXT){
                s->dct_quantize= dct_quantize_MMX2;
            } else {
                s->dct_quantize= dct_quantize_MMX;
            }
        }
    }
}
