/*
 * Copyright (C) 2004 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/intmath.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "snow_dwt.h"
#include "internal.h"
#include "snow.h"

#include "rangecoder.h"
#include "mathops.h"

#include "mpegvideo.h"
#include "h263.h"

static av_always_inline void predict_slice_buffered(SnowContext *s, slice_buffer * sb, IDWTELEM * old_buffer, int plane_index, int add, int mb_y){
    Plane *p= &s->plane[plane_index];
    const int mb_w= s->b_width  << s->block_max_depth;
    const int mb_h= s->b_height << s->block_max_depth;
    int x, y, mb_x;
    int block_size = MB_SIZE >> s->block_max_depth;
    int block_w    = plane_index ? block_size>>s->chroma_h_shift : block_size;
    int block_h    = plane_index ? block_size>>s->chroma_v_shift : block_size;
    const uint8_t *obmc  = plane_index ? ff_obmc_tab[s->block_max_depth+s->chroma_h_shift] : ff_obmc_tab[s->block_max_depth];
    int obmc_stride= plane_index ? (2*block_size)>>s->chroma_h_shift : 2*block_size;
    int ref_stride= s->current_picture->linesize[plane_index];
    uint8_t *dst8= s->current_picture->data[plane_index];
    int w= p->width;
    int h= p->height;

    if(s->keyframe || (s->avctx->debug&512)){
        if(mb_y==mb_h)
            return;

        if(add){
            for(y=block_h*mb_y; y<FFMIN(h,block_h*(mb_y+1)); y++){
//                DWTELEM * line = slice_buffer_get_line(sb, y);
                IDWTELEM * line = sb->line[y];
                for(x=0; x<w; x++){
//                    int v= buf[x + y*w] + (128<<FRAC_BITS) + (1<<(FRAC_BITS-1));
                    int v= line[x] + (128<<FRAC_BITS) + (1<<(FRAC_BITS-1));
                    v >>= FRAC_BITS;
                    if(v&(~255)) v= ~(v>>31);
                    dst8[x + y*ref_stride]= v;
                }
            }
        }else{
            for(y=block_h*mb_y; y<FFMIN(h,block_h*(mb_y+1)); y++){
//                DWTELEM * line = slice_buffer_get_line(sb, y);
                IDWTELEM * line = sb->line[y];
                for(x=0; x<w; x++){
                    line[x] -= 128 << FRAC_BITS;
//                    buf[x + y*w]-= 128<<FRAC_BITS;
                }
            }
        }

        return;
    }

    for(mb_x=0; mb_x<=mb_w; mb_x++){
        add_yblock(s, 1, sb, old_buffer, dst8, obmc,
                   block_w*mb_x - block_w/2,
                   block_h*mb_y - block_h/2,
                   block_w, block_h,
                   w, h,
                   w, ref_stride, obmc_stride,
                   mb_x - 1, mb_y - 1,
                   add, 0, plane_index);
    }

    if(s->avmv && mb_y < mb_h && plane_index == 0)
        for(mb_x=0; mb_x<mb_w; mb_x++){
            AVMotionVector *avmv = s->avmv + s->avmv_index;
            const int b_width = s->b_width  << s->block_max_depth;
            const int b_stride= b_width;
            BlockNode *bn= &s->block[mb_x + mb_y*b_stride];

            if (bn->type)
                continue;

            s->avmv_index++;

            avmv->w = block_w;
            avmv->h = block_h;
            avmv->dst_x = block_w*mb_x - block_w/2;
            avmv->dst_y = block_h*mb_y - block_h/2;
            avmv->motion_scale = 8;
            avmv->motion_x = bn->mx * s->mv_scale;
            avmv->motion_y = bn->my * s->mv_scale;
            avmv->src_x = avmv->dst_x + avmv->motion_x / 8;
            avmv->src_y = avmv->dst_y + avmv->motion_y / 8;
            avmv->source= -1 - bn->ref;
            avmv->flags = 0;
        }
}

static inline void decode_subband_slice_buffered(SnowContext *s, SubBand *b, slice_buffer * sb, int start_y, int h, int save_state[1]){
    const int w= b->width;
    int y;
    const int qlog= av_clip(s->qlog + b->qlog, 0, QROOT*16);
    int qmul= ff_qexp[qlog&(QROOT-1)]<<(qlog>>QSHIFT);
    int qadd= (s->qbias*qmul)>>QBIAS_SHIFT;
    int new_index = 0;

    if(b->ibuf == s->spatial_idwt_buffer || s->qlog == LOSSLESS_QLOG){
        qadd= 0;
        qmul= 1<<QEXPSHIFT;
    }

    /* If we are on the second or later slice, restore our index. */
    if (start_y != 0)
        new_index = save_state[0];


    for(y=start_y; y<h; y++){
        int x = 0;
        int v;
        IDWTELEM * line = slice_buffer_get_line(sb, y * b->stride_line + b->buf_y_offset) + b->buf_x_offset;
        memset(line, 0, b->width*sizeof(IDWTELEM));
        v = b->x_coeff[new_index].coeff;
        x = b->x_coeff[new_index++].x;
        while(x < w){
            register int t= (int)( (v>>1)*(unsigned)qmul + qadd)>>QEXPSHIFT;
            register int u= -(v&1);
            line[x] = (t^u) - u;

            v = b->x_coeff[new_index].coeff;
            x = b->x_coeff[new_index++].x;
        }
    }

    /* Save our variables for the next slice. */
    save_state[0] = new_index;

    return;
}

