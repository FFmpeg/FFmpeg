/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 / AVC / MPEG4 part10 codec.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavcore/imgutils.h"
#include "internal.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h264.h"
#include "h264data.h"
#include "h264_mvpred.h"
#include "h264_parser.h"
#include "golomb.h"
#include "mathops.h"
#include "rectangle.h"
#include "vdpau_internal.h"
#include "libavutil/avassert.h"

#include "cabac.h"

//#undef NDEBUG
#include <assert.h>

static const uint8_t rem6[52]={
0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3,
};

static const uint8_t div6[52]={
0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8,
};

static const enum PixelFormat hwaccel_pixfmt_list_h264_jpeg_420[] = {
    PIX_FMT_DXVA2_VLD,
    PIX_FMT_VAAPI_VLD,
    PIX_FMT_YUVJ420P,
    PIX_FMT_NONE
};

void ff_h264_write_back_intra_pred_mode(H264Context *h){
    int8_t *mode= h->intra4x4_pred_mode + h->mb2br_xy[h->mb_xy];

    AV_COPY32(mode, h->intra4x4_pred_mode_cache + 4 + 8*4);
    mode[4]= h->intra4x4_pred_mode_cache[7+8*3];
    mode[5]= h->intra4x4_pred_mode_cache[7+8*2];
    mode[6]= h->intra4x4_pred_mode_cache[7+8*1];
}

/**
 * checks if the top & left blocks are available if needed & changes the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra4x4_pred_mode(H264Context *h){
    MpegEncContext * const s = &h->s;
    static const int8_t top [12]= {-1, 0,LEFT_DC_PRED,-1,-1,-1,-1,-1, 0};
    static const int8_t left[12]= { 0,-1, TOP_DC_PRED, 0,-1,-1,-1, 0,-1,DC_128_PRED};
    int i;

    if(!(h->top_samples_available&0x8000)){
        for(i=0; i<4; i++){
            int status= top[ h->intra4x4_pred_mode_cache[scan8[0] + i] ];
            if(status<0){
                av_log(h->s.avctx, AV_LOG_ERROR, "top block unavailable for requested intra4x4 mode %d at %d %d\n", status, s->mb_x, s->mb_y);
                return -1;
            } else if(status){
                h->intra4x4_pred_mode_cache[scan8[0] + i]= status;
            }
        }
    }

    if((h->left_samples_available&0x8888)!=0x8888){
        static const int mask[4]={0x8000,0x2000,0x80,0x20};
        for(i=0; i<4; i++){
            if(!(h->left_samples_available&mask[i])){
                int status= left[ h->intra4x4_pred_mode_cache[scan8[0] + 8*i] ];
                if(status<0){
                    av_log(h->s.avctx, AV_LOG_ERROR, "left block unavailable for requested intra4x4 mode %d at %d %d\n", status, s->mb_x, s->mb_y);
                    return -1;
                } else if(status){
                    h->intra4x4_pred_mode_cache[scan8[0] + 8*i]= status;
                }
            }
        }
    }

    return 0;
} //FIXME cleanup like ff_h264_check_intra_pred_mode

/**
 * checks if the top & left blocks are available if needed & changes the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra_pred_mode(H264Context *h, int mode){
    MpegEncContext * const s = &h->s;
    static const int8_t top [7]= {LEFT_DC_PRED8x8, 1,-1,-1};
    static const int8_t left[7]= { TOP_DC_PRED8x8,-1, 2,-1,DC_128_PRED8x8};

    if(mode > 6U) {
        av_log(h->s.avctx, AV_LOG_ERROR, "out of range intra chroma pred mode at %d %d\n", s->mb_x, s->mb_y);
        return -1;
    }

    if(!(h->top_samples_available&0x8000)){
        mode= top[ mode ];
        if(mode<0){
            av_log(h->s.avctx, AV_LOG_ERROR, "top block unavailable for requested intra mode at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }
    }

    if((h->left_samples_available&0x8080) != 0x8080){
        mode= left[ mode ];
        if(h->left_samples_available&0x8080){ //mad cow disease mode, aka MBAFF + constrained_intra_pred
            mode= ALZHEIMER_DC_L0T_PRED8x8 + (!(h->left_samples_available&0x8000)) + 2*(mode == DC_128_PRED8x8);
        }
        if(mode<0){
            av_log(h->s.avctx, AV_LOG_ERROR, "left block unavailable for requested intra mode at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }
    }

    return mode;
}

const uint8_t *ff_h264_decode_nal(H264Context *h, const uint8_t *src, int *dst_length, int *consumed, int length){
    int i, si, di;
    uint8_t *dst;
    int bufidx;

//    src[0]&0x80;                //forbidden bit
    h->nal_ref_idc= src[0]>>5;
    h->nal_unit_type= src[0]&0x1F;

    src++; length--;
#if 0
    for(i=0; i<length; i++)
        printf("%2X ", src[i]);
#endif

#if HAVE_FAST_UNALIGNED
# if HAVE_FAST_64BIT
#   define RS 7
    for(i=0; i+1<length; i+=9){
        if(!((~AV_RN64A(src+i) & (AV_RN64A(src+i) - 0x0100010001000101ULL)) & 0x8000800080008080ULL))
# else
#   define RS 3
    for(i=0; i+1<length; i+=5){
        if(!((~AV_RN32A(src+i) & (AV_RN32A(src+i) - 0x01000101U)) & 0x80008080U))
# endif
            continue;
        if(i>0 && !src[i]) i--;
        while(src[i]) i++;
#else
#   define RS 0
    for(i=0; i+1<length; i+=2){
        if(src[i]) continue;
        if(i>0 && src[i-1]==0) i--;
#endif
        if(i+2<length && src[i+1]==0 && src[i+2]<=3){
            if(src[i+2]!=3){
                /* startcode, so we must be past the end */
                length=i;
            }
            break;
        }
        i-= RS;
    }

    if(i>=length-1){ //no escaped 0
        *dst_length= length;
        *consumed= length+1; //+1 for the header
        return src;
    }

    bufidx = h->nal_unit_type == NAL_DPC ? 1 : 0; // use second escape buffer for inter data
    av_fast_malloc(&h->rbsp_buffer[bufidx], &h->rbsp_buffer_size[bufidx], length+FF_INPUT_BUFFER_PADDING_SIZE);
    dst= h->rbsp_buffer[bufidx];

    if (dst == NULL){
        return NULL;
    }

//printf("decoding esc\n");
    memcpy(dst, src, i);
    si=di=i;
    while(si+2<length){
        //remove escapes (very rare 1:2^22)
        if(src[si+2]>3){
            dst[di++]= src[si++];
            dst[di++]= src[si++];
        }else if(src[si]==0 && src[si+1]==0){
            if(src[si+2]==3){ //escape
                dst[di++]= 0;
                dst[di++]= 0;
                si+=3;
                continue;
            }else //next start code
                goto nsc;
        }

        dst[di++]= src[si++];
    }
    while(si<length)
        dst[di++]= src[si++];
nsc:

    memset(dst+di, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    *dst_length= di;
    *consumed= si + 1;//+1 for the header
//FIXME store exact number of bits in the getbitcontext (it is needed for decoding)
    return dst;
}

int ff_h264_decode_rbsp_trailing(H264Context *h, const uint8_t *src){
    int v= *src;
    int r;

    tprintf(h->s.avctx, "rbsp trailing %X\n", v);

    for(r=1; r<9; r++){
        if(v&1) return r;
        v>>=1;
    }
    return 0;
}

static inline void mc_dir_part(H264Context *h, Picture *pic, int n, int square, int chroma_height, int delta, int list,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int src_x_offset, int src_y_offset,
                           qpel_mc_func *qpix_op, h264_chroma_mc_func chroma_op){
    MpegEncContext * const s = &h->s;
    const int mx= h->mv_cache[list][ scan8[n] ][0] + src_x_offset*8;
    int my=       h->mv_cache[list][ scan8[n] ][1] + src_y_offset*8;
    const int luma_xy= (mx&3) + ((my&3)<<2);
    uint8_t * src_y = pic->data[0] + (mx>>2) + (my>>2)*h->mb_linesize;
    uint8_t * src_cb, * src_cr;
    int extra_width= h->emu_edge_width;
    int extra_height= h->emu_edge_height;
    int emu=0;
    const int full_mx= mx>>2;
    const int full_my= my>>2;
    const int pic_width  = 16*s->mb_width;
    const int pic_height = 16*s->mb_height >> MB_FIELD;

    if(mx&7) extra_width -= 3;
    if(my&7) extra_height -= 3;

    if(   full_mx < 0-extra_width
       || full_my < 0-extra_height
       || full_mx + 16/*FIXME*/ > pic_width + extra_width
       || full_my + 16/*FIXME*/ > pic_height + extra_height){
        ff_emulated_edge_mc(s->edge_emu_buffer, src_y - 2 - 2*h->mb_linesize, h->mb_linesize, 16+5, 16+5/*FIXME*/, full_mx-2, full_my-2, pic_width, pic_height);
            src_y= s->edge_emu_buffer + 2 + 2*h->mb_linesize;
        emu=1;
    }

    qpix_op[luma_xy](dest_y, src_y, h->mb_linesize); //FIXME try variable height perhaps?
    if(!square){
        qpix_op[luma_xy](dest_y + delta, src_y + delta, h->mb_linesize);
    }

    if(CONFIG_GRAY && s->flags&CODEC_FLAG_GRAY) return;

    if(MB_FIELD){
        // chroma offset when predicting from a field of opposite parity
        my += 2 * ((s->mb_y & 1) - (pic->reference - 1));
        emu |= (my>>3) < 0 || (my>>3) + 8 >= (pic_height>>1);
    }
    src_cb= pic->data[1] + (mx>>3) + (my>>3)*h->mb_uvlinesize;
    src_cr= pic->data[2] + (mx>>3) + (my>>3)*h->mb_uvlinesize;

    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, src_cb, h->mb_uvlinesize, 9, 9/*FIXME*/, (mx>>3), (my>>3), pic_width>>1, pic_height>>1);
            src_cb= s->edge_emu_buffer;
    }
    chroma_op(dest_cb, src_cb, h->mb_uvlinesize, chroma_height, mx&7, my&7);

    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, src_cr, h->mb_uvlinesize, 9, 9/*FIXME*/, (mx>>3), (my>>3), pic_width>>1, pic_height>>1);
            src_cr= s->edge_emu_buffer;
    }
    chroma_op(dest_cr, src_cr, h->mb_uvlinesize, chroma_height, mx&7, my&7);
}

static inline void mc_part_std(H264Context *h, int n, int square, int chroma_height, int delta,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int x_offset, int y_offset,
                           qpel_mc_func *qpix_put, h264_chroma_mc_func chroma_put,
                           qpel_mc_func *qpix_avg, h264_chroma_mc_func chroma_avg,
                           int list0, int list1){
    MpegEncContext * const s = &h->s;
    qpel_mc_func *qpix_op=  qpix_put;
    h264_chroma_mc_func chroma_op= chroma_put;

    dest_y  += 2*x_offset + 2*y_offset*h->  mb_linesize;
    dest_cb +=   x_offset +   y_offset*h->mb_uvlinesize;
    dest_cr +=   x_offset +   y_offset*h->mb_uvlinesize;
    x_offset += 8*s->mb_x;
    y_offset += 8*(s->mb_y >> MB_FIELD);

    if(list0){
        Picture *ref= &h->ref_list[0][ h->ref_cache[0][ scan8[n] ] ];
        mc_dir_part(h, ref, n, square, chroma_height, delta, 0,
                           dest_y, dest_cb, dest_cr, x_offset, y_offset,
                           qpix_op, chroma_op);

        qpix_op=  qpix_avg;
        chroma_op= chroma_avg;
    }

    if(list1){
        Picture *ref= &h->ref_list[1][ h->ref_cache[1][ scan8[n] ] ];
        mc_dir_part(h, ref, n, square, chroma_height, delta, 1,
                           dest_y, dest_cb, dest_cr, x_offset, y_offset,
                           qpix_op, chroma_op);
    }
}

static inline void mc_part_weighted(H264Context *h, int n, int square, int chroma_height, int delta,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int x_offset, int y_offset,
                           qpel_mc_func *qpix_put, h264_chroma_mc_func chroma_put,
                           h264_weight_func luma_weight_op, h264_weight_func chroma_weight_op,
                           h264_biweight_func luma_weight_avg, h264_biweight_func chroma_weight_avg,
                           int list0, int list1){
    MpegEncContext * const s = &h->s;

    dest_y  += 2*x_offset + 2*y_offset*h->  mb_linesize;
    dest_cb +=   x_offset +   y_offset*h->mb_uvlinesize;
    dest_cr +=   x_offset +   y_offset*h->mb_uvlinesize;
    x_offset += 8*s->mb_x;
    y_offset += 8*(s->mb_y >> MB_FIELD);

    if(list0 && list1){
        /* don't optimize for luma-only case, since B-frames usually
         * use implicit weights => chroma too. */
        uint8_t *tmp_cb = s->obmc_scratchpad;
        uint8_t *tmp_cr = s->obmc_scratchpad + 8;
        uint8_t *tmp_y  = s->obmc_scratchpad + 8*h->mb_uvlinesize;
        int refn0 = h->ref_cache[0][ scan8[n] ];
        int refn1 = h->ref_cache[1][ scan8[n] ];

        mc_dir_part(h, &h->ref_list[0][refn0], n, square, chroma_height, delta, 0,
                    dest_y, dest_cb, dest_cr,
                    x_offset, y_offset, qpix_put, chroma_put);
        mc_dir_part(h, &h->ref_list[1][refn1], n, square, chroma_height, delta, 1,
                    tmp_y, tmp_cb, tmp_cr,
                    x_offset, y_offset, qpix_put, chroma_put);

        if(h->use_weight == 2){
            int weight0 = h->implicit_weight[refn0][refn1][s->mb_y&1];
            int weight1 = 64 - weight0;
            luma_weight_avg(  dest_y,  tmp_y,  h->  mb_linesize, 5, weight0, weight1, 0);
            chroma_weight_avg(dest_cb, tmp_cb, h->mb_uvlinesize, 5, weight0, weight1, 0);
            chroma_weight_avg(dest_cr, tmp_cr, h->mb_uvlinesize, 5, weight0, weight1, 0);
        }else{
            luma_weight_avg(dest_y, tmp_y, h->mb_linesize, h->luma_log2_weight_denom,
                            h->luma_weight[refn0][0][0] , h->luma_weight[refn1][1][0],
                            h->luma_weight[refn0][0][1] + h->luma_weight[refn1][1][1]);
            chroma_weight_avg(dest_cb, tmp_cb, h->mb_uvlinesize, h->chroma_log2_weight_denom,
                            h->chroma_weight[refn0][0][0][0] , h->chroma_weight[refn1][1][0][0],
                            h->chroma_weight[refn0][0][0][1] + h->chroma_weight[refn1][1][0][1]);
            chroma_weight_avg(dest_cr, tmp_cr, h->mb_uvlinesize, h->chroma_log2_weight_denom,
                            h->chroma_weight[refn0][0][1][0] , h->chroma_weight[refn1][1][1][0],
                            h->chroma_weight[refn0][0][1][1] + h->chroma_weight[refn1][1][1][1]);
        }
    }else{
        int list = list1 ? 1 : 0;
        int refn = h->ref_cache[list][ scan8[n] ];
        Picture *ref= &h->ref_list[list][refn];
        mc_dir_part(h, ref, n, square, chroma_height, delta, list,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put, chroma_put);

        luma_weight_op(dest_y, h->mb_linesize, h->luma_log2_weight_denom,
                       h->luma_weight[refn][list][0], h->luma_weight[refn][list][1]);
        if(h->use_weight_chroma){
            chroma_weight_op(dest_cb, h->mb_uvlinesize, h->chroma_log2_weight_denom,
                             h->chroma_weight[refn][list][0][0], h->chroma_weight[refn][list][0][1]);
            chroma_weight_op(dest_cr, h->mb_uvlinesize, h->chroma_log2_weight_denom,
                             h->chroma_weight[refn][list][1][0], h->chroma_weight[refn][list][1][1]);
        }
    }
}

static inline void mc_part(H264Context *h, int n, int square, int chroma_height, int delta,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int x_offset, int y_offset,
                           qpel_mc_func *qpix_put, h264_chroma_mc_func chroma_put,
                           qpel_mc_func *qpix_avg, h264_chroma_mc_func chroma_avg,
                           h264_weight_func *weight_op, h264_biweight_func *weight_avg,
                           int list0, int list1){
    if((h->use_weight==2 && list0 && list1
        && (h->implicit_weight[ h->ref_cache[0][scan8[n]] ][ h->ref_cache[1][scan8[n]] ][h->s.mb_y&1] != 32))
       || h->use_weight==1)
        mc_part_weighted(h, n, square, chroma_height, delta, dest_y, dest_cb, dest_cr,
                         x_offset, y_offset, qpix_put, chroma_put,
                         weight_op[0], weight_op[3], weight_avg[0], weight_avg[3], list0, list1);
    else
        mc_part_std(h, n, square, chroma_height, delta, dest_y, dest_cb, dest_cr,
                    x_offset, y_offset, qpix_put, chroma_put, qpix_avg, chroma_avg, list0, list1);
}

