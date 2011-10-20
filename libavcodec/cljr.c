/*
 * Cirrus Logic AccuPak (CLJR) codec
 * Copyright (c) 2003 Alex Beregszaszi
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
 * Cirrus Logic AccuPak codec.
 */

#include "avcodec.h"
#include "dsputil.h"
#include "get_bits.h"

/* Disable the encoder. */
#undef CONFIG_CLJR_ENCODER
#define CONFIG_CLJR_ENCODER 0

typedef struct CLJRContext{
    AVCodecContext *avctx;
    AVFrame picture;
    int delta[16];
    int offset[4];
    GetBitContext gb;
} CLJRContext;

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    CLJRContext * const a = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&a->picture;
    int x, y;

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    if(buf_size/avctx->height < avctx->width) {
        av_log(avctx, AV_LOG_ERROR, "Resolution larger than buffer size. Invalid header?\n");
        return -1;
    }

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type= AV_PICTURE_TYPE_I;
    p->key_frame= 1;

    init_get_bits(&a->gb, buf, buf_size * 8);

    for(y=0; y<avctx->height; y++){
        uint8_t *luma= &a->picture.data[0][ y*a->picture.linesize[0] ];
        uint8_t *cb= &a->picture.data[1][ y*a->picture.linesize[1] ];
        uint8_t *cr= &a->picture.data[2][ y*a->picture.linesize[2] ];
        for(x=0; x<avctx->width; x+=4){
                luma[3] = get_bits(&a->gb, 5) << 3;
            luma[2] = get_bits(&a->gb, 5) << 3;
            luma[1] = get_bits(&a->gb, 5) << 3;
            luma[0] = get_bits(&a->gb, 5) << 3;
            luma+= 4;
            *(cb++) = get_bits(&a->gb, 6) << 2;
            *(cr++) = get_bits(&a->gb, 6) << 2;
        }
    }

    *picture= *(AVFrame*)&a->picture;
    *data_size = sizeof(AVPicture);

    emms_c();

    return buf_size;
}

#if CONFIG_CLJR_ENCODER
static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    CLJRContext * const a = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p= (AVFrame*)&a->picture;
    int size;

    *p = *pict;
    p->pict_type= AV_PICTURE_TYPE_I;
    p->key_frame= 1;

    emms_c();

    avpriv_align_put_bits(&a->pb);
    while(get_bit_count(&a->pb)&31)
        put_bits(&a->pb, 8, 0);

    size= get_bit_count(&a->pb)/32;

    return size*4;
}
#endif

static av_cold void common_init(AVCodecContext *avctx){
    CLJRContext * const a = avctx->priv_data;

    avcodec_get_frame_defaults(&a->picture);
    avctx->coded_frame= (AVFrame*)&a->picture;
    a->avctx= avctx;
}

static av_cold int decode_init(AVCodecContext *avctx){

    common_init(avctx);

    avctx->pix_fmt= PIX_FMT_YUV411P;

    return 0;
}

#if CONFIG_CLJR_ENCODER
static av_cold int encode_init(AVCodecContext *avctx){

    common_init(avctx);

    return 0;
}
#endif

AVCodec ff_cljr_decoder = {
    .name           = "cljr",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_CLJR,
    .priv_data_size = sizeof(CLJRContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Cirrus Logic AccuPak"),
};

#if CONFIG_CLJR_ENCODER
AVCodec ff_cljr_encoder = {
    .name           = "cljr",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_CLJR,
    .priv_data_size = sizeof(CLJRContext),
    .init           = encode_init,
    .encode         = encode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("Cirrus Logic AccuPak"),
};
#endif