static int decode_q_branch(SnowContext *s, int level, int x, int y){
    const int w= s->b_width << s->block_max_depth;
    const int rem_depth= s->block_max_depth - level;
    const int index= (x + y*w) << rem_depth;
    int trx= (x+1)<<rem_depth;
    const BlockNode *left  = x ? &s->block[index-1] : &null_block;
    const BlockNode *top   = y ? &s->block[index-w] : &null_block;
    const BlockNode *tl    = y && x ? &s->block[index-w-1] : left;
    const BlockNode *tr    = y && trx<w && ((x&1)==0 || level==0) ? &s->block[index-w+(1<<rem_depth)] : tl; //FIXME use lt
    int s_context= 2*left->level + 2*top->level + tl->level + tr->level;
    int res;

    if(s->keyframe){
        set_blocks(s, level, x, y, null_block.color[0], null_block.color[1], null_block.color[2], null_block.mx, null_block.my, null_block.ref, BLOCK_INTRA);
        return 0;
    }

    if(level==s->block_max_depth || get_rac(&s->c, &s->block_state[4 + s_context])){
        int type, mx, my;
        int l = left->color[0];
        int cb= left->color[1];
        int cr= left->color[2];
        unsigned ref = 0;
        int ref_context= av_log2(2*left->ref) + av_log2(2*top->ref);
        int mx_context= av_log2(2*FFABS(left->mx - top->mx)) + 0*av_log2(2*FFABS(tr->mx - top->mx));
        int my_context= av_log2(2*FFABS(left->my - top->my)) + 0*av_log2(2*FFABS(tr->my - top->my));

        type= get_rac(&s->c, &s->block_state[1 + left->type + top->type]) ? BLOCK_INTRA : 0;
        if(type){
            int ld, cbd, crd;
            pred_mv(s, &mx, &my, 0, left, top, tr);
            ld = get_symbol(&s->c, &s->block_state[32], 1);
            if (ld < -255 || ld > 255) {
                return AVERROR_INVALIDDATA;
            }
            l += ld;
            if (s->nb_planes > 2) {
                cbd = get_symbol(&s->c, &s->block_state[64], 1);
                crd = get_symbol(&s->c, &s->block_state[96], 1);
                if (cbd < -255 || cbd > 255 || crd < -255 || crd > 255) {
                    return AVERROR_INVALIDDATA;
                }
                cb += cbd;
                cr += crd;
            }
        }else{
            if(s->ref_frames > 1)
                ref= get_symbol(&s->c, &s->block_state[128 + 1024 + 32*ref_context], 0);
            if (ref >= s->ref_frames) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid ref\n");
                return AVERROR_INVALIDDATA;
            }
            pred_mv(s, &mx, &my, ref, left, top, tr);
            mx+= (unsigned)get_symbol(&s->c, &s->block_state[128 + 32*(mx_context + 16*!!ref)], 1);
            my+= (unsigned)get_symbol(&s->c, &s->block_state[128 + 32*(my_context + 16*!!ref)], 1);
        }
        set_blocks(s, level, x, y, l, cb, cr, mx, my, ref, type);
    }else{
        if ((res = decode_q_branch(s, level+1, 2*x+0, 2*y+0)) < 0 ||
            (res = decode_q_branch(s, level+1, 2*x+1, 2*y+0)) < 0 ||
            (res = decode_q_branch(s, level+1, 2*x+0, 2*y+1)) < 0 ||
            (res = decode_q_branch(s, level+1, 2*x+1, 2*y+1)) < 0)
            return res;
    }
    return 0;
}

