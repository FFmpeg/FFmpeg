/*
 * The simplest mpeg encoder (well, it was the simplest!)
 * Copyright (c) 2000,2001 Gerard Lantau.
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
 *
 * 4MV & hq & b-frame encoding stuff by Michael Niedermayer <michaelni@gmx.at>
 */
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif

static void encode_picture(MpegEncContext *s, int picture_number);
static void dct_unquantize_mpeg1_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale);
static void dct_unquantize_mpeg2_c(MpegEncContext *s,
                                   DCTELEM *block, int n, int qscale);
static void dct_unquantize_h263_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale);
static void draw_edges_c(UINT8 *buf, int wrap, int width, int height, int w);
static int dct_quantize_c(MpegEncContext *s, DCTELEM *block, int n, int qscale, int *overflow);

int (*dct_quantize)(MpegEncContext *s, DCTELEM *block, int n, int qscale, int *overflow)= dct_quantize_c;
void (*draw_edges)(UINT8 *buf, int wrap, int width, int height, int w)= draw_edges_c;

#define EDGE_WIDTH 16

/* enable all paranoid tests for rounding, overflows, etc... */
//#define PARANOID

//#define DEBUG


/* for jpeg fast DCT */
#define CONST_BITS 14

static const unsigned short aanscales[64] = {
    /* precomputed values scaled up by 14 bits */
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    22725, 31521, 29692, 26722, 22725, 17855, 12299,  6270,
    21407, 29692, 27969, 25172, 21407, 16819, 11585,  5906,
    19266, 26722, 25172, 22654, 19266, 15137, 10426,  5315,
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    12873, 17855, 16819, 15137, 12873, 10114,  6967,  3552,
    8867, 12299, 11585, 10426,  8867,  6967,  4799,  2446,
    4520,  6270,  5906,  5315,  4520,  3552,  2446,  1247
};

static UINT8 h263_chroma_roundtab[16] = {
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
};

static UINT16 default_mv_penalty[MAX_FCODE+1][MAX_MV*2+1];
static UINT8 default_fcode_tab[MAX_MV*2+1];

extern UINT8 zigzag_end[64];

/* default motion estimation */
int motion_estimation_method = ME_EPZS;

static void convert_matrix(int (*qmat)[64], uint16_t (*qmat16)[64], uint16_t (*qmat16_bias)[64],
                           const UINT16 *quant_matrix, int bias)
{
    int qscale;

    for(qscale=1; qscale<32; qscale++){
        int i;
        if (av_fdct == jpeg_fdct_ifast) {
            for(i=0;i<64;i++) {
                const int j= block_permute_op(i);
                /* 16 <= qscale * quant_matrix[i] <= 7905 */
                /* 19952         <= aanscales[i] * qscale * quant_matrix[i]           <= 249205026 */
                /* (1<<36)/19952 >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= (1<<36)/249205026 */
                /* 3444240       >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= 275 */
                
                qmat[qscale][j] = (int)((UINT64_C(1) << (QMAT_SHIFT + 11)) / 
                                (aanscales[i] * qscale * quant_matrix[j]));
            }
        } else {
            for(i=0;i<64;i++) {
                /* We can safely suppose that 16 <= quant_matrix[i] <= 255
                   So 16           <= qscale * quant_matrix[i]             <= 7905
                   so (1<<19) / 16 >= (1<<19) / (qscale * quant_matrix[i]) >= (1<<19) / 7905
                   so 32768        >= (1<<19) / (qscale * quant_matrix[i]) >= 67
                */
                qmat  [qscale][i] = (1 << QMAT_SHIFT_MMX) / (qscale * quant_matrix[i]);
                qmat16[qscale][i] = (1 << QMAT_SHIFT_MMX) / (qscale * quant_matrix[block_permute_op(i)]);

                if(qmat16[qscale][i]==0 || qmat16[qscale][i]==128*256) qmat16[qscale][i]=128*256-1;

                qmat16_bias[qscale][i]= ROUNDED_DIV(bias<<(16-QUANT_BIAS_SHIFT), qmat16[qscale][i]);
            }
        }
    }
}

/* init common structure for both encoder and decoder */
int MPV_common_init(MpegEncContext *s)
{
    int c_size, i;
    UINT8 *pict;

    s->dct_unquantize_h263 = dct_unquantize_h263_c;
    s->dct_unquantize_mpeg1 = dct_unquantize_mpeg1_c;
    s->dct_unquantize_mpeg2 = dct_unquantize_mpeg2_c;
        
#ifdef HAVE_MMX
    MPV_common_init_mmx(s);
#endif
    //setup default unquantizers (mpeg4 might change it later)
    if(s->out_format == FMT_H263)
        s->dct_unquantize = s->dct_unquantize_h263;
    else
        s->dct_unquantize = s->dct_unquantize_mpeg1;
    
    s->mb_width = (s->width + 15) / 16;
    s->mb_height = (s->height + 15) / 16;
    s->mb_num = s->mb_width * s->mb_height;
    s->linesize = s->mb_width * 16 + 2 * EDGE_WIDTH;

    for(i=0;i<3;i++) {
        int w, h, shift, pict_start;

        w = s->linesize;
        h = s->mb_height * 16 + 2 * EDGE_WIDTH;
        shift = (i == 0) ? 0 : 1;
        c_size = (w >> shift) * (h >> shift);
        pict_start = (w >> shift) * (EDGE_WIDTH >> shift) + (EDGE_WIDTH >> shift);

        pict = av_mallocz(c_size);
        if (pict == NULL)
            goto fail;
        s->last_picture_base[i] = pict;
        s->last_picture[i] = pict + pict_start;
    
        pict = av_mallocz(c_size);
        if (pict == NULL)
            goto fail;
        s->next_picture_base[i] = pict;
        s->next_picture[i] = pict + pict_start;
        
        if (s->has_b_frames || s->codec_id==CODEC_ID_MPEG4) {
        /* Note the MPEG4 stuff is here cuz of buggy encoders which dont set the low_delay flag but 
           do low-delay encoding, so we cant allways distinguish b-frame containing streams from low_delay streams */
            pict = av_mallocz(c_size);
            if (pict == NULL) 
                goto fail;
            s->aux_picture_base[i] = pict;
            s->aux_picture[i] = pict + pict_start;
        }
    }
    
    if (s->encoding) {
        int j;
        int mv_table_size= (s->mb_width+2)*(s->mb_height+2);

        /* Allocate MB type table */
        s->mb_type = av_mallocz(s->mb_num * sizeof(char));
        if (s->mb_type == NULL) {
            perror("malloc");
            goto fail;
        }
        
        s->mb_var = av_mallocz(s->mb_num * sizeof(INT16));
        if (s->mb_var == NULL) {
            perror("malloc");
            goto fail;
        }

        /* Allocate MV tables */
        s->p_mv_table = av_mallocz(mv_table_size * 2 * sizeof(INT16));
        if (s->p_mv_table == NULL) {
            perror("malloc");
            goto fail;
        }
        s->last_p_mv_table = av_mallocz(mv_table_size * 2 * sizeof(INT16));
        if (s->last_p_mv_table == NULL) {
            perror("malloc");
            goto fail;
        }
        s->b_forw_mv_table = av_mallocz(mv_table_size * 2 * sizeof(INT16));
        if (s->b_forw_mv_table == NULL) {
            perror("malloc");
            goto fail;
        }
        s->b_back_mv_table = av_mallocz(mv_table_size * 2 * sizeof(INT16));
        if (s->b_back_mv_table == NULL) {
            perror("malloc");
            goto fail;
        }
        s->b_bidir_forw_mv_table = av_mallocz(mv_table_size * 2 * sizeof(INT16));
        if (s->b_bidir_forw_mv_table == NULL) {
            perror("malloc");
            goto fail;
        }
        s->b_bidir_back_mv_table = av_mallocz(mv_table_size * 2 * sizeof(INT16));
        if (s->b_bidir_back_mv_table == NULL) {
            perror("malloc");
            goto fail;
        }
        s->b_direct_forw_mv_table = av_mallocz(mv_table_size * 2 * sizeof(INT16));
        if (s->b_direct_forw_mv_table == NULL) {
            perror("malloc");
            goto fail;
        }
        s->b_direct_back_mv_table = av_mallocz(mv_table_size * 2 * sizeof(INT16));
        if (s->b_direct_back_mv_table == NULL) {
            perror("malloc");
            goto fail;
        }
        s->b_direct_mv_table = av_mallocz(mv_table_size * 2 * sizeof(INT16));
        if (s->b_direct_mv_table == NULL) {
            perror("malloc");
            goto fail;
        }

        s->me_scratchpad = av_mallocz( s->linesize*16*3*sizeof(uint8_t));
        if (s->me_scratchpad == NULL) {
            perror("malloc");
            goto fail;
        }

        if(s->max_b_frames){
            for(j=0; j<REORDER_BUFFER_SIZE; j++){
                int i;
                for(i=0;i<3;i++) {
                    int w, h, shift;

                    w = s->linesize;
                    h = s->mb_height * 16;
                    shift = (i == 0) ? 0 : 1;
                    c_size = (w >> shift) * (h >> shift);

                    pict = av_mallocz(c_size);
                    if (pict == NULL)
                        goto fail;
                    s->picture_buffer[j][i] = pict;
                }
            }
        }
    }
    
    if (s->out_format == FMT_H263 || s->encoding) {
        int size;
        /* MV prediction */
        size = (2 * s->mb_width + 2) * (2 * s->mb_height + 2);
        s->motion_val = av_malloc(size * 2 * sizeof(INT16));
        if (s->motion_val == NULL)
            goto fail;
        memset(s->motion_val, 0, size * 2 * sizeof(INT16));
    }

    if (s->h263_pred || s->h263_plus) {
        int y_size, c_size, i, size;
        
        /* dc values */

        y_size = (2 * s->mb_width + 2) * (2 * s->mb_height + 2);
        c_size = (s->mb_width + 2) * (s->mb_height + 2);
        size = y_size + 2 * c_size;
        s->dc_val[0] = av_malloc(size * sizeof(INT16));
        if (s->dc_val[0] == NULL)
            goto fail;
        s->dc_val[1] = s->dc_val[0] + y_size;
        s->dc_val[2] = s->dc_val[1] + c_size;
        for(i=0;i<size;i++)
            s->dc_val[0][i] = 1024;

        /* ac values */
        s->ac_val[0] = av_mallocz(size * sizeof(INT16) * 16);
        if (s->ac_val[0] == NULL)
            goto fail;
        s->ac_val[1] = s->ac_val[0] + y_size;
        s->ac_val[2] = s->ac_val[1] + c_size;
        
        /* cbp values */
        s->coded_block = av_mallocz(y_size);
        if (!s->coded_block)
            goto fail;

        /* which mb is a intra block */
        s->mbintra_table = av_mallocz(s->mb_num);
        if (!s->mbintra_table)
            goto fail;
        memset(s->mbintra_table, 1, s->mb_num);
        
        /* divx501 bitstream reorder buffer */
        s->bitstream_buffer= av_mallocz(BITSTREAM_BUFFER_SIZE);
        if (!s->bitstream_buffer)
            goto fail;
    }
    /* default structure is frame */
    s->picture_structure = PICT_FRAME;

    /* init macroblock skip table */
    s->mbskip_table = av_mallocz(s->mb_num);
    if (!s->mbskip_table)
        goto fail;
    
    s->block= s->blocks[0];

    s->context_initialized = 1;
    return 0;
 fail:
    MPV_common_end(s);
    return -1;
}

