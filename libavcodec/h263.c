/*
 * H263/MPEG4 backend for ffmpeg encoder and decoder
 * Copyright (c) 2000,2001 Gerard Lantau.
 * H263+ support for custom picture format.
 * Copyright (c) 2001 Juan J. Sierralta P.
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
#include <string.h>
#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h263data.h"
#include "mpeg4data.h"

#define NDEBUG
#include <assert.h>

static void h263_encode_block(MpegEncContext * s, DCTELEM * block,
			      int n);
static void h263_encode_motion(MpegEncContext * s, int val);
static void mpeg4_encode_block(MpegEncContext * s, DCTELEM * block,
			       int n);
static int h263_decode_motion(MpegEncContext * s, int pred);
static int h263_decode_block(MpegEncContext * s, DCTELEM * block,
                             int n, int coded);
static int mpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                              int n, int coded);

int h263_get_picture_format(int width, int height)
{
    int format;

    if (width == 128 && height == 96)
	format = 1;
    else if (width == 176 && height == 144)
	format = 2;
    else if (width == 352 && height == 288)
	format = 3;
    else if (width == 704 && height == 576)
	format = 4;
    else if (width == 1408 && height == 1152)
	format = 5;
    else
        format = 7;
    return format;
}

void h263_encode_picture_header(MpegEncContext * s, int picture_number)
{
    int format, umvplus;

    align_put_bits(&s->pb);
    put_bits(&s->pb, 22, 0x20);
    put_bits(&s->pb, 8, ((s->picture_number * 30 * FRAME_RATE_BASE) / 
                         s->frame_rate) & 0xff);

    put_bits(&s->pb, 1, 1);	/* marker */
    put_bits(&s->pb, 1, 0);	/* h263 id */
    put_bits(&s->pb, 1, 0);	/* split screen off */
    put_bits(&s->pb, 1, 0);	/* camera  off */
    put_bits(&s->pb, 1, 0);	/* freeze picture release off */

    if (!s->h263_plus) {
        /* H.263v1 */
        format = h263_get_picture_format(s->width, s->height);
        put_bits(&s->pb, 3, format);
        put_bits(&s->pb, 1, (s->pict_type == P_TYPE));
        /* By now UMV IS DISABLED ON H.263v1, since the restrictions
        of H.263v1 UMV implies to check the predicted MV after
        calculation of the current MB to see if we're on the limits */
        put_bits(&s->pb, 1, 0);	/* unrestricted motion vector: off */
        put_bits(&s->pb, 1, 0);	/* SAC: off */
        put_bits(&s->pb, 1, 0);	/* advanced prediction mode: off */
        put_bits(&s->pb, 1, 0);	/* not PB frame */
        put_bits(&s->pb, 5, s->qscale);
        put_bits(&s->pb, 1, 0);	/* Continuous Presence Multipoint mode: off */
    } else {
        /* H.263v2 */
        /* H.263 Plus PTYPE */
        put_bits(&s->pb, 3, 7);
        put_bits(&s->pb,3,1); /* Update Full Extended PTYPE */
        put_bits(&s->pb,3,6); /* Custom Source Format */
        put_bits(&s->pb,1,0); /* Custom PCF: off */
        umvplus = (s->pict_type == P_TYPE) && s->unrestricted_mv;
        put_bits(&s->pb, 1, umvplus); /* Unrestricted Motion Vector */
        put_bits(&s->pb,1,0); /* SAC: off */
        put_bits(&s->pb,1,0); /* Advanced Prediction Mode: off */
        put_bits(&s->pb,1,0); /* Advanced Intra Coding: off */
        put_bits(&s->pb,1,0); /* Deblocking Filter: off */
        put_bits(&s->pb,1,0); /* Slice Structured: off */
        put_bits(&s->pb,1,0); /* Reference Picture Selection: off */
        put_bits(&s->pb,1,0); /* Independent Segment Decoding: off */
        put_bits(&s->pb,1,0); /* Alternative Inter VLC: off */
        put_bits(&s->pb,1,0); /* Modified Quantization: off */
        put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */
        put_bits(&s->pb,3,0); /* Reserved */
		
        put_bits(&s->pb, 3, s->pict_type == P_TYPE);
		
        put_bits(&s->pb,1,0); /* Reference Picture Resampling: off */
        put_bits(&s->pb,1,0); /* Reduced-Resolution Update: off */
        put_bits(&s->pb,1,0); /* Rounding Type */
        put_bits(&s->pb,2,0); /* Reserved */
        put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */
		
        /* This should be here if PLUSPTYPE */
        put_bits(&s->pb, 1, 0);	/* Continuous Presence Multipoint mode: off */
		
        /* Custom Picture Format (CPFMT) */
		
        put_bits(&s->pb,4,2); /* Aspect ratio: CIF 12:11 (4:3) picture */
        put_bits(&s->pb,9,(s->width >> 2) - 1);
        put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */
        put_bits(&s->pb,9,(s->height >> 2));
        /* Unlimited Unrestricted Motion Vectors Indicator (UUI) */
        if (umvplus)
            put_bits(&s->pb,1,1); /* Limited according tables of Annex D */
        put_bits(&s->pb, 5, s->qscale);
    }

    put_bits(&s->pb, 1, 0);	/* no PEI */
}

