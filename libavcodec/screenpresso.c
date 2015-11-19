/*
 * Screenpresso decoder
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
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
 * Screenpresso decoder
 *
 * Fourcc: SPV1
 *
 * Screenpresso simply horizontally flips and then deflates frames,
 * alternating full pictures and deltas. Deltas are related to the currently
 * rebuilt frame (not the reference), and since there is no coordinate system
 * they contain exactly as many pixel as the keyframe.
 *
 * Supports: BGR24
 */

#include <stdint.h>
#include <string.h>
#include <zlib.h>

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "internal.h"

typedef struct ScreenpressoContext {
    AVFrame *current;

    /* zlib interation */
    uint8_t *inflated_buf;
    uLongf inflated_size;
} ScreenpressoContext;

static av_cold int screenpresso_close(AVCodecContext *avctx)
{
    ScreenpressoContext *ctx = avctx->priv_data;

    av_frame_free(&ctx->current);
    av_freep(&ctx->inflated_buf);

    return 0;
}

static av_cold int screenpresso_init(AVCodecContext *avctx)
{
    ScreenpressoContext *ctx = avctx->priv_data;

    /* These needs to be set to estimate uncompressed buffer */
    int ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid image size %dx%d.\n",
               avctx->width, avctx->height);
        return ret;
    }

    /* Allocate current frame */
    ctx->current = av_frame_alloc();
    if (!ctx->current)
        return AVERROR(ENOMEM);

    avctx->pix_fmt = AV_PIX_FMT_BGR24;

    /* Allocate maximum size possible, a full frame */
    ctx->inflated_size = avctx->width * avctx->height * 3;
    ctx->inflated_buf  = av_malloc(ctx->inflated_size);
    if (!ctx->inflated_buf)
        return AVERROR(ENOMEM);

    return 0;
}

static void sum_delta_flipped(uint8_t       *dst, int dst_linesize,
                              const uint8_t *src, int src_linesize,
                              int bytewidth, int height)
{
    int i;
    for (; height > 0; height--) {
        for (i = 0; i < bytewidth; i++)
            dst[i] += src[(height - 1) * src_linesize + i];
        dst += dst_linesize;
    }
}

static int screenpresso_decode_frame(AVCodecContext *avctx, void *data,
                                     int *got_frame, AVPacket *avpkt)
{
    ScreenpressoContext *ctx = avctx->priv_data;
    AVFrame *frame = data;
    uLongf length = ctx->inflated_size;
    int keyframe;
    int ret;

    /* Size check */
    if (avpkt->size < 3) {
        av_log(avctx, AV_LOG_ERROR, "Packet too small (%d)\n", avpkt->size);
        return AVERROR_INVALIDDATA;
    }

    /* Basic sanity check, but not really harmful */
    if ((avpkt->data[0] != 0x73 && avpkt->data[0] != 0x72) ||
        avpkt->data[1] != 8) { // bpp probably
        av_log(avctx, AV_LOG_WARNING, "Unknown header 0x%02X%02X\n",
               avpkt->data[0], avpkt->data[1]);
    }
    keyframe = (avpkt->data[0] == 0x73);

    /* Inflate the frame after the 2 byte header */
    ret = uncompress(ctx->inflated_buf, &length,
                     avpkt->data + 2, avpkt->size - 2);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "Deflate error %d.\n", ret);
        return AVERROR_UNKNOWN;
    }

    ret = ff_reget_buffer(avctx, ctx->current);
    if (ret < 0)
        return ret;

    /* When a keyframe is found, copy it (flipped) */
    if (keyframe)
        av_image_copy_plane(ctx->current->data[0] +
                            ctx->current->linesize[0] * (avctx->height - 1),
                            -1 * ctx->current->linesize[0],
                            ctx->inflated_buf, avctx->width * 3,
                            avctx->width * 3, avctx->height);
    /* Otherwise sum the delta on top of the current frame */
    else
        sum_delta_flipped(ctx->current->data[0], ctx->current->linesize[0],
                          ctx->inflated_buf, avctx->width * 3,
                          avctx->width * 3, avctx->height);

    /* Frame is ready to be output */
    ret = av_frame_ref(frame, ctx->current);
    if (ret < 0)
        return ret;

    /* Usual properties */
    if (keyframe) {
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->key_frame = 1;
    } else {
        frame->pict_type = AV_PICTURE_TYPE_P;
    }
    *got_frame = 1;

    return 0;
}

AVCodec ff_screenpresso_decoder = {
    .name           = "screenpresso",
    .long_name      = NULL_IF_CONFIG_SMALL("Screenpresso"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SCREENPRESSO,
    .init           = screenpresso_init,
    .decode         = screenpresso_decode_frame,
    .close          = screenpresso_close,
    .priv_data_size = sizeof(ScreenpressoContext),
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};