/* init common structure for both encoder and decoder */
void MPV_common_end(MpegEncContext *s)
{
    int i;

    av_freep(&s->mb_type);
    av_freep(&s->mb_var);
    av_freep(&s->p_mv_table);
    av_freep(&s->last_p_mv_table);
    av_freep(&s->b_forw_mv_table);
    av_freep(&s->b_back_mv_table);
    av_freep(&s->b_bidir_forw_mv_table);
    av_freep(&s->b_bidir_back_mv_table);
    av_freep(&s->b_direct_forw_mv_table);
    av_freep(&s->b_direct_back_mv_table);
    av_freep(&s->b_direct_mv_table);
    av_freep(&s->motion_val);
    av_freep(&s->dc_val[0]);
    av_freep(&s->ac_val[0]);
    av_freep(&s->coded_block);
    av_freep(&s->mbintra_table);
    av_freep(&s->me_scratchpad);

    av_freep(&s->mbskip_table);
    av_freep(&s->bitstream_buffer);
    for(i=0;i<3;i++) {
        int j;
        av_freep(&s->last_picture_base[i]);
        av_freep(&s->next_picture_base[i]);
        av_freep(&s->aux_picture_base[i]);
        for(j=0; j<REORDER_BUFFER_SIZE; j++){
            av_freep(&s->picture_buffer[j][i]);
        }
    }
    s->context_initialized = 0;
}

/* init video encoder */
int MPV_encode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    int i;

    avctx->pix_fmt = PIX_FMT_YUV420P;

    s->bit_rate = avctx->bit_rate;
    s->bit_rate_tolerance = avctx->bit_rate_tolerance;
    s->frame_rate = avctx->frame_rate;
    s->width = avctx->width;
    s->height = avctx->height;
    s->gop_size = avctx->gop_size;
    s->rtp_mode = avctx->rtp_mode;
    s->rtp_payload_size = avctx->rtp_payload_size;
    if (avctx->rtp_callback)
        s->rtp_callback = avctx->rtp_callback;
    s->qmin= avctx->qmin;
    s->qmax= avctx->qmax;
    s->max_qdiff= avctx->max_qdiff;
    s->qcompress= avctx->qcompress;
    s->qblur= avctx->qblur;
    s->b_quant_factor= avctx->b_quant_factor;
    s->avctx = avctx;
    s->aspect_ratio_info= avctx->aspect_ratio_info;
    s->flags= avctx->flags;
    s->max_b_frames= avctx->max_b_frames;
    s->rc_strategy= avctx->rc_strategy;
    s->b_frame_strategy= avctx->b_frame_strategy;
    s->codec_id= avctx->codec->id;

    if (s->gop_size <= 1) {
        s->intra_only = 1;
        s->gop_size = 12;
    } else {
        s->intra_only = 0;
    }
    
    /* ME algorithm */
    if (avctx->me_method == 0)
        /* For compatibility */
        s->me_method = motion_estimation_method;
    else
        s->me_method = avctx->me_method;
        
    /* Fixed QSCALE */
    s->fixed_qscale = (avctx->flags & CODEC_FLAG_QSCALE);
    
    switch(avctx->codec->id) {
    case CODEC_ID_MPEG1VIDEO:
        s->out_format = FMT_MPEG1;
        avctx->delay=0; //FIXME not sure, should check the spec
        break;
    case CODEC_ID_MJPEG:
        s->out_format = FMT_MJPEG;
        s->intra_only = 1; /* force intra only for jpeg */
        s->mjpeg_write_tables = 1; /* write all tables */
	s->mjpeg_data_only_frames = 0; /* write all the needed headers */
        s->mjpeg_vsample[0] = 2; /* set up default sampling factors */
        s->mjpeg_vsample[1] = 1; /* the only currently supported values */
        s->mjpeg_vsample[2] = 1; 
        s->mjpeg_hsample[0] = 2;
        s->mjpeg_hsample[1] = 1; 
        s->mjpeg_hsample[2] = 1; 
        if (mjpeg_init(s) < 0)
            return -1;
        avctx->delay=0;
        break;
    case CODEC_ID_H263:
        if (h263_get_picture_format(s->width, s->height) == 7) {
            printf("Input picture size isn't suitable for h263 codec! try h263+\n");
            return -1;
        }
        s->out_format = FMT_H263;
        avctx->delay=0;
        break;
    case CODEC_ID_H263P:
        s->out_format = FMT_H263;
        s->rtp_mode = 1;
        s->rtp_payload_size = 1200; 
        s->h263_plus = 1;
        s->unrestricted_mv = 1;
        s->h263_aic = 1;
        
        /* These are just to be sure */
        s->umvplus = 0;
        s->umvplus_dec = 0;
        avctx->delay=0;
        break;
    case CODEC_ID_RV10:
        s->out_format = FMT_H263;
        s->h263_rv10 = 1;
        avctx->delay=0;
        break;
    case CODEC_ID_MPEG4:
        s->out_format = FMT_H263;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->has_b_frames= s->max_b_frames ? 1 : 0;
        s->low_delay=0;
        avctx->delay= s->low_delay ? 0 : (s->max_b_frames + 1); 
        break;
    case CODEC_ID_MSMPEG4V1:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 1;
        avctx->delay=0;
        break;
    case CODEC_ID_MSMPEG4V2:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 2;
        avctx->delay=0;
        break;
    case CODEC_ID_MSMPEG4V3:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version= 3;
        avctx->delay=0;
        break;
    default:
        return -1;
    }
    
    if((s->flags&CODEC_FLAG_4MV) && !(s->flags&CODEC_FLAG_HQ)){
        printf("4MV is currently only supported in HQ mode\n");
        return -1;
    }

    { /* set up some save defaults, some codecs might override them later */
        static int done=0;
        if(!done){
            int i;
            done=1;
            memset(default_mv_penalty, 0, sizeof(UINT16)*(MAX_FCODE+1)*(2*MAX_MV+1));
            memset(default_fcode_tab , 0, sizeof(UINT8)*(2*MAX_MV+1));

            for(i=-16; i<16; i++){
                default_fcode_tab[i + MAX_MV]= 1;
            }
        }
    }
    s->mv_penalty= default_mv_penalty;
    s->fcode_tab= default_fcode_tab;

    if (s->out_format == FMT_H263)
        h263_encode_init(s);
    else if (s->out_format == FMT_MPEG1)
        mpeg1_encode_init(s);

    /* dont use mv_penalty table for crap MV as it would be confused */
    if (s->me_method < ME_EPZS) s->mv_penalty = default_mv_penalty;

    s->encoding = 1;

    /* init */
    if (MPV_common_init(s) < 0)
        return -1;
    
    /* init default q matrix */
    for(i=0;i<64;i++) {
        if(s->out_format == FMT_H263)
            s->intra_matrix[i] = default_non_intra_matrix[i];
        else
            s->intra_matrix[i] = default_intra_matrix[i];

        s->inter_matrix[i] = default_non_intra_matrix[i];
    }

    /* precompute matrix */
    /* for mjpeg, we do include qscale in the matrix */
    if (s->out_format != FMT_MJPEG) {
        convert_matrix(s->q_intra_matrix, s->q_intra_matrix16, s->q_intra_matrix16_bias, 
                       s->intra_matrix, s->intra_quant_bias);
        convert_matrix(s->q_inter_matrix, s->q_inter_matrix16, s->q_inter_matrix16_bias, 
                       s->inter_matrix, s->inter_quant_bias);
    }

    if(ff_rate_control_init(s) < 0)
        return -1;

    s->picture_number = 0;
    s->picture_in_gop_number = 0;
    s->fake_picture_number = 0;
    /* motion detector init */
    s->f_code = 1;
    s->b_code = 1;

    return 0;
}

int MPV_encode_end(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

#ifdef STATS
    print_stats();
#endif

    ff_rate_control_uninit(s);

    MPV_common_end(s);
    if (s->out_format == FMT_MJPEG)
        mjpeg_close(s);
      
    return 0;
}

/* draw the edges of width 'w' of an image of size width, height */
static void draw_edges_c(UINT8 *buf, int wrap, int width, int height, int w)
{
    UINT8 *ptr, *last_line;
    int i;

    last_line = buf + (height - 1) * wrap;
    for(i=0;i<w;i++) {
        /* top and bottom */
        memcpy(buf - (i + 1) * wrap, buf, width);
        memcpy(last_line + (i + 1) * wrap, last_line, width);
    }
    /* left and right */
    ptr = buf;
    for(i=0;i<height;i++) {
        memset(ptr - w, ptr[0], w);
        memset(ptr + width, ptr[width-1], w);
        ptr += wrap;
    }
    /* corners */
    for(i=0;i<w;i++) {
        memset(buf - (i + 1) * wrap - w, buf[0], w); /* top left */
        memset(buf - (i + 1) * wrap + width, buf[width-1], w); /* top right */
        memset(last_line + (i + 1) * wrap - w, last_line[0], w); /* top left */
        memset(last_line + (i + 1) * wrap + width, last_line[width-1], w); /* top right */
    }
}

/* generic function for encode/decode called before a frame is coded/decoded */
void MPV_frame_start(MpegEncContext *s)
{
    int i;
    UINT8 *tmp;

    s->mb_skiped = 0;
    if (s->pict_type == B_TYPE) {
        for(i=0;i<3;i++) {
            s->current_picture[i] = s->aux_picture[i];
        }
    } else {
        for(i=0;i<3;i++) {
            /* swap next and last */
            tmp = s->last_picture[i];
            s->last_picture[i] = s->next_picture[i];
            s->next_picture[i] = tmp;
            s->current_picture[i] = tmp;
        }
    }
}

