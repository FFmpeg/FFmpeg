/*
 * MSMPEG4 backend for ffmpeg encoder and decoder
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * msmpeg4v1 & v2 stuff by Michael Niedermayer <michaelni@gmx.at>
 */

/**
 * @file msmpeg4.c
 * MSMPEG4 backend for ffmpeg encoder and decoder.
 */

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

/*
 * You can also call this codec : MPEG4 with a twist ! 
 *
 * TODO: 
 *        - (encoding) select best mv table (two choices)
 *        - (encoding) select best vlc/dc table 
 */
//#define DEBUG

#define DC_VLC_BITS 9
#define CBPY_VLC_BITS 6
#define INTER_INTRA_VLC_BITS 3
#define V1_INTRA_CBPC_VLC_BITS 6
#define V1_INTER_CBPC_VLC_BITS 6
#define V2_INTRA_CBPC_VLC_BITS 3
#define V2_MB_TYPE_VLC_BITS 7
#define MV_VLC_BITS 9
#define V2_MV_VLC_BITS 9
#define TEX_VLC_BITS 9
#define MB_NON_INTRA_VLC_BITS 9
#define MB_INTRA_VLC_BITS 9

#define II_BITRATE 128*1024
#define MBAC_BITRATE 50*1024

#define DEFAULT_INTER_INDEX 3

static uint32_t v2_dc_lum_table[512][2];
static uint32_t v2_dc_chroma_table[512][2];

static inline void msmpeg4_encode_block(MpegEncContext * s, DCTELEM * block, int n);
static inline int msmpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                                       int n, int coded, const uint8_t *scantable);
static int msmpeg4_decode_dc(MpegEncContext * s, int n, int *dir_ptr);
static int msmpeg4_decode_motion(MpegEncContext * s, 
                                 int *mx_ptr, int *my_ptr);
static void msmpeg4v2_encode_motion(MpegEncContext * s, int val);
static void init_h263_dc_for_msmpeg4(void);
static inline void msmpeg4_memsetw(short *tab, int val, int n);
#ifdef CONFIG_ENCODERS
static int get_size_of_code(MpegEncContext * s, RLTable *rl, int last, int run, int level, int intra);
#endif //CONFIG_ENCODERS
static int msmpeg4v12_decode_mb(MpegEncContext *s, DCTELEM block[6][64]);
static int msmpeg4v34_decode_mb(MpegEncContext *s, DCTELEM block[6][64]);
static int wmv2_decode_mb(MpegEncContext *s, DCTELEM block[6][64]);


#ifdef DEBUG
int intra_count = 0;
int frame_count = 0;
#endif

#include "msmpeg4data.h"

#ifdef CONFIG_ENCODERS //strangely gcc includes this even if its not references
static uint8_t rl_length[NB_RL_TABLES][MAX_LEVEL+1][MAX_RUN+1][2];
#endif //CONFIG_ENCODERS

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

static void common_init(MpegEncContext * s)
{
    static int inited=0;
    
    switch(s->msmpeg4_version){
    case 1:
    case 2:
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
        break;
    case 3:
        if(s->workaround_bugs){
            s->y_dc_scale_table= old_ff_y_dc_scale_table;
            s->c_dc_scale_table= old_ff_c_dc_scale_table;
        } else{
            s->y_dc_scale_table= ff_mpeg4_y_dc_scale_table;
            s->c_dc_scale_table= ff_mpeg4_c_dc_scale_table;
        }
        break;
    case 4:
    case 5:
        s->y_dc_scale_table= wmv1_y_dc_scale_table;
        s->c_dc_scale_table= wmv1_c_dc_scale_table;
        break;
    }

    
    if(s->msmpeg4_version>=4){
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable  , wmv1_scantable[1]);
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_h_scantable, wmv1_scantable[2]);
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_v_scantable, wmv1_scantable[3]);
        ff_init_scantable(s->dsp.idct_permutation, &s->inter_scantable  , wmv1_scantable[0]);
    }
    //Note the default tables are set in common_init in mpegvideo.c
    
    if(!inited){
        inited=1;

        init_h263_dc_for_msmpeg4();
    }
}

#ifdef CONFIG_ENCODERS