static inline void prefetch_motion(H264Context *h, int list){
    /* fetch pixels for estimated mv 4 macroblocks ahead
     * optimized for 64byte cache lines */
    MpegEncContext * const s = &h->s;
    const int refn = h->ref_cache[list][scan8[0]];
    if(refn >= 0){
        const int mx= (h->mv_cache[list][scan8[0]][0]>>2) + 16*s->mb_x + 8;
        const int my= (h->mv_cache[list][scan8[0]][1]>>2) + 16*s->mb_y;
        uint8_t **src= h->ref_list[list][refn].data;
        int off= mx + (my + (s->mb_x&3)*4)*h->mb_linesize + 64;
        s->dsp.prefetch(src[0]+off, s->linesize, 4);
        off= (mx>>1) + ((my>>1) + (s->mb_x&7))*s->uvlinesize + 64;
        s->dsp.prefetch(src[1]+off, src[2]-src[1], 2);
    }
}

static void hl_motion(H264Context *h, uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                      qpel_mc_func (*qpix_put)[16], h264_chroma_mc_func (*chroma_put),
                      qpel_mc_func (*qpix_avg)[16], h264_chroma_mc_func (*chroma_avg),
                      h264_weight_func *weight_op, h264_biweight_func *weight_avg){
    MpegEncContext * const s = &h->s;
    const int mb_xy= h->mb_xy;
    const int mb_type= s->current_picture.mb_type[mb_xy];

    assert(IS_INTER(mb_type));

    prefetch_motion(h, 0);

    if(IS_16X16(mb_type)){
        mc_part(h, 0, 1, 8, 0, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[0], chroma_put[0], qpix_avg[0], chroma_avg[0],
                weight_op, weight_avg,
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
    }else if(IS_16X8(mb_type)){
        mc_part(h, 0, 0, 4, 8, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                &weight_op[1], &weight_avg[1],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        mc_part(h, 8, 0, 4, 8, dest_y, dest_cb, dest_cr, 0, 4,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                &weight_op[1], &weight_avg[1],
                IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1));
    }else if(IS_8X16(mb_type)){
        mc_part(h, 0, 0, 8, 8*h->mb_linesize, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                &weight_op[2], &weight_avg[2],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        mc_part(h, 4, 0, 8, 8*h->mb_linesize, dest_y, dest_cb, dest_cr, 4, 0,
                qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                &weight_op[2], &weight_avg[2],
                IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1));
    }else{
        int i;

        assert(IS_8X8(mb_type));

        for(i=0; i<4; i++){
            const int sub_mb_type= h->sub_mb_type[i];
            const int n= 4*i;
            int x_offset= (i&1)<<2;
            int y_offset= (i&2)<<1;

            if(IS_SUB_8X8(sub_mb_type)){
                mc_part(h, n, 1, 4, 0, dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                    &weight_op[3], &weight_avg[3],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else if(IS_SUB_8X4(sub_mb_type)){
                mc_part(h, n  , 0, 2, 4, dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                    &weight_op[4], &weight_avg[4],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                mc_part(h, n+2, 0, 2, 4, dest_y, dest_cb, dest_cr, x_offset, y_offset+2,
                    qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                    &weight_op[4], &weight_avg[4],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else if(IS_SUB_4X8(sub_mb_type)){
                mc_part(h, n  , 0, 4, 4*h->mb_linesize, dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                    &weight_op[5], &weight_avg[5],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                mc_part(h, n+1, 0, 4, 4*h->mb_linesize, dest_y, dest_cb, dest_cr, x_offset+2, y_offset,
                    qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                    &weight_op[5], &weight_avg[5],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else{
                int j;
                assert(IS_SUB_4X4(sub_mb_type));
                for(j=0; j<4; j++){
                    int sub_x_offset= x_offset + 2*(j&1);
                    int sub_y_offset= y_offset +   (j&2);
                    mc_part(h, n+j, 1, 2, 0, dest_y, dest_cb, dest_cr, sub_x_offset, sub_y_offset,
                        qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                        &weight_op[6], &weight_avg[6],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                }
            }
        }
    }

    prefetch_motion(h, 1);
}


static void free_tables(H264Context *h){
    int i;
    H264Context *hx;
    av_freep(&h->intra4x4_pred_mode);
    av_freep(&h->chroma_pred_mode_table);
    av_freep(&h->cbp_table);
    av_freep(&h->mvd_table[0]);
    av_freep(&h->mvd_table[1]);
    av_freep(&h->direct_table);
    av_freep(&h->non_zero_count);
    av_freep(&h->slice_table_base);
    h->slice_table= NULL;
    av_freep(&h->list_counts);

    av_freep(&h->mb2b_xy);
    av_freep(&h->mb2br_xy);

    for(i = 0; i < MAX_THREADS; i++) {
        hx = h->thread_context[i];
        if(!hx) continue;
        av_freep(&hx->top_borders[1]);
        av_freep(&hx->top_borders[0]);
        av_freep(&hx->s.obmc_scratchpad);
        av_freep(&hx->rbsp_buffer[1]);
        av_freep(&hx->rbsp_buffer[0]);
        hx->rbsp_buffer_size[0] = 0;
        hx->rbsp_buffer_size[1] = 0;
        if (i) av_freep(&h->thread_context[i]);
    }
}

static void init_dequant8_coeff_table(H264Context *h){
    int i,q,x;
    h->dequant8_coeff[0] = h->dequant8_buffer[0];
    h->dequant8_coeff[1] = h->dequant8_buffer[1];

    for(i=0; i<2; i++ ){
        if(i && !memcmp(h->pps.scaling_matrix8[0], h->pps.scaling_matrix8[1], 64*sizeof(uint8_t))){
            h->dequant8_coeff[1] = h->dequant8_buffer[0];
            break;
        }

        for(q=0; q<52; q++){
            int shift = div6[q];
            int idx = rem6[q];
            for(x=0; x<64; x++)
                h->dequant8_coeff[i][q][(x>>3)|((x&7)<<3)] =
                    ((uint32_t)dequant8_coeff_init[idx][ dequant8_coeff_init_scan[((x>>1)&12) | (x&3)] ] *
                    h->pps.scaling_matrix8[i][x]) << shift;
        }
    }
}

static void init_dequant4_coeff_table(H264Context *h){
    int i,j,q,x;
    for(i=0; i<6; i++ ){
        h->dequant4_coeff[i] = h->dequant4_buffer[i];
        for(j=0; j<i; j++){
            if(!memcmp(h->pps.scaling_matrix4[j], h->pps.scaling_matrix4[i], 16*sizeof(uint8_t))){
                h->dequant4_coeff[i] = h->dequant4_buffer[j];
                break;
            }
        }
        if(j<i)
            continue;

        for(q=0; q<52; q++){
            int shift = div6[q] + 2;
            int idx = rem6[q];
            for(x=0; x<16; x++)
                h->dequant4_coeff[i][q][(x>>2)|((x<<2)&0xF)] =
                    ((uint32_t)dequant4_coeff_init[idx][(x&1) + ((x>>2)&1)] *
                    h->pps.scaling_matrix4[i][x]) << shift;
        }
    }
}

static void init_dequant_tables(H264Context *h){
    int i,x;
    init_dequant4_coeff_table(h);
    if(h->pps.transform_8x8_mode)
        init_dequant8_coeff_table(h);
    if(h->sps.transform_bypass){
        for(i=0; i<6; i++)
            for(x=0; x<16; x++)
                h->dequant4_coeff[i][0][x] = 1<<6;
        if(h->pps.transform_8x8_mode)
            for(i=0; i<2; i++)
                for(x=0; x<64; x++)
                    h->dequant8_coeff[i][0][x] = 1<<6;
    }
}


int ff_h264_alloc_tables(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int big_mb_num= s->mb_stride * (s->mb_height+1);
    const int row_mb_num= 2*s->mb_stride*s->avctx->thread_count;
    int x,y;

    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->intra4x4_pred_mode, row_mb_num * 8  * sizeof(uint8_t), fail)

    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->non_zero_count    , big_mb_num * 32 * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->slice_table_base  , (big_mb_num+s->mb_stride) * sizeof(*h->slice_table_base), fail)
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->cbp_table, big_mb_num * sizeof(uint16_t), fail)

    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->chroma_pred_mode_table, big_mb_num * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->mvd_table[0], 16*row_mb_num * sizeof(uint8_t), fail);
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->mvd_table[1], 16*row_mb_num * sizeof(uint8_t), fail);
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->direct_table, 4*big_mb_num * sizeof(uint8_t) , fail);
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->list_counts, big_mb_num * sizeof(uint8_t), fail)

    memset(h->slice_table_base, -1, (big_mb_num+s->mb_stride)  * sizeof(*h->slice_table_base));
    h->slice_table= h->slice_table_base + s->mb_stride*2 + 1;

    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->mb2b_xy  , big_mb_num * sizeof(uint32_t), fail);
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->mb2br_xy , big_mb_num * sizeof(uint32_t), fail);
    for(y=0; y<s->mb_height; y++){
        for(x=0; x<s->mb_width; x++){
            const int mb_xy= x + y*s->mb_stride;
            const int b_xy = 4*x + 4*y*h->b_stride;

            h->mb2b_xy [mb_xy]= b_xy;
            h->mb2br_xy[mb_xy]= 8*(FMO ? mb_xy : (mb_xy % (2*s->mb_stride)));
        }
    }

    s->obmc_scratchpad = NULL;

    if(!h->dequant4_coeff[0])
        init_dequant_tables(h);

    return 0;
fail:
    free_tables(h);
    return -1;
}

/**
 * Mimic alloc_tables(), but for every context thread.
 */
static void clone_tables(H264Context *dst, H264Context *src, int i){
    MpegEncContext * const s = &src->s;
    dst->intra4x4_pred_mode       = src->intra4x4_pred_mode + i*8*2*s->mb_stride;
    dst->non_zero_count           = src->non_zero_count;
    dst->slice_table              = src->slice_table;
    dst->cbp_table                = src->cbp_table;
    dst->mb2b_xy                  = src->mb2b_xy;
    dst->mb2br_xy                 = src->mb2br_xy;
    dst->chroma_pred_mode_table   = src->chroma_pred_mode_table;
    dst->mvd_table[0]             = src->mvd_table[0] + i*8*2*s->mb_stride;
    dst->mvd_table[1]             = src->mvd_table[1] + i*8*2*s->mb_stride;
    dst->direct_table             = src->direct_table;
    dst->list_counts              = src->list_counts;

    dst->s.obmc_scratchpad = NULL;
    ff_h264_pred_init(&dst->hpc, src->s.codec_id);
}

/**
 * Init context
 * Allocate buffers which are not shared amongst multiple threads.
 */
static int context_init(H264Context *h){
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->top_borders[0], h->s.mb_width * (16+8+8) * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->top_borders[1], h->s.mb_width * (16+8+8) * sizeof(uint8_t), fail)

    h->ref_cache[0][scan8[5 ]+1] = h->ref_cache[0][scan8[7 ]+1] = h->ref_cache[0][scan8[13]+1] =
    h->ref_cache[1][scan8[5 ]+1] = h->ref_cache[1][scan8[7 ]+1] = h->ref_cache[1][scan8[13]+1] = PART_NOT_AVAILABLE;

    return 0;
fail:
    return -1; // free_tables will clean up for us
}

static int decode_nal_units(H264Context *h, const uint8_t *buf, int buf_size);

static av_cold void common_init(H264Context *h){
    MpegEncContext * const s = &h->s;

    s->width = s->avctx->width;
    s->height = s->avctx->height;
    s->codec_id= s->avctx->codec->id;

    ff_h264dsp_init(&h->h264dsp);
    ff_h264_pred_init(&h->hpc, s->codec_id);

    h->dequant_coeff_pps= -1;
    s->unrestricted_mv=1;
    s->decode=1; //FIXME

    dsputil_init(&s->dsp, s->avctx); // needed so that idct permutation is known early

    memset(h->pps.scaling_matrix4, 16, 6*16*sizeof(uint8_t));
    memset(h->pps.scaling_matrix8, 16, 2*64*sizeof(uint8_t));
}

int ff_h264_decode_extradata(H264Context *h)
{
    AVCodecContext *avctx = h->s.avctx;

    if(*(char *)avctx->extradata == 1){
        int i, cnt, nalsize;
        unsigned char *p = avctx->extradata;

        h->is_avc = 1;

        if(avctx->extradata_size < 7) {
            av_log(avctx, AV_LOG_ERROR, "avcC too short\n");
            return -1;
        }
        /* sps and pps in the avcC always have length coded with 2 bytes,
           so put a fake nal_length_size = 2 while parsing them */
        h->nal_length_size = 2;
        // Decode sps from avcC
        cnt = *(p+5) & 0x1f; // Number of sps
        p += 6;
        for (i = 0; i < cnt; i++) {
            nalsize = AV_RB16(p) + 2;
            if(decode_nal_units(h, p, nalsize) < 0) {
                av_log(avctx, AV_LOG_ERROR, "Decoding sps %d from avcC failed\n", i);
                return -1;
            }
            p += nalsize;
        }
        // Decode pps from avcC
        cnt = *(p++); // Number of pps
        for (i = 0; i < cnt; i++) {
            nalsize = AV_RB16(p) + 2;
            if(decode_nal_units(h, p, nalsize)  != nalsize) {
                av_log(avctx, AV_LOG_ERROR, "Decoding pps %d from avcC failed\n", i);
                return -1;
            }
            p += nalsize;
        }
        // Now store right nal length size, that will be use to parse all other nals
        h->nal_length_size = ((*(((char*)(avctx->extradata))+4))&0x03)+1;
    } else {
        h->is_avc = 0;
        if(decode_nal_units(h, avctx->extradata, avctx->extradata_size) < 0)
            return -1;
    }
    return 0;
}

av_cold int ff_h264_decode_init(AVCodecContext *avctx){
    H264Context *h= avctx->priv_data;
    MpegEncContext * const s = &h->s;

    MPV_decode_defaults(s);

    s->avctx = avctx;
    common_init(h);

    s->out_format = FMT_H264;
    s->workaround_bugs= avctx->workaround_bugs;

    // set defaults
//    s->decode_mb= ff_h263_decode_mb;
    s->quarter_sample = 1;
    if(!avctx->has_b_frames)
    s->low_delay= 1;

    avctx->chroma_sample_location = AVCHROMA_LOC_LEFT;

    ff_h264_decode_init_vlc();

    h->thread_context[0] = h;
    h->outputed_poc = INT_MIN;
    h->prev_poc_msb= 1<<16;
    h->x264_build = -1;
    ff_h264_reset_sei(h);
    if(avctx->codec_id == CODEC_ID_H264){
        if(avctx->ticks_per_frame == 1){
            s->avctx->time_base.den *=2;
        }
        avctx->ticks_per_frame = 2;
    }

    if(avctx->extradata_size > 0 && avctx->extradata &&
        ff_h264_decode_extradata(h))
        return -1;

    if(h->sps.bitstream_restriction_flag && s->avctx->has_b_frames < h->sps.num_reorder_frames){
        s->avctx->has_b_frames = h->sps.num_reorder_frames;
        s->low_delay = 0;
    }

    return 0;
}

