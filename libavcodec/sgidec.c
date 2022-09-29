/*
 * SGI image decoder
 * Todd Kirby <doubleshot@pacbell.net>
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

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "sgi.h"

/**
 * Expand an RLE row into a channel.
 * @param logctx a logcontext
 * @param out_buf Points to one line after the output buffer.
 * @param g   GetByteContext used to read input from
 * @param len length of out_buf in bytes
 * @param pixelstride pixel stride of input buffer
 * @return size of output in bytes, else return error code.
 */
static int expand_rle_row8(void *logctx, uint8_t *out_buf,
                           GetByteContext *g,
                           int len, int pixelstride)
{
    unsigned char pixel, count;
    unsigned char *orig = out_buf;
    uint8_t *out_end = out_buf + len;

    while (out_buf < out_end) {
        if (bytestream2_get_bytes_left(g) < 1)
            return AVERROR_INVALIDDATA;
        pixel = bytestream2_get_byteu(g);
        if (!(count = (pixel & 0x7f))) {
            break;
        }

        /* Check for buffer overflow. */
        if (out_end - out_buf <= pixelstride * (count - 1)) {
            av_log(logctx, AV_LOG_ERROR, "Invalid pixel count.\n");
            return AVERROR_INVALIDDATA;
        }

        if (pixel & 0x80) {
            while (count--) {
                *out_buf = bytestream2_get_byte(g);
                out_buf += pixelstride;
            }
        } else {
            pixel = bytestream2_get_byte(g);

            while (count--) {
                *out_buf = pixel;
                out_buf += pixelstride;
            }
        }
    }
    return (out_buf - orig) / pixelstride;
}