/* generic function for encode/decode called after a frame has been coded/decoded */
void MPV_frame_end(MpegEncContext *s)
{
    /* draw edge for correct motion prediction if outside */
    if (s->pict_type != B_TYPE && !s->intra_only) {
      if(s->avctx==NULL || s->avctx->codec->id!=CODEC_ID_MPEG4 || s->divx_version==500){
        draw_edges(s->current_picture[0], s->linesize, s->mb_width*16, s->mb_height*16, EDGE_WIDTH);
        draw_edges(s->current_picture[1], s->linesize/2, s->mb_width*8, s->mb_height*8, EDGE_WIDTH/2);
        draw_edges(s->current_picture[2], s->linesize/2, s->mb_width*8, s->mb_height*8, EDGE_WIDTH/2);
      }else{
        /* mpeg4? / opendivx / xvid */
        draw_edges(s->current_picture[0], s->linesize, s->width, s->height, EDGE_WIDTH);
        draw_edges(s->current_picture[1], s->linesize/2, s->width/2, s->height/2, EDGE_WIDTH/2);
        draw_edges(s->current_picture[2], s->linesize/2, s->width/2, s->height/2, EDGE_WIDTH/2);
      }
    }
    emms_c();
    
    if(s->pict_type!=B_TYPE){
        s->last_non_b_pict_type= s->pict_type;
        s->last_non_b_qscale= s->qscale;
        s->last_non_b_mc_mb_var= s->mc_mb_var;
        s->num_available_buffers++;
        if(s->num_available_buffers>2) s->num_available_buffers= 2;
    }
}

/* reorder input for encoding */
void reorder_input(MpegEncContext *s, AVPicture *pict)
{
    int i, j, index;
            
    if(s->max_b_frames > FF_MAX_B_FRAMES) s->max_b_frames= FF_MAX_B_FRAMES;

//        delay= s->max_b_frames+1; (or 0 if no b frames cuz decoder diff)

    for(j=0; j<REORDER_BUFFER_SIZE-1; j++){
        s->coded_order[j]= s->coded_order[j+1];
    }
    s->coded_order[j].picture[0]= s->coded_order[j].picture[1]= s->coded_order[j].picture[2]= NULL; //catch uninitalized buffers
    s->coded_order[j].pict_type=0;

    switch(s->input_pict_type){
    default: 
    case I_TYPE:
    case S_TYPE:
    case P_TYPE:
        index= s->max_b_frames - s->b_frames_since_non_b;
        s->b_frames_since_non_b=0;
        break;            
    case B_TYPE:
        index= s->max_b_frames + 1;
        s->b_frames_since_non_b++;
        break;          
    }
//printf("index:%d type:%d strides: %d %d\n", index, s->input_pict_type, pict->linesize[0], s->linesize);
    if(   (index==0 || (s->flags&CODEC_FLAG_INPUT_PRESERVED))
       && pict->linesize[0] == s->linesize
       && pict->linesize[1] == s->linesize>>1
       && pict->linesize[2] == s->linesize>>1){
//printf("ptr\n");
        for(i=0; i<3; i++){
            s->coded_order[index].picture[i]= pict->data[i];
        }
    }else{
//printf("copy\n");
        for(i=0; i<3; i++){
            uint8_t *src = pict->data[i];
            uint8_t *dest;
            int src_wrap = pict->linesize[i];
            int dest_wrap = s->linesize;
            int w = s->width;
            int h = s->height;

            if(index==0) dest= s->last_picture[i]+16; //is current_picture indeed but the switch hapens after reordering
            else         dest= s->picture_buffer[s->picture_buffer_index][i];

            if (i >= 1) {
                dest_wrap >>= 1;
                w >>= 1;
                h >>= 1;
            }

            s->coded_order[index].picture[i]= dest;
            for(j=0;j<h;j++) {
                memcpy(dest, src, w);
                dest += dest_wrap;
                src += src_wrap;
            }
        }
        if(index!=0){
            s->picture_buffer_index++;
            if(s->picture_buffer_index >= REORDER_BUFFER_SIZE-1) s->picture_buffer_index=0;
        }
    }
    s->coded_order[index].pict_type = s->input_pict_type;
    s->coded_order[index].qscale    = s->input_qscale;
    s->coded_order[index].force_type= s->force_input_type;
    s->coded_order[index].picture_in_gop_number= s->input_picture_in_gop_number;
    s->coded_order[index].picture_number= s->input_picture_number;

    for(i=0; i<3; i++){
        s->new_picture[i]= s->coded_order[0].picture[i];
    }
}

int MPV_encode_picture(AVCodecContext *avctx,
                       unsigned char *buf, int buf_size, void *data)
{
    MpegEncContext *s = avctx->priv_data;
    AVPicture *pict = data;

    s->input_qscale = avctx->quality;

    init_put_bits(&s->pb, buf, buf_size, NULL, NULL);

    if(avctx->flags&CODEC_FLAG_TYPE){
        s->input_pict_type=
        s->force_input_type= avctx->key_frame ? I_TYPE : P_TYPE;
    }else if(s->flags&CODEC_FLAG_PASS2){
        s->input_pict_type=
        s->force_input_type= s->rc_context.entry[s->input_picture_number].new_pict_type;
    }else{
        s->force_input_type=0;
        if (!s->intra_only) {
            /* first picture of GOP is intra */
            if (s->input_picture_in_gop_number % s->gop_size==0){
                s->input_pict_type = I_TYPE;
            }else if(s->max_b_frames==0){
                s->input_pict_type = P_TYPE;
            }else{
                if(s->b_frames_since_non_b < s->max_b_frames) //FIXME more IQ
                    s->input_pict_type = B_TYPE;
                else
                    s->input_pict_type = P_TYPE;
            }
        } else {
            s->input_pict_type = I_TYPE;
        }
    }

    if(s->input_pict_type==I_TYPE)
        s->input_picture_in_gop_number=0;
    
    reorder_input(s, pict);
    
    /* output? */
    if(s->coded_order[0].picture[0]){

        s->pict_type= s->coded_order[0].pict_type;
        if (s->fixed_qscale) /* the ratecontrol needs the last qscale so we dont touch it for CBR */
            s->qscale= s->coded_order[0].qscale;
        s->force_type= s->coded_order[0].force_type;
        s->picture_in_gop_number= s->coded_order[0].picture_in_gop_number;
        s->picture_number= s->coded_order[0].picture_number;

        MPV_frame_start(s);

        encode_picture(s, s->picture_number);
        avctx->key_frame   = (s->pict_type == I_TYPE);
        avctx->pict_type   = s->pict_type;
        avctx->real_pict_num  = s->picture_number;
        avctx->header_bits = s->header_bits;
        avctx->mv_bits     = s->mv_bits;
        avctx->misc_bits   = s->misc_bits;
        avctx->i_tex_bits  = s->i_tex_bits;
        avctx->p_tex_bits  = s->p_tex_bits;
        avctx->i_count     = s->i_count;
        avctx->p_count     = s->p_count;
        avctx->skip_count  = s->skip_count;

        MPV_frame_end(s);

        if (s->out_format == FMT_MJPEG)
            mjpeg_picture_trailer(s);

        avctx->quality = s->qscale;
        
        if(s->flags&CODEC_FLAG_PASS1)
            ff_write_pass1_stats(s);
    }

    s->input_picture_number++;
    s->input_picture_in_gop_number++;

    flush_put_bits(&s->pb);
    s->frame_bits  = (pbBufPtr(&s->pb) - s->pb.buf) * 8;
    if(s->pict_type==B_TYPE) s->pb_frame_bits+= s->frame_bits;
    else                     s->pb_frame_bits= s->frame_bits;

    s->total_bits += s->frame_bits;
    avctx->frame_bits  = s->frame_bits;
//printf("fcode: %d, type: %d, head: %d, mv: %d, misc: %d, frame: %d, itex: %d, ptex: %d\n", 
//s->f_code, avctx->key_frame, s->header_bits, s->mv_bits, s->misc_bits, s->frame_bits, s->i_tex_bits, s->p_tex_bits);

    if (avctx->get_psnr) {
        /* At this point pict->data should have the original frame   */
        /* an s->current_picture should have the coded/decoded frame */
        get_psnr(pict->data, s->current_picture,
                 pict->linesize, s->linesize, avctx);
//        printf("%f\n", avctx->psnr_y);
    }
    return pbBufPtr(&s->pb) - s->pb.buf;
}

static inline void gmc1_motion(MpegEncContext *s,
                               UINT8 *dest_y, UINT8 *dest_cb, UINT8 *dest_cr,
                               int dest_offset,
                               UINT8 **ref_picture, int src_offset,
                               int h)
{
    UINT8 *ptr;
    int offset, src_x, src_y, linesize;
    int motion_x, motion_y;

    if(s->real_sprite_warping_points>1) printf("more than 1 warp point isnt supported\n");
    motion_x= s->sprite_offset[0][0];
    motion_y= s->sprite_offset[0][1];
    src_x = s->mb_x * 16 + (motion_x >> (s->sprite_warping_accuracy+1));
    src_y = s->mb_y * 16 + (motion_y >> (s->sprite_warping_accuracy+1));
    motion_x<<=(3-s->sprite_warping_accuracy);
    motion_y<<=(3-s->sprite_warping_accuracy);
    src_x = clip(src_x, -16, s->width);
    if (src_x == s->width)
        motion_x =0;
    src_y = clip(src_y, -16, s->height);
    if (src_y == s->height)
        motion_y =0;
    
    linesize = s->linesize;
    ptr = ref_picture[0] + (src_y * linesize) + src_x + src_offset;

    dest_y+=dest_offset;
    gmc1(dest_y  , ptr  , linesize, h, motion_x&15, motion_y&15, s->no_rounding);
    gmc1(dest_y+8, ptr+8, linesize, h, motion_x&15, motion_y&15, s->no_rounding);

    motion_x= s->sprite_offset[1][0];
    motion_y= s->sprite_offset[1][1];
    src_x = s->mb_x * 8 + (motion_x >> (s->sprite_warping_accuracy+1));
    src_y = s->mb_y * 8 + (motion_y >> (s->sprite_warping_accuracy+1));
    motion_x<<=(3-s->sprite_warping_accuracy);
    motion_y<<=(3-s->sprite_warping_accuracy);
    src_x = clip(src_x, -8, s->width>>1);
    if (src_x == s->width>>1)
        motion_x =0;
    src_y = clip(src_y, -8, s->height>>1);
    if (src_y == s->height>>1)
        motion_y =0;

    offset = (src_y * linesize>>1) + src_x + (src_offset>>1);
    ptr = ref_picture[1] + offset;
    gmc1(dest_cb + (dest_offset>>1), ptr, linesize>>1, h>>1, motion_x&15, motion_y&15, s->no_rounding);
    ptr = ref_picture[2] + offset;
    gmc1(dest_cr + (dest_offset>>1), ptr, linesize>>1, h>>1, motion_x&15, motion_y&15, s->no_rounding);
    
    return;
}

