/*
 * CDXL video decoder
 * Copyright (c) 2011-2012 Paul B Mahol
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

#define UNCHECKED_BITSTREAM_READER 1

#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"

#define BIT_PLANAR   0x00
#define CHUNKY       0x20
#define BYTE_PLANAR  0x40
#define BIT_LINE     0x80
#define BYTE_LINE    0xC0

typedef struct {
    AVCodecContext *avctx;
    AVFrame        frame;
    int            bpp;
    int            format;
    int            padded_bits;
    const uint8_t  *palette;
    int            palette_size;
    const uint8_t  *video;
    int            video_size;
    uint8_t        *new_video;
    int            new_video_size;
} CDXLVideoContext;

static av_cold int cdxl_decode_init(AVCodecContext *avctx)
{
    CDXLVideoContext *c = avctx->priv_data;

    avcodec_get_frame_defaults(&c->frame);
    c->new_video_size = 0;
    c->avctx          = avctx;

    return 0;
}

static void import_palette(CDXLVideoContext *c, uint32_t *new_palette)
{
    int i;

    for (i = 0; i < c->palette_size / 2; i++) {
        unsigned rgb = AV_RB16(&c->palette[i * 2]);
        unsigned r   = ((rgb >> 8) & 0xF) * 0x11;
        unsigned g   = ((rgb >> 4) & 0xF) * 0x11;
        unsigned b   =  (rgb       & 0xF) * 0x11;
        AV_WN32(&new_palette[i], (0xFFU << 24) | (r << 16) | (g << 8) | b);
    }
}

static void bitplanar2chunky(CDXLVideoContext *c, int linesize, uint8_t *out)
{
    GetBitContext gb;
    int x, y, plane;

    init_get_bits(&gb, c->video, c->video_size * 8);
    for (plane = 0; plane < c->bpp; plane++) {
        for (y = 0; y < c->avctx->height; y++) {
            for (x = 0; x < c->avctx->width; x++)
                out[linesize * y + x] |= get_bits1(&gb) << plane;
            skip_bits(&gb, c->padded_bits);
        }
    }
}

static void bitline2chunky(CDXLVideoContext *c, int linesize, uint8_t *out)
{
    GetBitContext  gb;
    int x, y, plane;

    init_get_bits(&gb, c->video, c->video_size * 8);
    for (y = 0; y < c->avctx->height; y++) {
        for (plane = 0; plane < c->bpp; plane++) {
            for (x = 0; x < c->avctx->width; x++)
                out[linesize * y + x] |= get_bits1(&gb) << plane;
            skip_bits(&gb, c->padded_bits);
        }
    }
}

static void import_format(CDXLVideoContext *c, int linesize, uint8_t *out)
{
    memset(out, 0, linesize * c->avctx->height);

    switch (c->format) {
    case BIT_PLANAR:
        bitplanar2chunky(c, linesize, out);
        break;
    case BIT_LINE:
        bitline2chunky(c, linesize, out);
        break;
    }
}

static void cdxl_decode_rgb(CDXLVideoContext *c)
{
    uint32_t *new_palette = (uint32_t *)c->frame.data[1];

    memset(c->frame.data[1], 0, AVPALETTE_SIZE);
    import_palette(c, new_palette);
    import_format(c, c->frame.linesize[0], c->frame.data[0]);
}

static void cdxl_decode_ham6(CDXLVideoContext *c)
{
    AVCodecContext *avctx = c->avctx;
    uint32_t new_palette[16], r, g, b;
    uint8_t *ptr, *out, index, op;
    int x, y;

    ptr = c->new_video;
    out = c->frame.data[0];

    import_palette(c, new_palette);
    import_format(c, avctx->width, c->new_video);

    for (y = 0; y < avctx->height; y++) {
        r = new_palette[0] & 0xFF0000;
        g = new_palette[0] & 0xFF00;
        b = new_palette[0] & 0xFF;
        for (x = 0; x < avctx->width; x++) {
            index  = *ptr++;
            op     = index >> 4;
            index &= 15;
            switch (op) {
            case 0:
                r = new_palette[index] & 0xFF0000;
                g = new_palette[index] & 0xFF00;
                b = new_palette[index] & 0xFF;
                break;
            case 1:
                b = index * 0x11;
                break;
            case 2:
                r = index * 0x11 << 16;
                break;
            case 3:
                g = index * 0x11 << 8;
                break;
            }
            AV_WL24(out + x * 3, r | g | b);
        }
        out += c->frame.linesize[0];
    }
}

static void cdxl_decode_ham8(CDXLVideoContext *c)
{
    AVCodecContext *avctx = c->avctx;
    uint32_t new_palette[64], r, g, b;
    uint8_t *ptr, *out, index, op;
    int x, y;

    ptr = c->new_video;
    out = c->frame.data[0];

    import_palette(c, new_palette);
    import_format(c, avctx->width, c->new_video);

    for (y = 0; y < avctx->height; y++) {
        r = new_palette[0] & 0xFF0000;
        g = new_palette[0] & 0xFF00;
        b = new_palette[0] & 0xFF;
        for (x = 0; x < avctx->width; x++) {
            index  = *ptr++;
            op     = index >> 6;
            index &= 63;
            switch (op) {
            case 0:
                r = new_palette[index] & 0xFF0000;
                g = new_palette[index] & 0xFF00;
                b = new_palette[index] & 0xFF;
                break;
            case 1:
                b = (index <<  2) | (b & 3);
                break;
            case 2:
                r = (index << 18) | (r & (3 << 16));
                break;
            case 3:
                g = (index << 10) | (g & (3 << 8));
                break;
            }
            AV_WL24(out + x * 3, r | g | b);
        }
        out += c->frame.linesize[0];
    }
}

static int cdxl_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame, AVPacket *pkt)
{
    CDXLVideoContext *c = avctx->priv_data;
    AVFrame * const p = &c->frame;
    int ret, w, h, encoding, aligned_width, buf_size = pkt->size;
    const uint8_t *buf = pkt->data;

    if (buf_size < 32)
        return AVERROR_INVALIDDATA;
    encoding        = buf[1] & 7;
    c->format       = buf[1] & 0xE0;
    w               = AV_RB16(&buf[14]);
    h               = AV_RB16(&buf[16]);
    c->bpp          = buf[19];
    c->palette_size = AV_RB16(&buf[20]);
    c->palette      = buf + 32;
    c->video        = c->palette + c->palette_size;
    c->video_size   = buf_size - c->palette_size - 32;

    if (c->palette_size > 512)
        return AVERROR_INVALIDDATA;
    if (buf_size < c->palette_size + 32)
        return AVERROR_INVALIDDATA;
    if (c->bpp < 1)
        return AVERROR_INVALIDDATA;
    if (c->format != BIT_PLANAR && c->format != BIT_LINE) {
        av_log_ask_for_sample(avctx, "unsupported pixel format: 0x%0x\n", c->format);
        return AVERROR_PATCHWELCOME;
    }

    if ((ret = av_image_check_size(w, h, 0, avctx)) < 0)
        return ret;
    if (w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);

    aligned_width = FFALIGN(c->avctx->width, 16);
    c->padded_bits  = aligned_width - c->avctx->width;
    if (c->video_size < aligned_width * avctx->height * c->bpp / 8)
        return AVERROR_INVALIDDATA;
    if (!encoding && c->palette_size && c->bpp <= 8) {
        avctx->pix_fmt = AV_PIX_FMT_PAL8;
    } else if (encoding == 1 && (c->bpp == 6 || c->bpp == 8)) {
        if (c->palette_size != (1 << (c->bpp - 1)))
            return AVERROR_INVALIDDATA;
        avctx->pix_fmt = AV_PIX_FMT_BGR24;
    } else {
        av_log_ask_for_sample(avctx, "unsupported encoding %d and bpp %d\n",
                              encoding, c->bpp);
        return AVERROR_PATCHWELCOME;
    }

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference = 0;
    if ((ret = ff_get_buffer(avctx, p)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    p->pict_type = AV_PICTURE_TYPE_I;

    if (encoding) {
        av_fast_padded_malloc(&c->new_video, &c->new_video_size,
                              h * w + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!c->new_video)
            return AVERROR(ENOMEM);
        if (c->bpp == 8)
            cdxl_decode_ham8(c);
        else
            cdxl_decode_ham6(c);
    } else {
        cdxl_decode_rgb(c);
    }
    *got_frame = 1;
    *(AVFrame*)data = c->frame;

    return buf_size;
}

static av_cold int cdxl_decode_end(AVCodecContext *avctx)
{
    CDXLVideoContext *c = avctx->priv_data;

    av_free(c->new_video);
    if (c->frame.data[0])
        avctx->release_buffer(avctx, &c->frame);

    return 0;
}

AVCodec ff_cdxl_decoder = {
    .name           = "cdxl",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CDXL,
    .priv_data_size = sizeof(CDXLVideoContext),
    .init           = cdxl_decode_init,
    .close          = cdxl_decode_end,
    .decode         = cdxl_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Commodore CDXL video"),
};