int ff_h264_frame_start(H264Context *h){
    MpegEncContext * const s = &h->s;
    int i;

    if(MPV_frame_start(s, s->avctx) < 0)
        return -1;
    ff_er_frame_start(s);
    /*
     * MPV_frame_start uses pict_type to derive key_frame.
     * This is incorrect for H.264; IDR markings must be used.
     * Zero here; IDR markings per slice in frame or fields are ORed in later.
     * See decode_nal_units().
     */
    s->current_picture_ptr->key_frame= 0;
    s->current_picture_ptr->mmco_reset= 0;

    assert(s->linesize && s->uvlinesize);

    for(i=0; i<16; i++){
        h->block_offset[i]= 4*((scan8[i] - scan8[0])&7) + 4*s->linesize*((scan8[i] - scan8[0])>>3);
        h->block_offset[24+i]= 4*((scan8[i] - scan8[0])&7) + 8*s->linesize*((scan8[i] - scan8[0])>>3);
    }
    for(i=0; i<4; i++){
        h->block_offset[16+i]=
        h->block_offset[20+i]= 4*((scan8[i] - scan8[0])&7) + 4*s->uvlinesize*((scan8[i] - scan8[0])>>3);
        h->block_offset[24+16+i]=
        h->block_offset[24+20+i]= 4*((scan8[i] - scan8[0])&7) + 8*s->uvlinesize*((scan8[i] - scan8[0])>>3);
    }

    /* can't be in alloc_tables because linesize isn't known there.
     * FIXME: redo bipred weight to not require extra buffer? */
    for(i = 0; i < s->avctx->thread_count; i++)
        if(h->thread_context[i] && !h->thread_context[i]->s.obmc_scratchpad)
            h->thread_context[i]->s.obmc_scratchpad = av_malloc(16*2*s->linesize + 8*2*s->uvlinesize);

    /* some macroblocks can be accessed before they're available in case of lost slices, mbaff or threading*/
    memset(h->slice_table, -1, (s->mb_height*s->mb_stride-1) * sizeof(*h->slice_table));

//    s->decode= (s->flags&CODEC_FLAG_PSNR) || !s->encoding || s->current_picture.reference /*|| h->contains_intra*/ || 1;

    // We mark the current picture as non-reference after allocating it, so
    // that if we break out due to an error it can be released automatically
    // in the next MPV_frame_start().
    // SVQ3 as well as most other codecs have only last/next/current and thus
    // get released even with set reference, besides SVQ3 and others do not
    // mark frames as reference later "naturally".
    if(s->codec_id != CODEC_ID_SVQ3)
        s->current_picture_ptr->reference= 0;

    s->current_picture_ptr->field_poc[0]=
    s->current_picture_ptr->field_poc[1]= INT_MAX;
    assert(s->current_picture_ptr->long_ref==0);

    return 0;
}

static inline void backup_mb_border(H264Context *h, uint8_t *src_y, uint8_t *src_cb, uint8_t *src_cr, int linesize, int uvlinesize, int simple){
    MpegEncContext * const s = &h->s;
    uint8_t *top_border;
    int top_idx = 1;

    src_y  -=   linesize;
    src_cb -= uvlinesize;
    src_cr -= uvlinesize;

    if(!simple && FRAME_MBAFF){
        if(s->mb_y&1){
            if(!MB_MBAFF){
                top_border = h->top_borders[0][s->mb_x];
                AV_COPY128(top_border, src_y + 15*linesize);
                if(simple || !CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
                    AV_COPY64(top_border+16, src_cb+7*uvlinesize);
                    AV_COPY64(top_border+24, src_cr+7*uvlinesize);
                }
            }
        }else if(MB_MBAFF){
            top_idx = 0;
        }else
            return;
    }

    top_border = h->top_borders[top_idx][s->mb_x];
    // There are two lines saved, the line above the the top macroblock of a pair,
    // and the line above the bottom macroblock
    AV_COPY128(top_border, src_y + 16*linesize);

    if(simple || !CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
        AV_COPY64(top_border+16, src_cb+8*uvlinesize);
        AV_COPY64(top_border+24, src_cr+8*uvlinesize);
    }
}

static inline void xchg_mb_border(H264Context *h, uint8_t *src_y, uint8_t *src_cb, uint8_t *src_cr, int linesize, int uvlinesize, int xchg, int simple){
    MpegEncContext * const s = &h->s;
    int deblock_left;
    int deblock_top;
    int top_idx = 1;
    uint8_t *top_border_m1;
    uint8_t *top_border;

    if(!simple && FRAME_MBAFF){
        if(s->mb_y&1){
            if(!MB_MBAFF)
                return;
        }else{
            top_idx = MB_MBAFF ? 0 : 1;
        }
    }

    if(h->deblocking_filter == 2) {
        deblock_left = h->left_type[0];
        deblock_top  = h->top_type;
    } else {
        deblock_left = (s->mb_x > 0);
        deblock_top =  (s->mb_y > !!MB_FIELD);
    }

    src_y  -=   linesize + 1;
    src_cb -= uvlinesize + 1;
    src_cr -= uvlinesize + 1;

    top_border_m1 = h->top_borders[top_idx][s->mb_x-1];
    top_border    = h->top_borders[top_idx][s->mb_x];

#define XCHG(a,b,xchg)\
if (xchg) AV_SWAP64(b,a);\
else      AV_COPY64(b,a);

    if(deblock_top){
        if(deblock_left){
            XCHG(top_border_m1+8, src_y -7, 1);
        }
        XCHG(top_border+0, src_y +1, xchg);
        XCHG(top_border+8, src_y +9, 1);
        if(s->mb_x+1 < s->mb_width){
            XCHG(h->top_borders[top_idx][s->mb_x+1], src_y +17, 1);
        }
    }

    if(simple || !CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
        if(deblock_top){
            if(deblock_left){
                XCHG(top_border_m1+16, src_cb -7, 1);
                XCHG(top_border_m1+24, src_cr -7, 1);
            }
            XCHG(top_border+16, src_cb+1, 1);
            XCHG(top_border+24, src_cr+1, 1);
        }
    }
}

static av_always_inline void hl_decode_mb_internal(H264Context *h, int simple){
    MpegEncContext * const s = &h->s;
    const int mb_x= s->mb_x;
    const int mb_y= s->mb_y;
    const int mb_xy= h->mb_xy;
    const int mb_type= s->current_picture.mb_type[mb_xy];
    uint8_t  *dest_y, *dest_cb, *dest_cr;
    int linesize, uvlinesize /*dct_offset*/;
    int i;
    int *block_offset = &h->block_offset[0];
    const int transform_bypass = !simple && (s->qscale == 0 && h->sps.transform_bypass);
    /* is_h264 should always be true if SVQ3 is disabled. */
    const int is_h264 = !CONFIG_SVQ3_DECODER || simple || s->codec_id == CODEC_ID_H264;
    void (*idct_add)(uint8_t *dst, DCTELEM *block, int stride);
    void (*idct_dc_add)(uint8_t *dst, DCTELEM *block, int stride);

    dest_y  = s->current_picture.data[0] + (mb_x + mb_y * s->linesize  ) * 16;
    dest_cb = s->current_picture.data[1] + (mb_x + mb_y * s->uvlinesize) * 8;
    dest_cr = s->current_picture.data[2] + (mb_x + mb_y * s->uvlinesize) * 8;

    s->dsp.prefetch(dest_y + (s->mb_x&3)*4*s->linesize + 64, s->linesize, 4);
    s->dsp.prefetch(dest_cb + (s->mb_x&7)*s->uvlinesize + 64, dest_cr - dest_cb, 2);

    h->list_counts[mb_xy]= h->list_count;

    if (!simple && MB_FIELD) {
        linesize   = h->mb_linesize   = s->linesize * 2;
        uvlinesize = h->mb_uvlinesize = s->uvlinesize * 2;
        block_offset = &h->block_offset[24];
        if(mb_y&1){ //FIXME move out of this function?
            dest_y -= s->linesize*15;
            dest_cb-= s->uvlinesize*7;
            dest_cr-= s->uvlinesize*7;
        }
        if(FRAME_MBAFF) {
            int list;
            for(list=0; list<h->list_count; list++){
                if(!USES_LIST(mb_type, list))
                    continue;
                if(IS_16X16(mb_type)){
                    int8_t *ref = &h->ref_cache[list][scan8[0]];
                    fill_rectangle(ref, 4, 4, 8, (16+*ref)^(s->mb_y&1), 1);
                }else{
                    for(i=0; i<16; i+=4){
                        int ref = h->ref_cache[list][scan8[i]];
                        if(ref >= 0)
                            fill_rectangle(&h->ref_cache[list][scan8[i]], 2, 2, 8, (16+ref)^(s->mb_y&1), 1);
                    }
                }
            }
        }
    } else {
        linesize   = h->mb_linesize   = s->linesize;
        uvlinesize = h->mb_uvlinesize = s->uvlinesize;
//        dct_offset = s->linesize * 16;
    }

    if (!simple && IS_INTRA_PCM(mb_type)) {
        for (i=0; i<16; i++) {
            memcpy(dest_y + i*  linesize, h->mb       + i*8, 16);
        }
        for (i=0; i<8; i++) {
            memcpy(dest_cb+ i*uvlinesize, h->mb + 128 + i*4,  8);
            memcpy(dest_cr+ i*uvlinesize, h->mb + 160 + i*4,  8);
        }
    } else {
        if(IS_INTRA(mb_type)){
            if(h->deblocking_filter)
                xchg_mb_border(h, dest_y, dest_cb, dest_cr, linesize, uvlinesize, 1, simple);

            if(simple || !CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
                h->hpc.pred8x8[ h->chroma_pred_mode ](dest_cb, uvlinesize);
                h->hpc.pred8x8[ h->chroma_pred_mode ](dest_cr, uvlinesize);
            }

            if(IS_INTRA4x4(mb_type)){
                if(simple || !s->encoding){
                    if(IS_8x8DCT(mb_type)){
                        if(transform_bypass){
                            idct_dc_add =
                            idct_add    = s->dsp.add_pixels8;
                        }else{
                            idct_dc_add = h->h264dsp.h264_idct8_dc_add;
                            idct_add    = h->h264dsp.h264_idct8_add;
                        }
                        for(i=0; i<16; i+=4){
                            uint8_t * const ptr= dest_y + block_offset[i];
                            const int dir= h->intra4x4_pred_mode_cache[ scan8[i] ];
                            if(transform_bypass && h->sps.profile_idc==244 && dir<=1){
                                h->hpc.pred8x8l_add[dir](ptr, h->mb + i*16, linesize);
                            }else{
                                const int nnz = h->non_zero_count_cache[ scan8[i] ];
                                h->hpc.pred8x8l[ dir ](ptr, (h->topleft_samples_available<<i)&0x8000,
                                                            (h->topright_samples_available<<i)&0x4000, linesize);
                                if(nnz){
                                    if(nnz == 1 && h->mb[i*16])
                                        idct_dc_add(ptr, h->mb + i*16, linesize);
                                    else
                                        idct_add   (ptr, h->mb + i*16, linesize);
                                }
                            }
                        }
                    }else{
                        if(transform_bypass){
                            idct_dc_add =
                            idct_add    = s->dsp.add_pixels4;
                        }else{
                            idct_dc_add = h->h264dsp.h264_idct_dc_add;
                            idct_add    = h->h264dsp.h264_idct_add;
                        }
                        for(i=0; i<16; i++){
                            uint8_t * const ptr= dest_y + block_offset[i];
                            const int dir= h->intra4x4_pred_mode_cache[ scan8[i] ];

                            if(transform_bypass && h->sps.profile_idc==244 && dir<=1){
                                h->hpc.pred4x4_add[dir](ptr, h->mb + i*16, linesize);
                            }else{
                                uint8_t *topright;
                                int nnz, tr;
                                if(dir == DIAG_DOWN_LEFT_PRED || dir == VERT_LEFT_PRED){
                                    const int topright_avail= (h->topright_samples_available<<i)&0x8000;
                                    assert(mb_y || linesize <= block_offset[i]);
                                    if(!topright_avail){
                                        tr= ptr[3 - linesize]*0x01010101;
                                        topright= (uint8_t*) &tr;
                                    }else
                                        topright= ptr + 4 - linesize;
                                }else
                                    topright= NULL;

                                h->hpc.pred4x4[ dir ](ptr, topright, linesize);
                                nnz = h->non_zero_count_cache[ scan8[i] ];
                                if(nnz){
                                    if(is_h264){
                                        if(nnz == 1 && h->mb[i*16])
                                            idct_dc_add(ptr, h->mb + i*16, linesize);
                                        else
                                            idct_add   (ptr, h->mb + i*16, linesize);
                                    }else
                                        ff_svq3_add_idct_c(ptr, h->mb + i*16, linesize, s->qscale, 0);
                                }
                            }
                        }
                    }
                }
            }else{
                h->hpc.pred16x16[ h->intra16x16_pred_mode ](dest_y , linesize);
                if(is_h264){
                    if(h->non_zero_count_cache[ scan8[LUMA_DC_BLOCK_INDEX] ]){
                        if(!transform_bypass)
                            h->h264dsp.h264_luma_dc_dequant_idct(h->mb, h->mb_luma_dc, h->dequant4_coeff[0][s->qscale][0]);
                        else{
                            static const uint8_t dc_mapping[16] = { 0*16, 1*16, 4*16, 5*16, 2*16, 3*16, 6*16, 7*16,
                                                                    8*16, 9*16,12*16,13*16,10*16,11*16,14*16,15*16};
                            for(i = 0; i < 16; i++)
                                h->mb[dc_mapping[i]] = h->mb_luma_dc[i];
                        }
                    }
                }else
                    ff_svq3_luma_dc_dequant_idct_c(h->mb, h->mb_luma_dc, s->qscale);
            }
            if(h->deblocking_filter)
                xchg_mb_border(h, dest_y, dest_cb, dest_cr, linesize, uvlinesize, 0, simple);
        }else if(is_h264){
            hl_motion(h, dest_y, dest_cb, dest_cr,
                      s->me.qpel_put, s->dsp.put_h264_chroma_pixels_tab,
                      s->me.qpel_avg, s->dsp.avg_h264_chroma_pixels_tab,
                      h->h264dsp.weight_h264_pixels_tab, h->h264dsp.biweight_h264_pixels_tab);
        }


        if(!IS_INTRA4x4(mb_type)){
            if(is_h264){
                if(IS_INTRA16x16(mb_type)){
                    if(transform_bypass){
                        if(h->sps.profile_idc==244 && (h->intra16x16_pred_mode==VERT_PRED8x8 || h->intra16x16_pred_mode==HOR_PRED8x8)){
                            h->hpc.pred16x16_add[h->intra16x16_pred_mode](dest_y, block_offset, h->mb, linesize);
                        }else{
                            for(i=0; i<16; i++){
                                if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16])
                                    s->dsp.add_pixels4(dest_y + block_offset[i], h->mb + i*16, linesize);
                            }
                        }
                    }else{
                         h->h264dsp.h264_idct_add16intra(dest_y, block_offset, h->mb, linesize, h->non_zero_count_cache);
                    }
                }else if(h->cbp&15){
                    if(transform_bypass){
                        const int di = IS_8x8DCT(mb_type) ? 4 : 1;
                        idct_add= IS_8x8DCT(mb_type) ? s->dsp.add_pixels8 : s->dsp.add_pixels4;
                        for(i=0; i<16; i+=di){
                            if(h->non_zero_count_cache[ scan8[i] ]){
                                idct_add(dest_y + block_offset[i], h->mb + i*16, linesize);
                            }
                        }
                    }else{
                        if(IS_8x8DCT(mb_type)){
                            h->h264dsp.h264_idct8_add4(dest_y, block_offset, h->mb, linesize, h->non_zero_count_cache);
                        }else{
                            h->h264dsp.h264_idct_add16(dest_y, block_offset, h->mb, linesize, h->non_zero_count_cache);
                        }
                    }
                }
            }else{
                for(i=0; i<16; i++){
                    if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16]){ //FIXME benchmark weird rule, & below
                        uint8_t * const ptr= dest_y + block_offset[i];
                        ff_svq3_add_idct_c(ptr, h->mb + i*16, linesize, s->qscale, IS_INTRA(mb_type) ? 1 : 0);
                    }
                }
            }
        }

        if((simple || !CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)) && (h->cbp&0x30)){
            uint8_t *dest[2] = {dest_cb, dest_cr};
            if(transform_bypass){
                if(IS_INTRA(mb_type) && h->sps.profile_idc==244 && (h->chroma_pred_mode==VERT_PRED8x8 || h->chroma_pred_mode==HOR_PRED8x8)){
                    h->hpc.pred8x8_add[h->chroma_pred_mode](dest[0], block_offset + 16, h->mb + 16*16, uvlinesize);
                    h->hpc.pred8x8_add[h->chroma_pred_mode](dest[1], block_offset + 20, h->mb + 20*16, uvlinesize);
                }else{
                    idct_add = s->dsp.add_pixels4;
                    for(i=16; i<16+8; i++){
                        if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16])
                            idct_add   (dest[(i&4)>>2] + block_offset[i], h->mb + i*16, uvlinesize);
                    }
                }
            }else{
                int chroma_qpu = h->dequant4_coeff[IS_INTRA(mb_type) ? 1:4][h->chroma_qp[0]][0];
                int chroma_qpv = h->dequant4_coeff[IS_INTRA(mb_type) ? 2:5][h->chroma_qp[1]][0];
                if(is_h264){
                    if(h->non_zero_count_cache[ scan8[CHROMA_DC_BLOCK_INDEX+0] ])
                        h->h264dsp.h264_chroma_dc_dequant_idct(h->mb + 16*16+0*16, &h->mb_chroma_dc[0], chroma_qpu );
                    if(h->non_zero_count_cache[ scan8[CHROMA_DC_BLOCK_INDEX+1] ])
                        h->h264dsp.h264_chroma_dc_dequant_idct(h->mb + 16*16+4*16, &h->mb_chroma_dc[1], chroma_qpv );
                    h->h264dsp.h264_idct_add8(dest, block_offset,
                                              h->mb, uvlinesize,
                                              h->non_zero_count_cache);
                }else{
                    h->h264dsp.h264_chroma_dc_dequant_idct(h->mb + 16*16+0*16, &h->mb_chroma_dc[0], chroma_qpu );
                    h->h264dsp.h264_chroma_dc_dequant_idct(h->mb + 16*16+4*16, &h->mb_chroma_dc[1], chroma_qpv );
                    for(i=16; i<16+8; i++){
                        if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16]){
                            uint8_t * const ptr= dest[(i&4)>>2] + block_offset[i];
                            ff_svq3_add_idct_c(ptr, h->mb + i*16, uvlinesize, ff_h264_chroma_qp[s->qscale + 12] - 12, 2);
                        }
                    }
                }
            }
        }
    }
    if(h->cbp || IS_INTRA(mb_type))
        s->dsp.clear_blocks(h->mb);
}

