/*
 * MSMPEG4 backend for ffmpeg encoder and decoder
 * Copyright (c) 2001 Gerard Lantau.
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
#include "common.h"
#include "dsputil.h"
#include "mpegvideo.h"

/*
 * You can also call this codec : MPEG4 with a twist ! 
 *
 * TODO: 
 *        - (encoding) select best mv table (two choices)
 *        - (encoding) select best vlc/dc table 
 *        - (decoding) handle slice indication
 */
//#define DEBUG

/* motion vector table */
typedef struct MVTable {
    int n;
    const UINT16 *table_mv_code;
    const UINT8 *table_mv_bits;
    const UINT8 *table_mvx;
    const UINT8 *table_mvy;
    UINT16 *table_mv_index; /* encoding: convert mv to index in table_mv */
    VLC vlc;                /* decoding: vlc */
} MVTable;

static void msmpeg4_encode_block(MpegEncContext * s, DCTELEM * block, int n);
static int msmpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                                int n, int coded);
static int msmpeg4_decode_dc(MpegEncContext * s, int n, int *dir_ptr);
static int msmpeg4_decode_motion(MpegEncContext * s, 
                                 int *mx_ptr, int *my_ptr);

#ifdef DEBUG
int intra_count = 0;
int frame_count = 0;
#endif
/* XXX: move it to mpegvideo.h */

static int init_done = 0;

#include "msmpeg4data.h"

#ifdef STATS

const char *st_names[ST_NB] = {
    "unknown",
    "dc",
    "intra_ac",
    "inter_ac",
    "intra_mb",
    "inter_mb",
    "mv",
};

int st_current_index = 0;
unsigned int st_bit_counts[ST_NB];
unsigned int st_out_bit_counts[ST_NB];

#define set_stat(var) st_current_index = var;

void print_stats(void)
{
    unsigned int total;
    int i;

    printf("Input:\n");
    total = 0;
    for(i=0;i<ST_NB;i++)
        total += st_bit_counts[i];
    if (total == 0)
        total = 1;
    for(i=0;i<ST_NB;i++) {
        printf("%-10s : %10.1f %5.1f%%\n", 
               st_names[i], 
               (double)st_bit_counts[i] / 8.0, 
               (double)st_bit_counts[i] * 100.0 / total);
    }
    printf("%-10s : %10.1f %5.1f%%\n",
           "total", 
           (double)total / 8.0, 
           100.0);

    printf("Output:\n");
    total = 0;
    for(i=0;i<ST_NB;i++)
        total += st_out_bit_counts[i];
    if (total == 0)
        total = 1;
    for(i=0;i<ST_NB;i++) {
        printf("%-10s : %10.1f %5.1f%%\n", 
               st_names[i], 
               (double)st_out_bit_counts[i] / 8.0, 
               (double)st_out_bit_counts[i] * 100.0 / total);
    }
    printf("%-10s : %10.1f %5.1f%%\n",
           "total", 
           (double)total / 8.0, 
           100.0);
}

#else

#define set_stat(var)

#endif

/* build the table which associate a (x,y) motion vector to a vlc */
static void init_mv_table(MVTable *tab)
{
    int i, x, y;

    tab->table_mv_index = malloc(sizeof(UINT16) * 4096);
    /* mark all entries as not used */
    for(i=0;i<4096;i++)
        tab->table_mv_index[i] = tab->n;
    
    for(i=0;i<tab->n;i++) {
        x = tab->table_mvx[i];
        y = tab->table_mvy[i];
        tab->table_mv_index[(x << 6) | y] = i;
    }
}

static void code012(PutBitContext *pb, int n)
{
    if (n == 0) {
        put_bits(pb, 1, 0);
    } else {
        put_bits(pb, 1, 1);
        put_bits(pb, 1, (n >= 2));
    }
}