/* apply one mpeg motion vector to the three components */
static inline void mpeg_motion(MpegEncContext *s,
                               UINT8 *dest_y, UINT8 *dest_cb, UINT8 *dest_cr,
                               int dest_offset,
                               UINT8 **ref_picture, int src_offset,
                               int field_based, op_pixels_func *pix_op,
                               int motion_x, int motion_y, int h)
{
    UINT8 *ptr;
    int dxy, offset, mx, my, src_x, src_y, height, linesize;
if(s->quarter_sample)
{
    motion_x>>=1;
    motion_y>>=1;
}

    dxy = ((motion_y & 1) << 1) | (motion_x & 1);
    src_x = s->mb_x * 16 + (motion_x >> 1);
    src_y = s->mb_y * (16 >> field_based) + (motion_y >> 1);
                
    /* WARNING: do no forget half pels */
    height = s->height >> field_based;
    src_x = clip(src_x, -16, s->width);
    if (src_x == s->width)
        dxy &= ~1;
    src_y = clip(src_y, -16, height);
    if (src_y == height)
        dxy &= ~2;
    linesize = s->linesize << field_based;
    ptr = ref_picture[0] + (src_y * linesize) + (src_x) + src_offset;
    dest_y += dest_offset;
    pix_op[dxy](dest_y, ptr, linesize, h);
    pix_op[dxy](dest_y + 8, ptr + 8, linesize, h);

    if (s->out_format == FMT_H263) {
        dxy = 0;
        if ((motion_x & 3) != 0)
            dxy |= 1;
        if ((motion_y & 3) != 0)
            dxy |= 2;
        mx = motion_x >> 2;
        my = motion_y >> 2;
    } else {
        mx = motion_x / 2;
        my = motion_y / 2;
        dxy = ((my & 1) << 1) | (mx & 1);
        mx >>= 1;
        my >>= 1;
    }
    
    src_x = s->mb_x * 8 + mx;
    src_y = s->mb_y * (8 >> field_based) + my;
    src_x = clip(src_x, -8, s->width >> 1);
    if (src_x == (s->width >> 1))
        dxy &= ~1;
    src_y = clip(src_y, -8, height >> 1);
    if (src_y == (height >> 1))
        dxy &= ~2;

    offset = (src_y * (linesize >> 1)) + src_x + (src_offset >> 1);
    ptr = ref_picture[1] + offset;
    pix_op[dxy](dest_cb + (dest_offset >> 1), ptr, linesize >> 1, h >> 1);
    ptr = ref_picture[2] + offset;
    pix_op[dxy](dest_cr + (dest_offset >> 1), ptr, linesize >> 1, h >> 1);
}

static inline void qpel_motion(MpegEncContext *s,
                               UINT8 *dest_y, UINT8 *dest_cb, UINT8 *dest_cr,
                               int dest_offset,
                               UINT8 **ref_picture, int src_offset,
                               int field_based, op_pixels_func *pix_op,
                               qpel_mc_func *qpix_op,
                               int motion_x, int motion_y, int h)
{
    UINT8 *ptr;
    int dxy, offset, mx, my, src_x, src_y, height, linesize;

    dxy = ((motion_y & 3) << 2) | (motion_x & 3);
    src_x = s->mb_x * 16 + (motion_x >> 2);
    src_y = s->mb_y * (16 >> field_based) + (motion_y >> 2);

    height = s->height >> field_based;
    src_x = clip(src_x, -16, s->width);
    if (src_x == s->width)
        dxy &= ~3;
    src_y = clip(src_y, -16, height);
    if (src_y == height)
        dxy &= ~12;
    linesize = s->linesize << field_based;
    ptr = ref_picture[0] + (src_y * linesize) + src_x + src_offset;
    dest_y += dest_offset;
//printf("%d %d %d\n", src_x, src_y, dxy);
    qpix_op[dxy](dest_y                 , ptr                 , linesize, linesize, motion_x&3, motion_y&3);
    qpix_op[dxy](dest_y              + 8, ptr              + 8, linesize, linesize, motion_x&3, motion_y&3);
    qpix_op[dxy](dest_y + linesize*8    , ptr + linesize*8    , linesize, linesize, motion_x&3, motion_y&3);
    qpix_op[dxy](dest_y + linesize*8 + 8, ptr + linesize*8 + 8, linesize, linesize, motion_x&3, motion_y&3);
    
    mx= (motion_x>>1) | (motion_x&1);
    my= (motion_y>>1) | (motion_y&1);

    dxy = 0;
    if ((mx & 3) != 0)
        dxy |= 1;
    if ((my & 3) != 0)
        dxy |= 2;
    mx = mx >> 2;
    my = my >> 2;
    
    src_x = s->mb_x * 8 + mx;
    src_y = s->mb_y * (8 >> field_based) + my;
    src_x = clip(src_x, -8, s->width >> 1);
    if (src_x == (s->width >> 1))
        dxy &= ~1;
    src_y = clip(src_y, -8, height >> 1);
    if (src_y == (height >> 1))
        dxy &= ~2;

    offset = (src_y * (linesize >> 1)) + src_x + (src_offset >> 1);
    ptr = ref_picture[1] + offset;
    pix_op[dxy](dest_cb + (dest_offset >> 1), ptr, linesize >> 1, h >> 1);
    ptr = ref_picture[2] + offset;
    pix_op[dxy](dest_cr + (dest_offset >> 1), ptr, linesize >> 1, h >> 1);
}


static inline void MPV_motion(MpegEncContext *s, 
                              UINT8 *dest_y, UINT8 *dest_cb, UINT8 *dest_cr,
                              int dir, UINT8 **ref_picture, 
                              op_pixels_func *pix_op, qpel_mc_func *qpix_op)
{
    int dxy, offset, mx, my, src_x, src_y, motion_x, motion_y;
    int mb_x, mb_y, i;
    UINT8 *ptr, *dest;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

    switch(s->mv_type) {
    case MV_TYPE_16X16:
        if(s->mcsel){
#if 0
            mpeg_motion(s, dest_y, dest_cb, dest_cr, 0,
                        ref_picture, 0,
                        0, pix_op,
                        s->sprite_offset[0][0]>>3,
                        s->sprite_offset[0][1]>>3,
                        16);
#else
            gmc1_motion(s, dest_y, dest_cb, dest_cr, 0,
                        ref_picture, 0,
                        16);
#endif
        }else if(s->quarter_sample && dir==0){ //FIXME
            qpel_motion(s, dest_y, dest_cb, dest_cr, 0,
                        ref_picture, 0,
                        0, pix_op, qpix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16);
        }else{
            mpeg_motion(s, dest_y, dest_cb, dest_cr, 0,
                        ref_picture, 0,
                        0, pix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 16);
        }           
        break;
    case MV_TYPE_8X8:
        for(i=0;i<4;i++) {
            motion_x = s->mv[dir][i][0];
            motion_y = s->mv[dir][i][1];

            dxy = ((motion_y & 1) << 1) | (motion_x & 1);
            src_x = mb_x * 16 + (motion_x >> 1) + (i & 1) * 8;
            src_y = mb_y * 16 + (motion_y >> 1) + (i >>1) * 8;
                    
            /* WARNING: do no forget half pels */
            src_x = clip(src_x, -16, s->width);
            if (src_x == s->width)
                dxy &= ~1;
            src_y = clip(src_y, -16, s->height);
            if (src_y == s->height)
                dxy &= ~2;
                    
            ptr = ref_picture[0] + (src_y * s->linesize) + (src_x);
            dest = dest_y + ((i & 1) * 8) + (i >> 1) * 8 * s->linesize;
            pix_op[dxy](dest, ptr, s->linesize, 8);
        }
        /* In case of 8X8, we construct a single chroma motion vector
           with a special rounding */
        mx = 0;
        my = 0;
        for(i=0;i<4;i++) {
            mx += s->mv[dir][i][0];
            my += s->mv[dir][i][1];
        }
        if (mx >= 0)
            mx = (h263_chroma_roundtab[mx & 0xf] + ((mx >> 3) & ~1));
        else {
            mx = -mx;
            mx = -(h263_chroma_roundtab[mx & 0xf] + ((mx >> 3) & ~1));
        }
        if (my >= 0)
            my = (h263_chroma_roundtab[my & 0xf] + ((my >> 3) & ~1));
        else {
            my = -my;
            my = -(h263_chroma_roundtab[my & 0xf] + ((my >> 3) & ~1));
        }
        dxy = ((my & 1) << 1) | (mx & 1);
        mx >>= 1;
        my >>= 1;

        src_x = mb_x * 8 + mx;
        src_y = mb_y * 8 + my;
        src_x = clip(src_x, -8, s->width/2);
        if (src_x == s->width/2)
            dxy &= ~1;
        src_y = clip(src_y, -8, s->height/2);
        if (src_y == s->height/2)
            dxy &= ~2;
        
        offset = (src_y * (s->linesize >> 1)) + src_x;
        ptr = ref_picture[1] + offset;
        pix_op[dxy](dest_cb, ptr, s->linesize >> 1, 8);
        ptr = ref_picture[2] + offset;
        pix_op[dxy](dest_cr, ptr, s->linesize >> 1, 8);
        break;
    case MV_TYPE_FIELD:
        if (s->picture_structure == PICT_FRAME) {
            /* top field */
            mpeg_motion(s, dest_y, dest_cb, dest_cr, 0,
                        ref_picture, s->field_select[dir][0] ? s->linesize : 0,
                        1, pix_op,
                        s->mv[dir][0][0], s->mv[dir][0][1], 8);
            /* bottom field */
            mpeg_motion(s, dest_y, dest_cb, dest_cr, s->linesize,
                        ref_picture, s->field_select[dir][1] ? s->linesize : 0,
                        1, pix_op,
                        s->mv[dir][1][0], s->mv[dir][1][1], 8);
        } else {
            

        }
        break;
    }
}


