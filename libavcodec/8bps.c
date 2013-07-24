/*
 * Quicktime Planar RGB (8BPS) Video Decoder
 * Copyright (C) 2003 Roberto Togni
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
 * QT 8BPS Video Decoder by Roberto Togni
 * For more information about the 8BPS format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * Supports: PAL8 (RGB 8bpp, paletted)
 *         : BGR24 (RGB 24bpp) (can also output it as RGB32)
 *         : RGB32 (RGB 32bpp, 4th plane is alpha)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "internal.h"


static const enum AVPixelFormat pixfmt_rgb24[] = {
    AV_PIX_FMT_BGR24, AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE };

typedef struct EightBpsContext {
    AVCodecContext *avctx;

    unsigned char planes;
    unsigned char planemap[4];

    uint32_t pal[256];
} EightBpsContext;

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame, AVPacket *avpkt)
{
    AVFrame *frame = data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    EightBpsContext * const c = avctx->priv_data;
    const unsigned char *encoded = buf;
    unsigned char *pixptr, *pixptr_end;
    unsigned int height = avctx->height; // Real image height
    unsigned int dlen, p, row;
    const unsigned char *lp, *dp, *ep;
    unsigned char count;
    unsigned int planes     = c->planes;
    unsigned char *planemap = c->planemap;
    int ret;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    ep = encoded + buf_size;

    /* Set data pointer after line lengths */
    dp = encoded + planes * (height << 1);

    for (p = 0; p < planes; p++) {
        /* Lines length pointer for this plane */
        lp = encoded + p * (height << 1);

        /* Decode a plane */
        for (row = 0; row < height; row++) {
            pixptr = frame->data[0] + row * frame->linesize[0] + planemap[p];
            pixptr_end = pixptr + frame->linesize[0];
            if (ep - lp < row * 2 + 2)
                return AVERROR_INVALIDDATA;
            dlen = av_be2ne16(*(const unsigned short *)(lp + row * 2));
            /* Decode a row of this plane */
            while (dlen > 0) {
                if (ep - dp <= 1)
                    return AVERROR_INVALIDDATA;
                if ((count = *dp++) <= 127) {
                    count++;
                    dlen -= count + 1;
                    if (pixptr_end - pixptr < count * planes)
                        break;
                    if (ep - dp < count)
                        return AVERROR_INVALIDDATA;
                    while (count--) {
                        *pixptr = *dp++;
                        pixptr += planes;
                    }
                } else {
                    count = 257 - count;
                    if (pixptr_end - pixptr < count * planes)
                        break;
                    while (count--) {
                        *pixptr = *dp;
                        pixptr += planes;
                    }
                    dp++;
                    dlen -= 2;
                }
            }
        }
    }

    if (avctx->bits_per_coded_sample <= 8) {
        const uint8_t *pal = av_packet_get_side_data(avpkt,
                                                     AV_PKT_DATA_PALETTE,
                                                     NULL);
        if (pal) {
            frame->palette_has_changed = 1;
            memcpy(c->pal, pal, AVPALETTE_SIZE);
        }

        memcpy (frame->data[1], c->pal, AVPALETTE_SIZE);
    }

    *got_frame = 1;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    EightBpsContext * const c = avctx->priv_data;

    c->avctx       = avctx;

    switch (avctx->bits_per_coded_sample) {
    case 8:
        avctx->pix_fmt = AV_PIX_FMT_PAL8;
        c->planes      = 1;
        c->planemap[0] = 0; // 1st plane is palette indexes
        break;
    case 24:
        avctx->pix_fmt = avctx->get_format(avctx, pixfmt_rgb24);
        c->planes      = 3;
        c->planemap[0] = 2; // 1st plane is red
        c->planemap[1] = 1; // 2nd plane is green
        c->planemap[2] = 0; // 3rd plane is blue
        break;
    case 32:
        avctx->pix_fmt = AV_PIX_FMT_RGB32;
        c->planes      = 4;
#if HAVE_BIGENDIAN
        c->planemap[0] = 1; // 1st plane is red
        c->planemap[1] = 2; // 2nd plane is green
        c->planemap[2] = 3; // 3rd plane is blue
        c->planemap[3] = 0; // 4th plane is alpha
#else
        c->planemap[0] = 2; // 1st plane is red
        c->planemap[1] = 1; // 2nd plane is green
        c->planemap[2] = 0; // 3rd plane is blue
        c->planemap[3] = 3; // 4th plane is alpha
#endif
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Error: Unsupported color depth: %u.\n",
               avctx->bits_per_coded_sample);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

AVCodec ff_eightbps_decoder = {
    .name           = "8bps",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_8BPS,
    .priv_data_size = sizeof(EightBpsContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("QuickTime 8BPS video"),
};
