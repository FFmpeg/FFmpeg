/*
 * SVQ1 Encoder
 * Copyright (C) 2004 Mike Melanson <melanson@pcisys.net>
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
 * Sorenson Vector Quantizer #1 (SVQ1) video codec.
 * For more information of the SVQ1 algorithm, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */


#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "h263.h"
#include "internal.h"
#include "libavutil/avassert.h"

#include "svq1.h"
#include "svq1enc_cb.h"



typedef struct SVQ1Context {
    MpegEncContext m; // needed for motion estimation, should not be used for anything else, the idea is to make the motion estimation eventually independent of MpegEncContext, so this will be removed then (FIXME/XXX)
    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame picture;
    AVFrame current_picture;
    AVFrame last_picture;
    PutBitContext pb;
    GetBitContext gb;

    PutBitContext reorder_pb[6]; //why ooh why this sick breadth first order, everything is slower and more complex

    int frame_width;
    int frame_height;

    /* Y plane block dimensions */
    int y_block_width;
    int y_block_height;

    /* U & V plane (C planes) block dimensions */
    int c_block_width;
    int c_block_height;

    uint16_t *mb_type;
    uint32_t *dummy;
    int16_t (*motion_val8[3])[2];
    int16_t (*motion_val16[3])[2];

    int64_t rd_total;

    uint8_t *scratchbuf;
} SVQ1Context;

static void svq1_write_header(SVQ1Context *s, int frame_type)
{
    int i;

    /* frame code */
    put_bits(&s->pb, 22, 0x20);

    /* temporal reference (sure hope this is a "don't care") */
    put_bits(&s->pb, 8, 0x00);

    /* frame type */
    put_bits(&s->pb, 2, frame_type - 1);

    if (frame_type == AV_PICTURE_TYPE_I) {

        /* no checksum since frame code is 0x20 */

        /* no embedded string either */

        /* output 5 unknown bits (2 + 2 + 1) */
        put_bits(&s->pb, 5, 2); /* 2 needed by quicktime decoder */

        i= ff_match_2uint16((void*)ff_svq1_frame_size_table, FF_ARRAY_ELEMS(ff_svq1_frame_size_table), s->frame_width, s->frame_height);
        put_bits(&s->pb, 3, i);

        if (i == 7)
        {
                put_bits(&s->pb, 12, s->frame_width);
                put_bits(&s->pb, 12, s->frame_height);
        }
    }

    /* no checksum or extra data (next 2 bits get 0) */
    put_bits(&s->pb, 2, 0);
}


#define QUALITY_THRESHOLD 100
#define THRESHOLD_MULTIPLIER 0.6

