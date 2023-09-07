/*
 * PDV video format
 *
 * Copyright (c) 2023 Paul B Mahol
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
#include "codec_internal.h"
#include "decode.h"
#include "zlib_wrapper.h"

#include <zlib.h>

typedef struct PDVContext {
    AVFrame  *previous_frame;
    FFZStream zstream;
} PDVContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    PDVContext *s = avctx->priv_data;

    avctx->pix_fmt = AV_PIX_FMT_MONOBLACK;

    s->previous_frame = av_frame_alloc();
    if (!s->previous_frame)
        return AVERROR(ENOMEM);

    return ff_inflate_init(&s->zstream, avctx);
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    PDVContext *s = avctx->priv_data;

    av_frame_free(&s->previous_frame);
    ff_inflate_end(&s->zstream);

    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    PDVContext *s = avctx->priv_data;
    AVFrame *prev_frame = s->previous_frame;
    z_stream *const zstream = &s->zstream.zstream;
    uint8_t *dst, *prev = prev_frame->data[0];
    int ret, zret;

    if (avctx->skip_frame >= AVDISCARD_ALL ||
        (avctx->skip_frame >= AVDISCARD_NONINTRA &&
         !(avpkt->flags & AV_PKT_FLAG_KEY)))
        return avpkt->size;

    zret = inflateReset(zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not reset inflate: %d.\n", zret);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    zstream->next_in  = avpkt->data;
    zstream->avail_in = avpkt->size;

    dst = frame->data[0];
    for (int i = 0; i < avctx->height; i++) {
        zstream->next_out  = dst;
        zstream->avail_out = (avctx->width + 7) >> 3;

        zret = inflate(zstream, Z_SYNC_FLUSH);
        if (zret != Z_OK && zret != Z_STREAM_END) {
            av_log(avctx, AV_LOG_ERROR,
                   "Inflate failed with return code: %d.\n", zret);
            return AVERROR_INVALIDDATA;
        }

        if (prev && !(avpkt->flags & AV_PKT_FLAG_KEY)) {
            for (int j = 0; j < (avctx->width + 7) >> 3; j++)
                dst[j] ^= prev[j];
            prev += prev_frame->linesize[0];
        }

        dst += frame->linesize[0];
    }

    if ((ret = av_frame_replace(s->previous_frame, frame)) < 0)
        return ret;

    if (avpkt->flags & AV_PKT_FLAG_KEY) {
        frame->flags |= AV_FRAME_FLAG_KEY;
        frame->pict_type = AV_PICTURE_TYPE_I;
    } else {
        frame->pict_type = AV_PICTURE_TYPE_P;
    }

    *got_frame       = 1;

    return avpkt->size;
}

static void decode_flush(AVCodecContext *avctx)
{
    PDVContext *s = avctx->priv_data;

    av_frame_unref(s->previous_frame);
}

const FFCodec ff_pdv_decoder = {
    .p.name         = "pdv",
    CODEC_LONG_NAME("PDV (PlayDate Video)"),
    .priv_data_size = sizeof(PDVContext),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PDV,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .init           = decode_init,
    .close          = decode_end,
    .flush          = decode_flush,
    FF_CODEC_DECODE_CB(decode_frame),
};
