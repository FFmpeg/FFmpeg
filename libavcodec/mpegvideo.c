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
static int dct_quantize(MpegEncContext *s, DCTELEM *block, int n, int qscale);
static int dct_quantize_mmx(MpegEncContext *s, 
                            DCTELEM *block, int n,
                            int qscale);
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

/* default motion estimation */
int motion_estimation_method = ME_LOG;

/* XXX: should use variable shift ? */
#define QMAT_SHIFT_MMX 19
#define QMAT_SHIFT 25

static void convert_matrix(int *qmat, const UINT16 *quant_matrix, int qscale)
{
    int i;

    if (av_fdct == jpeg_fdct_ifast) {
        for(i=0;i<64;i++) {
            /* 16 <= qscale * quant_matrix[i] <= 7905 */
            /* 19952 <= aanscales[i] * qscale * quant_matrix[i] <= 249205026 */
            
            qmat[i] = (int)((1ULL << (QMAT_SHIFT + 11)) / (aanscales[i] * qscale * quant_matrix[i]));
        }
    } else {
        for(i=0;i<64;i++) {
            /* We can safely suppose that 16 <= quant_matrix[i] <= 255
               So 16 <= qscale * quant_matrix[i] <= 7905
               so (1 << QMAT_SHIFT) / 16 >= qmat[i] >= (1 << QMAT_SHIFT) / 7905
            */
            qmat[i] = (1 << QMAT_SHIFT_MMX) / (qscale * quant_matrix[i]);
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

    if (s->out_format == FMT_H263) {
        int size;
        /* MV prediction */
        size = (2 * s->mb_width + 2) * (2 * s->mb_height + 2);
        s->motion_val = malloc(size * 2 * sizeof(INT16));
        if (s->motion_val == NULL)
            goto fail;
        memset(s->motion_val, 0, size * 2 * sizeof(INT16));
    }

    if (s->h263_pred) {
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
    }
    /* default structure is frame */
    s->picture_structure = PICT_FRAME;

    /* init default q matrix (only for mpeg and mjpeg) */
    for(i=0;i<64;i++) {
        s->intra_matrix[i] = default_intra_matrix[i];
        s->chroma_intra_matrix[i] = default_intra_matrix[i];
        s->non_intra_matrix[i] = default_non_intra_matrix[i];
        s->chroma_non_intra_matrix[i] = default_non_intra_matrix[i];
    }
    /* init macroblock skip table */
    if (!s->encoding) {
        s->mbskip_table = av_mallocz(s->mb_width * s->mb_height);
        if (!s->mbskip_table)
            goto fail;
    }

    s->context_initialized = 1;
    return 0;
 fail:
    if (s->motion_val)
        free(s->motion_val);
    if (s->dc_val[0])
        free(s->dc_val[0]);
    if (s->ac_val[0])
        free(s->ac_val[0]);
    if (s->coded_block)
        free(s->coded_block);
    if (s->mbskip_table)
        free(s->mbskip_table);
    for(i=0;i<3;i++) {
        if (s->last_picture_base[i])
            free(s->last_picture_base[i]);
        if (s->next_picture_base[i])
            free(s->next_picture_base[i]);
        if (s->aux_picture_base[i])
            free(s->aux_picture_base[i]);
    }
    return -1;
}

/* init common structure for both encoder and decoder */
void MPV_common_end(MpegEncContext *s)
{
    int i;

    if (s->motion_val)
        free(s->motion_val);
    if (s->h263_pred) {
        free(s->dc_val[0]);
        free(s->ac_val[0]);
        free(s->coded_block);
    }
    if (s->mbskip_table)
        free(s->mbskip_table);
    for(i=0;i<3;i++) {
        free(s->last_picture_base[i]);
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

    s->bit_rate = avctx->bit_rate;
    s->frame_rate = avctx->frame_rate;
    s->width = avctx->width;
    s->height = avctx->height;
    s->gop_size = avctx->gop_size;
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
        if (mjpeg_init(s) < 0)
            return -1;
        break;
    case CODEC_ID_H263:
        if (h263_get_picture_format(s->width, s->height) == 7)
            return -1;
        s->out_format = FMT_H263;
        break;
    case CODEC_ID_H263P:
        s->out_format = FMT_H263;
        s->h263_plus = 1;
        /* XXX: not unrectricted mv yet */
        break;
    case CODEC_ID_RV10:
        s->out_format = FMT_H263;
        s->h263_rv10 = 1;
        break;
    case CODEC_ID_OPENDIVX:
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

    if (s->out_format == FMT_H263)
        h263_encode_init_vlc(s);

    s->encoding = 1;

    /* init */
    if (MPV_common_init(s) < 0)
        return -1;
    
    /* rate control init */
    rate_control_init(s);

    s->picture_number = 0;
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
static void draw_edges(UINT8 *buf, int wrap, int width, int height, int w)
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
    if (s->pict_type != B_TYPE) {
        draw_edges(s->current_picture[0], s->linesize, s->width, s->height, EDGE_WIDTH);
        draw_edges(s->current_picture[1], s->linesize/2, s->width/2, s->height/2, EDGE_WIDTH/2);
        draw_edges(s->current_picture[2], s->linesize/2, s->width/2, s->height/2, EDGE_WIDTH/2);
    }
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
        if ((s->picture_number % s->gop_size) == 0)
            s->pict_type = I_TYPE;
        else
            s->pict_type = P_TYPE;
    } else {
        s->pict_type = I_TYPE;
    }
    avctx->key_frame = (s->pict_type == I_TYPE);
    
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

        for(j=0;j<h;j++) {
            memcpy(dest, src, w);
            dest += dest_wrap;
            src += src_wrap;
        }
        s->new_picture[i] = s->current_picture[i];
    }

    encode_picture(s, s->picture_number);
    
    MPV_frame_end(s);
    s->picture_number++;

    if (s->out_format == FMT_MJPEG)
        mjpeg_picture_trailer(s);

    flush_put_bits(&s->pb);
    s->total_bits += (s->pb.buf_ptr - s->pb.buf) * 8;
    avctx->quality = s->qscale;
    return s->pb.buf_ptr - s->pb.buf;
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

static inline void MPV_motion(MpegEncContext *s, 
                              UINT8 *dest_y, UINT8 *dest_cb, UINT8 *dest_cr,
                              int dir, UINT8 **ref_picture, 
                              op_pixels_func *pix_op)
{
    int dxy, offset, mx, my, src_x, src_y, motion_x, motion_y;
    int mb_x, mb_y, i;
    UINT8 *ptr, *dest;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

    switch(s->mv_type) {
    case MV_TYPE_16X16:
        mpeg_motion(s, dest_y, dest_cb, dest_cr, 0,
                    ref_picture, 0,
                    0, pix_op,
                    s->mv[dir][0][0], s->mv[dir][0][1], 16);
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
    int mb_x, mb_y, motion_x, motion_y;
    int dct_linesize, dct_offset;
    op_pixels_func *op_pix;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

    /* update DC predictors for P macroblocks */
    if (!s->mb_intra) {
        if (s->h263_pred) {
            int wrap, x, y, v;
            wrap = 2 * s->mb_width + 2;
            v = 1024;
            x = 2 * mb_x + 1;
            y = 2 * mb_y + 1;
            s->dc_val[0][(x) + (y) * wrap] = v;
            s->dc_val[0][(x + 1) + (y) * wrap] = v;
            s->dc_val[0][(x) + (y + 1) * wrap] = v;
            s->dc_val[0][(x + 1) + (y + 1) * wrap] = v;
            /* ac pred */
            memset(s->ac_val[0][(x) + (y) * wrap], 0, 16 * sizeof(INT16));
            memset(s->ac_val[0][(x + 1) + (y) * wrap], 0, 16 * sizeof(INT16));
            memset(s->ac_val[0][(x) + (y + 1) * wrap], 0, 16 * sizeof(INT16));
            memset(s->ac_val[0][(x + 1) + (y + 1) * wrap], 0, 16 * sizeof(INT16));
            if (s->h263_msmpeg4) {
                s->coded_block[(x) + (y) * wrap] = 0;
                s->coded_block[(x + 1) + (y) * wrap] = 0;
                s->coded_block[(x) + (y + 1) * wrap] = 0;
                s->coded_block[(x + 1) + (y + 1) * wrap] = 0;
            }
            /* chroma */
            wrap = s->mb_width + 2;
            x = mb_x + 1;
            y = mb_y + 1;
            s->dc_val[1][(x) + (y) * wrap] = v;
            s->dc_val[2][(x) + (y) * wrap] = v;
            /* ac pred */
            memset(s->ac_val[1][(x) + (y) * wrap], 0, 16 * sizeof(INT16));
            memset(s->ac_val[2][(x) + (y) * wrap], 0, 16 * sizeof(INT16));
        } else {
            s->last_dc[0] = 128 << s->intra_dc_precision;
            s->last_dc[1] = 128 << s->intra_dc_precision;
            s->last_dc[2] = 128 << s->intra_dc_precision;
        }
    }
    
    /* update motion predictor */
    if (s->out_format == FMT_H263) {
        int x, y, wrap;
        
        x = 2 * mb_x + 1;
        y = 2 * mb_y + 1;
        wrap = 2 * s->mb_width + 2;
        if (s->mb_intra) {
            motion_x = 0;
            motion_y = 0;
            goto motion_init;
        } else if (s->mv_type == MV_TYPE_16X16) {
            motion_x = s->mv[0][0][0];
            motion_y = s->mv[0][0][1];
        motion_init:
            /* no update if 8X8 because it has been done during parsing */
            s->motion_val[(x) + (y) * wrap][0] = motion_x;
            s->motion_val[(x) + (y) * wrap][1] = motion_y;
            s->motion_val[(x + 1) + (y) * wrap][0] = motion_x;
            s->motion_val[(x + 1) + (y) * wrap][1] = motion_y;
            s->motion_val[(x) + (y + 1) * wrap][0] = motion_x;
            s->motion_val[(x) + (y + 1) * wrap][1] = motion_y;
            s->motion_val[(x + 1) + (y + 1) * wrap][0] = motion_x;
            s->motion_val[(x + 1) + (y + 1) * wrap][1] = motion_y;
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
            if (!s->no_rounding) 
                op_pix = put_pixels_tab;
            else
                op_pix = put_no_rnd_pixels_tab;

            if (s->mv_dir & MV_DIR_FORWARD) {
                MPV_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_picture, op_pix);
                if (!s->no_rounding) 
                    op_pix = avg_pixels_tab;
                else
                    op_pix = avg_no_rnd_pixels_tab;
            }
            if (s->mv_dir & MV_DIR_BACKWARD) {
                MPV_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_picture, op_pix);
            }

            /* add dct residue */
            add_dct(s, block[0], 0, dest_y, dct_linesize);
            add_dct(s, block[1], 1, dest_y + 8, dct_linesize);
            add_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize);
            add_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize);

            add_dct(s, block[4], 4, dest_cb, dct_linesize >> 1);
            add_dct(s, block[5], 5, dest_cr, dct_linesize >> 1);
        } else {
            /* dct only in intra block */
            put_dct(s, block[0], 0, dest_y, dct_linesize);
            put_dct(s, block[1], 1, dest_y + 8, dct_linesize);
            put_dct(s, block[2], 2, dest_y + dct_offset, dct_linesize);
            put_dct(s, block[3], 3, dest_y + dct_offset + 8, dct_linesize);

            put_dct(s, block[4], 4, dest_cb, dct_linesize >> 1);
            put_dct(s, block[5], 5, dest_cr, dct_linesize >> 1);
        }
    }
 the_end:
    emms_c();
}