static int encode_block(SVQ1Context *s, uint8_t *src, uint8_t *ref, uint8_t *decoded, int stride, int level, int threshold, int lambda, int intra){
    int count, y, x, i, j, split, best_mean, best_score, best_count;
    int best_vector[6];
    int block_sum[7]= {0, 0, 0, 0, 0, 0};
    int w= 2<<((level+2)>>1);
    int h= 2<<((level+1)>>1);
    int size=w*h;
    int16_t block[7][256];
    const int8_t *codebook_sum, *codebook;
    const uint16_t (*mean_vlc)[2];
    const uint8_t (*multistage_vlc)[2];

    best_score=0;
    //FIXME optimize, this doenst need to be done multiple times
    if(intra){
        codebook_sum= svq1_intra_codebook_sum[level];
        codebook= ff_svq1_intra_codebooks[level];
        mean_vlc= ff_svq1_intra_mean_vlc;
        multistage_vlc= ff_svq1_intra_multistage_vlc[level];
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v= src[x + y*stride];
                block[0][x + w*y]= v;
                best_score += v*v;
                block_sum[0] += v;
            }
        }
    }else{
        codebook_sum= svq1_inter_codebook_sum[level];
        codebook= ff_svq1_inter_codebooks[level];
        mean_vlc= ff_svq1_inter_mean_vlc + 256;
        multistage_vlc= ff_svq1_inter_multistage_vlc[level];
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v= src[x + y*stride] - ref[x + y*stride];
                block[0][x + w*y]= v;
                best_score += v*v;
                block_sum[0] += v;
            }
        }
    }

    best_count=0;
    best_score -= (int)(((unsigned)block_sum[0]*block_sum[0])>>(level+3));
    best_mean= (block_sum[0] + (size>>1)) >> (level+3);

    if(level<4){
        for(count=1; count<7; count++){
            int best_vector_score= INT_MAX;
            int best_vector_sum=-999, best_vector_mean=-999;
            const int stage= count-1;
            const int8_t *vector;

            for(i=0; i<16; i++){
                int sum= codebook_sum[stage*16 + i];
                int sqr, diff, score;

                vector = codebook + stage*size*16 + i*size;
                sqr = s->dsp.ssd_int8_vs_int16(vector, block[stage], size);
                diff= block_sum[stage] - sum;
                score= sqr - ((diff*(int64_t)diff)>>(level+3)); //FIXME 64bit slooow
                if(score < best_vector_score){
                    int mean= (diff + (size>>1)) >> (level+3);
                    av_assert2(mean >-300 && mean<300);
                    mean= av_clip(mean, intra?0:-256, 255);
                    best_vector_score= score;
                    best_vector[stage]= i;
                    best_vector_sum= sum;
                    best_vector_mean= mean;
                }
            }
            av_assert0(best_vector_mean != -999);
            vector= codebook + stage*size*16 + best_vector[stage]*size;
            for(j=0; j<size; j++){
                block[stage+1][j] = block[stage][j] - vector[j];
            }
            block_sum[stage+1]= block_sum[stage] - best_vector_sum;
            best_vector_score +=
                lambda*(+ 1 + 4*count
                        + multistage_vlc[1+count][1]
                        + mean_vlc[best_vector_mean][1]);

            if(best_vector_score < best_score){
                best_score= best_vector_score;
                best_count= count;
                best_mean= best_vector_mean;
            }
        }
    }

    split=0;
    if(best_score > threshold && level){
        int score=0;
        int offset= (level&1) ? stride*h/2 : w/2;
        PutBitContext backup[6];

        for(i=level-1; i>=0; i--){
            backup[i]= s->reorder_pb[i];
        }
        score += encode_block(s, src         , ref         , decoded         , stride, level-1, threshold>>1, lambda, intra);
        score += encode_block(s, src + offset, ref + offset, decoded + offset, stride, level-1, threshold>>1, lambda, intra);
        score += lambda;

        if(score < best_score){
            best_score= score;
            split=1;
        }else{
            for(i=level-1; i>=0; i--){
                s->reorder_pb[i]= backup[i];
            }
        }
    }
    if (level > 0)
        put_bits(&s->reorder_pb[level], 1, split);

    if(!split){
        av_assert1((best_mean >= 0 && best_mean<256) || !intra);
        av_assert1(best_mean >= -256 && best_mean<256);
        av_assert1(best_count >=0 && best_count<7);
        av_assert1(level<4 || best_count==0);

        /* output the encoding */
        put_bits(&s->reorder_pb[level],
            multistage_vlc[1 + best_count][1],
            multistage_vlc[1 + best_count][0]);
        put_bits(&s->reorder_pb[level], mean_vlc[best_mean][1],
            mean_vlc[best_mean][0]);

        for (i = 0; i < best_count; i++){
            av_assert2(best_vector[i]>=0 && best_vector[i]<16);
            put_bits(&s->reorder_pb[level], 4, best_vector[i]);
        }

        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                decoded[x + y*stride]= src[x + y*stride] - block[best_count][x + w*y] + best_mean;
            }
        }
    }

    return best_score;
}


