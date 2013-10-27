/*
 * Pictor/PC Paint decoder
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
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

/**
 * @file
 * Pictor/PC Paint decoder
 */

#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "cga_data.h"
#include "internal.h"

typedef struct PicContext {
    int width, height;
    int nb_planes;
    GetByteContext g;
} PicContext;

static void picmemset_8bpp(PicContext *s, AVFrame *frame, int value, int run,
                           int *x, int *y)
{
    while (run > 0) {
        uint8_t *d = frame->data[0] + *y * frame->linesize[0];
        if (*x + run >= s->width) {
            int n = s->width - *x;
            memset(d + *x, value, n);
            run -= n;
            *x = 0;
            *y -= 1;
            if (*y < 0)
                break;
        } else {
            memset(d + *x, value, run);
            *x += run;
            break;
        }
    }
}

static void picmemset(PicContext *s, AVFrame *frame, int value, int run,
                      int *x, int *y, int *plane, int bits_per_plane)
{
    uint8_t *d;
    int shift = *plane * bits_per_plane;
    int mask  = ((1 << bits_per_plane) - 1) << shift;
    value   <<= shift;

    while (run > 0) {
        int j;
        for (j = 8-bits_per_plane; j >= 0; j -= bits_per_plane) {
            d = frame->data[0] + *y * frame->linesize[0];
            d[*x] |= (value >> j) & mask;
            *x += 1;
            if (*x == s->width) {
                *y -= 1;
                *x = 0;
                if (*y < 0) {
                   *y = s->height - 1;
                   *plane += 1;
                   value <<= bits_per_plane;
                   mask  <<= bits_per_plane;
                   if (*plane >= s->nb_planes)
                       break;
                }
            }
        }
        run--;
    }
}