static void encode_picture(MpegEncContext *s, int picture_number)
{
    int mb_x, mb_y, wrap;
    UINT8 *ptr;
    int i, motion_x, motion_y;

    s->picture_number = picture_number;
    if (!s->fixed_qscale) 
        s->qscale = rate_estimate_qscale(s);

    /* precompute matrix */
    if (s->out_format == FMT_MJPEG) {
        /* for mjpeg, we do include qscale in the matrix */
        s->intra_matrix[0] = default_intra_matrix[0];
        for(i=1;i<64;i++)
            s->intra_matrix[i] = (default_intra_matrix[i] * s->qscale) >> 3;
        convert_matrix(s->q_intra_matrix, s->intra_matrix, 8);
    } else {
        convert_matrix(s->q_intra_matrix, s->intra_matrix, s->qscale);
        convert_matrix(s->q_non_intra_matrix, s->non_intra_matrix, s->qscale);
    }

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

    for(mb_y=0; mb_y < s->mb_height; mb_y++) {
        for(mb_x=0; mb_x < s->mb_width; mb_x++) {

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
                int last_index;
                if (av_fdct == jpeg_fdct_ifast)
                    last_index = dct_quantize(s, s->block[i], i, s->qscale);
                else
                    last_index = dct_quantize_mmx(s, s->block[i], i, s->qscale);
                s->block_last_index[i] = last_index;
            }

            /* huffman encode */
            switch(s->out_format) {
            case FMT_MPEG1:
                mpeg1_encode_mb(s, s->block, motion_x, motion_y);
                break;
            case FMT_H263:
                if (s->h263_msmpeg4)
                    msmpeg4_encode_mb(s, s->block, motion_x, motion_y);
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
    }
}

static int dct_quantize(MpegEncContext *s, 
                        DCTELEM *block, int n,
                        int qscale)
{
    int i, j, level, last_non_zero, q;
    const int *qmat;

    av_fdct (block);

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
            if (level > 127)
                level = 127;
            else if (level < -128)
                level = -128;
            block[j] = level;
            last_non_zero = i;
        } else {
            block[j] = 0;
        }
    }
    return last_non_zero;
}