/**
 * Process a macroblock; this case avoids checks for expensive uncommon cases.
 */
static void hl_decode_mb_simple(H264Context *h){
    hl_decode_mb_internal(h, 1);
}

/**
 * Process a macroblock; this handles edge cases, such as interlacing.
 */
static void av_noinline hl_decode_mb_complex(H264Context *h){
    hl_decode_mb_internal(h, 0);
}

void ff_h264_hl_decode_mb(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= h->mb_xy;
    const int mb_type= s->current_picture.mb_type[mb_xy];
    int is_complex = CONFIG_SMALL || h->is_complex || IS_INTRA_PCM(mb_type) || s->qscale == 0;

    if (is_complex)
        hl_decode_mb_complex(h);
    else hl_decode_mb_simple(h);
}

static int pred_weight_table(H264Context *h){
    MpegEncContext * const s = &h->s;
    int list, i;
    int luma_def, chroma_def;

    h->use_weight= 0;
    h->use_weight_chroma= 0;
    h->luma_log2_weight_denom= get_ue_golomb(&s->gb);
    if(CHROMA)
        h->chroma_log2_weight_denom= get_ue_golomb(&s->gb);
    luma_def = 1<<h->luma_log2_weight_denom;
    chroma_def = 1<<h->chroma_log2_weight_denom;

    for(list=0; list<2; list++){
        h->luma_weight_flag[list]   = 0;
        h->chroma_weight_flag[list] = 0;
        for(i=0; i<h->ref_count[list]; i++){
            int luma_weight_flag, chroma_weight_flag;

            luma_weight_flag= get_bits1(&s->gb);
            if(luma_weight_flag){
                h->luma_weight[i][list][0]= get_se_golomb(&s->gb);
                h->luma_weight[i][list][1]= get_se_golomb(&s->gb);
                if(   h->luma_weight[i][list][0] != luma_def
                   || h->luma_weight[i][list][1] != 0) {
                    h->use_weight= 1;
                    h->luma_weight_flag[list]= 1;
                }
            }else{
                h->luma_weight[i][list][0]= luma_def;
                h->luma_weight[i][list][1]= 0;
            }

            if(CHROMA){
                chroma_weight_flag= get_bits1(&s->gb);
                if(chroma_weight_flag){
                    int j;
                    for(j=0; j<2; j++){
                        h->chroma_weight[i][list][j][0]= get_se_golomb(&s->gb);
                        h->chroma_weight[i][list][j][1]= get_se_golomb(&s->gb);
                        if(   h->chroma_weight[i][list][j][0] != chroma_def
                           || h->chroma_weight[i][list][j][1] != 0) {
                            h->use_weight_chroma= 1;
                            h->chroma_weight_flag[list]= 1;
                        }
                    }
                }else{
                    int j;
                    for(j=0; j<2; j++){
                        h->chroma_weight[i][list][j][0]= chroma_def;
                        h->chroma_weight[i][list][j][1]= 0;
                    }
                }
            }
        }
        if(h->slice_type_nos != FF_B_TYPE) break;
    }
    h->use_weight= h->use_weight || h->use_weight_chroma;
    return 0;
}

/**
 * Initialize implicit_weight table.
 * @param field  0/1 initialize the weight for interlaced MBAFF
 *                -1 initializes the rest
 */
static void implicit_weight_table(H264Context *h, int field){
    MpegEncContext * const s = &h->s;
    int ref0, ref1, i, cur_poc, ref_start, ref_count0, ref_count1;

    for (i = 0; i < 2; i++) {
        h->luma_weight_flag[i]   = 0;
        h->chroma_weight_flag[i] = 0;
    }

    if(field < 0){
        cur_poc = s->current_picture_ptr->poc;
    if(   h->ref_count[0] == 1 && h->ref_count[1] == 1 && !FRAME_MBAFF
       && h->ref_list[0][0].poc + h->ref_list[1][0].poc == 2*cur_poc){
        h->use_weight= 0;
        h->use_weight_chroma= 0;
        return;
    }
        ref_start= 0;
        ref_count0= h->ref_count[0];
        ref_count1= h->ref_count[1];
    }else{
        cur_poc = s->current_picture_ptr->field_poc[field];
        ref_start= 16;
        ref_count0= 16+2*h->ref_count[0];
        ref_count1= 16+2*h->ref_count[1];
    }

    h->use_weight= 2;
    h->use_weight_chroma= 2;
    h->luma_log2_weight_denom= 5;
    h->chroma_log2_weight_denom= 5;

    for(ref0=ref_start; ref0 < ref_count0; ref0++){
        int poc0 = h->ref_list[0][ref0].poc;
        for(ref1=ref_start; ref1 < ref_count1; ref1++){
            int poc1 = h->ref_list[1][ref1].poc;
            int td = av_clip(poc1 - poc0, -128, 127);
            int w= 32;
            if(td){
                int tb = av_clip(cur_poc - poc0, -128, 127);
                int tx = (16384 + (FFABS(td) >> 1)) / td;
                int dist_scale_factor = (tb*tx + 32) >> 8;
                if(dist_scale_factor >= -64 && dist_scale_factor <= 128)
                    w = 64 - dist_scale_factor;
            }
            if(field<0){
                h->implicit_weight[ref0][ref1][0]=
                h->implicit_weight[ref0][ref1][1]= w;
            }else{
                h->implicit_weight[ref0][ref1][field]=w;
            }
        }
    }
}

/**
 * instantaneous decoder refresh.
 */
static void idr(H264Context *h){
    ff_h264_remove_all_refs(h);
    h->prev_frame_num= 0;
    h->prev_frame_num_offset= 0;
    h->prev_poc_msb=
    h->prev_poc_lsb= 0;
}

/* forget old pics after a seek */
static void flush_dpb(AVCodecContext *avctx){
    H264Context *h= avctx->priv_data;
    int i;
    for(i=0; i<MAX_DELAYED_PIC_COUNT; i++) {
        if(h->delayed_pic[i])
            h->delayed_pic[i]->reference= 0;
        h->delayed_pic[i]= NULL;
    }
    h->outputed_poc= INT_MIN;
    h->prev_interlaced_frame = 1;
    idr(h);
    if(h->s.current_picture_ptr)
        h->s.current_picture_ptr->reference= 0;
    h->s.first_field= 0;
    ff_h264_reset_sei(h);
    ff_mpeg_flush(avctx);
}

static int init_poc(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int max_frame_num= 1<<h->sps.log2_max_frame_num;
    int field_poc[2];
    Picture *cur = s->current_picture_ptr;

    h->frame_num_offset= h->prev_frame_num_offset;
    if(h->frame_num < h->prev_frame_num)
        h->frame_num_offset += max_frame_num;

    if(h->sps.poc_type==0){
        const int max_poc_lsb= 1<<h->sps.log2_max_poc_lsb;

        if     (h->poc_lsb < h->prev_poc_lsb && h->prev_poc_lsb - h->poc_lsb >= max_poc_lsb/2)
            h->poc_msb = h->prev_poc_msb + max_poc_lsb;
        else if(h->poc_lsb > h->prev_poc_lsb && h->prev_poc_lsb - h->poc_lsb < -max_poc_lsb/2)
            h->poc_msb = h->prev_poc_msb - max_poc_lsb;
        else
            h->poc_msb = h->prev_poc_msb;
//printf("poc: %d %d\n", h->poc_msb, h->poc_lsb);
        field_poc[0] =
        field_poc[1] = h->poc_msb + h->poc_lsb;
        if(s->picture_structure == PICT_FRAME)
            field_poc[1] += h->delta_poc_bottom;
    }else if(h->sps.poc_type==1){
        int abs_frame_num, expected_delta_per_poc_cycle, expectedpoc;
        int i;

        if(h->sps.poc_cycle_length != 0)
            abs_frame_num = h->frame_num_offset + h->frame_num;
        else
            abs_frame_num = 0;

        if(h->nal_ref_idc==0 && abs_frame_num > 0)
            abs_frame_num--;

        expected_delta_per_poc_cycle = 0;
        for(i=0; i < h->sps.poc_cycle_length; i++)
            expected_delta_per_poc_cycle += h->sps.offset_for_ref_frame[ i ]; //FIXME integrate during sps parse

        if(abs_frame_num > 0){
            int poc_cycle_cnt          = (abs_frame_num - 1) / h->sps.poc_cycle_length;
            int frame_num_in_poc_cycle = (abs_frame_num - 1) % h->sps.poc_cycle_length;

            expectedpoc = poc_cycle_cnt * expected_delta_per_poc_cycle;
            for(i = 0; i <= frame_num_in_poc_cycle; i++)
                expectedpoc = expectedpoc + h->sps.offset_for_ref_frame[ i ];
        } else
            expectedpoc = 0;

        if(h->nal_ref_idc == 0)
            expectedpoc = expectedpoc + h->sps.offset_for_non_ref_pic;

        field_poc[0] = expectedpoc + h->delta_poc[0];
        field_poc[1] = field_poc[0] + h->sps.offset_for_top_to_bottom_field;

        if(s->picture_structure == PICT_FRAME)
            field_poc[1] += h->delta_poc[1];
    }else{
        int poc= 2*(h->frame_num_offset + h->frame_num);

        if(!h->nal_ref_idc)
            poc--;

        field_poc[0]= poc;
        field_poc[1]= poc;
    }

    if(s->picture_structure != PICT_BOTTOM_FIELD)
        s->current_picture_ptr->field_poc[0]= field_poc[0];
    if(s->picture_structure != PICT_TOP_FIELD)
        s->current_picture_ptr->field_poc[1]= field_poc[1];
    cur->poc= FFMIN(cur->field_poc[0], cur->field_poc[1]);

    return 0;
}


/**
 * initialize scan tables
 */
static void init_scan_tables(H264Context *h){
    int i;
    for(i=0; i<16; i++){
#define T(x) (x>>2) | ((x<<2) & 0xF)
        h->zigzag_scan[i] = T(zigzag_scan[i]);
        h-> field_scan[i] = T( field_scan[i]);
#undef T
    }
    for(i=0; i<64; i++){
#define T(x) (x>>3) | ((x&7)<<3)
        h->zigzag_scan8x8[i]       = T(ff_zigzag_direct[i]);
        h->zigzag_scan8x8_cavlc[i] = T(zigzag_scan8x8_cavlc[i]);
        h->field_scan8x8[i]        = T(field_scan8x8[i]);
        h->field_scan8x8_cavlc[i]  = T(field_scan8x8_cavlc[i]);
#undef T
    }
    if(h->sps.transform_bypass){ //FIXME same ugly
        h->zigzag_scan_q0          = zigzag_scan;
        h->zigzag_scan8x8_q0       = ff_zigzag_direct;
        h->zigzag_scan8x8_cavlc_q0 = zigzag_scan8x8_cavlc;
        h->field_scan_q0           = field_scan;
        h->field_scan8x8_q0        = field_scan8x8;
        h->field_scan8x8_cavlc_q0  = field_scan8x8_cavlc;
    }else{
        h->zigzag_scan_q0          = h->zigzag_scan;
        h->zigzag_scan8x8_q0       = h->zigzag_scan8x8;
        h->zigzag_scan8x8_cavlc_q0 = h->zigzag_scan8x8_cavlc;
        h->field_scan_q0           = h->field_scan;
        h->field_scan8x8_q0        = h->field_scan8x8;
        h->field_scan8x8_cavlc_q0  = h->field_scan8x8_cavlc;
    }
}

static void field_end(H264Context *h){
    MpegEncContext * const s = &h->s;
    AVCodecContext * const avctx= s->avctx;
    s->mb_y= 0;

    s->current_picture_ptr->qscale_type= FF_QSCALE_TYPE_H264;
    s->current_picture_ptr->pict_type= s->pict_type;

    if (CONFIG_H264_VDPAU_DECODER && s->avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU)
        ff_vdpau_h264_set_reference_frames(s);

    if(!s->dropable) {
        ff_h264_execute_ref_pic_marking(h, h->mmco, h->mmco_index);
        h->prev_poc_msb= h->poc_msb;
        h->prev_poc_lsb= h->poc_lsb;
    }
    h->prev_frame_num_offset= h->frame_num_offset;
    h->prev_frame_num= h->frame_num;

    if (avctx->hwaccel) {
        if (avctx->hwaccel->end_frame(avctx) < 0)
            av_log(avctx, AV_LOG_ERROR, "hardware accelerator failed to decode picture\n");
    }

    if (CONFIG_H264_VDPAU_DECODER && s->avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU)
        ff_vdpau_h264_picture_complete(s);

    /*
     * FIXME: Error handling code does not seem to support interlaced
     * when slices span multiple rows
     * The ff_er_add_slice calls don't work right for bottom
     * fields; they cause massive erroneous error concealing
     * Error marking covers both fields (top and bottom).
     * This causes a mismatched s->error_count
     * and a bad error table. Further, the error count goes to
     * INT_MAX when called for bottom field, because mb_y is
     * past end by one (callers fault) and resync_mb_y != 0
     * causes problems for the first MB line, too.
     */
    if (!FIELD_PICTURE)
        ff_er_frame_end(s);

    MPV_frame_end(s);

    h->current_slice=0;
}

/**
 * Replicate H264 "master" context to thread contexts.
 */
static void clone_slice(H264Context *dst, H264Context *src)
{
    memcpy(dst->block_offset,     src->block_offset, sizeof(dst->block_offset));
    dst->s.current_picture_ptr  = src->s.current_picture_ptr;
    dst->s.current_picture      = src->s.current_picture;
    dst->s.linesize             = src->s.linesize;
    dst->s.uvlinesize           = src->s.uvlinesize;
    dst->s.first_field          = src->s.first_field;

    dst->prev_poc_msb           = src->prev_poc_msb;
    dst->prev_poc_lsb           = src->prev_poc_lsb;
    dst->prev_frame_num_offset  = src->prev_frame_num_offset;
    dst->prev_frame_num         = src->prev_frame_num;
    dst->short_ref_count        = src->short_ref_count;

    memcpy(dst->short_ref,        src->short_ref,        sizeof(dst->short_ref));
    memcpy(dst->long_ref,         src->long_ref,         sizeof(dst->long_ref));
    memcpy(dst->default_ref_list, src->default_ref_list, sizeof(dst->default_ref_list));
    memcpy(dst->ref_list,         src->ref_list,         sizeof(dst->ref_list));

    memcpy(dst->dequant4_coeff,   src->dequant4_coeff,   sizeof(src->dequant4_coeff));
    memcpy(dst->dequant8_coeff,   src->dequant8_coeff,   sizeof(src->dequant8_coeff));
}

/**
 * decodes a slice header.
 * This will also call MPV_common_init() and frame_start() as needed.
 *
 * @param h h264context
 * @param h0 h264 master context (differs from 'h' when doing sliced based parallel decoding)
 *
 * @return 0 if okay, <0 if an error occurred, 1 if decoding must not be multithreaded
 */
