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
 */
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif

static void encode_picture(MpegEncContext *s, int picture_number);
static void rate_control_init(MpegEncContext *s);
static int rate_estimate_qscale(MpegEncContext *s);
static void dct_unquantize_mpeg1_c(MpegEncContext *s, 
                                   DCTELEM *block, int n, int qscale);
static void dct_unquantize_h263_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale);
static void draw_edges_c(UINT8 *buf, int wrap, int width, int height, int w);
static int dct_quantize_c(MpegEncContext *s, DCTELEM *block, int n, int qscale);

int (*dct_quantize)(MpegEncContext *s, DCTELEM *block, int n, int qscale)= dct_quantize_c;
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

static UINT16 default_mv_penalty[MAX_FCODE][MAX_MV*2+1];
static UINT8 default_fcode_tab[MAX_MV*2+1];

/* default motion estimation */
int motion_estimation_method = ME_LOG;

extern UINT8 zigzag_end[64];

static void convert_matrix(int *qmat, UINT16 *qmat16, const UINT16 *quant_matrix, int qscale)
{
    int i;

    if (av_fdct == jpeg_fdct_ifast) {
        for(i=0;i<64;i++) {
            /* 16 <= qscale * quant_matrix[i] <= 7905 */
            /* 19952         <= aanscales[i] * qscale * quant_matrix[i]           <= 249205026 */
            /* (1<<36)/19952 >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= (1<<36)/249205026 */
            /* 3444240       >= (1<<36)/(aanscales[i] * qscale * quant_matrix[i]) >= 275 */
            
            qmat[block_permute_op(i)] = (int)((UINT64_C(1) << (QMAT_SHIFT + 11)) / 
                            (aanscales[i] * qscale * quant_matrix[block_permute_op(i)]));
        }
    } else {
        for(i=0;i<64;i++) {
            /* We can safely suppose that 16 <= quant_matrix[i] <= 255
               So 16           <= qscale * quant_matrix[i]             <= 7905
               so (1<<19) / 16 >= (1<<19) / (qscale * quant_matrix[i]) >= (1<<19) / 7905
               so 32768        >= (1<<19) / (qscale * quant_matrix[i]) >= 67
            */
            qmat[i]   = (1 << QMAT_SHIFT_MMX) / (qscale * quant_matrix[i]);
            qmat16[i] = (1 << QMAT_SHIFT_MMX) / (qscale * quant_matrix[block_permute_op(i)]);
        }
    }
}

/* init common structure for both encoder and decoder */
int MPV_common_init(MpegEncContext *s)
{
    int c_size, i;
    UINT8 *pict;

    if (s->out_format == FMT_H263) 
        s->dct_unquantize = dct_unquantize_h263_c;
    else
        s->dct_unquantize = dct_unquantize_mpeg1_c;
        
#ifdef HAVE_MMX
    MPV_common_init_mmx(s);
#endif
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

        if (s->has_b_frames) {
            pict = av_mallocz(c_size);
            if (pict == NULL) 
                goto fail;
            s->aux_picture_base[i] = pict;
            s->aux_picture[i] = pict + pict_start;
        }
    }
    
    if (s->encoding) {
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
        /* Allocate MV table */
        /* By now we just have one MV per MB */
        s->mv_table[0] = av_mallocz(s->mb_num * sizeof(INT16));
        s->mv_table[1] = av_mallocz(s->mb_num * sizeof(INT16));
        if (s->mv_table[1] == NULL || s->mv_table[0] == NULL) {
            perror("malloc");
            goto fail;
        }
    }
    
    if (s->out_format == FMT_H263) {
        int size;
        /* MV prediction */
        size = (2 * s->mb_width + 2) * (2 * s->mb_height + 2);
        s->motion_val = malloc(size * 2 * sizeof(INT16));
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
        s->dc_val[0] = malloc(size * sizeof(INT16));
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
    }
    /* default structure is frame */
    s->picture_structure = PICT_FRAME;

    /* init macroblock skip table */
    if (!s->encoding) {
        s->mbskip_table = av_mallocz(s->mb_num);
        if (!s->mbskip_table)
            goto fail;
    }

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

    if (s->mb_type)
        free(s->mb_type);
    if (s->mb_var)
        free(s->mb_var);
    if (s->mv_table[0])
        free(s->mv_table[0]);
    if (s->mv_table[1])
        free(s->mv_table[1]);
    if (s->motion_val)
        free(s->motion_val);
    if (s->dc_val[0])
        free(s->dc_val[0]);
    if (s->ac_val[0])
        free(s->ac_val[0]);
    if (s->coded_block)
        free(s->coded_block);
    if (s->mbintra_table)
        free(s->mbintra_table);

    if (s->mbskip_table)
        free(s->mbskip_table);
    for(i=0;i<3;i++) {
        if (s->last_picture_base[i])
	    free(s->last_picture_base[i]);
	if (s->next_picture_base[i])
	    free(s->next_picture_base[i]);
        if (s->has_b_frames)
            free(s->aux_picture_base[i]);
    }
    s->context_initialized = 0;
}

