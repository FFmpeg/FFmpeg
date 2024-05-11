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

#include "libavutil/mem.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "zlib_wrapper.h"

#include <zlib.h>

typedef struct MWSCContext {
    unsigned int      decomp_size;
    uint8_t          *decomp_buf;
    AVFrame          *prev_frame;
    FFZStream         zstream;
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

            if (bytestream2_tell_p(pb) + width - w < run)
                return AVERROR_INVALIDDATA;

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

            if (pos + width - w < fill)
                return AVERROR_INVALIDDATA;

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
            if (bytestream2_tell_p(pb) + width - w < run)
                return AVERROR_INVALIDDATA;

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

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    MWSCContext *s = avctx->priv_data;
    z_stream *const zstream = &s->zstream.zstream;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    GetByteContext gb;
    GetByteContext gbp;
    PutByteContext pb;
    int ret;

    ret = inflateReset(zstream);
    if (ret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", ret);
        return AVERROR_EXTERNAL;
    }
    zstream->next_in   = buf;
    zstream->avail_in  = buf_size;
    zstream->next_out  = s->decomp_buf;
    zstream->avail_out = s->decomp_size;
    ret = inflate(zstream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        av_log(avctx, AV_LOG_ERROR, "Inflate error: %d\n", ret);
        return AVERROR_EXTERNAL;
    }

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    bytestream2_init(&gb, s->decomp_buf, zstream->total_out);
    bytestream2_init(&gbp, s->prev_frame->data[0], avctx->height * s->prev_frame->linesize[0]);
    bytestream2_init_writer(&pb, frame->data[0], avctx->height * frame->linesize[0]);

    if (rle_uncompress(&gb, &pb, &gbp, avctx->width, avctx->height, avctx->width * 3,
                       frame->linesize[0], s->prev_frame->linesize[0]))
        frame->flags |= AV_FRAME_FLAG_KEY;
    else
        frame->flags &= ~AV_FRAME_FLAG_KEY;

    frame->pict_type = (frame->flags & AV_FRAME_FLAG_KEY) ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    if ((ret = av_frame_replace(s->prev_frame, frame)) < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    MWSCContext *s = avctx->priv_data;
    int64_t size;

    avctx->pix_fmt = AV_PIX_FMT_BGR24;

    size = 32LL * avctx->height * avctx->width;
    if (size >= INT32_MAX)
        return AVERROR_INVALIDDATA;
    s->decomp_size = size;
    if (!(s->decomp_buf = av_malloc(s->decomp_size)))
        return AVERROR(ENOMEM);

    s->prev_frame = av_frame_alloc();
    if (!s->prev_frame)
        return AVERROR(ENOMEM);

    return ff_inflate_init(&s->zstream, avctx);
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    MWSCContext *s = avctx->priv_data;

    av_frame_free(&s->prev_frame);
    av_freep(&s->decomp_buf);
    s->decomp_size = 0;
    ff_inflate_end(&s->zstream);

    return 0;
}

const FFCodec ff_mwsc_decoder = {
    .p.name           = "mwsc",
    CODEC_LONG_NAME("MatchWare Screen Capture Codec"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_MWSC,
    .priv_data_size   = sizeof(MWSCContext),
    .init             = decode_init,
    .close            = decode_close,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
};
