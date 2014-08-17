/*
 * TechSmith Camtasia decoder
 * Copyright (c) 2004 Konstantin Shishkov
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
 * TechSmith Camtasia decoder
 *
 * Fourcc: TSCC
 *
 * Codec is very simple:
 *  it codes picture (picture difference, really)
 *  with algorithm almost identical to Windows RLE8,
 *  only without padding and with greater pixel sizes,
 *  then this coded picture is packed with ZLib
 *
 * Supports: BGR8,BGR555,BGR24 - only BGR8 and BGR555 tested
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "avcodec.h"
#include "internal.h"
#include "msrledec.h"

#include <zlib.h>

typedef struct TsccContext {

    AVCodecContext *avctx;
    AVFrame *frame;

    // Bits per pixel
    int bpp;
    // Decompressed data size
    unsigned int decomp_size;
    // Decompression buffer
    unsigned char* decomp_buf;
    GetByteContext gb;
    int height;
    z_stream zstream;

    uint32_t pal[256];
} CamtasiaContext;

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    CamtasiaContext * const c = avctx->priv_data;
    AVFrame *frame = c->frame;
    int ret;

    if ((ret = ff_reget_buffer(avctx, frame)) < 0)
        return ret;

    ret = inflateReset(&c->zstream);
    if (ret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", ret);
        return AVERROR_UNKNOWN;
    }
    c->zstream.next_in   = buf;
    c->zstream.avail_in  = buf_size;
    c->zstream.next_out = c->decomp_buf;
    c->zstream.avail_out = c->decomp_size;
    ret = inflate(&c->zstream, Z_FINISH);
    // Z_DATA_ERROR means empty picture
    if ((ret != Z_OK) && (ret != Z_STREAM_END) && (ret != Z_DATA_ERROR)) {
        av_log(avctx, AV_LOG_ERROR, "Inflate error: %d\n", ret);
        return AVERROR_UNKNOWN;
    }


    if (ret != Z_DATA_ERROR) {
        bytestream2_init(&c->gb, c->decomp_buf,
                         c->decomp_size - c->zstream.avail_out);
        ff_msrle_decode(avctx, (AVPicture*)frame, c->bpp, &c->gb);
    }

    /* make the palette available on the way out */
    if (c->avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, NULL);

        if (pal) {
            frame->palette_has_changed = 1;
            memcpy(c->pal, pal, AVPALETTE_SIZE);
        }
        memcpy(frame->data[1], c->pal, AVPALETTE_SIZE);
    }

    if ((ret = av_frame_ref(data, frame)) < 0)
        return ret;
    *got_frame      = 1;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    CamtasiaContext * const c = avctx->priv_data;
    int zret; // Zlib return code

    c->avctx = avctx;

    c->height = avctx->height;

    // Needed if zlib unused or init aborted before inflateInit
    memset(&c->zstream, 0, sizeof(z_stream));
    switch(avctx->bits_per_coded_sample){
    case  8: avctx->pix_fmt = AV_PIX_FMT_PAL8; break;
    case 16: avctx->pix_fmt = AV_PIX_FMT_RGB555; break;
    case 24:
             avctx->pix_fmt = AV_PIX_FMT_BGR24;
             break;
    case 32: avctx->pix_fmt = AV_PIX_FMT_RGB32; break;
    default: av_log(avctx, AV_LOG_ERROR, "Camtasia error: unknown depth %i bpp\n", avctx->bits_per_coded_sample);
             return AVERROR_PATCHWELCOME;
    }
    c->bpp = avctx->bits_per_coded_sample;
    // buffer size for RLE 'best' case when 2-byte code precedes each pixel and there may be padding after it too
    c->decomp_size = (((avctx->width * c->bpp + 7) >> 3) + 3 * avctx->width + 2) * avctx->height + 2;

    /* Allocate decompression buffer */
    if (c->decomp_size) {
        if (!(c->decomp_buf = av_malloc(c->decomp_size))) {
            av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
            return AVERROR(ENOMEM);
        }
    }

    c->zstream.zalloc = Z_NULL;
    c->zstream.zfree = Z_NULL;
    c->zstream.opaque = Z_NULL;
    zret = inflateInit(&c->zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
        return AVERROR_UNKNOWN;
    }

    c->frame = av_frame_alloc();

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    CamtasiaContext * const c = avctx->priv_data;

    av_freep(&c->decomp_buf);
    av_frame_free(&c->frame);

    inflateEnd(&c->zstream);

    return 0;
}

AVCodec ff_tscc_decoder = {
    .name           = "camtasia",
    .long_name      = NULL_IF_CONFIG_SMALL("TechSmith Screen Capture Codec"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_TSCC,
    .priv_data_size = sizeof(CamtasiaContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
