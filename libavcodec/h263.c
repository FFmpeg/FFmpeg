/*
 * H263/MPEG4 backend for ffmpeg encoder and decoder
 * Copyright (c) 2000,2001 Gerard Lantau.
 * H263+ support.
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
#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h263data.h"
#include "mpeg4data.h"

//rounded divison & shift
#define RDIV(a,b) ((a) > 0 ? ((a)+((b)>>1))/(b) : ((a)-((b)>>1))/(b))
#define RSHIFT(a,b) ((a) > 0 ? ((a) + (1<<((b)-1)))>>(b) : ((a) + (1<<((b)-1))-1)>>(b))

static void h263_encode_block(MpegEncContext * s, DCTELEM * block,
			      int n);
static void h263_encode_motion(MpegEncContext * s, int val);
static void h263p_encode_umotion(MpegEncContext * s, int val);
static void mpeg4_encode_block(MpegEncContext * s, DCTELEM * block,
			       int n);
static int h263_decode_motion(MpegEncContext * s, int pred);
static int h263p_decode_umotion(MpegEncContext * s, int pred);
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
    int format;

    align_put_bits(&s->pb);

    /* Update the pointer to last GOB */
    s->ptr_lastgob = pbBufPtr(&s->pb);
    s->gob_number = 0;

    put_bits(&s->pb, 22, 0x20); /* PSC */
    put_bits(&s->pb, 8, (((INT64)s->picture_number * 30 * FRAME_RATE_BASE) / 
                         s->frame_rate) & 0xff);

    put_bits(&s->pb, 1, 1);	/* marker */
    put_bits(&s->pb, 1, 0);	/* h263 id */
    put_bits(&s->pb, 1, 0);	/* split screen off */
    put_bits(&s->pb, 1, 0);	/* camera  off */
    put_bits(&s->pb, 1, 0);	/* freeze picture release off */
    
    format = h263_get_picture_format(s->width, s->height);
    if (!s->h263_plus) {
        /* H.263v1 */
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
        if (format == 7)
            put_bits(&s->pb,3,6); /* Custom Source Format */
        else
            put_bits(&s->pb, 3, format);
            
        put_bits(&s->pb,1,0); /* Custom PCF: off */
        s->umvplus = (s->pict_type == P_TYPE) && s->unrestricted_mv;
        put_bits(&s->pb, 1, s->umvplus); /* Unrestricted Motion Vector */
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
		
		if (format == 7) {
            /* Custom Picture Format (CPFMT) */
		
            put_bits(&s->pb,4,2); /* Aspect ratio: CIF 12:11 (4:3) picture */
            put_bits(&s->pb,9,(s->width >> 2) - 1);
            put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */
            put_bits(&s->pb,9,(s->height >> 2));
        }
        
        /* Unlimited Unrestricted Motion Vectors Indicator (UUI) */
        if (s->umvplus)
            put_bits(&s->pb,1,1); /* Limited according tables of Annex D */
        put_bits(&s->pb, 5, s->qscale);
    }

    put_bits(&s->pb, 1, 0);	/* no PEI */
}