/* put block[] to dest[] */
static inline void put_dct(MpegEncContext *s, 
                           DCTELEM *block, int i, UINT8 *dest, int line_size)
{
    if (!s->mpeg2)
        s->dct_unquantize(s, block, i, s->qscale);
    ff_idct (block);
    put_pixels_clamped(block, dest, line_size);
}

/* add block[] to dest[] */
static inline void add_dct(MpegEncContext *s, 
                           DCTELEM *block, int i, UINT8 *dest, int line_size)
{
    /* skip dequant / idct if we are really late ;) */
    if(s->hurry_up>1) return;

    if (s->block_last_index[i] >= 0) {
        if (!s->mpeg2)
            if(s->encoding || (!s->h263_msmpeg4))
                s->dct_unquantize(s, block, i, s->qscale);

        ff_idct (block);
        add_pixels_clamped(block, dest, line_size);
    }
}

/* generic function called after a macroblock has been parsed by the
   decoder or after it has been encoded by the encoder.

   Important variables used:
   s->mb_intra : true if intra macroblock
   s->mv_dir   : motion vector direction
   s->mv_type  : motion vector type
   s->mv       : motion vector
   s->interlaced_dct : true if interlaced dct used (mpeg2)
 */
void MPV_decode_mb(MpegEncContext *s, DCTELEM block[6][64])
{
    int mb_x, mb_y;
    int dct_linesize, dct_offset;
    op_pixels_func *op_pix;
    qpel_mc_func *op_qpix;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

#ifdef FF_POSTPROCESS
    quant_store[mb_y][mb_x]=s->qscale;
    //printf("[%02d][%02d] %d\n",mb_x,mb_y,s->qscale);
#endif

    /* update DC predictors for P macroblocks */
    if (!s->mb_intra) {
        if (s->h263_pred || s->h263_aic) {
          if(s->mbintra_table[mb_x + mb_y*s->mb_width])
          {
            int wrap, xy, v;
            s->mbintra_table[mb_x + mb_y*s->mb_width]=0;
            wrap = 2 * s->mb_width + 2;
            xy = 2 * mb_x + 1 +  (2 * mb_y + 1) * wrap;
            v = 1024;
            
            s->dc_val[0][xy] = v;
            s->dc_val[0][xy + 1] = v;
            s->dc_val[0][xy + wrap] = v;
            s->dc_val[0][xy + 1 + wrap] = v;
            /* ac pred */
            memset(s->ac_val[0][xy], 0, 16 * sizeof(INT16));
            memset(s->ac_val[0][xy + 1], 0, 16 * sizeof(INT16));
            memset(s->ac_val[0][xy + wrap], 0, 16 * sizeof(INT16));
            memset(s->ac_val[0][xy + 1 + wrap], 0, 16 * sizeof(INT16));
            if (s->h263_msmpeg4) {
                s->coded_block[xy] = 0;
                s->coded_block[xy + 1] = 0;
                s->coded_block[xy + wrap] = 0;
                s->coded_block[xy + 1 + wrap] = 0;
            }
            /* chroma */
            wrap = s->mb_width + 2;
            xy = mb_x + 1 + (mb_y + 1) * wrap;
            s->dc_val[1][xy] = v;
            s->dc_val[2][xy] = v;
            /* ac pred */
            memset(s->ac_val[1][xy], 0, 16 * sizeof(INT16));
            memset(s->ac_val[2][xy], 0, 16 * sizeof(INT16));
          }
        } else {
            s->last_dc[0] = 128 << s->intra_dc_precision;
            s->last_dc[1] = 128 << s->intra_dc_precision;
            s->last_dc[2] = 128 << s->intra_dc_precision;
        }
    }
    else if (s->h263_pred || s->h263_aic)
        s->mbintra_table[mb_x + mb_y*s->mb_width]=1;

    /* update motion predictor, not for B-frames as they need the motion_val from the last P/S-Frame */
    if (s->out_format == FMT_H263) { //FIXME move into h263.c if possible, format specific stuff shouldnt be here
      if(s->pict_type!=B_TYPE){
        int xy, wrap, motion_x, motion_y;
        
        wrap = 2 * s->mb_width + 2;
        xy = 2 * mb_x + 1 + (2 * mb_y + 1) * wrap;
        if (s->mb_intra) {
            motion_x = 0;
            motion_y = 0;
            goto motion_init;
        } else if (s->mv_type == MV_TYPE_16X16) {
            motion_x = s->mv[0][0][0];
            motion_y = s->mv[0][0][1];
        motion_init:
            /* no update if 8X8 because it has been done during parsing */
            s->motion_val[xy][0] = motion_x;
            s->motion_val[xy][1] = motion_y;
            s->motion_val[xy + 1][0] = motion_x;
            s->motion_val[xy + 1][1] = motion_y;
            s->motion_val[xy + wrap][0] = motion_x;
            s->motion_val[xy + wrap][1] = motion_y;
            s->motion_val[xy + 1 + wrap][0] = motion_x;
            s->motion_val[xy + 1 + wrap][1] = motion_y;
        }
      }
    }
    
    if (!(s->encoding && (s->intra_only || s->pict_type==B_TYPE))) {
        UINT8 *dest_y, *dest_cb, *dest_cr;
        UINT8 *mbskip_ptr;

        /* avoid copy if macroblock skipped in last frame too 
           dont touch it for B-frames as they need the skip info from the next p-frame */
        if (s->pict_type != B_TYPE) {
            mbskip_ptr = &s->mbskip_table[s->mb_y * s->mb_width + s->mb_x];
            if (s->mb_skiped) {
                s->mb_skiped = 0;
                /* if previous was skipped too, then nothing to do ! 
                   skip only during decoding as we might trash the buffers during encoding a bit */
                if (*mbskip_ptr != 0 && !s->encoding) 
                    goto the_end;
                *mbskip_ptr = 1; /* indicate that this time we skiped it */
            } else {
                *mbskip_ptr = 0; /* not skipped */
            }
        }

        dest_y = s->current_picture[0] + (mb_y * 16 * s->linesize) + mb_x * 16;
        dest_cb = s->current_picture[1] + (mb_y * 8 * (s->linesize >> 1)) + mb_x * 8;
        dest_cr = s->current_picture[2] + (mb_y * 8 * (s->linesize >> 1)) + mb_x * 8;

        if (s->interlaced_dct) {
            dct_linesize = s->linesize * 2;
            dct_offset = s->linesize;
        } else {
            dct_linesize = s->linesize;
            dct_offset = s->linesize * 8;
        }

        if (!s->mb_intra) {
            /* motion handling */
            if((s->flags&CODEC_FLAG_HQ) || (!s->encoding)){
                if ((!s->no_rounding) || s->pict_type==B_TYPE){                
                    op_pix = put_pixels_tab;
                    op_qpix= qpel_mc_rnd_tab;
                }else{
                    op_pix = put_no_rnd_pixels_tab;
                    op_qpix= qpel_mc_no_rnd_tab;
                }

                if (s->mv_dir & MV_DIR_FORWARD) {
                    MPV_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_picture, op_pix, op_qpix);
                    if ((!s->no_rounding) || s->pict_type==B_TYPE)
                        op_pix = avg_pixels_tab;
                    else
                        op_pix = avg_no_rnd_pixels_tab;
                }
                if (s->mv_dir & MV_DIR_BACKWARD) {
                    MPV_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_picture, op_pix, op_qpix);
                }
            }

            /* add dct residue */
            add_dct(s, block[0], 0, dest_y, dct_linesize);
            add_dct(s, block[1], 1, dest_y + 8, dct_linesize);
            add_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize);
            add_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize);

            add_dct(s, block[4], 4, dest_cb, s->linesize >> 1);
            add_dct(s, block[5], 5, dest_cr, s->linesize >> 1);
        } else {
            /* dct only in intra block */
            put_dct(s, block[0], 0, dest_y, dct_linesize);
            put_dct(s, block[1], 1, dest_y + 8, dct_linesize);
            put_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize);
            put_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize);

            put_dct(s, block[4], 4, dest_cb, s->linesize >> 1);
            put_dct(s, block[5], 5, dest_cr, s->linesize >> 1);
        }
    }
 the_end:
    emms_c(); //FIXME remove
}

static inline void clip_coeffs(MpegEncContext *s, DCTELEM *block, int last_index)
{
    int i;
    const int maxlevel= s->max_qcoeff;
    const int minlevel= s->min_qcoeff;
        
    for(i=0;i<=last_index; i++){
        const int j = zigzag_direct[i];
        int level = block[j];
       
        if     (level>maxlevel) level=maxlevel;
        else if(level<minlevel) level=minlevel;
        block[j]= level;
    }
}

