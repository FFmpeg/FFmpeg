/*
 * Targa (.tga) image encoder
 * Copyright (c) 2007 Bobby Bingham
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

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "rle.h"

typedef struct TargaContext {
    AVFrame picture;
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
static int targa_encode_rle(uint8_t *outbuf, int out_size, AVFrame *pic,
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

static int targa_encode_normal(uint8_t *outbuf, AVFrame *pic, int bpp, int w, int h)
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

static int targa_encode_frame(AVCodecContext *avctx,
                              unsigned char *outbuf,
                              int buf_size, void *data){
    AVFrame *p = data;
    int bpp, picsize, datasize = -1;
    uint8_t *out;

    if(avctx->width > 0xffff || avctx->height > 0xffff) {
        av_log(avctx, AV_LOG_ERROR, "image dimensions too large\n");
        return -1;
    }
    picsize = avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height);
    if(buf_size < picsize + 45) {
        av_log(avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }

    p->pict_type= FF_I_TYPE;
    p->key_frame= 1;

    /* zero out the header and only set applicable fields */
    memset(outbuf, 0, 12);
    AV_WL16(outbuf+12, avctx->width);
    AV_WL16(outbuf+14, avctx->height);
    outbuf[17] = 0x20;           /* origin is top-left. no alpha */

    /* TODO: support alpha channel */
    switch(avctx->pix_fmt) {
    case PIX_FMT_GRAY8:
        outbuf[2] = 3;           /* uncompressed grayscale image */
        outbuf[16] = 8;          /* bpp */
        break;
    case PIX_FMT_RGB555LE:
        outbuf[2] = 2;           /* uncompresses true-color image */
        outbuf[16] = 16;         /* bpp */
        break;
    case PIX_FMT_BGR24:
        outbuf[2] = 2;           /* uncompressed true-color image */
        outbuf[16] = 24;         /* bpp */
        break;
    default:
        return -1;
    }
    bpp = outbuf[16] >> 3;

    out = outbuf + 18;  /* skip past the header we just output */

    /* try RLE compression */
    if (avctx->coder_type != FF_CODER_TYPE_RAW)
        datasize = targa_encode_rle(out, picsize, p, bpp, avctx->width, avctx->height);

    /* if that worked well, mark the picture as RLE compressed */
    if(datasize >= 0)
        outbuf[2] |= 8;

    /* if RLE didn't make it smaller, go back to no compression */
    else datasize = targa_encode_normal(out, p, bpp, avctx->width, avctx->height);

    out += datasize;

    /* The standard recommends including this section, even if we don't use
     * any of the features it affords. TODO: take advantage of the pixel
     * aspect ratio and encoder ID fields available? */
    memcpy(out, "\0\0\0\0\0\0\0\0TRUEVISION-XFILE.", 26);

    return out + 26 - outbuf;
}

static av_cold int targa_encode_init(AVCodecContext *avctx)
{
    TargaContext *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->picture);
    s->picture.key_frame= 1;
    avctx->coded_frame= &s->picture;

    return 0;
}

AVCodec targa_encoder = {
    .name = "targa",
    .type = AVMEDIA_TYPE_VIDEO,
    .id = CODEC_ID_TARGA,
    .priv_data_size = sizeof(TargaContext),
    .init = targa_encode_init,
    .encode = targa_encode_frame,
    .pix_fmts= (const enum PixelFormat[]){PIX_FMT_BGR24, PIX_FMT_RGB555LE, PIX_FMT_GRAY8, PIX_FMT_NONE},
    .long_name= NULL_IF_CONFIG_SMALL("Truevision Targa image"),
};
