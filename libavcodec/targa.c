/*
 * Targa (.tga) image decoder
 * Copyright (c) 2006 Konstantin Shishkov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "targa.h"

typedef struct TargaContext {
    AVFrame picture;
    GetByteContext gb;

    int color_type;
    int compression_type;
} TargaContext;

static int targa_decode_rle(AVCodecContext *avctx, TargaContext *s,
                            uint8_t *dst, int w, int h, int stride, int bpp)
{
    int x, y;
    int depth = (bpp + 1) >> 3;
    int type, count;
    int diff;

    diff = stride - w * depth;
    x = y = 0;
    while (y < h) {
        if (bytestream2_get_bytes_left(&s->gb) <= 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Ran ouf of data before end-of-image\n");
            return AVERROR_INVALIDDATA;
        }
        type  = bytestream2_get_byteu(&s->gb);
        count = (type & 0x7F) + 1;
        type &= 0x80;
        if (x + count > w && x + count + 1 > (h - y) * w) {
            av_log(avctx, AV_LOG_ERROR,
                   "Packet went out of bounds: position (%i,%i) size %i\n",
                   x, y, count);
            return AVERROR_INVALIDDATA;
        }
        if (!type) {
            do {
                int n  = FFMIN(count, w - x);
                bytestream2_get_buffer(&s->gb, dst, n * depth);
                count -= n;
                dst   += n * depth;
                x     += n;
                if (x == w) {
                    x    = 0;
                    y++;
                    dst += diff;
                }
            } while (count > 0);
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
                    y++;
                    dst += diff;
                }
            } while (count > 0);
        }
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    TargaContext * const s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p = &s->picture;
    uint8_t *dst;
    int stride;
    int idlen, compr, y, w, h, bpp, flags;
    int first_clr, colors, csize;

    bytestream2_init(&s->gb, avpkt->data, avpkt->size);

    /* parse image header */
    idlen     = bytestream2_get_byte(&s->gb);
    bytestream2_skip(&s->gb, 1); /* pal */
    compr     = bytestream2_get_byte(&s->gb);
    first_clr = bytestream2_get_le16(&s->gb);
    colors    = bytestream2_get_le16(&s->gb);
    csize     = bytestream2_get_byte(&s->gb);
    bytestream2_skip(&s->gb, 4); /* 2: x, 2: y */
    w         = bytestream2_get_le16(&s->gb);
    h         = bytestream2_get_le16(&s->gb);
    bpp       = bytestream2_get_byte(&s->gb);
    flags     = bytestream2_get_byte(&s->gb);
    // skip identifier if any
    bytestream2_skip(&s->gb, idlen);

    switch(bpp){
    case 8:
        avctx->pix_fmt = ((compr & (~TGA_RLE)) == TGA_BW) ? PIX_FMT_GRAY8 : PIX_FMT_PAL8;
        break;
    case 15:
        avctx->pix_fmt = PIX_FMT_RGB555LE;
        break;
    case 16:
        avctx->pix_fmt = PIX_FMT_RGB555LE;
        break;
    case 24:
        avctx->pix_fmt = PIX_FMT_BGR24;
        break;
    case 32:
        avctx->pix_fmt = PIX_FMT_BGRA;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Bit depth %i is not supported\n", bpp);
        return -1;
    }

    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    if(av_image_check_size(w, h, 0, avctx))
        return -1;
    if(w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    if(flags & 0x20){
        dst = p->data[0];
        stride = p->linesize[0];
    }else{ //image is upside-down
        dst = p->data[0] + p->linesize[0] * (h - 1);
        stride = -p->linesize[0];
    }

    if(colors){
        int pal_size, pal_sample_size;
        if((colors + first_clr) > 256){
            av_log(avctx, AV_LOG_ERROR, "Incorrect palette: %i colors with offset %i\n", colors, first_clr);
            return -1;
        }
        switch (csize) {
        case 24: pal_sample_size = 3; break;
        case 16:
        case 15: pal_sample_size = 2; break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Palette entry size %i bits is not supported\n", csize);
            return -1;
        }
        pal_size = colors * pal_sample_size;
        if(avctx->pix_fmt != PIX_FMT_PAL8)//should not occur but skip palette anyway
            bytestream2_skip(&s->gb, pal_size);
        else{
            int t;
            uint32_t *pal = ((uint32_t *)p->data[1]) + first_clr;

            if (bytestream2_get_bytes_left(&s->gb) < pal_size) {
                av_log(avctx, AV_LOG_ERROR,
                       "Not enough data to read palette\n");
                return AVERROR_INVALIDDATA;
            }
            switch (pal_sample_size) {
            case 3:
                /* RGB24 */
                for (t = 0; t < colors; t++)
                    *pal++ = bytestream2_get_le24u(&s->gb);
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
                    *pal++ = v;
                }
                break;
            }
            p->palette_has_changed = 1;
        }
    }
    if ((compr & (~TGA_RLE)) == TGA_NODATA) {
        memset(p->data[0], 0, p->linesize[0] * h);
    } else {
        if(compr & TGA_RLE){
            int res = targa_decode_rle(avctx, s, dst, w, h, stride, bpp);
            if (res < 0)
                return res;
        } else {
            size_t img_size = w * ((bpp + 1) >> 3);
            if (bytestream2_get_bytes_left(&s->gb) < img_size * h) {
                av_log(avctx, AV_LOG_ERROR,
                       "Not enough data available for image\n");
                return AVERROR_INVALIDDATA;
            }
            for (y = 0; y < h; y++) {
                bytestream2_get_bufferu(&s->gb, dst, img_size);
                dst += stride;
            }
        }
    }

    *picture   = s->picture;
    *data_size = sizeof(AVPicture);

    return avpkt->size;
}

static av_cold int targa_init(AVCodecContext *avctx){
    TargaContext *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame = &s->picture;

    return 0;
}

static av_cold int targa_end(AVCodecContext *avctx){
    TargaContext *s = avctx->priv_data;

    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

AVCodec ff_targa_decoder = {
    .name           = "targa",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_TARGA,
    .priv_data_size = sizeof(TargaContext),
    .init           = targa_init,
    .close          = targa_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Truevision Targa image"),
};