/* build the table which associate a (x,y) motion vector to a vlc */
static void init_mv_table(MVTable *tab)
{
    int i, x, y;

    tab->table_mv_index = av_malloc(sizeof(uint16_t) * 4096);
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

void ff_msmpeg4_encode_init(MpegEncContext *s)
{
    static int init_done=0;
    int i;

    common_init(s);
    if(s->msmpeg4_version>=4){
        s->min_qcoeff= -255;
        s->max_qcoeff=  255;
    }

    if (!init_done) {
        /* init various encoding tables */
        init_done = 1;
        init_mv_table(&mv_tables[0]);
        init_mv_table(&mv_tables[1]);
        for(i=0;i<NB_RL_TABLES;i++)
            init_rl(&rl_table[i]);

        for(i=0; i<NB_RL_TABLES; i++){
            int level;
            for(level=0; level<=MAX_LEVEL; level++){
                int run;
                for(run=0; run<=MAX_RUN; run++){
                    int last;
                    for(last=0; last<2; last++){
                        rl_length[i][level][run][last]= get_size_of_code(s, &rl_table[  i], last, run, level, 0);
                    }
                }
            }
        }
    }
}

static int get_size_of_code(MpegEncContext * s, RLTable *rl, int last, int run, int level, int intra){
    int size=0;
    int code;
    int run_diff= intra ? 0 : 1;
    
    code = get_rl_index(rl, last, run, level);
    size+= rl->table_vlc[code][1];
    if (code == rl->n) {
        int level1, run1;

        level1 = level - rl->max_level[last][run];
        if (level1 < 1) 
            goto esc2;
        code = get_rl_index(rl, last, run, level1);
        if (code == rl->n) {
            esc2:
            size++;
            if (level > MAX_LEVEL)
                goto esc3;
            run1 = run - rl->max_run[last][level] - run_diff;
            if (run1 < 0)
                goto esc3;
            code = get_rl_index(rl, last, run1, level);
            if (code == rl->n) {
            esc3:
                /* third escape */
                size+=1+1+6+8;
            } else {
                /* second escape */
                size+= 1+1+ rl->table_vlc[code][1];
            }
        } else {
            /* first escape */
            size+= 1+1+ rl->table_vlc[code][1];
        }
    } else {
        size++;
    }
    return size;
}

static void find_best_tables(MpegEncContext * s)
{
    int i;
    int best       =-1, best_size       =9999999;
    int chroma_best=-1, best_chroma_size=9999999;

    for(i=0; i<3; i++){
        int level;
        int chroma_size=0;
        int size=0;

        if(i>0){// ;)
            size++; 
            chroma_size++;
        }
        for(level=0; level<=MAX_LEVEL; level++){
            int run;
            for(run=0; run<=MAX_RUN; run++){
                int last;
                const int last_size= size + chroma_size;
                for(last=0; last<2; last++){
                    int inter_count       = s->ac_stats[0][0][level][run][last] + s->ac_stats[0][1][level][run][last];
                    int intra_luma_count  = s->ac_stats[1][0][level][run][last];
                    int intra_chroma_count= s->ac_stats[1][1][level][run][last];
                    
                    if(s->pict_type==I_TYPE){
                        size       += intra_luma_count  *rl_length[i  ][level][run][last];
                        chroma_size+= intra_chroma_count*rl_length[i+3][level][run][last];
                    }else{
                        size+=        intra_luma_count  *rl_length[i  ][level][run][last]
                                     +intra_chroma_count*rl_length[i+3][level][run][last]
                                     +inter_count       *rl_length[i+3][level][run][last];
                    }                   
                }
                if(last_size == size+chroma_size) break;
            }
        }
        if(size<best_size){
            best_size= size;
            best= i;
        }
        if(chroma_size<best_chroma_size){
            best_chroma_size= chroma_size;
            chroma_best= i;
        }
    }

//    printf("type:%d, best:%d, qp:%d, var:%d, mcvar:%d, size:%d //\n", 
//           s->pict_type, best, s->qscale, s->mb_var_sum, s->mc_mb_var_sum, best_size);
           
    if(s->pict_type==P_TYPE) chroma_best= best;

    memset(s->ac_stats, 0, sizeof(int)*(MAX_LEVEL+1)*(MAX_RUN+1)*2*2*2);

    s->rl_table_index       =        best;
    s->rl_chroma_table_index= chroma_best;
    
    if(s->pict_type != s->last_non_b_pict_type){
        s->rl_table_index= 2;
        if(s->pict_type==I_TYPE)
            s->rl_chroma_table_index= 1;
        else
            s->rl_chroma_table_index= 2;
    }

}

/* write MSMPEG4 compatible frame header */
void msmpeg4_encode_picture_header(MpegEncContext * s, int picture_number)
{
    find_best_tables(s);

    align_put_bits(&s->pb);
    put_bits(&s->pb, 2, s->pict_type - 1);

    put_bits(&s->pb, 5, s->qscale);
    if(s->msmpeg4_version<=2){
        s->rl_table_index = 2;
        s->rl_chroma_table_index = 2;
    }

    s->dc_table_index = 1;
    s->mv_table_index = 1; /* only if P frame */
    s->use_skip_mb_code = 1; /* only if P frame */
    s->per_mb_rl_table = 0;
    if(s->msmpeg4_version==4)
        s->inter_intra_pred= (s->width*s->height < 320*240 && s->bit_rate<=II_BITRATE && s->pict_type==P_TYPE);
//printf("%d %d %d %d %d\n", s->pict_type, s->bit_rate, s->inter_intra_pred, s->width, s->height);

    if (s->pict_type == I_TYPE) {
        s->slice_height= s->mb_height/1;
        put_bits(&s->pb, 5, 0x16 + s->mb_height/s->slice_height);
        
        if(s->msmpeg4_version==4){
            msmpeg4_encode_ext_header(s);
            if(s->bit_rate>MBAC_BITRATE)
                put_bits(&s->pb, 1, s->per_mb_rl_table);
        }

        if(s->msmpeg4_version>2){
            if(!s->per_mb_rl_table){
                code012(&s->pb, s->rl_chroma_table_index);
                code012(&s->pb, s->rl_table_index);
            }

            put_bits(&s->pb, 1, s->dc_table_index);
        }
    } else {
        put_bits(&s->pb, 1, s->use_skip_mb_code);
        
        if(s->msmpeg4_version==4 && s->bit_rate>MBAC_BITRATE)
            put_bits(&s->pb, 1, s->per_mb_rl_table);

        if(s->msmpeg4_version>2){
            if(!s->per_mb_rl_table)
                code012(&s->pb, s->rl_table_index);

            put_bits(&s->pb, 1, s->dc_table_index);

            put_bits(&s->pb, 1, s->mv_table_index);
        }
    }

    s->esc3_level_length= 0;
    s->esc3_run_length= 0;

#ifdef DEBUG
    intra_count = 0;
    printf("*****frame %d:\n", frame_count++);
#endif
}

void msmpeg4_encode_ext_header(MpegEncContext * s)
{
        put_bits(&s->pb, 5, s->avctx->frame_rate / s->avctx->frame_rate_base); //yes 29.97 -> 29

        put_bits(&s->pb, 11, FFMIN(s->bit_rate/1024, 2047));

        if(s->msmpeg4_version>=3)
            put_bits(&s->pb, 1, s->flipflop_rounding);
        else
            assert(s->flipflop_rounding==0);
}

#endif //CONFIG_ENCODERS

/* predict coded block */
static inline int coded_block_pred(MpegEncContext * s, int n, uint8_t **coded_block_ptr)
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

#ifdef CONFIG_ENCODERS

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

static inline void handle_slices(MpegEncContext *s){
    if (s->mb_x == 0) {
        if (s->slice_height && (s->mb_y % s->slice_height) == 0) {
            if(s->msmpeg4_version < 4){
                ff_mpeg4_clean_buffers(s);
            }
            s->first_slice_line = 1;
        } else {
            s->first_slice_line = 0; 
        }
    }
}

void msmpeg4_encode_mb(MpegEncContext * s, 
                       DCTELEM block[6][64],
                       int motion_x, int motion_y)
{
    int cbp, coded_cbp, i;
    int pred_x, pred_y;
    uint8_t *coded_block;

    handle_slices(s);
    
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
            s->last_bits++;
	    s->misc_bits++;
            s->skip_count++;

	    return;
	}
        if (s->use_skip_mb_code)
            put_bits(&s->pb, 1, 0);	/* mb coded */
        
        if(s->msmpeg4_version<=2){
            put_bits(&s->pb, 
                     v2_mb_type[cbp&3][1], 
                     v2_mb_type[cbp&3][0]);
            if((cbp&3) != 3) coded_cbp= cbp ^ 0x3C;
            else             coded_cbp= cbp;

            put_bits(&s->pb, 
                     cbpy_tab[coded_cbp>>2][1], 
                     cbpy_tab[coded_cbp>>2][0]);

            s->misc_bits += get_bits_diff(s);

            h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
            msmpeg4v2_encode_motion(s, motion_x - pred_x);
            msmpeg4v2_encode_motion(s, motion_y - pred_y);
        }else{
            put_bits(&s->pb, 
                     table_mb_non_intra[cbp + 64][1], 
                     table_mb_non_intra[cbp + 64][0]);

            s->misc_bits += get_bits_diff(s);

            /* motion vector */
            h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
            msmpeg4_encode_motion(s, motion_x - pred_x, 
                                  motion_y - pred_y);
        }

        s->mv_bits += get_bits_diff(s);

        for (i = 0; i < 6; i++) {
            msmpeg4_encode_block(s, block[i], i);
        }
        s->p_tex_bits += get_bits_diff(s);
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

        if(s->msmpeg4_version<=2){
            if (s->pict_type == I_TYPE) {
                put_bits(&s->pb, 
                         v2_intra_cbpc[cbp&3][1], v2_intra_cbpc[cbp&3][0]);
            } else {
                if (s->use_skip_mb_code)
                    put_bits(&s->pb, 1, 0);	/* mb coded */
                put_bits(&s->pb, 
                         v2_mb_type[(cbp&3) + 4][1], 
                         v2_mb_type[(cbp&3) + 4][0]);
            }
            put_bits(&s->pb, 1, 0);	/* no AC prediction yet */
            put_bits(&s->pb, 
                     cbpy_tab[cbp>>2][1], 
                     cbpy_tab[cbp>>2][0]);
        }else{
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
            if(s->inter_intra_pred){
                s->h263_aic_dir=0;
                put_bits(&s->pb, table_inter_intra[s->h263_aic_dir][1], table_inter_intra[s->h263_aic_dir][0]);
            }
        }
        s->misc_bits += get_bits_diff(s);

        for (i = 0; i < 6; i++) {
            msmpeg4_encode_block(s, block[i], i);
        }
        s->i_tex_bits += get_bits_diff(s);
        s->i_count++;
    }
}

