/*
 * Mandsoft Screen Capture Codec decoder
 *
 * Copyright (c) 2017 Paul B Mahol
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

typedef struct MSCCContext {
    unsigned          bpp;
    unsigned int      decomp_size;
    uint8_t          *decomp_buf;
    unsigned int      uncomp_size;
    uint8_t          *uncomp_buf;
    z_stream          zstream;

    uint32_t          pal[256];
} MSCCContext;

static int rle_uncompress(AVCodecContext *avctx, GetByteContext *gb, PutByteContext *pb)
{
    MSCCContext *s = avctx->priv_data;
    unsigned x = 0, y = 0;

    while (bytestream2_get_bytes_left(gb) > 0) {
        uint32_t fill;
        int j;
        unsigned run = bytestream2_get_byte(gb);

        if (run) {
            switch (avctx->bits_per_coded_sample) {
            case 8:
                fill = bytestream2_get_byte(gb);
                break;
            case 16:
                fill = bytestream2_get_le16(gb);
                break;
            case 24:
                fill = bytestream2_get_le24(gb);
                break;
            case 32:
                fill = bytestream2_get_le32(gb);
                break;
            }

            for (j = 0; j < run; j++) {
                switch (avctx->bits_per_coded_sample) {
                case 8:
                    bytestream2_put_byte(pb, fill);
                    break;
                case 16:
                    bytestream2_put_le16(pb, fill);
                    break;
                case 24:
                    bytestream2_put_le24(pb, fill);
                    break;
                case 32:
                    bytestream2_put_le32(pb, fill);
                    break;
                }
            }
            x += run;
        } else {
            unsigned copy = bytestream2_get_byte(gb);

            if (copy == 0) {
                x = 0;
                y++;
                bytestream2_seek_p(pb, y * avctx->width * s->bpp, SEEK_SET);
            } else if (copy == 1) {
                return 0;
            } else if (copy == 2) {

                x += bytestream2_get_byte(gb);
                y += bytestream2_get_byte(gb);

                bytestream2_seek_p(pb, y * avctx->width * s->bpp + x * s->bpp, SEEK_SET);
            } else {
                for (j = 0; j < copy; j++) {
                    switch (avctx->bits_per_coded_sample) {
                    case 8:
                        bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                        break;
                    case 16:
                        bytestream2_put_le16(pb, bytestream2_get_le16(gb));
                        break;
                    case 24:
                        bytestream2_put_le24(pb, bytestream2_get_le24(gb));
                        break;
                    case 32:
                        bytestream2_put_le32(pb, bytestream2_get_le32(gb));
                        break;
                    }
                }

                if (s->bpp == 1 && (copy & 1))
                    bytestream2_skip(gb, 1);
                x += copy;
            }
        }
    }

    return AVERROR_INVALIDDATA;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    MSCCContext *s = avctx->priv_data;
    AVFrame *frame = data;
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    GetByteContext gb;
    PutByteContext pb;
    int ret, j;

    if (avpkt->size < 3)
        return buf_size;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    if (avctx->codec_id == AV_CODEC_ID_MSCC) {
        avpkt->data[2] ^= avpkt->data[0];
        buf += 2;
        buf_size -= 2;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        size_t size;
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, &size);

        if (pal && size == AVPALETTE_SIZE) {
            frame->palette_has_changed = 1;
            for (j = 0; j < 256; j++)
                s->pal[j] = 0xFF000000 | AV_RL32(pal + j * 4);
        } else if (pal) {
            av_log(avctx, AV_LOG_ERROR,
                   "Palette size %"SIZE_SPECIFIER" is wrong\n", size);
        }
        memcpy(frame->data[1], s->pal, AVPALETTE_SIZE);
    }

    ret = inflateReset(&s->zstream);
    if (ret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", ret);
        return AVERROR_UNKNOWN;
    }
    s->zstream.next_in   = buf;
    s->zstream.avail_in  = buf_size;
    s->zstream.next_out  = s->decomp_buf;
    s->zstream.avail_out = s->decomp_size;
    ret = inflate(&s->zstream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        av_log(avctx, AV_LOG_ERROR, "Inflate error: %d\n", ret);
        return AVERROR_UNKNOWN;
    }

    bytestream2_init(&gb, s->decomp_buf, s->zstream.total_out);
    bytestream2_init_writer(&pb, s->uncomp_buf, s->uncomp_size);

    ret = rle_uncompress(avctx, &gb, &pb);
    if (ret)
        return ret;

    for (j = 0; j < avctx->height; j++) {
        memcpy(frame->data[0] + (avctx->height - j - 1) * frame->linesize[0],
               s->uncomp_buf + s->bpp * j * avctx->width, s->bpp * avctx->width);
    }

    frame->key_frame = 1;
    frame->pict_type = AV_PICTURE_TYPE_I;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    MSCCContext *s = avctx->priv_data;
    int stride, zret;

    switch (avctx->bits_per_coded_sample) {
    case  8: avctx->pix_fmt = AV_PIX_FMT_PAL8;   break;
    case 16: avctx->pix_fmt = AV_PIX_FMT_RGB555; break;
    case 24: avctx->pix_fmt = AV_PIX_FMT_BGR24;  break;
    case 32: avctx->pix_fmt = AV_PIX_FMT_BGRA;   break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported bitdepth %i\n", avctx->bits_per_coded_sample);
        return AVERROR_INVALIDDATA;
    }

    s->bpp = avctx->bits_per_coded_sample >> 3;
    stride = 4 * ((avctx->width * avctx->bits_per_coded_sample + 31) / 32);

    s->decomp_size = 2 * avctx->height * stride;
    if (!(s->decomp_buf = av_malloc(s->decomp_size)))
        return AVERROR(ENOMEM);

    s->uncomp_size = avctx->height * stride;
    if (!(s->uncomp_buf = av_malloc(s->uncomp_size)))
        return AVERROR(ENOMEM);

    s->zstream.zalloc = Z_NULL;
    s->zstream.zfree = Z_NULL;
    s->zstream.opaque = Z_NULL;
    zret = inflateInit(&s->zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
        return AVERROR_UNKNOWN;
    }

    return 0;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    MSCCContext *s = avctx->priv_data;

    av_freep(&s->decomp_buf);
    s->decomp_size = 0;
    av_freep(&s->uncomp_buf);
    s->uncomp_size = 0;
    inflateEnd(&s->zstream);

    return 0;
}

const AVCodec ff_mscc_decoder = {
    .name             = "mscc",
    .long_name        = NULL_IF_CONFIG_SMALL("Mandsoft Screen Capture Codec"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_MSCC,
    .priv_data_size   = sizeof(MSCCContext),
    .init             = decode_init,
    .close            = decode_close,
    .decode           = decode_frame,
    .capabilities     = AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};

const AVCodec ff_srgc_decoder = {
    .name             = "srgc",
    .long_name        = NULL_IF_CONFIG_SMALL("Screen Recorder Gold Codec"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_SRGC,
    .priv_data_size   = sizeof(MSCCContext),
    .init             = decode_init,
    .close            = decode_close,
    .decode           = decode_frame,
    .capabilities     = AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
