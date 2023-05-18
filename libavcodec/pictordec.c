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

#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "cga_data.h"
#include "codec_internal.h"
#include "decode.h"

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

static void picmemset(PicContext *s, AVFrame *frame, unsigned value, int run,
                      int *x, int *y, int *plane, int bits_per_plane)
{
    uint8_t *d;
    int shift = *plane * bits_per_plane;
    unsigned mask  = ((1U << bits_per_plane) - 1) << shift;
    int xl = *x;
    int yl = *y;
    int planel = *plane;
    int pixels_per_value = 8/bits_per_plane;
    value   <<= shift;

    d = frame->data[0] + yl * frame->linesize[0];
    while (run > 0) {
        int j;
        for (j = 8-bits_per_plane; j >= 0; j -= bits_per_plane) {
            d[xl] |= (value >> j) & mask;
            xl += 1;
            while (xl == s->width) {
                yl -= 1;
                xl = 0;
                if (yl < 0) {
                   yl = s->height - 1;
                   planel += 1;
                   if (planel >= s->nb_planes)
                       goto end;
                   value <<= bits_per_plane;
                   mask  <<= bits_per_plane;
                }
                d = frame->data[0] + yl * frame->linesize[0];
                if (s->nb_planes == 1 &&
                    run*pixels_per_value >= s->width &&
                    pixels_per_value < (s->width / pixels_per_value * pixels_per_value)
                    ) {
                    for (; xl < pixels_per_value; xl ++) {
                        j = (j < bits_per_plane ? 8 : j) - bits_per_plane;
                        d[xl] |= (value >> j) & mask;
                    }
                    av_memcpy_backptr(d+xl, pixels_per_value, s->width - xl);
                    run -= s->width / pixels_per_value;
                    xl = s->width / pixels_per_value * pixels_per_value;
                }
            }
        }
        run--;
    }
end:
    *x = xl;
    *y = yl;
    *plane = planel;
}

static const uint8_t cga_mode45_index[6][4] = {
    [0] = { 0, 3,  5,   7 }, // mode4, palette#1, low intensity
    [1] = { 0, 2,  4,   6 }, // mode4, palette#2, low intensity
    [2] = { 0, 3,  4,   7 }, // mode5, low intensity
    [3] = { 0, 11, 13, 15 }, // mode4, palette#1, high intensity
    [4] = { 0, 10, 12, 14 }, // mode4, palette#2, high intensity
    [5] = { 0, 11, 12, 15 }, // mode5, high intensity
};

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    PicContext *s = avctx->priv_data;
    uint32_t *palette;
    int bits_per_plane, bpp, etype, esize, npal, pos_after_pal;
    int i, x, y, plane, tmp, ret, val;

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

    if (bytestream2_peek_byte(&s->g) == 0xFF || bpp == 1 || bpp == 4 || bpp == 8) {
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

    if (av_image_check_size(s->width, s->height, 0, avctx) < 0)
        return -1;

    /*
        There are 2 coding modes, RLE and RAW.
        Undamaged RAW should be proportional to W*H and thus bigger than RLE
        RLE codes the most compressed runs by
        1 byte for val (=marker)
        1 byte run (=0)
        2 bytes run
        1 byte val
        thats 5 bytes and the maximum run we can code is 65535

        The RLE decoder can exit prematurly but it does not on any image available
        Based on this the formula is assumed correct for undamaged images.
        If an image is found which exploits the special end
        handling and breaks this formula then this needs to be adapted.
    */
    if (bytestream2_get_bytes_left(&s->g) < s->width * s->height / 65535 * 5)
        return AVERROR_INVALIDDATA;

    if (s->width != avctx->width || s->height != avctx->height) {
        ret = ff_set_dimensions(avctx, s->width, s->height);
        if (ret < 0)
            return ret;
    }

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    memset(frame->data[0], 0, s->height * frame->linesize[0]);
    frame->pict_type           = AV_PICTURE_TYPE_I;
#if FF_API_PALETTE_HAS_CHANGED
FF_DISABLE_DEPRECATION_WARNINGS
    frame->palette_has_changed = 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

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
            palette[i]  = ff_cga_palette[FFMIN(pal_idx, 15)];
        }
    } else if (etype == 3) {
        npal = FFMIN(esize, 16);
        for (i = 0; i < npal; i++) {
            int pal_idx = bytestream2_get_byte(&s->g);
            palette[i]  = ff_ega_palette[FFMIN(pal_idx, 63)];
        }
    } else if (etype == 4 || etype == 5) {
        npal = FFMIN(esize / 3, 256);
        for (i = 0; i < npal; i++) {
            palette[i] = bytestream2_get_be24(&s->g) << 2;
            palette[i] |= 0xFFU << 24 | palette[i] >> 6 & 0x30303;
        }
    } else {
        if (bpp == 1) {
            npal = 2;
            palette[0] = 0xFF000000;
            palette[1] = 0xFFFFFFFF;
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

    val = 0;
    y = s->height - 1;
    if (bytestream2_get_le16(&s->g)) {
        x = 0;
        plane = 0;
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
                val = bytestream2_get_byte(&s->g);
                if (val == marker) {
                    run = bytestream2_get_byte(&s->g);
                    if (run == 0)
                        run = bytestream2_get_le16(&s->g);
                    val = bytestream2_get_byte(&s->g);
                }

                if (bits_per_plane == 8) {
                    picmemset_8bpp(s, frame, val, run, &x, &y);
                    if (y < 0)
                        goto finish;
                } else {
                    picmemset(s, frame, val, run, &x, &y, &plane, bits_per_plane);
                }
            }
        }

        if (s->nb_planes - plane > 1)
            return AVERROR_INVALIDDATA;

        if (plane < s->nb_planes && x < avctx->width) {
            int run = (y + 1) * avctx->width - x;
            if (bits_per_plane == 8)
                picmemset_8bpp(s, frame, val, run, &x, &y);
            else
                picmemset(s, frame, val, run / (8 / bits_per_plane), &x, &y, &plane, bits_per_plane);
        }
    } else {
        while (y >= 0 && bytestream2_get_bytes_left(&s->g) > 0) {
            memcpy(frame->data[0] + y * frame->linesize[0], s->g.buffer, FFMIN(avctx->width, bytestream2_get_bytes_left(&s->g)));
            bytestream2_skip(&s->g, avctx->width);
            y--;
        }
    }
finish:

    *got_frame      = 1;
    return avpkt->size;
}

const FFCodec ff_pictor_decoder = {
    .p.name         = "pictor",
    CODEC_LONG_NAME("Pictor/PC Paint"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PICTOR,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(PicContext),
    FF_CODEC_DECODE_CB(decode_frame),
};
