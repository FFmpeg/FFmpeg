/*
 * MSMPEG4 backend for encoder and decoder
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * msmpeg4v1 & v2 stuff by Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * MSMPEG4 backend for encoder and decoder
 */

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "msmpeg4.h"
#include "libavutil/x86/asm.h"
#include "h263.h"
#include "mpeg4video.h"
#include "msmpeg4data.h"
#include "vc1data.h"
#include "libavutil/imgutils.h"

/*
 * You can also call this codec : MPEG4 with a twist !
 *
 * TODO:
 *        - (encoding) select best mv table (two choices)
 *        - (encoding) select best vlc/dc table
 */
//#define DEBUG

/* This table is practically identical to the one from h263
 * except that it is inverted. */
static av_cold void init_h263_dc_for_msmpeg4(void)
{
        int level, uni_code, uni_len;

        if(ff_v2_dc_chroma_table[255 + 256][1])
            return;

        for(level=-256; level<256; level++){
            int size, v, l;
            /* find number of bits */
            size = 0;
            v = abs(level);
            while (v) {
                v >>= 1;
                    size++;
            }

            if (level < 0)
                l= (-level) ^ ((1 << size) - 1);
            else
                l= level;

            /* luminance h263 */
            uni_code= ff_mpeg4_DCtab_lum[size][0];
            uni_len = ff_mpeg4_DCtab_lum[size][1];
            uni_code ^= (1<<uni_len)-1; //M$ does not like compatibility

            if (size > 0) {
                uni_code<<=size; uni_code|=l;
                uni_len+=size;
                if (size > 8){
                    uni_code<<=1; uni_code|=1;
                    uni_len++;
                }
            }
            ff_v2_dc_lum_table[level + 256][0] = uni_code;
            ff_v2_dc_lum_table[level + 256][1] = uni_len;

            /* chrominance h263 */
            uni_code= ff_mpeg4_DCtab_chrom[size][0];
            uni_len = ff_mpeg4_DCtab_chrom[size][1];
            uni_code ^= (1<<uni_len)-1; //M$ does not like compatibility

            if (size > 0) {
                uni_code<<=size; uni_code|=l;
                uni_len+=size;
                if (size > 8){
                    uni_code<<=1; uni_code|=1;
                    uni_len++;
                }
            }
            ff_v2_dc_chroma_table[level + 256][0] = uni_code;
            ff_v2_dc_chroma_table[level + 256][1] = uni_len;

        }
}

av_cold void ff_msmpeg4_common_init(MpegEncContext *s)
{
    switch(s->msmpeg4_version){
    case 1:
    case 2:
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
        break;
    case 3:
        if(s->workaround_bugs){
            s->y_dc_scale_table= ff_old_ff_y_dc_scale_table;
            s->c_dc_scale_table= ff_wmv1_c_dc_scale_table;
        } else{
            s->y_dc_scale_table= ff_mpeg4_y_dc_scale_table;
            s->c_dc_scale_table= ff_mpeg4_c_dc_scale_table;
        }
        break;
    case 4:
    case 5:
        s->y_dc_scale_table= ff_wmv1_y_dc_scale_table;
        s->c_dc_scale_table= ff_wmv1_c_dc_scale_table;
        break;
#if CONFIG_VC1_DECODER
    case 6:
        s->y_dc_scale_table= ff_wmv3_dc_scale_table;
        s->c_dc_scale_table= ff_wmv3_dc_scale_table;
        break;
#endif

    }


    if(s->msmpeg4_version>=4){
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable  , ff_wmv1_scantable[1]);
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_h_scantable, ff_wmv1_scantable[2]);
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_v_scantable, ff_wmv1_scantable[3]);
        ff_init_scantable(s->dsp.idct_permutation, &s->inter_scantable  , ff_wmv1_scantable[0]);
    }
    //Note the default tables are set in common_init in mpegvideo.c

    init_h263_dc_for_msmpeg4();
}

/* predict coded block */
int ff_msmpeg4_coded_block_pred(MpegEncContext * s, int n, uint8_t **coded_block_ptr)
{
    int xy, wrap, pred, a, b, c;

    xy = s->block_index[n];
    wrap = s->b8_stride;

    /* B C
     * A X
     */
    a = s->coded_block[xy - 1       ];
    b = s->coded_block[xy - 1 - wrap];
    c = s->coded_block[xy     - wrap];

    if (b == c) {
        pred = a;
    } else {
        pred = c;
    }

    /* store value */
    *coded_block_ptr = &s->coded_block[xy];

    return pred;
}

static int get_dc(uint8_t *src, int stride, int scale)
{
    int y;
    int sum=0;
    for(y=0; y<8; y++){
        int x;
        for(x=0; x<8; x++){
            sum+=src[x + y*stride];
        }
    }
    return FASTDIV((sum + (scale>>1)), scale);
}