void h263_encode_mb(MpegEncContext * s,
		    DCTELEM block[6][64],
		    int motion_x, int motion_y)
{
    int cbpc, cbpy, i, cbp, pred_x, pred_y;

    //    printf("**mb x=%d y=%d\n", s->mb_x, s->mb_y);
    if (!s->mb_intra) {
	/* compute cbp */
	cbp = 0;
	for (i = 0; i < 6; i++) {
	    if (s->block_last_index[i] >= 0)
		cbp |= 1 << (5 - i);
	}
	if ((cbp | motion_x | motion_y) == 0) {
	    /* skip macroblock */
	    put_bits(&s->pb, 1, 1);
	    return;
	}
	put_bits(&s->pb, 1, 0);	/* mb coded */
	cbpc = cbp & 3;
	put_bits(&s->pb,
		 inter_MCBPC_bits[cbpc],
		 inter_MCBPC_code[cbpc]);
	cbpy = cbp >> 2;
	cbpy ^= 0xf;
	put_bits(&s->pb, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);

	/* motion vectors: 16x16 mode only now */
        h263_pred_motion(s, 0, &pred_x, &pred_y);
        
        h263_encode_motion(s, motion_x - pred_x);
        h263_encode_motion(s, motion_y - pred_y);
    } else {
	/* compute cbp */
	cbp = 0;
	for (i = 0; i < 6; i++) {
	    if (s->block_last_index[i] >= 1)
		cbp |= 1 << (5 - i);
	}

	cbpc = cbp & 3;
	if (s->pict_type == I_TYPE) {
	    put_bits(&s->pb,
		     intra_MCBPC_bits[cbpc],
		     intra_MCBPC_code[cbpc]);
	} else {
	    put_bits(&s->pb, 1, 0);	/* mb coded */
	    put_bits(&s->pb,
		     inter_MCBPC_bits[cbpc + 4],
		     inter_MCBPC_code[cbpc + 4]);
	}
	if (s->h263_pred) {
	    /* XXX: currently, we do not try to use ac prediction */
	    put_bits(&s->pb, 1, 0);	/* no ac prediction */
	}
	cbpy = cbp >> 2;
	put_bits(&s->pb, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
    }

    /* encode each block */
    if (s->h263_pred) {
	for (i = 0; i < 6; i++) {
	    mpeg4_encode_block(s, block[i], i);
	}
    } else {
	for (i = 0; i < 6; i++) {
	    h263_encode_block(s, block[i], i);
	}
    }
}

static inline int mid_pred(int a, int b, int c)
{
    int vmin, vmax;
    vmin = a;
    if (b < vmin)
        vmin = b;
    if (c < vmin)
        vmin = c;

    vmax = a;
    if (b > vmax)
        vmax = b;
    if (c > vmax)
        vmax = c;

    return a + b + c - vmin - vmax;
}

INT16 *h263_pred_motion(MpegEncContext * s, int block, 
                        int *px, int *py)
{
    int x, y, wrap;
    INT16 *A, *B, *C, *mot_val;

    x = 2 * s->mb_x + 1 + (block & 1);
    y = 2 * s->mb_y + 1 + ((block >> 1) & 1);
    wrap = 2 * s->mb_width + 2;

    mot_val = s->motion_val[(x) + (y) * wrap];

    /* special case for first line */
    if (y == 1 || s->first_slice_line) {
        A = s->motion_val[(x-1) + (y) * wrap];
        *px = A[0];
        *py = A[1];
    } else {
        switch(block) {
        default:
        case 0:
            A = s->motion_val[(x-1) + (y) * wrap];
            B = s->motion_val[(x) + (y-1) * wrap];
            C = s->motion_val[(x+2) + (y-1) * wrap];
            break;
        case 1:
        case 2:
            A = s->motion_val[(x-1) + (y) * wrap];
            B = s->motion_val[(x) + (y-1) * wrap];
            C = s->motion_val[(x+1) + (y-1) * wrap];
            break;
        case 3:
            A = s->motion_val[(x-1) + (y) * wrap];
            B = s->motion_val[(x-1) + (y-1) * wrap];
            C = s->motion_val[(x) + (y-1) * wrap];
            break;
        }
        *px = mid_pred(A[0], B[0], C[0]);
        *py = mid_pred(A[1], B[1], C[1]);
    }
    return mot_val;
}


static void h263_encode_motion(MpegEncContext * s, int val)
{
    int range, l, m, bit_size, sign, code, bits;

    if (val == 0) {
        /* zero vector */
        code = 0;
        put_bits(&s->pb, mvtab[code][1], mvtab[code][0]);
    } else {
        bit_size = s->f_code - 1;
        range = 1 << bit_size;
        /* modulo encoding */
        l = range * 32;
        m = 2 * l;
        if (val < -l) {
            val += m;
        } else if (val >= l) {
            val -= m;
        }

        if (val >= 0) {
            val--;
            code = (val >> bit_size) + 1;
            bits = val & (range - 1);
            sign = 0;
        } else {
            val = -val;
            val--;
            code = (val >> bit_size) + 1;
            bits = val & (range - 1);
            sign = 1;
        }

        put_bits(&s->pb, mvtab[code][1] + 1, (mvtab[code][0] << 1) | sign); 
        if (bit_size > 0) {
            put_bits(&s->pb, bit_size, bits);
        }
    }
}

void h263_encode_init_vlc(MpegEncContext *s)
{
    static int done = 0;

    if (!done) {
        done = 1;
        init_rl(&rl_inter);
        init_rl(&rl_intra);
    }
}

static void h263_encode_block(MpegEncContext * s, DCTELEM * block, int n)
{
    int level, run, last, i, j, last_index, last_non_zero, sign, slevel;
    int code;
    RLTable *rl = &rl_inter;

    if (s->mb_intra) {
	/* DC coef */
	level = block[0];
        /* 255 cannot be represented, so we clamp */
        if (level > 254) {
            level = 254;
            block[0] = 254;
        }
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
    for (; i <= last_index; i++) {
	j = zigzag_direct[i];
	level = block[j];
	if (level) {
	    run = i - last_non_zero - 1;
	    last = (i == last_index);
	    sign = 0;
	    slevel = level;
	    if (level < 0) {
		sign = 1;
		level = -level;
	    }
            code = get_rl_index(rl, last, run, level);
            put_bits(&s->pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
            if (code == rl->n) {
                put_bits(&s->pb, 1, last);
                put_bits(&s->pb, 6, run);
                put_bits(&s->pb, 8, slevel & 0xff);
            } else {
                put_bits(&s->pb, 1, sign);
            }
	    last_non_zero = i;
	}
    }
}

/***************************************************/

/* write mpeg4 VOP header */
void mpeg4_encode_picture_header(MpegEncContext * s, int picture_number)
{
    align_put_bits(&s->pb);

    put_bits(&s->pb, 32, 0x1B6);	/* vop header */
    put_bits(&s->pb, 2, s->pict_type - 1);	/* pict type: I = 0 , P = 1 */
    /* XXX: time base + 1 not always correct */
    put_bits(&s->pb, 1, 1);
    put_bits(&s->pb, 1, 0);

    put_bits(&s->pb, 1, 1);	/* marker */
    put_bits(&s->pb, 4, 1);	/* XXX: correct time increment */
    put_bits(&s->pb, 1, 1);	/* marker */
    put_bits(&s->pb, 1, 1);	/* vop coded */
    if (s->pict_type == P_TYPE) {
        s->no_rounding = 0;
	put_bits(&s->pb, 1, s->no_rounding);	/* rounding type */
    }
    put_bits(&s->pb, 3, 0);	/* intra dc VLC threshold */

    put_bits(&s->pb, 5, s->qscale);

    if (s->pict_type != I_TYPE)
	put_bits(&s->pb, 3, s->f_code);	/* fcode_for */
    //    printf("****frame %d\n", picture_number);
}

void h263_dc_scale(MpegEncContext * s)
{
    int quant;

    quant = s->qscale;
    /* luminance */
    if (quant < 5)
	s->y_dc_scale = 8;
    else if (quant > 4 && quant < 9)
	s->y_dc_scale = (2 * quant);
    else if (quant > 8 && quant < 25)
	s->y_dc_scale = (quant + 8);
    else
	s->y_dc_scale = (2 * quant - 16);
    /* chrominance */
    if (quant < 5)
	s->c_dc_scale = 8;
    else if (quant > 4 && quant < 25)
	s->c_dc_scale = ((quant + 13) / 2);
    else
	s->c_dc_scale = (quant - 6);
}

static int mpeg4_pred_dc(MpegEncContext * s, int n, UINT16 **dc_val_ptr, int *dir_ptr)
{
    int a, b, c, x, y, wrap, pred, scale;
    UINT16 *dc_val;

    /* find prediction */
    if (n < 4) {
	x = 2 * s->mb_x + 1 + (n & 1);
	y = 2 * s->mb_y + 1 + ((n & 2) >> 1);
	wrap = s->mb_width * 2 + 2;
	dc_val = s->dc_val[0];
	scale = s->y_dc_scale;
    } else {
	x = s->mb_x + 1;
	y = s->mb_y + 1;
	wrap = s->mb_width + 2;
	dc_val = s->dc_val[n - 4 + 1];
	scale = s->c_dc_scale;
    }

    /* B C
     * A X 
     */
    a = dc_val[(x - 1) + (y) * wrap];
    b = dc_val[(x - 1) + (y - 1) * wrap];
    c = dc_val[(x) + (y - 1) * wrap];

    if (abs(a - b) < abs(b - c)) {
	pred = c;
        *dir_ptr = 1; /* top */
    } else {
	pred = a;
        *dir_ptr = 0; /* left */
    }
    /* we assume pred is positive */
    pred = (pred + (scale >> 1)) / scale;

    /* prepare address for prediction update */
    *dc_val_ptr = &dc_val[(x) + (y) * wrap];

    return pred;
}

void mpeg4_pred_ac(MpegEncContext * s, INT16 *block, int n, 
                   int dir)
{
    int x, y, wrap, i;
    INT16 *ac_val, *ac_val1;

    /* find prediction */
    if (n < 4) {
	x = 2 * s->mb_x + 1 + (n & 1);
	y = 2 * s->mb_y + 1 + ((n & 2) >> 1);
	wrap = s->mb_width * 2 + 2;
	ac_val = s->ac_val[0][0];
    } else {
	x = s->mb_x + 1;
	y = s->mb_y + 1;
	wrap = s->mb_width + 2;
	ac_val = s->ac_val[n - 4 + 1][0];
    }
    ac_val += ((y) * wrap + (x)) * 16;
    ac_val1 = ac_val;
    if (s->ac_pred) {
        if (dir == 0) {
            /* left prediction */
            ac_val -= 16;
            for(i=1;i<8;i++) {
                block[i*8] += ac_val[i];
            }
        } else {
            /* top prediction */
            ac_val -= 16 * wrap;
            for(i=1;i<8;i++) {
                block[i] += ac_val[i + 8];
            }
        }
    }
    /* left copy */
    for(i=1;i<8;i++)
        ac_val1[i] = block[i * 8];
    /* top copy */
    for(i=1;i<8;i++)
        ac_val1[8 + i] = block[i];
}

static inline void mpeg4_encode_dc(MpegEncContext * s, int level, int n, int *dir_ptr)
{
    int size, v, pred;
    UINT16 *dc_val;

    pred = mpeg4_pred_dc(s, n, &dc_val, dir_ptr);
    if (n < 4) {
        *dc_val = level * s->y_dc_scale;
    } else {
        *dc_val = level * s->c_dc_scale;
    }

    /* do the prediction */
    level -= pred;
    /* find number of bits */
    size = 0;
    v = abs(level);
    while (v) {
	v >>= 1;
	size++;
    }

    if (n < 4) {
	/* luminance */
	put_bits(&s->pb, DCtab_lum[size][1], DCtab_lum[size][0]);
    } else {
	/* chrominance */
	put_bits(&s->pb, DCtab_chrom[size][1], DCtab_chrom[size][0]);
    }

    /* encode remaining bits */
    if (size > 0) {
	if (level < 0)
	    level = (-level) ^ ((1 << size) - 1);
	put_bits(&s->pb, size, level);
	if (size > 8)
	    put_bits(&s->pb, 1, 1);
    }
}

static void mpeg4_encode_block(MpegEncContext * s, DCTELEM * block, int n)
{
    int level, run, last, i, j, last_index, last_non_zero, sign, slevel;
    int code, dc_pred_dir;
    const RLTable *rl;

    if (s->mb_intra) {
	/* mpeg4 based DC predictor */
	mpeg4_encode_dc(s, block[0], n, &dc_pred_dir);
	i = 1;
        rl = &rl_intra;
    } else {
	i = 0;
        rl = &rl_inter;
    }

    /* AC coefs */
    last_index = s->block_last_index[n];
    last_non_zero = i - 1;
    for (; i <= last_index; i++) {
	j = zigzag_direct[i];
	level = block[j];
	if (level) {
	    run = i - last_non_zero - 1;
	    last = (i == last_index);
	    sign = 0;
	    slevel = level;
	    if (level < 0) {
		sign = 1;
		level = -level;
	    }
            code = get_rl_index(rl, last, run, level);
            put_bits(&s->pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
            if (code == rl->n) {
                int level1, run1;
                level1 = level - rl->max_level[last][run];
                if (level1 < 1) 
                    goto esc2;
                code = get_rl_index(rl, last, run, level1);
                if (code == rl->n) {
                esc2:
                    put_bits(&s->pb, 1, 1);
                    if (level > MAX_LEVEL)
                        goto esc3;
                    run1 = run - rl->max_run[last][level] - 1;
                    if (run1 < 0)
                        goto esc3;
                    code = get_rl_index(rl, last, run1, level);
                    if (code == rl->n) {
                    esc3:
                        /* third escape */
                        put_bits(&s->pb, 1, 1);
                        put_bits(&s->pb, 1, last);
                        put_bits(&s->pb, 6, run);
                        put_bits(&s->pb, 1, 1);
                        put_bits(&s->pb, 12, slevel & 0xfff);
                        put_bits(&s->pb, 1, 1);
                    } else {
                        /* second escape */
                        put_bits(&s->pb, 1, 0);
                        put_bits(&s->pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
                        put_bits(&s->pb, 1, sign);
                    }
                } else {
                    /* first escape */
                    put_bits(&s->pb, 1, 0);
                    put_bits(&s->pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
                    put_bits(&s->pb, 1, sign);
                }
            } else {
                put_bits(&s->pb, 1, sign);
            }
	    last_non_zero = i;
	}
    }
}



/***********************************************/
/* decoding */

static VLC intra_MCBPC_vlc;
static VLC inter_MCBPC_vlc;
static VLC cbpy_vlc;
static VLC mv_vlc;
static VLC dc_lum, dc_chrom;

void init_rl(RLTable *rl)
{
    INT8 max_level[MAX_RUN+1], max_run[MAX_LEVEL+1];
    UINT8 index_run[MAX_RUN+1];
    int last, run, level, start, end, i;

    /* compute max_level[], max_run[] and index_run[] */
    for(last=0;last<2;last++) {
        if (last == 0) {
            start = 0;
            end = rl->last;
        } else {
            start = rl->last;
            end = rl->n;
        }

        memset(max_level, 0, MAX_RUN + 1);
        memset(max_run, 0, MAX_LEVEL + 1);
        memset(index_run, rl->n, MAX_RUN + 1);
        for(i=start;i<end;i++) {
            run = rl->table_run[i];
            level = rl->table_level[i];
            if (index_run[run] == rl->n)
                index_run[run] = i;
            if (level > max_level[run])
                max_level[run] = level;
            if (run > max_run[level])
                max_run[level] = run;
        }
        rl->max_level[last] = malloc(MAX_RUN + 1);
        memcpy(rl->max_level[last], max_level, MAX_RUN + 1);
        rl->max_run[last] = malloc(MAX_LEVEL + 1);
        memcpy(rl->max_run[last], max_run, MAX_LEVEL + 1);
        rl->index_run[last] = malloc(MAX_RUN + 1);
        memcpy(rl->index_run[last], index_run, MAX_RUN + 1);
    }
}

void init_vlc_rl(RLTable *rl)
{
    init_vlc(&rl->vlc, 9, rl->n + 1, 
             &rl->table_vlc[0][1], 4, 2,
             &rl->table_vlc[0][0], 4, 2);
}

/* init vlcs */

/* XXX: find a better solution to handle static init */
void h263_decode_init_vlc(MpegEncContext *s)
{
    static int done = 0;

    if (!done) {
        done = 1;

        init_vlc(&intra_MCBPC_vlc, 6, 8, 
                 intra_MCBPC_bits, 1, 1,
                 intra_MCBPC_code, 1, 1);
        init_vlc(&inter_MCBPC_vlc, 9, 20, 
                 inter_MCBPC_bits, 1, 1,
                 inter_MCBPC_code, 1, 1);
        init_vlc(&cbpy_vlc, 6, 16,
                 &cbpy_tab[0][1], 2, 1,
                 &cbpy_tab[0][0], 2, 1);
        init_vlc(&mv_vlc, 9, 33,
                 &mvtab[0][1], 2, 1,
                 &mvtab[0][0], 2, 1);
        init_rl(&rl_inter);
        init_rl(&rl_intra);
        init_vlc_rl(&rl_inter);
        init_vlc_rl(&rl_intra);
        init_vlc(&dc_lum, 9, 13,
                 &DCtab_lum[0][1], 2, 1,
                 &DCtab_lum[0][0], 2, 1);
        init_vlc(&dc_chrom, 9, 13,
                 &DCtab_chrom[0][1], 2, 1,
                 &DCtab_chrom[0][0], 2, 1);
    }
}

int h263_decode_mb(MpegEncContext *s,
                   DCTELEM block[6][64])
{
    int cbpc, cbpy, i, cbp, pred_x, pred_y, mx, my, dquant;
    INT16 *mot_val;
    static UINT8 quant_tab[4] = { -1, -2, 1, 2 };

    if (s->pict_type == P_TYPE) {
        if (get_bits1(&s->gb)) {
            /* skip mb */
            s->mb_intra = 0;
            for(i=0;i<6;i++)
                s->block_last_index[i] = -1;
            s->mv_dir = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16;
            s->mv[0][0][0] = 0;
            s->mv[0][0][1] = 0;
            s->mb_skiped = 1;
            return 0;
        }
        cbpc = get_vlc(&s->gb, &inter_MCBPC_vlc);
        if (cbpc < 0)
            return -1;
        dquant = cbpc & 8;
        s->mb_intra = ((cbpc & 4) != 0);
    } else {
        cbpc = get_vlc(&s->gb, &intra_MCBPC_vlc);
        if (cbpc < 0)
            return -1;
        dquant = cbpc & 4;
        s->mb_intra = 1;
    }

    if (!s->mb_intra) {
        cbpy = get_vlc(&s->gb, &cbpy_vlc);
        cbp = (cbpc & 3) | ((cbpy ^ 0xf) << 2);
        if (dquant) {
            s->qscale += quant_tab[get_bits(&s->gb, 2)];
            if (s->qscale < 1)
                s->qscale = 1;
            else if (s->qscale > 31)
                s->qscale = 31;
        }
        s->mv_dir = MV_DIR_FORWARD;
        if ((cbpc & 16) == 0) {
            /* 16x16 motion prediction */
            s->mv_type = MV_TYPE_16X16;
            h263_pred_motion(s, 0, &pred_x, &pred_y);
            mx = h263_decode_motion(s, pred_x);
            if (mx >= 0xffff)
                return -1;
            my = h263_decode_motion(s, pred_y);
            if (my >= 0xffff)
                return -1;
            s->mv[0][0][0] = mx;
            s->mv[0][0][1] = my;
        } else {
            s->mv_type = MV_TYPE_8X8;
            for(i=0;i<4;i++) {
                mot_val = h263_pred_motion(s, i, &pred_x, &pred_y);
                mx = h263_decode_motion(s, pred_x);
                if (mx >= 0xffff)
                    return -1;
                my = h263_decode_motion(s, pred_y);
                if (my >= 0xffff)
                    return -1;
                s->mv[0][i][0] = mx;
                s->mv[0][i][1] = my;
                mot_val[0] = mx;
                mot_val[1] = my;
            }
        }
    } else {
        s->ac_pred = 0;
	if (s->h263_pred) {
            s->ac_pred = get_bits1(&s->gb);
        }
        cbpy = get_vlc(&s->gb, &cbpy_vlc);
        cbp = (cbpc & 3) | (cbpy << 2);
        if (dquant) {
            s->qscale += quant_tab[get_bits(&s->gb, 2)];
            if (s->qscale < 1)
                s->qscale = 1;
            else if (s->qscale > 31)
                s->qscale = 31;
        }
    }

    /* decode each block */
    if (s->h263_pred) {
	for (i = 0; i < 6; i++) {
	    if (mpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1) < 0)
                return -1;
	}
    } else {
	for (i = 0; i < 6; i++) {
	    if (h263_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1) < 0)
                return -1;
	}
    }
    return 0;
}

static int h263_decode_motion(MpegEncContext * s, int pred)
{
    int code, val, sign, shift, l, m;

    code = get_vlc(&s->gb, &mv_vlc);
    if (code < 0)
        return 0xffff;

    if (code == 0)
        return pred;
    sign = get_bits1(&s->gb);
    shift = s->f_code - 1;
    val = (code - 1) << shift;
    if (shift > 0)
        val |= get_bits(&s->gb, shift);
    val++;
    if (sign)
        val = -val;
    val += pred;
    
    /* modulo decoding */
    if (!s->h263_long_vectors) {
        l = (1 << (s->f_code - 1)) * 32;
        m = 2 * l;
        if (val < -l) {
            val += m;
        } else if (val >= l) {
            val -= m;
        }
    } else {
        /* horrible h263 long vector mode */
        if (pred < -31 && val < -63)
            val += 64;
        if (pred > 32 && val > 63)
            val -= 64;
    }
    return val;
}

static int h263_decode_block(MpegEncContext * s, DCTELEM * block,
                             int n, int coded)
{
    int code, level, i, j, last, run;
    RLTable *rl = &rl_inter;

    if (s->mb_intra) {
	/* DC coef */
        if (s->h263_rv10 && s->rv10_version == 3 && s->pict_type == I_TYPE) {
            int component, diff;
            component = (n <= 3 ? 0 : n - 4 + 1);
            level = s->last_dc[component];
            if (s->rv10_first_dc_coded[component]) {
                diff = rv_decode_dc(s, n);
                if (diff == 0xffff)
                    return -1;
                level += diff;
                level = level & 0xff; /* handle wrap round */
                s->last_dc[component] = level;
            } else {
                s->rv10_first_dc_coded[component] = 1;
            }
        } else {
            level = get_bits(&s->gb, 8);
            if (level == 255)
                level = 128;
        }
        block[0] = level;
	i = 1;
    } else {
	i = 0;
    }
    if (!coded) {
        s->block_last_index[n] = i - 1;
        return 0;
    }

    for(;;) {
        code = get_vlc(&s->gb, &rl->vlc);
        if (code < 0)
            return -1;
        if (code == rl->n) {
            /* escape */
            last = get_bits1(&s->gb);
            run = get_bits(&s->gb, 6);
            level = (INT8)get_bits(&s->gb, 8);
            if (s->h263_rv10 && level == -128) {
                /* XXX: should patch encoder too */
                level = get_bits(&s->gb, 12);
                level = (level << 20) >> 20;
            }
        } else {
            run = rl->table_run[code];
            level = rl->table_level[code];
            last = code >= rl->last;
            if (get_bits1(&s->gb))
                level = -level;
        }
        i += run;
        if (i >= 64)
            return -1;
	j = zigzag_direct[i];
        block[j] = level;
        if (last)
            break;
        i++;
    }
    s->block_last_index[n] = i;
    return 0;
}

static int mpeg4_decode_dc(MpegEncContext * s, int n, int *dir_ptr)
{
    int level, pred, code;
    UINT16 *dc_val;

    if (n < 4) 
        code = get_vlc(&s->gb, &dc_lum);
    else 
        code = get_vlc(&s->gb, &dc_chrom);
    if (code < 0)
        return -1;
    if (code == 0) {
        level = 0;
    } else {
        level = get_bits(&s->gb, code);
        if ((level >> (code - 1)) == 0) /* if MSB not set it is negative*/
            level = - (level ^ ((1 << code) - 1));
        if (code > 8)
            skip_bits1(&s->gb); /* marker */
    }

    pred = mpeg4_pred_dc(s, n, &dc_val, dir_ptr);
    level += pred;
    if (level < 0)
        level = 0;
    if (n < 4) {
        *dc_val = level * s->y_dc_scale;
    } else {
        *dc_val = level * s->c_dc_scale;
    }
    return level;
}

static int mpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                              int n, int coded)
{
    int code, level, i, j, last, run;
    int dc_pred_dir;
    RLTable *rl;
    const UINT8 *scan_table;

    if (s->mb_intra) {
	/* DC coef */
        level = mpeg4_decode_dc(s, n, &dc_pred_dir);
        if (level < 0)
            return -1;
        block[0] = level;
	i = 1;
        if (!coded) 
            goto not_coded;
        rl = &rl_intra;
        if (s->ac_pred) {
            if (dc_pred_dir == 0) 
                scan_table = ff_alternate_vertical_scan; /* left */
            else
                scan_table = ff_alternate_horizontal_scan; /* top */
        } else {
            scan_table = zigzag_direct;
        }
    } else {
	i = 0;
        if (!coded) {
            s->block_last_index[n] = i - 1;
            return 0;
        }
        rl = &rl_inter;
        scan_table = zigzag_direct;
    }

    for(;;) {
        code = get_vlc(&s->gb, &rl->vlc);
        if (code < 0)
            return -1;
        if (code == rl->n) {
            /* escape */
            if (get_bits1(&s->gb) != 0) {
                if (get_bits1(&s->gb) != 0) {
                    /* third escape */
                    last = get_bits1(&s->gb);
                    run = get_bits(&s->gb, 6);
                    get_bits1(&s->gb); /* marker */
                    level = get_bits(&s->gb, 12);
                    level = (level << 20) >> 20; /* sign extend */
                    skip_bits1(&s->gb); /* marker */
                } else {
                    /* second escape */
                    code = get_vlc(&s->gb, &rl->vlc);
                    if (code < 0 || code >= rl->n)
                        return -1;
                    run = rl->table_run[code];
                    level = rl->table_level[code];
                    last = code >= rl->last;
                    run += rl->max_run[last][level] + 1;
                    if (get_bits1(&s->gb))
                        level = -level;
                }
            } else {
                /* first escape */
                code = get_vlc(&s->gb, &rl->vlc);
                if (code < 0 || code >= rl->n)
                    return -1;
                run = rl->table_run[code];
                level = rl->table_level[code];
                last = code >= rl->last;
                level += rl->max_level[last][run];
                if (get_bits1(&s->gb))
                    level = -level;
            }
        } else {
            run = rl->table_run[code];
            level = rl->table_level[code];
            last = code >= rl->last;
            if (get_bits1(&s->gb))
                level = -level;
        }
        i += run;
        if (i >= 64)
            return -1;
	j = scan_table[i];
        block[j] = level;
        i++;
        if (last)
            break;
    }
 not_coded:
    if (s->mb_intra) {
        mpeg4_pred_ac(s, block, n, dc_pred_dir);
        if (s->ac_pred) {
            i = 64; /* XXX: not optimal */
        }
    }
    s->block_last_index[n] = i - 1;
    return 0;
}

/* most is hardcoded. should extend to handle all h263 streams */
int h263_decode_picture_header(MpegEncContext *s)
{
    int format, width, height;

    /* picture header */
    if (get_bits(&s->gb, 22) != 0x20)
        return -1;
    skip_bits(&s->gb, 8); /* picture timestamp */

    if (get_bits1(&s->gb) != 1)
        return -1;	/* marker */
    if (get_bits1(&s->gb) != 0)
        return -1;	/* h263 id */
    skip_bits1(&s->gb);	/* split screen off */
    skip_bits1(&s->gb);	/* camera  off */
    skip_bits1(&s->gb);	/* freeze picture release off */

    format = get_bits(&s->gb, 3);

    if (format != 7) {
        s->h263_plus = 0;
        /* H.263v1 */
        width = h263_format[format][0];
        height = h263_format[format][1];
        if (!width)
            return -1;

        s->pict_type = I_TYPE + get_bits1(&s->gb);

        s->unrestricted_mv = get_bits1(&s->gb); 
        s->h263_long_vectors = s->unrestricted_mv;

        if (get_bits1(&s->gb) != 0)
            return -1;	/* SAC: off */
        if (get_bits1(&s->gb) != 0)
            return -1;	/* advanced prediction mode: off */
        if (get_bits1(&s->gb) != 0)
            return -1;	/* not PB frame */

        s->qscale = get_bits(&s->gb, 5);
        skip_bits1(&s->gb);	/* Continuous Presence Multipoint mode: off */
    } else {
        s->h263_plus = 1;
        /* H.263v2 */
        if (get_bits(&s->gb, 3) != 1)
            return -1;
        if (get_bits(&s->gb, 3) != 6) /* custom source format */
            return -1;
        skip_bits(&s->gb, 12);
        skip_bits(&s->gb, 3);
        s->pict_type = get_bits(&s->gb, 3) + 1;
        if (s->pict_type != I_TYPE &&
            s->pict_type != P_TYPE)
            return -1;
        skip_bits(&s->gb, 7);
        skip_bits(&s->gb, 4); /* aspect ratio */
        width = (get_bits(&s->gb, 9) + 1) * 4;
        skip_bits1(&s->gb);
        height = get_bits(&s->gb, 9) * 4;
        if (height == 0)
            return -1;
        s->qscale = get_bits(&s->gb, 5);
    }
    /* PEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }
    s->f_code = 1;
    s->width = width;
    s->height = height;
    return 0;
}

/* decode mpeg4 VOP header */
int mpeg4_decode_picture_header(MpegEncContext * s)
{
    int time_incr, startcode, state, v;

 redo:
    /* search next start code */
    align_get_bits(&s->gb);
    state = 0xff;
    for(;;) {
        v = get_bits(&s->gb, 8);
        if (state == 0x000001) {
            state = ((state << 8) | v) & 0xffffff;
            startcode = state;
            break;
        }
        state = ((state << 8) | v) & 0xffffff;
        /* XXX: really detect end of frame */
        if (state == 0)
            return -1;
    }

    if (startcode == 0x120) {
        int time_increment_resolution, width, height;

        /* vol header */
        skip_bits(&s->gb, 1); /* random access */
        skip_bits(&s->gb, 8); /* vo_type */
        skip_bits(&s->gb, 1); /* is_ol_id */
        skip_bits(&s->gb, 4); /* vo_ver_id */
        skip_bits(&s->gb, 3); /* vo_priority */
        
        skip_bits(&s->gb, 4); /* aspect_ratio_info */
        skip_bits(&s->gb, 1); /* vol control parameter */
        skip_bits(&s->gb, 2); /* vol shape */
        skip_bits1(&s->gb);   /* marker */
        
        time_increment_resolution = get_bits(&s->gb, 16);
        s->time_increment_bits = log2(time_increment_resolution - 1) + 1;
        skip_bits1(&s->gb);   /* marker */

        skip_bits1(&s->gb);   /* vop rate  */
        skip_bits(&s->gb, s->time_increment_bits);
        skip_bits1(&s->gb);   /* marker */
        
        width = get_bits(&s->gb, 13);
        skip_bits1(&s->gb);   /* marker */
        height = get_bits(&s->gb, 13);
        skip_bits1(&s->gb);   /* marker */
        
        skip_bits1(&s->gb);   /* interfaced */
        skip_bits1(&s->gb);   /* OBMC */
        skip_bits(&s->gb, 2); /* vol_sprite_usage */
        skip_bits1(&s->gb);   /* not_8_bit */

        skip_bits1(&s->gb);   /* vol_quant_type */
        skip_bits1(&s->gb);   /* vol_quarter_pixel */
        skip_bits1(&s->gb);   /* complexity_estimation_disabled */
        skip_bits1(&s->gb);   /* resync_marker_disabled */
        skip_bits1(&s->gb);   /* data_partioning_enabled */
        goto redo;
    } else if (startcode != 0x1b6) {
        goto redo;
    }

    s->pict_type = get_bits(&s->gb, 2) + 1;	/* pict type: I = 0 , P = 1 */
    if (s->pict_type != I_TYPE &&
        s->pict_type != P_TYPE)
        return -1;
    
    /* XXX: parse time base */
    time_incr = 0;
    while (get_bits1(&s->gb) != 0) 
        time_incr++;

    skip_bits1(&s->gb);   	/* marker */
    skip_bits(&s->gb, s->time_increment_bits);
    skip_bits1(&s->gb);   	/* marker */
    /* vop coded */
    if (get_bits1(&s->gb) != 1)
	return -1; 
    
    if (s->pict_type == P_TYPE) {
        /* rounding type for motion estimation */
	s->no_rounding = get_bits1(&s->gb);
    }
        
    if (get_bits(&s->gb, 3) != 0)
	return -1; /* intra dc VLC threshold */

    s->qscale = get_bits(&s->gb, 5);

    if (s->pict_type != I_TYPE) {
	s->f_code = get_bits(&s->gb, 3);	/* fcode_for */
    }
    return 0;
}

/* don't understand why they choose a different header ! */
int intel_h263_decode_picture_header(MpegEncContext *s)
{
    int format;

    /* picture header */
    if (get_bits(&s->gb, 22) != 0x20)
        return -1;
    skip_bits(&s->gb, 8); /* picture timestamp */

    if (get_bits1(&s->gb) != 1)
        return -1;	/* marker */
    if (get_bits1(&s->gb) != 0)
        return -1;	/* h263 id */
    skip_bits1(&s->gb);	/* split screen off */
    skip_bits1(&s->gb);	/* camera  off */
    skip_bits1(&s->gb);	/* freeze picture release off */

    format = get_bits(&s->gb, 3);
    if (format != 7)
        return -1;

    s->h263_plus = 0;

    s->pict_type = I_TYPE + get_bits1(&s->gb);
    
    s->unrestricted_mv = get_bits1(&s->gb); 
    s->h263_long_vectors = s->unrestricted_mv;

    if (get_bits1(&s->gb) != 0)
        return -1;	/* SAC: off */
    if (get_bits1(&s->gb) != 0)
        return -1;	/* advanced prediction mode: off */
    if (get_bits1(&s->gb) != 0)
        return -1;	/* not PB frame */

    /* skip unknown header garbage */
    skip_bits(&s->gb, 41);

    s->qscale = get_bits(&s->gb, 5);
    skip_bits1(&s->gb);	/* Continuous Presence Multipoint mode: off */

    /* PEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }
    s->f_code = 1;
    return 0;
}
