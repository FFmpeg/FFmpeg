/*
    Copyright (C) 2002 Michael Niedermayer <michaelni@gmx.at>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
                            int qscale)
{
    int i, level, last_non_zero_p1, q;
    const UINT16 *qmat;
    static __align8 INT16 temp_block[64];
    int minLevel, maxLevel;
    
    if(s->avctx!=NULL && s->avctx->codec->id==CODEC_ID_MPEG4){
	/* mpeg4 */
        minLevel= -2048;
	maxLevel= 2047;
    }else if(s->out_format==FMT_MPEG1){
	/* mpeg1 */
        minLevel= -255;
	maxLevel= 255;
    }else if(s->out_format==FMT_MJPEG){
	/* (m)jpeg */
        minLevel= -1023;
	maxLevel= 1023;
    }else{
	/* h263 / msmpeg4 */
        minLevel= -128;
	maxLevel= 127;
    }

    av_fdct (block);
    
    if (s->mb_intra) {
        int dummy;
        if (n < 4)
            q = s->y_dc_scale;
        else
            q = s->c_dc_scale;
        
        /* note: block[0] is assumed to be positive */
#if 1
	asm volatile (
		"xorl %%edx, %%edx	\n\t"
		"mul %%ecx		\n\t"
		: "=d" (temp_block[0]), "=a"(dummy)
		: "a" (block[0] + (q >> 1)), "c" (inverse[q])
	);
#else
	asm volatile (
		"xorl %%edx, %%edx	\n\t"
		"divw %%cx		\n\t"
		"movzwl %%ax, %%eax	\n\t"
		: "=a" (temp_block[0])
		: "a" (block[0] + (q >> 1)), "c" (q)
		: "%edx"
	);
#endif
//        temp_block[0] = (block[0] + (q >> 1)) / q;
        i = 1;
        last_non_zero_p1 = 1;
        if (s->out_format == FMT_H263) {
            qmat = s->q_non_intra_matrix16;
        } else {
            qmat = s->q_intra_matrix16;
        }
        for(i=1;i<4;i++) {
            level = block[i] * qmat[i];
            level = level / (1 << (QMAT_SHIFT_MMX - 3));
            /* XXX: currently, this code is not optimal. the range should be:
               mpeg1: -255..255
               mpeg2: -2048..2047
               h263:  -128..127
               mpeg4: -2048..2047
            */
            if (level > maxLevel)
                level = maxLevel;
            else if (level < minLevel)
                level = minLevel;
            temp_block[i] = level;

	    if(level) 
	        if(last_non_zero_p1 < inv_zigzag_direct16[i]) last_non_zero_p1= inv_zigzag_direct16[i];
	    block[i]=0;
        }
    } else {
        i = 0;
        last_non_zero_p1 = 0;
        qmat = s->q_non_intra_matrix16;
    }

    asm volatile( /* XXX: small rounding bug, but it shouldnt matter */
	"movd %3, %%mm3			\n\t"
	SPREADW(%%mm3)
	"movd %4, %%mm4			\n\t"
	SPREADW(%%mm4)
#ifndef HAVE_MMX2	
	"movd %5, %%mm5			\n\t"
	SPREADW(%%mm5)
#endif
	"pxor %%mm7, %%mm7		\n\t"
	"movd %%eax, %%mm2		\n\t"
	SPREADW(%%mm2)
	"movl %6, %%eax			\n\t"
	".balign 16			\n\t"
	"1:				\n\t"
	"movq (%1, %%eax), %%mm0	\n\t"
	"movq (%2, %%eax), %%mm1	\n\t"
	"movq %%mm0, %%mm6		\n\t"
	"psraw $15, %%mm6		\n\t"
	"pmulhw %%mm0, %%mm1		\n\t"
	"psubsw %%mm6, %%mm1		\n\t"
#ifdef HAVE_MMX2
	"pminsw %%mm3, %%mm1		\n\t"
	"pmaxsw %%mm4, %%mm1		\n\t"
#else
	"paddsw %%mm3, %%mm1		\n\t"
	"psubusw %%mm4, %%mm1		\n\t"
	"paddsw %%mm5, %%mm1		\n\t"
#endif
	"movq %%mm1, (%8, %%eax)	\n\t"
	"pcmpeqw %%mm7, %%mm1		\n\t"
	"movq (%7, %%eax), %%mm0	\n\t"
	"movq %%mm7, (%1, %%eax)	\n\t"
	"pandn %%mm0, %%mm1		\n\t"
	PMAXW(%%mm1, %%mm2)
	"addl $8, %%eax			\n\t"
	" js 1b				\n\t"
	"movq %%mm2, %%mm0		\n\t"
	"psrlq $32, %%mm2		\n\t"
	PMAXW(%%mm0, %%mm2)
	"movq %%mm2, %%mm0		\n\t"
	"psrlq $16, %%mm2		\n\t"
	PMAXW(%%mm0, %%mm2)
	"movd %%mm2, %%eax		\n\t"
	"movzbl %%al, %%eax		\n\t"
	: "+a" (last_non_zero_p1)
	: "r" (block+64), "r" (qmat+64), 
#ifdef HAVE_MMX2
	  "m" (maxLevel),          "m" (minLevel),                    "m" (minLevel /* dummy */), "g" (2*i - 128),
#else
	  "m" (0x7FFF - maxLevel), "m" (0x7FFF -maxLevel + minLevel), "m" (minLevel),             "g" (2*i - 128),
#endif
	  "r" (inv_zigzag_direct16+64), "r" (temp_block+64)
    );
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