static void encode_mb(MpegEncContext *s, int motion_x, int motion_y)
{
    const int mb_x= s->mb_x;
    const int mb_y= s->mb_y;
    int i;
#if 0
        if (s->interlaced_dct) {
            dct_linesize = s->linesize * 2;
            dct_offset = s->linesize;
        } else {
            dct_linesize = s->linesize;
            dct_offset = s->linesize * 8;
        }
#endif

    if (s->mb_intra) {
        UINT8 *ptr;
        int wrap;

        wrap = s->linesize;
        ptr = s->new_picture[0] + (mb_y * 16 * wrap) + mb_x * 16;
        get_pixels(s->block[0], ptr               , wrap);
        get_pixels(s->block[1], ptr            + 8, wrap);
        get_pixels(s->block[2], ptr + 8 * wrap    , wrap);
        get_pixels(s->block[3], ptr + 8 * wrap + 8, wrap);

        wrap >>=1;
        ptr = s->new_picture[1] + (mb_y * 8 * wrap) + mb_x * 8;
        get_pixels(s->block[4], ptr, wrap);

        ptr = s->new_picture[2] + (mb_y * 8 * wrap) + mb_x * 8;
        get_pixels(s->block[5], ptr, wrap);
    }else{
        op_pixels_func *op_pix;
        qpel_mc_func *op_qpix;
        UINT8 *dest_y, *dest_cb, *dest_cr;
        UINT8 *ptr;
        int wrap;

        dest_y  = s->current_picture[0] + (mb_y * 16 * s->linesize       ) + mb_x * 16;
        dest_cb = s->current_picture[1] + (mb_y * 8  * (s->linesize >> 1)) + mb_x * 8;
        dest_cr = s->current_picture[2] + (mb_y * 8  * (s->linesize >> 1)) + mb_x * 8;

        if ((!s->no_rounding) || s->pict_type==B_TYPE){
            op_pix = put_pixels_tab;
            op_qpix= qpel_mc_rnd_tab;
        }else{
            op_pix = put_no_rnd_pixels_tab;
            op_qpix= qpel_mc_no_rnd_tab;
        }

        if (s->mv_dir & MV_DIR_FORWARD) {
            MPV_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_picture, op_pix, op_qpix);
           if ((!s->no_rounding) || s->pict_type==B_TYPE)
                op_pix = avg_pixels_tab;
            else
                op_pix = avg_no_rnd_pixels_tab;
        }
        if (s->mv_dir & MV_DIR_BACKWARD) {
            MPV_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_picture, op_pix, op_qpix);
        }
        wrap = s->linesize;
        ptr = s->new_picture[0] + (mb_y * 16 * wrap) + mb_x * 16;
        diff_pixels(s->block[0], ptr               , dest_y               , wrap);
        diff_pixels(s->block[1], ptr            + 8, dest_y            + 8, wrap);
        diff_pixels(s->block[2], ptr + 8 * wrap    , dest_y + 8 * wrap    , wrap);
        diff_pixels(s->block[3], ptr + 8 * wrap + 8, dest_y + 8 * wrap + 8, wrap);

        wrap >>=1;
        ptr = s->new_picture[1] + (mb_y * 8 * wrap) + mb_x * 8;
        diff_pixels(s->block[4], ptr, dest_cb, wrap);

        ptr = s->new_picture[2] + (mb_y * 8 * wrap) + mb_x * 8;
        diff_pixels(s->block[5], ptr, dest_cr, wrap);
    }
            
#if 0
            {
                float adap_parm;
                
                adap_parm = ((s->avg_mb_var << 1) + s->mb_var[s->mb_width*mb_y+mb_x] + 1.0) /
                            ((s->mb_var[s->mb_width*mb_y+mb_x] << 1) + s->avg_mb_var + 1.0);
            
                printf("\ntype=%c qscale=%2d adap=%0.2f dquant=%4.2f var=%4d avgvar=%4d", 
                        (s->mb_type[s->mb_width*mb_y+mb_x] > 0) ? 'I' : 'P', 
                        s->qscale, adap_parm, s->qscale*adap_parm,
                        s->mb_var[s->mb_width*mb_y+mb_x], s->avg_mb_var);
            }
#endif
    /* DCT & quantize */
    if (s->h263_pred && s->msmpeg4_version!=2) {
        h263_dc_scale(s);
    } else if (s->h263_aic) {
        s->y_dc_scale = 2*s->qscale;
        s->c_dc_scale = 2*s->qscale;
    } else {
        /* default quantization values */
        s->y_dc_scale = 8;
        s->c_dc_scale = 8;
    }
    if(s->out_format==FMT_MJPEG){
        for(i=0;i<6;i++) {
            int overflow;
            s->block_last_index[i] = dct_quantize(s, s->block[i], i, 8, &overflow);
            if (overflow) clip_coeffs(s, s->block[i], s->block_last_index[i]);
        }
    }else{
        for(i=0;i<6;i++) {
            int overflow;
            s->block_last_index[i] = dct_quantize(s, s->block[i], i, s->qscale, &overflow);
            // FIXME we could decide to change to quantizer instead of clipping
            // JS: I don't think that would be a good idea it could lower quality instead
            //     of improve it. Just INTRADC clipping deserves changes in quantizer
            if (overflow) clip_coeffs(s, s->block[i], s->block_last_index[i]);
        }
    }

    /* huffman encode */
    switch(s->out_format) {
    case FMT_MPEG1:
        mpeg1_encode_mb(s, s->block, motion_x, motion_y);
        break;
    case FMT_H263:
        if (s->h263_msmpeg4)
            msmpeg4_encode_mb(s, s->block, motion_x, motion_y);
        else if(s->h263_pred)
            mpeg4_encode_mb(s, s->block, motion_x, motion_y);
        else
            h263_encode_mb(s, s->block, motion_x, motion_y);
        break;
    case FMT_MJPEG:
        mjpeg_encode_mb(s, s->block);
        break;
    }
}

static void copy_bits(PutBitContext *pb, UINT8 *src, int length)
{
#if 1
    int bytes= length>>4;
    int bits= length&15;
    int i;

    for(i=0; i<bytes; i++) put_bits(pb, 16, be2me_16(((uint16_t*)src)[i]));
    put_bits(pb, bits, be2me_16(((uint16_t*)src)[i])>>(16-bits));
#else
    int bytes= length>>3;
    int bits= length&7;
    int i;

    for(i=0; i<bytes; i++) put_bits(pb, 8, src[i]);
    put_bits(pb, bits, src[i]>>(8-bits));
#endif
}

static void copy_context_before_encode(MpegEncContext *d, MpegEncContext *s, int type){
    int i;

    memcpy(d->last_mv, s->last_mv, 2*2*2*sizeof(int)); //FIXME is memcpy faster then a loop?

    /* mpeg1 */
    d->mb_incr= s->mb_incr;
    for(i=0; i<3; i++)
        d->last_dc[i]= s->last_dc[i];
    
    /* statistics */
    d->mv_bits= s->mv_bits;
    d->i_tex_bits= s->i_tex_bits;
    d->p_tex_bits= s->p_tex_bits;
    d->i_count= s->i_count;
    d->p_count= s->p_count;
    d->skip_count= s->skip_count;
    d->misc_bits= s->misc_bits;
    d->last_bits= 0;

    d->mb_skiped= s->mb_skiped;
}

static void copy_context_after_encode(MpegEncContext *d, MpegEncContext *s, int type){
    int i;

    memcpy(d->mv, s->mv, 2*4*2*sizeof(int)); 
    memcpy(d->last_mv, s->last_mv, 2*2*2*sizeof(int)); //FIXME is memcpy faster then a loop?
    
    /* mpeg1 */
    d->mb_incr= s->mb_incr;
    for(i=0; i<3; i++)
        d->last_dc[i]= s->last_dc[i];
    
    /* statistics */
    d->mv_bits= s->mv_bits;
    d->i_tex_bits= s->i_tex_bits;
    d->p_tex_bits= s->p_tex_bits;
    d->i_count= s->i_count;
    d->p_count= s->p_count;
    d->skip_count= s->skip_count;
    d->misc_bits= s->misc_bits;

    d->mb_intra= s->mb_intra;
    d->mb_skiped= s->mb_skiped;
    d->mv_type= s->mv_type;
    d->mv_dir= s->mv_dir;
    d->pb= s->pb;
    d->block= s->block;
    for(i=0; i<6; i++)
        d->block_last_index[i]= s->block_last_index[i];
}