static const uint8_t cga_mode45_index[6][4] = {
    [0] = { 0, 3,  5,   7 }, // mode4, palette#1, low intensity
    [1] = { 0, 2,  4,   6 }, // mode4, palette#2, low intensity
    [2] = { 0, 3,  4,   7 }, // mode5, low intensity
    [3] = { 0, 11, 13, 15 }, // mode4, palette#1, high intensity
    [4] = { 0, 10, 12, 14 }, // mode4, palette#2, high intensity
    [5] = { 0, 11, 12, 15 }, // mode5, high intensity
};

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    PicContext *s = avctx->priv_data;
    AVFrame *frame = data;
    uint32_t *palette;
    int bits_per_plane, bpp, etype, esize, npal, pos_after_pal;
    int i, x, y, plane, tmp, ret;

    bytestream2_init(&s->g, avpkt->data, avpkt->size);

    if (bytestream2_get_bytes_left(&s->g) < 11)
        return AVERROR_INVALIDDATA;

    if (bytestream2_get_le16u(&s->g) != 0x1234)
        return AVERROR_INVALIDDATA;

    s->width       = bytestream2_get_le16u(&s->g);
    s->height      = bytestream2_get_le16u(&s->g);
    bytestream2_skip(&s->g, 4);
    tmp            = bytestream2_get_byteu(&s->g);
    bits_per_plane = tmp & 0xF;
    s->nb_planes   = (tmp >> 4) + 1;
    bpp            = bits_per_plane * s->nb_planes;
    if (bits_per_plane > 8 || bpp < 1 || bpp > 32) {
        avpriv_request_sample(avctx, "Unsupported bit depth");
        return AVERROR_PATCHWELCOME;
    }

    if (bytestream2_peek_byte(&s->g) == 0xFF) {
        bytestream2_skip(&s->g, 2);
        etype = bytestream2_get_le16(&s->g);
        esize = bytestream2_get_le16(&s->g);
        if (bytestream2_get_bytes_left(&s->g) < esize)
            return AVERROR_INVALIDDATA;
    } else {
        etype = -1;
        esize = 0;
    }

    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    if (s->width != avctx->width && s->height != avctx->height) {
        ret = ff_set_dimensions(avctx, s->width, s->height);
        if (ret < 0)
            return ret;
    }

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    memset(frame->data[0], 0, s->height * frame->linesize[0]);
    frame->pict_type           = AV_PICTURE_TYPE_I;
    frame->palette_has_changed = 1;

    pos_after_pal = bytestream2_tell(&s->g) + esize;
    palette = (uint32_t*)frame->data[1];
    if (etype == 1 && esize > 1 && bytestream2_peek_byte(&s->g) < 6) {
        int idx = bytestream2_get_byte(&s->g);
        npal = 4;
        for (i = 0; i < npal; i++)
            palette[i] = ff_cga_palette[ cga_mode45_index[idx][i] ];
    } else if (etype == 2) {
        npal = FFMIN(esize, 16);
        for (i = 0; i < npal; i++) {
            int pal_idx = bytestream2_get_byte(&s->g);
            palette[i]  = ff_cga_palette[FFMIN(pal_idx, 16)];
        }
    } else if (etype == 3) {
        npal = FFMIN(esize, 16);
        for (i = 0; i < npal; i++) {
            int pal_idx = bytestream2_get_byte(&s->g);
            palette[i]  = ff_ega_palette[FFMIN(pal_idx, 63)];
        }
    } else if (etype == 4 || etype == 5) {
        npal = FFMIN(esize / 3, 256);
        for (i = 0; i < npal; i++)
            palette[i] = bytestream2_get_be24(&s->g) << 2;
    } else {
        if (bpp == 1) {
            npal = 2;
            palette[0] = 0x000000;
            palette[1] = 0xFFFFFF;
        } else if (bpp == 2) {
            npal = 4;
            for (i = 0; i < npal; i++)
                palette[i] = ff_cga_palette[ cga_mode45_index[0][i] ];
        } else {
            npal = 16;
            memcpy(palette, ff_cga_palette, npal * 4);
        }
    }
    // fill remaining palette entries
    memset(palette + npal, 0, AVPALETTE_SIZE - npal * 4);
    // skip remaining palette bytes
    bytestream2_seek(&s->g, pos_after_pal, SEEK_SET);

    x = 0;
    y = s->height - 1;
    plane = 0;
    if (bytestream2_get_le16(&s->g)) {
        while (bytestream2_get_bytes_left(&s->g) >= 6) {
            int stop_size, marker, t1, t2;

            t1        = bytestream2_get_bytes_left(&s->g);
            t2        = bytestream2_get_le16(&s->g);
            stop_size = t1 - FFMIN(t1, t2);
            // ignore uncompressed block size
            bytestream2_skip(&s->g, 2);
            marker    = bytestream2_get_byte(&s->g);

            while (plane < s->nb_planes &&
                   bytestream2_get_bytes_left(&s->g) > stop_size) {
                int run = 1;
                int val = bytestream2_get_byte(&s->g);
                if (val == marker) {
                    run = bytestream2_get_byte(&s->g);
                    if (run == 0)
                        run = bytestream2_get_le16(&s->g);
                    val = bytestream2_get_byte(&s->g);
                }
                if (!bytestream2_get_bytes_left(&s->g))
                    break;

                if (bits_per_plane == 8) {
                    picmemset_8bpp(s, frame, val, run, &x, &y);
                    if (y < 0)
                        goto finish;
                } else {
                    picmemset(s, frame, val, run, &x, &y, &plane, bits_per_plane);
                }
            }
        }
    } else {
        avpriv_request_sample(avctx, "Uncompressed image");
        return avpkt->size;
    }
finish:

    *got_frame      = 1;
    return avpkt->size;
}

AVCodec ff_pictor_decoder = {
    .name           = "pictor",
    .long_name      = NULL_IF_CONFIG_SMALL("Pictor/PC Paint"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PICTOR,
    .priv_data_size = sizeof(PicContext),
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