static int svq1_encode_plane(SVQ1Context *s, int plane, unsigned char *src_plane, unsigned char *ref_plane, unsigned char *decoded_plane,
    int width, int height, int src_stride, int stride)
{
    int x, y;
    int i;
    int block_width, block_height;
    int level;
    int threshold[6];
    uint8_t *src = s->scratchbuf + stride * 16;
    const int lambda= (s->picture.quality*s->picture.quality) >> (2*FF_LAMBDA_SHIFT);

    /* figure out the acceptable level thresholds in advance */
    threshold[5] = QUALITY_THRESHOLD;
    for (level = 4; level >= 0; level--)
        threshold[level] = threshold[level + 1] * THRESHOLD_MULTIPLIER;

    block_width = (width + 15) / 16;
    block_height = (height + 15) / 16;

    if(s->picture.pict_type == AV_PICTURE_TYPE_P){
        s->m.avctx= s->avctx;
        s->m.current_picture_ptr= &s->m.current_picture;
        s->m.last_picture_ptr   = &s->m.last_picture;
        s->m.last_picture.f.data[0] = ref_plane;
        s->m.linesize=
        s->m.last_picture.f.linesize[0] =
        s->m.new_picture.f.linesize[0] =
        s->m.current_picture.f.linesize[0] = stride;
        s->m.width= width;
        s->m.height= height;
        s->m.mb_width= block_width;
        s->m.mb_height= block_height;
        s->m.mb_stride= s->m.mb_width+1;
        s->m.b8_stride= 2*s->m.mb_width+1;
        s->m.f_code=1;
        s->m.pict_type= s->picture.pict_type;
        s->m.me_method= s->avctx->me_method;
        s->m.me.scene_change_score=0;
        s->m.flags= s->avctx->flags;
//        s->m.out_format = FMT_H263;
//        s->m.unrestricted_mv= 1;

        s->m.lambda= s->picture.quality;
        s->m.qscale= (s->m.lambda*139 + FF_LAMBDA_SCALE*64) >> (FF_LAMBDA_SHIFT + 7);
        s->m.lambda2= (s->m.lambda*s->m.lambda + FF_LAMBDA_SCALE/2) >> FF_LAMBDA_SHIFT;

        if(!s->motion_val8[plane]){
            s->motion_val8 [plane]= av_mallocz((s->m.b8_stride*block_height*2 + 2)*2*sizeof(int16_t));
            s->motion_val16[plane]= av_mallocz((s->m.mb_stride*(block_height + 2) + 1)*2*sizeof(int16_t));
        }

        s->m.mb_type= s->mb_type;

        //dummies, to avoid segfaults
        s->m.current_picture.mb_mean=   (uint8_t *)s->dummy;
        s->m.current_picture.mb_var=    (uint16_t*)s->dummy;
        s->m.current_picture.mc_mb_var= (uint16_t*)s->dummy;
        s->m.current_picture.f.mb_type = s->dummy;

        s->m.current_picture.f.motion_val[0] = s->motion_val8[plane] + 2;
        s->m.p_mv_table= s->motion_val16[plane] + s->m.mb_stride + 1;
        s->m.dsp= s->dsp; //move
        ff_init_me(&s->m);

        s->m.me.dia_size= s->avctx->dia_size;
        s->m.first_slice_line=1;
        for (y = 0; y < block_height; y++) {
            s->m.new_picture.f.data[0] = src - y*16*stride; //ugly
            s->m.mb_y= y;

            for(i=0; i<16 && i + 16*y<height; i++){
                memcpy(&src[i*stride], &src_plane[(i+16*y)*src_stride], width);
                for(x=width; x<16*block_width; x++)
                    src[i*stride+x]= src[i*stride+x-1];
            }
            for(; i<16 && i + 16*y<16*block_height; i++)
                memcpy(&src[i*stride], &src[(i-1)*stride], 16*block_width);

            for (x = 0; x < block_width; x++) {
                s->m.mb_x= x;
                ff_init_block_index(&s->m);
                ff_update_block_index(&s->m);

                ff_estimate_p_frame_motion(&s->m, x, y);
            }
            s->m.first_slice_line=0;
        }

        ff_fix_long_p_mvs(&s->m);
        ff_fix_long_mvs(&s->m, NULL, 0, s->m.p_mv_table, s->m.f_code, CANDIDATE_MB_TYPE_INTER, 0);
    }

    s->m.first_slice_line=1;
    for (y = 0; y < block_height; y++) {
        for(i=0; i<16 && i + 16*y<height; i++){
            memcpy(&src[i*stride], &src_plane[(i+16*y)*src_stride], width);
            for(x=width; x<16*block_width; x++)
                src[i*stride+x]= src[i*stride+x-1];
        }
        for(; i<16 && i + 16*y<16*block_height; i++)
            memcpy(&src[i*stride], &src[(i-1)*stride], 16*block_width);

        s->m.mb_y= y;
        for (x = 0; x < block_width; x++) {
            uint8_t reorder_buffer[3][6][7*32];
            int count[3][6];
            int offset = y * 16 * stride + x * 16;
            uint8_t *decoded= decoded_plane + offset;
            uint8_t *ref= ref_plane + offset;
            int score[4]={0,0,0,0}, best;
            uint8_t *temp = s->scratchbuf;

            if(s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < 3000){ //FIXME check size
                av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
                return -1;
            }

            s->m.mb_x= x;
            ff_init_block_index(&s->m);
            ff_update_block_index(&s->m);

            if(s->picture.pict_type == AV_PICTURE_TYPE_I || (s->m.mb_type[x + y*s->m.mb_stride]&CANDIDATE_MB_TYPE_INTRA)){
                for(i=0; i<6; i++){
                    init_put_bits(&s->reorder_pb[i], reorder_buffer[0][i], 7*32);
                }
                if(s->picture.pict_type == AV_PICTURE_TYPE_P){
                    const uint8_t *vlc= ff_svq1_block_type_vlc[SVQ1_BLOCK_INTRA];
                    put_bits(&s->reorder_pb[5], vlc[1], vlc[0]);
                    score[0]= vlc[1]*lambda;
                }
                score[0]+= encode_block(s, src+16*x, NULL, temp, stride, 5, 64, lambda, 1);
                for(i=0; i<6; i++){
                    count[0][i]= put_bits_count(&s->reorder_pb[i]);
                    flush_put_bits(&s->reorder_pb[i]);
                }
            }else
                score[0]= INT_MAX;

            best=0;

            if(s->picture.pict_type == AV_PICTURE_TYPE_P){
                const uint8_t *vlc= ff_svq1_block_type_vlc[SVQ1_BLOCK_INTER];
                int mx, my, pred_x, pred_y, dxy;
                int16_t *motion_ptr;

                motion_ptr= ff_h263_pred_motion(&s->m, 0, 0, &pred_x, &pred_y);
                if(s->m.mb_type[x + y*s->m.mb_stride]&CANDIDATE_MB_TYPE_INTER){
                    for(i=0; i<6; i++)
                        init_put_bits(&s->reorder_pb[i], reorder_buffer[1][i], 7*32);

                    put_bits(&s->reorder_pb[5], vlc[1], vlc[0]);

                    s->m.pb= s->reorder_pb[5];
                    mx= motion_ptr[0];
                    my= motion_ptr[1];
                    av_assert1(mx>=-32 && mx<=31);
                    av_assert1(my>=-32 && my<=31);
                    av_assert1(pred_x>=-32 && pred_x<=31);
                    av_assert1(pred_y>=-32 && pred_y<=31);
                    ff_h263_encode_motion(&s->m, mx - pred_x, 1);
                    ff_h263_encode_motion(&s->m, my - pred_y, 1);
                    s->reorder_pb[5]= s->m.pb;
                    score[1] += lambda*put_bits_count(&s->reorder_pb[5]);

                    dxy= (mx&1) + 2*(my&1);

                    s->dsp.put_pixels_tab[0][dxy](temp+16, ref + (mx>>1) + stride*(my>>1), stride, 16);

                    score[1]+= encode_block(s, src+16*x, temp+16, decoded, stride, 5, 64, lambda, 0);
                    best= score[1] <= score[0];

                    vlc= ff_svq1_block_type_vlc[SVQ1_BLOCK_SKIP];
                    score[2]= s->dsp.sse[0](NULL, src+16*x, ref, stride, 16);
                    score[2]+= vlc[1]*lambda;
                    if(score[2] < score[best] && mx==0 && my==0){
                        best=2;
                        s->dsp.put_pixels_tab[0][0](decoded, ref, stride, 16);
                        for(i=0; i<6; i++){
                            count[2][i]=0;
                        }
                        put_bits(&s->pb, vlc[1], vlc[0]);
                    }
                }

                if(best==1){
                    for(i=0; i<6; i++){
                        count[1][i]= put_bits_count(&s->reorder_pb[i]);
                        flush_put_bits(&s->reorder_pb[i]);
                    }
                }else{
                    motion_ptr[0                 ] = motion_ptr[1                 ]=
                    motion_ptr[2                 ] = motion_ptr[3                 ]=
                    motion_ptr[0+2*s->m.b8_stride] = motion_ptr[1+2*s->m.b8_stride]=
                    motion_ptr[2+2*s->m.b8_stride] = motion_ptr[3+2*s->m.b8_stride]=0;
                }
            }

            s->rd_total += score[best];

            for(i=5; i>=0; i--){
                avpriv_copy_bits(&s->pb, reorder_buffer[best][i], count[best][i]);
            }
            if(best==0){
                s->dsp.put_pixels_tab[0][0](decoded, temp, stride, 16);
            }
        }
        s->m.first_slice_line=0;
    }
    return 0;
}