static int decode_slice_header(H264Context *h, H264Context *h0){
    MpegEncContext * const s = &h->s;
    MpegEncContext * const s0 = &h0->s;
    unsigned int first_mb_in_slice;
    unsigned int pps_id;
    int num_ref_idx_active_override_flag;
    unsigned int slice_type, tmp, i, j;
    int default_ref_list_done = 0;
    int last_pic_structure;

    s->dropable= h->nal_ref_idc == 0;

    if((s->avctx->flags2 & CODEC_FLAG2_FAST) && !h->nal_ref_idc){
        s->me.qpel_put= s->dsp.put_2tap_qpel_pixels_tab;
        s->me.qpel_avg= s->dsp.avg_2tap_qpel_pixels_tab;
    }else{
        s->me.qpel_put= s->dsp.put_h264_qpel_pixels_tab;
        s->me.qpel_avg= s->dsp.avg_h264_qpel_pixels_tab;
    }

    first_mb_in_slice= get_ue_golomb(&s->gb);

    if(first_mb_in_slice == 0){ //FIXME better field boundary detection
        if(h0->current_slice && FIELD_PICTURE){
            field_end(h);
        }

        h0->current_slice = 0;
        if (!s0->first_field)
            s->current_picture_ptr= NULL;
    }

    slice_type= get_ue_golomb_31(&s->gb);
    if(slice_type > 9){
        av_log(h->s.avctx, AV_LOG_ERROR, "slice type too large (%d) at %d %d\n", h->slice_type, s->mb_x, s->mb_y);
        return -1;
    }
    if(slice_type > 4){
        slice_type -= 5;
        h->slice_type_fixed=1;
    }else
        h->slice_type_fixed=0;

    slice_type= golomb_to_pict_type[ slice_type ];
    if (slice_type == FF_I_TYPE
        || (h0->current_slice != 0 && slice_type == h0->last_slice_type) ) {
        default_ref_list_done = 1;
    }
    h->slice_type= slice_type;
    h->slice_type_nos= slice_type & 3;

    s->pict_type= h->slice_type; // to make a few old functions happy, it's wrong though

    pps_id= get_ue_golomb(&s->gb);
    if(pps_id>=MAX_PPS_COUNT){
        av_log(h->s.avctx, AV_LOG_ERROR, "pps_id out of range\n");
        return -1;
    }
    if(!h0->pps_buffers[pps_id]) {
        av_log(h->s.avctx, AV_LOG_ERROR, "non-existing PPS %u referenced\n", pps_id);
        return -1;
    }
    h->pps= *h0->pps_buffers[pps_id];

    if(!h0->sps_buffers[h->pps.sps_id]) {
        av_log(h->s.avctx, AV_LOG_ERROR, "non-existing SPS %u referenced\n", h->pps.sps_id);
        return -1;
    }
    h->sps = *h0->sps_buffers[h->pps.sps_id];

    s->avctx->profile = h->sps.profile_idc;
    s->avctx->level   = h->sps.level_idc;
    s->avctx->refs    = h->sps.ref_frame_count;

    if(h == h0 && h->dequant_coeff_pps != pps_id){
        h->dequant_coeff_pps = pps_id;
        init_dequant_tables(h);
    }

    s->mb_width= h->sps.mb_width;
    s->mb_height= h->sps.mb_height * (2 - h->sps.frame_mbs_only_flag);

    h->b_stride=  s->mb_width*4;

    s->width = 16*s->mb_width - 2*FFMIN(h->sps.crop_right, 7);
    if(h->sps.frame_mbs_only_flag)
        s->height= 16*s->mb_height - 2*FFMIN(h->sps.crop_bottom, 7);
    else
        s->height= 16*s->mb_height - 4*FFMIN(h->sps.crop_bottom, 7);

    if (s->context_initialized
        && (   s->width != s->avctx->width || s->height != s->avctx->height
            || av_cmp_q(h->sps.sar, s->avctx->sample_aspect_ratio))) {
        if(h != h0)
            return -1;   // width / height changed during parallelized decoding
        free_tables(h);
        flush_dpb(s->avctx);
        MPV_common_end(s);
    }
    if (!s->context_initialized) {
        if(h != h0)
            return -1;  // we cant (re-)initialize context during parallel decoding

        avcodec_set_dimensions(s->avctx, s->width, s->height);
        s->avctx->sample_aspect_ratio= h->sps.sar;
        av_assert0(s->avctx->sample_aspect_ratio.den);

        if(h->sps.video_signal_type_present_flag){
            s->avctx->color_range = h->sps.full_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
            if(h->sps.colour_description_present_flag){
                s->avctx->color_primaries = h->sps.color_primaries;
                s->avctx->color_trc       = h->sps.color_trc;
                s->avctx->colorspace      = h->sps.colorspace;
            }
        }

        if(h->sps.timing_info_present_flag){
            int64_t den= h->sps.time_scale;
            if(h->x264_build < 44U)
                den *= 2;
            av_reduce(&s->avctx->time_base.num, &s->avctx->time_base.den,
                      h->sps.num_units_in_tick, den, 1<<30);
        }
        s->avctx->pix_fmt = s->avctx->get_format(s->avctx,
                                                 s->avctx->codec->pix_fmts ?
                                                 s->avctx->codec->pix_fmts :
                                                 s->avctx->color_range == AVCOL_RANGE_JPEG ?
                                                 hwaccel_pixfmt_list_h264_jpeg_420 :
                                                 ff_hwaccel_pixfmt_list_420);
        s->avctx->hwaccel = ff_find_hwaccel(s->avctx->codec->id, s->avctx->pix_fmt);

        if (MPV_common_init(s) < 0)
            return -1;
        s->first_field = 0;
        h->prev_interlaced_frame = 1;

        init_scan_tables(h);
        ff_h264_alloc_tables(h);

        for(i = 1; i < s->avctx->thread_count; i++) {
            H264Context *c;
            c = h->thread_context[i] = av_malloc(sizeof(H264Context));
            memcpy(c, h->s.thread_context[i], sizeof(MpegEncContext));
            memset(&c->s + 1, 0, sizeof(H264Context) - sizeof(MpegEncContext));
            c->h264dsp = h->h264dsp;
            c->sps = h->sps;
            c->pps = h->pps;
            init_scan_tables(c);
            clone_tables(c, h, i);
        }

        for(i = 0; i < s->avctx->thread_count; i++)
            if(context_init(h->thread_context[i]) < 0)
                return -1;
    }

    h->frame_num= get_bits(&s->gb, h->sps.log2_max_frame_num);

    h->mb_mbaff = 0;
    h->mb_aff_frame = 0;
    last_pic_structure = s0->picture_structure;
    if(h->sps.frame_mbs_only_flag){
        s->picture_structure= PICT_FRAME;
    }else{
        if(get_bits1(&s->gb)) { //field_pic_flag
            s->picture_structure= PICT_TOP_FIELD + get_bits1(&s->gb); //bottom_field_flag
        } else {
            s->picture_structure= PICT_FRAME;
            h->mb_aff_frame = h->sps.mb_aff;
        }
    }
    h->mb_field_decoding_flag= s->picture_structure != PICT_FRAME;

    if(h0->current_slice == 0){
        while(h->frame_num !=  h->prev_frame_num &&
              h->frame_num != (h->prev_frame_num+1)%(1<<h->sps.log2_max_frame_num)){
            Picture *prev = h->short_ref_count ? h->short_ref[0] : NULL;
            av_log(h->s.avctx, AV_LOG_DEBUG, "Frame num gap %d %d\n", h->frame_num, h->prev_frame_num);
            if (ff_h264_frame_start(h) < 0)
                return -1;
            h->prev_frame_num++;
            h->prev_frame_num %= 1<<h->sps.log2_max_frame_num;
            s->current_picture_ptr->frame_num= h->prev_frame_num;
            ff_generate_sliding_window_mmcos(h);
            ff_h264_execute_ref_pic_marking(h, h->mmco, h->mmco_index);
            /* Error concealment: if a ref is missing, copy the previous ref in its place.
             * FIXME: avoiding a memcpy would be nice, but ref handling makes many assumptions
             * about there being no actual duplicates.
             * FIXME: this doesn't copy padding for out-of-frame motion vectors.  Given we're
             * concealing a lost frame, this probably isn't noticable by comparison, but it should
             * be fixed. */
            if (h->short_ref_count) {
                if (prev) {
                    av_image_copy(h->short_ref[0]->data, h->short_ref[0]->linesize,
                                  (const uint8_t**)prev->data, prev->linesize,
                                  s->avctx->pix_fmt, s->mb_width*16, s->mb_height*16);
                    h->short_ref[0]->poc = prev->poc+2;
                }
                h->short_ref[0]->frame_num = h->prev_frame_num;
            }
        }

        /* See if we have a decoded first field looking for a pair... */
        if (s0->first_field) {
            assert(s0->current_picture_ptr);
            assert(s0->current_picture_ptr->data[0]);
            assert(s0->current_picture_ptr->reference != DELAYED_PIC_REF);

            /* figure out if we have a complementary field pair */
            if (!FIELD_PICTURE || s->picture_structure == last_pic_structure) {
                /*
                 * Previous field is unmatched. Don't display it, but let it
                 * remain for reference if marked as such.
                 */
                s0->current_picture_ptr = NULL;
                s0->first_field = FIELD_PICTURE;

            } else {
                if (h->nal_ref_idc &&
                        s0->current_picture_ptr->reference &&
                        s0->current_picture_ptr->frame_num != h->frame_num) {
                    /*
                     * This and previous field were reference, but had
                     * different frame_nums. Consider this field first in
                     * pair. Throw away previous field except for reference
                     * purposes.
                     */
                    s0->first_field = 1;
                    s0->current_picture_ptr = NULL;

                } else {
                    /* Second field in complementary pair */
                    s0->first_field = 0;
                }
            }

        } else {
            /* Frame or first field in a potentially complementary pair */
            assert(!s0->current_picture_ptr);
            s0->first_field = FIELD_PICTURE;
        }

        if((!FIELD_PICTURE || s0->first_field) && ff_h264_frame_start(h) < 0) {
            s0->first_field = 0;
            return -1;
        }
    }
    if(h != h0)
        clone_slice(h, h0);

    s->current_picture_ptr->frame_num= h->frame_num; //FIXME frame_num cleanup

    assert(s->mb_num == s->mb_width * s->mb_height);
    if(first_mb_in_slice << FIELD_OR_MBAFF_PICTURE >= s->mb_num ||
       first_mb_in_slice                    >= s->mb_num){
        av_log(h->s.avctx, AV_LOG_ERROR, "first_mb_in_slice overflow\n");
        return -1;
    }
    s->resync_mb_x = s->mb_x = first_mb_in_slice % s->mb_width;
    s->resync_mb_y = s->mb_y = (first_mb_in_slice / s->mb_width) << FIELD_OR_MBAFF_PICTURE;
    if (s->picture_structure == PICT_BOTTOM_FIELD)
        s->resync_mb_y = s->mb_y = s->mb_y + 1;
    assert(s->mb_y < s->mb_height);

    if(s->picture_structure==PICT_FRAME){
        h->curr_pic_num=   h->frame_num;
        h->max_pic_num= 1<< h->sps.log2_max_frame_num;
    }else{
        h->curr_pic_num= 2*h->frame_num + 1;
        h->max_pic_num= 1<<(h->sps.log2_max_frame_num + 1);
    }

    if(h->nal_unit_type == NAL_IDR_SLICE){
        get_ue_golomb(&s->gb); /* idr_pic_id */
    }

    if(h->sps.poc_type==0){
        h->poc_lsb= get_bits(&s->gb, h->sps.log2_max_poc_lsb);

        if(h->pps.pic_order_present==1 && s->picture_structure==PICT_FRAME){
            h->delta_poc_bottom= get_se_golomb(&s->gb);
        }
    }

    if(h->sps.poc_type==1 && !h->sps.delta_pic_order_always_zero_flag){
        h->delta_poc[0]= get_se_golomb(&s->gb);

        if(h->pps.pic_order_present==1 && s->picture_structure==PICT_FRAME)
            h->delta_poc[1]= get_se_golomb(&s->gb);
    }

    init_poc(h);

    if(h->pps.redundant_pic_cnt_present){
        h->redundant_pic_count= get_ue_golomb(&s->gb);
    }

    //set defaults, might be overridden a few lines later
    h->ref_count[0]= h->pps.ref_count[0];
    h->ref_count[1]= h->pps.ref_count[1];

    if(h->slice_type_nos != FF_I_TYPE){
        if(h->slice_type_nos == FF_B_TYPE){
            h->direct_spatial_mv_pred= get_bits1(&s->gb);
        }
        num_ref_idx_active_override_flag= get_bits1(&s->gb);

        if(num_ref_idx_active_override_flag){
            h->ref_count[0]= get_ue_golomb(&s->gb) + 1;
            if(h->slice_type_nos==FF_B_TYPE)
                h->ref_count[1]= get_ue_golomb(&s->gb) + 1;

            if(h->ref_count[0]-1 > 32-1 || h->ref_count[1]-1 > 32-1){
                av_log(h->s.avctx, AV_LOG_ERROR, "reference overflow\n");
                h->ref_count[0]= h->ref_count[1]= 1;
                return -1;
            }
        }
        if(h->slice_type_nos == FF_B_TYPE)
            h->list_count= 2;
        else
            h->list_count= 1;
    }else
        h->list_count= 0;

    if(!default_ref_list_done){
        ff_h264_fill_default_ref_list(h);
    }

    if(h->slice_type_nos!=FF_I_TYPE && ff_h264_decode_ref_pic_list_reordering(h) < 0)
        return -1;

    if(h->slice_type_nos!=FF_I_TYPE){
        s->last_picture_ptr= &h->ref_list[0][0];
        ff_copy_picture(&s->last_picture, s->last_picture_ptr);
    }
    if(h->slice_type_nos==FF_B_TYPE){
        s->next_picture_ptr= &h->ref_list[1][0];
        ff_copy_picture(&s->next_picture, s->next_picture_ptr);
    }

    if(   (h->pps.weighted_pred          && h->slice_type_nos == FF_P_TYPE )
       ||  (h->pps.weighted_bipred_idc==1 && h->slice_type_nos== FF_B_TYPE ) )
        pred_weight_table(h);
    else if(h->pps.weighted_bipred_idc==2 && h->slice_type_nos== FF_B_TYPE){
        implicit_weight_table(h, -1);
    }else {
        h->use_weight = 0;
        for (i = 0; i < 2; i++) {
            h->luma_weight_flag[i]   = 0;
            h->chroma_weight_flag[i] = 0;
        }
    }

    if(h->nal_ref_idc)
        ff_h264_decode_ref_pic_marking(h0, &s->gb);

    if(FRAME_MBAFF){
        ff_h264_fill_mbaff_ref_list(h);

        if(h->pps.weighted_bipred_idc==2 && h->slice_type_nos== FF_B_TYPE){
            implicit_weight_table(h, 0);
            implicit_weight_table(h, 1);
        }
    }

    if(h->slice_type_nos==FF_B_TYPE && !h->direct_spatial_mv_pred)
        ff_h264_direct_dist_scale_factor(h);
    ff_h264_direct_ref_list_init(h);

    if( h->slice_type_nos != FF_I_TYPE && h->pps.cabac ){
        tmp = get_ue_golomb_31(&s->gb);
        if(tmp > 2){
            av_log(s->avctx, AV_LOG_ERROR, "cabac_init_idc overflow\n");
            return -1;
        }
        h->cabac_init_idc= tmp;
    }

    h->last_qscale_diff = 0;
    tmp = h->pps.init_qp + get_se_golomb(&s->gb);
    if(tmp>51){
        av_log(s->avctx, AV_LOG_ERROR, "QP %u out of range\n", tmp);
        return -1;
    }
    s->qscale= tmp;
    h->chroma_qp[0] = get_chroma_qp(h, 0, s->qscale);
    h->chroma_qp[1] = get_chroma_qp(h, 1, s->qscale);
    //FIXME qscale / qp ... stuff
    if(h->slice_type == FF_SP_TYPE){
        get_bits1(&s->gb); /* sp_for_switch_flag */
    }
    if(h->slice_type==FF_SP_TYPE || h->slice_type == FF_SI_TYPE){
        get_se_golomb(&s->gb); /* slice_qs_delta */
    }

    h->deblocking_filter = 1;
    h->slice_alpha_c0_offset = 52;
    h->slice_beta_offset = 52;
    if( h->pps.deblocking_filter_parameters_present ) {
        tmp= get_ue_golomb_31(&s->gb);
        if(tmp > 2){
            av_log(s->avctx, AV_LOG_ERROR, "deblocking_filter_idc %u out of range\n", tmp);
            return -1;
        }
        h->deblocking_filter= tmp;
        if(h->deblocking_filter < 2)
            h->deblocking_filter^= 1; // 1<->0

        if( h->deblocking_filter ) {
            h->slice_alpha_c0_offset += get_se_golomb(&s->gb) << 1;
            h->slice_beta_offset     += get_se_golomb(&s->gb) << 1;
            if(   h->slice_alpha_c0_offset > 104U
               || h->slice_beta_offset     > 104U){
                av_log(s->avctx, AV_LOG_ERROR, "deblocking filter parameters %d %d out of range\n", h->slice_alpha_c0_offset, h->slice_beta_offset);
                return -1;
            }
        }
    }

    if(   s->avctx->skip_loop_filter >= AVDISCARD_ALL
       ||(s->avctx->skip_loop_filter >= AVDISCARD_NONKEY && h->slice_type_nos != FF_I_TYPE)
       ||(s->avctx->skip_loop_filter >= AVDISCARD_BIDIR  && h->slice_type_nos == FF_B_TYPE)
       ||(s->avctx->skip_loop_filter >= AVDISCARD_NONREF && h->nal_ref_idc == 0))
        h->deblocking_filter= 0;

    if(h->deblocking_filter == 1 && h0->max_contexts > 1) {
        if(s->avctx->flags2 & CODEC_FLAG2_FAST) {
            /* Cheat slightly for speed:
               Do not bother to deblock across slices. */
            h->deblocking_filter = 2;
        } else {
            h0->max_contexts = 1;
            if(!h0->single_decode_warning) {
                av_log(s->avctx, AV_LOG_INFO, "Cannot parallelize deblocking type 1, decoding such frames in sequential order\n");
                h0->single_decode_warning = 1;
            }
            if(h != h0)
                return 1; // deblocking switched inside frame
        }
    }
    h->qp_thresh= 15 + 52 - FFMIN(h->slice_alpha_c0_offset, h->slice_beta_offset) - FFMAX3(0, h->pps.chroma_qp_index_offset[0], h->pps.chroma_qp_index_offset[1]);

#if 0 //FMO
    if( h->pps.num_slice_groups > 1  && h->pps.mb_slice_group_map_type >= 3 && h->pps.mb_slice_group_map_type <= 5)
        slice_group_change_cycle= get_bits(&s->gb, ?);
#endif

    h0->last_slice_type = slice_type;
    h->slice_num = ++h0->current_slice;
    if(h->slice_num >= MAX_SLICES){
        av_log(s->avctx, AV_LOG_ERROR, "Too many slices, increase MAX_SLICES and recompile\n");
    }

    for(j=0; j<2; j++){
        int id_list[16];
        int *ref2frm= h->ref2frm[h->slice_num&(MAX_SLICES-1)][j];
        for(i=0; i<16; i++){
            id_list[i]= 60;
            if(h->ref_list[j][i].data[0]){
                int k;
                uint8_t *base= h->ref_list[j][i].base[0];
                for(k=0; k<h->short_ref_count; k++)
                    if(h->short_ref[k]->base[0] == base){
                        id_list[i]= k;
                        break;
                    }
                for(k=0; k<h->long_ref_count; k++)
                    if(h->long_ref[k] && h->long_ref[k]->base[0] == base){
                        id_list[i]= h->short_ref_count + k;
                        break;
                    }
            }
        }

        ref2frm[0]=
        ref2frm[1]= -1;
        for(i=0; i<16; i++)
            ref2frm[i+2]= 4*id_list[i]
                          +(h->ref_list[j][i].reference&3);
        ref2frm[18+0]=
        ref2frm[18+1]= -1;
        for(i=16; i<48; i++)
            ref2frm[i+4]= 4*id_list[(i-16)>>1]
                          +(h->ref_list[j][i].reference&3);
    }

    h->emu_edge_width= (s->flags&CODEC_FLAG_EMU_EDGE) ? 0 : 16;
    h->emu_edge_height= (FRAME_MBAFF || FIELD_PICTURE) ? 0 : h->emu_edge_width;

    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        av_log(h->s.avctx, AV_LOG_DEBUG, "slice:%d %s mb:%d %c%s%s pps:%u frame:%d poc:%d/%d ref:%d/%d qp:%d loop:%d:%d:%d weight:%d%s %s\n",
               h->slice_num,
               (s->picture_structure==PICT_FRAME ? "F" : s->picture_structure==PICT_TOP_FIELD ? "T" : "B"),
               first_mb_in_slice,
               av_get_pict_type_char(h->slice_type), h->slice_type_fixed ? " fix" : "", h->nal_unit_type == NAL_IDR_SLICE ? " IDR" : "",
               pps_id, h->frame_num,
               s->current_picture_ptr->field_poc[0], s->current_picture_ptr->field_poc[1],
               h->ref_count[0], h->ref_count[1],
               s->qscale,
               h->deblocking_filter, h->slice_alpha_c0_offset/2-26, h->slice_beta_offset/2-26,
               h->use_weight,
               h->use_weight==1 && h->use_weight_chroma ? "c" : "",
               h->slice_type == FF_B_TYPE ? (h->direct_spatial_mv_pred ? "SPAT" : "TEMP") : ""
               );
    }

    return 0;
}