/* write MSMPEG4 V3 compatible frame header */
void msmpeg4_encode_picture_header(MpegEncContext * s, int picture_number)
{
    int i;

    align_put_bits(&s->pb);

    put_bits(&s->pb, 2, s->pict_type - 1);

    put_bits(&s->pb, 5, s->qscale);

    s->rl_table_index = 2;
    s->rl_chroma_table_index = 1; /* only for I frame */
    s->dc_table_index = 1;
    s->mv_table_index = 1; /* only if P frame */
    s->use_skip_mb_code = 1; /* only if P frame */
    
    if (s->pict_type == I_TYPE) {
        put_bits(&s->pb, 5, 0x17); /* indicate only one "slice" */

        code012(&s->pb, s->rl_chroma_table_index);
        code012(&s->pb, s->rl_table_index);

        put_bits(&s->pb, 1, s->dc_table_index);
        s->no_rounding = 1;
    } else {
        put_bits(&s->pb, 1, s->use_skip_mb_code);
        
        s->rl_chroma_table_index = s->rl_table_index;
        code012(&s->pb, s->rl_table_index);

        put_bits(&s->pb, 1, s->dc_table_index);

        put_bits(&s->pb, 1, s->mv_table_index);
        s->no_rounding ^= 1;
    }

    if (!init_done) {
        /* init various encoding tables */
        init_done = 1;
        init_mv_table(&mv_tables[0]);
        init_mv_table(&mv_tables[1]);
        for(i=0;i<NB_RL_TABLES;i++)
            init_rl(&rl_table[i]);
    }

#ifdef DEBUG
    intra_count = 0;
    printf("*****frame %d:\n", frame_count++);
#endif
}

/* predict coded block */
static inline int coded_block_pred(MpegEncContext * s, int n, UINT8 **coded_block_ptr)
{
    int x, y, wrap, pred, a, b, c;

    x = 2 * s->mb_x + 1 + (n & 1);
    y = 2 * s->mb_y + 1 + ((n & 2) >> 1);
    wrap = s->mb_width * 2 + 2;

    /* B C
     * A X 
     */
    a = s->coded_block[(x - 1) + (y) * wrap];
    b = s->coded_block[(x - 1) + (y - 1) * wrap];
    c = s->coded_block[(x) + (y - 1) * wrap];
    
    if (b == c) {
        pred = a;
    } else {
        pred = c;
    }
    
    /* store value */
    *coded_block_ptr = &s->coded_block[(x) + (y) * wrap];

    return pred;
}

static void msmpeg4_encode_motion(MpegEncContext * s, 
                                  int mx, int my)
{
    int code;
    MVTable *mv;

    /* modulo encoding */
    /* WARNING : you cannot reach all the MVs even with the modulo
       encoding. This is a somewhat strange compromise they took !!!  */
    if (mx <= -64)
        mx += 64;
    else if (mx >= 64)
        mx -= 64;
    if (my <= -64)
        my += 64;
    else if (my >= 64)
        my -= 64;
    
    mx += 32;
    my += 32;
#if 0
    if ((unsigned)mx >= 64 ||
        (unsigned)my >= 64) 
        fprintf(stderr, "error mx=%d my=%d\n", mx, my);
#endif
    mv = &mv_tables[s->mv_table_index];

    code = mv->table_mv_index[(mx << 6) | my];
    set_stat(ST_MV);
    put_bits(&s->pb, 
             mv->table_mv_bits[code], 
             mv->table_mv_code[code]);
    if (code == mv->n) {
        /* escape : code litterally */
        put_bits(&s->pb, 6, mx);
        put_bits(&s->pb, 6, my);
    }
}

