/*
 * Copyright (c) 2012 Derek Buitenhuis
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * version 2 of the License.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Known FOURCCs:
 *     'ULY0' (YCbCr 4:2:0), 'ULY2' (YCbCr 4:2:2), 'ULRG' (RGB), 'ULRA' (RGBA),
 *     'ULH0' (YCbCr 4:2:0 BT.709), 'ULH2' (YCbCr 4:2:2 BT.709)
 */

extern "C" {
#include "libavutil/avassert.h"
#include "avcodec.h"
#include "internal.h"
}

#include "libutvideo.h"
#include "put_bits.h"

static av_cold int utvideo_encode_init(AVCodecContext *avctx)
{
    UtVideoContext *utv = (UtVideoContext *)avctx->priv_data;
    UtVideoExtra *info;
    uint32_t flags, in_format;
    int ret;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        in_format = UTVF_YV12;
        avctx->bits_per_coded_sample = 12;
        if (avctx->colorspace == AVCOL_SPC_BT709)
            avctx->codec_tag = MKTAG('U', 'L', 'H', '0');
        else
            avctx->codec_tag = MKTAG('U', 'L', 'Y', '0');
        break;
    case AV_PIX_FMT_YUYV422:
        in_format = UTVF_YUYV;
        avctx->bits_per_coded_sample = 16;
        if (avctx->colorspace == AVCOL_SPC_BT709)
            avctx->codec_tag = MKTAG('U', 'L', 'H', '2');
        else
            avctx->codec_tag = MKTAG('U', 'L', 'Y', '2');
        break;
    case AV_PIX_FMT_BGR24:
        in_format = UTVF_NFCC_BGR_BU;
        avctx->bits_per_coded_sample = 24;
        avctx->codec_tag = MKTAG('U', 'L', 'R', 'G');
        break;
    case AV_PIX_FMT_RGB32:
        in_format = UTVF_NFCC_BGRA_BU;
        avctx->bits_per_coded_sample = 32;
        avctx->codec_tag = MKTAG('U', 'L', 'R', 'A');
        break;
    default:
        return AVERROR(EINVAL);
    }

    /* Check before we alloc anything */
    if (avctx->prediction_method != 0 && avctx->prediction_method != 2) {
        av_log(avctx, AV_LOG_ERROR, "Invalid prediction method.\n");
        return AVERROR(EINVAL);
    }

    flags = ((avctx->prediction_method + 1) << 8) | (avctx->thread_count - 1);

    avctx->priv_data = utv;
    avctx->coded_frame = av_frame_alloc();

    /* Alloc extradata buffer */
    info = (UtVideoExtra *)av_malloc(sizeof(*info));

    if (!info) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate extradata buffer.\n");
        return AVERROR(ENOMEM);
    }

    /*
     * We use this buffer to hold the data that Ut Video returns,
     * since we cannot decode planes separately with it.
     */
    ret = avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height);
    if (ret < 0) {
        av_free(info);
        return ret;
    }
    utv->buf_size = ret;

    utv->buffer = (uint8_t *)av_malloc(utv->buf_size);

    if (utv->buffer == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate output buffer.\n");
        av_free(info);
        return AVERROR(ENOMEM);
    }

    /*
     * Create a Ut Video instance. Since the function wants
     * an "interface name" string, pass it the name of the lib.
     */
    utv->codec = CCodec::CreateInstance(UNFCC(avctx->codec_tag), "libavcodec");

    /* Initialize encoder */
    utv->codec->EncodeBegin(in_format, avctx->width, avctx->height,
                            CBGROSSWIDTH_WINDOWS);

    /* Get extradata from encoder */
    avctx->extradata_size = utv->codec->EncodeGetExtraDataSize();
    utv->codec->EncodeGetExtraData(info, avctx->extradata_size, in_format,
                                   avctx->width, avctx->height,
                                   CBGROSSWIDTH_WINDOWS);
    avctx->extradata = (uint8_t *)info;

    /* Set flags */
    utv->codec->SetState(&flags, sizeof(flags));

    return 0;
}