int ff_h264_get_slice_type(const H264Context *h)
{
    switch (h->slice_type) {
    case FF_P_TYPE:  return 0;
    case FF_B_TYPE:  return 1;
    case FF_I_TYPE:  return 2;
    case FF_SP_TYPE: return 3;
    case FF_SI_TYPE: return 4;
    default:         return -1;
    }
}

/**
 *
 * @return non zero if the loop filter can be skiped
 */
static int fill_filter_caches(H264Context *h, int mb_type){
    MpegEncContext * const s = &h->s;
    const int mb_xy= h->mb_xy;
    int top_xy, left_xy[2];
    int top_type, left_type[2];

    top_xy     = mb_xy  - (s->mb_stride << MB_FIELD);

    //FIXME deblocking could skip the intra and nnz parts.

    /* Wow, what a mess, why didn't they simplify the interlacing & intra
     * stuff, I can't imagine that these complex rules are worth it. */

    left_xy[1] = left_xy[0] = mb_xy-1;
    if(FRAME_MBAFF){
        const int left_mb_field_flag     = IS_INTERLACED(s->current_picture.mb_type[mb_xy-1]);
        const int curr_mb_field_flag     = IS_INTERLACED(mb_type);
        if(s->mb_y&1){
            if (left_mb_field_flag != curr_mb_field_flag) {
                left_xy[0] -= s->mb_stride;
            }
        }else{
            if(curr_mb_field_flag){
                top_xy      += s->mb_stride & (((s->current_picture.mb_type[top_xy    ]>>7)&1)-1);
            }
            if (left_mb_field_flag != curr_mb_field_flag) {
                left_xy[1] += s->mb_stride;
            }
        }
    }

    h->top_mb_xy = top_xy;
    h->left_mb_xy[0] = left_xy[0];
    h->left_mb_xy[1] = left_xy[1];
    {
        //for sufficiently low qp, filtering wouldn't do anything
        //this is a conservative estimate: could also check beta_offset and more accurate chroma_qp
        int qp_thresh = h->qp_thresh; //FIXME strictly we should store qp_thresh for each mb of a slice
        int qp = s->current_picture.qscale_table[mb_xy];
        if(qp <= qp_thresh
           && (left_xy[0]<0 || ((qp + s->current_picture.qscale_table[left_xy[0]] + 1)>>1) <= qp_thresh)
           && (top_xy   < 0 || ((qp + s->current_picture.qscale_table[top_xy    ] + 1)>>1) <= qp_thresh)){
            if(!FRAME_MBAFF)
                return 1;
            if(   (left_xy[0]< 0            || ((qp + s->current_picture.qscale_table[left_xy[1]             ] + 1)>>1) <= qp_thresh)
               && (top_xy    < s->mb_stride || ((qp + s->current_picture.qscale_table[top_xy    -s->mb_stride] + 1)>>1) <= qp_thresh))
                return 1;
        }
    }

    top_type     = s->current_picture.mb_type[top_xy]    ;
    left_type[0] = s->current_picture.mb_type[left_xy[0]];
    left_type[1] = s->current_picture.mb_type[left_xy[1]];
    if(h->deblocking_filter == 2){
        if(h->slice_table[top_xy     ] != h->slice_num) top_type= 0;
        if(h->slice_table[left_xy[0] ] != h->slice_num) left_type[0]= left_type[1]= 0;
    }else{
        if(h->slice_table[top_xy     ] == 0xFFFF) top_type= 0;
        if(h->slice_table[left_xy[0] ] == 0xFFFF) left_type[0]= left_type[1] =0;
    }
    h->top_type    = top_type    ;
    h->left_type[0]= left_type[0];
    h->left_type[1]= left_type[1];

    if(IS_INTRA(mb_type))
        return 0;

    AV_COPY64(&h->non_zero_count_cache[0+8*1], &h->non_zero_count[mb_xy][ 0]);
    AV_COPY64(&h->non_zero_count_cache[0+8*2], &h->non_zero_count[mb_xy][ 8]);
    AV_COPY32(&h->non_zero_count_cache[0+8*5], &h->non_zero_count[mb_xy][16]);
    AV_COPY32(&h->non_zero_count_cache[4+8*3], &h->non_zero_count[mb_xy][20]);
    AV_COPY64(&h->non_zero_count_cache[0+8*4], &h->non_zero_count[mb_xy][24]);

    h->cbp= h->cbp_table[mb_xy];

    {
        int list;
        for(list=0; list<h->list_count; list++){
            int8_t *ref;
            int y, b_stride;
            int16_t (*mv_dst)[2];
            int16_t (*mv_src)[2];

            if(!USES_LIST(mb_type, list)){
                fill_rectangle(  h->mv_cache[list][scan8[0]], 4, 4, 8, pack16to32(0,0), 4);
                AV_WN32A(&h->ref_cache[list][scan8[ 0]], ((LIST_NOT_USED)&0xFF)*0x01010101u);
                AV_WN32A(&h->ref_cache[list][scan8[ 2]], ((LIST_NOT_USED)&0xFF)*0x01010101u);
                AV_WN32A(&h->ref_cache[list][scan8[ 8]], ((LIST_NOT_USED)&0xFF)*0x01010101u);
                AV_WN32A(&h->ref_cache[list][scan8[10]], ((LIST_NOT_USED)&0xFF)*0x01010101u);
                continue;
            }

            ref = &s->current_picture.ref_index[list][4*mb_xy];
            {
                int (*ref2frm)[64] = h->ref2frm[ h->slice_num&(MAX_SLICES-1) ][0] + (MB_MBAFF ? 20 : 2);
                AV_WN32A(&h->ref_cache[list][scan8[ 0]], (pack16to32(ref2frm[list][ref[0]],ref2frm[list][ref[1]])&0x00FF00FF)*0x0101);
                AV_WN32A(&h->ref_cache[list][scan8[ 2]], (pack16to32(ref2frm[list][ref[0]],ref2frm[list][ref[1]])&0x00FF00FF)*0x0101);
                ref += 2;
                AV_WN32A(&h->ref_cache[list][scan8[ 8]], (pack16to32(ref2frm[list][ref[0]],ref2frm[list][ref[1]])&0x00FF00FF)*0x0101);
                AV_WN32A(&h->ref_cache[list][scan8[10]], (pack16to32(ref2frm[list][ref[0]],ref2frm[list][ref[1]])&0x00FF00FF)*0x0101);
            }

            b_stride = h->b_stride;
            mv_dst   = &h->mv_cache[list][scan8[0]];
            mv_src   = &s->current_picture.motion_val[list][4*s->mb_x + 4*s->mb_y*b_stride];
            for(y=0; y<4; y++){
                AV_COPY128(mv_dst + 8*y, mv_src + y*b_stride);
            }

        }
    }


/*
0 . T T. T T T T
1 L . .L . . . .
2 L . .L . . . .
3 . T TL . . . .
4 L . .L . . . .
5 L . .. . . . .
*/
//FIXME constraint_intra_pred & partitioning & nnz (let us hope this is just a typo in the spec)
    if(top_type){
        AV_COPY32(&h->non_zero_count_cache[4+8*0], &h->non_zero_count[top_xy][4+3*8]);
    }

    if(left_type[0]){
        h->non_zero_count_cache[3+8*1]= h->non_zero_count[left_xy[0]][7+0*8];
        h->non_zero_count_cache[3+8*2]= h->non_zero_count[left_xy[0]][7+1*8];
        h->non_zero_count_cache[3+8*3]= h->non_zero_count[left_xy[0]][7+2*8];
        h->non_zero_count_cache[3+8*4]= h->non_zero_count[left_xy[0]][7+3*8];
    }

    // CAVLC 8x8dct requires NNZ values for residual decoding that differ from what the loop filter needs
    if(!CABAC && h->pps.transform_8x8_mode){
        if(IS_8x8DCT(top_type)){
            h->non_zero_count_cache[4+8*0]=
            h->non_zero_count_cache[5+8*0]= h->cbp_table[top_xy] & 4;
            h->non_zero_count_cache[6+8*0]=
            h->non_zero_count_cache[7+8*0]= h->cbp_table[top_xy] & 8;
        }
        if(IS_8x8DCT(left_type[0])){
            h->non_zero_count_cache[3+8*1]=
            h->non_zero_count_cache[3+8*2]= h->cbp_table[left_xy[0]]&2; //FIXME check MBAFF
        }
        if(IS_8x8DCT(left_type[1])){
            h->non_zero_count_cache[3+8*3]=
            h->non_zero_count_cache[3+8*4]= h->cbp_table[left_xy[1]]&8; //FIXME check MBAFF
        }

        if(IS_8x8DCT(mb_type)){
            h->non_zero_count_cache[scan8[0   ]]= h->non_zero_count_cache[scan8[1   ]]=
            h->non_zero_count_cache[scan8[2   ]]= h->non_zero_count_cache[scan8[3   ]]= h->cbp & 1;

            h->non_zero_count_cache[scan8[0+ 4]]= h->non_zero_count_cache[scan8[1+ 4]]=
            h->non_zero_count_cache[scan8[2+ 4]]= h->non_zero_count_cache[scan8[3+ 4]]= h->cbp & 2;

            h->non_zero_count_cache[scan8[0+ 8]]= h->non_zero_count_cache[scan8[1+ 8]]=
            h->non_zero_count_cache[scan8[2+ 8]]= h->non_zero_count_cache[scan8[3+ 8]]= h->cbp & 4;

            h->non_zero_count_cache[scan8[0+12]]= h->non_zero_count_cache[scan8[1+12]]=
            h->non_zero_count_cache[scan8[2+12]]= h->non_zero_count_cache[scan8[3+12]]= h->cbp & 8;
        }
    }

    if(IS_INTER(mb_type) || IS_DIRECT(mb_type)){
        int list;
        for(list=0; list<h->list_count; list++){
            if(USES_LIST(top_type, list)){
                const int b_xy= h->mb2b_xy[top_xy] + 3*h->b_stride;
                const int b8_xy= 4*top_xy + 2;
                int (*ref2frm)[64] = h->ref2frm[ h->slice_table[top_xy]&(MAX_SLICES-1) ][0] + (MB_MBAFF ? 20 : 2);
                AV_COPY128(h->mv_cache[list][scan8[0] + 0 - 1*8], s->current_picture.motion_val[list][b_xy + 0]);
                h->ref_cache[list][scan8[0] + 0 - 1*8]=
                h->ref_cache[list][scan8[0] + 1 - 1*8]= ref2frm[list][s->current_picture.ref_index[list][b8_xy + 0]];
                h->ref_cache[list][scan8[0] + 2 - 1*8]=
                h->ref_cache[list][scan8[0] + 3 - 1*8]= ref2frm[list][s->current_picture.ref_index[list][b8_xy + 1]];
            }else{
                AV_ZERO128(h->mv_cache[list][scan8[0] + 0 - 1*8]);
                AV_WN32A(&h->ref_cache[list][scan8[0] + 0 - 1*8], ((LIST_NOT_USED)&0xFF)*0x01010101u);
            }

            if(!IS_INTERLACED(mb_type^left_type[0])){
                if(USES_LIST(left_type[0], list)){
                    const int b_xy= h->mb2b_xy[left_xy[0]] + 3;
                    const int b8_xy= 4*left_xy[0] + 1;
                    int (*ref2frm)[64] = h->ref2frm[ h->slice_table[left_xy[0]]&(MAX_SLICES-1) ][0] + (MB_MBAFF ? 20 : 2);
                    AV_COPY32(h->mv_cache[list][scan8[0] - 1 + 0 ], s->current_picture.motion_val[list][b_xy + h->b_stride*0]);
                    AV_COPY32(h->mv_cache[list][scan8[0] - 1 + 8 ], s->current_picture.motion_val[list][b_xy + h->b_stride*1]);
                    AV_COPY32(h->mv_cache[list][scan8[0] - 1 +16 ], s->current_picture.motion_val[list][b_xy + h->b_stride*2]);
                    AV_COPY32(h->mv_cache[list][scan8[0] - 1 +24 ], s->current_picture.motion_val[list][b_xy + h->b_stride*3]);
                    h->ref_cache[list][scan8[0] - 1 + 0 ]=
                    h->ref_cache[list][scan8[0] - 1 + 8 ]= ref2frm[list][s->current_picture.ref_index[list][b8_xy + 2*0]];
                    h->ref_cache[list][scan8[0] - 1 +16 ]=
                    h->ref_cache[list][scan8[0] - 1 +24 ]= ref2frm[list][s->current_picture.ref_index[list][b8_xy + 2*1]];
                }else{
                    AV_ZERO32(h->mv_cache [list][scan8[0] - 1 + 0 ]);
                    AV_ZERO32(h->mv_cache [list][scan8[0] - 1 + 8 ]);
                    AV_ZERO32(h->mv_cache [list][scan8[0] - 1 +16 ]);
                    AV_ZERO32(h->mv_cache [list][scan8[0] - 1 +24 ]);
                    h->ref_cache[list][scan8[0] - 1 + 0  ]=
                    h->ref_cache[list][scan8[0] - 1 + 8  ]=
                    h->ref_cache[list][scan8[0] - 1 + 16 ]=
                    h->ref_cache[list][scan8[0] - 1 + 24 ]= LIST_NOT_USED;
                }
            }
        }
    }

    return 0;
}