static void encode_picture(MpegEncContext *s, int picture_number)
{
    int mb_x, mb_y, last_gob, pdif = 0;
    int i;
    int bits;
    MpegEncContext best_s, backup_s;
    UINT8 bit_buf[7][3000]; //FIXME check that this is ALLWAYS large enogh for a MB

    s->picture_number = picture_number;

    s->block_wrap[0]=
    s->block_wrap[1]=
    s->block_wrap[2]=
    s->block_wrap[3]= s->mb_width*2 + 2;
    s->block_wrap[4]=
    s->block_wrap[5]= s->mb_width + 2;
    
    /* Reset the average MB variance */
    s->avg_mb_var = 0;
    s->mc_mb_var = 0;

    /* we need to initialize some time vars before we can encode b-frames */
    if (s->h263_pred && !s->h263_msmpeg4)
        ff_set_mpeg4_time(s, s->picture_number); 

    /* Estimate motion for every MB */
    if(s->pict_type != I_TYPE){
//        int16_t (*tmp)[2]= s->p_mv_table;
//        s->p_mv_table= s->last_mv_table;
//        s->last_mv_table= s->mv_table;
    
        for(mb_y=0; mb_y < s->mb_height; mb_y++) {
            s->block_index[0]= s->block_wrap[0]*(mb_y*2 + 1) - 1;
            s->block_index[1]= s->block_wrap[0]*(mb_y*2 + 1);
            s->block_index[2]= s->block_wrap[0]*(mb_y*2 + 2) - 1;
            s->block_index[3]= s->block_wrap[0]*(mb_y*2 + 2);
            for(mb_x=0; mb_x < s->mb_width; mb_x++) {
                s->mb_x = mb_x;
                s->mb_y = mb_y;
                s->block_index[0]+=2;
                s->block_index[1]+=2;
                s->block_index[2]+=2;
                s->block_index[3]+=2;

                /* compute motion vector & mb_type and store in context */
                if(s->pict_type==B_TYPE)
                    ff_estimate_b_frame_motion(s, mb_x, mb_y);
                else
                    ff_estimate_p_frame_motion(s, mb_x, mb_y);
//                s->mb_type[mb_y*s->mb_width + mb_x]=MB_TYPE_INTER;
            }
        }
        emms_c();
    }else if(s->pict_type == I_TYPE){
        /* I-Frame */
        //FIXME do we need to zero them?
        memset(s->motion_val[0], 0, sizeof(INT16)*(s->mb_width*2 + 2)*(s->mb_height*2 + 2)*2);
        memset(s->p_mv_table   , 0, sizeof(INT16)*(s->mb_width+2)*(s->mb_height+2)*2);
        memset(s->mb_type      , MB_TYPE_INTRA, sizeof(UINT8)*s->mb_width*s->mb_height);
    }

    if(s->avg_mb_var < s->mc_mb_var && s->pict_type == P_TYPE){ //FIXME subtract MV bits
        s->pict_type= I_TYPE;
        memset(s->mb_type   , MB_TYPE_INTRA, sizeof(UINT8)*s->mb_width*s->mb_height);
        if(s->max_b_frames==0){
            s->input_pict_type= I_TYPE;
            s->input_picture_in_gop_number=0;
        }
//printf("Scene change detected, encoding as I Frame\n");
    }
    
    if(s->pict_type==P_TYPE || s->pict_type==S_TYPE) 
        s->f_code= ff_get_best_fcode(s, s->p_mv_table, MB_TYPE_INTER);
        ff_fix_long_p_mvs(s);
    if(s->pict_type==B_TYPE){
        s->f_code= ff_get_best_fcode(s, s->b_forw_mv_table, MB_TYPE_FORWARD);
        s->b_code= ff_get_best_fcode(s, s->b_back_mv_table, MB_TYPE_BACKWARD);

        ff_fix_long_b_mvs(s, s->b_forw_mv_table, s->f_code, MB_TYPE_FORWARD);
        ff_fix_long_b_mvs(s, s->b_back_mv_table, s->b_code, MB_TYPE_BACKWARD);
        ff_fix_long_b_mvs(s, s->b_bidir_forw_mv_table, s->f_code, MB_TYPE_BIDIR);
        ff_fix_long_b_mvs(s, s->b_bidir_back_mv_table, s->b_code, MB_TYPE_BIDIR);
    }
    
//printf("f_code %d ///\n", s->f_code);

//    printf("%d %d\n", s->avg_mb_var, s->mc_mb_var);

    if(s->flags&CODEC_FLAG_PASS2)
        s->qscale = ff_rate_estimate_qscale_pass2(s);
    else if (!s->fixed_qscale) 
        s->qscale = ff_rate_estimate_qscale(s);

    if (s->out_format == FMT_MJPEG) {
        /* for mjpeg, we do include qscale in the matrix */
        s->intra_matrix[0] = default_intra_matrix[0];
        for(i=1;i<64;i++)
            s->intra_matrix[i] = CLAMP_TO_8BIT((default_intra_matrix[i] * s->qscale) >> 3);
        convert_matrix(s->q_intra_matrix, s->q_intra_matrix16, 
                       s->q_intra_matrix16_bias, s->intra_matrix, s->intra_quant_bias);
    }

    s->last_bits= get_bit_count(&s->pb);
    switch(s->out_format) {
    case FMT_MJPEG:
        mjpeg_picture_header(s);
        break;
    case FMT_H263:
        if (s->h263_msmpeg4) 
            msmpeg4_encode_picture_header(s, picture_number);
        else if (s->h263_pred)
            mpeg4_encode_picture_header(s, picture_number);
        else if (s->h263_rv10) 
            rv10_encode_picture_header(s, picture_number);
        else
            h263_encode_picture_header(s, picture_number);
        break;
    case FMT_MPEG1:
        mpeg1_encode_picture_header(s, picture_number);
        break;
    }
    bits= get_bit_count(&s->pb);
    s->header_bits= bits - s->last_bits;
    s->last_bits= bits;
    s->mv_bits=0;
    s->misc_bits=0;
    s->i_tex_bits=0;
    s->p_tex_bits=0;
    s->i_count=0;
    s->p_count=0;
    s->skip_count=0;

    /* init last dc values */
    /* note: quant matrix value (8) is implied here */
    s->last_dc[0] = 128;
    s->last_dc[1] = 128;
    s->last_dc[2] = 128;
    s->mb_incr = 1;
    s->last_mv[0][0][0] = 0;
    s->last_mv[0][0][1] = 0;

    /* Get the GOB height based on picture height */
    if (s->out_format == FMT_H263 && !s->h263_pred && !s->h263_msmpeg4) {
        if (s->height <= 400)
            s->gob_index = 1;
        else if (s->height <= 800)
            s->gob_index = 2;
        else
            s->gob_index = 4;
    }
        
    s->avg_mb_var = s->avg_mb_var / s->mb_num;        
    
    for(mb_y=0; mb_y < s->mb_height; mb_y++) {
        /* Put GOB header based on RTP MTU */
        /* TODO: Put all this stuff in a separate generic function */
        if (s->rtp_mode) {
            if (!mb_y) {
                s->ptr_lastgob = s->pb.buf;
                s->ptr_last_mb_line = s->pb.buf;
            } else if (s->out_format == FMT_H263 && !s->h263_pred && !s->h263_msmpeg4 && !(mb_y % s->gob_index)) {
                last_gob = h263_encode_gob_header(s, mb_y);
                if (last_gob) {
                    s->first_gob_line = 1;
                }
            }
        }
        
        s->block_index[0]= s->block_wrap[0]*(mb_y*2 + 1) - 1;
        s->block_index[1]= s->block_wrap[0]*(mb_y*2 + 1);
        s->block_index[2]= s->block_wrap[0]*(mb_y*2 + 2) - 1;
        s->block_index[3]= s->block_wrap[0]*(mb_y*2 + 2);
        s->block_index[4]= s->block_wrap[4]*(mb_y + 1)                    + s->block_wrap[0]*(s->mb_height*2 + 2);
        s->block_index[5]= s->block_wrap[4]*(mb_y + 1 + s->mb_height + 2) + s->block_wrap[0]*(s->mb_height*2 + 2);
        for(mb_x=0; mb_x < s->mb_width; mb_x++) {
            const int mb_type= s->mb_type[mb_y * s->mb_width + mb_x];
            const int xy= (mb_y+1) * (s->mb_width+2) + mb_x + 1;
            PutBitContext pb;
            int d;
            int dmin=10000000;
            int best=0;

            s->mb_x = mb_x;
            s->mb_y = mb_y;
            s->block_index[0]+=2;
            s->block_index[1]+=2;
            s->block_index[2]+=2;
            s->block_index[3]+=2;
            s->block_index[4]++;
            s->block_index[5]++;
            if(mb_type & (mb_type-1)){ // more than 1 MB type possible
                int next_block=0;
                pb= s->pb;

                copy_context_before_encode(&backup_s, s, -1);

                if(mb_type&MB_TYPE_INTER){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->p_mv_table[xy][0];
                    s->mv[0][0][1] = s->p_mv_table[xy][1];
                    init_put_bits(&s->pb, bit_buf[1], 3000, NULL, NULL);
                    s->block= s->blocks[next_block];
                    s->last_bits= 0; //done in copy_context_before_encode but we skip that here

                    encode_mb(s, s->mv[0][0][0], s->mv[0][0][1]);
                    d= get_bit_count(&s->pb);
                    if(d<dmin){
                        flush_put_bits(&s->pb);
                        dmin=d;
                        copy_context_after_encode(&best_s, s, MB_TYPE_INTER);
                        best=1;
                        next_block^=1;
                    }
                }
                if(mb_type&MB_TYPE_INTER4V){                 
                    copy_context_before_encode(s, &backup_s, MB_TYPE_INTER4V);
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_8X8;
                    s->mb_intra= 0;
                    for(i=0; i<4; i++){
                        s->mv[0][i][0] = s->motion_val[s->block_index[i]][0];
                        s->mv[0][i][1] = s->motion_val[s->block_index[i]][1];
                    }
                    init_put_bits(&s->pb, bit_buf[2], 3000, NULL, NULL);
                    s->block= s->blocks[next_block];

                    encode_mb(s, 0, 0);
                    d= get_bit_count(&s->pb);
                    if(d<dmin){
                        flush_put_bits(&s->pb);
                        dmin=d;
                        copy_context_after_encode(&best_s, s, MB_TYPE_INTER4V);
                        best=2;
                        next_block^=1;
                    }
                }
                if(mb_type&MB_TYPE_FORWARD){
                    copy_context_before_encode(s, &backup_s, MB_TYPE_FORWARD);
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->b_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_forw_mv_table[xy][1];
                    init_put_bits(&s->pb, bit_buf[3], 3000, NULL, NULL);
                    s->block= s->blocks[next_block];

                    encode_mb(s, s->mv[0][0][0], s->mv[0][0][1]);
                    d= get_bit_count(&s->pb);
                    if(d<dmin){
                        flush_put_bits(&s->pb);
                        dmin=d;
                        copy_context_after_encode(&best_s, s, MB_TYPE_FORWARD);
                        best=3;
                        next_block^=1;
                    }
                }
                if(mb_type&MB_TYPE_BACKWARD){
                    copy_context_before_encode(s, &backup_s, MB_TYPE_BACKWARD);
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[1][0][0] = s->b_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_back_mv_table[xy][1];
                    init_put_bits(&s->pb, bit_buf[4], 3000, NULL, NULL);
                    s->block= s->blocks[next_block];

                    encode_mb(s, s->mv[1][0][0], s->mv[1][0][1]);
                    d= get_bit_count(&s->pb);
                    if(d<dmin){
                        flush_put_bits(&s->pb);
                        dmin=d;
                        copy_context_after_encode(&best_s, s, MB_TYPE_BACKWARD);
                        best=4;
                        next_block^=1;
                    }
                }
                if(mb_type&MB_TYPE_BIDIR){
                    copy_context_before_encode(s, &backup_s, MB_TYPE_BIDIR);
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->b_bidir_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_bidir_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_bidir_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_bidir_back_mv_table[xy][1];
                    init_put_bits(&s->pb, bit_buf[5], 3000, NULL, NULL);
                    s->block= s->blocks[next_block];

                    encode_mb(s, 0, 0);
                    d= get_bit_count(&s->pb);
                    if(d<dmin){
                        flush_put_bits(&s->pb);
                        dmin=d;
                        copy_context_after_encode(&best_s, s, MB_TYPE_BIDIR);
                        best=5;
                        next_block^=1;
                    }
                }
                if(mb_type&MB_TYPE_DIRECT){
                    copy_context_before_encode(s, &backup_s, MB_TYPE_DIRECT);
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
                    s->mv_type = MV_TYPE_16X16; //FIXME
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->b_direct_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_direct_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_direct_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_direct_back_mv_table[xy][1];
                    init_put_bits(&s->pb, bit_buf[6], 3000, NULL, NULL);
                    s->block= s->blocks[next_block];

                    encode_mb(s, s->b_direct_mv_table[xy][0], s->b_direct_mv_table[xy][1]);
                    d= get_bit_count(&s->pb);
                    if(d<dmin){
                        flush_put_bits(&s->pb);
                        dmin=d;
                        copy_context_after_encode(&best_s, s, MB_TYPE_DIRECT);
                        best=6;
                        next_block^=1;
                    }
                }
                if(mb_type&MB_TYPE_INTRA){
                    copy_context_before_encode(s, &backup_s, MB_TYPE_INTRA);
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 1;
                    s->mv[0][0][0] = 0;
                    s->mv[0][0][1] = 0;
                    init_put_bits(&s->pb, bit_buf[0], 3000, NULL, NULL);
                    s->block= s->blocks[next_block];
                   
                    encode_mb(s, 0, 0);
                    d= get_bit_count(&s->pb);
                    if(d<dmin){
                        flush_put_bits(&s->pb);
                        dmin=d;
                        copy_context_after_encode(&best_s, s, MB_TYPE_INTRA);
                        best=0;
                        next_block^=1;
                    }
                    /* force cleaning of ac/dc pred stuff if needed ... */
                    if(s->h263_pred || s->h263_aic)
                        s->mbintra_table[mb_x + mb_y*s->mb_width]=1;
                }
                copy_context_after_encode(s, &best_s, -1);
                copy_bits(&pb, bit_buf[best], dmin);
                s->pb= pb;
                s->last_bits= get_bit_count(&s->pb);
            } else {
                int motion_x, motion_y;
                s->mv_type=MV_TYPE_16X16;
                // only one MB-Type possible
                switch(mb_type){
                case MB_TYPE_INTRA:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mb_intra= 1;
                    motion_x= s->mv[0][0][0] = 0;
                    motion_y= s->mv[0][0][1] = 0;
                    break;
                case MB_TYPE_INTER:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[0][0][0] = s->p_mv_table[xy][0];
                    motion_y= s->mv[0][0][1] = s->p_mv_table[xy][1];
                    break;
                case MB_TYPE_DIRECT:
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
                    s->mb_intra= 0;
                    motion_x=s->b_direct_mv_table[xy][0];
                    motion_y=s->b_direct_mv_table[xy][1];
                    s->mv[0][0][0] = s->b_direct_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_direct_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_direct_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_direct_back_mv_table[xy][1];
                    break;
                case MB_TYPE_BIDIR:
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mb_intra= 0;
                    motion_x=0;
                    motion_y=0;
                    s->mv[0][0][0] = s->b_bidir_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_bidir_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_bidir_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_bidir_back_mv_table[xy][1];
                    break;
                case MB_TYPE_BACKWARD:
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[1][0][0] = s->b_back_mv_table[xy][0];
                    motion_y= s->mv[1][0][1] = s->b_back_mv_table[xy][1];
                    break;
                case MB_TYPE_FORWARD:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[0][0][0] = s->b_forw_mv_table[xy][0];
                    motion_y= s->mv[0][0][1] = s->b_forw_mv_table[xy][1];
//                    printf(" %d %d ", motion_x, motion_y);
                    break;
                default:
                    motion_x=motion_y=0; //gcc warning fix
                    printf("illegal MB type\n");
                }
                encode_mb(s, motion_x, motion_y);
            }
            /* clean the MV table in IPS frames for direct mode in B frames */
            if(s->mb_intra /* && I,P,S_TYPE */){
                s->p_mv_table[xy][0]=0;
                s->p_mv_table[xy][1]=0;
            }

            MPV_decode_mb(s, s->block);
        }


        /* Obtain average GOB size for RTP */
        if (s->rtp_mode) {
            if (!mb_y)
                s->mb_line_avgsize = pbBufPtr(&s->pb) - s->ptr_last_mb_line;
            else if (!(mb_y % s->gob_index)) {    
                s->mb_line_avgsize = (s->mb_line_avgsize + pbBufPtr(&s->pb) - s->ptr_last_mb_line) >> 1;
                s->ptr_last_mb_line = pbBufPtr(&s->pb);
            }
            //fprintf(stderr, "\nMB line: %d\tSize: %u\tAvg. Size: %u", s->mb_y, 
            //                    (s->pb.buf_ptr - s->ptr_last_mb_line), s->mb_line_avgsize);
            s->first_gob_line = 0;
        }
    }
    emms_c();

    if (s->h263_msmpeg4 && s->msmpeg4_version<4 && s->pict_type == I_TYPE)
        msmpeg4_encode_ext_header(s);

    //if (s->gob_number)
    //    fprintf(stderr,"\nNumber of GOB: %d", s->gob_number);
    
    /* Send the last GOB if RTP */    
    if (s->rtp_mode) {
        flush_put_bits(&s->pb);
        pdif = pbBufPtr(&s->pb) - s->ptr_lastgob;
        /* Call the RTP callback to send the last GOB */
        if (s->rtp_callback)
            s->rtp_callback(s->ptr_lastgob, pdif, s->gob_number);
        s->ptr_lastgob = pbBufPtr(&s->pb);
        //fprintf(stderr,"\nGOB: %2d size: %d (last)", s->gob_number, pdif);
    }
}

