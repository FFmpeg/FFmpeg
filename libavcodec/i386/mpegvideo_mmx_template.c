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
    const uint16_t *qmat, *bias;
    __align8 int16_t temp_block[64];
    
    assert((7&(int)(&temp_block[0])) == 0); //did gcc align it correctly?

    //s->fdct (block);
    ff_fdct_mmx (block); //cant be anything else ...

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
        	"imul %%ecx		\n\t"
        	: "=d" (level), "=a"(dummy)
        	: "a" ((block[0]>>2) + q), "c" (inverse[q<<1])
        );
#else
        asm volatile (
        	"xorl %%edx, %%edx	\n\t"
        	"divw %%cx		\n\t"
        	"movzwl %%ax, %%eax	\n\t"
        	: "=a" (level)
        	: "a" ((block[0]>>2) + q), "c" (q<<1)
        	: "%edx"
        );
#endif
        } else
            /* For AIC we skip quant/dequant of INTRADC */
            level = (block[0] + 4)>>3;
            
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

    if(s->out_format == FMT_H263 && s->mpeg_quant==0){
    
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

    if(s->mb_intra) block[0]= level;
    else            block[0]= temp_block[0];

    if(s->dsp.idct_permutation_type == FF_SIMPLE_IDCT_PERM){
        if(last_non_zero_p1 <= 1) goto end;
        block[0x08] = temp_block[0x01]; block[0x10] = temp_block[0x08]; 
        block[0x20] = temp_block[0x10]; 
        if(last_non_zero_p1 <= 4) goto end;
        block[0x18] = temp_block[0x09]; block[0x04] = temp_block[0x02]; 
        block[0x09] = temp_block[0x03]; 
        if(last_non_zero_p1 <= 7) goto end;
        block[0x14] = temp_block[0x0A]; block[0x28] = temp_block[0x11]; 
        block[0x12] = temp_block[0x18]; block[0x02] = temp_block[0x20]; 
        if(last_non_zero_p1 <= 11) goto end;
        block[0x1A] = temp_block[0x19]; block[0x24] = temp_block[0x12]; 
        block[0x19] = temp_block[0x0B]; block[0x01] = temp_block[0x04]; 
        block[0x0C] = temp_block[0x05]; 
        if(last_non_zero_p1 <= 16) goto end;
        block[0x11] = temp_block[0x0C]; block[0x29] = temp_block[0x13]; 
        block[0x16] = temp_block[0x1A]; block[0x0A] = temp_block[0x21]; 
        block[0x30] = temp_block[0x28]; block[0x22] = temp_block[0x30]; 
        block[0x38] = temp_block[0x29]; block[0x06] = temp_block[0x22]; 
        if(last_non_zero_p1 <= 24) goto end;
        block[0x1B] = temp_block[0x1B]; block[0x21] = temp_block[0x14]; 
        block[0x1C] = temp_block[0x0D]; block[0x05] = temp_block[0x06]; 
        block[0x0D] = temp_block[0x07]; block[0x15] = temp_block[0x0E]; 
        block[0x2C] = temp_block[0x15]; block[0x13] = temp_block[0x1C]; 
        if(last_non_zero_p1 <= 32) goto end;
        block[0x0B] = temp_block[0x23]; block[0x34] = temp_block[0x2A]; 
        block[0x2A] = temp_block[0x31]; block[0x32] = temp_block[0x38]; 
        block[0x3A] = temp_block[0x39]; block[0x26] = temp_block[0x32]; 
        block[0x39] = temp_block[0x2B]; block[0x03] = temp_block[0x24]; 
        if(last_non_zero_p1 <= 40) goto end;
        block[0x1E] = temp_block[0x1D]; block[0x25] = temp_block[0x16]; 
        block[0x1D] = temp_block[0x0F]; block[0x2D] = temp_block[0x17]; 
        block[0x17] = temp_block[0x1E]; block[0x0E] = temp_block[0x25]; 
        block[0x31] = temp_block[0x2C]; block[0x2B] = temp_block[0x33]; 
        if(last_non_zero_p1 <= 48) goto end;
        block[0x36] = temp_block[0x3A]; block[0x3B] = temp_block[0x3B]; 
        block[0x23] = temp_block[0x34]; block[0x3C] = temp_block[0x2D]; 
        block[0x07] = temp_block[0x26]; block[0x1F] = temp_block[0x1F]; 
        block[0x0F] = temp_block[0x27]; block[0x35] = temp_block[0x2E]; 
        if(last_non_zero_p1 <= 56) goto end;
        block[0x2E] = temp_block[0x35]; block[0x33] = temp_block[0x3C]; 
        block[0x3E] = temp_block[0x3D]; block[0x27] = temp_block[0x36]; 
        block[0x3D] = temp_block[0x2F]; block[0x2F] = temp_block[0x37]; 
        block[0x37] = temp_block[0x3E]; block[0x3F] = temp_block[0x3F];
    }else if(s->dsp.idct_permutation_type == FF_LIBMPEG2_IDCT_PERM){
        if(last_non_zero_p1 <= 1) goto end;
        block[0x04] = temp_block[0x01]; 
        block[0x08] = temp_block[0x08]; block[0x10] = temp_block[0x10]; 
        if(last_non_zero_p1 <= 4) goto end;
        block[0x0C] = temp_block[0x09]; block[0x01] = temp_block[0x02]; 
        block[0x05] = temp_block[0x03]; 
        if(last_non_zero_p1 <= 7) goto end;
        block[0x09] = temp_block[0x0A]; block[0x14] = temp_block[0x11]; 
        block[0x18] = temp_block[0x18]; block[0x20] = temp_block[0x20]; 
        if(last_non_zero_p1 <= 11) goto end;
        block[0x1C] = temp_block[0x19]; 
        block[0x11] = temp_block[0x12]; block[0x0D] = temp_block[0x0B]; 
        block[0x02] = temp_block[0x04]; block[0x06] = temp_block[0x05]; 
        if(last_non_zero_p1 <= 16) goto end;
        block[0x0A] = temp_block[0x0C]; block[0x15] = temp_block[0x13]; 
        block[0x19] = temp_block[0x1A]; block[0x24] = temp_block[0x21]; 
        block[0x28] = temp_block[0x28]; block[0x30] = temp_block[0x30]; 
        block[0x2C] = temp_block[0x29]; block[0x21] = temp_block[0x22]; 
        if(last_non_zero_p1 <= 24) goto end;
        block[0x1D] = temp_block[0x1B]; block[0x12] = temp_block[0x14]; 
        block[0x0E] = temp_block[0x0D]; block[0x03] = temp_block[0x06]; 
        block[0x07] = temp_block[0x07]; block[0x0B] = temp_block[0x0E]; 
        block[0x16] = temp_block[0x15]; block[0x1A] = temp_block[0x1C]; 
        if(last_non_zero_p1 <= 32) goto end;
        block[0x25] = temp_block[0x23]; block[0x29] = temp_block[0x2A]; 
        block[0x34] = temp_block[0x31]; block[0x38] = temp_block[0x38]; 
        block[0x3C] = temp_block[0x39]; block[0x31] = temp_block[0x32]; 
        block[0x2D] = temp_block[0x2B]; block[0x22] = temp_block[0x24]; 
        if(last_non_zero_p1 <= 40) goto end;
        block[0x1E] = temp_block[0x1D]; block[0x13] = temp_block[0x16]; 
        block[0x0F] = temp_block[0x0F]; block[0x17] = temp_block[0x17]; 
        block[0x1B] = temp_block[0x1E]; block[0x26] = temp_block[0x25]; 
        block[0x2A] = temp_block[0x2C]; block[0x35] = temp_block[0x33]; 
        if(last_non_zero_p1 <= 48) goto end;
        block[0x39] = temp_block[0x3A]; block[0x3D] = temp_block[0x3B]; 
        block[0x32] = temp_block[0x34]; block[0x2E] = temp_block[0x2D]; 
            block[0x23] = temp_block[0x26]; block[0x1F] = temp_block[0x1F]; 
        block[0x27] = temp_block[0x27]; block[0x2B] = temp_block[0x2E]; 
        if(last_non_zero_p1 <= 56) goto end;
        block[0x36] = temp_block[0x35]; block[0x3A] = temp_block[0x3C]; 
        block[0x3E] = temp_block[0x3D]; block[0x33] = temp_block[0x36]; 
        block[0x2F] = temp_block[0x2F]; block[0x37] = temp_block[0x37]; 
        block[0x3B] = temp_block[0x3E]; block[0x3F] = temp_block[0x3F];
    }else{
        if(last_non_zero_p1 <= 1) goto end;
        block[0x01] = temp_block[0x01]; 
        block[0x08] = temp_block[0x08]; block[0x10] = temp_block[0x10]; 
        if(last_non_zero_p1 <= 4) goto end;
        block[0x09] = temp_block[0x09]; block[0x02] = temp_block[0x02]; 
        block[0x03] = temp_block[0x03]; 
        if(last_non_zero_p1 <= 7) goto end;
        block[0x0A] = temp_block[0x0A]; block[0x11] = temp_block[0x11]; 
        block[0x18] = temp_block[0x18]; block[0x20] = temp_block[0x20]; 
        if(last_non_zero_p1 <= 11) goto end;
        block[0x19] = temp_block[0x19]; 
        block[0x12] = temp_block[0x12]; block[0x0B] = temp_block[0x0B]; 
        block[0x04] = temp_block[0x04]; block[0x05] = temp_block[0x05]; 
        if(last_non_zero_p1 <= 16) goto end;
        block[0x0C] = temp_block[0x0C]; block[0x13] = temp_block[0x13]; 
        block[0x1A] = temp_block[0x1A]; block[0x21] = temp_block[0x21]; 
        block[0x28] = temp_block[0x28]; block[0x30] = temp_block[0x30]; 
        block[0x29] = temp_block[0x29]; block[0x22] = temp_block[0x22]; 
        if(last_non_zero_p1 <= 24) goto end;
        block[0x1B] = temp_block[0x1B]; block[0x14] = temp_block[0x14]; 
        block[0x0D] = temp_block[0x0D]; block[0x06] = temp_block[0x06]; 
        block[0x07] = temp_block[0x07]; block[0x0E] = temp_block[0x0E]; 
        block[0x15] = temp_block[0x15]; block[0x1C] = temp_block[0x1C]; 
        if(last_non_zero_p1 <= 32) goto end;
        block[0x23] = temp_block[0x23]; block[0x2A] = temp_block[0x2A]; 
        block[0x31] = temp_block[0x31]; block[0x38] = temp_block[0x38]; 
        block[0x39] = temp_block[0x39]; block[0x32] = temp_block[0x32]; 
        block[0x2B] = temp_block[0x2B]; block[0x24] = temp_block[0x24]; 
        if(last_non_zero_p1 <= 40) goto end;
        block[0x1D] = temp_block[0x1D]; block[0x16] = temp_block[0x16]; 
        block[0x0F] = temp_block[0x0F]; block[0x17] = temp_block[0x17]; 
        block[0x1E] = temp_block[0x1E]; block[0x25] = temp_block[0x25]; 
        block[0x2C] = temp_block[0x2C]; block[0x33] = temp_block[0x33]; 
        if(last_non_zero_p1 <= 48) goto end;
        block[0x3A] = temp_block[0x3A]; block[0x3B] = temp_block[0x3B]; 
        block[0x34] = temp_block[0x34]; block[0x2D] = temp_block[0x2D]; 
        block[0x26] = temp_block[0x26]; block[0x1F] = temp_block[0x1F]; 
        block[0x27] = temp_block[0x27]; block[0x2E] = temp_block[0x2E]; 
        if(last_non_zero_p1 <= 56) goto end;
        block[0x35] = temp_block[0x35]; block[0x3C] = temp_block[0x3C]; 
        block[0x3D] = temp_block[0x3D]; block[0x36] = temp_block[0x36]; 
        block[0x2F] = temp_block[0x2F]; block[0x37] = temp_block[0x37]; 
        block[0x3E] = temp_block[0x3E]; block[0x3F] = temp_block[0x3F];
    }
    end:
/*
    for(i=0; i<last_non_zero_p1; i++)
    {
       int j= zigzag_direct_noperm[i];
       block[block_permute_op(j)]= temp_block[j];
    }
*/

    return last_non_zero_p1 - 1;
}
