/*
 * Targa (.tga) image decoder
 * Copyright (c) 2006 Konstantin Shishkov
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

#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "targa.h"

typedef struct TargaContext {
    GetByteContext gb;
} TargaContext;

static uint8_t *advance_line(uint8_t *start, uint8_t *line,
                             int stride, int *y, int h, int interleave)
{
    *y += interleave;

    if (*y < h) {
        return line + interleave * stride;
    } else {
        *y = (*y + 1) & (interleave - 1);
        if (*y && *y < h) {
            return start + *y * stride;
        } else {
            return NULL;
        }
    }
}

static int targa_decode_rle(AVCodecContext *avctx, TargaContext *s,
                            uint8_t *start, int w, int h, int stride,
                            int bpp, int interleave)
{
    int x, y;
    int depth = (bpp + 1) >> 3;
    int type, count;
    uint8_t *line = start;
    uint8_t *dst  = line;

    x = y = count = 0;
    while (dst) {
        if (bytestream2_get_bytes_left(&s->gb) <= 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Ran ouf of data before end-of-image\n");
            return AVERROR_INVALIDDATA;
        }
        type  = bytestream2_get_byteu(&s->gb);
        count = (type & 0x7F) + 1;
        type &= 0x80;
        if (!type) {
            do {
                int n  = FFMIN(count, w - x);
                bytestream2_get_buffer(&s->gb, dst, n * depth);
                count -= n;
                dst   += n * depth;
                x     += n;
                if (x == w) {
                    x    = 0;
                    dst = line = advance_line(start, line, stride, &y, h, interleave);
                }
            } while (dst && count > 0);
        } else {
            uint8_t tmp[4];
            bytestream2_get_buffer(&s->gb, tmp, depth);
            do {
                int n  = FFMIN(count, w - x);
                count -= n;
                x     += n;
                do {
                    memcpy(dst, tmp, depth);
                    dst += depth;
                } while (--n);
                if (x == w) {
                    x    = 0;
                    dst = line = advance_line(start, line, stride, &y, h, interleave);
                }
            } while (dst && count > 0);
        }
    }

    if (count) {
        av_log(avctx, AV_LOG_ERROR, "Packet went out of bounds\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    TargaContext * const s = avctx->priv_data;
    AVFrame * const p = data;
    uint8_t *dst;
    int stride;
    int idlen, pal, compr, y, w, h, bpp, flags, ret;
    int first_clr, colors, csize;
    int interleave;

    bytestream2_init(&s->gb, avpkt->data, avpkt->size);

    /* parse image header */
    idlen     = bytestream2_get_byte(&s->gb);
    pal       = bytestream2_get_byte(&s->gb);
    compr     = bytestream2_get_byte(&s->gb);
    first_clr = bytestream2_get_le16(&s->gb);
    colors    = bytestream2_get_le16(&s->gb);
    csize     = bytestream2_get_byte(&s->gb);
    bytestream2_skip(&s->gb, 4); /* 2: x, 2: y */
    w         = bytestream2_get_le16(&s->gb);
    h         = bytestream2_get_le16(&s->gb);
    bpp       = bytestream2_get_byte(&s->gb);

    if (bytestream2_get_bytes_left(&s->gb) <= idlen) {
        av_log(avctx, AV_LOG_ERROR,
                "Not enough data to read header\n");
        return AVERROR_INVALIDDATA;
    }

    flags     = bytestream2_get_byte(&s->gb);

    if (!pal && (first_clr || colors || csize)) {
        av_log(avctx, AV_LOG_WARNING, "File without colormap has colormap information set.\n");
        // specification says we should ignore those value in this case
        first_clr = colors = csize = 0;
    }

    // skip identifier if any
    bytestream2_skip(&s->gb, idlen);

    switch (bpp) {
    case 8:
        avctx->pix_fmt = ((compr & (~TGA_RLE)) == TGA_BW) ? AV_PIX_FMT_GRAY8 : AV_PIX_FMT_PAL8;
        break;
    case 15:
    case 16:
        avctx->pix_fmt = AV_PIX_FMT_RGB555LE;
        break;
    case 24:
        avctx->pix_fmt = AV_PIX_FMT_BGR24;
        break;
    case 32:
        avctx->pix_fmt = AV_PIX_FMT_BGRA;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Bit depth %i is not supported\n", bpp);
        return AVERROR_INVALIDDATA;
    }

    if (colors && (colors + first_clr) > 256) {
        av_log(avctx, AV_LOG_ERROR, "Incorrect palette: %i colors with offset %i\n", colors, first_clr);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = av_image_check_size(w, h, 0, avctx)) < 0)
        return ret;
    if (w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);
    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;
    p->pict_type = AV_PICTURE_TYPE_I;

    if (flags & TGA_TOPTOBOTTOM) {
        dst = p->data[0];
        stride = p->linesize[0];
    } else { //image is upside-down
        dst = p->data[0] + p->linesize[0] * (h - 1);
        stride = -p->linesize[0];
    }

    interleave = flags & TGA_INTERLEAVE2 ? 2 :
                 flags & TGA_INTERLEAVE4 ? 4 : 1;

    if (colors) {
        int pal_size, pal_sample_size;

        switch (csize) {
        case 32: pal_sample_size = 4; break;
        case 24: pal_sample_size = 3; break;
        case 16:
        case 15: pal_sample_size = 2; break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Palette entry size %i bits is not supported\n", csize);
            return AVERROR_INVALIDDATA;
        }
        pal_size = colors * pal_sample_size;
        if (avctx->pix_fmt != AV_PIX_FMT_PAL8) //should not occur but skip palette anyway
            bytestream2_skip(&s->gb, pal_size);
        else {
            int t;
            uint32_t *pal = ((uint32_t *)p->data[1]) + first_clr;

            if (bytestream2_get_bytes_left(&s->gb) < pal_size) {
                av_log(avctx, AV_LOG_ERROR,
                       "Not enough data to read palette\n");
                return AVERROR_INVALIDDATA;
            }
            switch (pal_sample_size) {
            case 4:
                for (t = 0; t < colors; t++)
                    *pal++ = bytestream2_get_le32u(&s->gb);
                break;
            case 3:
                /* RGB24 */
                for (t = 0; t < colors; t++)
                    *pal++ = (0xffU<<24) | bytestream2_get_le24u(&s->gb);
                break;
            case 2:
                /* RGB555 */
                for (t = 0; t < colors; t++) {
                    uint32_t v = bytestream2_get_le16u(&s->gb);
                    v = ((v & 0x7C00) <<  9) |
                        ((v & 0x03E0) <<  6) |
                        ((v & 0x001F) <<  3);
                    /* left bit replication */
                    v |= (v & 0xE0E0E0U) >> 5;
                    *pal++ = (0xffU<<24) | v;
                }
                break;
            }
            p->palette_has_changed = 1;
        }
    }

    if ((compr & (~TGA_RLE)) == TGA_NODATA) {
        memset(p->data[0], 0, p->linesize[0] * h);
    } else {
        if (compr & TGA_RLE) {
            int res = targa_decode_rle(avctx, s, dst, w, h, stride, bpp, interleave);
            if (res < 0)
                return res;
        } else {
            size_t img_size = w * ((bpp + 1) >> 3);
            uint8_t *line;
            if (bytestream2_get_bytes_left(&s->gb) < img_size * h) {
                av_log(avctx, AV_LOG_ERROR,
                       "Not enough data available for image\n");
                return AVERROR_INVALIDDATA;
            }

            line = dst;
            y = 0;
            do {
                bytestream2_get_buffer(&s->gb, line, img_size);
                line = advance_line(dst, line, stride, &y, h, interleave);
            } while (line);
        }
    }

    if (flags & TGA_RIGHTTOLEFT) { // right-to-left, needs horizontal flip
        int x;
        for (y = 0; y < h; y++) {
            void *line = &p->data[0][y * p->linesize[0]];
            for (x = 0; x < w >> 1; x++) {
                switch (bpp) {
                case 32:
                    FFSWAP(uint32_t, ((uint32_t *)line)[x], ((uint32_t *)line)[w - x - 1]);
                    break;
                case 24:
                    FFSWAP(uint8_t, ((uint8_t *)line)[3 * x    ], ((uint8_t *)line)[3 * w - 3 * x - 3]);
                    FFSWAP(uint8_t, ((uint8_t *)line)[3 * x + 1], ((uint8_t *)line)[3 * w - 3 * x - 2]);
                    FFSWAP(uint8_t, ((uint8_t *)line)[3 * x + 2], ((uint8_t *)line)[3 * w - 3 * x - 1]);
                    break;
                case 16:
                    FFSWAP(uint16_t, ((uint16_t *)line)[x], ((uint16_t *)line)[w - x - 1]);
                    break;
                case 8:
                    FFSWAP(uint8_t, ((uint8_t *)line)[x], ((uint8_t *)line)[w - x - 1]);
                }
            }
        }
    }

    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_targa_decoder = {
    .name           = "targa",
    .long_name      = NULL_IF_CONFIG_SMALL("Truevision Targa image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_TARGA,
    .priv_data_size = sizeof(TargaContext),
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
