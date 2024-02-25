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
 *         : GBRP (RGB 24bpp)
 *         : GBRAP (RGB 32bpp, 4th plane is alpha)
 */

#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/internal.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"

typedef struct EightBpsContext {
    AVCodecContext *avctx;

    uint8_t planes;
    uint8_t planemap[4];

    uint32_t pal[256];
} EightBpsContext;

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    EightBpsContext * const c = avctx->priv_data;
    const uint8_t *encoded = buf;
    uint8_t *pixptr, *pixptr_end;
    unsigned int height = avctx->height; // Real image height
    unsigned int dlen, p, row;
    const uint8_t *lp, *dp, *ep;
    uint8_t count;
    const uint8_t *planemap = c->planemap;
    unsigned int planes = c->planes;
    int ret;

    if (buf_size < planes * height * (2 + 2*((avctx->width+128)/129)))
        return AVERROR_INVALIDDATA;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    ep = encoded + buf_size;

    /* Set data pointer after line lengths */
    dp = encoded + planes * (height << 1);

    for (p = 0; p < planes; p++) {
        const int pi = planemap[p];
        /* Lines length pointer for this plane */
        lp = encoded + p * (height << 1);

        /* Decode a plane */
        for (row = 0; row < height; row++) {
            pixptr = frame->data[pi] + row * frame->linesize[pi];
            pixptr_end = pixptr + frame->linesize[pi];
            if (ep - lp < row * 2 + 2)
                return AVERROR_INVALIDDATA;
            dlen = AV_RB16(lp + row * 2);
            /* Decode a row of this plane */
            while (dlen > 0) {
                if (ep - dp <= 1)
                    return AVERROR_INVALIDDATA;
                if ((count = *dp++) <= 127) {
                    count++;
                    dlen -= count + 1;
                    if (pixptr_end - pixptr < count)
                        break;
                    if (ep - dp < count)
                        return AVERROR_INVALIDDATA;
                    memcpy(pixptr, dp, count);
                    pixptr += count;
                    dp += count;
                } else {
                    count = 257 - count;
                    if (pixptr_end - pixptr < count)
                        break;
                    memset(pixptr, dp[0], count);
                    pixptr += count;
                    dp++;
                    dlen -= 2;
                }
            }
        }
    }

    if (avctx->bits_per_coded_sample <= 8) {
#if FF_API_PALETTE_HAS_CHANGED
FF_DISABLE_DEPRECATION_WARNINGS
        frame->palette_has_changed =
#endif
        ff_copy_palette(c->pal, avpkt, avctx);
#if FF_API_PALETTE_HAS_CHANGED
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        memcpy(frame->data[1], c->pal, AVPALETTE_SIZE);
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
        avctx->pix_fmt = AV_PIX_FMT_GBRP;
        c->planes      = 3;
        c->planemap[0] = 2; // 1st plane is red
        c->planemap[1] = 0; // 2nd plane is green
        c->planemap[2] = 1; // 3rd plane is blue
        break;
    case 32:
        avctx->pix_fmt = AV_PIX_FMT_GBRAP;
        c->planes      = 4;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Error: Unsupported color depth: %u.\n",
               avctx->bits_per_coded_sample);
        return AVERROR_INVALIDDATA;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_GBRAP) {
        c->planemap[0] = 2; // 1st plane is red
        c->planemap[1] = 0; // 2nd plane is green
        c->planemap[2] = 1; // 3rd plane is blue
        c->planemap[3] = 3; // 4th plane is alpha
    }
    return 0;
}

const FFCodec ff_eightbps_decoder = {
    .p.name         = "8bps",
    CODEC_LONG_NAME("QuickTime 8BPS video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_8BPS,
    .priv_data_size = sizeof(EightBpsContext),
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
