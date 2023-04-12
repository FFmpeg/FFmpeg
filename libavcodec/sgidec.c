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
 * @param width length of out_buf in nb of elements
 * @return nb of elements written, else return error code.
 */
static int expand_rle_row8(void *logctx, uint8_t *out_buf,
                           GetByteContext *g, unsigned width)
{
    unsigned char pixel, count;
    unsigned char *orig = out_buf;
    uint8_t *out_end = out_buf + width;

    while (out_buf < out_end) {
        if (bytestream2_get_bytes_left(g) < 1)
            return AVERROR_INVALIDDATA;
        pixel = bytestream2_get_byteu(g);
        if (!(count = (pixel & 0x7f))) {
            break;
        }

        /* Check for buffer overflow. */
        if (out_end - out_buf < count) {
            av_log(logctx, AV_LOG_ERROR, "Invalid pixel count.\n");
            return AVERROR_INVALIDDATA;
        }

        if (pixel & 0x80) {
            while (count--)
                *out_buf++ = bytestream2_get_byte(g);
        } else {
            pixel = bytestream2_get_byte(g);

            while (count--)
                *out_buf++ = pixel;
        }
    }
    return out_buf - orig;
}

static int expand_rle_row16(void *logctx, uint16_t *out_buf,
                            GetByteContext *g, unsigned width)
{
    unsigned short pixel;
    unsigned char count;
    unsigned short *orig = out_buf;
    uint16_t *out_end = out_buf + width;

    while (out_buf < out_end) {
        if (bytestream2_get_bytes_left(g) < 2)
            return AVERROR_INVALIDDATA;
        pixel = bytestream2_get_be16u(g);
        if (!(count = (pixel & 0x7f)))
            break;

        /* Check for buffer overflow. */
        if (out_end - out_buf < count) {
            av_log(logctx, AV_LOG_ERROR, "Invalid pixel count.\n");
            return AVERROR_INVALIDDATA;
        }

        if (pixel & 0x80) {
            while (count--) {
                pixel = bytestream2_get_ne16(g);
                AV_WN16A(out_buf, pixel);
                out_buf++;
            }
        } else {
            pixel = bytestream2_get_ne16(g);

            while (count--) {
                AV_WN16A(out_buf, pixel);
                out_buf++;
            }
        }
    }
    return out_buf - orig;
}


/**
 * Read a run length encoded SGI image.
 * @param out_buf output buffer
 * @param s the current image state
 * @return 0 if no error, else return error code.
 */
static int read_rle_sgi(void *logctx, uint8_t *out[4], ptrdiff_t stride[4],
                        GetByteContext *g, unsigned width, int height,
                        unsigned nb_components, unsigned bytes_per_channel)
{
    unsigned int len = height * nb_components * 4;
    GetByteContext g_table = *g;
    unsigned int start_offset;
    int ret;

    /* size of  RLE offset and length tables */
    if (len * 2 > bytestream2_get_bytes_left(g)) {
        return AVERROR_INVALIDDATA;
    }

    for (unsigned z = 0; z < nb_components; z++) {
        uint8_t *dest_row = out[z] + (height - 1) * stride[z];
        while (1) {
            start_offset = bytestream2_get_be32(&g_table);
            bytestream2_seek(g, start_offset, SEEK_SET);
            if (bytes_per_channel == 1)
                ret = expand_rle_row8(logctx, dest_row, g, width);
            else
                ret = expand_rle_row16(logctx, (uint16_t *)dest_row, g, width);
            if (ret != width)
                return AVERROR_INVALIDDATA;
            if (dest_row == out[z])
                break;
            dest_row -= stride[z];
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
static int read_uncompressed_sgi(uint8_t *const out[4], const ptrdiff_t stride[4],
                                 GetByteContext *g, unsigned width, int height,
                                 unsigned nb_components, unsigned bytes_per_channel)
{
    unsigned rowsize = width * bytes_per_channel;

    /* Test buffer size. */
    if (rowsize * (int64_t)height * nb_components > bytestream2_get_bytes_left(g))
        return AVERROR_INVALIDDATA;

    for (unsigned z = 0; z < nb_components; z++) {
        uint8_t *cur_row = out[z] + (height - 1) * stride[z];
        while (1) {
            bytestream2_get_bufferu(g, cur_row, rowsize);
            if (cur_row == out[z])
                break;
            cur_row -= stride[z];
        }
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *p,
                        int *got_frame, AVPacket *avpkt)
{
    GetByteContext g;
    unsigned int bytes_per_channel, nb_components, dimension, rle, width;
    uint8_t *out[4];
    ptrdiff_t linesize[4];
    int height;
    int ret = 0;

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
        avctx->pix_fmt = bytes_per_channel == 2 ? AV_PIX_FMT_GBRP16BE : AV_PIX_FMT_GBRP;
    } else if (nb_components == SGI_RGBA) {
        avctx->pix_fmt = bytes_per_channel == 2 ? AV_PIX_FMT_GBRAP16BE : AV_PIX_FMT_GBRAP;
    } else {
        av_log(avctx, AV_LOG_ERROR, "wrong picture format\n");
        return AVERROR_INVALIDDATA;
    }

    ret = ff_set_dimensions(avctx, width, height);
    if (ret < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    switch (nb_components) {
#define MAP(in_idx, out_idx) \
    out[(in_idx)]      = p->data[(out_idx)]; \
    linesize[(in_idx)] = p->linesize[(out_idx)]
    case SGI_GRAYSCALE:
        MAP(0, 0);
        break;
    case SGI_RGBA:
        MAP(3, 3);
        /* fallthrough */
    case SGI_RGB:
        MAP(0, 2);
        MAP(1, 0);
        MAP(2, 1);
        break;
    }
    p->pict_type = AV_PICTURE_TYPE_I;
    p->flags |= AV_FRAME_FLAG_KEY;

    /* Skip header. */
    bytestream2_seek(&g, SGI_HEADER_SIZE, SEEK_SET);
    if (rle) {
        ret = read_rle_sgi(avctx, out, linesize, &g,
                           width, height, nb_components, bytes_per_channel);
    } else {
        ret = read_uncompressed_sgi(out, linesize, &g,
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