static void dequantize_slice_buffered(SnowContext *s, slice_buffer * sb, SubBand *b, IDWTELEM *src, int stride, int start_y, int end_y){
    const int w= b->width;
    const int qlog= av_clip(s->qlog + b->qlog, 0, QROOT*16);
    const int qmul= ff_qexp[qlog&(QROOT-1)]<<(qlog>>QSHIFT);
    const int qadd= (s->qbias*qmul)>>QBIAS_SHIFT;
    int x,y;

    if(s->qlog == LOSSLESS_QLOG) return;

    for(y=start_y; y<end_y; y++){
//        DWTELEM * line = slice_buffer_get_line_from_address(sb, src + (y * stride));
        IDWTELEM * line = slice_buffer_get_line(sb, (y * b->stride_line) + b->buf_y_offset) + b->buf_x_offset;
        for(x=0; x<w; x++){
            int i= line[x];
            if(i<0){
                line[x]= -((-i*(unsigned)qmul + qadd)>>(QEXPSHIFT)); //FIXME try different bias
            }else if(i>0){
                line[x]=  (( i*(unsigned)qmul + qadd)>>(QEXPSHIFT));
            }
        }
    }
}

static void correlate_slice_buffered(SnowContext *s, slice_buffer * sb, SubBand *b, IDWTELEM *src, int stride, int inverse, int use_median, int start_y, int end_y){
    const int w= b->width;
    int x,y;

    IDWTELEM * line=0; // silence silly "could be used without having been initialized" warning
    IDWTELEM * prev;

    if (start_y != 0)
        line = slice_buffer_get_line(sb, ((start_y - 1) * b->stride_line) + b->buf_y_offset) + b->buf_x_offset;

    for(y=start_y; y<end_y; y++){
        prev = line;
//        line = slice_buffer_get_line_from_address(sb, src + (y * stride));
        line = slice_buffer_get_line(sb, (y * b->stride_line) + b->buf_y_offset) + b->buf_x_offset;
        for(x=0; x<w; x++){
            if(x){
                if(use_median){
                    if(y && x+1<w) line[x] += mid_pred(line[x - 1], prev[x], prev[x + 1]);
                    else  line[x] += line[x - 1];
                }else{
                    if(y) line[x] += mid_pred(line[x - 1], prev[x], line[x - 1] + prev[x] - prev[x - 1]);
                    else  line[x] += line[x - 1];
                }
            }else{
                if(y) line[x] += prev[x];
            }
        }
    }
}

