/*
 * Copyright (c) 2019 Paul B Mahol
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

#include <stdint.h>
#include <zlib.h>

#include "libavutil/frame.h"
#include "libavutil/error.h"
#include "libavutil/log.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec.h"
#include "codec_internal.h"
#include "internal.h"
#include "packet.h"
#include "png.h"
#include "pngdsp.h"
#include "zlib_wrapper.h"

typedef struct LSCRContext {
    PNGDSPContext   dsp;
    AVCodecContext *avctx;

    AVFrame        *last_picture;
    uint8_t        *buffer;
    int             buffer_size;
    uint8_t        *crow_buf;
    int             crow_size;
    uint8_t        *last_row;
    unsigned int    last_row_size;

    GetByteContext  gb;
    uint8_t        *image_buf;
    int             image_linesize;
    int             row_size;
    int             cur_h;
    int             y;

    FFZStream       zstream;
} LSCRContext;

static void handle_row(LSCRContext *s)
{
    uint8_t *ptr, *last_row;

    ptr = s->image_buf + s->image_linesize * s->y;
    if (s->y == 0)
        last_row = s->last_row;
    else
        last_row = ptr - s->image_linesize;

    ff_png_filter_row(&s->dsp, ptr, s->crow_buf[0], s->crow_buf + 1,
                      last_row, s->row_size, 3);

    s->y++;
}

static int decode_idat(LSCRContext *s, z_stream *zstream, int length)
{
    int ret;
    zstream->avail_in = FFMIN(length, bytestream2_get_bytes_left(&s->gb));
    zstream->next_in  = s->gb.buffer;

    if (length <= 0)
        return AVERROR_INVALIDDATA;

    bytestream2_skip(&s->gb, length);

    /* decode one line if possible */
    while (zstream->avail_in > 0) {
        ret = inflate(zstream, Z_PARTIAL_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            av_log(s->avctx, AV_LOG_ERROR, "inflate returned error %d\n", ret);
            return AVERROR_EXTERNAL;
        }
        if (zstream->avail_out == 0) {
            if (s->y < s->cur_h) {
                handle_row(s);
            }
            zstream->avail_out = s->crow_size;
            zstream->next_out  = s->crow_buf;
        }
        if (ret == Z_STREAM_END && zstream->avail_in > 0) {
            av_log(s->avctx, AV_LOG_WARNING,
                   "%d undecompressed bytes left in buffer\n", zstream->avail_in);
            return 0;
        }
    }
    return 0;
}

static int decode_frame_lscr(AVCodecContext *avctx, AVFrame *rframe,
                             int *got_frame, AVPacket *avpkt)
{
    LSCRContext *const s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    AVFrame *frame = s->last_picture;
    int ret, nb_blocks, offset = 0;

    if (avpkt->size < 2)
        return AVERROR_INVALIDDATA;
    if (avpkt->size == 2)
        return 0;

    bytestream2_init(gb, avpkt->data, avpkt->size);

    nb_blocks = bytestream2_get_le16(gb);
    if (bytestream2_get_bytes_left(gb) < 2 + nb_blocks * (12 + 8))
        return AVERROR_INVALIDDATA;

    ret = ff_reget_buffer(avctx, frame,
                          nb_blocks ? 0 : FF_REGET_BUFFER_FLAG_READONLY);
    if (ret < 0)
        return ret;

    for (int b = 0; b < nb_blocks; b++) {
        z_stream *const zstream = &s->zstream.zstream;
        int x, y, x2, y2, w, h, left;
        uint32_t csize, size;

        if (inflateReset(zstream) != Z_OK)
            return AVERROR_EXTERNAL;

        bytestream2_seek(gb, 2 + b * 12, SEEK_SET);

        x = bytestream2_get_le16(gb);
        y = bytestream2_get_le16(gb);
        x2 = bytestream2_get_le16(gb);
        y2 = bytestream2_get_le16(gb);
        w = x2-x;
        s->cur_h = h = y2-y;

        if (w <= 0 || x < 0 || x >= avctx->width || w + x > avctx->width ||
            h <= 0 || y < 0 || y >= avctx->height || h + y > avctx->height)
            return AVERROR_INVALIDDATA;

        size = bytestream2_get_le32(gb);

        frame->key_frame = (nb_blocks == 1) &&
                           (w == avctx->width) &&
                           (h == avctx->height) &&
                           (x == 0) && (y == 0);

        bytestream2_seek(gb, 2 + nb_blocks * 12 + offset, SEEK_SET);
        csize = bytestream2_get_be32(gb);
        if (bytestream2_get_le32(gb) != MKTAG('I', 'D', 'A', 'T'))
            return AVERROR_INVALIDDATA;

        offset += size;
        left = size;

        s->y                 = 0;
        s->row_size          = w * 3;

        av_fast_padded_malloc(&s->buffer, &s->buffer_size, s->row_size + 16);
        if (!s->buffer)
            return AVERROR(ENOMEM);

        av_fast_padded_malloc(&s->last_row, &s->last_row_size, s->row_size);
        if (!s->last_row)
            return AVERROR(ENOMEM);

        s->crow_size         = w * 3 + 1;
        s->crow_buf          = s->buffer + 15;
        zstream->avail_out   = s->crow_size;
        zstream->next_out    = s->crow_buf;
        s->image_buf         = frame->data[0] + (avctx->height - y - 1) * frame->linesize[0] + x * 3;
        s->image_linesize    =-frame->linesize[0];

        while (left > 16) {
            ret = decode_idat(s, zstream, csize);
            if (ret < 0)
                return ret;
            left -= csize + 16;
            if (left > 16) {
                bytestream2_skip(gb, 4);
                csize = bytestream2_get_be32(gb);
                if (bytestream2_get_le32(gb) != MKTAG('I', 'D', 'A', 'T'))
                    return AVERROR_INVALIDDATA;
            }
        }
    }

    frame->pict_type = frame->key_frame ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    if ((ret = av_frame_ref(rframe, frame)) < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
}

static int lscr_decode_close(AVCodecContext *avctx)
{
    LSCRContext *s = avctx->priv_data;

    av_frame_free(&s->last_picture);
    av_freep(&s->buffer);
    av_freep(&s->last_row);
    ff_inflate_end(&s->zstream);

    return 0;
}

static int lscr_decode_init(AVCodecContext *avctx)
{
    LSCRContext *s = avctx->priv_data;

    avctx->color_range = AVCOL_RANGE_JPEG;
    avctx->pix_fmt     = AV_PIX_FMT_BGR24;

    s->avctx = avctx;
    s->last_picture = av_frame_alloc();
    if (!s->last_picture)
        return AVERROR(ENOMEM);

    ff_pngdsp_init(&s->dsp);

    return ff_inflate_init(&s->zstream, avctx);
}

static void lscr_decode_flush(AVCodecContext *avctx)
{
    LSCRContext *s = avctx->priv_data;
    av_frame_unref(s->last_picture);
}

const FFCodec ff_lscr_decoder = {
    .p.name         = "lscr",
    .p.long_name    = NULL_IF_CONFIG_SMALL("LEAD Screen Capture"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_LSCR,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(LSCRContext),
    .init           = lscr_decode_init,
    .close          = lscr_decode_close,
    FF_CODEC_DECODE_CB(decode_frame_lscr),
    .flush          = lscr_decode_flush,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
