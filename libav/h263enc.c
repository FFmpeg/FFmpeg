/*
 * H263 backend for ffmpeg encoder
 * Copyright (c) 2000 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include "common.h"
#include "mpegvideo.h"
#include "h263data.h"

void h263_picture_header(MpegEncContext *s, int picture_number)
{
    int format;

    align_put_bits(&s->pb);
    put_bits(&s->pb, 22, 0x20);
    put_bits(&s->pb, 8, ((s->picture_number * 30) / s->frame_rate) & 0xff); 

    put_bits(&s->pb, 1, 1); /* marker */
    put_bits(&s->pb, 1, 0); /* h263 id */
    put_bits(&s->pb, 1, 0); /* split screen off */
    put_bits(&s->pb, 1, 0); /* camera  off */
    put_bits(&s->pb, 1, 0); /* freeze picture release off */

    if (s->width == 128 && s->height == 96)
        format = 1;
    else if (s->width == 176 && s->height == 144)
        format = 2;
    else if (s->width == 352 && s->height == 288)
        format = 3;
    else if (s->width == 704 && s->height == 576)
        format = 4;
    else if (s->width == 1408 && s->height == 1152)
        format = 5;
    else
        abort();

    put_bits(&s->pb, 3, format);
    
    put_bits(&s->pb, 1, (s->pict_type == P_TYPE));

    put_bits(&s->pb, 1, 0); /* unrestricted motion vector: off */

    put_bits(&s->pb, 1, 0); /* SAC: off */

    put_bits(&s->pb, 1, 0); /* advanced prediction mode: off */

    put_bits(&s->pb, 1, 0); /* not PB frame */

    put_bits(&s->pb, 5, s->qscale);
    
    put_bits(&s->pb, 1, 0); /* Continuous Presence Multipoint mode: off */
    
    put_bits(&s->pb, 1, 0); /* no PEI */
}

static void h263_encode_block(MpegEncContext *s, DCTELEM *block, 
                              int n);

void h263_encode_mb(MpegEncContext *s, 
                    DCTELEM block[6][64],
                    int motion_x, int motion_y)
{
    int cbpc, cbpy, i, cbp;

    if (!s->mb_intra) {
        /* compute cbp */
        cbp = 0;
        for(i=0;i<6;i++) {
            if (s->block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }
        if ((cbp | motion_x | motion_y) == 0) {
            /* skip macroblock */
            put_bits(&s->pb, 1, 1);
            return;
        }
            
        put_bits(&s->pb, 1, 0); /* mb coded */
        cbpc = cbp & 3;
        put_bits(&s->pb, 
                 inter_MCBPC_bits[cbpc], 
                 inter_MCBPC_code[cbpc]);
        cbpy = cbp >> 2;
        cbpy ^= 0xf;
        put_bits(&s->pb, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
        
        /* motion vectors: zero */
        put_bits(&s->pb, 1, 1);
        put_bits(&s->pb, 1, 1);

    } else {
        /* compute cbp */
        cbp = 0;
        for(i=0;i<6;i++) {
            if (s->block_last_index[i] >= 1)
                cbp |= 1 << (5 - i);
        }

        cbpc = cbp & 3;
        if (s->pict_type == I_TYPE) {
            put_bits(&s->pb, 
                     intra_MCBPC_bits[cbpc], 
                     intra_MCBPC_code[cbpc]);
        } else {
            put_bits(&s->pb, 1, 0); /* mb coded */
            put_bits(&s->pb, 
                     inter_MCBPC_bits[cbpc + 4], 
                     inter_MCBPC_code[cbpc + 4]);
        }
        cbpy = cbp >> 2;
        put_bits(&s->pb, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
    }
    
    /* encode each block */
    for(i=0;i<6;i++) {
        h263_encode_block(s, block[i], i);
    }
}

static void h263_encode_block(MpegEncContext *s, DCTELEM *block, int n)
{
    int level, run, last, i, j, last_index, last_non_zero, sign, alevel;
    int code, len;

    if (s->mb_intra) {
        /* DC coef */
        level = block[0];
        if (level == 128)
            put_bits(&s->pb, 8, 0xff);
        else
            put_bits(&s->pb, 8, level & 0xff);
        i = 1;
    } else {
        i = 0;
    }
    
    /* AC coefs */
    last_index = s->block_last_index[n];
    last_non_zero = i - 1;
    for(;i<=last_index;i++) {
        j = zigzag_direct[i];
        level = block[j];
        if (level) {
            run = i - last_non_zero - 1;
            last = (i == last_index);
            sign = 0;
            alevel = level;
            if (level < 0) {
                sign = 1;
                alevel = -level;
            }
            len = 0;
            code = 0; /* only to disable warning */
            if (last == 0) {
                if (run < 2 && alevel < 13 ) {
                    len = coeff_tab0[run][alevel-1][1];
                    code = coeff_tab0[run][alevel-1][0];
                } else if (run >= 2 && run < 27 && alevel < 5) {
                    len = coeff_tab1[run-2][alevel-1][1];
                    code = coeff_tab1[run-2][alevel-1][0];
                }
            } else {
                if (run < 2 && alevel < 4) {
                    len = coeff_tab2[run][alevel-1][1];
                    code = coeff_tab2[run][alevel-1][0];
                } else if (run >= 2 && run < 42 && alevel == 1) {
                    len = coeff_tab3[run-2][1];
                    code = coeff_tab3[run-2][0];
                }
            }
            
            if (len != 0) {
                code = (code << 1) | sign;
                put_bits(&s->pb, len + 1, code);
            } else {
                    /* escape */
                    put_bits(&s->pb, 7, 3);
                    put_bits(&s->pb, 1, last);
                    put_bits(&s->pb, 6, run);
                    put_bits(&s->pb, 8, level & 0xff);
            }

            last_non_zero = i;
        }
    }
}

/* write RV 1.0 compatible frame header */
void rv10_encode_picture_header(MpegEncContext *s, int picture_number)
{
    align_put_bits(&s->pb);

    put_bits(&s->pb, 1, 1); /* marker */

    put_bits(&s->pb, 1, (s->pict_type == P_TYPE));

    put_bits(&s->pb, 1, 0); /* not PB frame */

    put_bits(&s->pb, 5, s->qscale);
    
    if (s->pict_type == I_TYPE) {
        /* specific MPEG like DC coding not used */
    }
    
    /* if multiple packets per frame are sent, the position at which
       to display the macro blocks is coded here */
    put_bits(&s->pb, 6, 0); /* mb_x */
    put_bits(&s->pb, 6, 0); /* mb_y */
    put_bits(&s->pb, 12, s->mb_width * s->mb_height);
    
    put_bits(&s->pb, 3, 0); /* ignored */
}

