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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "internal.h"


static const enum AVPixelFormat pixfmt_rgb24[] = {
    AV_PIX_FMT_BGR24, AV_PIX_FMT_0RGB32, AV_PIX_FMT_NONE };

typedef struct EightBpsContext {
    AVCodecContext *avctx;

    unsigned char planes;
    unsigned char planemap[4];

    uint32_t pal[256];
} EightBpsContext;

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    EightBpsContext * const c = avctx->priv_data;
    const unsigned char *encoded = buf;
    unsigned char *pixptr, *pixptr_end;
    unsigned int height = avctx->height; // Real image height
    unsigned int dlen, p, row;
    const unsigned char *lp, *dp, *ep;
    unsigned char count;
    unsigned int px_inc;
    unsigned int planes     = c->planes;
    unsigned char *planemap = c->planemap;
    int ret;

    if (buf_size < planes * height *2)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    ep = encoded + buf_size;

    /* Set data pointer after line lengths */
    dp = encoded + planes * (height << 1);

    px_inc = planes + (avctx->pix_fmt == AV_PIX_FMT_0RGB32);

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
                    if (pixptr_end - pixptr < count * px_inc)
                        break;
                    if (ep - dp < count)
                        return AVERROR_INVALIDDATA;
                    while (count--) {
                        *pixptr = *dp++;
                        pixptr += px_inc;
                    }
                } else {
                    count = 257 - count;
                    if (pixptr_end - pixptr < count * px_inc)
                        break;
                    while (count--) {
                        *pixptr = *dp;
                        pixptr += px_inc;
                    }
                    dp++;
                    dlen -= 2;
                }
            }
        }
    }

    if (avctx->bits_per_coded_sample <= 8) {
        frame->palette_has_changed = ff_copy_palette(c->pal, avpkt, avctx);

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
        avctx->pix_fmt = ff_get_format(avctx, pixfmt_rgb24);
        c->planes      = 3;
        c->planemap[0] = 2; // 1st plane is red
        c->planemap[1] = 1; // 2nd plane is green
        c->planemap[2] = 0; // 3rd plane is blue
        break;
    case 32:
        avctx->pix_fmt = AV_PIX_FMT_RGB32;
        c->planes      = 4;
        /* handle planemap setup later for decoding rgb24 data as rbg32 */
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Error: Unsupported color depth: %u.\n",
               avctx->bits_per_coded_sample);
        return AVERROR_INVALIDDATA;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_RGB32) {
        c->planemap[0] = HAVE_BIGENDIAN ? 1 : 2; // 1st plane is red
        c->planemap[1] = HAVE_BIGENDIAN ? 2 : 1; // 2nd plane is green
        c->planemap[2] = HAVE_BIGENDIAN ? 3 : 0; // 3rd plane is blue
        c->planemap[3] = HAVE_BIGENDIAN ? 0 : 3; // 4th plane is alpha
    }
    return 0;
}

const FFCodec ff_eightbps_decoder = {
    .p.name         = "8bps",
    .p.long_name    = NULL_IF_CONFIG_SMALL("QuickTime 8BPS video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_8BPS,
    .priv_data_size = sizeof(EightBpsContext),
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
