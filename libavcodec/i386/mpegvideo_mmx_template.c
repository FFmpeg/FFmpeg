/*
 * MPEG video MMX templates
 *
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
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
 */
#undef SPREADW
#undef PMAXW
#ifdef HAVE_MMX2
#define SPREADW(a) "pshufw $0, " #a ", " #a " \n\t"
#define PMAXW(a,b) "pmaxsw " #a ", " #b " \n\t"

#else
#define SPREADW(a) \
	"punpcklwd " #a ", " #a " \n\t"\
	"punpcklwd " #a ", " #a " \n\t"
#define PMAXW(a,b) \
	"psubusw " #a ", " #b " \n\t"\
	"paddw " #a ", " #b " \n\t"
#endif

static int RENAME(dct_quantize)(MpegEncContext *s,
                            DCTELEM *block, int n,
                            int qscale, int *overflow)
{
    int level=0, last_non_zero_p1, q; //=0 is cuz gcc says uninitalized ...
    const UINT16 *qmat, *bias;
    static __align8 INT16 temp_block[64];

    av_fdct (block);

    if (s->mb_intra) {
        int dummy;
        if (n < 4)
            q = s->y_dc_scale;
        else
            q = s->c_dc_scale;
        /* note: block[0] is assumed to be positive */
        if (!s->h263_aic) {
#if 1
        asm volatile (
        	"xorl %%edx, %%edx	\n\t"
        	"mul %%ecx		\n\t"
        	: "=d" (level), "=a"(dummy)
        	: "a" (block[0] + (q >> 1)), "c" (inverse[q])
        );
#else
        asm volatile (
        	"xorl %%edx, %%edx	\n\t"
        	"divw %%cx		\n\t"
        	"movzwl %%ax, %%eax	\n\t"
        	: "=a" (level)
        	: "a" (block[0] + (q >> 1)), "c" (q)
        	: "%edx"
        );
#endif
        } else
            /* For AIC we skip quant/dequant of INTRADC */
            level = block[0];
            
        block[0]=0; //avoid fake overflow
//        temp_block[0] = (block[0] + (q >> 1)) / q;
        last_non_zero_p1 = 1;
        bias = s->q_intra_matrix16_bias[qscale];
        qmat = s->q_intra_matrix16[qscale];
    } else {
        last_non_zero_p1 = 0;
        bias = s->q_inter_matrix16_bias[qscale];
        qmat = s->q_inter_matrix16[qscale];
    }

    if(s->out_format == FMT_H263){
    
        asm volatile(
            "movd %%eax, %%mm3			\n\t" // last_non_zero_p1
            SPREADW(%%mm3)
            "pxor %%mm7, %%mm7			\n\t" // 0
            "pxor %%mm4, %%mm4			\n\t" // 0
            "movq (%2), %%mm5			\n\t" // qmat[0]
            "pxor %%mm6, %%mm6			\n\t"
            "psubw (%3), %%mm6			\n\t" // -bias[0]
            "movl $-128, %%eax			\n\t"
            ".balign 16				\n\t"
            "1:					\n\t"
            "pxor %%mm1, %%mm1			\n\t" // 0
            "movq (%1, %%eax), %%mm0		\n\t" // block[i]
            "pcmpgtw %%mm0, %%mm1		\n\t" // block[i] <= 0 ? 0xFF : 0x00
            "pxor %%mm1, %%mm0			\n\t" 
            "psubw %%mm1, %%mm0			\n\t" // ABS(block[i])
            "psubusw %%mm6, %%mm0		\n\t" // ABS(block[i]) + bias[0]
            "pmulhw %%mm5, %%mm0		\n\t" // (ABS(block[i])*qmat[0] - bias[0]*qmat[0])>>16
            "por %%mm0, %%mm4			\n\t" 
            "pxor %%mm1, %%mm0			\n\t" 
            "psubw %%mm1, %%mm0			\n\t" // out=((ABS(block[i])*qmat[0] - bias[0]*qmat[0])>>16)*sign(block[i])
            "movq %%mm0, (%5, %%eax)		\n\t"
            "pcmpeqw %%mm7, %%mm0		\n\t" // out==0 ? 0xFF : 0x00
            "movq (%4, %%eax), %%mm1		\n\t" 
            "movq %%mm7, (%1, %%eax)		\n\t" // 0
            "pandn %%mm1, %%mm0			\n\t"
	    PMAXW(%%mm0, %%mm3)
            "addl $8, %%eax			\n\t"
            " js 1b				\n\t"
            "movq %%mm3, %%mm0			\n\t"
            "psrlq $32, %%mm3			\n\t"
	    PMAXW(%%mm0, %%mm3)
            "movq %%mm3, %%mm0			\n\t"
            "psrlq $16, %%mm3			\n\t"
	    PMAXW(%%mm0, %%mm3)
            "movd %%mm3, %%eax			\n\t"
            "movzbl %%al, %%eax			\n\t" // last_non_zero_p1
	    : "+a" (last_non_zero_p1)
            : "r" (block+64), "r" (qmat), "r" (bias),
              "r" (inv_zigzag_direct16+64), "r" (temp_block+64)
        );
        // note the asm is split cuz gcc doesnt like that many operands ...
        asm volatile(
            "movd %1, %%mm1			\n\t" // max_qcoeff
	    SPREADW(%%mm1)
            "psubusw %%mm1, %%mm4		\n\t" 
            "packuswb %%mm4, %%mm4		\n\t"
            "movd %%mm4, %0			\n\t" // *overflow
        : "=g" (*overflow)
        : "g" (s->max_qcoeff)
        );
    }else{ // FMT_H263
        asm volatile(
            "movd %%eax, %%mm3			\n\t" // last_non_zero_p1
            SPREADW(%%mm3)
            "pxor %%mm7, %%mm7			\n\t" // 0
            "pxor %%mm4, %%mm4			\n\t" // 0
            "movl $-128, %%eax			\n\t"
            ".balign 16				\n\t"
            "1:					\n\t"
            "pxor %%mm1, %%mm1			\n\t" // 0
            "movq (%1, %%eax), %%mm0		\n\t" // block[i]
            "pcmpgtw %%mm0, %%mm1		\n\t" // block[i] <= 0 ? 0xFF : 0x00
            "pxor %%mm1, %%mm0			\n\t" 
            "psubw %%mm1, %%mm0			\n\t" // ABS(block[i])
            "movq (%3, %%eax), %%mm6		\n\t" // bias[0]
            "paddusw %%mm6, %%mm0		\n\t" // ABS(block[i]) + bias[0]
            "movq (%2, %%eax), %%mm5		\n\t" // qmat[i]
            "pmulhw %%mm5, %%mm0		\n\t" // (ABS(block[i])*qmat[0] + bias[0]*qmat[0])>>16
            "por %%mm0, %%mm4			\n\t" 
            "pxor %%mm1, %%mm0			\n\t" 
            "psubw %%mm1, %%mm0			\n\t" // out=((ABS(block[i])*qmat[0] - bias[0]*qmat[0])>>16)*sign(block[i])
            "movq %%mm0, (%5, %%eax)		\n\t"
            "pcmpeqw %%mm7, %%mm0		\n\t" // out==0 ? 0xFF : 0x00
            "movq (%4, %%eax), %%mm1		\n\t" 
            "movq %%mm7, (%1, %%eax)		\n\t" // 0
            "pandn %%mm1, %%mm0			\n\t"
	    PMAXW(%%mm0, %%mm3)
            "addl $8, %%eax			\n\t"
            " js 1b				\n\t"
            "movq %%mm3, %%mm0			\n\t"
            "psrlq $32, %%mm3			\n\t"
	    PMAXW(%%mm0, %%mm3)
            "movq %%mm3, %%mm0			\n\t"
            "psrlq $16, %%mm3			\n\t"
	    PMAXW(%%mm0, %%mm3)
            "movd %%mm3, %%eax			\n\t"
            "movzbl %%al, %%eax			\n\t" // last_non_zero_p1
	    : "+a" (last_non_zero_p1)
            : "r" (block+64), "r" (qmat+64), "r" (bias+64),
              "r" (inv_zigzag_direct16+64), "r" (temp_block+64)
        );
        // note the asm is split cuz gcc doesnt like that many operands ...
        asm volatile(
            "movd %1, %%mm1			\n\t" // max_qcoeff
	    SPREADW(%%mm1)
            "psubusw %%mm1, %%mm4		\n\t" 
            "packuswb %%mm4, %%mm4		\n\t"
            "movd %%mm4, %0			\n\t" // *overflow
        : "=g" (*overflow)
        : "g" (s->max_qcoeff)
        );
    }

    if(s->mb_intra) temp_block[0]= level; //FIXME move afer permute
// last_non_zero_p1=64;       
    /* permute for IDCT */
    asm volatile(
        "movl %0, %%eax			\n\t"
	"pushl %%ebp			\n\t"
	"movl %%esp, " MANGLE(esp_temp) "\n\t"
	"1:				\n\t"
	"movzbl (%1, %%eax), %%ebx	\n\t"
	"movzbl 1(%1, %%eax), %%ebp	\n\t"
	"movw (%2, %%ebx, 2), %%cx	\n\t"
	"movw (%2, %%ebp, 2), %%sp	\n\t"
	"movzbl " MANGLE(permutation) "(%%ebx), %%ebx\n\t"
	"movzbl " MANGLE(permutation) "(%%ebp), %%ebp\n\t"
	"movw %%cx, (%3, %%ebx, 2)	\n\t"
	"movw %%sp, (%3, %%ebp, 2)	\n\t"
	"addl $2, %%eax			\n\t"
	" js 1b				\n\t"
	"movl " MANGLE(esp_temp) ", %%esp\n\t"
	"popl %%ebp			\n\t"
	: 
	: "g" (-last_non_zero_p1), "d" (zigzag_direct_noperm+last_non_zero_p1), "S" (temp_block), "D" (block)
	: "%eax", "%ebx", "%ecx"
	);
/*
    for(i=0; i<last_non_zero_p1; i++)
    {
       int j= zigzag_direct_noperm[i];
       block[block_permute_op(j)]= temp_block[j];
    }
*/
//block_permute(block);

    return last_non_zero_p1 - 1;
}
