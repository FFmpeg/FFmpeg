/*
 * SGI image encoder
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

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "sgi.h"
#include "rle.h"

#define SGI_SINGLE_CHAN 2
#define SGI_MULTI_CHAN 3

static av_cold int encode_init(AVCodecContext *avctx)
{
    avctx->coded_frame = av_frame_alloc();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *frame, int *got_packet)
{
    const AVFrame * const p = frame;
    uint8_t *offsettab, *lengthtab, *in_buf, *encode_buf, *buf;
    int x, y, z, length, tablesize, ret;
    unsigned int width, height, depth, dimension;
    unsigned char *end_buf;

    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->coded_frame->key_frame = 1;

    width  = avctx->width;
    height = avctx->height;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_GRAY8:
        dimension = SGI_SINGLE_CHAN;
        depth     = SGI_GRAYSCALE;
        break;
    case AV_PIX_FMT_RGB24:
        dimension = SGI_MULTI_CHAN;
        depth     = SGI_RGB;
        break;
    case AV_PIX_FMT_RGBA:
        dimension = SGI_MULTI_CHAN;
        depth     = SGI_RGBA;
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    tablesize = depth * height * 4;
    length = SGI_HEADER_SIZE;
    if (avctx->coder_type == FF_CODER_TYPE_RAW)
        length += depth * height * width;
    else // assume ff_rl_encode() produces at most 2x size of input
        length += tablesize * 2 + depth * height * (2 * width + 1);

    if ((ret = ff_alloc_packet(pkt, length)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet of size %d.\n", length);
        return ret;
    }
    buf     = pkt->data;
    end_buf = pkt->data + pkt->size;

    /* Encode header. */
    bytestream_put_be16(&buf, SGI_MAGIC);
    bytestream_put_byte(&buf, avctx->coder_type != FF_CODER_TYPE_RAW); /* RLE 1 - VERBATIM 0*/
    bytestream_put_byte(&buf, 1); /* bytes_per_channel */
    bytestream_put_be16(&buf, dimension);
    bytestream_put_be16(&buf, width);
    bytestream_put_be16(&buf, height);
    bytestream_put_be16(&buf, depth);

    /* The rest are constant in this implementation. */
    bytestream_put_be32(&buf, 0L); /* pixmin */
    bytestream_put_be32(&buf, 255L); /* pixmax */
    bytestream_put_be32(&buf, 0L); /* dummy */

    /* name */
    memset(buf, 0, SGI_HEADER_SIZE);
    buf += 80;

     /* colormap */
    bytestream_put_be32(&buf, 0L);

    /* The rest of the 512 byte header is unused. */
    buf += 404;
    offsettab = buf;

    if (avctx->coder_type  != FF_CODER_TYPE_RAW) {
        /* Skip RLE offset table. */
        buf += tablesize;
        lengthtab = buf;

        /* Skip RLE length table. */
        buf += tablesize;

        /* Make an intermediate consecutive buffer. */
        if (!(encode_buf = av_malloc(width)))
            return -1;

        for (z = 0; z < depth; z++) {
            in_buf = p->data[0] + p->linesize[0] * (height - 1) + z;

            for (y = 0; y < height; y++) {
                bytestream_put_be32(&offsettab, buf - pkt->data);

                for (x = 0; x < width; x++)
                    encode_buf[x] = in_buf[depth * x];

                if ((length = ff_rle_encode(buf, end_buf - buf - 1, encode_buf, 1, width, 0, 0, 0x80, 0)) < 1) {
                    av_free(encode_buf);
                    return -1;
                }

                buf += length;
                bytestream_put_byte(&buf, 0);
                bytestream_put_be32(&lengthtab, length + 1);
                in_buf -= p->linesize[0];
            }
        }

        av_free(encode_buf);
    } else {
        for (z = 0; z < depth; z++) {
            in_buf = p->data[0] + p->linesize[0] * (height - 1) + z;

            for (y = 0; y < height; y++) {
                for (x = 0; x < width * depth; x += depth)
                    bytestream_put_byte(&buf, in_buf[x]);

                in_buf -= p->linesize[0];
            }
        }
    }

    /* total length */
    pkt->size = buf - pkt->data;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    av_frame_free(&avctx->coded_frame);
    return 0;
}

AVCodec ff_sgi_encoder = {
    .name           = "sgi",
    .long_name      = NULL_IF_CONFIG_SMALL("SGI image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SGI,
    .init           = encode_init,
    .encode2        = encode_frame,
    .close          = encode_close,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGBA, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE
    },
};
