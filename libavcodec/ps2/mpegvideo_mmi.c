/*
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
 * MMI optimization by Leon van Stuivenberg
 */
 
#include "../dsputil.h"
#include "../mpegvideo.h"
#include "../avcodec.h"

static void dct_unquantize_h263_mmi(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale)
{
    int level=0, qmul, qadd;
    int nCoeffs;
    
    assert(s->block_last_index[n]>=0);
    
    qadd = (qscale - 1) | 1;
    qmul = qscale << 1;
    
    if (s->mb_intra) {
        if (!s->h263_aic) {
            if (n < 4) 
                level = block[0] * s->y_dc_scale;
            else
                level = block[0] * s->c_dc_scale;
        }else {
            qadd = 0;
	    level = block[0];
        }
        nCoeffs= 63; //does not allways use zigzag table 
    } else {
        nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ];
    }

    asm volatile(
        "add    $14, $0, %3	\n\t"
        "pcpyld $8, %0, %0	\n\t"	
        "pcpyh  $8, $8		\n\t"   //r8 = qmul
        "pcpyld $9, %1, %1	\n\t"	
        "pcpyh  $9, $9		\n\t"   //r9 = qadd
        ".p2align 2             \n\t"
        "1:			\n\t"
        "lq     $10, 0($14)	\n\t"   //r10 = level
        "addi   $14, $14, 16	\n\t"	//block+=8
        "addi   %2, %2, -8	\n\t"
        "pcgth  $11, $0, $10	\n\t"   //r11 = level < 0 ? -1 : 0
        "pcgth  $12, $10, $0	\n\t"   //r12 = level > 0 ? -1 : 0
        "por    $12, $11, $12	\n\t"
        "pmulth $10, $10, $8	\n\t"	
        "paddh  $13, $9, $11	\n\t"
        "pxor   $13, $13, $11   \n\t"   //r13 = level < 0 ? -qadd : qadd
        "pmfhl.uw $11		\n\t"
        "pinteh $10, $11, $10	\n\t"   //r10 = level * qmul
        "paddh  $10, $10, $13	\n\t"
        "pand   $10, $10, $12   \n\t"
        "sq     $10, -16($14)	\n\t"
        "bgez   %2, 1b		\n\t"
	:: "r"(qmul), "r" (qadd), "r" (nCoeffs), "r" (block) : "$8", "$9", "$10", "$11", "$12", "$13", "$14", "memory" );

    if(s->mb_intra)
        block[0]= level;
}


void MPV_common_init_mmi(MpegEncContext *s)
{
    s->dct_unquantize_h263_intra = 
    s->dct_unquantize_h263_inter = dct_unquantize_h263_mmi;
}