/* dir = 0: left, dir = 1: top prediction */
int ff_msmpeg4_pred_dc(MpegEncContext *s, int n,
                       int16_t **dc_val_ptr, int *dir_ptr)
{
    int a, b, c, wrap, pred, scale;
    int16_t *dc_val;

    /* find prediction */
    if (n < 4) {
        scale = s->y_dc_scale;
    } else {
        scale = s->c_dc_scale;
    }

    wrap = s->block_wrap[n];
    dc_val= s->dc_val[0] + s->block_index[n];

    /* B C
     * A X
     */
    a = dc_val[ - 1];
    b = dc_val[ - 1 - wrap];
    c = dc_val[ - wrap];

    if(s->first_slice_line && (n&2)==0 && s->msmpeg4_version<4){
        b=c=1024;
    }

    /* XXX: the following solution consumes divisions, but it does not
       necessitate to modify mpegvideo.c. The problem comes from the
       fact they decided to store the quantized DC (which would lead
       to problems if Q could vary !) */
#if ARCH_X86 && HAVE_7REGS && HAVE_EBX_AVAILABLE
    __asm__ volatile(
        "movl %3, %%eax         \n\t"
        "shrl $1, %%eax         \n\t"
        "addl %%eax, %2         \n\t"
        "addl %%eax, %1         \n\t"
        "addl %0, %%eax         \n\t"
        "imull %4               \n\t"
        "movl %%edx, %0         \n\t"
        "movl %1, %%eax         \n\t"
        "imull %4               \n\t"
        "movl %%edx, %1         \n\t"
        "movl %2, %%eax         \n\t"
        "imull %4               \n\t"
        "movl %%edx, %2         \n\t"
        : "+b" (a), "+c" (b), "+D" (c)
        : "g" (scale), "S" (ff_inverse[scale])
        : "%eax", "%edx"
    );
#else
    /* #elif ARCH_ALPHA */
    /* Divisions are extremely costly on Alpha; optimize the most
       common case. But they are costly everywhere...
     */
    if (scale == 8) {
        a = (a + (8 >> 1)) / 8;
        b = (b + (8 >> 1)) / 8;
        c = (c + (8 >> 1)) / 8;
    } else {
        a = FASTDIV((a + (scale >> 1)), scale);
        b = FASTDIV((b + (scale >> 1)), scale);
        c = FASTDIV((c + (scale >> 1)), scale);
    }
#endif
    /* XXX: WARNING: they did not choose the same test as MPEG4. This
       is very important ! */
    if(s->msmpeg4_version>3){
        if(s->inter_intra_pred){
            uint8_t *dest;
            int wrap;

            if(n==1){
                pred=a;
                *dir_ptr = 0;
            }else if(n==2){
                pred=c;
                *dir_ptr = 1;
            }else if(n==3){
                if (abs(a - b) < abs(b - c)) {
                    pred = c;
                    *dir_ptr = 1;
                } else {
                    pred = a;
                    *dir_ptr = 0;
                }
            }else{
                if(n<4){
                    wrap= s->linesize;
                    dest= s->current_picture.f.data[0] + (((n >> 1) + 2*s->mb_y) * 8*  wrap ) + ((n & 1) + 2*s->mb_x) * 8;
                }else{
                    wrap= s->uvlinesize;
                    dest= s->current_picture.f.data[n - 3] + (s->mb_y * 8 * wrap) + s->mb_x * 8;
                }
                if(s->mb_x==0) a= (1024 + (scale>>1))/scale;
                else           a= get_dc(dest-8, wrap, scale*8);
                if(s->mb_y==0) c= (1024 + (scale>>1))/scale;
                else           c= get_dc(dest-8*wrap, wrap, scale*8);

                if (s->h263_aic_dir==0) {
                    pred= a;
                    *dir_ptr = 0;
                }else if (s->h263_aic_dir==1) {
                    if(n==0){
                        pred= c;
                        *dir_ptr = 1;
                    }else{
                        pred= a;
                        *dir_ptr = 0;
                    }
                }else if (s->h263_aic_dir==2) {
                    if(n==0){
                        pred= a;
                        *dir_ptr = 0;
                    }else{
                        pred= c;
                        *dir_ptr = 1;
                    }
                } else {
                    pred= c;
                    *dir_ptr = 1;
                }
            }
        }else{
            if (abs(a - b) < abs(b - c)) {
                pred = c;
                *dir_ptr = 1;
            } else {
                pred = a;
                *dir_ptr = 0;
            }
        }
    }else{
        if (abs(a - b) <= abs(b - c)) {
            pred = c;
            *dir_ptr = 1;
        } else {
            pred = a;
            *dir_ptr = 0;
        }
    }

    /* update predictor */
    *dc_val_ptr = &dc_val[0];
    return pred;
}