static int utvideo_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *pic, int *got_packet)
{
    UtVideoContext *utv = (UtVideoContext *)avctx->priv_data;
    int w = avctx->width, h = avctx->height;
    int ret, rgb_size, i;
    bool keyframe;
    uint8_t *y, *u, *v;
    uint8_t *dst;

    /* Alloc buffer */
    if ((ret = ff_alloc_packet2(avctx, pkt, utv->buf_size)) < 0)
        return ret;

    dst = pkt->data;

    /* Move input if needed data into Ut Video friendly buffer */
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        y = utv->buffer;
        u = y + w * h;
        v = u + w * h / 4;
        for (i = 0; i < h; i++) {
            memcpy(y, pic->data[0] + i * pic->linesize[0], w);
            y += w;
        }
        for (i = 0; i < h / 2; i++) {
            memcpy(u, pic->data[2] + i * pic->linesize[2], w >> 1);
            memcpy(v, pic->data[1] + i * pic->linesize[1], w >> 1);
            u += w >> 1;
            v += w >> 1;
        }
        break;
    case AV_PIX_FMT_YUYV422:
        for (i = 0; i < h; i++)
            memcpy(utv->buffer + i * (w << 1),
                   pic->data[0] + i * pic->linesize[0], w << 1);
        break;
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_RGB32:
        /* Ut Video takes bottom-up BGR */
        rgb_size = avctx->pix_fmt == AV_PIX_FMT_BGR24 ? 3 : 4;
        for (i = 0; i < h; i++)
            memcpy(utv->buffer + (h - i - 1) * w * rgb_size,
                   pic->data[0] + i * pic->linesize[0],
                   w * rgb_size);
        break;
    default:
        return AVERROR(EINVAL);
    }

    /* Encode frame */
    pkt->size = utv->codec->EncodeFrame(dst, &keyframe, utv->buffer);

    if (!pkt->size) {
        av_log(avctx, AV_LOG_ERROR, "EncodeFrame failed!\n");
        return AVERROR_INVALIDDATA;
    }

    /*
     * Ut Video is intra-only and every frame is a keyframe,
     * and the API always returns true. In case something
     * durastic changes in the future, such as inter support,
     * assert that this is true.
     */
    av_assert2(keyframe == true);
    avctx->coded_frame->key_frame = 1;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

static av_cold int utvideo_encode_close(AVCodecContext *avctx)
{
    UtVideoContext *utv = (UtVideoContext *)avctx->priv_data;

    av_frame_free(&avctx->coded_frame);
    av_freep(&avctx->extradata);
    av_freep(&utv->buffer);

    utv->codec->EncodeEnd();
    CCodec::DeleteInstance(utv->codec);

    return 0;
}

AVCodec ff_libutvideo_encoder = {
    "libutvideo",
    NULL_IF_CONFIG_SMALL("Ut Video"),
    AVMEDIA_TYPE_VIDEO,
    AV_CODEC_ID_UTVIDEO,
    CODEC_CAP_AUTO_THREADS | CODEC_CAP_LOSSLESS,
    NULL, /* supported_framerates */
    (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUYV422, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE
    },
    NULL, /* supported_samplerates */
    NULL, /* sample_fmts */
    NULL, /* channel_layouts */
    0,    /* max_lowres */
    NULL, /* priv_class */
    NULL, /* profiles */
    sizeof(UtVideoContext),
    NULL, /* next */
    NULL, /* init_thread_copy */
    NULL, /* update_thread_context */
    NULL, /* defaults */
    NULL, /* init_static_data */
    utvideo_encode_init,
    NULL, /* encode */
    utvideo_encode_frame,
    NULL, /* decode */
    utvideo_encode_close,
    NULL, /* flush */
};
