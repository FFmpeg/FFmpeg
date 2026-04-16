/*
 * PDV video encoder
 *
 * Copyright (c) 2026 Priyanshu Thapliyal <priyanshuthapliyal2005@gmail.com>
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

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "zlib_wrapper.h"

#include <limits.h>
#include <zlib.h>

typedef struct PDVEncContext {
    FFZStream zstream;
    uint8_t *previous_frame;
    uint8_t *work_frame;
    int row_size;
    int frame_size;
    int frame_number;
    int last_keyframe;
} PDVEncContext;

static av_cold int encode_init(AVCodecContext *avctx)
{
    PDVEncContext *s = avctx->priv_data;
    int ret;

    ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);
    if (ret < 0)
        return ret;

    s->row_size   = (avctx->width + 7) >> 3;

    s->frame_size = s->row_size * avctx->height;

    s->previous_frame = av_malloc(s->frame_size);
    s->work_frame     = av_malloc(s->frame_size);
    if (!s->previous_frame || !s->work_frame)
        goto fail;

    avctx->bits_per_coded_sample = 1;

    ret = ff_deflate_init(&s->zstream,
                          avctx->compression_level == FF_COMPRESSION_DEFAULT ?
                          Z_DEFAULT_COMPRESSION :
                          av_clip(avctx->compression_level, 0, 9),
                          avctx);
    if (ret < 0)
        goto fail;

    return 0;

fail:
    av_freep(&s->work_frame);
    av_freep(&s->previous_frame);
    return ret < 0 ? ret : AVERROR(ENOMEM);
}

static av_cold int encode_end(AVCodecContext *avctx)
{
    PDVEncContext *s = avctx->priv_data;

    av_freep(&s->previous_frame);
    av_freep(&s->work_frame);
    ff_deflate_end(&s->zstream);

    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *frame, int *got_packet)
{
    PDVEncContext *s = avctx->priv_data;
    z_stream *const zstream = &s->zstream.zstream;
    uint8_t *prev = s->previous_frame;
    uint8_t *curr = s->work_frame;
    uint8_t *payload;
    const int keyframe = s->frame_number == 0 || avctx->gop_size <= 1 ||
                         s->frame_number - s->last_keyframe >= avctx->gop_size;
    int ret;
    int zret;

    if (s->frame_number == INT_MAX) {
        av_log(avctx, AV_LOG_ERROR, "Frame counter reached INT_MAX.\n");
        return AVERROR(EINVAL);
    }

    {
        const uint8_t *src = frame->data[0];
        const ptrdiff_t src_linesize = frame->linesize[0];

        for (int y = 0; y < avctx->height; y++) {
            memcpy(curr + y * s->row_size, src, s->row_size);
            src += src_linesize;
        }
    }

    ret = ff_get_encode_buffer(avctx, pkt, deflateBound(zstream, s->frame_size), 0);
    if (ret < 0)
        return ret;

    if (keyframe) {
        payload = curr;
    } else {
        for (int i = 0; i < s->frame_size; i++)
            prev[i] ^= curr[i];
        payload = prev;
    }

    zret = deflateReset(zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not reset deflate: %d.\n", zret);
        return AVERROR_EXTERNAL;
    }

    zstream->next_in   = payload;
    zstream->avail_in  = s->frame_size;
    zstream->next_out  = pkt->data;
    zstream->avail_out = pkt->size;

    zret = deflate(zstream, Z_FINISH);
    if (zret != Z_STREAM_END) {
        av_log(avctx, AV_LOG_ERROR, "Deflate failed with return code: %d.\n", zret);
        return AVERROR_EXTERNAL;
    }

    pkt->size = zstream->total_out;
    if (keyframe) {
        pkt->flags |= AV_PKT_FLAG_KEY;
        s->last_keyframe = s->frame_number;
    }

    FFSWAP(uint8_t*, s->previous_frame, s->work_frame);
    s->frame_number++;
    *got_packet = 1;

    return 0;
}

const FFCodec ff_pdv_encoder = {
    .p.name         = "pdv",
    CODEC_LONG_NAME("PDV (PlayDate Video)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PDV,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE |
                      AV_CODEC_CAP_EXPERIMENTAL,
    .priv_data_size = sizeof(PDVEncContext),
    .init           = encode_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    .close          = encode_end,
    CODEC_PIXFMTS(AV_PIX_FMT_MONOBLACK),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
