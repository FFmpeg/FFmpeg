/*
 * Pictor/PC Paint decoder
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
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
 * Pictor/PC Paint decoder
 */

#include "libavcore/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "cga_data.h"

typedef struct PicContext {
    AVFrame frame;
    int width, height;
    int nb_planes;
} PicContext;

static void picmemset_8bpp(PicContext *s, int value, int run, int *x, int *y)
{
    while (run > 0) {
        uint8_t *d = s->frame.data[0] + *y * s->frame.linesize[0];
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

static void picmemset(PicContext *s, int value, int run, int *x, int *y, int *plane, int bits_per_plane)
{
    uint8_t *d;
    int shift = *plane * bits_per_plane;
    int mask  = ((1 << bits_per_plane) - 1) << shift;
    value   <<= shift;

    while (run > 0) {
        int j;
        for (j = 8-bits_per_plane; j >= 0; j -= bits_per_plane) {
            d = s->frame.data[0] + *y * s->frame.linesize[0];
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
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    PicContext *s = avctx->priv_data;
    int buf_size = avpkt->size;
    const uint8_t *buf = avpkt->data;
    const uint8_t *buf_end = avpkt->data + buf_size;
    uint32_t *palette;
    int bits_per_plane, bpp, etype, esize, npal;
    int i, x, y, plane;

    if (buf_size < 11)
        return AVERROR_INVALIDDATA;

    if (bytestream_get_le16(&buf) != 0x1234)
        return AVERROR_INVALIDDATA;
    s->width  = bytestream_get_le16(&buf);
    s->height = bytestream_get_le16(&buf);
    buf += 4;
    bits_per_plane    = *buf & 0xF;
    s->nb_planes      = (*buf++ >> 4) + 1;
    bpp               = s->nb_planes ? bits_per_plane*s->nb_planes : bits_per_plane;
    if (bits_per_plane > 8 || bpp < 1 || bpp > 32) {
        av_log_ask_for_sample(s, "unsupported bit depth\n");
        return AVERROR_INVALIDDATA;
    }

    if (*buf == 0xFF) {
        buf += 2;
        etype  = bytestream_get_le16(&buf);
        esize  = bytestream_get_le16(&buf);
        if (buf_end - buf < esize)
            return AVERROR_INVALIDDATA;
    } else {
        etype = -1;
        esize = 0;
    }

    avctx->pix_fmt = PIX_FMT_PAL8;

    if (s->width != avctx->width && s->height != avctx->height) {
        if (av_image_check_size(s->width, s->height, 0, avctx) < 0)
            return -1;
        avcodec_set_dimensions(avctx, s->width, s->height);
        if (s->frame.data[0])
            avctx->release_buffer(avctx, &s->frame);
    }

    if (avctx->get_buffer(avctx, &s->frame) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    memset(s->frame.data[0], 0, s->height * s->frame.linesize[0]);
    s->frame.pict_type           = FF_I_TYPE;
    s->frame.palette_has_changed = 1;

    palette = (uint32_t*)s->frame.data[1];
    if (etype == 1 && esize > 1 && *buf < 6) {
        int idx = *buf;
        npal = 4;
        for (i = 0; i < npal; i++)
            palette[i] = ff_cga_palette[ cga_mode45_index[idx][i] ];
    } else if (etype == 2) {
        npal = FFMIN(esize, 16);
        for (i = 0; i < npal; i++)
            palette[i] = ff_cga_palette[ FFMIN(buf[i], 16)];
    } else if (etype == 3) {
        npal = FFMIN(esize, 16);
        for (i = 0; i < npal; i++)
            palette[i] = ff_ega_palette[ FFMIN(buf[i], 63)];
    } else if (etype == 4 || etype == 5) {
        npal = FFMIN(esize / 3, 256);
        for (i = 0; i < npal; i++)
            palette[i] = AV_RB24(buf + i*3) << 2;
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
    buf += esize;


    x = 0;
    y = s->height - 1;
    plane = 0;
    if (bytestream_get_le16(&buf)) {
        while (buf_end - buf >= 6) {
            const uint8_t *buf_pend = buf + FFMIN(AV_RL16(buf), buf_end - buf);
            //ignore uncompressed block size reported at buf[2]
            int marker = buf[4];
            buf += 5;

            while (plane < s->nb_planes && buf_pend - buf >= 1) {
                int run = 1;
                int val = *buf++;
                if (val == marker) {
                    run = *buf++;
                    if (run == 0)
                        run = bytestream_get_le16(&buf);
                    val = *buf++;
                }
                if (buf > buf_end)
                    break;

                if (bits_per_plane == 8) {
                    picmemset_8bpp(s, val, run, &x, &y);
                    if (y < 0)
                        break;
                } else {
                    picmemset(s, val, run, &x, &y, &plane, bits_per_plane);
                }
            }
        }
    } else {
        av_log_ask_for_sample(s, "uncompressed image\n");
        return buf_size;
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;
    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    PicContext *s = avctx->priv_data;
    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);
    return 0;
}

AVCodec pictor_decoder = {
    "pictor",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_PICTOR,
    sizeof(PicContext),
    NULL,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Pictor/PC Paint"),
};