static av_cold int svq1_encode_init(AVCodecContext *avctx)
{
    SVQ1Context * const s = avctx->priv_data;

    ff_dsputil_init(&s->dsp, avctx);
    avctx->coded_frame = &s->picture;

    s->frame_width = avctx->width;
    s->frame_height = avctx->height;

    s->y_block_width = (s->frame_width + 15) / 16;
    s->y_block_height = (s->frame_height + 15) / 16;

    s->c_block_width = (s->frame_width / 4 + 15) / 16;
    s->c_block_height = (s->frame_height / 4 + 15) / 16;

    s->avctx= avctx;
    s->m.avctx= avctx;
    s->m.picture_structure = PICT_FRAME;
    s->m.me.temp      =
    s->m.me.scratchpad= av_mallocz((avctx->width+64)*2*16*2*sizeof(uint8_t));
    s->m.me.map       = av_mallocz(ME_MAP_SIZE*sizeof(uint32_t));
    s->m.me.score_map = av_mallocz(ME_MAP_SIZE*sizeof(uint32_t));
    s->mb_type        = av_mallocz((s->y_block_width+1)*s->y_block_height*sizeof(int16_t));
    s->dummy          = av_mallocz((s->y_block_width+1)*s->y_block_height*sizeof(int32_t));
    ff_h263_encode_init(&s->m); //mv_penalty

    return 0;
}

