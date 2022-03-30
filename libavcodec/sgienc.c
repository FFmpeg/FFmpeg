/*
 * SGI image encoder
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

#include "libavutil/opt.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"
#include "sgi.h"
#include "rle.h"

#define SGI_SINGLE_CHAN 2
#define SGI_MULTI_CHAN 3

typedef struct SgiContext {
    AVClass *class;

    int rle;
} SgiContext;

static av_cold int encode_init(AVCodecContext *avctx)
{
    if (avctx->width > 65535 || avctx->height > 65535) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported resolution %dx%d. "
               "SGI does not support resolutions above 65535x65535\n",
               avctx->width, avctx->height);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int sgi_rle_encode(PutByteContext *pbc, const uint8_t *src,
                          int w, int bpp)
{
    int val, count, x, start = bytestream2_tell_p(pbc);
    void (*bytestream2_put)(PutByteContext *, unsigned int);

    if (bpp == 1)
        bytestream2_put = bytestream2_put_byte;
    else
        bytestream2_put = bytestream2_put_be16;

    for (x = 0; x < w; x += count) {
        /* see if we can encode the next set of pixels with RLE */
        count = ff_rle_count_pixels(src, w - x, bpp, 1);
        if (count > 1) {
            if (bytestream2_get_bytes_left_p(pbc) < bpp * 2)
                return AVERROR_INVALIDDATA;

            val = bpp == 1 ? *src : AV_RB16(src);
            bytestream2_put(pbc, count);
            bytestream2_put(pbc, val);
        } else {
            int i;
            /* fall back on uncompressed */
            count = ff_rle_count_pixels(src, w - x, bpp, 0);
            if (bytestream2_get_bytes_left_p(pbc) < bpp * (count + 1))
                return AVERROR_INVALIDDATA;

            bytestream2_put(pbc, count + 0x80);
            for (i = 0; i < count; i++) {
                val = bpp == 1 ? src[i] : AV_RB16(src + i * bpp);
                bytestream2_put(pbc, val);
            }
        }

        src += count * bpp;
    }

    return bytestream2_tell_p(pbc) - start;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *frame, int *got_packet)
{
    SgiContext *s = avctx->priv_data;
    const AVFrame * const p = frame;
    PutByteContext pbc;
    uint8_t *in_buf, *encode_buf;
    int x, y, z, length, tablesize, ret, i;
    unsigned int width, height, depth, dimension;
    unsigned int bytes_per_channel, pixmax, put_be;

    width  = avctx->width;
    height = avctx->height;
    bytes_per_channel = 1;
    pixmax = 0xFF;
    put_be = HAVE_BIGENDIAN;

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
    case AV_PIX_FMT_GRAY16LE:
        put_be = !HAVE_BIGENDIAN;
    case AV_PIX_FMT_GRAY16BE:
        bytes_per_channel = 2;
        pixmax = 0xFFFF;
        dimension = SGI_SINGLE_CHAN;
        depth     = SGI_GRAYSCALE;
        break;
    case AV_PIX_FMT_RGB48LE:
        put_be = !HAVE_BIGENDIAN;
    case AV_PIX_FMT_RGB48BE:
        bytes_per_channel = 2;
        pixmax = 0xFFFF;
        dimension = SGI_MULTI_CHAN;
        depth     = SGI_RGB;
        break;
    case AV_PIX_FMT_RGBA64LE:
        put_be = !HAVE_BIGENDIAN;
    case AV_PIX_FMT_RGBA64BE:
        bytes_per_channel = 2;
        pixmax = 0xFFFF;
        dimension = SGI_MULTI_CHAN;
        depth     = SGI_RGBA;
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    tablesize = depth * height * 4;
    length = SGI_HEADER_SIZE;
    if (!s->rle)
        length += depth * height * width;
    else // assume sgi_rle_encode() produces at most 2x size of input
        length += tablesize * 2 + depth * height * (2 * width + 1);

    if ((ret = ff_alloc_packet(avctx, pkt, bytes_per_channel * length)) < 0)
        return ret;

    bytestream2_init_writer(&pbc, pkt->data, pkt->size);

    /* Encode header. */
    bytestream2_put_be16(&pbc, SGI_MAGIC);
    bytestream2_put_byte(&pbc, s->rle); /* RLE 1 - VERBATIM 0 */
    bytestream2_put_byte(&pbc, bytes_per_channel);
    bytestream2_put_be16(&pbc, dimension);
    bytestream2_put_be16(&pbc, width);
    bytestream2_put_be16(&pbc, height);
    bytestream2_put_be16(&pbc, depth);

    bytestream2_put_be32(&pbc, 0L); /* pixmin */
    bytestream2_put_be32(&pbc, pixmax);
    bytestream2_put_be32(&pbc, 0L); /* dummy */

    /* name */
    for (i = 0; i < 80; i++)
        bytestream2_put_byte(&pbc, 0L);

    /* colormap */
    bytestream2_put_be32(&pbc, 0L);

    /* The rest of the 512 byte header is unused. */
    for (i = 0; i < 404; i++)
        bytestream2_put_byte(&pbc, 0L);

    if (s->rle) {
        PutByteContext taboff_pcb, tablen_pcb;

        /* Skip RLE offset table. */
        bytestream2_init_writer(&taboff_pcb, pbc.buffer, tablesize);
        bytestream2_skip_p(&pbc, tablesize);

        /* Skip RLE length table. */
        bytestream2_init_writer(&tablen_pcb, pbc.buffer, tablesize);
        bytestream2_skip_p(&pbc, tablesize);

        /* Make an intermediate consecutive buffer. */
        if (!(encode_buf = av_malloc(width * bytes_per_channel)))
            return AVERROR(ENOMEM);

        for (z = 0; z < depth; z++) {
            in_buf = p->data[0] + p->linesize[0] * (height - 1) + z * bytes_per_channel;

            for (y = 0; y < height; y++) {
                bytestream2_put_be32(&taboff_pcb, bytestream2_tell_p(&pbc));

                for (x = 0; x < width * bytes_per_channel; x += bytes_per_channel)
                    if (bytes_per_channel == 1) {
                        encode_buf[x]     = in_buf[depth * x];
                    } else if (HAVE_BIGENDIAN ^ put_be) {
                        encode_buf[x + 1] = in_buf[depth * x];
                        encode_buf[x]     = in_buf[depth * x + 1];
                    } else {
                        encode_buf[x]     = in_buf[depth * x];
                        encode_buf[x + 1] = in_buf[depth * x + 1];
                    }

                length = sgi_rle_encode(&pbc, encode_buf, width,
                                        bytes_per_channel);
                if (length < 1) {
                    av_free(encode_buf);
                    return AVERROR_INVALIDDATA;
                }

                bytestream2_put_be32(&tablen_pcb, length);
                in_buf -= p->linesize[0];
            }
        }

        av_free(encode_buf);
    } else {
        for (z = 0; z < depth; z++) {
            in_buf = p->data[0] + p->linesize[0] * (height - 1) + z * bytes_per_channel;

            for (y = 0; y < height; y++) {
                for (x = 0; x < width * depth; x += depth)
                    if (bytes_per_channel == 1)
                        bytestream2_put_byte(&pbc, in_buf[x]);
                    else
                        if (put_be)
                            bytestream2_put_be16(&pbc, ((uint16_t *)in_buf)[x]);
                        else
                            bytestream2_put_le16(&pbc, ((uint16_t *)in_buf)[x]);

                in_buf -= p->linesize[0];
            }
        }
    }

    /* total length */
    pkt->size   = bytestream2_tell_p(&pbc);
    *got_packet = 1;

    return 0;
}

#define OFFSET(x) offsetof(SgiContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "rle", "Use run-length compression", OFFSET(rle), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, VE },

    { NULL },
};

static const AVClass sgi_class = {
    .class_name = "sgi",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_sgi_encoder = {
    .p.name    = "sgi",
    .p.long_name = NULL_IF_CONFIG_SMALL("SGI image"),
    .p.type    = AVMEDIA_TYPE_VIDEO,
    .p.id      = AV_CODEC_ID_SGI,
    .priv_data_size = sizeof(SgiContext),
    .p.priv_class = &sgi_class,
    .init      = encode_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    .p.pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGBA,
        AV_PIX_FMT_RGB48LE, AV_PIX_FMT_RGB48BE,
        AV_PIX_FMT_RGBA64LE, AV_PIX_FMT_RGBA64BE,
        AV_PIX_FMT_GRAY16LE, AV_PIX_FMT_GRAY16BE, AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    },
    .caps_internal = FF_CODEC_CAP_INIT_THREADSAFE,
};
