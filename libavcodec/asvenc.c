/*
 * Copyright (c) 2003 Michael Niedermayer
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
 * ASUS V1/V2 encoder.
 */

#include "libavutil/attributes.h"
#include "libavutil/mem.h"

#include "asv.h"
#include "avcodec.h"
#include "internal.h"
#include "mathops.h"
#include "mpeg12data.h"

static inline void asv2_put_bits(PutBitContext *pb, int n, int v){
    put_bits(pb, n, ff_reverse[ v << (8-n) ]);
}

static inline void asv1_put_level(PutBitContext *pb, int level){
    unsigned int index= level + 3;

    if(index <= 6) put_bits(pb, ff_asv_level_tab[index][1], ff_asv_level_tab[index][0]);
    else{
        put_bits(pb, ff_asv_level_tab[3][1], ff_asv_level_tab[3][0]);
        put_sbits(pb, 8, level);
    }
}

static inline void asv2_put_level(PutBitContext *pb, int level){
    unsigned int index= level + 31;

    if(index <= 62) put_bits(pb, ff_asv2_level_tab[index][1], ff_asv2_level_tab[index][0]);
    else{
        put_bits(pb, ff_asv2_level_tab[31][1], ff_asv2_level_tab[31][0]);
        asv2_put_bits(pb, 8, level&0xFF);
    }
}

static inline void asv1_encode_block(ASV1Context *a, int16_t block[64]){
    int i;
    int nc_count=0;

    put_bits(&a->pb, 8, (block[0] + 32)>>6);
    block[0]= 0;

    for(i=0; i<10; i++){
        const int index = ff_asv_scantab[4*i];
        int ccp=0;

        if( (block[index + 0] = (block[index + 0]*a->q_intra_matrix[index + 0] + (1<<15))>>16) ) ccp |= 8;
        if( (block[index + 8] = (block[index + 8]*a->q_intra_matrix[index + 8] + (1<<15))>>16) ) ccp |= 4;
        if( (block[index + 1] = (block[index + 1]*a->q_intra_matrix[index + 1] + (1<<15))>>16) ) ccp |= 2;
        if( (block[index + 9] = (block[index + 9]*a->q_intra_matrix[index + 9] + (1<<15))>>16) ) ccp |= 1;

        if(ccp){
            for(;nc_count; nc_count--)
                put_bits(&a->pb, ff_asv_ccp_tab[0][1], ff_asv_ccp_tab[0][0]);

            put_bits(&a->pb, ff_asv_ccp_tab[ccp][1], ff_asv_ccp_tab[ccp][0]);

            if(ccp&8) asv1_put_level(&a->pb, block[index + 0]);
            if(ccp&4) asv1_put_level(&a->pb, block[index + 8]);
            if(ccp&2) asv1_put_level(&a->pb, block[index + 1]);
            if(ccp&1) asv1_put_level(&a->pb, block[index + 9]);
        }else{
            nc_count++;
        }
    }
    put_bits(&a->pb, ff_asv_ccp_tab[16][1], ff_asv_ccp_tab[16][0]);
}

static inline void asv2_encode_block(ASV1Context *a, int16_t block[64]){
    int i;
    int count=0;

    for(count=63; count>3; count--){
        const int index = ff_asv_scantab[count];

        if( (block[index]*a->q_intra_matrix[index] + (1<<15))>>16 )
            break;
    }

    count >>= 2;

    asv2_put_bits(&a->pb, 4, count);
    asv2_put_bits(&a->pb, 8, (block[0] + 32)>>6);
    block[0]= 0;

    for(i=0; i<=count; i++){
        const int index = ff_asv_scantab[4*i];
        int ccp=0;

        if( (block[index + 0] = (block[index + 0]*a->q_intra_matrix[index + 0] + (1<<15))>>16) ) ccp |= 8;
        if( (block[index + 8] = (block[index + 8]*a->q_intra_matrix[index + 8] + (1<<15))>>16) ) ccp |= 4;
        if( (block[index + 1] = (block[index + 1]*a->q_intra_matrix[index + 1] + (1<<15))>>16) ) ccp |= 2;
        if( (block[index + 9] = (block[index + 9]*a->q_intra_matrix[index + 9] + (1<<15))>>16) ) ccp |= 1;

        av_assert2(i || ccp<8);
        if(i) put_bits(&a->pb, ff_asv_ac_ccp_tab[ccp][1], ff_asv_ac_ccp_tab[ccp][0]);
        else  put_bits(&a->pb, ff_asv_dc_ccp_tab[ccp][1], ff_asv_dc_ccp_tab[ccp][0]);

        if(ccp){
            if(ccp&8) asv2_put_level(&a->pb, block[index + 0]);
            if(ccp&4) asv2_put_level(&a->pb, block[index + 8]);
            if(ccp&2) asv2_put_level(&a->pb, block[index + 1]);
            if(ccp&1) asv2_put_level(&a->pb, block[index + 9]);
        }
    }
}

#define MAX_MB_SIZE (30*16*16*3/2/8)