#endif //CONFIG_ENCODERS

static inline int msmpeg4v1_pred_dc(MpegEncContext * s, int n, 
                                    int32_t **dc_val_ptr)
{
    int i;

    if (n < 4) {
        i= 0;
    } else {
        i= n-3;
    }
    
    *dc_val_ptr= &s->last_dc[i];
    return s->last_dc[i]; 
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
static inline int msmpeg4_pred_dc(MpegEncContext * s, int n, 
                             uint16_t **dc_val_ptr, int *dir_ptr)
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
#if defined ARCH_X86 && !defined PIC
    asm volatile(
        "movl %3, %%eax		\n\t"
	"shrl $1, %%eax		\n\t"
	"addl %%eax, %2		\n\t"
	"addl %%eax, %1		\n\t"
	"addl %0, %%eax		\n\t"
	"mull %4		\n\t"
	"movl %%edx, %0		\n\t"
	"movl %1, %%eax		\n\t"
	"mull %4		\n\t"
	"movl %%edx, %1		\n\t"
	"movl %2, %%eax		\n\t"
	"mull %4		\n\t"
	"movl %%edx, %2		\n\t"
	: "+b" (a), "+c" (b), "+D" (c)
	: "g" (scale), "S" (inverse[scale])
	: "%eax", "%edx"
    );
#else
    /* #elif defined (ARCH_ALPHA) */
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
                    dest= s->current_picture.data[0] + (((n>>1) + 2*s->mb_y) * 8*  wrap ) + ((n&1) + 2*s->mb_x) * 8;
                }else{
                    wrap= s->uvlinesize;
                    dest= s->current_picture.data[n-3] + (s->mb_y * 8 * wrap) + s->mb_x * 8;
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

#define DC_MAX 119

static void msmpeg4_encode_dc(MpegEncContext * s, int level, int n, int *dir_ptr)
{
    int sign, code;
    int pred;

    if(s->msmpeg4_version==1){
        int32_t *dc_val;
        pred = msmpeg4v1_pred_dc(s, n, &dc_val);
        
        /* update predictor */
        *dc_val= level;
    }else{
        uint16_t *dc_val;
        pred = msmpeg4_pred_dc(s, n, &dc_val, dir_ptr);

        /* update predictor */
        if (n < 4) {
            *dc_val = level * s->y_dc_scale;
        } else {
            *dc_val = level * s->c_dc_scale;
        }
    }

    /* do the prediction */
    level -= pred;

    if(s->msmpeg4_version<=2){
        if (n < 4) {
            put_bits(&s->pb, 
                     v2_dc_lum_table[level+256][1],
                     v2_dc_lum_table[level+256][0]);
        }else{
            put_bits(&s->pb, 
                     v2_dc_chroma_table[level+256][1],
                     v2_dc_chroma_table[level+256][0]);
        }
    }else{
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
}

/* Encoding of a block. Very similar to MPEG4 except for a different
   escape coding (same as H263) and more vlc tables.
 */
static inline void msmpeg4_encode_block(MpegEncContext * s, DCTELEM * block, int n)
{
    int level, run, last, i, j, last_index;
    int last_non_zero, sign, slevel;
    int code, run_diff, dc_pred_dir;
    const RLTable *rl;
    const uint8_t *scantable;

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
        scantable= s->intra_scantable.permutated;
        set_stat(ST_INTRA_AC);
    } else {
        i = 0;
        rl = &rl_table[3 + s->rl_table_index];
        if(s->msmpeg4_version<=2)
            run_diff = 0;
        else
            run_diff = 1;
        scantable= s->inter_scantable.permutated;
        set_stat(ST_INTER_AC);
    }

    /* recalculate block_last_index for M$ wmv1 */
    if(s->msmpeg4_version>=4 && s->block_last_index[n]>0){
        for(last_index=63; last_index>=0; last_index--){
            if(block[scantable[last_index]]) break;
        }
        s->block_last_index[n]= last_index;
    }else
        last_index = s->block_last_index[n];
    /* AC coefs */
    last_non_zero = i - 1;
    for (; i <= last_index; i++) {
	j = scantable[i];
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

            if(level<=MAX_LEVEL && run<=MAX_RUN){
                s->ac_stats[s->mb_intra][n>3][level][run][last]++;
            }
#if 0
else
    s->ac_stats[s->mb_intra][n>3][40][63][0]++; //esc3 like
#endif
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
                        if(s->msmpeg4_version>=4){
                            if(s->esc3_level_length==0){
                                s->esc3_level_length=8;
                                s->esc3_run_length= 6;
                                if(s->qscale<8)
                                    put_bits(&s->pb, 6, 3);
                                else
                                    put_bits(&s->pb, 8, 3);
                            }
                            put_bits(&s->pb, s->esc3_run_length, run);
                            put_bits(&s->pb, 1, sign);
                            put_bits(&s->pb, s->esc3_level_length, level);
                        }else{
                            put_bits(&s->pb, 6, run);
                            put_bits(&s->pb, 8, slevel & 0xff);
                        }
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

static VLC mb_non_intra_vlc[4];
static VLC mb_intra_vlc;
static VLC dc_lum_vlc[2];
static VLC dc_chroma_vlc[2];
static VLC v2_dc_lum_vlc;
static VLC v2_dc_chroma_vlc;
static VLC cbpy_vlc;
static VLC v2_intra_cbpc_vlc;
static VLC v2_mb_type_vlc;
static VLC v2_mv_vlc;
static VLC v1_intra_cbpc_vlc;
static VLC v1_inter_cbpc_vlc;
static VLC inter_intra_vlc;

/* this table is practically identical to the one from h263 except that its inverted */
static void init_h263_dc_for_msmpeg4(void)
{
        int level, uni_code, uni_len;

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
            uni_code= DCtab_lum[size][0];
            uni_len = DCtab_lum[size][1];
            uni_code ^= (1<<uni_len)-1; //M$ doesnt like compatibility

            if (size > 0) {
                uni_code<<=size; uni_code|=l;
                uni_len+=size;
                if (size > 8){
                    uni_code<<=1; uni_code|=1;
                    uni_len++;
                }
            }
            v2_dc_lum_table[level+256][0]= uni_code;
            v2_dc_lum_table[level+256][1]= uni_len;

            /* chrominance h263 */
            uni_code= DCtab_chrom[size][0];
            uni_len = DCtab_chrom[size][1];
            uni_code ^= (1<<uni_len)-1; //M$ doesnt like compatibility
            
            if (size > 0) {
                uni_code<<=size; uni_code|=l;
                uni_len+=size;
                if (size > 8){
                    uni_code<<=1; uni_code|=1;
                    uni_len++;
                }
            }
            v2_dc_chroma_table[level+256][0]= uni_code;
            v2_dc_chroma_table[level+256][1]= uni_len;

        }
}

/* init all vlc decoding tables */
int ff_msmpeg4_decode_init(MpegEncContext *s)
{
    static int done = 0;
    int i;
    MVTable *mv;

    common_init(s);

    if (!done) {
        done = 1;

        for(i=0;i<NB_RL_TABLES;i++) {
            init_rl(&rl_table[i]);
            init_vlc_rl(&rl_table[i]);
        }
        for(i=0;i<2;i++) {
            mv = &mv_tables[i];
            init_vlc(&mv->vlc, MV_VLC_BITS, mv->n + 1, 
                     mv->table_mv_bits, 1, 1,
                     mv->table_mv_code, 2, 2);
        }

        init_vlc(&dc_lum_vlc[0], DC_VLC_BITS, 120, 
                 &table0_dc_lum[0][1], 8, 4,
                 &table0_dc_lum[0][0], 8, 4);
        init_vlc(&dc_chroma_vlc[0], DC_VLC_BITS, 120, 
                 &table0_dc_chroma[0][1], 8, 4,
                 &table0_dc_chroma[0][0], 8, 4);
        init_vlc(&dc_lum_vlc[1], DC_VLC_BITS, 120, 
                 &table1_dc_lum[0][1], 8, 4,
                 &table1_dc_lum[0][0], 8, 4);
        init_vlc(&dc_chroma_vlc[1], DC_VLC_BITS, 120, 
                 &table1_dc_chroma[0][1], 8, 4,
                 &table1_dc_chroma[0][0], 8, 4);
    
        init_vlc(&v2_dc_lum_vlc, DC_VLC_BITS, 512, 
                 &v2_dc_lum_table[0][1], 8, 4,
                 &v2_dc_lum_table[0][0], 8, 4);
        init_vlc(&v2_dc_chroma_vlc, DC_VLC_BITS, 512, 
                 &v2_dc_chroma_table[0][1], 8, 4,
                 &v2_dc_chroma_table[0][0], 8, 4);
    
        init_vlc(&cbpy_vlc, CBPY_VLC_BITS, 16,
                 &cbpy_tab[0][1], 2, 1,
                 &cbpy_tab[0][0], 2, 1);
        init_vlc(&v2_intra_cbpc_vlc, V2_INTRA_CBPC_VLC_BITS, 4,
                 &v2_intra_cbpc[0][1], 2, 1,
                 &v2_intra_cbpc[0][0], 2, 1);
        init_vlc(&v2_mb_type_vlc, V2_MB_TYPE_VLC_BITS, 8,
                 &v2_mb_type[0][1], 2, 1,
                 &v2_mb_type[0][0], 2, 1);
        init_vlc(&v2_mv_vlc, V2_MV_VLC_BITS, 33,
                 &mvtab[0][1], 2, 1,
                 &mvtab[0][0], 2, 1);

        for(i=0; i<4; i++){
            init_vlc(&mb_non_intra_vlc[i], MB_NON_INTRA_VLC_BITS, 128, 
                     &wmv2_inter_table[i][0][1], 8, 4,
                     &wmv2_inter_table[i][0][0], 8, 4); //FIXME name?
        }
        
        init_vlc(&mb_intra_vlc, MB_INTRA_VLC_BITS, 64, 
                 &table_mb_intra[0][1], 4, 2,
                 &table_mb_intra[0][0], 4, 2);
        
        init_vlc(&v1_intra_cbpc_vlc, V1_INTRA_CBPC_VLC_BITS, 8, 
                 intra_MCBPC_bits, 1, 1,
                 intra_MCBPC_code, 1, 1);
        init_vlc(&v1_inter_cbpc_vlc, V1_INTER_CBPC_VLC_BITS, 25, 
                 inter_MCBPC_bits, 1, 1,
                 inter_MCBPC_code, 1, 1);
        
        init_vlc(&inter_intra_vlc, INTER_INTRA_VLC_BITS, 4, 
                 &table_inter_intra[0][1], 2, 1,
                 &table_inter_intra[0][0], 2, 1);
    }
    
    switch(s->msmpeg4_version){
    case 1:
    case 2:
        s->decode_mb= msmpeg4v12_decode_mb;
        break;
    case 3:
    case 4:
        s->decode_mb= msmpeg4v34_decode_mb;
        break;
    case 5:
        s->decode_mb= wmv2_decode_mb;
        break;
    }
    
    s->slice_height= s->mb_height; //to avoid 1/0 if the first frame isnt a keyframe
    
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

#if 0
{
int i;
for(i=0; i<s->gb.size_in_bits; i++)
    printf("%d", get_bits1(&s->gb));
//    get_bits1(&s->gb);
printf("END\n");
return -1;
}
#endif

    if(s->msmpeg4_version==1){
        int start_code, num;
        start_code = (get_bits(&s->gb, 16)<<16) | get_bits(&s->gb, 16);
        if(start_code!=0x00000100){
            av_log(s->avctx, AV_LOG_ERROR, "invalid startcode\n");
            return -1;
        }

        num= get_bits(&s->gb, 5); // frame number */
    }

    s->pict_type = get_bits(&s->gb, 2) + 1;
    if (s->pict_type != I_TYPE &&
        s->pict_type != P_TYPE){
        av_log(s->avctx, AV_LOG_ERROR, "invalid picture type\n");
        return -1;
    }
#if 0
{
    static int had_i=0;
    if(s->pict_type == I_TYPE) had_i=1;
    if(!had_i) return -1;
}
#endif
    s->chroma_qscale= s->qscale = get_bits(&s->gb, 5);
    if(s->qscale==0){
        av_log(s->avctx, AV_LOG_ERROR, "invalid qscale\n");
        return -1;
    }

    if (s->pict_type == I_TYPE) {
        code = get_bits(&s->gb, 5); 
        if(s->msmpeg4_version==1){
            if(code==0 || code>s->mb_height){
                av_log(s->avctx, AV_LOG_ERROR, "invalid slice height %d\n", code);
                return -1;
            }

            s->slice_height = code;
        }else{
            /* 0x17: one slice, 0x18: two slices, ... */
            if (code < 0x17){
                av_log(s->avctx, AV_LOG_ERROR, "error, slice code was %X\n", code);
                return -1;
            }

            s->slice_height = s->mb_height / (code - 0x16);
        }

        switch(s->msmpeg4_version){
        case 1:
        case 2:
            s->rl_chroma_table_index = 2;
            s->rl_table_index = 2;

            s->dc_table_index = 0; //not used
            break;
        case 3:
            s->rl_chroma_table_index = decode012(&s->gb);
            s->rl_table_index = decode012(&s->gb);

            s->dc_table_index = get_bits1(&s->gb);
            break;
        case 4:
            msmpeg4_decode_ext_header(s, (2+5+5+17+7)/8);

            if(s->bit_rate > MBAC_BITRATE) s->per_mb_rl_table= get_bits1(&s->gb);
            else                           s->per_mb_rl_table= 0;
            
            if(!s->per_mb_rl_table){
                s->rl_chroma_table_index = decode012(&s->gb);
                s->rl_table_index = decode012(&s->gb);
            }

            s->dc_table_index = get_bits1(&s->gb);
            s->inter_intra_pred= 0;
            break;
        }
        s->no_rounding = 1;
        if(s->avctx->debug&FF_DEBUG_PICT_INFO)
	    av_log(s->avctx, AV_LOG_DEBUG, "qscale:%d rlc:%d rl:%d dc:%d mbrl:%d slice:%d   \n", 
		s->qscale,
		s->rl_chroma_table_index,
		s->rl_table_index, 
		s->dc_table_index,
                s->per_mb_rl_table,
                s->slice_height);
    } else {
        switch(s->msmpeg4_version){
        case 1:
        case 2:
            if(s->msmpeg4_version==1)
                s->use_skip_mb_code = 1;
            else
                s->use_skip_mb_code = get_bits1(&s->gb);
            s->rl_table_index = 2;
            s->rl_chroma_table_index = s->rl_table_index;
            s->dc_table_index = 0; //not used
            s->mv_table_index = 0;
            break;
        case 3:
            s->use_skip_mb_code = get_bits1(&s->gb);
            s->rl_table_index = decode012(&s->gb);
            s->rl_chroma_table_index = s->rl_table_index;

            s->dc_table_index = get_bits1(&s->gb);

            s->mv_table_index = get_bits1(&s->gb);
            break;
        case 4:
            s->use_skip_mb_code = get_bits1(&s->gb);

            if(s->bit_rate > MBAC_BITRATE) s->per_mb_rl_table= get_bits1(&s->gb);
            else                           s->per_mb_rl_table= 0;

            if(!s->per_mb_rl_table){
                s->rl_table_index = decode012(&s->gb);
                s->rl_chroma_table_index = s->rl_table_index;
            }

            s->dc_table_index = get_bits1(&s->gb);

            s->mv_table_index = get_bits1(&s->gb);
            s->inter_intra_pred= (s->width*s->height < 320*240 && s->bit_rate<=II_BITRATE);
            break;
        }
        
        if(s->avctx->debug&FF_DEBUG_PICT_INFO)
	    av_log(s->avctx, AV_LOG_DEBUG, "skip:%d rl:%d rlc:%d dc:%d mv:%d mbrl:%d qp:%d   \n", 
		s->use_skip_mb_code, 
		s->rl_table_index, 
		s->rl_chroma_table_index, 
		s->dc_table_index,
		s->mv_table_index,
                s->per_mb_rl_table,
                s->qscale);

	if(s->flipflop_rounding){
	    s->no_rounding ^= 1;
	}else{
	    s->no_rounding = 0;
	}
    }
//printf("%d %d %d %d %d\n", s->pict_type, s->bit_rate, s->inter_intra_pred, s->width, s->height);

    s->esc3_level_length= 0;
    s->esc3_run_length= 0;

#ifdef DEBUG
    printf("*****frame %d:\n", frame_count++);
#endif
    return 0;
}

int msmpeg4_decode_ext_header(MpegEncContext * s, int buf_size)
{
    int left= buf_size*8 - get_bits_count(&s->gb);
    int length= s->msmpeg4_version>=3 ? 17 : 16;
    /* the alt_bitstream reader could read over the end so we need to check it */
    if(left>=length && left<length+8)
    {
        int fps;

        fps= get_bits(&s->gb, 5);
        s->bit_rate= get_bits(&s->gb, 11)*1024;
        if(s->msmpeg4_version>=3)
            s->flipflop_rounding= get_bits1(&s->gb);
        else
            s->flipflop_rounding= 0;

//        printf("fps:%2d bps:%2d roundingType:%1d\n", fps, s->bit_rate/1024, s->flipflop_rounding);
    }
    else if(left<length+8)
    {
        s->flipflop_rounding= 0;
        if(s->msmpeg4_version != 2)
            av_log(s->avctx, AV_LOG_ERROR, "ext header missing, %d left\n", left);
    }
    else
    {
        av_log(s->avctx, AV_LOG_ERROR, "I frame too long, ignoring ext header\n");
    }

    return 0;
}

static inline void msmpeg4_memsetw(short *tab, int val, int n)
{
    int i;
    for(i=0;i<n;i++)
        tab[i] = val;
}

static void msmpeg4v2_encode_motion(MpegEncContext * s, int val)
{
    int range, bit_size, sign, code, bits;

    if (val == 0) {
        /* zero vector */
        code = 0;
        put_bits(&s->pb, mvtab[code][1], mvtab[code][0]);
    } else {
        bit_size = s->f_code - 1;
        range = 1 << bit_size;
        if (val <= -64)
            val += 64;
        else if (val >= 64)
            val -= 64;

        if (val >= 0) {
            sign = 0;
        } else {
            val = -val;
            sign = 1;
        }
        val--;
        code = (val >> bit_size) + 1;
        bits = val & (range - 1);

        put_bits(&s->pb, mvtab[code][1] + 1, (mvtab[code][0] << 1) | sign); 
        if (bit_size > 0) {
            put_bits(&s->pb, bit_size, bits);
        }
    }
}

/* this is identical to h263 except that its range is multiplied by 2 */
static int msmpeg4v2_decode_motion(MpegEncContext * s, int pred, int f_code)
{
    int code, val, sign, shift;

    code = get_vlc2(&s->gb, v2_mv_vlc.table, V2_MV_VLC_BITS, 2);
//     printf("MV code %d at %d %d pred: %d\n", code, s->mb_x,s->mb_y, pred);
    if (code < 0)
        return 0xffff;

    if (code == 0)
        return pred;
    sign = get_bits1(&s->gb);
    shift = f_code - 1;
    val = code;
    if (shift) {
        val = (val - 1) << shift;
        val |= get_bits(&s->gb, shift);
        val++;
    }
    if (sign)
        val = -val;

    val += pred;
    if (val <= -64)
        val += 64;
    else if (val >= 64)
        val -= 64;

    return val;
}

static int msmpeg4v12_decode_mb(MpegEncContext *s, DCTELEM block[6][64])
{
    int cbp, code, i;
    
    if (s->pict_type == P_TYPE) {
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

        if(s->msmpeg4_version==2)
            code = get_vlc2(&s->gb, v2_mb_type_vlc.table, V2_MB_TYPE_VLC_BITS, 1);
        else
            code = get_vlc2(&s->gb, v1_inter_cbpc_vlc.table, V1_INTER_CBPC_VLC_BITS, 3);
        if(code<0 || code>7){
            av_log(s->avctx, AV_LOG_ERROR, "cbpc %d invalid at %d %d\n", code, s->mb_x, s->mb_y);
            return -1;
        }

        s->mb_intra = code >>2;
    
        cbp = code & 0x3;
    } else {
        s->mb_intra = 1;
        if(s->msmpeg4_version==2)
            cbp= get_vlc2(&s->gb, v2_intra_cbpc_vlc.table, V2_INTRA_CBPC_VLC_BITS, 1);
        else
            cbp= get_vlc2(&s->gb, v1_intra_cbpc_vlc.table, V1_INTRA_CBPC_VLC_BITS, 1);
        if(cbp<0 || cbp>3){
            av_log(s->avctx, AV_LOG_ERROR, "cbpc %d invalid at %d %d\n", cbp, s->mb_x, s->mb_y);
            return -1;
        }
    }

    if (!s->mb_intra) {
        int mx, my, cbpy;
        
        cbpy= get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);
        if(cbpy<0){
            av_log(s->avctx, AV_LOG_ERROR, "cbpy %d invalid at %d %d\n", cbp, s->mb_x, s->mb_y);
            return -1;
        }

        cbp|= cbpy<<2;
        if(s->msmpeg4_version==1 || (cbp&3) != 3) cbp^= 0x3C;
        
        h263_pred_motion(s, 0, 0, &mx, &my);
        mx= msmpeg4v2_decode_motion(s, mx, 1);
        my= msmpeg4v2_decode_motion(s, my, 1);
        
        s->mv_dir = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        s->mv[0][0][0] = mx;
        s->mv[0][0][1] = my;
    } else {
        if(s->msmpeg4_version==2){
            s->ac_pred = get_bits1(&s->gb);
            cbp|= get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1)<<2; //FIXME check errors
        } else{
            s->ac_pred = 0;
            cbp|= get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1)<<2; //FIXME check errors
            if(s->pict_type==P_TYPE) cbp^=0x3C;
        }
    }

    for (i = 0; i < 6; i++) {
        if (msmpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1, NULL) < 0)
	{
             av_log(s->avctx, AV_LOG_ERROR, "\nerror while decoding block: %d x %d (%d)\n", s->mb_x, s->mb_y, i);
             return -1;
	}
    }
    return 0;
}

