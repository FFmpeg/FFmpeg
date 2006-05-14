/*
 * Raw Video Codec
 * Copyright (c) 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file raw.c
 * Raw Video Codec
 */

#include "avcodec.h"

typedef struct RawVideoContext {
    unsigned char * buffer;  /* block of memory for holding one frame */
    int             length;  /* number of bytes in buffer */
    AVFrame pic;             ///< AVCodecContext.coded_frame
} RawVideoContext;

typedef struct PixelFormatTag {
    int pix_fmt;
    unsigned int fourcc;
} PixelFormatTag;

const PixelFormatTag pixelFormatTags[] = {
    { PIX_FMT_YUV420P, MKTAG('I', '4', '2', '0') }, /* Planar formats */
    { PIX_FMT_YUV420P, MKTAG('I', 'Y', 'U', 'V') },
    { PIX_FMT_YUV410P, MKTAG('Y', 'U', 'V', '9') },
    { PIX_FMT_YUV411P, MKTAG('Y', '4', '1', 'B') },
    { PIX_FMT_YUV422P, MKTAG('Y', '4', '2', 'B') },
    { PIX_FMT_GRAY8,   MKTAG('Y', '8', '0', '0') },
    { PIX_FMT_GRAY8,   MKTAG(' ', ' ', 'Y', '8') },


    { PIX_FMT_YUV422,  MKTAG('Y', 'U', 'Y', '2') }, /* Packed formats */
    { PIX_FMT_YUV422,  MKTAG('Y', '4', '2', '2') },
    { PIX_FMT_UYVY422, MKTAG('U', 'Y', 'V', 'Y') },
    { PIX_FMT_GRAY8,   MKTAG('G', 'R', 'E', 'Y') },

    /* quicktime */
    { PIX_FMT_UYVY422, MKTAG('2', 'v', 'u', 'y') },

    { -1, 0 },
};

static int findPixelFormat(unsigned int fourcc)
{
    const PixelFormatTag * tags = pixelFormatTags;
    while (tags->pix_fmt >= 0) {
        if (tags->fourcc == fourcc)
            return tags->pix_fmt;
        tags++;
    }
    return PIX_FMT_YUV420P;
}

unsigned int avcodec_pix_fmt_to_codec_tag(enum PixelFormat fmt)
{
    const PixelFormatTag * tags = pixelFormatTags;
    while (tags->pix_fmt >= 0) {
        if (tags->pix_fmt == fmt)
            return tags->fourcc;
        tags++;
    }
    return 0;
}

/* RAW Decoder Implementation */

static int raw_init_decoder(AVCodecContext *avctx)
{
    RawVideoContext *context = avctx->priv_data;

    if (avctx->codec_tag)
        avctx->pix_fmt = findPixelFormat(avctx->codec_tag);
    else if (avctx->bits_per_sample){
        switch(avctx->bits_per_sample){
        case  8: avctx->pix_fmt= PIX_FMT_PAL8  ; break;
        case 15: avctx->pix_fmt= PIX_FMT_RGB555; break;
        case 16: avctx->pix_fmt= PIX_FMT_RGB565; break;
        case 24: avctx->pix_fmt= PIX_FMT_BGR24 ; break;
        case 32: avctx->pix_fmt= PIX_FMT_RGBA32; break;
        }
    }

    context->length = avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height);
    context->buffer = av_malloc(context->length);
    context->pic.pict_type = FF_I_TYPE;
    context->pic.key_frame = 1;

    avctx->coded_frame= &context->pic;

    if (!context->buffer)
        return -1;

    return 0;
}

static void flip(AVCodecContext *avctx, AVPicture * picture){
    if(!avctx->codec_tag && avctx->bits_per_sample && picture->linesize[2]==0){
        picture->data[0] += picture->linesize[0] * (avctx->height-1);
        picture->linesize[0] *= -1;
    }
}

static int raw_decode(AVCodecContext *avctx,
                            void *data, int *data_size,
                            uint8_t *buf, int buf_size)
{
    RawVideoContext *context = avctx->priv_data;

    AVFrame * frame = (AVFrame *) data;
    AVPicture * picture = (AVPicture *) data;

    frame->interlaced_frame = avctx->coded_frame->interlaced_frame;
    frame->top_field_first = avctx->coded_frame->top_field_first;

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

    flip(avctx, picture);
    *data_size = sizeof(AVPicture);
    return buf_size;
}

static int raw_close_decoder(AVCodecContext *avctx)
{
    RawVideoContext *context = avctx->priv_data;

    av_freep(&context->buffer);
    return 0;
}

/* RAW Encoder Implementation */

static int raw_init_encoder(AVCodecContext *avctx)
{
    avctx->coded_frame = (AVFrame *)avctx->priv_data;
    avctx->coded_frame->pict_type = FF_I_TYPE;
    avctx->coded_frame->key_frame = 1;
    if(!avctx->codec_tag)
        avctx->codec_tag = avcodec_pix_fmt_to_codec_tag(avctx->pix_fmt);
    return 0;
}

static int raw_encode(AVCodecContext *avctx,
                            unsigned char *frame, int buf_size, void *data)
{
    return avpicture_layout((AVPicture *)data, avctx->pix_fmt, avctx->width,
                                               avctx->height, frame, buf_size);
}

#ifdef CONFIG_RAWVIDEO_ENCODER
AVCodec rawvideo_encoder = {
    "rawvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_RAWVIDEO,
    sizeof(AVFrame),
    raw_init_encoder,
    raw_encode,
};
#endif // CONFIG_RAWVIDEO_ENCODER

AVCodec rawvideo_decoder = {
    "rawvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_RAWVIDEO,
    sizeof(RawVideoContext),
    raw_init_decoder,
    NULL,
    raw_close_decoder,
    raw_decode,
};
