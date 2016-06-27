/*
 * Targa (.tga) image encoder
 * Copyright (c) 2007 Bobby Bingham
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

#include <string.h>

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "internal.h"
#include "rle.h"
#include "targa.h"

typedef struct TargaContext {
    AVClass *class;

    int rle;
} TargaContext;

/**
 * RLE compress the image, with maximum size of out_size
 * @param outbuf Output buffer
 * @param out_size Maximum output size
 * @param pic Image to compress
 * @param bpp Bytes per pixel
 * @param w Image width
 * @param h Image height
 * @return Size of output in bytes, or -1 if larger than out_size
 */
static int targa_encode_rle(uint8_t *outbuf, int out_size, const AVFrame *pic,
                            int bpp, int w, int h)
{
    int y,ret;
    uint8_t *out;

    out = outbuf;

    for(y = 0; y < h; y ++) {
        ret = ff_rle_encode(out, out_size, pic->data[0] + pic->linesize[0] * y, bpp, w, 0x7f, 0, -1, 0);
        if(ret == -1){
            return -1;
        }
        out+= ret;
        out_size -= ret;
    }

    return out - outbuf;
}

static int targa_encode_normal(uint8_t *outbuf, const AVFrame *pic, int bpp, int w, int h)
{
    int i, n = bpp * w;
    uint8_t *out = outbuf;
    uint8_t *ptr = pic->data[0];

    for(i=0; i < h; i++) {
        memcpy(out, ptr, n);
        out += n;
        ptr += pic->linesize[0];
    }

    return out - outbuf;
}

static int targa_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                              const AVFrame *p, int *got_packet)
{
    TargaContext *s = avctx->priv_data;
    int bpp, picsize, datasize = -1, ret;
    uint8_t *out;

    picsize = av_image_get_buffer_size(avctx->pix_fmt,
                                       avctx->width, avctx->height, 1);
    if ((ret = ff_alloc_packet(pkt, picsize + 45)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return ret;
    }

    /* zero out the header and only set applicable fields */
    memset(pkt->data, 0, 12);
    AV_WL16(pkt->data+12, avctx->width);
    AV_WL16(pkt->data+14, avctx->height);
    /* image descriptor byte: origin is always top-left, bits 0-3 specify alpha */
    pkt->data[17] = 0x20 | (avctx->pix_fmt == AV_PIX_FMT_BGRA ? 8 : 0);

    switch(avctx->pix_fmt) {
    case AV_PIX_FMT_GRAY8:
        pkt->data[2]  = TGA_BW;     /* uncompressed grayscale image */
        pkt->data[16] = 8;          /* bpp */
        break;
    case AV_PIX_FMT_RGB555LE:
        pkt->data[2]  = TGA_RGB;    /* uncompresses true-color image */
        pkt->data[16] = 16;         /* bpp */
        break;
    case AV_PIX_FMT_BGR24:
        pkt->data[2]  = TGA_RGB;    /* uncompressed true-color image */
        pkt->data[16] = 24;         /* bpp */
        break;
    case AV_PIX_FMT_BGRA:
        pkt->data[2]  = TGA_RGB;    /* uncompressed true-color image */
        pkt->data[16] = 32;         /* bpp */
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Pixel format '%s' not supported.\n",
               av_get_pix_fmt_name(avctx->pix_fmt));
        return AVERROR(EINVAL);
    }
    bpp = pkt->data[16] >> 3;

    out = pkt->data + 18;  /* skip past the header we just output */

#if FF_API_CODER_TYPE
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->coder_type == FF_CODER_TYPE_RAW)
        s->rle = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    /* try RLE compression */
    if (s->rle)
        datasize = targa_encode_rle(out, picsize, p, bpp, avctx->width, avctx->height);

    /* if that worked well, mark the picture as RLE compressed */
    if(datasize >= 0)
        pkt->data[2] |= 8;

    /* if RLE didn't make it smaller, go back to no compression */
    else datasize = targa_encode_normal(out, p, bpp, avctx->width, avctx->height);

    out += datasize;

    /* The standard recommends including this section, even if we don't use
     * any of the features it affords. TODO: take advantage of the pixel
     * aspect ratio and encoder ID fields available? */
    memcpy(out, "\0\0\0\0\0\0\0\0TRUEVISION-XFILE.", 26);

    pkt->size   = out + 26 - pkt->data;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static av_cold int targa_encode_init(AVCodecContext *avctx)
{
    if (avctx->width > 0xffff || avctx->height > 0xffff) {
        av_log(avctx, AV_LOG_ERROR, "image dimensions too large\n");
        return AVERROR(EINVAL);
    }

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->key_frame = 1;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    return 0;
}

#define OFFSET(x) offsetof(TargaContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "rle", "Use run-length compression", OFFSET(rle), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, VE },

    { NULL },
};

static const AVClass targa_class = {
    .class_name = "targa",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_targa_encoder = {
    .name           = "targa",
    .long_name      = NULL_IF_CONFIG_SMALL("Truevision Targa image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_TARGA,
    .priv_data_size = sizeof(TargaContext),
    .priv_class     = &targa_class,
    .init           = targa_encode_init,
    .encode2        = targa_encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_BGR24, AV_PIX_FMT_BGRA, AV_PIX_FMT_RGB555LE, AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    },
};
