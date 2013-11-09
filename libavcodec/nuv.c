/*
 * NuppelVideo decoder
 * Copyright (c) 2006 Reimar Doeffinger
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

#include <stdio.h>
#include <stdlib.h>

#include "libavutil/bswap.h"
#include "libavutil/common.h"
#include "libavutil/lzo.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "internal.h"
#include "rtjpeg.h"

typedef struct {
    AVFrame *pic;
    int codec_frameheader;
    int quality;
    int width, height;
    unsigned int decomp_size;
    unsigned char *decomp_buf;
    uint32_t lq[64], cq[64];
    RTJpegContext rtj;
    DSPContext dsp;
} NuvContext;

static const uint8_t fallback_lquant[] = {
    16,  11,  10,  16,  24,  40,  51,  61,
    12,  12,  14,  19,  26,  58,  60,  55,
    14,  13,  16,  24,  40,  57,  69,  56,
    14,  17,  22,  29,  51,  87,  80,  62,
    18,  22,  37,  56,  68, 109, 103,  77,
    24,  35,  55,  64,  81, 104, 113,  92,
    49,  64,  78,  87, 103, 121, 120, 101,
    72,  92,  95,  98, 112, 100, 103,  99
};

static const uint8_t fallback_cquant[] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

/**
 * @brief copy frame data from buffer to AVFrame, handling stride.
 * @param f destination AVFrame
 * @param src source buffer, does not use any line-stride
 * @param width width of the video frame
 * @param height height of the video frame
 */
static void copy_frame(AVFrame *f, const uint8_t *src, int width, int height)
{
    AVPicture pic;
    avpicture_fill(&pic, src, AV_PIX_FMT_YUV420P, width, height);
    av_picture_copy((AVPicture *)f, &pic, AV_PIX_FMT_YUV420P, width, height);
}

/**
 * @brief extract quantization tables from codec data into our context
 */
static int get_quant(AVCodecContext *avctx, NuvContext *c, const uint8_t *buf,
                     int size)
{
    int i;
    if (size < 2 * 64 * 4) {
        av_log(avctx, AV_LOG_ERROR, "insufficient rtjpeg quant data\n");
        return AVERROR_INVALIDDATA;
    }
    for (i = 0; i < 64; i++, buf += 4)
        c->lq[i] = AV_RL32(buf);
    for (i = 0; i < 64; i++, buf += 4)
        c->cq[i] = AV_RL32(buf);
    return 0;
}

/**
 * @brief set quantization tables from a quality value
 */
static void get_quant_quality(NuvContext *c, int quality)
{
    int i;
    quality = FFMAX(quality, 1);
    for (i = 0; i < 64; i++) {
        c->lq[i] = (fallback_lquant[i] << 7) / quality;
        c->cq[i] = (fallback_cquant[i] << 7) / quality;
    }
}

