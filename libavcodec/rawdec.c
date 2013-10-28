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
 * @file
 * Raw Video Decoder
 */

#include "avcodec.h"
#include "raw.h"
#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

typedef struct RawVideoContext {
    AVClass *av_class;
    AVBufferRef *palette;
    int frame_size;  /* size of the frame in bytes */
    int flip;
    int is_2_4_bpp; // 2 or 4 bpp raw in avi/mov
    int is_yuv2;
    int tff;
} RawVideoContext;

static const AVOption options[]={
{"top", "top field first", offsetof(RawVideoContext, tff), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 1, AV_OPT_FLAG_DECODING_PARAM|AV_OPT_FLAG_VIDEO_PARAM},
{NULL}
};

static const AVClass rawdec_class = {
    .class_name = "rawdec",
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const PixelFormatTag pix_fmt_bps_avi[] = {
    { AV_PIX_FMT_MONOWHITE, 1 },
    { AV_PIX_FMT_PAL8,    2 },
    { AV_PIX_FMT_PAL8,    4 },
    { AV_PIX_FMT_PAL8,    8 },
    { AV_PIX_FMT_RGB444LE, 12 },
    { AV_PIX_FMT_RGB555LE, 15 },
    { AV_PIX_FMT_RGB555LE, 16 },
    { AV_PIX_FMT_BGR24,  24 },
    { AV_PIX_FMT_BGRA,   32 },
    { AV_PIX_FMT_NONE,    0 },
};

static const PixelFormatTag pix_fmt_bps_mov[] = {
    { AV_PIX_FMT_MONOWHITE, 1 },
    { AV_PIX_FMT_PAL8,      2 },
    { AV_PIX_FMT_PAL8,      4 },
    { AV_PIX_FMT_PAL8,      8 },
    // FIXME swscale does not support 16 bit in .mov, sample 16bit.mov
    // http://developer.apple.com/documentation/QuickTime/QTFF/QTFFChap3/qtff3.html
    { AV_PIX_FMT_RGB555BE, 16 },
    { AV_PIX_FMT_RGB24,    24 },
    { AV_PIX_FMT_ARGB,     32 },
    { AV_PIX_FMT_MONOWHITE,33 },
    { AV_PIX_FMT_NONE,      0 },
};

enum AVPixelFormat avpriv_find_pix_fmt(const PixelFormatTag *tags,
                                       unsigned int fourcc)
{
    while (tags->pix_fmt >= 0) {
        if (tags->fourcc == fourcc)
            return tags->pix_fmt;
        tags++;
    }
    return AV_PIX_FMT_NONE;
}

#if LIBAVCODEC_VERSION_MAJOR < 55
enum AVPixelFormat ff_find_pix_fmt(const PixelFormatTag *tags, unsigned int fourcc)
{
    return avpriv_find_pix_fmt(tags, fourcc);
}
#endif

static av_cold int raw_init_decoder(AVCodecContext *avctx)
{
    RawVideoContext *context = avctx->priv_data;
    const AVPixFmtDescriptor *desc;

    if (   avctx->codec_tag == MKTAG('r','a','w',' ')
        || avctx->codec_tag == MKTAG('N','O','1','6'))
        avctx->pix_fmt = avpriv_find_pix_fmt(pix_fmt_bps_mov,
                                      avctx->bits_per_coded_sample);
    else if (avctx->codec_tag == MKTAG('W', 'R', 'A', 'W'))
        avctx->pix_fmt = avpriv_find_pix_fmt(pix_fmt_bps_avi,
                                      avctx->bits_per_coded_sample);
    else if (avctx->codec_tag)
        avctx->pix_fmt = avpriv_find_pix_fmt(ff_raw_pix_fmt_tags, avctx->codec_tag);
    else if (avctx->pix_fmt == AV_PIX_FMT_NONE && avctx->bits_per_coded_sample)
        avctx->pix_fmt = avpriv_find_pix_fmt(pix_fmt_bps_avi,
                                      avctx->bits_per_coded_sample);

    desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    if (!desc) {
        av_log(avctx, AV_LOG_ERROR, "Invalid pixel format.\n");
        return AVERROR(EINVAL);
    }

    if (desc->flags & (AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_PSEUDOPAL)) {
        context->palette = av_buffer_alloc(AVPALETTE_SIZE);
        if (!context->palette)
            return AVERROR(ENOMEM);
        if (desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL)
            avpriv_set_systematic_pal2((uint32_t*)context->palette->data, avctx->pix_fmt);
        else
            memset(context->palette->data, 0, AVPALETTE_SIZE);
    }

    if ((avctx->bits_per_coded_sample == 4 || avctx->bits_per_coded_sample == 2) &&
        avctx->pix_fmt == AV_PIX_FMT_PAL8 &&
       (!avctx->codec_tag || avctx->codec_tag == MKTAG('r','a','w',' '))) {
        context->is_2_4_bpp = 1;
        context->frame_size = avpicture_get_size(avctx->pix_fmt,
                                                 FFALIGN(avctx->width, 16),
                                                 avctx->height);
    } else {
        context->frame_size = avpicture_get_size(avctx->pix_fmt, avctx->width,
                                                 avctx->height);
    }

    if ((avctx->extradata_size >= 9 &&
         !memcmp(avctx->extradata + avctx->extradata_size - 9, "BottomUp", 9)) ||
        avctx->codec_tag == MKTAG('c','y','u','v') ||
        avctx->codec_tag == MKTAG(3, 0, 0, 0) ||
        avctx->codec_tag == MKTAG('W','R','A','W'))
        context->flip = 1;

    if (avctx->codec_tag == AV_RL32("yuv2") &&
        avctx->pix_fmt   == AV_PIX_FMT_YUYV422)
        context->is_yuv2 = 1;

    return 0;
}

static void flip(AVCodecContext *avctx, AVPicture *picture)
{
    picture->data[0]     += picture->linesize[0] * (avctx->height - 1);
    picture->linesize[0] *= -1;
}

static int raw_decode(AVCodecContext *avctx, void *data, int *got_frame,
                      AVPacket *avpkt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    RawVideoContext *context       = avctx->priv_data;
    const uint8_t *buf             = avpkt->data;
    int buf_size                   = avpkt->size;
    int linesize_align             = 4;
    int res, len;
    int need_copy                  = !avpkt->buf || context->is_2_4_bpp || context->is_yuv2;

    AVFrame   *frame   = data;
    AVPicture *picture = data;

    frame->pict_type        = AV_PICTURE_TYPE_I;
    frame->key_frame        = 1;
    frame->reordered_opaque = avctx->reordered_opaque;
    frame->pkt_pts          = avctx->pkt->pts;
    av_frame_set_pkt_pos     (frame, avctx->pkt->pos);
    av_frame_set_pkt_duration(frame, avctx->pkt->duration);

    if (context->tff >= 0) {
        frame->interlaced_frame = 1;
        frame->top_field_first  = context->tff;
    }

    if ((res = av_image_check_size(avctx->width, avctx->height, 0, avctx)) < 0)
        return res;

    if (need_copy)
        frame->buf[0] = av_buffer_alloc(FFMAX(context->frame_size, buf_size));
    else
        frame->buf[0] = av_buffer_ref(avpkt->buf);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    //2bpp and 4bpp raw in avi and mov (yes this is ugly ...)
    if (context->is_2_4_bpp) {
        int i;
        uint8_t *dst = frame->buf[0]->data;
        buf_size = context->frame_size - AVPALETTE_SIZE;
        if (avctx->bits_per_coded_sample == 4) {
            for (i = 0; 2 * i + 1 < buf_size && i<avpkt->size; i++) {
                dst[2 * i + 0] = buf[i] >> 4;
                dst[2 * i + 1] = buf[i] & 15;
            }
            linesize_align = 8;
        } else {
            av_assert0(avctx->bits_per_coded_sample == 2);
            for (i = 0; 4 * i + 3 < buf_size && i<avpkt->size; i++) {
                dst[4 * i + 0] = buf[i] >> 6;
                dst[4 * i + 1] = buf[i] >> 4 & 3;
                dst[4 * i + 2] = buf[i] >> 2 & 3;
                dst[4 * i + 3] = buf[i]      & 3;
            }
            linesize_align = 16;
        }
        buf = dst;
    } else if (need_copy) {
        memcpy(frame->buf[0]->data, buf, buf_size);
        buf = frame->buf[0]->data;
    }

    if (avctx->codec_tag == MKTAG('A', 'V', '1', 'x') ||
        avctx->codec_tag == MKTAG('A', 'V', 'u', 'p'))
        buf += buf_size - context->frame_size;

    len = context->frame_size - (avctx->pix_fmt==AV_PIX_FMT_PAL8 ? AVPALETTE_SIZE : 0);
    if (buf_size < len) {
        av_log(avctx, AV_LOG_ERROR, "Invalid buffer size, packet size %d < expected frame_size %d\n", buf_size, len);
        av_buffer_unref(&frame->buf[0]);
        return AVERROR(EINVAL);
    }

    if ((res = avpicture_fill(picture, buf, avctx->pix_fmt,
                              avctx->width, avctx->height)) < 0) {
        av_buffer_unref(&frame->buf[0]);
        return res;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE,
                                                     NULL);

        if (pal) {
            av_buffer_unref(&context->palette);
            context->palette = av_buffer_alloc(AVPALETTE_SIZE);
            if (!context->palette) {
                av_buffer_unref(&frame->buf[0]);
                return AVERROR(ENOMEM);
            }
            memcpy(context->palette->data, pal, AVPALETTE_SIZE);
            frame->palette_has_changed = 1;
        }
    }

    if ((avctx->pix_fmt==AV_PIX_FMT_BGR24    ||
        avctx->pix_fmt==AV_PIX_FMT_GRAY8    ||
        avctx->pix_fmt==AV_PIX_FMT_RGB555LE ||
        avctx->pix_fmt==AV_PIX_FMT_RGB555BE ||
        avctx->pix_fmt==AV_PIX_FMT_RGB565LE ||
        avctx->pix_fmt==AV_PIX_FMT_MONOWHITE ||
        avctx->pix_fmt==AV_PIX_FMT_PAL8) &&
        FFALIGN(frame->linesize[0], linesize_align) * avctx->height <= buf_size)
        frame->linesize[0] = FFALIGN(frame->linesize[0], linesize_align);

    if (avctx->pix_fmt == AV_PIX_FMT_NV12 && avctx->codec_tag == MKTAG('N', 'V', '1', '2') &&
        FFALIGN(frame->linesize[0], linesize_align) * avctx->height +
        FFALIGN(frame->linesize[1], linesize_align) * ((avctx->height + 1) / 2) <= buf_size) {
        int la0 = FFALIGN(frame->linesize[0], linesize_align);
        frame->data[1] += (la0 - frame->linesize[0]) * avctx->height;
        frame->linesize[0] = la0;
        frame->linesize[1] = FFALIGN(frame->linesize[1], linesize_align);
    }

    if ((avctx->pix_fmt == AV_PIX_FMT_PAL8 && buf_size < context->frame_size) ||
        (desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL)) {
        frame->buf[1]  = av_buffer_ref(context->palette);
        if (!frame->buf[1]) {
            av_buffer_unref(&frame->buf[0]);
            return AVERROR(ENOMEM);
        }
        frame->data[1] = frame->buf[1]->data;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_BGR24 &&
        ((frame->linesize[0] + 3) & ~3) * avctx->height <= buf_size)
        frame->linesize[0] = (frame->linesize[0] + 3) & ~3;

    if (context->flip)
        flip(avctx, picture);

    if (avctx->codec_tag == MKTAG('Y', 'V', '1', '2') ||
        avctx->codec_tag == MKTAG('Y', 'V', '1', '6') ||
        avctx->codec_tag == MKTAG('Y', 'V', '2', '4') ||
        avctx->codec_tag == MKTAG('Y', 'V', 'U', '9'))
        FFSWAP(uint8_t *, picture->data[1], picture->data[2]);

    if (avctx->codec_tag == AV_RL32("I420") && (avctx->width+1)*(avctx->height+1) * 3/2 == buf_size) {
        picture->data[1] = picture->data[1] +  (avctx->width+1)*(avctx->height+1) -avctx->width*avctx->height;
        picture->data[2] = picture->data[2] + ((avctx->width+1)*(avctx->height+1) -avctx->width*avctx->height)*5/4;
    }

    if (avctx->codec_tag == AV_RL32("yuv2") &&
        avctx->pix_fmt   == AV_PIX_FMT_YUYV422) {
        int x, y;
        uint8_t *line = picture->data[0];
        for (y = 0; y < avctx->height; y++) {
            for (x = 0; x < avctx->width; x++)
                line[2 * x + 1] ^= 0x80;
            line += picture->linesize[0];
        }
    }
    if (avctx->codec_tag == AV_RL32("YVYU") &&
        avctx->pix_fmt   == AV_PIX_FMT_YUYV422) {
        int x, y;
        uint8_t *line = picture->data[0];
        for(y = 0; y < avctx->height; y++) {
            for(x = 0; x < avctx->width - 1; x += 2)
                FFSWAP(uint8_t, line[2*x + 1], line[2*x + 3]);
            line += picture->linesize[0];
        }
    }

    if (avctx->field_order > AV_FIELD_PROGRESSIVE) { /* we have interlaced material flagged in container */
        frame->interlaced_frame = 1;
        if (avctx->field_order == AV_FIELD_TT || avctx->field_order == AV_FIELD_TB)
            frame->top_field_first = 1;
    }

    *got_frame = 1;
    return buf_size;
}

static av_cold int raw_close_decoder(AVCodecContext *avctx)
{
    RawVideoContext *context = avctx->priv_data;

    av_buffer_unref(&context->palette);
    return 0;
}

AVCodec ff_rawvideo_decoder = {
    .name           = "rawvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("raw video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_RAWVIDEO,
    .priv_data_size = sizeof(RawVideoContext),
    .init           = raw_init_decoder,
    .close          = raw_close_decoder,
    .decode         = raw_decode,
    .priv_class     = &rawdec_class,
};