/* init video encoder */
int MPV_encode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    int i;

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
    s->avctx = avctx;
    s->aspect_ratio_info= avctx->aspect_ratio_info;
    
    if (s->gop_size <= 1) {
        s->intra_only = 1;
        s->gop_size = 12;
    } else {
        s->intra_only = 0;
    }
    s->full_search = motion_estimation_method;

    s->fixed_qscale = (avctx->flags & CODEC_FLAG_QSCALE);
    
    switch(avctx->codec->id) {
    case CODEC_ID_MPEG1VIDEO:
        s->out_format = FMT_MPEG1;
        break;
    case CODEC_ID_MJPEG:
        s->out_format = FMT_MJPEG;
        s->intra_only = 1; /* force intra only for jpeg */
        s->mjpeg_write_tables = 1; /* write all tables */
        s->mjpeg_vsample[0] = 2; /* set up default sampling factors */
        s->mjpeg_vsample[1] = 1; /* the only currently supported values */
        s->mjpeg_vsample[2] = 1; 
        s->mjpeg_hsample[0] = 2; 
        s->mjpeg_hsample[1] = 1; 
        s->mjpeg_hsample[2] = 1; 
        if (mjpeg_init(s) < 0)
            return -1;
        break;
    case CODEC_ID_H263:
        if (h263_get_picture_format(s->width, s->height) == 7) {
            printf("Input picture size isn't suitable for h263 codec! try h263+\n");
            return -1;
        }
        s->out_format = FMT_H263;
        break;
    case CODEC_ID_H263P:
        s->out_format = FMT_H263;
        s->rtp_mode = 1;
        s->rtp_payload_size = 1200; 
        s->h263_plus = 1;
        s->unrestricted_mv = 1;
        
        /* These are just to be sure */
        s->umvplus = 0;
        s->umvplus_dec = 0;
        break;
    case CODEC_ID_RV10:
        s->out_format = FMT_H263;
        s->h263_rv10 = 1;
        break;
    case CODEC_ID_MPEG4:
        s->out_format = FMT_H263;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        break;
    case CODEC_ID_MSMPEG4:
        s->out_format = FMT_H263;
        s->h263_msmpeg4 = 1;
        s->h263_pred = 1;
        s->unrestricted_mv = 1;
        break;
    default:
        return -1;
    }

    { /* set up some save defaults, some codecs might override them later */
        static int done=0;
        if(!done){
            int i;
            done=1;
            memset(default_mv_penalty, 0, sizeof(UINT16)*MAX_FCODE*(2*MAX_MV+1));
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

    s->encoding = 1;

    /* init */
    if (MPV_common_init(s) < 0)
        return -1;
    
    /* init default q matrix */
    for(i=0;i<64;i++) {
        s->intra_matrix[i] = default_intra_matrix[i];
        s->non_intra_matrix[i] = default_non_intra_matrix[i];
    }

    /* rate control init */
    rate_control_init(s);

    s->picture_number = 0;
    s->picture_in_gop_number = 0;
    s->fake_picture_number = 0;
    /* motion detector init */
    s->f_code = 1;

    return 0;
}

int MPV_encode_end(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

#ifdef STATS
    print_stats();
#endif
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
        s->last_non_b_pict_type= s->pict_type;
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
}

int MPV_encode_picture(AVCodecContext *avctx,
                       unsigned char *buf, int buf_size, void *data)
{
    MpegEncContext *s = avctx->priv_data;
    AVPicture *pict = data;
    int i, j;

    if (s->fixed_qscale) 
        s->qscale = avctx->quality;

    init_put_bits(&s->pb, buf, buf_size, NULL, NULL);

    if (!s->intra_only) {
        /* first picture of GOP is intra */
        if (s->picture_in_gop_number >= s->gop_size){
            s->picture_in_gop_number=0;
            s->pict_type = I_TYPE;
        }else
            s->pict_type = P_TYPE;
    } else {
        s->pict_type = I_TYPE;
    }
    
    MPV_frame_start(s);
    
    for(i=0;i<3;i++) {
        UINT8 *src = pict->data[i];
        UINT8 *dest = s->current_picture[i];
        int src_wrap = pict->linesize[i];
        int dest_wrap = s->linesize;
        int w = s->width;
        int h = s->height;

        if (i >= 1) {
            dest_wrap >>= 1;
            w >>= 1;
            h >>= 1;
        }

        if(dest_wrap==src_wrap){
            s->new_picture[i] = pict->data[i];
        } else {
            for(j=0;j<h;j++) {
                memcpy(dest, src, w);
                dest += dest_wrap;
                src += src_wrap;
            }
            s->new_picture[i] = s->current_picture[i];
	    }
    }

    encode_picture(s, s->picture_number);
    avctx->key_frame = (s->pict_type == I_TYPE);
    avctx->header_bits = s->header_bits;
    avctx->mv_bits     = s->mv_bits;
    avctx->misc_bits   = s->misc_bits;
    avctx->i_tex_bits  = s->i_tex_bits;
    avctx->p_tex_bits  = s->p_tex_bits;
    avctx->i_count     = s->i_count;
    avctx->p_count     = s->p_count;
    avctx->skip_count  = s->skip_count;

    MPV_frame_end(s);
    s->picture_number++;
    s->picture_in_gop_number++;

    if (s->out_format == FMT_MJPEG)
        mjpeg_picture_trailer(s);

    flush_put_bits(&s->pb);
    s->last_frame_bits= s->frame_bits;
    s->frame_bits  = (pbBufPtr(&s->pb) - s->pb.buf) * 8;
    s->total_bits += s->frame_bits;
    avctx->frame_bits  = s->frame_bits;
//printf("fcode: %d, type: %d, head: %d, mv: %d, misc: %d, frame: %d, itex: %d, ptex: %d\n", 
//s->f_code, avctx->key_frame, s->header_bits, s->mv_bits, s->misc_bits, s->frame_bits, s->i_tex_bits, s->p_tex_bits);

    avctx->quality = s->qscale;
    if (avctx->get_psnr) {
        /* At this point pict->data should have the original frame   */
        /* an s->current_picture should have the coded/decoded frame */
        get_psnr(pict->data, s->current_picture,
                 pict->linesize, s->linesize, avctx);
    }
    return pbBufPtr(&s->pb) - s->pb.buf;
}

static inline int clip(int a, int amin, int amax)
{
    if (a < amin)
        return amin;
    else if (a > amax)
        return amax;
    else
        return a;
}

static inline void gmc1_motion(MpegEncContext *s,
                               UINT8 *dest_y, UINT8 *dest_cb, UINT8 *dest_cr,
                               int dest_offset,
                               UINT8 **ref_picture, int src_offset,
                               int h)
{
    UINT8 *ptr;
    int dxy, offset, mx, my, src_x, src_y, height, linesize;
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
            src_y = mb_y * 16 + (motion_y >> 1) + ((i >> 1) & 1) * 8;
                    
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
    if (s->out_format == FMT_H263) {
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
    
    if (!s->intra_only) {
        UINT8 *dest_y, *dest_cb, *dest_cr;
        UINT8 *mbskip_ptr;

        /* avoid copy if macroblock skipped in last frame too */
        if (!s->encoding && s->pict_type != B_TYPE) {
            mbskip_ptr = &s->mbskip_table[s->mb_y * s->mb_width + s->mb_x];
            if (s->mb_skiped) {
                s->mb_skiped = 0;
                /* if previous was skipped too, then nothing to do ! */
                if (*mbskip_ptr != 0) 
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
            if (!s->no_rounding){
                op_pix = put_pixels_tab;
                op_qpix= qpel_mc_rnd_tab;
            }else{
                op_pix = put_no_rnd_pixels_tab;
                op_qpix= qpel_mc_no_rnd_tab;
            }

            if (s->mv_dir & MV_DIR_FORWARD) {
                MPV_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_picture, op_pix, op_qpix);
                if (!s->no_rounding) 
                    op_pix = avg_pixels_tab;
                else
                    op_pix = avg_no_rnd_pixels_tab;
            }
            if (s->mv_dir & MV_DIR_BACKWARD) {
                MPV_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_picture, op_pix, op_qpix);
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
    emms_c();
}

static void encode_picture(MpegEncContext *s, int picture_number)
{
    int mb_x, mb_y, wrap, last_gob, pdif = 0;
    UINT8 *ptr;
    int i, motion_x, motion_y;
    int bits;

    s->picture_number = picture_number;

    s->last_mc_mb_var = s->mc_mb_var;
    /* Reset the average MB variance */
    s->avg_mb_var = 0;
    s->mc_mb_var = 0;
    /* Estimate motion for every MB */
    for(mb_y=0; mb_y < s->mb_height; mb_y++) {
        for(mb_x=0; mb_x < s->mb_width; mb_x++) {
            int xy= mb_y * s->mb_width + mb_x;
            s->mb_x = mb_x;
            s->mb_y = mb_y;

            /* compute motion vector and macro block type (intra or non intra) */
            motion_x = 0;
            motion_y = 0;
            if (s->pict_type == P_TYPE) {
                s->mb_intra = estimate_motion(s, mb_x, mb_y,
                                              &motion_x,
                                              &motion_y);
            } else {
                s->mb_intra = 1;
            }
            /* Store MB type and MV */
            s->mb_type[xy] = s->mb_intra;
            s->mv_table[0][xy] = motion_x;
            s->mv_table[1][xy] = motion_y;
        }
    }
    emms_c();

    if(s->avg_mb_var < s->mc_mb_var && s->pict_type != B_TYPE){ //FIXME subtract MV bits
        int i;
        s->pict_type= I_TYPE;
        s->picture_in_gop_number=0;
        for(i=0; i<s->mb_num; i++){
            s->mb_type[i] = 1;
            s->mv_table[0][i] = 0;
            s->mv_table[1][i] = 0;
        }
    }

    /* find best f_code */
    if(s->pict_type==P_TYPE){
        int mv_num[8];
        int i;
        int loose=0;
        UINT8 * fcode_tab= s->fcode_tab;

        for(i=0; i<8; i++) mv_num[i]=0;

        for(i=0; i<s->mb_num; i++){
            if(s->mb_type[i] == 0){
                mv_num[ fcode_tab[s->mv_table[0][i] + MAX_MV] ]++;
                mv_num[ fcode_tab[s->mv_table[1][i] + MAX_MV] ]++;
//printf("%d %d %d\n", s->mv_table[0][i], fcode_tab[s->mv_table[0][i] + MAX_MV], i);
            }
//else printf("I");
        }

        for(i=MAX_FCODE; i>1; i--){
            loose+= mv_num[i];
            if(loose > 10) break; //FIXME this is pretty ineffective
        }
        s->f_code= i;
    }else{
        s->f_code= 1;
    }

//printf("f_code %d ///\n", s->f_code);
    /* convert MBs with too long MVs to I-Blocks */
    if(s->pict_type==P_TYPE){
        int i;
        const int f_code= s->f_code;
        UINT8 * fcode_tab= s->fcode_tab;

        for(i=0; i<s->mb_num; i++){
            if(s->mb_type[i] == 0){
                if(   fcode_tab[s->mv_table[0][i] + MAX_MV] > f_code
                   || fcode_tab[s->mv_table[0][i] + MAX_MV] == 0
                   || fcode_tab[s->mv_table[1][i] + MAX_MV] > f_code
                   || fcode_tab[s->mv_table[1][i] + MAX_MV] == 0 ){
                    s->mb_type[i] = 1;
                    s->mv_table[0][i] = 0;
                    s->mv_table[1][i] = 0;
                }
            }
        }
    }

//    printf("%d %d\n", s->avg_mb_var, s->mc_mb_var);

    if (!s->fixed_qscale) 
        s->qscale = rate_estimate_qscale(s);

    /* precompute matrix */
    if (s->out_format == FMT_MJPEG) {
        /* for mjpeg, we do include qscale in the matrix */
        s->intra_matrix[0] = default_intra_matrix[0];
        for(i=1;i<64;i++)
            s->intra_matrix[i] = (default_intra_matrix[i] * s->qscale) >> 3;
        convert_matrix(s->q_intra_matrix, s->q_intra_matrix16, s->intra_matrix, 8);
    } else {
        convert_matrix(s->q_intra_matrix, s->q_intra_matrix16, s->intra_matrix, s->qscale);
        convert_matrix(s->q_non_intra_matrix, s->q_non_intra_matrix16, s->non_intra_matrix, s->qscale);
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
    s->mv_type = MV_TYPE_16X16;
    s->mv_dir = MV_DIR_FORWARD;

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
    
    s->block_wrap[0]=
    s->block_wrap[1]=
    s->block_wrap[2]=
    s->block_wrap[3]= s->mb_width*2 + 2;
    s->block_wrap[4]=
    s->block_wrap[5]= s->mb_width + 2;
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

            s->mb_x = mb_x;
            s->mb_y = mb_y;
            s->block_index[0]+=2;
            s->block_index[1]+=2;
            s->block_index[2]+=2;
            s->block_index[3]+=2;
            s->block_index[4]++;
            s->block_index[5]++;
#if 0
            /* compute motion vector and macro block type (intra or non intra) */
            motion_x = 0;
            motion_y = 0;
            if (s->pict_type == P_TYPE) {
                s->mb_intra = estimate_motion(s, mb_x, mb_y,
                                              &motion_x,
                                              &motion_y);
            } else {
                s->mb_intra = 1;
            }
#endif

            s->mb_intra = s->mb_type[mb_y * s->mb_width + mb_x];
            motion_x = s->mv_table[0][mb_y * s->mb_width + mb_x];
            motion_y = s->mv_table[1][mb_y * s->mb_width + mb_x];
            
            /* get the pixels */
            wrap = s->linesize;
            ptr = s->new_picture[0] + (mb_y * 16 * wrap) + mb_x * 16;
            get_pixels(s->block[0], ptr, wrap);
            get_pixels(s->block[1], ptr + 8, wrap);
            get_pixels(s->block[2], ptr + 8 * wrap, wrap);
            get_pixels(s->block[3], ptr + 8 * wrap + 8, wrap);
            wrap = s->linesize >> 1;
            ptr = s->new_picture[1] + (mb_y * 8 * wrap) + mb_x * 8;
            get_pixels(s->block[4], ptr, wrap);

            wrap = s->linesize >> 1;
            ptr = s->new_picture[2] + (mb_y * 8 * wrap) + mb_x * 8;
            get_pixels(s->block[5], ptr, wrap);

            /* subtract previous frame if non intra */
            if (!s->mb_intra) {
                int dxy, offset, mx, my;

                dxy = ((motion_y & 1) << 1) | (motion_x & 1);
                ptr = s->last_picture[0] + 
                    ((mb_y * 16 + (motion_y >> 1)) * s->linesize) + 
                    (mb_x * 16 + (motion_x >> 1));

                sub_pixels_2(s->block[0], ptr, s->linesize, dxy);
                sub_pixels_2(s->block[1], ptr + 8, s->linesize, dxy);
                sub_pixels_2(s->block[2], ptr + s->linesize * 8, s->linesize, dxy);
                sub_pixels_2(s->block[3], ptr + 8 + s->linesize * 8, s->linesize ,dxy);

                if (s->out_format == FMT_H263) {
                    /* special rounding for h263 */
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
                offset = ((mb_y * 8 + my) * (s->linesize >> 1)) + (mb_x * 8 + mx);
                ptr = s->last_picture[1] + offset;
                sub_pixels_2(s->block[4], ptr, s->linesize >> 1, dxy);
                ptr = s->last_picture[2] + offset;
                sub_pixels_2(s->block[5], ptr, s->linesize >> 1, dxy);
            }
            emms_c();
            
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
            if (s->h263_msmpeg4) {
                msmpeg4_dc_scale(s);
            } else if (s->h263_pred) {
                h263_dc_scale(s);
            } else {
                /* default quantization values */
                s->y_dc_scale = 8;
                s->c_dc_scale = 8;
            }
            for(i=0;i<6;i++) {
                s->block_last_index[i] = dct_quantize(s, s->block[i], i, s->qscale);
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
            
            /* decompress blocks so that we keep the state of the decoder */
            s->mv[0][0][0] = motion_x;
            s->mv[0][0][1] = motion_y;

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

    if (s->h263_msmpeg4 && s->pict_type == I_TYPE)
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
                        int qscale)
{
    int i, j, level, last_non_zero, q;
    const int *qmat;
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

    /* we need this permutation so that we correct the IDCT
       permutation. will be moved into DCT code */
    block_permute(block);

    if (s->mb_intra) {
        if (n < 4)
            q = s->y_dc_scale;
        else
            q = s->c_dc_scale;
        q = q << 3;
        
        /* note: block[0] is assumed to be positive */
        block[0] = (block[0] + (q >> 1)) / q;
        i = 1;
        last_non_zero = 0;
        if (s->out_format == FMT_H263) {
            qmat = s->q_non_intra_matrix;
        } else {
            qmat = s->q_intra_matrix;
        }
    } else {
        i = 0;
        last_non_zero = -1;
        qmat = s->q_non_intra_matrix;
    }

    for(;i<64;i++) {
        j = zigzag_direct[i];
        level = block[j];
        level = level * qmat[j];
#ifdef PARANOID
        {
            static int count = 0;
            int level1, level2, qmat1;
            double val;
            if (qmat == s->q_non_intra_matrix) {
                qmat1 = default_non_intra_matrix[j] * s->qscale;
            } else {
                qmat1 = default_intra_matrix[j] * s->qscale;
            }
            if (av_fdct != jpeg_fdct_ifast)
                val = ((double)block[j] * 8.0) / (double)qmat1;
            else
                val = ((double)block[j] * 8.0 * 2048.0) / 
                    ((double)qmat1 * aanscales[j]);
            level1 = (int)val;
            level2 = level / (1 << (QMAT_SHIFT - 3));
            if (level1 != level2) {
                fprintf(stderr, "%d: quant error qlevel=%d wanted=%d level=%d qmat1=%d qmat=%d wantedf=%0.6f\n", 
                        count, level2, level1, block[j], qmat1, qmat[j],
                        val);
                count++;
            }

        }
#endif
        /* XXX: slight error for the low range. Test should be equivalent to
           (level <= -(1 << (QMAT_SHIFT - 3)) || level >= (1 <<
           (QMAT_SHIFT - 3)))
        */
        if (((level << (31 - (QMAT_SHIFT - 3))) >> (31 - (QMAT_SHIFT - 3))) != 
            level) {
            level = level / (1 << (QMAT_SHIFT - 3));
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

            block[j] = level;
            last_non_zero = i;
        } else {
            block[j] = 0;
        }
    }
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
        quant_matrix = s->non_intra_matrix;
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

/* rate control */

/* an I frame is I_FRAME_SIZE_RATIO bigger than a P frame */
#define I_FRAME_SIZE_RATIO 3.0
#define QSCALE_K           20

static void rate_control_init(MpegEncContext *s)
{
#if 1
    emms_c();

    //initial values, they dont really matter as they will be totally different within a few frames
    s->i_pred.coeff= s->p_pred.coeff= 7.0;
    s->i_pred.count= s->p_pred.count= 1.0;
    
    s->i_pred.decay= s->p_pred.decay= 0.4;
    
    // use more bits at the beginning, otherwise high motion at the begin will look like shit
    s->qsum=100;
    s->qcount=100;

    s->short_term_qsum=0.001;
    s->short_term_qcount=0.001;
#else
    s->wanted_bits = 0;

    if (s->intra_only) {
        s->I_frame_bits = ((INT64)s->bit_rate * FRAME_RATE_BASE) / s->frame_rate;
        s->P_frame_bits = s->I_frame_bits;
    } else {
        s->P_frame_bits = (int) ((float)(s->gop_size * s->bit_rate) / 
                                 (float)((float)s->frame_rate / FRAME_RATE_BASE * (I_FRAME_SIZE_RATIO + s->gop_size - 1)));
        s->I_frame_bits = (int)(s->P_frame_bits * I_FRAME_SIZE_RATIO);
    }

#if defined(DEBUG)
    printf("I_frame_size=%d P_frame_size=%d\n",
           s->I_frame_bits, s->P_frame_bits);
#endif
#endif
}

static double predict(Predictor *p, double q, double var)
{
    return p->coeff*var / (q*p->count);
}

static void update_predictor(Predictor *p, double q, double var, double size)
{
    double new_coeff= size*q / (var + 1);
    if(var<1000) return;
/*{
int pred= predict(p, q, var);
int error= abs(pred-size);
static double sum=0;
static int count=0;
if(count>5) sum+=error;
count++;
if(256*256*256*64%count==0){
    printf("%d %f %f\n", count, sum/count, p->coeff);
}
}*/
    p->count*= p->decay;
    p->coeff*= p->decay;
    p->count++;
    p->coeff+= new_coeff;
}

static int rate_estimate_qscale(MpegEncContext *s)
{
#if 1
    int qmin= s->qmin;
    int qmax= s->qmax;
    int rate_q=5;
    float q;
    int qscale;
    float br_compensation;
    double diff;
    double short_term_q;
    double long_term_q;
    int last_qscale= s->qscale;
    double fps;
    INT64 wanted_bits;
    emms_c();
    
    fps= (double)s->frame_rate / FRAME_RATE_BASE;
    wanted_bits= s->bit_rate*(double)s->picture_number/fps;

    
    if(s->picture_number>2){
        /* update predictors */
        if(s->last_pict_type == I_TYPE){
        //FIXME
        }else{ //P Frame
//printf("%d %d %d %f\n", s->qscale, s->last_mc_mb_var, s->frame_bits, s->p_pred.coeff);
            update_predictor(&s->p_pred, s->qscale, s->last_mc_mb_var, s->frame_bits);
        }
    }

    if(s->pict_type == I_TYPE){
        //FIXME
        rate_q= s->qsum/s->qcount;
    }else{ //P Frame
        int i;
        int diff, best_diff=1000000000;
        for(i=1; i<=31; i++){
            diff= predict(&s->p_pred, i, s->mc_mb_var) - (double)s->bit_rate/fps;
            if(diff<0) diff= -diff;
            if(diff<best_diff){
                best_diff= diff;
                rate_q= i;
            }
        }
    }

    s->short_term_qsum*=s->qblur;
    s->short_term_qcount*=s->qblur;

    s->short_term_qsum+= rate_q;
    s->short_term_qcount++;
    short_term_q= s->short_term_qsum/s->short_term_qcount;
    
    long_term_q= s->qsum/s->qcount*s->total_bits/wanted_bits;

//    q= (long_term_q - short_term_q)*s->qcompress + short_term_q;
    q= 1/((1/long_term_q - 1/short_term_q)*s->qcompress + 1/short_term_q);

    diff= s->total_bits - wanted_bits;
    br_compensation= (s->bit_rate_tolerance - diff)/s->bit_rate_tolerance;
    if(br_compensation<=0.0) br_compensation=0.001;
    q/=br_compensation;

    qscale= (int)(q + 0.5);
    if     (qscale<qmin) qscale=qmin;
    else if(qscale>qmax) qscale=qmax;
    
    if     (qscale<last_qscale-s->max_qdiff) qscale=last_qscale-s->max_qdiff;
    else if(qscale>last_qscale+s->max_qdiff) qscale=last_qscale+s->max_qdiff;

    s->qsum+= qscale;
    s->qcount++;

    s->last_pict_type= s->pict_type;
//printf("q:%d diff:%d comp:%f rate_q:%d st_q:%d fvar:%d last_size:%d\n", qscale, (int)diff, br_compensation, 
//       rate_q, (int)short_term_q, s->mc_mb_var, s->frame_bits);
//printf("%d %d\n", s->bit_rate, (int)fps);
    return qscale;
#else
    INT64 diff, total_bits = s->total_bits;
    float q;
    int qscale;
    if (s->pict_type == I_TYPE) {
        s->wanted_bits += s->I_frame_bits;
    } else {
        s->wanted_bits += s->P_frame_bits;
    }
    diff = s->wanted_bits - total_bits;
    q = 31.0 - (float)diff / (QSCALE_K * s->mb_height * s->mb_width);
    /* adjust for I frame */
    if (s->pict_type == I_TYPE && !s->intra_only) {
        q /= I_FRAME_SIZE_RATIO;
    }

    /* using a too small Q scale leeds to problems in mpeg1 and h263
       because AC coefficients are clamped to 255 or 127 */
    qmin = 3;
    if (q < qmin)
        q = qmin;
    else if (q > 31)
        q = 31;
    qscale = (int)(q + 0.5);
#if defined(DEBUG)
    printf("\n%d: total=%0.0f wanted=%0.0f br=%0.1f diff=%d qest=%2.1f\n", 
           s->picture_number, 
           (double)total_bits, 
           (double)s->wanted_bits,
           (float)s->frame_rate / FRAME_RATE_BASE * 
           total_bits / s->picture_number, 
           (int)diff, q);
#endif
    return qscale;
#endif
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

AVCodec msmpeg4_encoder = {
    "msmpeg4",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSMPEG4,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};