static int codec_reinit(AVCodecContext *avctx, int width, int height,
                        int quality)
{
    NuvContext *c = avctx->priv_data;
    int ret;

    width  = FFALIGN(width,  2);
    height = FFALIGN(height, 2);
    if (quality >= 0)
        get_quant_quality(c, quality);
    if (width != c->width || height != c->height) {
        void *ptr;
        if ((ret = av_image_check_size(height, width, 0, avctx)) < 0)
            return ret;
        avctx->width  = c->width  = width;
        avctx->height = c->height = height;
        ptr = av_fast_realloc(c->decomp_buf, &c->decomp_size,
                              c->height * c->width * 3 / 2 +
                              FF_INPUT_BUFFER_PADDING_SIZE +
                              RTJPEG_HEADER_SIZE);
        if (!ptr) {
            av_log(avctx, AV_LOG_ERROR,
                   "Can't allocate decompression buffer.\n");
            return AVERROR(ENOMEM);
        } else
            c->decomp_buf = ptr;
        ff_rtjpeg_decode_init(&c->rtj, &c->dsp, c->width, c->height,
                              c->lq, c->cq);
        av_frame_unref(c->pic);
    } else if (quality != c->quality)
        ff_rtjpeg_decode_init(&c->rtj, &c->dsp, c->width, c->height,
                              c->lq, c->cq);

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    NuvContext *c      = avctx->priv_data;
    AVFrame *picture   = data;
    int orig_size      = buf_size;
    int keyframe, ret;
    int result, init_frame = !avctx->frame_number;
    enum {
        NUV_UNCOMPRESSED  = '0',
        NUV_RTJPEG        = '1',
        NUV_RTJPEG_IN_LZO = '2',
        NUV_LZO           = '3',
        NUV_BLACK         = 'N',
        NUV_COPY_LAST     = 'L'
    } comptype;

    if (buf_size < 12) {
        av_log(avctx, AV_LOG_ERROR, "coded frame too small\n");
        return AVERROR_INVALIDDATA;
    }

    // codec data (rtjpeg quant tables)
    if (buf[0] == 'D' && buf[1] == 'R') {
        int ret;
        // skip rest of the frameheader.
        buf       = &buf[12];
        buf_size -= 12;
        ret       = get_quant(avctx, c, buf, buf_size);
        if (ret < 0)
            return ret;
        ff_rtjpeg_decode_init(&c->rtj, &c->dsp, c->width, c->height, c->lq,
                              c->cq);
        return orig_size;
    }

    if (buf[0] != 'V' || buf_size < 12) {
        av_log(avctx, AV_LOG_ERROR, "not a nuv video frame\n");
        return AVERROR_INVALIDDATA;
    }
    comptype = buf[1];
    switch (comptype) {
    case NUV_RTJPEG_IN_LZO:
    case NUV_RTJPEG:
        keyframe = !buf[2];
        break;
    case NUV_COPY_LAST:
        keyframe = 0;
        break;
    default:
        keyframe = 1;
        break;
    }
    // skip rest of the frameheader.
    buf       = &buf[12];
    buf_size -= 12;
    if (comptype == NUV_RTJPEG_IN_LZO || comptype == NUV_LZO) {
        int outlen = c->decomp_size - FF_INPUT_BUFFER_PADDING_SIZE;
        int inlen  = buf_size;
        if (av_lzo1x_decode(c->decomp_buf, &outlen, buf, &inlen)) {
            av_log(avctx, AV_LOG_ERROR, "error during lzo decompression\n");
            return AVERROR_INVALIDDATA;
        }
        buf      = c->decomp_buf;
        buf_size = outlen;
    }
    if (c->codec_frameheader) {
        int w, h, q;
        if (buf_size < RTJPEG_HEADER_SIZE || buf[4] != RTJPEG_HEADER_SIZE ||
            buf[5] != RTJPEG_FILE_VERSION) {
            av_log(avctx, AV_LOG_ERROR, "invalid nuv video frame\n");
            return AVERROR_INVALIDDATA;
        }
        w = AV_RL16(&buf[6]);
        h = AV_RL16(&buf[8]);
        q = buf[10];
        if ((result = codec_reinit(avctx, w, h, q)) < 0)
            return result;
        if (comptype == NUV_RTJPEG_IN_LZO || comptype == NUV_LZO)
            buf = c->decomp_buf;
        buf       = &buf[RTJPEG_HEADER_SIZE];
        buf_size -= RTJPEG_HEADER_SIZE;
    }

    if (keyframe) {
        av_frame_unref(c->pic);
        init_frame = 1;
    }

    result = ff_reget_buffer(avctx, c->pic);
    if (result < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return result;
    }
    if (init_frame) {
        memset(c->pic->data[0], 0,    avctx->height * c->pic->linesize[0]);
        memset(c->pic->data[1], 0x80, avctx->height * c->pic->linesize[1] / 2);
        memset(c->pic->data[2], 0x80, avctx->height * c->pic->linesize[2] / 2);
    }

    c->pic->pict_type = keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;
    c->pic->key_frame = keyframe;
    // decompress/copy/whatever data
    switch (comptype) {
    case NUV_LZO:
    case NUV_UNCOMPRESSED: {
        int height = c->height;
        if (buf_size < c->width * height * 3 / 2) {
            av_log(avctx, AV_LOG_ERROR, "uncompressed frame too short\n");
            height = buf_size / c->width / 3 * 2;
        }
        copy_frame(c->pic, buf, c->width, height);
        break;
    }
    case NUV_RTJPEG_IN_LZO:
    case NUV_RTJPEG:
        ret = ff_rtjpeg_decode_frame_yuv420(&c->rtj, c->pic, buf, buf_size);
        if (ret < 0)
            return ret;
        break;
    case NUV_BLACK:
        memset(c->pic->data[0], 0, c->width * c->height);
        memset(c->pic->data[1], 128, c->width * c->height / 4);
        memset(c->pic->data[2], 128, c->width * c->height / 4);
        break;
    case NUV_COPY_LAST:
        /* nothing more to do here */
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "unknown compression\n");
        return AVERROR_INVALIDDATA;
    }

    if ((result = av_frame_ref(picture, c->pic)) < 0)
        return result;

    *got_frame = 1;
    return orig_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    NuvContext *c  = avctx->priv_data;
    int ret;

    c->pic = av_frame_alloc();
    if (!c->pic)
        return AVERROR(ENOMEM);

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    c->decomp_buf  = NULL;
    c->quality     = -1;
    c->width       = 0;
    c->height      = 0;

    c->codec_frameheader = avctx->codec_tag == MKTAG('R', 'J', 'P', 'G');

    if (avctx->extradata_size)
        get_quant(avctx, c, avctx->extradata, avctx->extradata_size);

    ff_dsputil_init(&c->dsp, avctx);

    if ((ret = codec_reinit(avctx, avctx->width, avctx->height, -1)) < 0)
        return ret;

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    NuvContext *c = avctx->priv_data;

    av_freep(&c->decomp_buf);
    av_frame_free(&c->pic);

    return 0;
}

AVCodec ff_nuv_decoder = {
    .name           = "nuv",
    .long_name      = NULL_IF_CONFIG_SMALL("NuppelVideo/RTJPEG"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_NUV,
    .priv_data_size = sizeof(NuvContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
