/*
 * SGI image decoder
 * Todd Kirby <doubleshot@pacbell.net>
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
#include "internal.h"
#include "sgi.h"

typedef struct SgiState {
    AVCodecContext *avctx;
    unsigned int width;
    unsigned int height;
    unsigned int depth;
    unsigned int bytes_per_channel;
    int linesize;
    GetByteContext g;
} SgiState;

/**
 * Expand an RLE row into a channel.
 * @param s the current image state
 * @param out_buf Points to one line after the output buffer.
 * @param len length of out_buf in bytes
 * @param pixelstride pixel stride of input buffer
 * @return size of output in bytes, -1 if buffer overflows
 */
static int expand_rle_row8(SgiState *s, uint8_t *out_buf,
                           int len, int pixelstride)
{
    unsigned char pixel, count;
    unsigned char *orig = out_buf;
    uint8_t *out_end = out_buf + len;

    while (out_buf < out_end) {
        if (bytestream2_get_bytes_left(&s->g) < 1)
            return AVERROR_INVALIDDATA;
        pixel = bytestream2_get_byteu(&s->g);
        if (!(count = (pixel & 0x7f))) {
            break;
        }

        /* Check for buffer overflow. */
        if (pixelstride * (count - 1) >= len) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid pixel count.\n");
            return AVERROR_INVALIDDATA;
        }

        if (pixel & 0x80) {
            while (count--) {
                *out_buf = bytestream2_get_byte(&s->g);
                out_buf += pixelstride;
            }
        } else {
            pixel = bytestream2_get_byte(&s->g);

            while (count--) {
                *out_buf = pixel;
                out_buf += pixelstride;
            }
        }
    }
    return (out_buf - orig) / pixelstride;
}

static int expand_rle_row16(SgiState *s, uint16_t *out_buf,
                            int len, int pixelstride)
{
    unsigned short pixel;
    unsigned char count;
    unsigned short *orig = out_buf;
    uint16_t *out_end = out_buf + len;

    while (out_buf < out_end) {
        if (bytestream2_get_bytes_left(&s->g) < 2)
            return AVERROR_INVALIDDATA;
        pixel = bytestream2_get_be16u(&s->g);
        if (!(count = (pixel & 0x7f)))
            break;

        /* Check for buffer overflow. */
        if (pixelstride * (count - 1) >= len) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid pixel count.\n");
            return AVERROR_INVALIDDATA;
        }

        if (pixel & 0x80) {
            while (count--) {
                pixel = bytestream2_get_ne16(&s->g);
                AV_WN16A(out_buf, pixel);
                out_buf += pixelstride;
            }
        } else {
            pixel = bytestream2_get_ne16(&s->g);

            while (count--) {
                AV_WN16A(out_buf, pixel);
                out_buf += pixelstride;
            }
        }
    }
    return (out_buf - orig) / pixelstride;
}


/**
 * Read a run length encoded SGI image.
 * @param out_buf output buffer
 * @param s the current image state
 * @return 0 if no error, else return error number.
 */
