/*
 * BlackFin MPEGVIDEO OPTIMIZATIONS
 *
 * Copyright (C) 2007 Marc Hoffman <mmh@pleasantst.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/mpegvideo.h"
#include "dsputil_bfin.h"


void ff_bfin_fdct (DCTELEM *block) attribute_l1_text;


static int dct_quantize_bfin (MpegEncContext *s,
                              DCTELEM *block, int n,
                              int qscale, int *overflow)
{
    int last_non_zero, q, start_i;
    const short *qmat;
    short *bias;
    const uint8_t *scantable= s->intra_scantable.scantable;
    short dc;
    int   max=0;

    PROF("fdct",0);
    ff_bfin_fdct (block);
    EPROF();

    PROF("denoise",1);
    if(s->dct_error_sum)
        s->denoise_dct(s, block);
    EPROF();

    PROF("quant-init",2);
    if (s->mb_intra) {
        if (!s->h263_aic) {
            if (n < 4)
                q = s->y_dc_scale;
            else
                q = s->c_dc_scale;
            q = q << 3;
        } else
            /* For AIC we skip quant/dequant of INTRADC */
            q = 1 << 3;

        /* note: block[0] is assumed to be positive */
        dc = block[0] = (block[0] + (q >> 1)) / q;
        start_i = 1;
        last_non_zero = 0;
        bias = s->q_intra_matrix16[qscale][1];
        qmat = s->q_intra_matrix16[qscale][0];

    } else {
        start_i = 0;
        last_non_zero = -1;
        bias = s->q_inter_matrix16[qscale][1];
        qmat = s->q_inter_matrix16[qscale][0];

    }
    EPROF();

    PROF("quantize",4);

    /*  for(i=start_i; i<64; i++) {                           */
    /*      sign     = (block[i]>>15)|1;                      */
    /*      level    = ((abs(block[i])+bias[0])*qmat[i])>>16; */
    /*      if (level < 0) level = 0;                         */
    /*      max     |= level;                                 */
    /*      level    = level * sign;                          */
    /*      block[i] = level;                                 */
    /*  } */

    __asm__ volatile
        ("i2=%1;\n\t"
         "r1=[%1++];                                                         \n\t"
         "r0=r1>>>15 (v);                                                    \n\t"
         "lsetup (0f,1f) lc0=%3;                                             \n\t"
         "0:   r0=r0|%4;                                                     \n\t"
         "     r1=abs r1 (v)                                    || r2=[%2++];\n\t"
         "     r1=r1+|+%5;                                                   \n\t"
         "     r1=max(r1,%6) (v);                                            \n\t"
         "     r1.h=(a1 =r1.h*r2.h), r1.l=(a0 =r1.l*r2.l) (tfu);             \n\t"
         "     %0=%0|r1;                                                     \n\t"
         "     r0.h=(a1 =r1.h*r0.h), r0.l=(a0 =r1.l*r0.l) (is)  || r1=[%1++];\n\t"
         "1:   r0=r1>>>15 (v)                                   || [i2++]=r0;\n\t"
         "r1=%0>>16;                                                         \n\t"
         "%0=%0|r1;                                                          \n\t"
         "%0.h=0;                                                            \n\t"
         : "=&d" (max)
         : "b" (block), "b" (qmat), "a" (32), "d" (0x00010001), "d" (bias[0]*0x10001), "d" (0)
         : "R0","R1","R2", "I2");
    if (start_i == 1) block[0] = dc;

    EPROF();


    PROF("zzscan",5);

    __asm__ volatile
        ("r0=b[%1--] (x);         \n\t"
         "lsetup (0f,1f) lc0=%3;  \n\t"     /*    for(i=63; i>=start_i; i--) { */
         "0: p0=r0;               \n\t"     /*        j = scantable[i];        */
         "   p0=%2+(p0<<1);       \n\t"     /*        if (block[j]) {          */
         "   r0=w[p0];            \n\t"     /*           last_non_zero = i;    */
         "   cc=r0==0;            \n\t"     /*           break;                */
         "   if !cc jump 2f;      \n\t"     /*        }                        */
         "1: r0=b[%1--] (x);      \n\t"     /*    }                            */
         "   %0=%4;               \n\t"
         "   jump 3f;             \n\t"
         "2: %0=lc0;              \n\t"
         "3:\n\t"

         : "=d" (last_non_zero)
         : "a" (scantable+63), "a" (block), "a" (63), "d" (last_non_zero)
         : "P0","R0");

    EPROF();

    *overflow= s->max_qcoeff < max; //overflow might have happened

    bfprof();

    /* we need this permutation so that we correct the IDCT, we only permute the !=0 elements */
    if (s->dsp.idct_permutation_type != FF_NO_IDCT_PERM)
        ff_block_permute(block, s->dsp.idct_permutation, scantable, last_non_zero);

    return last_non_zero;
}

void MPV_common_init_bfin (MpegEncContext *s)
{
    s->dct_quantize= dct_quantize_bfin;
}

