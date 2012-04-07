/*
 * Copyright (C) 2009 Michael Niedermayer <michaelni@gmx.at>
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

#include "avcodec.h"
#include "libavutil/bswap.h"

static av_cold int decode_init(AVCodecContext *avctx)
{
    if(avctx->width & 1){
        av_log(avctx, AV_LOG_ERROR, "v210x needs even width\n");
        return -1;
    }
    avctx->pix_fmt = PIX_FMT_YUV422P16;
    avctx->bits_per_raw_sample= 10;

    avctx->coded_frame= avcodec_alloc_frame();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *avpkt)
{
    int y=0;
    int width= avctx->width;
    AVFrame *pic= avctx->coded_frame;
    const uint32_t *src= (const uint32_t *)avpkt->data;
    uint16_t *ydst, *udst, *vdst, *yend;

    if(pic->data[0])
        avctx->release_buffer(avctx, pic);

    if(avpkt->size < avctx->width * avctx->height * 8 / 3){
        av_log(avctx, AV_LOG_ERROR, "Packet too small\n");
        return -1;
    }

    if(avpkt->size > avctx->width * avctx->height * 8 / 3){
        av_log_ask_for_sample(avctx, "Probably padded data\n");
    }

    pic->reference= 0;
    if(avctx->get_buffer(avctx, pic) < 0)
        return -1;

    ydst= (uint16_t *)pic->data[0];
    udst= (uint16_t *)pic->data[1];
    vdst= (uint16_t *)pic->data[2];
    yend= ydst + width;
    pic->pict_type= AV_PICTURE_TYPE_I;
    pic->key_frame= 1;

    for(;;){
        uint32_t v= av_be2ne32(*src++);
        *udst++= (v>>16) & 0xFFC0;
        *ydst++= (v>>6 ) & 0xFFC0;
        *vdst++= (v<<4 ) & 0xFFC0;

        v= av_be2ne32(*src++);
        *ydst++= (v>>16) & 0xFFC0;

        if(ydst >= yend){
            ydst+= pic->linesize[0]/2 - width;
            udst+= pic->linesize[1]/2 - width/2;
            vdst+= pic->linesize[2]/2 - width/2;
            yend= ydst + width;
            if(++y >= avctx->height)
                break;
        }

        *udst++= (v>>6 ) & 0xFFC0;
        *ydst++= (v<<4 ) & 0xFFC0;

        v= av_be2ne32(*src++);
        *vdst++= (v>>16) & 0xFFC0;
        *ydst++= (v>>6 ) & 0xFFC0;

        if(ydst >= yend){
            ydst+= pic->linesize[0]/2 - width;
            udst+= pic->linesize[1]/2 - width/2;
            vdst+= pic->linesize[2]/2 - width/2;
            yend= ydst + width;
            if(++y >= avctx->height)
                break;
        }

        *udst++= (v<<4 ) & 0xFFC0;

        v= av_be2ne32(*src++);
        *ydst++= (v>>16) & 0xFFC0;
        *vdst++= (v>>6 ) & 0xFFC0;
        *ydst++= (v<<4 ) & 0xFFC0;
        if(ydst >= yend){
            ydst+= pic->linesize[0]/2 - width;
            udst+= pic->linesize[1]/2 - width/2;
            vdst+= pic->linesize[2]/2 - width/2;
            yend= ydst + width;
            if(++y >= avctx->height)
                break;
        }
    }

    *data_size=sizeof(AVFrame);
    *(AVFrame*)data= *avctx->coded_frame;

    return avpkt->size;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    AVFrame *pic = avctx->coded_frame;
    if (pic->data[0])
        avctx->release_buffer(avctx, pic);
    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec ff_v210x_decoder = {
    .name           = "v210x",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_V210X,
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
};
