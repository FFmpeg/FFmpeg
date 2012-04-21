/*
 * V210 decoder
 *
 * Copyright (C) 2009 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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
#include "v210dec.h"
#include "libavutil/bswap.h"

#define READ_PIXELS(a, b, c)         \
    do {                             \
        val  = av_le2ne32(*src++);   \
        *a++ =  val & 0x3FF;         \
        *b++ = (val >> 10) & 0x3FF;  \
        *c++ = (val >> 20) & 0x3FF;  \
    } while (0)

static void v210_planar_unpack_c(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int width)
{
    uint32_t val;
    int i;

    for( i = 0; i < width-5; i += 6 ){
        READ_PIXELS(u, y, v);
        READ_PIXELS(y, u, y);
        READ_PIXELS(v, y, u);
        READ_PIXELS(y, v, y);
    }
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    V210DecContext *s = avctx->priv_data;

    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "v210 needs even width\n");
        return -1;
    }
    avctx->pix_fmt             = PIX_FMT_YUV422P10;
    avctx->bits_per_raw_sample = 10;

    avctx->coded_frame         = avcodec_alloc_frame();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    s->unpack_frame            = v210_planar_unpack_c;

    if (HAVE_MMX)
        v210_x86_init(s);

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt)
{
    V210DecContext *s = avctx->priv_data;

    int h, w, stride, aligned_input;
    AVFrame *pic = avctx->coded_frame;
    const uint8_t *psrc = avpkt->data;
    uint16_t *y, *u, *v;

    if (s->custom_stride )
        stride = s->custom_stride;
    else {
        int aligned_width = ((avctx->width + 47) / 48) * 48;
        stride = aligned_width * 8 / 3;
    }

    if (avpkt->size < stride * avctx->height) {
        if ((((avctx->width + 23) / 24) * 24 * 8) / 3 * avctx->height == avpkt->size) {
            stride = avpkt->size / avctx->height;
            if (!s->stride_warning_shown)
                av_log(avctx, AV_LOG_WARNING, "Broken v210 with too small padding (64 byte) detected\n");
            s->stride_warning_shown = 1;
        } else {
            av_log(avctx, AV_LOG_ERROR, "packet too small\n");
            return -1;
        }
    }

    aligned_input = !((uintptr_t)psrc & 0xf) && !(stride & 0xf);
    if (aligned_input != s->aligned_input) {
        s->aligned_input = aligned_input;
        if (HAVE_MMX)
            v210_x86_init(s);
    }

    if (pic->data[0])
        avctx->release_buffer(avctx, pic);

    pic->reference = 0;
    if (avctx->get_buffer(avctx, pic) < 0)
        return -1;

    y = (uint16_t*)pic->data[0];
    u = (uint16_t*)pic->data[1];
    v = (uint16_t*)pic->data[2];
    pic->pict_type = AV_PICTURE_TYPE_I;
    pic->key_frame = 1;

    for (h = 0; h < avctx->height; h++) {
        const uint32_t *src = (const uint32_t*)psrc;
        uint32_t val;

        w = (avctx->width / 6) * 6;
        s->unpack_frame(src, y, u, v, w);

        y += w;
        u += w >> 1;
        v += w >> 1;
        src += (w << 1) / 3;

        if (w < avctx->width - 1) {
            READ_PIXELS(u, y, v);

            val  = av_le2ne32(*src++);
            *y++ =  val & 0x3FF;
            if (w < avctx->width - 3) {
                *u++ = (val >> 10) & 0x3FF;
                *y++ = (val >> 20) & 0x3FF;

                val  = av_le2ne32(*src++);
                *v++ =  val & 0x3FF;
                *y++ = (val >> 10) & 0x3FF;
            }
        }

        psrc += stride;
        y += pic->linesize[0] / 2 - avctx->width;
        u += pic->linesize[1] / 2 - avctx->width / 2;
        v += pic->linesize[2] / 2 - avctx->width / 2;
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = *avctx->coded_frame;

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

#define V210DEC_FLAGS AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption v210dec_options[] = {
    {"custom_stride", "Custom V210 stride", offsetof(V210DecContext, custom_stride), FF_OPT_TYPE_INT,
     {.dbl = 0}, INT_MIN, INT_MAX, V210DEC_FLAGS},
    {NULL}
};

static const AVClass v210dec_class = {
    "V210 Decoder",
    av_default_item_name,
    v210dec_options,
    LIBAVUTIL_VERSION_INT,
};

AVCodec ff_v210_decoder = {
    .name           = "v210",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_V210,
    .priv_data_size = sizeof(V210DecContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
    .priv_class     = &v210dec_class,
};
