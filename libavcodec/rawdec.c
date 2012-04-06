/*
 * Raw Video Decoder
 * Copyright (c) 2001 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Raw Video Decoder
 */

#include "avcodec.h"
#include "imgconvert.h"
#include "raw.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"

typedef struct RawVideoContext {
    uint32_t palette[AVPALETTE_COUNT];
    unsigned char * buffer;  /* block of memory for holding one frame */
    int             length;  /* number of bytes in buffer */
    int flip;
    AVFrame pic;             ///< AVCodecContext.coded_frame
} RawVideoContext;

static const PixelFormatTag pix_fmt_bps_avi[] = {
    { PIX_FMT_PAL8,    4 },
    { PIX_FMT_PAL8,    8 },
    { PIX_FMT_RGB444, 12 },
    { PIX_FMT_RGB555, 15 },
    { PIX_FMT_RGB555, 16 },
    { PIX_FMT_BGR24,  24 },
    { PIX_FMT_RGB32,  32 },
    { PIX_FMT_NONE, 0 },
};

static const PixelFormatTag pix_fmt_bps_mov[] = {
    { PIX_FMT_MONOWHITE, 1 },
    { PIX_FMT_PAL8,      2 },
    { PIX_FMT_PAL8,      4 },
    { PIX_FMT_PAL8,      8 },
    // FIXME swscale does not support 16 bit in .mov, sample 16bit.mov
    // http://developer.apple.com/documentation/QuickTime/QTFF/QTFFChap3/qtff3.html
    { PIX_FMT_RGB555BE, 16 },
    { PIX_FMT_RGB24,    24 },
    { PIX_FMT_ARGB,     32 },
    { PIX_FMT_MONOWHITE,33 },
    { PIX_FMT_NONE, 0 },
};

static enum PixelFormat find_pix_fmt(const PixelFormatTag *tags, unsigned int fourcc)
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
        avctx->pix_fmt = find_pix_fmt(pix_fmt_bps_mov, avctx->bits_per_coded_sample);
    else if (avctx->codec_tag == MKTAG('W','R','A','W'))
        avctx->pix_fmt = find_pix_fmt(pix_fmt_bps_avi, avctx->bits_per_coded_sample);
    else if (avctx->codec_tag)
        avctx->pix_fmt = find_pix_fmt(ff_raw_pix_fmt_tags, avctx->codec_tag);
    else if (avctx->pix_fmt == PIX_FMT_NONE && avctx->bits_per_coded_sample)
        avctx->pix_fmt = find_pix_fmt(pix_fmt_bps_avi, avctx->bits_per_coded_sample);

    ff_set_systematic_pal2(context->palette, avctx->pix_fmt);
    context->length = avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height);
    if((avctx->bits_per_coded_sample == 4 || avctx->bits_per_coded_sample == 2) &&
       avctx->pix_fmt==PIX_FMT_PAL8 &&
       (!avctx->codec_tag || avctx->codec_tag == MKTAG('r','a','w',' '))){
        context->buffer = av_malloc(context->length);
        if (!context->buffer)
            return -1;
    }
    context->pic.pict_type = AV_PICTURE_TYPE_I;
    context->pic.key_frame = 1;

    avctx->coded_frame= &context->pic;

    if((avctx->extradata_size >= 9 && !memcmp(avctx->extradata + avctx->extradata_size - 9, "BottomUp", 9)) ||
        avctx->codec_tag == MKTAG(3, 0, 0, 0) || avctx->codec_tag == MKTAG('W','R','A','W'))
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
    int res;

    AVFrame   *frame   = data;
    AVPicture *picture = data;

    frame->pict_type        = avctx->coded_frame->pict_type;
    frame->interlaced_frame = avctx->coded_frame->interlaced_frame;
    frame->top_field_first = avctx->coded_frame->top_field_first;
    frame->reordered_opaque = avctx->reordered_opaque;
    frame->pkt_pts          = avctx->pkt->pts;

    if(buf_size < context->length - (avctx->pix_fmt==PIX_FMT_PAL8 ? 256*4 : 0))
        return -1;

    //2bpp and 4bpp raw in avi and mov (yes this is ugly ...)
    if (context->buffer) {
        int i;
        uint8_t *dst = context->buffer;
        buf_size = context->length - 256*4;
        if (avctx->bits_per_coded_sample == 4){
            for(i=0; 2*i+1 < buf_size; i++){
                dst[2*i+0]= buf[i]>>4;
                dst[2*i+1]= buf[i]&15;
            }
        } else
            for(i=0; 4*i+3 < buf_size; i++){
                dst[4*i+0]= buf[i]>>6;
                dst[4*i+1]= buf[i]>>4&3;
                dst[4*i+2]= buf[i]>>2&3;
                dst[4*i+3]= buf[i]   &3;
            }
        buf= dst;
    }

    if(avctx->codec_tag == MKTAG('A', 'V', '1', 'x') ||
       avctx->codec_tag == MKTAG('A', 'V', 'u', 'p'))
        buf += buf_size - context->length;

    if ((res = avpicture_fill(picture, buf, avctx->pix_fmt,
                              avctx->width, avctx->height)) < 0)
        return res;
    if((avctx->pix_fmt==PIX_FMT_PAL8 && buf_size < context->length) ||
       (av_pix_fmt_descriptors[avctx->pix_fmt].flags & PIX_FMT_PSEUDOPAL)) {
        frame->data[1]= context->palette;
    }
    if (avctx->pix_fmt == PIX_FMT_PAL8) {
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, NULL);

        if (pal) {
            memcpy(frame->data[1], pal, AVPALETTE_SIZE);
            frame->palette_has_changed = 1;
        }
    }
    if(avctx->pix_fmt==PIX_FMT_BGR24 && ((frame->linesize[0]+3)&~3)*avctx->height <= buf_size)
        frame->linesize[0] = (frame->linesize[0]+3)&~3;

    if(context->flip)
        flip(avctx, picture);

    if (   avctx->codec_tag == MKTAG('Y', 'V', '1', '2')
        || avctx->codec_tag == MKTAG('Y', 'V', '1', '6')
        || avctx->codec_tag == MKTAG('Y', 'V', '2', '4')
        || avctx->codec_tag == MKTAG('Y', 'V', 'U', '9'))
        FFSWAP(uint8_t *, picture->data[1], picture->data[2]);

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

AVCodec ff_rawvideo_decoder = {
    .name           = "rawvideo",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_RAWVIDEO,
    .priv_data_size = sizeof(RawVideoContext),
    .init           = raw_init_decoder,
    .close          = raw_close_decoder,
    .decode         = raw_decode,
    .long_name      = NULL_IF_CONFIG_SMALL("raw video"),
};