static int dct_quantize_c(MpegEncContext *s, 
                        DCTELEM *block, int n,
                        int qscale, int *overflow)
{
    int i, j, level, last_non_zero, q;
    const int *qmat;
    int bias;
    int max=0;
    unsigned int threshold1, threshold2;

    av_fdct (block);

    /* we need this permutation so that we correct the IDCT
       permutation. will be moved into DCT code */
    block_permute(block);

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
        block[0] = (block[0] + (q >> 1)) / q;
        i = 1;
        last_non_zero = 0;
        qmat = s->q_intra_matrix[qscale];
        bias= s->intra_quant_bias<<(QMAT_SHIFT - 3 - QUANT_BIAS_SHIFT);
    } else {
        i = 0;
        last_non_zero = -1;
        qmat = s->q_inter_matrix[qscale];
        bias= s->inter_quant_bias<<(QMAT_SHIFT - 3 - QUANT_BIAS_SHIFT);
    }
    threshold1= (1<<(QMAT_SHIFT - 3)) - bias - 1;
    threshold2= threshold1<<1;

    for(;i<64;i++) {
        j = zigzag_direct[i];
        level = block[j];
        level = level * qmat[j];

//        if(   bias+level >= (1<<(QMAT_SHIFT - 3))
//           || bias-level >= (1<<(QMAT_SHIFT - 3))){
        if(((unsigned)(level+threshold1))>threshold2){
            if(level>0){
                level= (bias + level)>>(QMAT_SHIFT - 3);
                block[j]= level;
            }else{
                level= (bias - level)>>(QMAT_SHIFT - 3);
                block[j]= -level;
            }
            max |=level;
            last_non_zero = i;
        }else{
            block[j]=0;
        }
    }
    *overflow= s->max_qcoeff < max; //overflow might have happend
    
    return last_non_zero;
}

static void dct_unquantize_mpeg1_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const UINT16 *quant_matrix;

    if(s->alternate_scan) nCoeffs= 64;
    else nCoeffs= s->block_last_index[n]+1;
    
    if (s->mb_intra) {
        if (n < 4) 
            block[0] = block[0] * s->y_dc_scale;
        else
            block[0] = block[0] * s->c_dc_scale;
        /* XXX: only mpeg1 */
        quant_matrix = s->intra_matrix;
        for(i=1;i<nCoeffs;i++) {
            int j= zigzag_direct[i];
            level = block[j];
            if (level) {
                if (level < 0) {
                    level = -level;
                    level = (int)(level * qscale * quant_matrix[j]) >> 3;
                    level = (level - 1) | 1;
                    level = -level;
                } else {
                    level = (int)(level * qscale * quant_matrix[j]) >> 3;
                    level = (level - 1) | 1;
                }
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
                block[j] = level;
            }
        }
    } else {
        i = 0;
        quant_matrix = s->inter_matrix;
        for(;i<nCoeffs;i++) {
            int j= zigzag_direct[i];
            level = block[j];
            if (level) {
                if (level < 0) {
                    level = -level;
                    level = (((level << 1) + 1) * qscale *
                             ((int) (quant_matrix[j]))) >> 4;
                    level = (level - 1) | 1;
                    level = -level;
                } else {
                    level = (((level << 1) + 1) * qscale *
                             ((int) (quant_matrix[j]))) >> 4;
                    level = (level - 1) | 1;
                }
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
                block[j] = level;
            }
        }
    }
}

static void dct_unquantize_mpeg2_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const UINT16 *quant_matrix;

    if(s->alternate_scan) nCoeffs= 64;
    else nCoeffs= s->block_last_index[n]+1;
    
    if (s->mb_intra) {
        if (n < 4) 
            block[0] = block[0] * s->y_dc_scale;
        else
            block[0] = block[0] * s->c_dc_scale;
        quant_matrix = s->intra_matrix;
        for(i=1;i<nCoeffs;i++) {
            int j= zigzag_direct[i];
            level = block[j];
            if (level) {
                if (level < 0) {
                    level = -level;
                    level = (int)(level * qscale * quant_matrix[j]) >> 3;
                    level = -level;
                } else {
                    level = (int)(level * qscale * quant_matrix[j]) >> 3;
                }
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
                block[j] = level;
            }
        }
    } else {
        int sum=-1;
        i = 0;
        quant_matrix = s->inter_matrix;
        for(;i<nCoeffs;i++) {
            int j= zigzag_direct[i];
            level = block[j];
            if (level) {
                if (level < 0) {
                    level = -level;
                    level = (((level << 1) + 1) * qscale *
                             ((int) (quant_matrix[j]))) >> 4;
                    level = -level;
                } else {
                    level = (((level << 1) + 1) * qscale *
                             ((int) (quant_matrix[j]))) >> 4;
                }
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
                block[j] = level;
                sum+=level;
            }
        }
        block[63]^=sum&1;
    }
}


static void dct_unquantize_h263_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale)
{
    int i, level, qmul, qadd;
    int nCoeffs;
    
    if (s->mb_intra) {
        if (!s->h263_aic) {
            if (n < 4) 
                block[0] = block[0] * s->y_dc_scale;
            else
                block[0] = block[0] * s->c_dc_scale;
        }
        i = 1;
        nCoeffs= 64; //does not allways use zigzag table 
    } else {
        i = 0;
        nCoeffs= zigzag_end[ s->block_last_index[n] ];
    }

    qmul = s->qscale << 1;
    if (s->h263_aic && s->mb_intra)
        qadd = 0;
    else
        qadd = (s->qscale - 1) | 1;

    for(;i<nCoeffs;i++) {
        level = block[i];
        if (level) {
            if (level < 0) {
                level = level * qmul - qadd;
            } else {
                level = level * qmul + qadd;
            }
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
            block[i] = level;
        }
    }
}

AVCodec mpeg1video_encoder = {
    "mpeg1video",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG1VIDEO,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec h263_encoder = {
    "h263",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H263,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec h263p_encoder = {
    "h263p",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H263P,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec rv10_encoder = {
    "rv10",
    CODEC_TYPE_VIDEO,
    CODEC_ID_RV10,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec mjpeg_encoder = {
    "mjpeg",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MJPEG,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec mpeg4_encoder = {
    "mpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG4,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec msmpeg4v1_encoder = {
    "msmpeg4v1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V1,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec msmpeg4v2_encoder = {
    "msmpeg4v2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V2,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVCodec msmpeg4v3_encoder = {
    "msmpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4V3,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};
