/*
 * QuickDraw (qdrw) codec
 * Copyright (c) 2004 Konstantin Shishkov
 * Copyright (c) 2015 Vittorio Giovara
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
 * Apple QuickDraw codec.
 * https://developer.apple.com/legacy/library/documentation/mac/QuickDraw/QuickDraw-461.html
 */

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

enum QuickdrawOpcodes {
    PACKBITSRECT = 0x0098,
    PACKBITSRGN,

    EOP = 0x00FF,
};

static int parse_palette(AVCodecContext *avctx, GetByteContext *gbc,
                         uint32_t *pal, int colors)
{
    int i;

    for (i = 0; i <= colors; i++) {
        uint8_t r, g, b;
        unsigned int idx = bytestream2_get_be16(gbc); /* color index */
        if (idx > 255) {
            av_log(avctx, AV_LOG_WARNING,
                   "Palette index out of range: %u\n", idx);
            bytestream2_skip(gbc, 6);
            continue;
        }
        r = bytestream2_get_byte(gbc);
        bytestream2_skip(gbc, 1);
        g = bytestream2_get_byte(gbc);
        bytestream2_skip(gbc, 1);
        b = bytestream2_get_byte(gbc);
        bytestream2_skip(gbc, 1);
        pal[idx] = 0xFFU << 24 | r << 16 | g << 8 | b;
    }
    return 0;
}

static int decode_rle(AVCodecContext *avctx, AVFrame *p, GetByteContext *gbc)
{
    int i;
    uint8_t *outdata = p->data[0];

    for (i = 0; i < avctx->height; i++) {
        int size, left, code, pix;
        uint8_t *out = outdata;

        /* size of packed line */
        size = left = bytestream2_get_be16(gbc);
        if (bytestream2_get_bytes_left(gbc) < size)
            return AVERROR_INVALIDDATA;

        /* decode line */
        while (left > 0) {
            code = bytestream2_get_byte(gbc);
            if (code & 0x80 ) { /* run */
                pix = bytestream2_get_byte(gbc);
                memset(out, pix, 257 - code);
                out   += 257 - code;
                left  -= 2;
            } else { /* copy */
                bytestream2_get_buffer(gbc, out, code + 1);
                out   += code + 1;
                left  -= 2 + code;
            }
        }
        outdata += p->linesize[0];
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    AVFrame * const p      = data;
    GetByteContext gbc;
    int colors;
    int w, h, ret;

    bytestream2_init(&gbc, avpkt->data, avpkt->size);

    /* smallest PICT header */
    if (bytestream2_get_bytes_left(&gbc) < 40) {
        av_log(avctx, AV_LOG_ERROR, "Frame is too small %d\n",
               bytestream2_get_bytes_left(&gbc));
        return AVERROR_INVALIDDATA;
    }

    bytestream2_skip(&gbc, 6);
    h = bytestream2_get_be16(&gbc);
    w = bytestream2_get_be16(&gbc);

    ret = ff_set_dimensions(avctx, w, h);
    if (ret < 0)
        return ret;

    /* version 1 is identified by 0x1101
     * it uses byte-aligned opcodes rather than word-aligned */
    if (bytestream2_get_be32(&gbc) != 0x001102FF) {
        avpriv_request_sample(avctx, "QuickDraw version 1");
        return AVERROR_PATCHWELCOME;
    }

    bytestream2_skip(&gbc, 26);

    while (bytestream2_get_bytes_left(&gbc) >= 4) {
        int bppcnt, bpp;
        int opcode = bytestream2_get_be16(&gbc);

        switch(opcode) {
        case PACKBITSRECT:
        case PACKBITSRGN:
            av_log(avctx, AV_LOG_DEBUG, "Parsing Packbit opcode\n");

            bytestream2_skip(&gbc, 30);
            bppcnt = bytestream2_get_be16(&gbc); /* cmpCount */
            bpp    = bytestream2_get_be16(&gbc); /* cmpSize */

            av_log(avctx, AV_LOG_DEBUG, "bppcount %d bpp %d\n", bppcnt, bpp);
            if (bppcnt == 1 && bpp == 8) {
                avctx->pix_fmt = AV_PIX_FMT_PAL8;
            } else {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid pixel format (bppcnt %d bpp %d) in Packbit\n",
                       bppcnt, bpp);
                return AVERROR_INVALIDDATA;
            }

            /* jump to palette */
            bytestream2_skip(&gbc, 18);
            colors = bytestream2_get_be16(&gbc);

            if (colors < 0 || colors > 256) {
                av_log(avctx, AV_LOG_ERROR,
                       "Error color count - %i(0x%X)\n", colors, colors);
                return AVERROR_INVALIDDATA;
            }
            if (bytestream2_get_bytes_left(&gbc) < (colors + 1) * 8) {
                av_log(avctx, AV_LOG_ERROR, "Palette is too small %d\n",
                       bytestream2_get_bytes_left(&gbc));
                return AVERROR_INVALIDDATA;
            }
            if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
                return ret;

            parse_palette(avctx, &gbc, (uint32_t *)p->data[1], colors);
            p->palette_has_changed = 1;

            /* jump to image data */
            bytestream2_skip(&gbc, 18);

            if (opcode == PACKBITSRGN) {
                bytestream2_skip(&gbc, 2 + 8); /* size + rect */
                avpriv_report_missing_feature(avctx, "Packbit mask region");
            }

            ret = decode_rle(avctx, p, &gbc);
            if (ret < 0)
                return ret;
            *got_frame = 1;
            break;
        default:
            av_log(avctx, AV_LOG_TRACE, "Unknown 0x%04X opcode\n", opcode);
            break;
        }
        /* exit the loop when a known pixel block has been found */
        if (*got_frame) {
            int eop, trail;

            /* re-align to a word */
            bytestream2_skip(&gbc, bytestream2_get_bytes_left(&gbc) % 2);

            eop = bytestream2_get_be16(&gbc);
            trail = bytestream2_get_bytes_left(&gbc);
            if (eop != EOP)
                av_log(avctx, AV_LOG_WARNING,
                       "Missing end of picture opcode (found 0x%04X)\n", eop);
            if (trail)
                av_log(avctx, AV_LOG_WARNING, "Got %d trailing bytes\n", trail);
            break;
        }
    }

    if (*got_frame) {
        p->pict_type = AV_PICTURE_TYPE_I;
        p->key_frame = 1;

        return avpkt->size;
    } else {
        av_log(avctx, AV_LOG_ERROR, "Frame contained no usable data\n");

        return AVERROR_INVALIDDATA;
    }
}

AVCodec ff_qdraw_decoder = {
    .name           = "qdraw",
    .long_name      = NULL_IF_CONFIG_SMALL("Apple QuickDraw"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_QDRAW,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