static void loop_filter(H264Context *h){
    MpegEncContext * const s = &h->s;
    uint8_t  *dest_y, *dest_cb, *dest_cr;
    int linesize, uvlinesize, mb_x, mb_y;
    const int end_mb_y= s->mb_y + FRAME_MBAFF;
    const int old_slice_type= h->slice_type;

    if(h->deblocking_filter) {
        for(mb_x= 0; mb_x<s->mb_width; mb_x++){
            for(mb_y=end_mb_y - FRAME_MBAFF; mb_y<= end_mb_y; mb_y++){
                int mb_xy, mb_type;
                mb_xy = h->mb_xy = mb_x + mb_y*s->mb_stride;
                h->slice_num= h->slice_table[mb_xy];
                mb_type= s->current_picture.mb_type[mb_xy];
                h->list_count= h->list_counts[mb_xy];

                if(FRAME_MBAFF)
                    h->mb_mbaff = h->mb_field_decoding_flag = !!IS_INTERLACED(mb_type);

                s->mb_x= mb_x;
                s->mb_y= mb_y;
                dest_y  = s->current_picture.data[0] + (mb_x + mb_y * s->linesize  ) * 16;
                dest_cb = s->current_picture.data[1] + (mb_x + mb_y * s->uvlinesize) * 8;
                dest_cr = s->current_picture.data[2] + (mb_x + mb_y * s->uvlinesize) * 8;
                    //FIXME simplify above

                if (MB_FIELD) {
                    linesize   = h->mb_linesize   = s->linesize * 2;
                    uvlinesize = h->mb_uvlinesize = s->uvlinesize * 2;
                    if(mb_y&1){ //FIXME move out of this function?
                        dest_y -= s->linesize*15;
                        dest_cb-= s->uvlinesize*7;
                        dest_cr-= s->uvlinesize*7;
                    }
                } else {
                    linesize   = h->mb_linesize   = s->linesize;
                    uvlinesize = h->mb_uvlinesize = s->uvlinesize;
                }
                backup_mb_border(h, dest_y, dest_cb, dest_cr, linesize, uvlinesize, 0);
                if(fill_filter_caches(h, mb_type))
                    continue;
                h->chroma_qp[0] = get_chroma_qp(h, 0, s->current_picture.qscale_table[mb_xy]);
                h->chroma_qp[1] = get_chroma_qp(h, 1, s->current_picture.qscale_table[mb_xy]);

                if (FRAME_MBAFF) {
                    ff_h264_filter_mb     (h, mb_x, mb_y, dest_y, dest_cb, dest_cr, linesize, uvlinesize);
                } else {
                    ff_h264_filter_mb_fast(h, mb_x, mb_y, dest_y, dest_cb, dest_cr, linesize, uvlinesize);
                }
            }
        }
    }
    h->slice_type= old_slice_type;
    s->mb_x= 0;
    s->mb_y= end_mb_y - FRAME_MBAFF;
    h->chroma_qp[0] = get_chroma_qp(h, 0, s->qscale);
    h->chroma_qp[1] = get_chroma_qp(h, 1, s->qscale);
}

static void predict_field_decoding_flag(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;
    int mb_type = (h->slice_table[mb_xy-1] == h->slice_num)
                ? s->current_picture.mb_type[mb_xy-1]
                : (h->slice_table[mb_xy-s->mb_stride] == h->slice_num)
                ? s->current_picture.mb_type[mb_xy-s->mb_stride]
                : 0;
    h->mb_mbaff = h->mb_field_decoding_flag = IS_INTERLACED(mb_type) ? 1 : 0;
}

static int decode_slice(struct AVCodecContext *avctx, void *arg){
    H264Context *h = *(void**)arg;
    MpegEncContext * const s = &h->s;
    const int part_mask= s->partitioned_frame ? (AC_END|AC_ERROR) : 0x7F;

    s->mb_skip_run= -1;

    h->is_complex = FRAME_MBAFF || s->picture_structure != PICT_FRAME || s->codec_id != CODEC_ID_H264 ||
                    (CONFIG_GRAY && (s->flags&CODEC_FLAG_GRAY));

    if( h->pps.cabac ) {
        /* realign */
        align_get_bits( &s->gb );

        /* init cabac */
        ff_init_cabac_states( &h->cabac);
        ff_init_cabac_decoder( &h->cabac,
                               s->gb.buffer + get_bits_count(&s->gb)/8,
                               (get_bits_left(&s->gb) + 7)/8);

        ff_h264_init_cabac_states(h);

        for(;;){
//START_TIMER
            int ret = ff_h264_decode_mb_cabac(h);
            int eos;
//STOP_TIMER("decode_mb_cabac")

            if(ret>=0) ff_h264_hl_decode_mb(h);

            if( ret >= 0 && FRAME_MBAFF ) { //FIXME optimal? or let mb_decode decode 16x32 ?
                s->mb_y++;

                ret = ff_h264_decode_mb_cabac(h);

                if(ret>=0) ff_h264_hl_decode_mb(h);
                s->mb_y--;
            }
            eos = get_cabac_terminate( &h->cabac );

            if((s->workaround_bugs & FF_BUG_TRUNCATED) && h->cabac.bytestream > h->cabac.bytestream_end + 2){
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);
                return 0;
            }
            if( ret < 0 || h->cabac.bytestream > h->cabac.bytestream_end + 2) {
                av_log(h->s.avctx, AV_LOG_ERROR, "error while decoding MB %d %d, bytestream (%td)\n", s->mb_x, s->mb_y, h->cabac.bytestream_end - h->cabac.bytestream);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);
                return -1;
            }

            if( ++s->mb_x >= s->mb_width ) {
                s->mb_x = 0;
                loop_filter(h);
                ff_draw_horiz_band(s, 16*s->mb_y, 16);
                ++s->mb_y;
                if(FIELD_OR_MBAFF_PICTURE) {
                    ++s->mb_y;
                    if(FRAME_MBAFF && s->mb_y < s->mb_height)
                        predict_field_decoding_flag(h);
                }
            }

            if( eos || s->mb_y >= s->mb_height ) {
                tprintf(s->avctx, "slice end %d %d\n", get_bits_count(&s->gb), s->gb.size_in_bits);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);
                return 0;
            }
        }

    } else {
        for(;;){
            int ret = ff_h264_decode_mb_cavlc(h);

            if(ret>=0) ff_h264_hl_decode_mb(h);

            if(ret>=0 && FRAME_MBAFF){ //FIXME optimal? or let mb_decode decode 16x32 ?
                s->mb_y++;
                ret = ff_h264_decode_mb_cavlc(h);

                if(ret>=0) ff_h264_hl_decode_mb(h);
                s->mb_y--;
            }

            if(ret<0){
                av_log(h->s.avctx, AV_LOG_ERROR, "error while decoding MB %d %d\n", s->mb_x, s->mb_y);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

                return -1;
            }

            if(++s->mb_x >= s->mb_width){
                s->mb_x=0;
                loop_filter(h);
                ff_draw_horiz_band(s, 16*s->mb_y, 16);
                ++s->mb_y;
                if(FIELD_OR_MBAFF_PICTURE) {
                    ++s->mb_y;
                    if(FRAME_MBAFF && s->mb_y < s->mb_height)
                        predict_field_decoding_flag(h);
                }
                if(s->mb_y >= s->mb_height){
                    tprintf(s->avctx, "slice end %d %d\n", get_bits_count(&s->gb), s->gb.size_in_bits);

                    if(get_bits_count(&s->gb) == s->gb.size_in_bits ) {
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                        return 0;
                    }else{
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                        return -1;
                    }
                }
            }

            if(get_bits_count(&s->gb) >= s->gb.size_in_bits && s->mb_skip_run<=0){
                tprintf(s->avctx, "slice end %d %d\n", get_bits_count(&s->gb), s->gb.size_in_bits);
                if(get_bits_count(&s->gb) == s->gb.size_in_bits ){
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                    return 0;
                }else{
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

                    return -1;
                }
            }
        }
    }

#if 0
    for(;s->mb_y < s->mb_height; s->mb_y++){
        for(;s->mb_x < s->mb_width; s->mb_x++){
            int ret= decode_mb(h);

            ff_h264_hl_decode_mb(h);

            if(ret<0){
                av_log(s->avctx, AV_LOG_ERROR, "error while decoding MB %d %d\n", s->mb_x, s->mb_y);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

                return -1;
            }

            if(++s->mb_x >= s->mb_width){
                s->mb_x=0;
                if(++s->mb_y >= s->mb_height){
                    if(get_bits_count(s->gb) == s->gb.size_in_bits){
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                        return 0;
                    }else{
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                        return -1;
                    }
                }
            }

            if(get_bits_count(s->?gb) >= s->gb?.size_in_bits){
                if(get_bits_count(s->gb) == s->gb.size_in_bits){
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                    return 0;
                }else{
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

                    return -1;
                }
            }
        }
        s->mb_x=0;
        ff_draw_horiz_band(s, 16*s->mb_y, 16);
    }
#endif
    return -1; //not reached
}

/**
 * Call decode_slice() for each context.
 *
 * @param h h264 master context
 * @param context_count number of contexts to execute
 */
static void execute_decode_slices(H264Context *h, int context_count){
    MpegEncContext * const s = &h->s;
    AVCodecContext * const avctx= s->avctx;
    H264Context *hx;
    int i;

    if (s->avctx->hwaccel)
        return;
    if(s->avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU)
        return;
    if(context_count == 1) {
        decode_slice(avctx, &h);
    } else {
        for(i = 1; i < context_count; i++) {
            hx = h->thread_context[i];
            hx->s.error_recognition = avctx->error_recognition;
            hx->s.error_count = 0;
        }

        avctx->execute(avctx, (void *)decode_slice,
                       h->thread_context, NULL, context_count, sizeof(void*));

        /* pull back stuff from slices to master context */
        hx = h->thread_context[context_count - 1];
        s->mb_x = hx->s.mb_x;
        s->mb_y = hx->s.mb_y;
        s->dropable = hx->s.dropable;
        s->picture_structure = hx->s.picture_structure;
        for(i = 1; i < context_count; i++)
            h->s.error_count += h->thread_context[i]->s.error_count;
    }
}


static int decode_nal_units(H264Context *h, const uint8_t *buf, int buf_size){
    MpegEncContext * const s = &h->s;
    AVCodecContext * const avctx= s->avctx;
    int buf_index=0;
    H264Context *hx; ///< thread context
    int context_count = 0;
    int next_avc= h->is_avc ? 0 : buf_size;

    h->max_contexts = avctx->thread_count;
#if 0
    int i;
    for(i=0; i<50; i++){
        av_log(NULL, AV_LOG_ERROR,"%02X ", buf[i]);
    }
#endif
    if(!(s->flags2 & CODEC_FLAG2_CHUNKS)){
        h->current_slice = 0;
        if (!s->first_field)
            s->current_picture_ptr= NULL;
        ff_h264_reset_sei(h);
    }

    for(;;){
        int consumed;
        int dst_length;
        int bit_length;
        const uint8_t *ptr;
        int i, nalsize = 0;
        int err;

        if(buf_index >= next_avc) {
            if(buf_index >= buf_size) break;
            nalsize = 0;
            for(i = 0; i < h->nal_length_size; i++)
                nalsize = (nalsize << 8) | buf[buf_index++];
            if(nalsize <= 0 || nalsize > buf_size - buf_index){
                av_log(h->s.avctx, AV_LOG_ERROR, "AVC: nal size %d\n", nalsize);
                break;
            }
            next_avc= buf_index + nalsize;
        } else {
            // start code prefix search
            for(; buf_index + 3 < next_avc; buf_index++){
                // This should always succeed in the first iteration.
                if(buf[buf_index] == 0 && buf[buf_index+1] == 0 && buf[buf_index+2] == 1)
                    break;
            }

            if(buf_index+3 >= buf_size) break;

            buf_index+=3;
            if(buf_index >= next_avc) continue;
        }

        hx = h->thread_context[context_count];

        ptr= ff_h264_decode_nal(hx, buf + buf_index, &dst_length, &consumed, next_avc - buf_index);
        if (ptr==NULL || dst_length < 0){
            return -1;
        }
        i= buf_index + consumed;
        if((s->workaround_bugs & FF_BUG_AUTODETECT) && i+3<next_avc &&
           buf[i]==0x00 && buf[i+1]==0x00 && buf[i+2]==0x01 && buf[i+3]==0xE0)
            s->workaround_bugs |= FF_BUG_TRUNCATED;

        if(!(s->workaround_bugs & FF_BUG_TRUNCATED)){
        while(ptr[dst_length - 1] == 0 && dst_length > 0)
            dst_length--;
        }
        bit_length= !dst_length ? 0 : (8*dst_length - ff_h264_decode_rbsp_trailing(h, ptr + dst_length - 1));

        if(s->avctx->debug&FF_DEBUG_STARTCODE){
            av_log(h->s.avctx, AV_LOG_DEBUG, "NAL %d at %d/%d length %d\n", hx->nal_unit_type, buf_index, buf_size, dst_length);
        }

        if (h->is_avc && (nalsize != consumed) && nalsize){
            av_log(h->s.avctx, AV_LOG_DEBUG, "AVC: Consumed only %d bytes instead of %d\n", consumed, nalsize);
        }

        buf_index += consumed;

        if(  (s->hurry_up == 1 && h->nal_ref_idc  == 0) //FIXME do not discard SEI id
           ||(avctx->skip_frame >= AVDISCARD_NONREF && h->nal_ref_idc  == 0))
            continue;

      again:
        err = 0;
        switch(hx->nal_unit_type){
        case NAL_IDR_SLICE:
            if (h->nal_unit_type != NAL_IDR_SLICE) {
                av_log(h->s.avctx, AV_LOG_ERROR, "Invalid mix of idr and non-idr slices");
                return -1;
            }
            idr(h); //FIXME ensure we don't loose some frames if there is reordering
        case NAL_SLICE:
            init_get_bits(&hx->s.gb, ptr, bit_length);
            hx->intra_gb_ptr=
            hx->inter_gb_ptr= &hx->s.gb;
            hx->s.data_partitioning = 0;

            if((err = decode_slice_header(hx, h)))
               break;

            if (h->current_slice == 1) {
                if (s->avctx->hwaccel && s->avctx->hwaccel->start_frame(s->avctx, NULL, 0) < 0)
                    return -1;
                if(CONFIG_H264_VDPAU_DECODER && s->avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU)
                    ff_vdpau_h264_picture_start(s);
            }

            s->current_picture_ptr->key_frame |=
                    (hx->nal_unit_type == NAL_IDR_SLICE) ||
                    (h->sei_recovery_frame_cnt >= 0);
            if(hx->redundant_pic_count==0 && hx->s.hurry_up < 5
               && (avctx->skip_frame < AVDISCARD_NONREF || hx->nal_ref_idc)
               && (avctx->skip_frame < AVDISCARD_BIDIR  || hx->slice_type_nos!=FF_B_TYPE)
               && (avctx->skip_frame < AVDISCARD_NONKEY || hx->slice_type_nos==FF_I_TYPE)
               && avctx->skip_frame < AVDISCARD_ALL){
                if(avctx->hwaccel) {
                    if (avctx->hwaccel->decode_slice(avctx, &buf[buf_index - consumed], consumed) < 0)
                        return -1;
                }else
                if(CONFIG_H264_VDPAU_DECODER && s->avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU){
                    static const uint8_t start_code[] = {0x00, 0x00, 0x01};
                    ff_vdpau_add_data_chunk(s, start_code, sizeof(start_code));
                    ff_vdpau_add_data_chunk(s, &buf[buf_index - consumed], consumed );
                }else
                    context_count++;
            }
            break;
        case NAL_DPA:
            init_get_bits(&hx->s.gb, ptr, bit_length);
            hx->intra_gb_ptr=
            hx->inter_gb_ptr= NULL;

            if ((err = decode_slice_header(hx, h)) < 0)
                break;

            hx->s.data_partitioning = 1;

            break;
        case NAL_DPB:
            init_get_bits(&hx->intra_gb, ptr, bit_length);
            hx->intra_gb_ptr= &hx->intra_gb;
            break;
        case NAL_DPC:
            init_get_bits(&hx->inter_gb, ptr, bit_length);
            hx->inter_gb_ptr= &hx->inter_gb;

            if(hx->redundant_pic_count==0 && hx->intra_gb_ptr && hx->s.data_partitioning
               && s->context_initialized
               && s->hurry_up < 5
               && (avctx->skip_frame < AVDISCARD_NONREF || hx->nal_ref_idc)
               && (avctx->skip_frame < AVDISCARD_BIDIR  || hx->slice_type_nos!=FF_B_TYPE)
               && (avctx->skip_frame < AVDISCARD_NONKEY || hx->slice_type_nos==FF_I_TYPE)
               && avctx->skip_frame < AVDISCARD_ALL)
                context_count++;
            break;
        case NAL_SEI:
            init_get_bits(&s->gb, ptr, bit_length);
            ff_h264_decode_sei(h);
            break;
        case NAL_SPS:
            init_get_bits(&s->gb, ptr, bit_length);
            ff_h264_decode_seq_parameter_set(h);

            if(s->flags& CODEC_FLAG_LOW_DELAY)
                s->low_delay=1;

            if(avctx->has_b_frames < 2)
                avctx->has_b_frames= !s->low_delay;
            break;
        case NAL_PPS:
            init_get_bits(&s->gb, ptr, bit_length);

            ff_h264_decode_picture_parameter_set(h, bit_length);

            break;
        case NAL_AUD:
        case NAL_END_SEQUENCE:
        case NAL_END_STREAM:
        case NAL_FILLER_DATA:
        case NAL_SPS_EXT:
        case NAL_AUXILIARY_SLICE:
            break;
        default:
            av_log(avctx, AV_LOG_DEBUG, "Unknown NAL code: %d (%d bits)\n", hx->nal_unit_type, bit_length);
        }

        if(context_count == h->max_contexts) {
            execute_decode_slices(h, context_count);
            context_count = 0;
        }

        if (err < 0)
            av_log(h->s.avctx, AV_LOG_ERROR, "decode_slice_header error\n");
        else if(err == 1) {
            /* Slice could not be decoded in parallel mode, copy down
             * NAL unit stuff to context 0 and restart. Note that
             * rbsp_buffer is not transferred, but since we no longer
             * run in parallel mode this should not be an issue. */
            h->nal_unit_type = hx->nal_unit_type;
            h->nal_ref_idc   = hx->nal_ref_idc;
            hx = h;
            goto again;
        }
    }
    if(context_count)
        execute_decode_slices(h, context_count);
    return buf_index;
}