static int dct_quantize_mmx(MpegEncContext *s, 
                            DCTELEM *block, int n,
                            int qscale)
{
    int i, j, level, last_non_zero, q;
    const int *qmat;

    av_fdct (block);
    
    /* we need this permutation so that we correct the IDCT
       permutation. will be moved into DCT code */
    block_permute(block);

    if (s->mb_intra) {
        if (n < 4)
            q = s->y_dc_scale;
        else
            q = s->c_dc_scale;
        
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
        /* XXX: slight error for the low range. Test should be equivalent to
           (level <= -(1 << (QMAT_SHIFT_MMX - 3)) || level >= (1 <<
           (QMAT_SHIFT_MMX - 3)))
        */
        if (((level << (31 - (QMAT_SHIFT_MMX - 3))) >> (31 - (QMAT_SHIFT_MMX - 3))) != 
            level) {
            level = level / (1 << (QMAT_SHIFT_MMX - 3));
            /* XXX: currently, this code is not optimal. the range should be:
               mpeg1: -255..255
               mpeg2: -2048..2047
               h263:  -128..127
               mpeg4: -2048..2047
            */
            if (level > 127)
                level = 127;
            else if (level < -128)
                level = -128;
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
    int i, level;
    const UINT16 *quant_matrix;

    if (s->mb_intra) {
        if (n < 4) 
            block[0] = block[0] * s->y_dc_scale;
        else
            block[0] = block[0] * s->c_dc_scale;
        /* XXX: only mpeg1 */
        quant_matrix = s->intra_matrix;
        for(i=1;i<64;i++) {
            level = block[i];
            if (level) {
                if (level < 0) {
                    level = -level;
                    level = (int)(level * qscale * quant_matrix[i]) >> 3;
                    level = (level - 1) | 1;
                    level = -level;
                } else {
                    level = (int)(level * qscale * quant_matrix[i]) >> 3;
                    level = (level - 1) | 1;
                }
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
                block[i] = level;
            }
        }
    } else {
        i = 0;
        quant_matrix = s->non_intra_matrix;
        for(;i<64;i++) {
            level = block[i];
            if (level) {
                if (level < 0) {
                    level = -level;
                    level = (((level << 1) + 1) * qscale *
                             ((int) (quant_matrix[i]))) >> 4;
                    level = (level - 1) | 1;
                    level = -level;
                } else {
                    level = (((level << 1) + 1) * qscale *
                             ((int) (quant_matrix[i]))) >> 4;
                    level = (level - 1) | 1;
                }
#ifdef PARANOID
                if (level < -2048 || level > 2047)
                    fprintf(stderr, "unquant error %d %d\n", i, level);
#endif
                block[i] = level;
            }
        }
    }
}

static void dct_unquantize_h263_c(MpegEncContext *s, 
                                  DCTELEM *block, int n, int qscale)
{
    int i, level, qmul, qadd;

    if (s->mb_intra) {
        if (n < 4) 
            block[0] = block[0] * s->y_dc_scale;
        else
            block[0] = block[0] * s->c_dc_scale;
        i = 1;
    } else {
        i = 0;
    }

    qmul = s->qscale << 1;
    qadd = (s->qscale - 1) | 1;

    for(;i<64;i++) {
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
}


/*
 * This heuristic is rather poor, but at least we do not have to
 * change the qscale at every macroblock.
 */
static int rate_estimate_qscale(MpegEncContext *s)
{
    long long total_bits = s->total_bits;
    float q;
    int qscale, diff, qmin;

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
    printf("%d: total=%Ld br=%0.1f diff=%d qest=%0.1f\n", 
           s->picture_number, 
           total_bits, 
           (float)s->frame_rate / FRAME_RATE_BASE * 
           total_bits / s->picture_number, 
           diff, q);
#endif
    return qscale;
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

AVCodec opendivx_encoder = {
    "opendivx",
    CODEC_TYPE_VIDEO,
    CODEC_ID_OPENDIVX,
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
