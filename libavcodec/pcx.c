/*
 * PC Paintbrush PCX (.pcx) image decoder
 * Copyright (c) 2007, 2008 Ivo van Poorten
 *
 * This decoder does not support CGA palettes. I am unable to find samples
 * and Netpbm cannot generate them.
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

#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"

typedef struct PCXContext {
    AVFrame picture;
} PCXContext;

static av_cold int pcx_init(AVCodecContext *avctx) {
    PCXContext *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame= &s->picture;

    return 0;
}

/**
 * @return advanced src pointer
 */
static const uint8_t *pcx_rle_decode(const uint8_t *src, uint8_t *dst,
                            unsigned int bytes_per_scanline, int compressed) {
    unsigned int i = 0;
    unsigned char run, value;

    if (compressed) {
        while (i<bytes_per_scanline) {
            run = 1;
            value = *src++;
            if (value >= 0xc0) {
                run = value & 0x3f;
                value = *src++;
            }
            while (i<bytes_per_scanline && run--)
                dst[i++] = value;
        }
    } else {
        memcpy(dst, src, bytes_per_scanline);
        src += bytes_per_scanline;
    }

    return src;
}

static void pcx_palette(const uint8_t **src, uint32_t *dst, unsigned int pallen) {
    unsigned int i;

    for (i=0; i<pallen; i++)
        *dst++ = bytestream_get_be24(src);
    if (pallen < 256)
        memset(dst, 0, (256 - pallen) * sizeof(*dst));
}

static int pcx_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                            AVPacket *avpkt) {
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    PCXContext * const s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p = &s->picture;
    int compressed, xmin, ymin, xmax, ymax;
    unsigned int w, h, bits_per_pixel, bytes_per_line, nplanes, stride, y, x,
                 bytes_per_scanline;
    uint8_t *ptr;
    uint8_t const *bufstart = buf;
    uint8_t *scanline;
    int ret = -1;

    if (buf[0] != 0x0a || buf[1] > 5) {
        av_log(avctx, AV_LOG_ERROR, "this is not PCX encoded data\n");
        return -1;
    }

    compressed = buf[2];
    xmin = AV_RL16(buf+ 4);
    ymin = AV_RL16(buf+ 6);
    xmax = AV_RL16(buf+ 8);
    ymax = AV_RL16(buf+10);

    if (xmax < xmin || ymax < ymin) {
        av_log(avctx, AV_LOG_ERROR, "invalid image dimensions\n");
        return -1;
    }

    w = xmax - xmin + 1;
    h = ymax - ymin + 1;

    bits_per_pixel     = buf[3];
    bytes_per_line     = AV_RL16(buf+66);
    nplanes            = buf[65];
    bytes_per_scanline = nplanes * bytes_per_line;

    if (bytes_per_scanline < w * bits_per_pixel * nplanes / 8) {
        av_log(avctx, AV_LOG_ERROR, "PCX data is corrupted\n");
        return -1;
    }

    switch ((nplanes<<8) + bits_per_pixel) {
        case 0x0308:
            avctx->pix_fmt = PIX_FMT_RGB24;
            break;
        case 0x0108:
        case 0x0104:
        case 0x0102:
        case 0x0101:
        case 0x0401:
        case 0x0301:
        case 0x0201:
            avctx->pix_fmt = PIX_FMT_PAL8;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "invalid PCX file\n");
            return -1;
    }

    buf += 128;

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    if (av_image_check_size(w, h, 0, avctx))
        return -1;
    if (w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);
    if (avctx->get_buffer(avctx, p) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    p->pict_type = AV_PICTURE_TYPE_I;

    ptr    = p->data[0];
    stride = p->linesize[0];

    scanline = av_malloc(bytes_per_scanline);
    if (!scanline)
        return AVERROR(ENOMEM);

    if (nplanes == 3 && bits_per_pixel == 8) {
        for (y=0; y<h; y++) {
            buf = pcx_rle_decode(buf, scanline, bytes_per_scanline, compressed);

            for (x=0; x<w; x++) {
                ptr[3*x  ] = scanline[x                    ];
                ptr[3*x+1] = scanline[x+ bytes_per_line    ];
                ptr[3*x+2] = scanline[x+(bytes_per_line<<1)];
            }

            ptr += stride;
        }

    } else if (nplanes == 1 && bits_per_pixel == 8) {
        const uint8_t *palstart = bufstart + buf_size - 769;

        for (y=0; y<h; y++, ptr+=stride) {
            buf = pcx_rle_decode(buf, scanline, bytes_per_scanline, compressed);
            memcpy(ptr, scanline, w);
        }

        if (buf != palstart) {
            av_log(avctx, AV_LOG_WARNING, "image data possibly corrupted\n");
            buf = palstart;
        }
        if (*buf++ != 12) {
            av_log(avctx, AV_LOG_ERROR, "expected palette after image data\n");
            goto end;
        }

    } else if (nplanes == 1) {   /* all packed formats, max. 16 colors */
        GetBitContext s;

        for (y=0; y<h; y++) {
            init_get_bits(&s, scanline, bytes_per_scanline<<3);

            buf = pcx_rle_decode(buf, scanline, bytes_per_scanline, compressed);

            for (x=0; x<w; x++)
                ptr[x] = get_bits(&s, bits_per_pixel);
            ptr += stride;
        }

    } else {    /* planar, 4, 8 or 16 colors */
        int i;

        for (y=0; y<h; y++) {
            buf = pcx_rle_decode(buf, scanline, bytes_per_scanline, compressed);

            for (x=0; x<w; x++) {
                int m = 0x80 >> (x&7), v = 0;
                for (i=nplanes - 1; i>=0; i--) {
                    v <<= 1;
                    v  += !!(scanline[i*bytes_per_line + (x>>3)] & m);
                }
                ptr[x] = v;
            }
            ptr += stride;
        }
    }

    if (nplanes == 1 && bits_per_pixel == 8) {
        pcx_palette(&buf, (uint32_t *) p->data[1], 256);
    } else if (bits_per_pixel < 8) {
        const uint8_t *palette = bufstart+16;
        pcx_palette(&palette, (uint32_t *) p->data[1], 16);
    }

    *picture = s->picture;
    *data_size = sizeof(AVFrame);

    ret = buf - bufstart;
end:
    av_free(scanline);
    return ret;
}

static av_cold int pcx_end(AVCodecContext *avctx) {
    PCXContext *s = avctx->priv_data;

    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

AVCodec ff_pcx_decoder = {
    .name           = "pcx",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PCX,
    .priv_data_size = sizeof(PCXContext),
    .init           = pcx_init,
    .close          = pcx_end,
    .decode         = pcx_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("PC Paintbrush PCX image"),
};
