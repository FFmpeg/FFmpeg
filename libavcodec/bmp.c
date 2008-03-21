/*
 * BMP image format decoder
 * Copyright (c) 2005 Mans Rullgard
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
#include "bmp.h"

static av_cold int bmp_decode_init(AVCodecContext *avctx){
    BMPContext *s = avctx->priv_data;

    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame = (AVFrame*)&s->picture;

    return 0;
}

static int bmp_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            const uint8_t *buf, int buf_size)
{
    BMPContext *s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame *p = &s->picture;
    unsigned int fsize, hsize;
    int width, height;
    unsigned int depth;
    BiCompression comp;
    unsigned int ihsize;
    int i, j, n, linesize;
    uint32_t rgb[3];
    uint8_t *ptr;
    int dsize;
    const uint8_t *buf0 = buf;

    if(buf_size < 14){
        av_log(avctx, AV_LOG_ERROR, "buf size too small (%d)\n", buf_size);
        return -1;
    }

    if(bytestream_get_byte(&buf) != 'B' ||
       bytestream_get_byte(&buf) != 'M') {
        av_log(avctx, AV_LOG_ERROR, "bad magic number\n");
        return -1;
    }

    fsize = bytestream_get_le32(&buf);
    if(buf_size < fsize){
        av_log(avctx, AV_LOG_ERROR, "not enough data (%d < %d)\n",
               buf_size, fsize);
        return -1;
    }

    buf += 2; /* reserved1 */
    buf += 2; /* reserved2 */

    hsize = bytestream_get_le32(&buf); /* header size */
    if(fsize <= hsize){
        av_log(avctx, AV_LOG_ERROR, "not enough data (%d < %d)\n",
               fsize, hsize);
        return -1;
    }

    ihsize = bytestream_get_le32(&buf);       /* more header size */
    if(ihsize + 14 > hsize){
        av_log(avctx, AV_LOG_ERROR, "invalid header size %d\n", hsize);
        return -1;
    }

    width = bytestream_get_le32(&buf);
    height = bytestream_get_le32(&buf);

    if(bytestream_get_le16(&buf) != 1){ /* planes */
        av_log(avctx, AV_LOG_ERROR, "invalid BMP header\n");
        return -1;
    }

    depth = bytestream_get_le16(&buf);

    if(ihsize > 16)
        comp = bytestream_get_le32(&buf);
    else
        comp = BMP_RGB;

    if(comp != BMP_RGB && comp != BMP_BITFIELDS){
        av_log(avctx, AV_LOG_ERROR, "BMP coding %d not supported\n", comp);
        return -1;
    }

    if(comp == BMP_BITFIELDS){
        buf += 20;
        rgb[0] = bytestream_get_le32(&buf);
        rgb[1] = bytestream_get_le32(&buf);
        rgb[2] = bytestream_get_le32(&buf);
    }

    avctx->width = width;
    avctx->height = height > 0? height: -height;

    avctx->pix_fmt = PIX_FMT_NONE;

    switch(depth){
    case 32:
        if(comp == BMP_BITFIELDS){
            rgb[0] = (rgb[0] >> 15) & 3;
            rgb[1] = (rgb[1] >> 15) & 3;
            rgb[2] = (rgb[2] >> 15) & 3;

            if(rgb[0] + rgb[1] + rgb[2] != 3 ||
               rgb[0] == rgb[1] || rgb[0] == rgb[2] || rgb[1] == rgb[2]){
                break;
            }
        } else {
            rgb[0] = 2;
            rgb[1] = 1;
            rgb[2] = 0;
        }

        avctx->pix_fmt = PIX_FMT_BGR24;
        break;
    case 24:
        avctx->pix_fmt = PIX_FMT_BGR24;
        break;
    case 16:
        if(comp == BMP_RGB)
            avctx->pix_fmt = PIX_FMT_RGB555;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "depth %d not supported\n", depth);
        return -1;
    }

    if(avctx->pix_fmt == PIX_FMT_NONE){
        av_log(avctx, AV_LOG_ERROR, "unsupported pixel format\n");
        return -1;
    }

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference = 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type = FF_I_TYPE;
    p->key_frame = 1;

    buf = buf0 + hsize;
    dsize = buf_size - hsize;

    /* Line size in file multiple of 4 */
    n = (avctx->width * (depth / 8) + 3) & ~3;

    if(n * avctx->height > dsize){
        av_log(avctx, AV_LOG_ERROR, "not enough data (%d < %d)\n",
               dsize, n * avctx->height);
        return -1;
    }

    if(height > 0){
        ptr = p->data[0] + (avctx->height - 1) * p->linesize[0];
        linesize = -p->linesize[0];
    } else {
        ptr = p->data[0];
        linesize = p->linesize[0];
    }

    switch(depth){
    case 24:
        for(i = 0; i < avctx->height; i++){
            memcpy(ptr, buf, avctx->width*(depth>>3));
            buf += n;
            ptr += linesize;
        }
        break;
    case 16:
        for(i = 0; i < avctx->height; i++){
            const uint16_t *src = (const uint16_t *) buf;
            uint16_t *dst = (uint16_t *) ptr;

            for(j = 0; j < avctx->width; j++)
                *dst++ = le2me_16(*src++);

            buf += n;
            ptr += linesize;
        }
        break;
    case 32:
        for(i = 0; i < avctx->height; i++){
            const uint8_t *src = buf;
            uint8_t *dst = ptr;

            for(j = 0; j < avctx->width; j++){
                dst[0] = src[rgb[2]];
                dst[1] = src[rgb[1]];
                dst[2] = src[rgb[0]];
                dst += 3;
                src += 4;
            }

            buf += n;
            ptr += linesize;
        }
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "BMP decoder is broken\n");
        return -1;
    }

    *picture = s->picture;
    *data_size = sizeof(AVPicture);

    return buf_size;
}

static av_cold int bmp_decode_end(AVCodecContext *avctx)
{
    BMPContext* c = avctx->priv_data;

    if (c->picture.data[0])
        avctx->release_buffer(avctx, &c->picture);

    return 0;
}

AVCodec bmp_decoder = {
    "bmp",
    CODEC_TYPE_VIDEO,
    CODEC_ID_BMP,
    sizeof(BMPContext),
    bmp_decode_init,
    NULL,
    bmp_decode_end,
    bmp_decode_frame
};