static int expand_rle_row16(void *logctx, uint16_t *out_buf,
                            GetByteContext *g,
                            int len, int pixelstride)
{
    unsigned short pixel;
    unsigned char count;
    unsigned short *orig = out_buf;
    uint16_t *out_end = out_buf + len;

    while (out_buf < out_end) {
        if (bytestream2_get_bytes_left(g) < 2)
            return AVERROR_INVALIDDATA;
        pixel = bytestream2_get_be16u(g);
        if (!(count = (pixel & 0x7f)))
            break;

        /* Check for buffer overflow. */
        if (out_end - out_buf <= pixelstride * (count - 1)) {
            av_log(logctx, AV_LOG_ERROR, "Invalid pixel count.\n");
            return AVERROR_INVALIDDATA;
        }

        if (pixel & 0x80) {
            while (count--) {
                pixel = bytestream2_get_ne16(g);
                AV_WN16A(out_buf, pixel);
                out_buf += pixelstride;
            }
        } else {
            pixel = bytestream2_get_ne16(g);

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
 * @return 0 if no error, else return error code.
 */
static int read_rle_sgi(void *logctx, uint8_t *last_line, GetByteContext *g,
                        ptrdiff_t stride, unsigned width, unsigned height,
                        unsigned nb_components, unsigned bytes_per_channel)
{
    uint8_t *dest_row;
    unsigned int len = height * nb_components * 4;
    GetByteContext g_table = *g;
    unsigned int start_offset;
    int linesize, ret;

    /* size of  RLE offset and length tables */
    if (len * 2 > bytestream2_get_bytes_left(g)) {
        return AVERROR_INVALIDDATA;
    }

    for (unsigned z = 0; z < nb_components; z++) {
        dest_row = last_line;
        for (unsigned remaining_lines = height;;) {
            linesize = width * nb_components;
            start_offset = bytestream2_get_be32(&g_table);
            bytestream2_seek(g, start_offset, SEEK_SET);
            if (bytes_per_channel == 1)
                ret = expand_rle_row8(logctx, dest_row + z, g,
                                      linesize, nb_components);
            else
                ret = expand_rle_row16(logctx, (uint16_t *)dest_row + z, g,
                                       linesize, nb_components);
            if (ret != width)
                return AVERROR_INVALIDDATA;
            if (--remaining_lines == 0)
                break;
            dest_row -= stride;
        }
    }
    return 0;
}

/**
 * Read an uncompressed SGI image.
 * @param out_buf output buffer
 * @param s the current image state
 * @return 0 if read success, else return error code.
 */
static int read_uncompressed_sgi(unsigned char *out_buf, GetByteContext *g,
                                 ptrdiff_t stride, unsigned width, unsigned height,
                                 unsigned nb_components, unsigned bytes_per_channel)
{
    unsigned int offset = height * width * bytes_per_channel;
    GetByteContext gp[4];
    uint8_t *out_end;

    /* Test buffer size. */
    if (offset * nb_components > bytestream2_get_bytes_left(g))
        return AVERROR_INVALIDDATA;

    /* Create a reader for each plane */
    for (unsigned z = 0; z < nb_components; z++) {
        gp[z] = *g;
        bytestream2_skip(&gp[z], z * offset);
    }

    for (int y = height - 1; y >= 0; y--) {
        out_end = out_buf + y * stride;
        if (bytes_per_channel == 1) {
            for (unsigned x = width; x > 0; x--)
                for (unsigned z = 0; z < nb_components; z++)
                    *out_end++ = bytestream2_get_byteu(&gp[z]);
        } else {
            uint16_t *out16 = (uint16_t *)out_end;
            for (unsigned x = width; x > 0; x--)
                for (unsigned z = 0; z < nb_components; z++)
                    *out16++ = bytestream2_get_ne16u(&gp[z]);
        }
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *p,
                        int *got_frame, AVPacket *avpkt)
{
    GetByteContext g;
    unsigned int bytes_per_channel, nb_components, dimension, rle, width;
    int height;
    int ret = 0;
    uint8_t *out_buf, *last_line;

    bytestream2_init(&g, avpkt->data, avpkt->size);
    if (bytestream2_get_bytes_left(&g) < SGI_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "buf_size too small (%d)\n", avpkt->size);
        return AVERROR_INVALIDDATA;
    }

    /* Test for SGI magic. */
    if (bytestream2_get_be16u(&g) != SGI_MAGIC) {
        av_log(avctx, AV_LOG_ERROR, "bad magic number\n");
        return AVERROR_INVALIDDATA;
    }

    rle                  = bytestream2_get_byteu(&g);
    bytes_per_channel    = bytestream2_get_byteu(&g);
    dimension            = bytestream2_get_be16u(&g);
    width                = bytestream2_get_be16u(&g);
    height               = bytestream2_get_be16u(&g);
    nb_components        = bytestream2_get_be16u(&g);

    if (bytes_per_channel != 1 && bytes_per_channel != 2) {
        av_log(avctx, AV_LOG_ERROR, "wrong channel number\n");
        return AVERROR_INVALIDDATA;
    }

    /* Check for supported image dimensions. */
    if (dimension != 2 && dimension != 3) {
        av_log(avctx, AV_LOG_ERROR, "wrong dimension number\n");
        return AVERROR_INVALIDDATA;
    }

    if (nb_components == SGI_GRAYSCALE) {
        avctx->pix_fmt = bytes_per_channel == 2 ? AV_PIX_FMT_GRAY16BE : AV_PIX_FMT_GRAY8;
    } else if (nb_components == SGI_RGB) {
        avctx->pix_fmt = bytes_per_channel == 2 ? AV_PIX_FMT_RGB48BE : AV_PIX_FMT_RGB24;
    } else if (nb_components == SGI_RGBA) {
        avctx->pix_fmt = bytes_per_channel == 2 ? AV_PIX_FMT_RGBA64BE : AV_PIX_FMT_RGBA;
    } else {
        av_log(avctx, AV_LOG_ERROR, "wrong picture format\n");
        return AVERROR_INVALIDDATA;
    }

    ret = ff_set_dimensions(avctx, width, height);
    if (ret < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;
    out_buf = p->data[0];

    last_line = out_buf + p->linesize[0] * (height - 1);

    /* Skip header. */
    bytestream2_seek(&g, SGI_HEADER_SIZE, SEEK_SET);
    if (rle) {
        ret = read_rle_sgi(avctx, last_line, &g, p->linesize[0],
                           width, height, nb_components, bytes_per_channel);
    } else {
        ret = read_uncompressed_sgi(out_buf, &g, p->linesize[0],
                                    width, height, nb_components, bytes_per_channel);
    }
    if (ret)
        return ret;

    *got_frame = 1;
    return avpkt->size;
}

const FFCodec ff_sgi_decoder = {
    .p.name         = "sgi",
    CODEC_LONG_NAME("SGI image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_SGI,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