static int svq1_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pict, int *got_packet)
{
    SVQ1Context * const s = avctx->priv_data;
    AVFrame * const p = &s->picture;
    AVFrame temp;
    int i, ret;

    if ((ret = ff_alloc_packet2(avctx, pkt, s->y_block_width*s->y_block_height*MAX_MB_BYTES*3 + FF_MIN_BUFFER_SIZE) < 0))
        return ret;

    if(avctx->pix_fmt != PIX_FMT_YUV410P){
        av_log(avctx, AV_LOG_ERROR, "unsupported pixel format\n");
        return -1;
    }

    if(!s->current_picture.data[0]){
        avctx->get_buffer(avctx, &s->current_picture);
        avctx->get_buffer(avctx, &s->last_picture);
        s->scratchbuf = av_malloc(s->current_picture.linesize[0] * 16 * 2);
    }

    temp= s->current_picture;
    s->current_picture= s->last_picture;
    s->last_picture= temp;

    init_put_bits(&s->pb, pkt->data, pkt->size);

    *p = *pict;
    p->pict_type = avctx->gop_size && avctx->frame_number % avctx->gop_size ? AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_I;
    p->key_frame = p->pict_type == AV_PICTURE_TYPE_I;

    svq1_write_header(s, p->pict_type);
    for(i=0; i<3; i++){
        if(svq1_encode_plane(s, i,
            s->picture.data[i], s->last_picture.data[i], s->current_picture.data[i],
            s->frame_width / (i?4:1), s->frame_height / (i?4:1),
            s->picture.linesize[i], s->current_picture.linesize[i]) < 0)
                return -1;
    }

//    avpriv_align_put_bits(&s->pb);
    while(put_bits_count(&s->pb) & 31)
        put_bits(&s->pb, 1, 0);

    flush_put_bits(&s->pb);

    pkt->size = put_bits_count(&s->pb) / 8;
    if (p->pict_type == AV_PICTURE_TYPE_I)
        pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static av_cold int svq1_encode_end(AVCodecContext *avctx)
{
    SVQ1Context * const s = avctx->priv_data;
    int i;

    av_log(avctx, AV_LOG_DEBUG, "RD: %f\n", s->rd_total/(double)(avctx->width*avctx->height*avctx->frame_number));

    av_freep(&s->m.me.scratchpad);
    av_freep(&s->m.me.map);
    av_freep(&s->m.me.score_map);
    av_freep(&s->mb_type);
    av_freep(&s->dummy);
    av_freep(&s->scratchbuf);

    for(i=0; i<3; i++){
        av_freep(&s->motion_val8[i]);
        av_freep(&s->motion_val16[i]);
    }
    if(s->current_picture.data[0])
        avctx->release_buffer(avctx, &s->current_picture);
    if(s->last_picture.data[0])
        avctx->release_buffer(avctx, &s->last_picture);

    return 0;
}


AVCodec ff_svq1_encoder = {
    .name           = "svq1",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SVQ1,
    .priv_data_size = sizeof(SVQ1Context),
    .init           = svq1_encode_init,
    .encode2        = svq1_encode_frame,
    .close          = svq1_encode_end,
    .pix_fmts       = (const enum PixelFormat[]){ PIX_FMT_YUV410P, PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("Sorenson Vector Quantizer 1 / Sorenson Video 1 / SVQ1"),
};
