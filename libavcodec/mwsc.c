/*
 * MatchWare Screen Capture Codec decoder
 *
 * Copyright (c) 2018 Paul B Mahol
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

#include <zlib.h>

typedef struct MWSCContext {
    unsigned int      decomp_size;
    uint8_t          *decomp_buf;
    z_stream          zstream;
    AVFrame          *prev_frame;
} MWSCContext;

static int rle_uncompress(GetByteContext *gb, PutByteContext *pb, GetByteContext *gbp,
                          int width, int height, int stride, int pb_linesize, int gbp_linesize)
{
    int intra = 1, w = 0;

    bytestream2_seek_p(pb, (height - 1) * pb_linesize, SEEK_SET);

    while (bytestream2_get_bytes_left(gb) > 0) {
        uint32_t fill = bytestream2_get_le24(gb);
        unsigned run = bytestream2_get_byte(gb);

        if (run == 0) {
            run = bytestream2_get_le32(gb);
            for (int j = 0; j < run; j++, w++) {
                if (w == width) {
                    w = 0;
                    bytestream2_seek_p(pb, -(pb_linesize + stride), SEEK_CUR);
                }
                bytestream2_put_le24(pb, fill);
            }
        } else if (run == 255) {
            int pos = bytestream2_tell_p(pb);

            bytestream2_seek(gbp, pos, SEEK_SET);
            for (int j = 0; j < fill; j++, w++) {
                if (w == width) {
                    w = 0;
                    bytestream2_seek_p(pb, -(pb_linesize + stride), SEEK_CUR);
                    bytestream2_seek(gbp, -(gbp_linesize + stride), SEEK_CUR);
                }
                bytestream2_put_le24(pb, bytestream2_get_le24(gbp));
            }

            intra = 0;
        } else {
            for (int j = 0; j < run; j++, w++) {
                if (w == width) {
                    w = 0;
                    bytestream2_seek_p(pb, -(pb_linesize + stride), SEEK_CUR);
                }
                bytestream2_put_le24(pb, fill);
            }
        }
    }

    return intra;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    MWSCContext *s = avctx->priv_data;
    AVFrame *frame = data;
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    GetByteContext gb;
    GetByteContext gbp;
    PutByteContext pb;
    int ret;

    ret = inflateReset(&s->zstream);
    if (ret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", ret);
        return AVERROR_EXTERNAL;
    }
    s->zstream.next_in   = buf;
    s->zstream.avail_in  = buf_size;
    s->zstream.next_out  = s->decomp_buf;
    s->zstream.avail_out = s->decomp_size;
    ret = inflate(&s->zstream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        av_log(avctx, AV_LOG_ERROR, "Inflate error: %d\n", ret);
        return AVERROR_EXTERNAL;
    }

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    bytestream2_init(&gb, s->decomp_buf, s->zstream.total_out);
    bytestream2_init(&gbp, s->prev_frame->data[0], avctx->height * s->prev_frame->linesize[0]);
    bytestream2_init_writer(&pb, frame->data[0], avctx->height * frame->linesize[0]);

    frame->key_frame = rle_uncompress(&gb, &pb, &gbp, avctx->width, avctx->height, avctx->width * 3,
                                      frame->linesize[0], s->prev_frame->linesize[0]);

    frame->pict_type = frame->key_frame ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    av_frame_unref(s->prev_frame);
    if ((ret = av_frame_ref(s->prev_frame, frame)) < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    MWSCContext *s = avctx->priv_data;
    int64_t size;
    int zret;

    avctx->pix_fmt = AV_PIX_FMT_BGR24;

    size = 32LL * avctx->height * avctx->width;
    if (size >= INT32_MAX)
        return AVERROR_INVALIDDATA;
    s->decomp_size = size;
    if (!(s->decomp_buf = av_malloc(s->decomp_size)))
        return AVERROR(ENOMEM);

    s->zstream.zalloc = Z_NULL;
    s->zstream.zfree = Z_NULL;
    s->zstream.opaque = Z_NULL;
    zret = inflateInit(&s->zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
        return AVERROR_EXTERNAL;
    }

    s->prev_frame = av_frame_alloc();
    if (!s->prev_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    MWSCContext *s = avctx->priv_data;

    av_frame_free(&s->prev_frame);
    av_freep(&s->decomp_buf);
    s->decomp_size = 0;
    inflateEnd(&s->zstream);

    return 0;
}

AVCodec ff_mwsc_decoder = {
    .name             = "mwsc",
    .long_name        = NULL_IF_CONFIG_SMALL("MatchWare Screen Capture Codec"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_MWSC,
    .priv_data_size   = sizeof(MWSCContext),
    .init             = decode_init,
    .close            = decode_close,
    .decode           = decode_frame,
    .capabilities     = AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE |
                        FF_CODEC_CAP_INIT_CLEANUP,
};