static inline int encode_mb(ASV1Context *a, int16_t block[6][64]){
    int i;

    if (a->pb.buf_end - a->pb.buf - (put_bits_count(&a->pb)>>3) < MAX_MB_SIZE) {
        av_log(a->avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }

    if(a->avctx->codec_id == AV_CODEC_ID_ASV1){
        for(i=0; i<6; i++)
            asv1_encode_block(a, block[i]);
    }else{
        for(i=0; i<6; i++)
            asv2_encode_block(a, block[i]);
    }
    return 0;
}

static inline void dct_get(ASV1Context *a, int mb_x, int mb_y){
    int16_t (*block)[64]= a->block;
    int linesize= a->picture.linesize[0];
    int i;

    uint8_t *ptr_y  = a->picture.data[0] + (mb_y * 16* linesize              ) + mb_x * 16;
    uint8_t *ptr_cb = a->picture.data[1] + (mb_y * 8 * a->picture.linesize[1]) + mb_x * 8;
    uint8_t *ptr_cr = a->picture.data[2] + (mb_y * 8 * a->picture.linesize[2]) + mb_x * 8;

    a->dsp.get_pixels(block[0], ptr_y                 , linesize);
    a->dsp.get_pixels(block[1], ptr_y              + 8, linesize);
    a->dsp.get_pixels(block[2], ptr_y + 8*linesize    , linesize);
    a->dsp.get_pixels(block[3], ptr_y + 8*linesize + 8, linesize);
    for(i=0; i<4; i++)
        a->dsp.fdct(block[i]);

    if(!(a->avctx->flags&CODEC_FLAG_GRAY)){
        a->dsp.get_pixels(block[4], ptr_cb, a->picture.linesize[1]);
        a->dsp.get_pixels(block[5], ptr_cr, a->picture.linesize[2]);
        for(i=4; i<6; i++)
            a->dsp.fdct(block[i]);
    }
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pict, int *got_packet)
{
    ASV1Context * const a = avctx->priv_data;
    AVFrame * const p= &a->picture;
    int size, ret;
    int mb_x, mb_y;

    if ((ret = ff_alloc_packet2(avctx, pkt, a->mb_height*a->mb_width*MAX_MB_SIZE +
                                  FF_MIN_BUFFER_SIZE)) < 0)
        return ret;

    init_put_bits(&a->pb, pkt->data, pkt->size);

    *p = *pict;
    p->pict_type= AV_PICTURE_TYPE_I;
    p->key_frame= 1;

    for(mb_y=0; mb_y<a->mb_height2; mb_y++){
        for(mb_x=0; mb_x<a->mb_width2; mb_x++){
            dct_get(a, mb_x, mb_y);
            encode_mb(a, a->block);
        }
    }

    if(a->mb_width2 != a->mb_width){
        mb_x= a->mb_width2;
        for(mb_y=0; mb_y<a->mb_height2; mb_y++){
            dct_get(a, mb_x, mb_y);
            encode_mb(a, a->block);
        }
    }

    if(a->mb_height2 != a->mb_height){
        mb_y= a->mb_height2;
        for(mb_x=0; mb_x<a->mb_width; mb_x++){
            dct_get(a, mb_x, mb_y);
            encode_mb(a, a->block);
        }
    }
    emms_c();

    avpriv_align_put_bits(&a->pb);
    while(put_bits_count(&a->pb)&31)
        put_bits(&a->pb, 8, 0);

    size= put_bits_count(&a->pb)/32;

    if(avctx->codec_id == AV_CODEC_ID_ASV1)
        a->dsp.bswap_buf((uint32_t*)pkt->data, (uint32_t*)pkt->data, size);
    else{
        int i;
        for(i=0; i<4*size; i++)
            pkt->data[i] = ff_reverse[pkt->data[i]];
    }

    pkt->size   = size*4;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static av_cold int encode_init(AVCodecContext *avctx){
    ASV1Context * const a = avctx->priv_data;
    int i;
    const int scale= avctx->codec_id == AV_CODEC_ID_ASV1 ? 1 : 2;

    ff_asv_common_init(avctx);

    if(avctx->global_quality == 0) avctx->global_quality= 4*FF_QUALITY_SCALE;

    a->inv_qscale= (32*scale*FF_QUALITY_SCALE +  avctx->global_quality/2) / avctx->global_quality;

    avctx->extradata= av_mallocz(8);
    avctx->extradata_size=8;
    ((uint32_t*)avctx->extradata)[0]= av_le2ne32(a->inv_qscale);
    ((uint32_t*)avctx->extradata)[1]= av_le2ne32(AV_RL32("ASUS"));

    for(i=0; i<64; i++){
        int q= 32*scale*ff_mpeg1_default_intra_matrix[i];
        a->q_intra_matrix[i]= ((a->inv_qscale<<16) + q/2) / q;
    }

    return 0;
}

#if CONFIG_ASV1_ENCODER
AVCodec ff_asv1_encoder = {
    .name           = "asv1",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ASV1,
    .priv_data_size = sizeof(ASV1Context),
    .init           = encode_init,
    .encode2        = encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P,
                                                    AV_PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("ASUS V1"),
};
#endif

#if CONFIG_ASV2_ENCODER
AVCodec ff_asv2_encoder = {
    .name           = "asv2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ASV2,
    .priv_data_size = sizeof(ASV1Context),
    .init           = encode_init,
    .encode2        = encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P,
                                                    AV_PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("ASUS V2"),
};
#endif