void msmpeg4_encode_mb(MpegEncContext * s, 
                       DCTELEM block[6][64],
                       int motion_x, int motion_y)
{
    int cbp, coded_cbp, i;
    int pred_x, pred_y;
    UINT8 *coded_block;

    if (!s->mb_intra) {
	/* compute cbp */
        set_stat(ST_INTER_MB);
	cbp = 0;
	for (i = 0; i < 6; i++) {
	    if (s->block_last_index[i] >= 0)
		cbp |= 1 << (5 - i);
	}
	if (s->use_skip_mb_code && (cbp | motion_x | motion_y) == 0) {
	    /* skip macroblock */
	    put_bits(&s->pb, 1, 1);
	    return;
	}
        if (s->use_skip_mb_code)
            put_bits(&s->pb, 1, 0);	/* mb coded */
        
        put_bits(&s->pb, 
                 table_mb_non_intra[cbp + 64][1], 
                 table_mb_non_intra[cbp + 64][0]);

        /* motion vector */
        h263_pred_motion(s, 0, &pred_x, &pred_y);
        msmpeg4_encode_motion(s, motion_x - pred_x, 
                              motion_y - pred_y);
    } else {
	/* compute cbp */
	cbp = 0;
        coded_cbp = 0;
	for (i = 0; i < 6; i++) {
            int val, pred;
            val = (s->block_last_index[i] >= 1);
            cbp |= val << (5 - i);
            if (i < 4) {
                /* predict value for close blocks only for luma */
                pred = coded_block_pred(s, i, &coded_block);
                *coded_block = val;
                val = val ^ pred;
            }
            coded_cbp |= val << (5 - i);
	}
#if 0
        if (coded_cbp)
            printf("cbp=%x %x\n", cbp, coded_cbp);
#endif

	if (s->pict_type == I_TYPE) {
            set_stat(ST_INTRA_MB);
            put_bits(&s->pb, 
                     table_mb_intra[coded_cbp][1], table_mb_intra[coded_cbp][0]);
        } else {
            if (s->use_skip_mb_code)
                put_bits(&s->pb, 1, 0);	/* mb coded */
            put_bits(&s->pb, 
                     table_mb_non_intra[cbp][1], 
                     table_mb_non_intra[cbp][0]);
        }
        set_stat(ST_INTRA_MB);
        put_bits(&s->pb, 1, 0);	/* no AC prediction yet */
    }

    for (i = 0; i < 6; i++) {
        msmpeg4_encode_block(s, block[i], i);
    }
}


/* strongly inspirated from MPEG4, but not exactly the same ! */
void msmpeg4_dc_scale(MpegEncContext * s)
{
    int scale;

    if (s->qscale < 5)
        scale = 8;
    else if (s->qscale < 9)
        scale = 2 * s->qscale;
    else 
        scale = s->qscale + 8;
    s->y_dc_scale = scale;
    s->c_dc_scale = (s->qscale + 13) / 2;
}

/* dir = 0: left, dir = 1: top prediction */
static int msmpeg4_pred_dc(MpegEncContext * s, int n, 
                           INT16 **dc_val_ptr, int *dir_ptr)
{
    int a, b, c, x, y, wrap, pred, scale;
    INT16 *dc_val;

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

    /* XXX: the following solution consumes divisions, but it does not
       necessitate to modify mpegvideo.c. The problem comes from the
       fact they decided to store the quantized DC (which would lead
       to problems if Q could vary !) */
    a = (a + (scale >> 1)) / scale;
    b = (b + (scale >> 1)) / scale;
    c = (c + (scale >> 1)) / scale;

    /* XXX: WARNING: they did not choose the same test as MPEG4. This
       is very important ! */
    if (abs(a - b) <= abs(b - c)) {
	pred = c;
        *dir_ptr = 1;
    } else {
	pred = a;
        *dir_ptr = 0;
    }

    /* update predictor */
    *dc_val_ptr = &dc_val[(x) + (y) * wrap];
    return pred;
}

#define DC_MAX 119

static void msmpeg4_encode_dc(MpegEncContext * s, int level, int n, int *dir_ptr)
{
    int sign, code;
    int pred;
    INT16 *dc_val;

    pred = msmpeg4_pred_dc(s, n, &dc_val, dir_ptr);

    /* update predictor */
    if (n < 4) {
        *dc_val = level * s->y_dc_scale;
    } else {
        *dc_val = level * s->c_dc_scale;
    }

    /* do the prediction */
    level -= pred;

    sign = 0;
    if (level < 0) {
        level = -level;
        sign = 1;
    }
    
    code = level;
    if (code > DC_MAX) 
        code = DC_MAX;

    if (s->dc_table_index == 0) {
        if (n < 4) {
            put_bits(&s->pb, table0_dc_lum[code][1], table0_dc_lum[code][0]);
        } else {
            put_bits(&s->pb, table0_dc_chroma[code][1], table0_dc_chroma[code][0]);
        }
    } else {
        if (n < 4) {
            put_bits(&s->pb, table1_dc_lum[code][1], table1_dc_lum[code][0]);
        } else {
            put_bits(&s->pb, table1_dc_chroma[code][1], table1_dc_chroma[code][0]);
        }
    }
        
    if (code == DC_MAX)
        put_bits(&s->pb, 8, level);
        
    if (level != 0) {
        put_bits(&s->pb, 1, sign);
    }
}