static int read_rle_sgi(uint8_t *out_buf, SgiState *s)
{
    uint8_t *dest_row;
    unsigned int len = s->height * s->depth * 4;
    GetByteContext g_table = s->g;
    unsigned int y, z;
    unsigned int start_offset;
    int linesize, ret;

    /* size of  RLE offset and length tables */
    if (len * 2 > bytestream2_get_bytes_left(&s->g)) {
        return AVERROR_INVALIDDATA;
    }

    for (z = 0; z < s->depth; z++) {
        dest_row = out_buf;
        for (y = 0; y < s->height; y++) {
            linesize = s->width * s->depth * s->bytes_per_channel;
            dest_row -= s->linesize;
            start_offset = bytestream2_get_be32(&g_table);
            bytestream2_seek(&s->g, start_offset, SEEK_SET);
            if (s->bytes_per_channel == 1)
                ret = expand_rle_row8(s, dest_row + z, linesize, s->depth);
            else
                ret = expand_rle_row16(s, (uint16_t *)dest_row + z, linesize, s->depth);
            if (ret != s->width)
                return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}

/**
 * Read an uncompressed SGI image.
 * @param out_buf output buffer
 * @param s the current image state
 * @return 0 if read success, otherwise return -1.
 */
static int read_uncompressed_sgi(unsigned char *out_buf, SgiState *s)
{
    int x, y, z;
    unsigned int offset = s->height * s->width * s->bytes_per_channel;
    GetByteContext gp[4];
    uint8_t *out_end;

    /* Test buffer size. */
    if (offset * s->depth > bytestream2_get_bytes_left(&s->g))
        return AVERROR_INVALIDDATA;

    /* Create a reader for each plane */
    for (z = 0; z < s->depth; z++) {
        gp[z] = s->g;
        bytestream2_skip(&gp[z], z * offset);
    }

    for (y = s->height - 1; y >= 0; y--) {
        out_end = out_buf + (y * s->linesize);
        if (s->bytes_per_channel == 1) {
            for (x = s->width; x > 0; x--)
                for (z = 0; z < s->depth; z++)
                    *out_end++ = bytestream2_get_byteu(&gp[z]);
        } else {
            uint16_t *out16 = (uint16_t *)out_end;
            for (x = s->width; x > 0; x--)
                for (z = 0; z < s->depth; z++)
                    *out16++ = bytestream2_get_ne16u(&gp[z]);
        }
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    SgiState *s = avctx->priv_data;
    AVFrame *p = data;
    unsigned int dimension, rle;
    int ret = 0;
    uint8_t *out_buf, *out_end;

    bytestream2_init(&s->g, avpkt->data, avpkt->size);
    if (bytestream2_get_bytes_left(&s->g) < SGI_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "buf_size too small (%d)\n", avpkt->size);
        return AVERROR_INVALIDDATA;
    }

    /* Test for SGI magic. */
    if (bytestream2_get_be16(&s->g) != SGI_MAGIC) {
        av_log(avctx, AV_LOG_ERROR, "bad magic number\n");
        return AVERROR_INVALIDDATA;
    }

    rle                  = bytestream2_get_byte(&s->g);
    s->bytes_per_channel = bytestream2_get_byte(&s->g);
    dimension            = bytestream2_get_be16(&s->g);
    s->width             = bytestream2_get_be16(&s->g);
    s->height            = bytestream2_get_be16(&s->g);
    s->depth             = bytestream2_get_be16(&s->g);

    if (s->bytes_per_channel != 1 && s->bytes_per_channel != 2) {
        av_log(avctx, AV_LOG_ERROR, "wrong channel number\n");
        return AVERROR(EINVAL);
    }

    /* Check for supported image dimensions. */
    if (dimension != 2 && dimension != 3) {
        av_log(avctx, AV_LOG_ERROR, "wrong dimension number\n");
        return AVERROR(EINVAL);
    }

    if (s->depth == SGI_GRAYSCALE) {
        avctx->pix_fmt = s->bytes_per_channel == 2 ? AV_PIX_FMT_GRAY16BE : AV_PIX_FMT_GRAY8;
    } else if (s->depth == SGI_RGB) {
        avctx->pix_fmt = s->bytes_per_channel == 2 ? AV_PIX_FMT_RGB48BE : AV_PIX_FMT_RGB24;
    } else if (s->depth == SGI_RGBA) {
        avctx->pix_fmt = s->bytes_per_channel == 2 ? AV_PIX_FMT_RGBA64BE : AV_PIX_FMT_RGBA;
    } else {
        av_log(avctx, AV_LOG_ERROR, "wrong picture format\n");
        return AVERROR(EINVAL);
    }

    ret = ff_set_dimensions(avctx, s->width, s->height);
    if (ret < 0)
        return ret;

    ret = ff_get_buffer(avctx, p, 0);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed.\n");
        return ret;
    }

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;
    out_buf = p->data[0];

    out_end = out_buf + p->linesize[0] * s->height;

    s->linesize = p->linesize[0];

    /* Skip header. */
    bytestream2_seek(&s->g, SGI_HEADER_SIZE, SEEK_SET);
    if (rle) {
        ret = read_rle_sgi(out_end, s);
    } else {
        ret = read_uncompressed_sgi(out_buf, s);
    }

    if (ret == 0) {
        *got_frame = 1;
        return avpkt->size;
    } else {
        return ret;
    }
}

static av_cold int sgi_decode_init(AVCodecContext *avctx)
{
    SgiState *s = avctx->priv_data;

    s->avctx = avctx;

    return 0;
}

AVCodec ff_sgi_decoder = {
    .name           = "sgi",
    .long_name      = NULL_IF_CONFIG_SMALL("SGI image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SGI,
    .priv_data_size = sizeof(SgiState),
    .decode         = decode_frame,
    .init           = sgi_decode_init,
    .capabilities   = AV_CODEC_CAP_DR1,
};
