/*
 * BRender PIX (.pix) image decoder
 * Copyright (c) 2012 Aleksi Nurmi
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

/*
 * Tested against samples from I-War / Independence War and Defiance.
 * If the PIX file does not contain a palette, the
 * palette_has_changed property of the AVFrame is set to 0.
 */

#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

typedef struct BRPixContext {
    AVFrame frame;
} BRPixContext;

typedef struct BRPixHeader {
    int format;
    unsigned int width, height;
} BRPixHeader;

static av_cold int brpix_init(AVCodecContext *avctx)
{
    BRPixContext *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->frame);
    avctx->coded_frame = &s->frame;

    return 0;
}

static int brpix_decode_header(BRPixHeader *out, GetByteContext *pgb)
{
    unsigned int header_len = bytestream2_get_be32(pgb);

    out->format = bytestream2_get_byte(pgb);
    bytestream2_skip(pgb, 2);
    out->width = bytestream2_get_be16(pgb);
    out->height = bytestream2_get_be16(pgb);

    // the header is at least 11 bytes long; we read the first 7
    if (header_len < 11) {
        return 0;
    }

    // skip the rest of the header
    bytestream2_skip(pgb, header_len-7);

    return 1;
}

static int brpix_decode_frame(AVCodecContext *avctx,
                              void *data, int *got_frame,
                              AVPacket *avpkt)
{
    BRPixContext *s = avctx->priv_data;
    AVFrame *frame_out = data;

    int ret;
    GetByteContext gb;

    unsigned int bytes_pp;

    unsigned int magic[4];
    unsigned int chunk_type;
    unsigned int data_len;
    BRPixHeader hdr;

    bytestream2_init(&gb, avpkt->data, avpkt->size);

    magic[0] = bytestream2_get_be32(&gb);
    magic[1] = bytestream2_get_be32(&gb);
    magic[2] = bytestream2_get_be32(&gb);
    magic[3] = bytestream2_get_be32(&gb);

    if (magic[0] != 0x12 ||
        magic[1] != 0x8 ||
        magic[2] != 0x2 ||
        magic[3] != 0x2) {
        av_log(avctx, AV_LOG_ERROR, "Not a BRender PIX file\n");
        return AVERROR_INVALIDDATA;
    }

    chunk_type = bytestream2_get_be32(&gb);
    if (chunk_type != 0x3 && chunk_type != 0x3d) {
        av_log(avctx, AV_LOG_ERROR, "Invalid chunk type %d\n", chunk_type);
        return AVERROR_INVALIDDATA;
    }

    ret = brpix_decode_header(&hdr, &gb);
    if (!ret) {
        av_log(avctx, AV_LOG_ERROR, "Invalid header length\n");
        return AVERROR_INVALIDDATA;
    }
    switch (hdr.format) {
    case 3:
        avctx->pix_fmt = AV_PIX_FMT_PAL8;
        bytes_pp = 1;
        break;
    case 4:
        avctx->pix_fmt = AV_PIX_FMT_RGB555BE;
        bytes_pp = 2;
        break;
    case 5:
        avctx->pix_fmt = AV_PIX_FMT_RGB565BE;
        bytes_pp = 2;
        break;
    case 6:
        avctx->pix_fmt = AV_PIX_FMT_RGB24;
        bytes_pp = 3;
        break;
    case 7:
        avctx->pix_fmt = AV_PIX_FMT_0RGB;
        bytes_pp = 4;
        break;
    case 18:
        avctx->pix_fmt = AV_PIX_FMT_GRAY8A;
        bytes_pp = 2;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Format %d is not supported\n",
                                    hdr.format);
        return AVERROR_PATCHWELCOME;
    }

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    if (av_image_check_size(hdr.width, hdr.height, 0, avctx) < 0)
        return AVERROR_INVALIDDATA;

    if (hdr.width != avctx->width || hdr.height != avctx->height)
        avcodec_set_dimensions(avctx, hdr.width, hdr.height);

    if ((ret = ff_get_buffer(avctx, &s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    chunk_type = bytestream2_get_be32(&gb);

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8 &&
        (chunk_type == 0x3 || chunk_type == 0x3d)) {
        BRPixHeader palhdr;
        uint32_t *pal_out = (uint32_t *)s->frame.data[1];
        int i;

        ret = brpix_decode_header(&palhdr, &gb);
        if (!ret) {
            av_log(avctx, AV_LOG_ERROR, "Invalid palette header length\n");
            return AVERROR_INVALIDDATA;
        }
        if (palhdr.format != 7) {
            av_log(avctx, AV_LOG_ERROR, "Palette is not in 0RGB format\n");
            return AVERROR_INVALIDDATA;
        }

        chunk_type = bytestream2_get_be32(&gb);
        data_len = bytestream2_get_be32(&gb);
        bytestream2_skip(&gb, 8);
        if (chunk_type != 0x21 || data_len != 1032 ||
            bytestream2_get_bytes_left(&gb) < 1032) {
            av_log(avctx, AV_LOG_ERROR, "Invalid palette data\n");
            return AVERROR_INVALIDDATA;
        }
        // convert 0RGB to machine endian format (ARGB32)
        for (i = 0; i < 256; ++i) {
            bytestream2_skipu(&gb, 1);
            *pal_out++ = (0xFFU << 24) | bytestream2_get_be24u(&gb);
        }
        bytestream2_skip(&gb, 8);

        s->frame.palette_has_changed = 1;

        chunk_type = bytestream2_get_be32(&gb);
    }

    data_len = bytestream2_get_be32(&gb);
    bytestream2_skip(&gb, 8);

    // read the image data to the buffer
    {
        unsigned int bytes_per_scanline = bytes_pp * hdr.width;
        unsigned int bytes_left = bytestream2_get_bytes_left(&gb);

        if (chunk_type != 0x21 || data_len != bytes_left ||
            bytes_left / bytes_per_scanline < hdr.height)
        {
            av_log(avctx, AV_LOG_ERROR, "Invalid image data\n");
            return AVERROR_INVALIDDATA;
        }

        av_image_copy_plane(s->frame.data[0], s->frame.linesize[0],
                            avpkt->data + bytestream2_tell(&gb),
                            bytes_per_scanline,
                            bytes_per_scanline, hdr.height);
    }

    *frame_out = s->frame;
    *got_frame = 1;

    return avpkt->size;
}

static av_cold int brpix_end(AVCodecContext *avctx)
{
    BRPixContext *s = avctx->priv_data;

    if(s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec ff_brender_pix_decoder = {
    .name           = "brender_pix",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_BRENDER_PIX,
    .priv_data_size = sizeof(BRPixContext),
    .init           = brpix_init,
    .close          = brpix_end,
    .decode         = brpix_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("BRender PIX image"),
};