static void decode_qlogs(SnowContext *s){
    int plane_index, level, orientation;

    for(plane_index=0; plane_index < s->nb_planes; plane_index++){
        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1:0; orientation<4; orientation++){
                int q;
                if     (plane_index==2) q= s->plane[1].band[level][orientation].qlog;
                else if(orientation==2) q= s->plane[plane_index].band[level][1].qlog;
                else                    q= get_symbol(&s->c, s->header_state, 1);
                s->plane[plane_index].band[level][orientation].qlog= q;
            }
        }
    }
}

#define GET_S(dst, check) \
    tmp= get_symbol(&s->c, s->header_state, 0);\
    if(!(check)){\
        av_log(s->avctx, AV_LOG_ERROR, "Error " #dst " is %d\n", tmp);\
        return AVERROR_INVALIDDATA;\
    }\
    dst= tmp;

static int decode_header(SnowContext *s){
    int plane_index, tmp;
    uint8_t kstate[32];

    memset(kstate, MID_STATE, sizeof(kstate));

    s->keyframe= get_rac(&s->c, kstate);
    if(s->keyframe || s->always_reset){
        ff_snow_reset_contexts(s);
        s->spatial_decomposition_type=
        s->qlog=
        s->qbias=
        s->mv_scale=
        s->block_max_depth= 0;
    }
    if(s->keyframe){
        GET_S(s->version, tmp <= 0U)
        s->always_reset= get_rac(&s->c, s->header_state);
        s->temporal_decomposition_type= get_symbol(&s->c, s->header_state, 0);
        s->temporal_decomposition_count= get_symbol(&s->c, s->header_state, 0);
        GET_S(s->spatial_decomposition_count, 0 < tmp && tmp <= MAX_DECOMPOSITIONS)
        s->colorspace_type= get_symbol(&s->c, s->header_state, 0);
        if (s->colorspace_type == 1) {
            s->avctx->pix_fmt= AV_PIX_FMT_GRAY8;
            s->nb_planes = 1;
        } else if(s->colorspace_type == 0) {
            s->chroma_h_shift= get_symbol(&s->c, s->header_state, 0);
            s->chroma_v_shift= get_symbol(&s->c, s->header_state, 0);

            if(s->chroma_h_shift == 1 && s->chroma_v_shift==1){
                s->avctx->pix_fmt= AV_PIX_FMT_YUV420P;
            }else if(s->chroma_h_shift == 0 && s->chroma_v_shift==0){
                s->avctx->pix_fmt= AV_PIX_FMT_YUV444P;
            }else if(s->chroma_h_shift == 2 && s->chroma_v_shift==2){
                s->avctx->pix_fmt= AV_PIX_FMT_YUV410P;
            } else {
                av_log(s, AV_LOG_ERROR, "unsupported color subsample mode %d %d\n", s->chroma_h_shift, s->chroma_v_shift);
                s->chroma_h_shift = s->chroma_v_shift = 1;
                s->avctx->pix_fmt= AV_PIX_FMT_YUV420P;
                return AVERROR_INVALIDDATA;
            }
            s->nb_planes = 3;
        } else {
            av_log(s, AV_LOG_ERROR, "unsupported color space\n");
            s->chroma_h_shift = s->chroma_v_shift = 1;
            s->avctx->pix_fmt= AV_PIX_FMT_YUV420P;
            return AVERROR_INVALIDDATA;
        }


        s->spatial_scalability= get_rac(&s->c, s->header_state);
//        s->rate_scalability= get_rac(&s->c, s->header_state);
        GET_S(s->max_ref_frames, tmp < (unsigned)MAX_REF_FRAMES)
        s->max_ref_frames++;

        decode_qlogs(s);
    }

    if(!s->keyframe){
        if(get_rac(&s->c, s->header_state)){
            for(plane_index=0; plane_index<FFMIN(s->nb_planes, 2); plane_index++){
                int htaps, i, sum=0;
                Plane *p= &s->plane[plane_index];
                p->diag_mc= get_rac(&s->c, s->header_state);
                htaps= get_symbol(&s->c, s->header_state, 0);
                if((unsigned)htaps >= HTAPS_MAX/2 - 1)
                    return AVERROR_INVALIDDATA;
                htaps = htaps*2 + 2;
                p->htaps= htaps;
                for(i= htaps/2; i; i--){
                    p->hcoeff[i]= get_symbol(&s->c, s->header_state, 0) * (1-2*(i&1));
                    sum += p->hcoeff[i];
                }
                p->hcoeff[0]= 32-sum;
            }
            s->plane[2].diag_mc= s->plane[1].diag_mc;
            s->plane[2].htaps  = s->plane[1].htaps;
            memcpy(s->plane[2].hcoeff, s->plane[1].hcoeff, sizeof(s->plane[1].hcoeff));
        }
        if(get_rac(&s->c, s->header_state)){
            GET_S(s->spatial_decomposition_count, 0 < tmp && tmp <= MAX_DECOMPOSITIONS)
            decode_qlogs(s);
        }
    }

    s->spatial_decomposition_type+= (unsigned)get_symbol(&s->c, s->header_state, 1);
    if(s->spatial_decomposition_type > 1U){
        av_log(s->avctx, AV_LOG_ERROR, "spatial_decomposition_type %d not supported\n", s->spatial_decomposition_type);
        return AVERROR_INVALIDDATA;
    }
    if(FFMIN(s->avctx-> width>>s->chroma_h_shift,
             s->avctx->height>>s->chroma_v_shift) >> (s->spatial_decomposition_count-1) <= 1){
        av_log(s->avctx, AV_LOG_ERROR, "spatial_decomposition_count %d too large for size\n", s->spatial_decomposition_count);
        return AVERROR_INVALIDDATA;
    }
    if (s->avctx->width > 65536-4) {
        av_log(s->avctx, AV_LOG_ERROR, "Width %d is too large\n", s->avctx->width);
        return AVERROR_INVALIDDATA;
    }


    s->qlog           += (unsigned)get_symbol(&s->c, s->header_state, 1);
    s->mv_scale       += (unsigned)get_symbol(&s->c, s->header_state, 1);
    s->qbias          += (unsigned)get_symbol(&s->c, s->header_state, 1);
    s->block_max_depth+= (unsigned)get_symbol(&s->c, s->header_state, 1);
    if(s->block_max_depth > 1 || s->block_max_depth < 0 || s->mv_scale > 256U){
        av_log(s->avctx, AV_LOG_ERROR, "block_max_depth= %d is too large\n", s->block_max_depth);
        s->block_max_depth= 0;
        s->mv_scale = 0;
        return AVERROR_INVALIDDATA;
    }
    if (FFABS(s->qbias) > 127) {
        av_log(s->avctx, AV_LOG_ERROR, "qbias %d is too large\n", s->qbias);
        s->qbias = 0;
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    int ret;

    if ((ret = ff_snow_common_init(avctx)) < 0) {
        return ret;
    }

    return 0;
}

static int decode_blocks(SnowContext *s){
    int x, y;
    int w= s->b_width;
    int h= s->b_height;
    int res;

    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            if (s->c.bytestream >= s->c.bytestream_end)
                return AVERROR_INVALIDDATA;
            if ((res = decode_q_branch(s, 0, x, y)) < 0)
                return res;
        }
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    SnowContext *s = avctx->priv_data;
    RangeCoder * const c= &s->c;
    int bytes_read;
    AVFrame *picture = data;
    int level, orientation, plane_index;
    int res;

    ff_init_range_decoder(c, buf, buf_size);
    ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);

    s->current_picture->pict_type= AV_PICTURE_TYPE_I; //FIXME I vs. P
    if ((res = decode_header(s)) < 0)
        return res;
    if ((res=ff_snow_common_init_after_header(avctx)) < 0)
        return res;

    // realloc slice buffer for the case that spatial_decomposition_count changed
    ff_slice_buffer_destroy(&s->sb);
    if ((res = ff_slice_buffer_init(&s->sb, s->plane[0].height,
                                    (MB_SIZE >> s->block_max_depth) +
                                    s->spatial_decomposition_count * 11 + 1,
                                    s->plane[0].width,
                                    s->spatial_idwt_buffer)) < 0)
        return res;

    for(plane_index=0; plane_index < s->nb_planes; plane_index++){
        Plane *p= &s->plane[plane_index];
        p->fast_mc= p->diag_mc && p->htaps==6 && p->hcoeff[0]==40
                                              && p->hcoeff[1]==-10
                                              && p->hcoeff[2]==2;
    }

    ff_snow_alloc_blocks(s);

    if((res = ff_snow_frame_start(s)) < 0)
        return res;

    s->current_picture->pict_type = s->keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    //keyframe flag duplication mess FIXME
    if(avctx->debug&FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_ERROR,
               "keyframe:%d qlog:%d qbias: %d mvscale: %d "
               "decomposition_type:%d decomposition_count:%d\n",
               s->keyframe, s->qlog, s->qbias, s->mv_scale,
               s->spatial_decomposition_type,
               s->spatial_decomposition_count
              );

    av_assert0(!s->avmv);
    if (s->avctx->export_side_data & AV_CODEC_EXPORT_DATA_MVS) {
        s->avmv = av_malloc_array(s->b_width * s->b_height, sizeof(AVMotionVector) << (s->block_max_depth*2));
    }
    s->avmv_index = 0;

    if ((res = decode_blocks(s)) < 0)
        return res;

    for(plane_index=0; plane_index < s->nb_planes; plane_index++){
        Plane *p= &s->plane[plane_index];
        int w= p->width;
        int h= p->height;
        int x, y;
        int decode_state[MAX_DECOMPOSITIONS][4][1]; /* Stored state info for unpack_coeffs. 1 variable per instance. */

        if(s->avctx->debug&2048){
            memset(s->spatial_dwt_buffer, 0, sizeof(DWTELEM)*w*h);
            predict_plane(s, s->spatial_idwt_buffer, plane_index, 1);

            for(y=0; y<h; y++){
                for(x=0; x<w; x++){
                    int v= s->current_picture->data[plane_index][y*s->current_picture->linesize[plane_index] + x];
                    s->mconly_picture->data[plane_index][y*s->mconly_picture->linesize[plane_index] + x]= v;
                }
            }
        }

        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &p->band[level][orientation];
                unpack_coeffs(s, b, b->parent, orientation);
            }
        }

        {
        const int mb_h= s->b_height << s->block_max_depth;
        const int block_size = MB_SIZE >> s->block_max_depth;
        const int block_h    = plane_index ? block_size>>s->chroma_v_shift : block_size;
        int mb_y;
        DWTCompose cs[MAX_DECOMPOSITIONS];
        int yd=0, yq=0;
        int y;
        int end_y;

        ff_spatial_idwt_buffered_init(cs, &s->sb, w, h, 1, s->spatial_decomposition_type, s->spatial_decomposition_count);
        for(mb_y=0; mb_y<=mb_h; mb_y++){

            int slice_starty = block_h*mb_y;
            int slice_h = block_h*(mb_y+1);

            if (!(s->keyframe || s->avctx->debug&512)){
                slice_starty = FFMAX(0, slice_starty - (block_h >> 1));
                slice_h -= (block_h >> 1);
            }

            for(level=0; level<s->spatial_decomposition_count; level++){
                for(orientation=level ? 1 : 0; orientation<4; orientation++){
                    SubBand *b= &p->band[level][orientation];
                    int start_y;
                    int end_y;
                    int our_mb_start = mb_y;
                    int our_mb_end = (mb_y + 1);
                    const int extra= 3;
                    start_y = (mb_y ? ((block_h * our_mb_start) >> (s->spatial_decomposition_count - level)) + s->spatial_decomposition_count - level + extra: 0);
                    end_y = (((block_h * our_mb_end) >> (s->spatial_decomposition_count - level)) + s->spatial_decomposition_count - level + extra);
                    if (!(s->keyframe || s->avctx->debug&512)){
                        start_y = FFMAX(0, start_y - (block_h >> (1+s->spatial_decomposition_count - level)));
                        end_y = FFMAX(0, end_y - (block_h >> (1+s->spatial_decomposition_count - level)));
                    }
                    start_y = FFMIN(b->height, start_y);
                    end_y = FFMIN(b->height, end_y);

                    if (start_y != end_y){
                        if (orientation == 0){
                            SubBand * correlate_band = &p->band[0][0];
                            int correlate_end_y = FFMIN(b->height, end_y + 1);
                            int correlate_start_y = FFMIN(b->height, (start_y ? start_y + 1 : 0));
                            decode_subband_slice_buffered(s, correlate_band, &s->sb, correlate_start_y, correlate_end_y, decode_state[0][0]);
                            correlate_slice_buffered(s, &s->sb, correlate_band, correlate_band->ibuf, correlate_band->stride, 1, 0, correlate_start_y, correlate_end_y);
                            dequantize_slice_buffered(s, &s->sb, correlate_band, correlate_band->ibuf, correlate_band->stride, start_y, end_y);
                        }
                        else
                            decode_subband_slice_buffered(s, b, &s->sb, start_y, end_y, decode_state[level][orientation]);
                    }
                }
            }

            for(; yd<slice_h; yd+=4){
                ff_spatial_idwt_buffered_slice(&s->dwt, cs, &s->sb, s->temp_idwt_buffer, w, h, 1, s->spatial_decomposition_type, s->spatial_decomposition_count, yd);
            }

            if(s->qlog == LOSSLESS_QLOG){
                for(; yq<slice_h && yq<h; yq++){
                    IDWTELEM * line = slice_buffer_get_line(&s->sb, yq);
                    for(x=0; x<w; x++){
                        line[x] *= 1<<FRAC_BITS;
                    }
                }
            }

            predict_slice_buffered(s, &s->sb, s->spatial_idwt_buffer, plane_index, 1, mb_y);

            y = FFMIN(p->height, slice_starty);
            end_y = FFMIN(p->height, slice_h);
            while(y < end_y)
                ff_slice_buffer_release(&s->sb, y++);
        }

        ff_slice_buffer_flush(&s->sb);
        }

    }

    emms_c();

    ff_snow_release_buffer(avctx);

    if(!(s->avctx->debug&2048))
        res = av_frame_ref(picture, s->current_picture);
    else
        res = av_frame_ref(picture, s->mconly_picture);
    if (res >= 0 && s->avmv_index) {
        AVFrameSideData *sd;

        sd = av_frame_new_side_data(picture, AV_FRAME_DATA_MOTION_VECTORS, s->avmv_index * sizeof(AVMotionVector));
        if (!sd)
            return AVERROR(ENOMEM);
        memcpy(sd->data, s->avmv, s->avmv_index * sizeof(AVMotionVector));
    }

    av_freep(&s->avmv);

    if (res < 0)
        return res;

    *got_frame = 1;

    bytes_read= c->bytestream - c->bytestream_start;
    if(bytes_read ==0) av_log(s->avctx, AV_LOG_ERROR, "error at end of frame\n"); //FIXME

    return bytes_read;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    SnowContext *s = avctx->priv_data;

    ff_slice_buffer_destroy(&s->sb);

    ff_snow_common_end(s);

    return 0;
}

AVCodec ff_snow_decoder = {
    .name           = "snow",
    .long_name      = NULL_IF_CONFIG_SMALL("Snow"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SNOW,
    .priv_data_size = sizeof(SnowContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 /*| AV_CODEC_CAP_DRAW_HORIZ_BAND*/,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};