static int msmpeg4v34_decode_mb(MpegEncContext *s, DCTELEM block[6][64])
{
    int cbp, code, i;
    uint8_t *coded_val;
    uint32_t * const mb_type_ptr= &s->current_picture.mb_type[ s->mb_x + s->mb_y*s->mb_stride ];

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
                *mb_type_ptr = MB_TYPE_SKIP | MB_TYPE_L0 | MB_TYPE_16x16;

                return 0;
            }
        }
        
        code = get_vlc2(&s->gb, mb_non_intra_vlc[DEFAULT_INTER_INDEX].table, MB_NON_INTRA_VLC_BITS, 3);
        if (code < 0)
            return -1;
	//s->mb_intra = (code & 0x40) ? 0 : 1;
	s->mb_intra = (~code & 0x40) >> 6;
            
        cbp = code & 0x3f;
    } else {
        set_stat(ST_INTRA_MB);
        s->mb_intra = 1;
        code = get_vlc2(&s->gb, mb_intra_vlc.table, MB_INTRA_VLC_BITS, 2);
        if (code < 0)
            return -1;
        /* predict coded block pattern */
        cbp = 0;
        for(i=0;i<6;i++) {
            int val = ((code >> (5 - i)) & 1);
            if (i < 4) {
                int pred = coded_block_pred(s, i, &coded_val);
                val = val ^ pred;
                *coded_val = val;
            }
            cbp |= val << (5 - i);
        }
    }

    if (!s->mb_intra) {
        int mx, my;
//printf("P at %d %d\n", s->mb_x, s->mb_y);
        if(s->per_mb_rl_table && cbp){
            s->rl_table_index = decode012(&s->gb);
            s->rl_chroma_table_index = s->rl_table_index;
        }
        set_stat(ST_MV);
        h263_pred_motion(s, 0, 0, &mx, &my);
        if (msmpeg4_decode_motion(s, &mx, &my) < 0)
            return -1;
        s->mv_dir = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        s->mv[0][0][0] = mx;
        s->mv[0][0][1] = my;
        *mb_type_ptr = MB_TYPE_L0 | MB_TYPE_16x16;
    } else {
//printf("I at %d %d %d %06X\n", s->mb_x, s->mb_y, ((cbp&3)? 1 : 0) +((cbp&0x3C)? 2 : 0), show_bits(&s->gb, 24));
        set_stat(ST_INTRA_MB);
        s->ac_pred = get_bits1(&s->gb);
        *mb_type_ptr = MB_TYPE_INTRA;
        if(s->inter_intra_pred){
            s->h263_aic_dir= get_vlc2(&s->gb, inter_intra_vlc.table, INTER_INTRA_VLC_BITS, 1);
//            printf("%d%d %d %d/", s->ac_pred, s->h263_aic_dir, s->mb_x, s->mb_y);
        }
        if(s->per_mb_rl_table && cbp){
            s->rl_table_index = decode012(&s->gb);
            s->rl_chroma_table_index = s->rl_table_index;
        }
    }

    for (i = 0; i < 6; i++) {
        if (msmpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1, NULL) < 0)
	{
	    av_log(s->avctx, AV_LOG_ERROR, "\nerror while decoding block: %d x %d (%d)\n", s->mb_x, s->mb_y, i);
	    return -1;
	}
    }
    
    return 0;
}
//#define ERROR_DETAILS
static inline int msmpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                              int n, int coded, const uint8_t *scan_table)
{
    int level, i, last, run, run_diff;
    int dc_pred_dir;
    RLTable *rl;
    RL_VLC_ELEM *rl_vlc;
    int qmul, qadd;

    if (s->mb_intra) {
        qmul=1;
        qadd=0;

	/* DC coef */
        set_stat(ST_DC);
        level = msmpeg4_decode_dc(s, n, &dc_pred_dir);
        
        if (level < 0){
            av_log(s->avctx, AV_LOG_ERROR, "dc overflow- block: %d qscale: %d//\n", n, s->qscale);
            if(s->inter_intra_pred) level=0;
            else                    return -1;
        }
        if (n < 4) {
            rl = &rl_table[s->rl_table_index];
            if(level > 256*s->y_dc_scale){
                av_log(s->avctx, AV_LOG_ERROR, "dc overflow+ L qscale: %d//\n", s->qscale);
                if(!s->inter_intra_pred) return -1;
            }
        } else {
            rl = &rl_table[3 + s->rl_chroma_table_index];
            if(level > 256*s->c_dc_scale){
                av_log(s->avctx, AV_LOG_ERROR, "dc overflow+ C qscale: %d//\n", s->qscale);
                if(!s->inter_intra_pred) return -1;
            }
        }
        block[0] = level;

        run_diff = 0;
        i = 0;
        if (!coded) {
            goto not_coded;
        }
        if (s->ac_pred) {
            if (dc_pred_dir == 0) 
                scan_table = s->intra_v_scantable.permutated; /* left */
            else
                scan_table = s->intra_h_scantable.permutated; /* top */
        } else {
            scan_table = s->intra_scantable.permutated;
        }
        set_stat(ST_INTRA_AC);
        rl_vlc= rl->rl_vlc[0];
    } else {
        qmul = s->qscale << 1;
        qadd = (s->qscale - 1) | 1;
        i = -1;
        rl = &rl_table[3 + s->rl_table_index];

        if(s->msmpeg4_version==2)
            run_diff = 0;
        else
            run_diff = 1;

        if (!coded) {
            s->block_last_index[n] = i;
            return 0;
        }
        if(!scan_table)
            scan_table = s->inter_scantable.permutated;
        set_stat(ST_INTER_AC);
        rl_vlc= rl->rl_vlc[s->qscale];
    }
  {
    OPEN_READER(re, &s->gb);
    for(;;) {
        UPDATE_CACHE(re, &s->gb);
        GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2);
        if (level==0) {
            int cache;
            cache= GET_CACHE(re, &s->gb);
            /* escape */
            if (s->msmpeg4_version==1 || (cache&0x80000000)==0) {
                if (s->msmpeg4_version==1 || (cache&0x40000000)==0) {
                    /* third escape */
                    if(s->msmpeg4_version!=1) LAST_SKIP_BITS(re, &s->gb, 2);
                    UPDATE_CACHE(re, &s->gb);
                    if(s->msmpeg4_version<=3){
                        last=  SHOW_UBITS(re, &s->gb, 1); SKIP_CACHE(re, &s->gb, 1);
                        run=   SHOW_UBITS(re, &s->gb, 6); SKIP_CACHE(re, &s->gb, 6);
                        level= SHOW_SBITS(re, &s->gb, 8); LAST_SKIP_CACHE(re, &s->gb, 8);
                        SKIP_COUNTER(re, &s->gb, 1+6+8);
                    }else{                        
                        int sign;
                        last=  SHOW_UBITS(re, &s->gb, 1); SKIP_BITS(re, &s->gb, 1);
                        if(!s->esc3_level_length){
                            int ll;
                            //printf("ESC-3 %X at %d %d\n", show_bits(&s->gb, 24), s->mb_x, s->mb_y);
                            if(s->qscale<8){
                                ll= SHOW_UBITS(re, &s->gb, 3); SKIP_BITS(re, &s->gb, 3);
                                if(ll==0){
                                    if(SHOW_UBITS(re, &s->gb, 1)) av_log(s->avctx, AV_LOG_ERROR, "cool a new vlc code ,contact the ffmpeg developers and upload the file\n");
                                    SKIP_BITS(re, &s->gb, 1);
                                    ll=8;
                                }
                            }else{
                                ll=2;
                                while(ll<8 && SHOW_UBITS(re, &s->gb, 1)==0){
                                    ll++;
                                    SKIP_BITS(re, &s->gb, 1);
                                }
                                if(ll<8) SKIP_BITS(re, &s->gb, 1);
                            }

                            s->esc3_level_length= ll;
                            s->esc3_run_length= SHOW_UBITS(re, &s->gb, 2) + 3; SKIP_BITS(re, &s->gb, 2);
//printf("level length:%d, run length: %d\n", ll, s->esc3_run_length);
                            UPDATE_CACHE(re, &s->gb);
                        }
                        run=   SHOW_UBITS(re, &s->gb, s->esc3_run_length); 
                        SKIP_BITS(re, &s->gb, s->esc3_run_length);
                        
                        sign=  SHOW_UBITS(re, &s->gb, 1); 
                        SKIP_BITS(re, &s->gb, 1);
                        
                        level= SHOW_UBITS(re, &s->gb, s->esc3_level_length); 
                        SKIP_BITS(re, &s->gb, s->esc3_level_length);
                        if(sign) level= -level;
                    }
//printf("level: %d, run: %d at %d %d\n", level, run, s->mb_x, s->mb_y);
#if 0 // waste of time / this will detect very few errors
                    {
                        const int abs_level= ABS(level);
                        const int run1= run - rl->max_run[last][abs_level] - run_diff;
                        if(abs_level<=MAX_LEVEL && run<=MAX_RUN){
                            if(abs_level <= rl->max_level[last][run]){
                                fprintf(stderr, "illegal 3. esc, vlc encoding possible\n");
                                return DECODING_AC_LOST;
                            }
                            if(abs_level <= rl->max_level[last][run]*2){
                                fprintf(stderr, "illegal 3. esc, esc 1 encoding possible\n");
                                return DECODING_AC_LOST;
                            }
                            if(run1>=0 && abs_level <= rl->max_level[last][run1]){
                                fprintf(stderr, "illegal 3. esc, esc 2 encoding possible\n");
                                return DECODING_AC_LOST;
                            }
                        }
                    }
#endif
		    //level = level * qmul + (level>0) * qadd - (level<=0) * qadd ;
		    if (level>0) level= level * qmul + qadd;
                    else         level= level * qmul - qadd;
#if 0 // waste of time too :(
                    if(level>2048 || level<-2048){
                        fprintf(stderr, "|level| overflow in 3. esc\n");
                        return DECODING_AC_LOST;
                    }
#endif
                    i+= run + 1;
                    if(last) i+=192;
#ifdef ERROR_DETAILS
                if(run==66)
                    fprintf(stderr, "illegal vlc code in ESC3 level=%d\n", level);
                else if((i>62 && i<192) || i>192+63)
                    fprintf(stderr, "run overflow in ESC3 i=%d run=%d level=%d\n", i, run, level);
#endif
                } else {
                    /* second escape */
#if MIN_CACHE_BITS < 23
                    LAST_SKIP_BITS(re, &s->gb, 2);
                    UPDATE_CACHE(re, &s->gb);
#else
                    SKIP_BITS(re, &s->gb, 2);
#endif
                    GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2);
                    i+= run + rl->max_run[run>>7][level/qmul] + run_diff; //FIXME opt indexing
                    level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                    LAST_SKIP_BITS(re, &s->gb, 1);
#ifdef ERROR_DETAILS
                if(run==66)
                    fprintf(stderr, "illegal vlc code in ESC2 level=%d\n", level);
                else if((i>62 && i<192) || i>192+63)
                    fprintf(stderr, "run overflow in ESC2 i=%d run=%d level=%d\n", i, run, level);
#endif
                }
            } else {
                /* first escape */
#if MIN_CACHE_BITS < 22
                LAST_SKIP_BITS(re, &s->gb, 1);
                UPDATE_CACHE(re, &s->gb);
#else
                SKIP_BITS(re, &s->gb, 1);
#endif
                GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2);
                i+= run;
                level = level + rl->max_level[run>>7][(run-1)&63] * qmul;//FIXME opt indexing
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                LAST_SKIP_BITS(re, &s->gb, 1);
#ifdef ERROR_DETAILS
                if(run==66)
                    fprintf(stderr, "illegal vlc code in ESC1 level=%d\n", level);
                else if((i>62 && i<192) || i>192+63)
                    fprintf(stderr, "run overflow in ESC1 i=%d run=%d level=%d\n", i, run, level);
#endif
            }
        } else {
            i+= run;
            level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
            LAST_SKIP_BITS(re, &s->gb, 1);
#ifdef ERROR_DETAILS
                if(run==66)
                    fprintf(stderr, "illegal vlc code level=%d\n", level);
                else if((i>62 && i<192) || i>192+63)
                    fprintf(stderr, "run overflow i=%d run=%d level=%d\n", i, run, level);
#endif
        }
        if (i > 62){
            i-= 192;
            if(i&(~63)){
                const int left= s->gb.size_in_bits - get_bits_count(&s->gb);
                if(((i+192 == 64 && level/qmul==-1) || s->error_resilience<=1) && left>=0){
                    av_log(s->avctx, AV_LOG_ERROR, "ignoring overflow at %d %d\n", s->mb_x, s->mb_y);
                    break;
                }else{
                    av_log(s->avctx, AV_LOG_ERROR, "ac-tex damaged at %d %d\n", s->mb_x, s->mb_y);
                    return -1;
                }
            }

            block[scan_table[i]] = level;
            break;
        }

        block[scan_table[i]] = level;
    }
    CLOSE_READER(re, &s->gb);
  }
 not_coded:
    if (s->mb_intra) {
        mpeg4_pred_ac(s, block, n, dc_pred_dir);
        if (s->ac_pred) {
            i = 63; /* XXX: not optimal */
        }
    }
    if(s->msmpeg4_version>=4 && i>0) i=63; //FIXME/XXX optimize
    s->block_last_index[n] = i;
    
    return 0;
}

