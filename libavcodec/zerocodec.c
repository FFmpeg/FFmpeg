/*
 * ZeroCodec Decoder
 *
 * Copyright (c) 2012, Derek Buitenhuis
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <zlib.h>

#include "avcodec.h"
#include "internal.h"
#include "libavutil/common.h"

typedef struct ZeroCodecContext {
    AVFrame  *previous_frame;
    z_stream zstream;
} ZeroCodecContext;

static int zerocodec_decode_frame(AVCodecContext *avctx, void *data,
                                  int *got_frame, AVPacket *avpkt)
{
    ZeroCodecContext *zc = avctx->priv_data;
    AVFrame *pic         = data;
    AVFrame *prev_pic    = zc->previous_frame;
    z_stream *zstream    = &zc->zstream;
    uint8_t *prev        = prev_pic->data[0];
    uint8_t *dst;
    int i, j, zret, ret;

    if (avpkt->flags & AV_PKT_FLAG_KEY) {
        pic->key_frame = 1;
        pic->pict_type = AV_PICTURE_TYPE_I;
    } else {
        if (!prev) {
            av_log(avctx, AV_LOG_ERROR, "Missing reference frame.\n");
            return AVERROR_INVALIDDATA;
        }

        prev += (avctx->height - 1) * prev_pic->linesize[0];

        pic->key_frame = 0;
        pic->pict_type = AV_PICTURE_TYPE_P;
    }

    zret = inflateReset(zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not reset inflate: %d.\n", zret);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, pic, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    zstream->next_in  = avpkt->data;
    zstream->avail_in = avpkt->size;

    dst = pic->data[0] + (avctx->height - 1) * pic->linesize[0];

    /**
     * ZeroCodec has very simple interframe compression. If a value
     * is the same as the previous frame, set it to 0.
     */

    for (i = 0; i < avctx->height; i++) {
        zstream->next_out  = dst;
        zstream->avail_out = avctx->width << 1;

        zret = inflate(zstream, Z_SYNC_FLUSH);
        if (zret != Z_OK && zret != Z_STREAM_END) {
            av_log(avctx, AV_LOG_ERROR,
                   "Inflate failed with return code: %d.\n", zret);
            return AVERROR_INVALIDDATA;
        }

        if (!(avpkt->flags & AV_PKT_FLAG_KEY))
            for (j = 0; j < avctx->width << 1; j++)
                dst[j] += prev[j] & -!dst[j];

        prev -= prev_pic->linesize[0];
        dst  -= pic->linesize[0];
    }

    av_frame_unref(zc->previous_frame);
    if ((ret = av_frame_ref(zc->previous_frame, pic)) < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int zerocodec_decode_close(AVCodecContext *avctx)
{
    ZeroCodecContext *zc = avctx->priv_data;

    av_frame_free(&zc->previous_frame);

    inflateEnd(&zc->zstream);

    return 0;
}

static av_cold int zerocodec_decode_init(AVCodecContext *avctx)
{
    ZeroCodecContext *zc = avctx->priv_data;
    z_stream *zstream    = &zc->zstream;
    int zret;

    avctx->pix_fmt             = AV_PIX_FMT_UYVY422;
    avctx->bits_per_raw_sample = 8;

    zstream->zalloc = Z_NULL;
    zstream->zfree  = Z_NULL;
    zstream->opaque = Z_NULL;

    zret = inflateInit(zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not initialize inflate: %d.\n", zret);
        return AVERROR(ENOMEM);
    }

    zc->previous_frame = av_frame_alloc();
    if (!zc->previous_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static void zerocodec_decode_flush(AVCodecContext *avctx)
{
    ZeroCodecContext *zc = avctx->priv_data;

    av_frame_unref(zc->previous_frame);
}

AVCodec ff_zerocodec_decoder = {
    .type           = AVMEDIA_TYPE_VIDEO,
    .name           = "zerocodec",
    .long_name      = NULL_IF_CONFIG_SMALL("ZeroCodec Lossless Video"),
    .id             = AV_CODEC_ID_ZEROCODEC,
    .priv_data_size = sizeof(ZeroCodecContext),
    .init           = zerocodec_decode_init,
    .decode         = zerocodec_decode_frame,
    .flush          = zerocodec_decode_flush,
    .close          = zerocodec_decode_close,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};