int h263_encode_gob_header(MpegEncContext * s, int mb_line)
{
    int pdif=0;
    
    /* Check to see if we need to put a new GBSC */
    /* for RTP packetization                    */
    if (s->rtp_mode) {
        pdif = pbBufPtr(&s->pb) - s->ptr_lastgob;
        if (pdif >= s->rtp_payload_size) {
            /* Bad luck, packet must be cut before */
            align_put_bits(&s->pb);
            flush_put_bits(&s->pb);
            /* Call the RTP callback to send the last GOB */
            if (s->rtp_callback) {
                pdif = pbBufPtr(&s->pb) - s->ptr_lastgob;
                s->rtp_callback(s->ptr_lastgob, pdif, s->gob_number);
            }
            s->ptr_lastgob = pbBufPtr(&s->pb);
            put_bits(&s->pb, 17, 1); /* GBSC */
            s->gob_number = mb_line / s->gob_index;
            put_bits(&s->pb, 5, s->gob_number); /* GN */
            put_bits(&s->pb, 2, s->pict_type == I_TYPE); /* GFID */
            put_bits(&s->pb, 5, s->qscale); /* GQUANT */
            //fprintf(stderr,"\nGOB: %2d size: %d", s->gob_number - 1, pdif);
            return pdif;
       } else if (pdif + s->mb_line_avgsize >= s->rtp_payload_size) {
           /* Cut the packet before we can't */
           align_put_bits(&s->pb);
           flush_put_bits(&s->pb);
           /* Call the RTP callback to send the last GOB */
           if (s->rtp_callback) {
               pdif = pbBufPtr(&s->pb) - s->ptr_lastgob;
               s->rtp_callback(s->ptr_lastgob, pdif, s->gob_number);
           }
           s->ptr_lastgob = pbBufPtr(&s->pb);
           put_bits(&s->pb, 17, 1); /* GBSC */
           s->gob_number = mb_line / s->gob_index;
           put_bits(&s->pb, 5, s->gob_number); /* GN */
           put_bits(&s->pb, 2, s->pict_type == I_TYPE); /* GFID */
           put_bits(&s->pb, 5, s->qscale); /* GQUANT */
           //fprintf(stderr,"\nGOB: %2d size: %d", s->gob_number - 1, pdif);
           return pdif;
       }
   }
   return 0;
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
      
      if (!s->umvplus) {  
         h263_encode_motion(s, motion_x - pred_x);
         h263_encode_motion(s, motion_y - pred_y);
      }
      else {
         h263p_encode_umotion(s, motion_x - pred_x);
         h263p_encode_umotion(s, motion_y - pred_y);
         if (((motion_x - pred_x) == 1) && ((motion_y - pred_y) == 1))
            /* To prevent Start Code emulation */
            put_bits(&s->pb,1,1);
      }
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


void h263_pred_acdc(MpegEncContext * s, INT16 *block, int n)
{
    int x, y, wrap, a, c, pred_dc, scale, i;
    INT16 *dc_val, *ac_val, *ac_val1;

    /* find prediction */
    if (n < 4) {
        x = 2 * s->mb_x + 1 + (n & 1);
        y = 2 * s->mb_y + 1 + ((n & 2) >> 1);
        wrap = s->mb_width * 2 + 2;
        dc_val = s->dc_val[0];
        ac_val = s->ac_val[0][0];
        scale = s->y_dc_scale;
    } else {
        x = s->mb_x + 1;
        y = s->mb_y + 1;
        wrap = s->mb_width + 2;
        dc_val = s->dc_val[n - 4 + 1];
        ac_val = s->ac_val[n - 4 + 1][0];
        scale = s->c_dc_scale;
    }
    
    ac_val += ((y) * wrap + (x)) * 16;
    ac_val1 = ac_val;
    
    /* B C
     * A X 
     */
    a = dc_val[(x - 1) + (y) * wrap];
    c = dc_val[(x) + (y - 1) * wrap];
    
    pred_dc = 1024;
    if (s->ac_pred) {
        if (s->h263_aic_dir) {
            /* left prediction */
            if (a != 1024) {
                ac_val -= 16;
                for(i=1;i<8;i++) {
                    block[block_permute_op(i*8)] += ac_val[i];
                }
                pred_dc = a;
            }
        } else {
            /* top prediction */
            if (c != 1024) {
                ac_val -= 16 * wrap;
                for(i=1;i<8;i++) {
                    block[block_permute_op(i)] += ac_val[i + 8];
                }
                pred_dc = c;
            }
        }
    } else {
        /* just DC prediction */
        if (a != 1024 && c != 1024)
            pred_dc = (a + c) >> 1;
        else if (a != 1024)
            pred_dc = a;
        else
            pred_dc = c;
    }
    
    /* we assume pred is positive */
    block[0]=block[0]*scale + pred_dc;
    
    if (block[0] < 0)
        block[0] = 0;
    else if (!(block[0] & 1))
        block[0]++;
    
    /* Update AC/DC tables */
    dc_val[(x) + (y) * wrap] = block[0];
    
    /* left copy */
    for(i=1;i<8;i++)
        ac_val1[i] = block[block_permute_op(i * 8)];
    /* top copy */
    for(i=1;i<8;i++)
        ac_val1[8 + i] = block[block_permute_op(i)];
}


static inline int mid_pred(int a, int b, int c)
{
    int vmin, vmax;
    vmax = vmin = a;
    if (b < vmin)
        vmin = b;
    else
	vmax = b;

    if (c < vmin)
        vmin = c;
    else if (c > vmax)
        vmax = c;

    return a + b + c - vmin - vmax;
}

INT16 *h263_pred_motion(MpegEncContext * s, int block, 
                        int *px, int *py)
{
    int xy, y, wrap;
    INT16 *A, *B, *C, *mot_val;

    wrap = 2 * s->mb_width + 2;
    y = xy = 2 * s->mb_y + 1 + ((block >> 1) & 1); // y
    xy *= wrap; // y * wrap
    xy += 2 * s->mb_x + 1 + (block & 1); // x + y * wrap

    mot_val = s->motion_val[xy];

    /* special case for first line */
    if (y == 1 || s->first_slice_line || s->first_gob_line) {
        A = s->motion_val[xy - 1];
        *px = A[0];
        *py = A[1];
    } else {
        switch(block) {
        default:
        case 0:
            A = s->motion_val[xy - 1];
            B = s->motion_val[xy - wrap];
            C = s->motion_val[xy + 2 - wrap];
            break;
        case 1:
        case 2:
            A = s->motion_val[xy - 1];
            B = s->motion_val[xy - wrap];
            C = s->motion_val[xy + 1 - wrap];
            break;
        case 3:
            A = s->motion_val[xy - 1];
            B = s->motion_val[xy - 1 - wrap];
            C = s->motion_val[xy - wrap];
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

/* Encode MV differences on H.263+ with Unrestricted MV mode */
static void h263p_encode_umotion(MpegEncContext * s, int val)
{
    short sval = 0; 
    short i = 0;
    short n_bits = 0;
    short temp_val;
    int code = 0;
    int tcode;
    
    if ( val == 0)
        put_bits(&s->pb, 1, 1);
    else if (val == 1)
        put_bits(&s->pb, 3, 0);
    else if (val == -1)
        put_bits(&s->pb, 3, 2);
    else {
        
        sval = ((val < 0) ? (short)(-val):(short)val);
        temp_val = sval;
        
        while (temp_val != 0) {
            temp_val = temp_val >> 1;
            n_bits++;
        }
        
        i = n_bits - 1;
        while (i > 0) {
            tcode = (sval & (1 << (i-1))) >> (i-1);
            tcode = (tcode << 1) | 1;
            code = (code << 2) | tcode;
            i--;
        }
        code = ((code << 1) | (val < 0)) << 1;
        put_bits(&s->pb, (2*n_bits)+1, code);
        //printf("\nVal = %d\tCode = %d", sval, code);
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
        /* 0 cannot be represented also */
        else if (!level) {
            level = 1;
            block[0] = 1;
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

    put_bits(&s->pb, 16, 0);	        /* vop header */
    put_bits(&s->pb, 16, 0x1B6);	/* vop header */
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
    int a, b, c, xy, wrap, pred, scale;
    UINT16 *dc_val;

    /* find prediction */
    if (n < 4) {
	wrap = s->mb_width * 2 + 2;
	xy = 2 * s->mb_y + 1 + ((n & 2) >> 1);
        xy *= wrap;
	xy += 2 * s->mb_x + 1 + (n & 1);
	dc_val = s->dc_val[0];
	scale = s->y_dc_scale;
    } else {
	wrap = s->mb_width + 2;
	xy = s->mb_y + 1;
	xy *= wrap;
	xy += s->mb_x + 1;
	dc_val = s->dc_val[n - 4 + 1];
	scale = s->c_dc_scale;
    }

    /* B C
     * A X 
     */
    a = dc_val[xy - 1];
    b = dc_val[xy - 1 - wrap];
    c = dc_val[xy - wrap];

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
    *dc_val_ptr = &dc_val[xy];

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
                block[block_permute_op(i*8)] += ac_val[i];
            }
        } else {
            /* top prediction */
            ac_val -= 16 * wrap;
            for(i=1;i<8;i++) {
                block[block_permute_op(i)] += ac_val[i + 8];
            }
        }
    }
    /* left copy */
    for(i=1;i<8;i++)
        ac_val1[i] = block[block_permute_op(i * 8)];
    /* top copy */
    for(i=1;i<8;i++)
        ac_val1[8 + i] = block[block_permute_op(i)];
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
static VLC sprite_trajectory;

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
        init_vlc(&inter_MCBPC_vlc, 9, 25, 
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
        init_rl(&rl_intra_aic);
        init_vlc_rl(&rl_inter);
        init_vlc_rl(&rl_intra);
        init_vlc_rl(&rl_intra_aic);
        init_vlc(&dc_lum, 9, 13,
                 &DCtab_lum[0][1], 2, 1,
                 &DCtab_lum[0][0], 2, 1);
        init_vlc(&dc_chrom, 9, 13,
                 &DCtab_chrom[0][1], 2, 1,
                 &DCtab_chrom[0][0], 2, 1);
        init_vlc(&sprite_trajectory, 9, 15,
                 &sprite_trajectory_tab[0][1], 4, 2,
                 &sprite_trajectory_tab[0][0], 4, 2);
    }
}

int h263_decode_gob_header(MpegEncContext *s)
{
    unsigned int val, gfid;
    
    /* Check for GOB Start Code */
    val = show_bits(&s->gb, 16);
    if (val == 0) {
        /* We have a GBSC probably with GSTUFF */
        skip_bits(&s->gb, 16); /* Drop the zeros */
        while (get_bits1(&s->gb) == 0); /* Seek the '1' bit */
#ifdef DEBUG
        fprintf(stderr,"\nGOB Start Code at MB %d\n", (s->mb_y * s->mb_width) + s->mb_x);
#endif
        s->gob_number = get_bits(&s->gb, 5); /* GN */
        gfid = get_bits(&s->gb, 2); /* GFID */
        s->qscale = get_bits(&s->gb, 5); /* GQUANT */
#ifdef DEBUG
        fprintf(stderr, "\nGN: %u GFID: %u Quant: %u\n", s->gob_number, gfid, s->qscale);
#endif
        return 1;
    }
    return 0;
            
}

int h263_decode_mb(MpegEncContext *s,
                   DCTELEM block[6][64])
{
    int cbpc, cbpy, i, cbp, pred_x, pred_y, mx, my, dquant;
    INT16 *mot_val;
    static INT8 quant_tab[4] = { -1, -2, 1, 2 };
    
    if (s->pict_type == P_TYPE || s->pict_type==S_TYPE) {
        if (get_bits1(&s->gb)) {
            /* skip mb */
            s->mb_intra = 0;
            for(i=0;i<6;i++)
                s->block_last_index[i] = -1;
            s->mv_dir = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16;
            if(s->pict_type==S_TYPE && s->vol_sprite_usage==GMC_SPRITE){
                const int a= s->sprite_warping_accuracy;
//                int l = (1 << (s->f_code - 1)) * 32;

                s->mcsel=1;
                s->mv[0][0][0] = RSHIFT(s->sprite_offset[0][0], a-s->quarter_sample);
                s->mv[0][0][1] = RSHIFT(s->sprite_offset[0][1], a-s->quarter_sample);
/*                if (s->mv[0][0][0] < -l) s->mv[0][0][0]= -l;
                else if (s->mv[0][0][0] >= l) s->mv[0][0][0]= l-1;
                if (s->mv[0][0][1] < -l) s->mv[0][0][1]= -l;
                else if (s->mv[0][0][1] >= l) s->mv[0][0][1]= l-1;*/

                s->mb_skiped = 0;
            }else{
                s->mcsel=0;
                s->mv[0][0][0] = 0;
                s->mv[0][0][1] = 0;
                s->mb_skiped = 1;
            }
            return 0;
        }
        cbpc = get_vlc(&s->gb, &inter_MCBPC_vlc);
        //fprintf(stderr, "\tCBPC: %d", cbpc);
        if (cbpc < 0)
            return -1;
        if (cbpc > 20)
            cbpc+=3;
        else if (cbpc == 20)
            fprintf(stderr, "Stuffing !");
        
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
        if(s->pict_type==S_TYPE && s->vol_sprite_usage==GMC_SPRITE && (cbpc & 16) == 0)
            s->mcsel= get_bits1(&s->gb);
        else s->mcsel= 0;
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
            if (s->umvplus_dec)
               mx = h263p_decode_umotion(s, pred_x);
            else if(!s->mcsel)
               mx = h263_decode_motion(s, pred_x);
            else {
               const int a= s->sprite_warping_accuracy;
//        int l = (1 << (s->f_code - 1)) * 32;
               mx= RSHIFT(s->sprite_offset[0][0], a-s->quarter_sample);
//        if (mx < -l) mx= -l;
//        else if (mx >= l) mx= l-1;
            }
            if (mx >= 0xffff)
                return -1;
            
            if (s->umvplus_dec)
               my = h263p_decode_umotion(s, pred_y);
            else if(!s->mcsel)
               my = h263_decode_motion(s, pred_y);
            else{
               const int a= s->sprite_warping_accuracy;
//       int l = (1 << (s->f_code - 1)) * 32;
               my= RSHIFT(s->sprite_offset[0][1], a-s->quarter_sample);
//       if (my < -l) my= -l;
//       else if (my >= l) my= l-1;
            }
            if (my >= 0xffff)
                return -1;
            s->mv[0][0][0] = mx;
            s->mv[0][0][1] = my;
            /*fprintf(stderr, "\n MB %d", (s->mb_y * s->mb_width) + s->mb_x);
            fprintf(stderr, "\n\tmvx: %d\t\tpredx: %d", mx, pred_x);
            fprintf(stderr, "\n\tmvy: %d\t\tpredy: %d", my, pred_y);*/
            if (s->umvplus_dec && (mx - pred_x) == 1 && (my - pred_y) == 1)
               skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */
                           
        } else {
            s->mv_type = MV_TYPE_8X8;
            for(i=0;i<4;i++) {
                mot_val = h263_pred_motion(s, i, &pred_x, &pred_y);
                if (s->umvplus_dec)
                  mx = h263p_decode_umotion(s, pred_x);
                else
                  mx = h263_decode_motion(s, pred_x);
                if (mx >= 0xffff)
                    return -1;
                
                if (s->umvplus_dec)
                  my = h263p_decode_umotion(s, pred_y);
                else    
                  my = h263_decode_motion(s, pred_y);
                if (my >= 0xffff)
                    return -1;
                s->mv[0][i][0] = mx;
                s->mv[0][i][1] = my;
                if (s->umvplus_dec && (mx - pred_x) == 1 && (my - pred_y) == 1)
                  skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */
                mot_val[0] = mx;
                mot_val[1] = my;
            }
        }
    } else {
        s->ac_pred = 0;
        if (s->h263_pred || s->h263_aic) {
            s->ac_pred = get_bits1(&s->gb);
            if (s->ac_pred && s->h263_aic)
                s->h263_aic_dir = get_bits1(&s->gb);
        }
        if (s->h263_aic) {
            s->y_dc_scale = 2 * s->qscale;
            s->c_dc_scale = 2 * s->qscale;
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

/* Decodes RVLC of H.263+ UMV */
static int h263p_decode_umotion(MpegEncContext * s, int pred)
{
   int code = 0, sign;
   
   if (get_bits1(&s->gb)) /* Motion difference = 0 */
      return pred;
   
   code = 2 + get_bits1(&s->gb);
   
   while (get_bits1(&s->gb))
   {
      code <<= 1;
      code += get_bits1(&s->gb);
   }
   sign = code & 1;
   code >>= 1;
   
   code = (sign) ? (pred - code) : (pred + code);
#ifdef DEBUG
   fprintf(stderr,"H.263+ UMV Motion = %d\n", code);
#endif
   return code;   

}

static int h263_decode_block(MpegEncContext * s, DCTELEM * block,
                             int n, int coded)
{
    int code, level, i, j, last, run;
    RLTable *rl = &rl_inter;
    const UINT8 *scan_table;

    scan_table = zigzag_direct;
    if (s->h263_aic && s->mb_intra) {
        rl = &rl_intra_aic;
        i = 0;
        if (s->ac_pred) {
            if (s->h263_aic_dir) 
                scan_table = ff_alternate_vertical_scan; /* left */
            else
                scan_table = ff_alternate_horizontal_scan; /* top */
        }
    } else if (s->mb_intra) {
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
        if (s->mb_intra && s->h263_aic)
            goto not_coded;
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
        j = scan_table[i];
        block[j] = level;
        if (last)
            break;
        i++;
    }
not_coded:    
    if (s->mb_intra && s->h263_aic) {
        h263_pred_acdc(s, block, n);
        i = 64;
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
    s->picture_number = get_bits(&s->gb, 8); /* picture timestamp */
    
    if (get_bits1(&s->gb) != 1)
        return -1;	/* marker */
    if (get_bits1(&s->gb) != 0)
        return -1;	/* h263 id */
    skip_bits1(&s->gb);	/* split screen off */
    skip_bits1(&s->gb);	/* camera  off */
    skip_bits1(&s->gb);	/* freeze picture release off */

    /* Reset GOB number */
    s->gob_number = 0;
        
    format = get_bits(&s->gb, 3);

    if (format != 7 && format != 6) {
        s->h263_plus = 0;
        /* H.263v1 */
        width = h263_format[format][0];
        height = h263_format[format][1];
        if (!width)
            return -1;
        
        s->width = width;
        s->height = height;
        s->pict_type = I_TYPE + get_bits1(&s->gb);

        s->unrestricted_mv = get_bits1(&s->gb); 
        s->h263_long_vectors = s->unrestricted_mv;

        if (get_bits1(&s->gb) != 0)
            return -1;	/* SAC: off */
        if (get_bits1(&s->gb) != 0) {
            s->mv_type = MV_TYPE_8X8; /* Advanced prediction mode */
        }   
        
        if (get_bits1(&s->gb) != 0)
            return -1;	/* not PB frame */

        s->qscale = get_bits(&s->gb, 5);
        skip_bits1(&s->gb);	/* Continuous Presence Multipoint mode: off */
    } else {
        int ufep;
        
        /* H.263v2 */
        s->h263_plus = 1;
        ufep = get_bits(&s->gb, 3); /* Update Full Extended PTYPE */
        
        if (ufep == 1) {
            /* OPPTYPE */       
            format = get_bits(&s->gb, 3);
            skip_bits(&s->gb,1); /* Custom PCF */
            s->umvplus_dec = get_bits(&s->gb, 1); /* Unrestricted Motion Vector */
            skip_bits1(&s->gb); /* Syntax-based Arithmetic Coding (SAC) */
            if (get_bits1(&s->gb) != 0) {
                s->mv_type = MV_TYPE_8X8; /* Advanced prediction mode */
            }
            if (get_bits1(&s->gb) != 0) { /* Advanced Intra Coding (AIC) */
                s->h263_aic = 1;
            }
            skip_bits(&s->gb, 7);
            skip_bits(&s->gb, 3); /* Reserved */
        } else if (ufep != 0)
            return -1;
            
        /* MPPTYPE */
        s->pict_type = get_bits(&s->gb, 3) + 1;
        if (s->pict_type != I_TYPE &&
            s->pict_type != P_TYPE)
            return -1;
        skip_bits(&s->gb, 2);
        s->no_rounding = get_bits1(&s->gb);
        //fprintf(stderr, "\nRTYPE: %d", s->no_rounding);
        skip_bits(&s->gb, 4);
        
        /* Get the picture dimensions */
        if (ufep) {
            if (format == 6) {
                /* Custom Picture Format (CPFMT) */
                skip_bits(&s->gb, 4); /* aspect ratio */
                width = (get_bits(&s->gb, 9) + 1) * 4;
                skip_bits1(&s->gb);
                height = get_bits(&s->gb, 9) * 4;
#ifdef DEBUG 
                fprintf(stderr,"\nH.263+ Custom picture: %dx%d\n",width,height);
#endif            
            }
            else {
                width = h263_format[format][0];
                height = h263_format[format][1];
            }
            if ((width == 0) || (height == 0))
                return -1;
            s->width = width;
            s->height = height;
            if (s->umvplus_dec) {
                skip_bits1(&s->gb); /* Unlimited Unrestricted Motion Vectors Indicator (UUI) */
            }
        }
            
        s->qscale = get_bits(&s->gb, 5);
    }
    /* PEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }
    s->f_code = 1;
    return 0;
}

static void mpeg4_decode_sprite_trajectory(MpegEncContext * s)
{
    int i;
    int a= 2<<s->sprite_warping_accuracy;
    int rho= 3-s->sprite_warping_accuracy;
    int r=16/a;
    const int vop_ref[4][2]= {{0,0}, {s->width,0}, {0, s->height}, {s->width, s->height}}; // only true for rectangle shapes
    int d[4][2]={{0,0}, {0,0}, {0,0}, {0,0}};
    int sprite_ref[4][2];
    int virtual_ref[2][2];
    int w2, h2;
    int alpha=0, beta=0;
    int w= s->width;
    int h= s->height;
//printf("SP %d\n", s->sprite_warping_accuracy);
    for(i=0; i<s->num_sprite_warping_points; i++){
        int length;
        int x=0, y=0;

        length= get_vlc(&s->gb, &sprite_trajectory);
        if(length){
            x= get_bits(&s->gb, length);
//printf("lx %d %d\n", length, x);
            if ((x >> (length - 1)) == 0) /* if MSB not set it is negative*/
                x = - (x ^ ((1 << length) - 1));
        }
        if(!(s->divx_version==500 && s->divx_build==413)) skip_bits1(&s->gb); /* marker bit */
        
        length= get_vlc(&s->gb, &sprite_trajectory);
        if(length){
            y=get_bits(&s->gb, length);
//printf("ly %d %d\n", length, y);
            if ((y >> (length - 1)) == 0) /* if MSB not set it is negative*/
                y = - (y ^ ((1 << length) - 1));
        }
        skip_bits1(&s->gb); /* marker bit */
//printf("%d %d %d %d\n", x, y, i, s->sprite_warping_accuracy);
//if(i>0 && (x!=0 || y!=0)) printf("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
//x=y=0;
        d[i][0]= x;
        d[i][1]= y;
    }

    while((1<<alpha)<w) alpha++;
    while((1<<beta )<h) beta++; // there seems to be a typo in the mpeg4 std for the definition of w' and h'
    w2= 1<<alpha;
    h2= 1<<beta;

// Note, the 4th point isnt used for GMC
/*
    sprite_ref[0][0]= (a>>1)*(2*vop_ref[0][0] + d[0][0]);
    sprite_ref[0][1]= (a>>1)*(2*vop_ref[0][1] + d[0][1]);
    sprite_ref[1][0]= (a>>1)*(2*vop_ref[1][0] + d[0][0] + d[1][0]);
    sprite_ref[1][1]= (a>>1)*(2*vop_ref[1][1] + d[0][1] + d[1][1]);
    sprite_ref[2][0]= (a>>1)*(2*vop_ref[2][0] + d[0][0] + d[2][0]);
    sprite_ref[2][1]= (a>>1)*(2*vop_ref[2][1] + d[0][1] + d[2][1]);
*/
//FIXME DIVX5 vs. mpeg4 ?
    sprite_ref[0][0]= a*vop_ref[0][0] + d[0][0];
    sprite_ref[0][1]= a*vop_ref[0][1] + d[0][1];
    sprite_ref[1][0]= a*vop_ref[1][0] + d[0][0] + d[1][0];
    sprite_ref[1][1]= a*vop_ref[1][1] + d[0][1] + d[1][1];
    sprite_ref[2][0]= a*vop_ref[2][0] + d[0][0] + d[2][0];
    sprite_ref[2][1]= a*vop_ref[2][1] + d[0][1] + d[2][1];
/*    sprite_ref[3][0]= (a>>1)*(2*vop_ref[3][0] + d[0][0] + d[1][0] + d[2][0] + d[3][0]);
    sprite_ref[3][1]= (a>>1)*(2*vop_ref[3][1] + d[0][1] + d[1][1] + d[2][1] + d[3][1]); */
    
// this is mostly identical to the mpeg4 std (and is totally unreadable because of that ...)
// perhaps it should be reordered to be more readable ...
// the idea behind this virtual_ref mess is to be able to use shifts later per pixel instead of divides
// so the distance between points is converted from w&h based to w2&h2 based which are of the 2^x form
    virtual_ref[0][0]= 16*(vop_ref[0][0] + w2) 
        + RDIV(((w - w2)*(r*sprite_ref[0][0] - 16*vop_ref[0][0]) + w2*(r*sprite_ref[1][0] - 16*vop_ref[1][0])),w);
    virtual_ref[0][1]= 16*vop_ref[0][1] 
        + RDIV(((w - w2)*(r*sprite_ref[0][1] - 16*vop_ref[0][1]) + w2*(r*sprite_ref[1][1] - 16*vop_ref[1][1])),w);
    virtual_ref[1][0]= 16*vop_ref[0][0] 
        + RDIV(((h - h2)*(r*sprite_ref[0][0] - 16*vop_ref[0][0]) + h2*(r*sprite_ref[2][0] - 16*vop_ref[2][0])),h);
    virtual_ref[1][1]= 16*(vop_ref[0][1] + h2) 
        + RDIV(((h - h2)*(r*sprite_ref[0][1] - 16*vop_ref[0][1]) + h2*(r*sprite_ref[2][1] - 16*vop_ref[2][1])),h);

    switch(s->num_sprite_warping_points)
    {
        case 0:
            s->sprite_offset[0][0]= 0;
            s->sprite_offset[0][1]= 0;
            s->sprite_offset[1][0]= 0;
            s->sprite_offset[1][1]= 0;
            s->sprite_delta[0][0][0]= a;
            s->sprite_delta[0][0][1]= 0;
            s->sprite_delta[0][1][0]= 0;
            s->sprite_delta[0][1][1]= a;
            s->sprite_delta[1][0][0]= a;
            s->sprite_delta[1][0][1]= 0;
            s->sprite_delta[1][1][0]= 0;
            s->sprite_delta[1][1][1]= a;
            s->sprite_shift[0][0]= 0;
            s->sprite_shift[0][1]= 0;
            s->sprite_shift[1][0]= 0;
            s->sprite_shift[1][1]= 0;
            break;
        case 1: //GMC only
            s->sprite_offset[0][0]= sprite_ref[0][0] - a*vop_ref[0][0];
            s->sprite_offset[0][1]= sprite_ref[0][1] - a*vop_ref[0][1];
            s->sprite_offset[1][0]= ((sprite_ref[0][0]>>1)|(sprite_ref[0][0]&1)) - a*(vop_ref[0][0]/2);
            s->sprite_offset[1][1]= ((sprite_ref[0][1]>>1)|(sprite_ref[0][1]&1)) - a*(vop_ref[0][1]/2);
            s->sprite_delta[0][0][0]= a;
            s->sprite_delta[0][0][1]= 0;
            s->sprite_delta[0][1][0]= 0;
            s->sprite_delta[0][1][1]= a;
            s->sprite_delta[1][0][0]= a;
            s->sprite_delta[1][0][1]= 0;
            s->sprite_delta[1][1][0]= 0;
            s->sprite_delta[1][1][1]= a;
            s->sprite_shift[0][0]= 0;
            s->sprite_shift[0][1]= 0;
            s->sprite_shift[1][0]= 0;
            s->sprite_shift[1][1]= 0;
            break;
        case 2:
        case 3: //FIXME
            s->sprite_offset[0][0]= (sprite_ref[0][0]<<(alpha+rho))
                                                  + ((-r*sprite_ref[0][0] + virtual_ref[0][0])*(-vop_ref[0][0])
                                                    +( r*sprite_ref[0][1] - virtual_ref[0][1])*(-vop_ref[0][1]));
            s->sprite_offset[0][1]= (sprite_ref[0][1]<<(alpha+rho))
                                                  + ((-r*sprite_ref[0][1] + virtual_ref[0][1])*(-vop_ref[0][0])
                                                    +(-r*sprite_ref[0][0] + virtual_ref[0][0])*(-vop_ref[0][1]));
            s->sprite_offset[1][0]= ((-r*sprite_ref[0][0] + virtual_ref[0][0])*(-2*vop_ref[0][0] + 1)
                                 +( r*sprite_ref[0][1] - virtual_ref[0][1])*(-2*vop_ref[0][1] + 1)
                                 +2*w2*r*sprite_ref[0][0] - 16*w2);
            s->sprite_offset[1][1]= ((-r*sprite_ref[0][1] + virtual_ref[0][1])*(-2*vop_ref[0][0] + 1) 
                                 +(-r*sprite_ref[0][0] + virtual_ref[0][0])*(-2*vop_ref[0][1] + 1)
                                 +2*w2*r*sprite_ref[0][1] - 16*w2);
            s->sprite_delta[0][0][0]=   (-r*sprite_ref[0][0] + virtual_ref[0][0]);
            s->sprite_delta[0][0][1]=   ( r*sprite_ref[0][1] - virtual_ref[0][1]);
            s->sprite_delta[0][1][0]=   (-r*sprite_ref[0][1] + virtual_ref[0][1]);
            s->sprite_delta[0][1][1]=   (-r*sprite_ref[0][0] + virtual_ref[0][0]);
            s->sprite_delta[1][0][0]= 4*(-r*sprite_ref[0][0] + virtual_ref[0][0]);
            s->sprite_delta[1][0][1]= 4*( r*sprite_ref[0][1] - virtual_ref[0][1]);
            s->sprite_delta[1][1][0]= 4*(-r*sprite_ref[0][1] + virtual_ref[0][1]);
            s->sprite_delta[1][1][1]= 4*(-r*sprite_ref[0][0] + virtual_ref[0][0]);
            s->sprite_shift[0][0]= alpha+rho;
            s->sprite_shift[0][1]= alpha+rho;
            s->sprite_shift[1][0]= alpha+rho+2;
            s->sprite_shift[1][1]= alpha+rho+2;
            break;
//        case 3:
            break;
    }
/*printf("%d %d\n", s->sprite_delta[0][0][0], a<<s->sprite_shift[0][0]);
printf("%d %d\n", s->sprite_delta[0][0][1], 0);
printf("%d %d\n", s->sprite_delta[0][1][0], 0);
printf("%d %d\n", s->sprite_delta[0][1][1], a<<s->sprite_shift[0][1]);
printf("%d %d\n", s->sprite_delta[1][0][0], a<<s->sprite_shift[1][0]);
printf("%d %d\n", s->sprite_delta[1][0][1], 0);
printf("%d %d\n", s->sprite_delta[1][1][0], 0);
printf("%d %d\n", s->sprite_delta[1][1][1], a<<s->sprite_shift[1][1]);*/
    /* try to simplify the situation */ 
    if(   s->sprite_delta[0][0][0] == a<<s->sprite_shift[0][0]
       && s->sprite_delta[0][0][1] == 0
       && s->sprite_delta[0][1][0] == 0
       && s->sprite_delta[0][1][1] == a<<s->sprite_shift[0][1]
       && s->sprite_delta[1][0][0] == a<<s->sprite_shift[1][0]
       && s->sprite_delta[1][0][1] == 0
       && s->sprite_delta[1][1][0] == 0
       && s->sprite_delta[1][1][1] == a<<s->sprite_shift[1][1])
    {
        s->sprite_offset[0][0]>>=s->sprite_shift[0][0];
        s->sprite_offset[0][1]>>=s->sprite_shift[0][1];
        s->sprite_offset[1][0]>>=s->sprite_shift[1][0];
        s->sprite_offset[1][1]>>=s->sprite_shift[1][1];
        s->sprite_delta[0][0][0]= a;
        s->sprite_delta[0][0][1]= 0;
        s->sprite_delta[0][1][0]= 0;
        s->sprite_delta[0][1][1]= a;
        s->sprite_delta[1][0][0]= a;
        s->sprite_delta[1][0][1]= 0;
        s->sprite_delta[1][1][0]= 0;
        s->sprite_delta[1][1][1]= a;
        s->sprite_shift[0][0]= 0;
        s->sprite_shift[0][1]= 0;
        s->sprite_shift[1][0]= 0;
        s->sprite_shift[1][1]= 0;
        s->real_sprite_warping_points=1;
    }
    else
        s->real_sprite_warping_points= s->num_sprite_warping_points;

//FIXME convert stuff if accurace != 3
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
//printf("startcode %X %d\n", startcode, get_bits_count(&s->gb));
    if (startcode == 0x120) { // Video Object Layer
        int time_increment_resolution, width, height, vo_ver_id;

        /* vol header */
        skip_bits(&s->gb, 1); /* random access */
        skip_bits(&s->gb, 8); /* vo_type */
        if (get_bits1(&s->gb) != 0) { /* is_ol_id */
            vo_ver_id = get_bits(&s->gb, 4); /* vo_ver_id */
            skip_bits(&s->gb, 3); /* vo_priority */
        } else {
            vo_ver_id = 1;
        }
        
        s->aspect_ratio_info= get_bits(&s->gb, 4);
	if(s->aspect_ratio_info == EXTENDET_PAR){
            skip_bits(&s->gb, 8); //par_width
            skip_bits(&s->gb, 8); // par_height
        }
        if(get_bits1(&s->gb)){ /* vol control parameter */
            printf("vol control parameter not supported\n");
            return -1;   
        }
        s->shape = get_bits(&s->gb, 2); /* vol shape */
        if(s->shape == GRAY_SHAPE && vo_ver_id != 1){
            printf("Gray shape not supported\n");
            skip_bits(&s->gb, 4);  //video_object_layer_shape_extension
        }

        skip_bits1(&s->gb);   /* marker */
        
        time_increment_resolution = get_bits(&s->gb, 16);
        s->time_increment_bits = av_log2(time_increment_resolution - 1) + 1;
        if (s->time_increment_bits < 1)
            s->time_increment_bits = 1;
        skip_bits1(&s->gb);   /* marker */

        if (get_bits1(&s->gb) != 0) {   /* fixed_vop_rate  */
            skip_bits(&s->gb, s->time_increment_bits);
        }

        if (s->shape != BIN_ONLY_SHAPE) {
            if (s->shape == RECT_SHAPE) {
                skip_bits1(&s->gb);   /* marker */
                width = get_bits(&s->gb, 13);
                skip_bits1(&s->gb);   /* marker */
                height = get_bits(&s->gb, 13);
                skip_bits1(&s->gb);   /* marker */
            }
            
            skip_bits1(&s->gb);   /* interlaced */
            if(!get_bits1(&s->gb)) printf("OBMC not supported\n");   /* OBMC Disable */
            if (vo_ver_id == 1) {
                s->vol_sprite_usage = get_bits1(&s->gb); /* vol_sprite_usage */
            } else {
                s->vol_sprite_usage = get_bits(&s->gb, 2); /* vol_sprite_usage */
            }
            if(s->vol_sprite_usage==STATIC_SPRITE) printf("Static Sprites not supported\n");
            if(s->vol_sprite_usage==STATIC_SPRITE || s->vol_sprite_usage==GMC_SPRITE){
                if(s->vol_sprite_usage==STATIC_SPRITE){
                    s->sprite_width = get_bits(&s->gb, 13);
                    skip_bits1(&s->gb); /* marker */
                    s->sprite_height= get_bits(&s->gb, 13);
                    skip_bits1(&s->gb); /* marker */
                    s->sprite_left  = get_bits(&s->gb, 13);
                    skip_bits1(&s->gb); /* marker */
                    s->sprite_top   = get_bits(&s->gb, 13);
                    skip_bits1(&s->gb); /* marker */
                }
                s->num_sprite_warping_points= get_bits(&s->gb, 6);
                s->sprite_warping_accuracy = get_bits(&s->gb, 2);
                s->sprite_brightness_change= get_bits1(&s->gb);
                if(s->vol_sprite_usage==STATIC_SPRITE)
                    s->low_latency_sprite= get_bits1(&s->gb);            
            }
            // FIXME sadct disable bit if verid!=1 && shape not rect
            
            if (get_bits1(&s->gb) == 1) {   /* not_8_bit */
                s->quant_precision = get_bits(&s->gb, 4); /* quant_precision */
                skip_bits(&s->gb, 4); /* bits_per_pixel */
            } else {
                s->quant_precision = 5;
            }
            
            // FIXME a bunch of grayscale shape things
            if(get_bits1(&s->gb)) printf("Quant-Type not supported\n");  /* vol_quant_type */ //FIXME
            if(vo_ver_id != 1)
                 s->quarter_sample= get_bits1(&s->gb);
            else s->quarter_sample=0;
#if 0
            if(get_bits1(&s->gb)) printf("Complexity est disabled\n");
            if(get_bits1(&s->gb)) printf("resync disable\n");
#else
            skip_bits1(&s->gb);   /* complexity_estimation_disabled */
            skip_bits1(&s->gb);   /* resync_marker_disabled */
#endif
            s->data_partioning= get_bits1(&s->gb);
            if(s->data_partioning){
                printf("data partitioning not supported\n");
                skip_bits1(&s->gb); // reversible vlc
            }
            
            if(vo_ver_id != 1) {
                s->new_pred= get_bits1(&s->gb);
                if(s->new_pred){
                    printf("new pred not supported\n");
                    skip_bits(&s->gb, 2); /* requested upstream message type */
                    skip_bits1(&s->gb); /* newpred segment type */
                }
                s->reduced_res_vop= get_bits1(&s->gb);
                if(s->reduced_res_vop) printf("reduced resolution VOP not supported\n");
            }
            else{
                s->new_pred=0;
                s->reduced_res_vop= 0;
            }

            s->scalability= get_bits1(&s->gb);
            if (s->scalability) {
                printf("bad scalability!!!\n");
                return -1;
            }
        }
//printf("end Data %X %d\n", show_bits(&s->gb, 32), get_bits_count(&s->gb)&0x7);
        goto redo;
    } else if (startcode == 0x1b2) { //userdata
        char buf[256];
        int i;
        int e;
        int ver, build;

//printf("user Data %X\n", show_bits(&s->gb, 32));
        buf[0]= show_bits(&s->gb, 8);
        for(i=1; i<256; i++){
            buf[i]= show_bits(&s->gb, 16)&0xFF;
            if(buf[i]==0) break;
            skip_bits(&s->gb, 8);
        }
        buf[255]=0;
        e=sscanf(buf, "DivX%dBuild%d", &ver, &build);
        if(e==2){
            s->divx_version= ver;
            s->divx_build= build;
            if(s->picture_number==0){
                printf("This file was encoded with DivX%d Build%d\n", ver, build);
                if(ver==500 && build==413){ //most likely all version are indeed totally buggy but i dunno for sure ...
                    printf("WARNING: this version of DivX is not MPEG4 compatible, trying to workaround these bugs...\n");
                }else{
                    printf("hmm, i havnt seen that version of divx yet, lets assume they fixed these bugs ...\n"
                           "using mpeg4 decoder, if it fails contact the developers (of ffmpeg)\n");
                }
            }
        }
//printf("User Data: %s\n", buf);
        goto redo;
    } else if (startcode != 0x1b6) { //VOP
        goto redo;
    }

    s->pict_type = get_bits(&s->gb, 2) + 1;	/* pict type: I = 0 , P = 1 */
    if(s->pict_type == B_TYPE)
    {
        printf("B-VOP\n");
	return -1;
    }
 
    /* XXX: parse time base */
    time_incr = 0;
    while (get_bits1(&s->gb) != 0) 
        time_incr++;

    skip_bits1(&s->gb);   	/* marker */
    skip_bits(&s->gb, s->time_increment_bits);
    skip_bits1(&s->gb);   	/* marker */
    /* vop coded */
    if (get_bits1(&s->gb) != 1)
        goto redo;
    
    if (s->shape != BIN_ONLY_SHAPE && ( s->pict_type == P_TYPE
                          || (s->pict_type == S_TYPE && s->vol_sprite_usage==GMC_SPRITE))) {
        /* rounding type for motion estimation */
	s->no_rounding = get_bits1(&s->gb);
    } else {
	s->no_rounding = 0;
    }
//FIXME reduced res stuff

     if (s->shape != RECT_SHAPE) {
         if (s->vol_sprite_usage != 1 || s->pict_type != I_TYPE) {
             int width, height, hor_spat_ref, ver_spat_ref;
 
             width = get_bits(&s->gb, 13);
             skip_bits1(&s->gb);   /* marker */
             height = get_bits(&s->gb, 13);
             skip_bits1(&s->gb);   /* marker */
             hor_spat_ref = get_bits(&s->gb, 13); /* hor_spat_ref */
             skip_bits1(&s->gb);   /* marker */
             ver_spat_ref = get_bits(&s->gb, 13); /* ver_spat_ref */
         }
         skip_bits1(&s->gb); /* change_CR_disable */
 
         if (get_bits1(&s->gb) != 0) {
             skip_bits(&s->gb, 8); /* constant_alpha_value */
         }
     }
//FIXME complexity estimation stuff
     
     if (s->shape != BIN_ONLY_SHAPE) {
         skip_bits(&s->gb, 3); /* intra dc VLC threshold */
         //FIXME interlaced specific bits
     }

     if(s->pict_type == S_TYPE && (s->vol_sprite_usage==STATIC_SPRITE || s->vol_sprite_usage==GMC_SPRITE)){
         if(s->num_sprite_warping_points){
             mpeg4_decode_sprite_trajectory(s);
         }
         if(s->sprite_brightness_change) printf("sprite_brightness_change not supported\n");
         if(s->vol_sprite_usage==STATIC_SPRITE) printf("static sprite not supported\n");
     }

     if (s->shape != BIN_ONLY_SHAPE) {
         /* note: we do not use quant_precision to avoid problem if no
            MPEG4 vol header as it is found on some old opendivx
            movies */
         s->qscale = get_bits(&s->gb, 5);
  
         if (s->pict_type != I_TYPE) {
             s->f_code = get_bits(&s->gb, 3);	/* fcode_for */
         }
         if (s->pict_type == B_TYPE) {
             s->b_code = get_bits(&s->gb, 3);
         }
//printf("quant:%d fcode:%d\n", s->qscale, s->f_code);
         if(!s->scalability){
             if (s->shape!=RECT_SHAPE && s->pict_type!=I_TYPE) {
                 skip_bits1(&s->gb); // vop shape coding type
             }
         }
     }
//printf("end Data %X %d\n", show_bits(&s->gb, 32), get_bits_count(&s->gb)&0x7);
     s->picture_number++; // better than pic number==0 allways ;)
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