static int msmpeg4_decode_dc(MpegEncContext * s, int n, int *dir_ptr)
{
    int level, pred;

    if(s->msmpeg4_version<=2){
        if (n < 4) {
            level = get_vlc2(&s->gb, v2_dc_lum_vlc.table, DC_VLC_BITS, 3);
        } else {
            level = get_vlc2(&s->gb, v2_dc_chroma_vlc.table, DC_VLC_BITS, 3);
        }
        if (level < 0) 
            return -1;
        level-=256;
    }else{  //FIXME optimize use unified tables & index
        if (n < 4) {
            level = get_vlc2(&s->gb, dc_lum_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
        } else {
            level = get_vlc2(&s->gb, dc_chroma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
        }
        if (level < 0){
            av_log(s->avctx, AV_LOG_ERROR, "illegal dc vlc\n");
            return -1;
        }

        if (level == DC_MAX) {
            level = get_bits(&s->gb, 8);
            if (get_bits1(&s->gb))
                level = -level;
        } else if (level != 0) {
            if (get_bits1(&s->gb))
                level = -level;
        }
    }

    if(s->msmpeg4_version==1){
        int32_t *dc_val;
        pred = msmpeg4v1_pred_dc(s, n, &dc_val);
        level += pred;
        
        /* update predictor */
        *dc_val= level;
    }else{
        uint16_t *dc_val;
        pred = msmpeg4_pred_dc(s, n, &dc_val, dir_ptr);
        level += pred;

        /* update predictor */
        if (n < 4) {
            *dc_val = level * s->y_dc_scale;
        } else {
            *dc_val = level * s->c_dc_scale;
        }
    }

    return level;
}

static int msmpeg4_decode_motion(MpegEncContext * s, 
                                 int *mx_ptr, int *my_ptr)
{
    MVTable *mv;
    int code, mx, my;

    mv = &mv_tables[s->mv_table_index];

    code = get_vlc2(&s->gb, mv->vlc.table, MV_VLC_BITS, 2);
    if (code < 0){
        av_log(s->avctx, AV_LOG_ERROR, "illegal MV code at %d %d\n", s->mb_x, s->mb_y);
        return -1;
    }
    if (code == mv->n) {
//printf("MV ESC %X at %d %d\n", show_bits(&s->gb, 24), s->mb_x, s->mb_y);
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

/* cleanest way to support it
 * there is too much shared between versions so that we cant have 1 file per version & 1 common
 * as allmost everything would be in the common file 
 */
#include "wmv2.c"