/**
 * returns the number of bytes consumed for building the current frame
 */
static int get_consumed_bytes(MpegEncContext *s, int pos, int buf_size){
        if(pos==0) pos=1; //avoid infinite loops (i doubt that is needed but ...)
        if(pos+10>buf_size) pos=buf_size; // oops ;)

        return pos;
}

static int decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    H264Context *h = avctx->priv_data;
    MpegEncContext *s = &h->s;
    AVFrame *pict = data;
    int buf_index;

    s->flags= avctx->flags;
    s->flags2= avctx->flags2;

   /* end of stream, output what is still in the buffers */
 out:
    if (buf_size == 0) {
        Picture *out;
        int i, out_idx;

//FIXME factorize this with the output code below
        out = h->delayed_pic[0];
        out_idx = 0;
        for(i=1; h->delayed_pic[i] && !h->delayed_pic[i]->key_frame && !h->delayed_pic[i]->mmco_reset; i++)
            if(h->delayed_pic[i]->poc < out->poc){
                out = h->delayed_pic[i];
                out_idx = i;
            }

        for(i=out_idx; h->delayed_pic[i]; i++)
            h->delayed_pic[i] = h->delayed_pic[i+1];

        if(out){
            *data_size = sizeof(AVFrame);
            *pict= *(AVFrame*)out;
        }

        return 0;
    }

    buf_index=decode_nal_units(h, buf, buf_size);
    if(buf_index < 0)
        return -1;

    if (!s->current_picture_ptr && h->nal_unit_type == NAL_END_SEQUENCE) {
        buf_size = 0;
        goto out;
    }

    if(!(s->flags2 & CODEC_FLAG2_CHUNKS) && !s->current_picture_ptr){
        if (avctx->skip_frame >= AVDISCARD_NONREF || s->hurry_up) return 0;
        av_log(avctx, AV_LOG_ERROR, "no frame!\n");
        return -1;
    }

    if(!(s->flags2 & CODEC_FLAG2_CHUNKS) || (s->mb_y >= s->mb_height && s->mb_height)){
        Picture *out = s->current_picture_ptr;
        Picture *cur = s->current_picture_ptr;
        int i, pics, out_of_order, out_idx;

        field_end(h);

        if (cur->field_poc[0]==INT_MAX || cur->field_poc[1]==INT_MAX) {
            /* Wait for second field. */
            *data_size = 0;

        } else {
            cur->interlaced_frame = 0;
            cur->repeat_pict = 0;

            /* Signal interlacing information externally. */
            /* Prioritize picture timing SEI information over used decoding process if it exists. */

            if(h->sps.pic_struct_present_flag){
                switch (h->sei_pic_struct)
                {
                case SEI_PIC_STRUCT_FRAME:
                    break;
                case SEI_PIC_STRUCT_TOP_FIELD:
                case SEI_PIC_STRUCT_BOTTOM_FIELD:
                    cur->interlaced_frame = 1;
                    break;
                case SEI_PIC_STRUCT_TOP_BOTTOM:
                case SEI_PIC_STRUCT_BOTTOM_TOP:
                    if (FIELD_OR_MBAFF_PICTURE)
                        cur->interlaced_frame = 1;
                    else
                        // try to flag soft telecine progressive
                        cur->interlaced_frame = h->prev_interlaced_frame;
                    break;
                case SEI_PIC_STRUCT_TOP_BOTTOM_TOP:
                case SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM:
                    // Signal the possibility of telecined film externally (pic_struct 5,6)
                    // From these hints, let the applications decide if they apply deinterlacing.
                    cur->repeat_pict = 1;
                    break;
                case SEI_PIC_STRUCT_FRAME_DOUBLING:
                    // Force progressive here, as doubling interlaced frame is a bad idea.
                    cur->repeat_pict = 2;
                    break;
                case SEI_PIC_STRUCT_FRAME_TRIPLING:
                    cur->repeat_pict = 4;
                    break;
                }

                if ((h->sei_ct_type & 3) && h->sei_pic_struct <= SEI_PIC_STRUCT_BOTTOM_TOP)
                    cur->interlaced_frame = (h->sei_ct_type & (1<<1)) != 0;
            }else{
                /* Derive interlacing flag from used decoding process. */
                cur->interlaced_frame = FIELD_OR_MBAFF_PICTURE;
            }
            h->prev_interlaced_frame = cur->interlaced_frame;

            if (cur->field_poc[0] != cur->field_poc[1]){
                /* Derive top_field_first from field pocs. */
                cur->top_field_first = cur->field_poc[0] < cur->field_poc[1];
            }else{
                if(cur->interlaced_frame || h->sps.pic_struct_present_flag){
                    /* Use picture timing SEI information. Even if it is a information of a past frame, better than nothing. */
                    if(h->sei_pic_struct == SEI_PIC_STRUCT_TOP_BOTTOM
                      || h->sei_pic_struct == SEI_PIC_STRUCT_TOP_BOTTOM_TOP)
                        cur->top_field_first = 1;
                    else
                        cur->top_field_first = 0;
                }else{
                    /* Most likely progressive */
                    cur->top_field_first = 0;
                }
            }

        //FIXME do something with unavailable reference frames

            /* Sort B-frames into display order */

            if(h->sps.bitstream_restriction_flag
               && s->avctx->has_b_frames < h->sps.num_reorder_frames){
                s->avctx->has_b_frames = h->sps.num_reorder_frames;
                s->low_delay = 0;
            }

            if(   s->avctx->strict_std_compliance >= FF_COMPLIANCE_STRICT
               && !h->sps.bitstream_restriction_flag){
                s->avctx->has_b_frames= MAX_DELAYED_PIC_COUNT;
                s->low_delay= 0;
            }

            pics = 0;
            while(h->delayed_pic[pics]) pics++;

            assert(pics <= MAX_DELAYED_PIC_COUNT);

            h->delayed_pic[pics++] = cur;
            if(cur->reference == 0)
                cur->reference = DELAYED_PIC_REF;

            out = h->delayed_pic[0];
            out_idx = 0;
            for(i=1; h->delayed_pic[i] && !h->delayed_pic[i]->key_frame && !h->delayed_pic[i]->mmco_reset; i++)
                if(h->delayed_pic[i]->poc < out->poc){
                    out = h->delayed_pic[i];
                    out_idx = i;
                }
            if(s->avctx->has_b_frames == 0 && (h->delayed_pic[0]->key_frame || h->delayed_pic[0]->mmco_reset))
                h->outputed_poc= INT_MIN;
            out_of_order = out->poc < h->outputed_poc;

            if(h->sps.bitstream_restriction_flag && s->avctx->has_b_frames >= h->sps.num_reorder_frames)
                { }
            else if((out_of_order && pics-1 == s->avctx->has_b_frames && s->avctx->has_b_frames < MAX_DELAYED_PIC_COUNT)
               || (s->low_delay &&
                ((h->outputed_poc != INT_MIN && out->poc > h->outputed_poc + 2)
                 || cur->pict_type == FF_B_TYPE)))
            {
                s->low_delay = 0;
                s->avctx->has_b_frames++;
            }

            if(out_of_order || pics > s->avctx->has_b_frames){
                out->reference &= ~DELAYED_PIC_REF;
                for(i=out_idx; h->delayed_pic[i]; i++)
                    h->delayed_pic[i] = h->delayed_pic[i+1];
            }
            if(!out_of_order && pics > s->avctx->has_b_frames){
                *data_size = sizeof(AVFrame);

                if(out_idx==0 && h->delayed_pic[0] && (h->delayed_pic[0]->key_frame || h->delayed_pic[0]->mmco_reset)) {
                    h->outputed_poc = INT_MIN;
                } else
                    h->outputed_poc = out->poc;
                *pict= *(AVFrame*)out;
            }else{
                av_log(avctx, AV_LOG_DEBUG, "no picture\n");
            }
        }
    }

    assert(pict->data[0] || !*data_size);
    ff_print_debug_info(s, pict);
//printf("out %d\n", (int)pict->data[0]);

    return get_consumed_bytes(s, buf_index, buf_size);
}
#if 0
static inline void fill_mb_avail(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;

    if(s->mb_y){
        h->mb_avail[0]= s->mb_x                 && h->slice_table[mb_xy - s->mb_stride - 1] == h->slice_num;
        h->mb_avail[1]=                            h->slice_table[mb_xy - s->mb_stride    ] == h->slice_num;
        h->mb_avail[2]= s->mb_x+1 < s->mb_width && h->slice_table[mb_xy - s->mb_stride + 1] == h->slice_num;
    }else{
        h->mb_avail[0]=
        h->mb_avail[1]=
        h->mb_avail[2]= 0;
    }
    h->mb_avail[3]= s->mb_x && h->slice_table[mb_xy - 1] == h->slice_num;
    h->mb_avail[4]= 1; //FIXME move out
    h->mb_avail[5]= 0; //FIXME move out
}
#endif

#ifdef TEST
#undef printf
#undef random
#define COUNT 8000
#define SIZE (COUNT*40)
int main(void){
    int i;
    uint8_t temp[SIZE];
    PutBitContext pb;
    GetBitContext gb;
//    int int_temp[10000];
    DSPContext dsp;
    AVCodecContext avctx;

    dsputil_init(&dsp, &avctx);

    init_put_bits(&pb, temp, SIZE);
    printf("testing unsigned exp golomb\n");
    for(i=0; i<COUNT; i++){
        START_TIMER
        set_ue_golomb(&pb, i);
        STOP_TIMER("set_ue_golomb");
    }
    flush_put_bits(&pb);

    init_get_bits(&gb, temp, 8*SIZE);
    for(i=0; i<COUNT; i++){
        int j, s;

        s= show_bits(&gb, 24);

        START_TIMER
        j= get_ue_golomb(&gb);
        if(j != i){
            printf("mismatch! at %d (%d should be %d) bits:%6X\n", i, j, i, s);
//            return -1;
        }
        STOP_TIMER("get_ue_golomb");
    }


    init_put_bits(&pb, temp, SIZE);
    printf("testing signed exp golomb\n");
    for(i=0; i<COUNT; i++){
        START_TIMER
        set_se_golomb(&pb, i - COUNT/2);
        STOP_TIMER("set_se_golomb");
    }
    flush_put_bits(&pb);

    init_get_bits(&gb, temp, 8*SIZE);
    for(i=0; i<COUNT; i++){
        int j, s;

        s= show_bits(&gb, 24);

        START_TIMER
        j= get_se_golomb(&gb);
        if(j != i - COUNT/2){
            printf("mismatch! at %d (%d should be %d) bits:%6X\n", i, j, i, s);
//            return -1;
        }
        STOP_TIMER("get_se_golomb");
    }

#if 0
    printf("testing 4x4 (I)DCT\n");

    DCTELEM block[16];
    uint8_t src[16], ref[16];
    uint64_t error= 0, max_error=0;

    for(i=0; i<COUNT; i++){
        int j;
//        printf("%d %d %d\n", r1, r2, (r2-r1)*16);
        for(j=0; j<16; j++){
            ref[j]= random()%255;
            src[j]= random()%255;
        }

        h264_diff_dct_c(block, src, ref, 4);

        //normalize
        for(j=0; j<16; j++){
//            printf("%d ", block[j]);
            block[j]= block[j]*4;
            if(j&1) block[j]= (block[j]*4 + 2)/5;
            if(j&4) block[j]= (block[j]*4 + 2)/5;
        }
//        printf("\n");

        h->h264dsp.h264_idct_add(ref, block, 4);
/*        for(j=0; j<16; j++){
            printf("%d ", ref[j]);
        }
        printf("\n");*/

        for(j=0; j<16; j++){
            int diff= FFABS(src[j] - ref[j]);

            error+= diff*diff;
            max_error= FFMAX(max_error, diff);
        }
    }
    printf("error=%f max_error=%d\n", ((float)error)/COUNT/16, (int)max_error );
    printf("testing quantizer\n");
    for(qp=0; qp<52; qp++){
        for(i=0; i<16; i++)
            src1_block[i]= src2_block[i]= random()%255;

    }
    printf("Testing NAL layer\n");

    uint8_t bitstream[COUNT];
    uint8_t nal[COUNT*2];
    H264Context h;
    memset(&h, 0, sizeof(H264Context));

    for(i=0; i<COUNT; i++){
        int zeros= i;
        int nal_length;
        int consumed;
        int out_length;
        uint8_t *out;
        int j;

        for(j=0; j<COUNT; j++){
            bitstream[j]= (random() % 255) + 1;
        }

        for(j=0; j<zeros; j++){
            int pos= random() % COUNT;
            while(bitstream[pos] == 0){
                pos++;
                pos %= COUNT;
            }
            bitstream[pos]=0;
        }

        START_TIMER

        nal_length= encode_nal(&h, nal, bitstream, COUNT, COUNT*2);
        if(nal_length<0){
            printf("encoding failed\n");
            return -1;
        }

        out= ff_h264_decode_nal(&h, nal, &out_length, &consumed, nal_length);

        STOP_TIMER("NAL")

        if(out_length != COUNT){
            printf("incorrect length %d %d\n", out_length, COUNT);
            return -1;
        }

        if(consumed != nal_length){
            printf("incorrect consumed length %d %d\n", nal_length, consumed);
            return -1;
        }

        if(memcmp(bitstream, out, COUNT)){
            printf("mismatch\n");
            return -1;
        }
    }
#endif

    printf("Testing RBSP\n");


    return 0;
}
#endif /* TEST */


av_cold void ff_h264_free_context(H264Context *h)
{
    int i;

    free_tables(h); //FIXME cleanup init stuff perhaps

    for(i = 0; i < MAX_SPS_COUNT; i++)
        av_freep(h->sps_buffers + i);

    for(i = 0; i < MAX_PPS_COUNT; i++)
        av_freep(h->pps_buffers + i);
}

av_cold int ff_h264_decode_end(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    MpegEncContext *s = &h->s;

    ff_h264_free_context(h);

    MPV_common_end(s);

//    memset(h, 0, sizeof(H264Context));

    return 0;
}


AVCodec h264_decoder = {
    "h264",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_H264,
    sizeof(H264Context),
    ff_h264_decode_init,
    NULL,
    ff_h264_decode_end,
    decode_frame,
    /*CODEC_CAP_DRAW_HORIZ_BAND |*/ CODEC_CAP_DR1 | CODEC_CAP_DELAY,
    .flush= flush_dpb,
    .long_name = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"),
};

#if CONFIG_H264_VDPAU_DECODER
AVCodec h264_vdpau_decoder = {
    "h264_vdpau",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_H264,
    sizeof(H264Context),
    ff_h264_decode_init,
    NULL,
    ff_h264_decode_end,
    decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DELAY | CODEC_CAP_HWACCEL_VDPAU,
    .flush= flush_dpb,
    .long_name = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (VDPAU acceleration)"),
    .pix_fmts = (const enum PixelFormat[]){PIX_FMT_VDPAU_H264, PIX_FMT_NONE},
};
#endif
