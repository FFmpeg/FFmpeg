/*
 * Raw Video Decoder
 * Copyright (c) 2001 Fabrice Bellard
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
 * @file libavcodec/rawdec.c
 * Raw Video Decoder
 */

#include "avcodec.h"
#include "raw.h"
#include "libavutil/intreadwrite.h"

typedef struct RawVideoContext {
    unsigned char * buffer;  /* block of memory for holding one frame */
    int             length;  /* number of bytes in buffer */
    int flip;
    AVFrame pic;             ///< AVCodecContext.coded_frame
} RawVideoContext;

static const PixelFormatTag pixelFormatBpsAVI[] = {
    { PIX_FMT_PAL8,    4 },
    { PIX_FMT_PAL8,    8 },
    { PIX_FMT_RGB555, 15 },
    { PIX_FMT_RGB555, 16 },
    { PIX_FMT_BGR24,  24 },
    { PIX_FMT_RGB32,  32 },
    { PIX_FMT_NONE, 0 },
};

static const PixelFormatTag pixelFormatBpsMOV[] = {
    /* FIXME fix swscaler to support those */
    /* http://developer.apple.com/documentation/QuickTime/QTFF/QTFFChap3/chapter_4_section_2.html */
    { PIX_FMT_PAL8,      4 },
    { PIX_FMT_PAL8,      8 },
    { PIX_FMT_BGR555,   16 },
    { PIX_FMT_RGB24,    24 },
    { PIX_FMT_BGR32_1,  32 },
    { PIX_FMT_NONE, 0 },
};

static enum PixelFormat findPixelFormat(const PixelFormatTag *tags, unsigned int fourcc)
{
    while (tags->pix_fmt >= 0) {
        if (tags->fourcc == fourcc)
            return tags->pix_fmt;
        tags++;
    }
    return PIX_FMT_YUV420P;
}

static av_cold int raw_init_decoder(AVCodecContext *avctx)
{
    RawVideoContext *context = avctx->priv_data;

    if (avctx->codec_tag == MKTAG('r','a','w',' '))
        avctx->pix_fmt = findPixelFormat(pixelFormatBpsMOV, avctx->bits_per_coded_sample);
    else if (avctx->codec_tag)
        avctx->pix_fmt = findPixelFormat(ff_raw_pixelFormatTags, avctx->codec_tag);
    else if (avctx->bits_per_coded_sample)
        avctx->pix_fmt = findPixelFormat(pixelFormatBpsAVI, avctx->bits_per_coded_sample);

    context->length = avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height);
    context->buffer = av_malloc(context->length);
    context->pic.pict_type = FF_I_TYPE;
    context->pic.key_frame = 1;

    avctx->coded_frame= &context->pic;

    if (!context->buffer)
        return -1;

    if(avctx->extradata_size >= 9 && !memcmp(avctx->extradata + avctx->extradata_size - 9, "BottomUp", 9))
        context->flip=1;

    return 0;
}

static void flip(AVCodecContext *avctx, AVPicture * picture){
    picture->data[0] += picture->linesize[0] * (avctx->height-1);
    picture->linesize[0] *= -1;
}

static int raw_decode(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    RawVideoContext *context = avctx->priv_data;

    AVFrame * frame = (AVFrame *) data;
    AVPicture * picture = (AVPicture *) data;

    frame->interlaced_frame = avctx->coded_frame->interlaced_frame;
    frame->top_field_first = avctx->coded_frame->top_field_first;

    //4bpp raw in avi and mov (yes this is ugly ...)
    if(avctx->bits_per_coded_sample == 4 && avctx->pix_fmt==PIX_FMT_PAL8 &&
       (!avctx->codec_tag || avctx->codec_tag == MKTAG('r','a','w',' '))){
        int i;
        for(i=256*2; i+1 < context->length>>1; i++){
            context->buffer[2*i+0]= buf[i-256*2]>>4;
            context->buffer[2*i+1]= buf[i-256*2]&15;
        }
        buf= context->buffer + 256*4;
        buf_size= context->length - 256*4;
    }

    if(buf_size < context->length - (avctx->pix_fmt==PIX_FMT_PAL8 ? 256*4 : 0))
        return -1;

    avpicture_fill(picture, buf, avctx->pix_fmt, avctx->width, avctx->height);
    if(avctx->pix_fmt==PIX_FMT_PAL8 && buf_size < context->length){
        frame->data[1]= context->buffer;
    }
    if (avctx->palctrl && avctx->palctrl->palette_changed) {
        memcpy(frame->data[1], avctx->palctrl->palette, AVPALETTE_SIZE);
        avctx->palctrl->palette_changed = 0;
    }

    if(context->flip)
        flip(avctx, picture);

    if (avctx->codec_tag == MKTAG('Y', 'V', '1', '2'))
    {
        // swap fields
        unsigned char *tmp = picture->data[1];
        picture->data[1] = picture->data[2];
        picture->data[2] = tmp;
    }

    if(avctx->codec_tag == AV_RL32("yuv2") &&
       avctx->pix_fmt   == PIX_FMT_YUYV422) {
        int x, y;
        uint8_t *line = picture->data[0];
        for(y = 0; y < avctx->height; y++) {
            for(x = 0; x < avctx->width; x++)
                line[2*x + 1] ^= 0x80;
            line += picture->linesize[0];
        }
    }

    *data_size = sizeof(AVPicture);
    return buf_size;
}

static av_cold int raw_close_decoder(AVCodecContext *avctx)
{
    RawVideoContext *context = avctx->priv_data;

    av_freep(&context->buffer);
    return 0;
}

AVCodec rawvideo_decoder = {
    "rawvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_RAWVIDEO,
    sizeof(RawVideoContext),
    raw_init_decoder,
    NULL,
    raw_close_decoder,
    raw_decode,
    .long_name = NULL_IF_CONFIG_SMALL("raw video"),
};