/* Encoding of a block. Very similar to MPEG4 except for a different
   escape coding (same as H263) and more vlc tables.
 */
static void msmpeg4_encode_block(MpegEncContext * s, DCTELEM * block, int n)
{
    int level, run, last, i, j, last_index;
    int last_non_zero, sign, slevel;
    int code, run_diff, dc_pred_dir;
    const RLTable *rl;

    if (s->mb_intra) {
        set_stat(ST_DC);
        msmpeg4_encode_dc(s, block[0], n, &dc_pred_dir);
        i = 1;
        if (n < 4) {
            rl = &rl_table[s->rl_table_index];
        } else {
            rl = &rl_table[3 + s->rl_chroma_table_index];
        }
        run_diff = 0;
        set_stat(ST_INTRA_AC);
    } else {
        i = 0;
        rl = &rl_table[3 + s->rl_table_index];
        run_diff = 1;
        set_stat(ST_INTER_AC);
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
                    put_bits(&s->pb, 1, 0);
                    if (level > MAX_LEVEL)
                        goto esc3;
                    run1 = run - rl->max_run[last][level] - run_diff;
                    if (run1 < 0)
                        goto esc3;
                    code = get_rl_index(rl, last, run1, level);
                    if (code == rl->n) {
                    esc3:
                        /* third escape */
                        put_bits(&s->pb, 1, 0);
                        put_bits(&s->pb, 1, last);
                        put_bits(&s->pb, 6, run);
                        put_bits(&s->pb, 8, slevel & 0xff);
                    } else {
                        /* second escape */
                        put_bits(&s->pb, 1, 1);
                        put_bits(&s->pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
                        put_bits(&s->pb, 1, sign);
                    }
                } else {
                    /* first escape */
                    put_bits(&s->pb, 1, 1);
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

/****************************************/
/* decoding stuff */

static VLC mb_non_intra_vlc;
static VLC mb_intra_vlc;
static VLC dc_lum_vlc[2];
static VLC dc_chroma_vlc[2];

/* init all vlc decoding tables */
int msmpeg4_decode_init_vlc(MpegEncContext *s)
{
    int i;
    MVTable *mv;

    for(i=0;i<NB_RL_TABLES;i++) {
        init_rl(&rl_table[i]);
        init_vlc_rl(&rl_table[i]);
    }
    for(i=0;i<2;i++) {
        mv = &mv_tables[i];
        init_vlc(&mv->vlc, 9, mv->n + 1, 
                 mv->table_mv_bits, 1, 1,
                 mv->table_mv_code, 2, 2);
    }

    init_vlc(&dc_lum_vlc[0], 9, 120, 
             &table0_dc_lum[0][1], 8, 4,
             &table0_dc_lum[0][0], 8, 4);
    init_vlc(&dc_chroma_vlc[0], 9, 120, 
             &table0_dc_chroma[0][1], 8, 4,
             &table0_dc_chroma[0][0], 8, 4);
    init_vlc(&dc_lum_vlc[1], 9, 120, 
             &table1_dc_lum[0][1], 8, 4,
             &table1_dc_lum[0][0], 8, 4);
    init_vlc(&dc_chroma_vlc[1], 9, 120, 
             &table1_dc_chroma[0][1], 8, 4,
             &table1_dc_chroma[0][0], 8, 4);

    init_vlc(&mb_non_intra_vlc, 9, 128, 
             &table_mb_non_intra[0][1], 8, 4,
             &table_mb_non_intra[0][0], 8, 4);
    init_vlc(&mb_intra_vlc, 9, 64, 
             &table_mb_intra[0][1], 4, 2,
             &table_mb_intra[0][0], 4, 2);
    return 0;
}

static int decode012(GetBitContext *gb)
{
    int n;
    n = get_bits1(gb);
    if (n == 0)
        return 0;
    else
        return get_bits1(gb) + 1;
}

int msmpeg4_decode_picture_header(MpegEncContext * s)
{
    int code;

    s->pict_type = get_bits(&s->gb, 2) + 1;
    if (s->pict_type != I_TYPE &&
        s->pict_type != P_TYPE)
        return -1;

    s->qscale = get_bits(&s->gb, 5);

    if (s->pict_type == I_TYPE) {
        code = get_bits(&s->gb, 5); 
        /* 0x17: one slice, 0x18: three slices */
        /* XXX: implement it */
        if (code < 0x17)
            return -1;
        s->slice_height = s->mb_height / (code - 0x16);
        s->rl_chroma_table_index = decode012(&s->gb);
        s->rl_table_index = decode012(&s->gb);

        s->dc_table_index = get_bits1(&s->gb);
        s->no_rounding = 1;
    } else {
        s->use_skip_mb_code = get_bits1(&s->gb);
        
        s->rl_table_index = decode012(&s->gb);
        s->rl_chroma_table_index = s->rl_table_index;

        s->dc_table_index = get_bits1(&s->gb);

        s->mv_table_index = get_bits1(&s->gb);
        s->no_rounding ^= 1;
    }
#ifdef DEBUG
    printf("*****frame %d:\n", frame_count++);
#endif
    return 0;
}

void memsetw(short *tab, int val, int n)
{
    int i;
    for(i=0;i<n;i++)
        tab[i] = val;
}

int msmpeg4_decode_mb(MpegEncContext *s, 
                      DCTELEM block[6][64])
{
    int cbp, code, i;
    int pred, val;
    UINT8 *coded_val;

    /* special slice handling */
    if (s->mb_x == 0) {
        if (s->slice_height && (s->mb_y % s->slice_height) == 0) {
            int wrap;
            /* reset DC pred (set previous line to 1024) */
            wrap = 2 * s->mb_width + 2;
            memsetw(&s->dc_val[0][(1) + (2 * s->mb_y) * wrap], 
                    1024, 2 * s->mb_width);
            wrap = s->mb_width + 2;
            memsetw(&s->dc_val[1][(1) + (s->mb_y) * wrap], 
                    1024, s->mb_width);
            memsetw(&s->dc_val[2][(1) + (s->mb_y) * wrap], 
                    1024, s->mb_width);

            s->first_slice_line = 1;
        } else {
            s->first_slice_line = 0; 
        }
    }

    if (s->pict_type == P_TYPE) {
        set_stat(ST_INTER_MB);
        if (s->use_skip_mb_code) {
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
        }
        
        code = get_vlc(&s->gb, &mb_non_intra_vlc);
        if (code < 0)
            return -1;
        if (code & 0x40)
            s->mb_intra = 0;
        else
            s->mb_intra = 1;
            
        cbp = code & 0x3f;
    } else {
        set_stat(ST_INTRA_MB);
        s->mb_intra = 1;
        code = get_vlc(&s->gb, &mb_intra_vlc);
        if (code < 0)
            return -1;
        /* predict coded block pattern */
        cbp = 0;
        for(i=0;i<6;i++) {
            val = ((code >> (5 - i)) & 1);
            if (i < 4) {
                pred = coded_block_pred(s, i, &coded_val);
                val = val ^ pred;
                *coded_val = val;
            }
            cbp |= val << (5 - i);
        }
    }

    if (!s->mb_intra) {
        int mx, my;
        set_stat(ST_MV);
        h263_pred_motion(s, 0, &mx, &my);
        if (msmpeg4_decode_motion(s, &mx, &my) < 0)
            return -1;
        s->mv_dir = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        s->mv[0][0][0] = mx;
        s->mv[0][0][1] = my;
    } else {
        set_stat(ST_INTRA_MB);
        s->ac_pred = get_bits1(&s->gb);
    }

    for (i = 0; i < 6; i++) {
        if (msmpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1) < 0)
            return -1;
    }
    return 0;
}

static int msmpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                              int n, int coded)
{
    int code, level, i, j, last, run, run_diff;
    int dc_pred_dir;
    RLTable *rl;
    const UINT8 *scan_table;

    if (s->mb_intra) {
	/* DC coef */
        set_stat(ST_DC);
        level = msmpeg4_decode_dc(s, n, &dc_pred_dir);
        if (level < 0)
            return -1;
        block[0] = level;
        if (n < 4) {
            rl = &rl_table[s->rl_table_index];
        } else {
            rl = &rl_table[3 + s->rl_chroma_table_index];
        }
        run_diff = 0;
	i = 1;
        if (!coded) {
            goto not_coded;
        }
        if (s->ac_pred) {
            if (dc_pred_dir == 0) 
                scan_table = ff_alternate_vertical_scan; /* left */
            else
                scan_table = ff_alternate_horizontal_scan; /* top */
        } else {
            scan_table = zigzag_direct;
        }
        set_stat(ST_INTRA_AC);
    } else {
	i = 0;
        rl = &rl_table[3 + s->rl_table_index];
        run_diff = 1;
        if (!coded) {
            s->block_last_index[n] = i - 1;
            return 0;
        }
        scan_table = zigzag_direct;
        set_stat(ST_INTER_AC);
    }

    for(;;) {
        code = get_vlc(&s->gb, &rl->vlc);
        if (code < 0)
            return -1;
        if (code == rl->n) {
            /* escape */
            if (get_bits1(&s->gb) == 0) {
                if (get_bits1(&s->gb) == 0) {
                    /* third escape */
                    last = get_bits1(&s->gb);
                    run = get_bits(&s->gb, 6);
                    level = get_bits(&s->gb, 8);
                    level = (level << 24) >> 24; /* sign extend */
                } else {
                    /* second escape */
                    code = get_vlc(&s->gb, &rl->vlc);
                    if (code < 0 || code >= rl->n)
                        return -1;
                    run = rl->table_run[code];
                    level = rl->table_level[code];
                    last = code >= rl->last;
                    run += rl->max_run[last][level] + run_diff;
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

static int msmpeg4_decode_dc(MpegEncContext * s, int n, int *dir_ptr)
{
    int level, pred;
    INT16 *dc_val;

    if (n < 4) {
        level = get_vlc(&s->gb, &dc_lum_vlc[s->dc_table_index]);
    } else {
        level = get_vlc(&s->gb, &dc_chroma_vlc[s->dc_table_index]);
    }
    if (level < 0)
        return -1;

    if (level == DC_MAX) {
        level = get_bits(&s->gb, 8);
        if (get_bits1(&s->gb))
            level = -level;
    } else if (level != 0) {
        if (get_bits1(&s->gb))
            level = -level;
    }

    pred = msmpeg4_pred_dc(s, n, &dc_val, dir_ptr);
    level += pred;

    /* update predictor */
    if (n < 4) {
        *dc_val = level * s->y_dc_scale;
    } else {
        *dc_val = level * s->c_dc_scale;
    }

    return level;
}

static int msmpeg4_decode_motion(MpegEncContext * s, 
                                 int *mx_ptr, int *my_ptr)
{
    MVTable *mv;
    int code, mx, my;

    mv = &mv_tables[s->mv_table_index];

    code = get_vlc(&s->gb, &mv->vlc);
    if (code < 0)
        return -1;
    if (code == mv->n) {
        mx = get_bits(&s->gb, 6);
        my = get_bits(&s->gb, 6);
    } else {
        mx = mv->table_mvx[code];
        my = mv->table_mvy[code];
    }

    mx += *mx_ptr - 32;
    my += *my_ptr - 32;
    /* WARNING : they do not do exactly modulo encoding */
    if (mx <= -64)
        mx += 64;
    else if (mx >= 64)
        mx -= 64;

    if (my <= -64)
        my += 64;
    else if (my >= 64)
        my -= 64;
    *mx_ptr = mx;
    *my_ptr = my;
    return 0;
}
