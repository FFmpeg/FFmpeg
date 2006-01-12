/*
 * BMP image format
 * Copyright (c) 2005 Mans Rullgard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avcodec.h"
#include "bitstream.h"
#include "bswap.h"

typedef struct BMPContext {
    AVFrame picture;
} BMPContext;

#define BMP_RGB       0
#define BMP_RLE8      1
#define BMP_RLE4      2
#define BMP_BITFIELDS 3

#define read16(bits) bswap_16(get_bits(bits, 16))
#define read32(bits) bswap_32(get_bits_long(bits, 32))

static int bmp_decode_init(AVCodecContext *avctx){
    BMPContext *s = avctx->priv_data;

    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame = (AVFrame*)&s->picture;

    return 0;
}

static int bmp_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            uint8_t *buf, int buf_size)
{
    BMPContext *s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame *p = &s->picture;
    GetBitContext bits;
    unsigned int fsize, hsize;
    int width, height;
    unsigned int depth;
    unsigned int comp;
    unsigned int ihsize;
    int i, j, n, linesize;
    uint32_t rgb[3];
    uint8_t *ptr;
    int dsize;

    if(buf_size < 14){
        av_log(avctx, AV_LOG_ERROR, "buf size too small (%d)\n", buf_size);
        return -1;
    }

    init_get_bits(&bits, buf, buf_size);

    if(get_bits(&bits, 16) != 0x424d){ /* 'BM' */
        av_log(avctx, AV_LOG_ERROR, "bad magic number\n");
        return -1;
    }

    fsize = read32(&bits);
    if(buf_size < fsize){
        av_log(avctx, AV_LOG_ERROR, "not enough data (%d < %d)\n",
               buf_size, fsize);
        return -1;
    }

    skip_bits(&bits, 16);       /* reserved1 */
    skip_bits(&bits, 16);       /* reserved2 */

    hsize = read32(&bits); /* header size */
    if(fsize <= hsize){
        av_log(avctx, AV_LOG_ERROR, "not enough data (%d < %d)\n",
               fsize, hsize);
        return -1;
    }

    ihsize = read32(&bits);       /* more header size */
    if(ihsize + 14 > hsize){
        av_log(avctx, AV_LOG_ERROR, "invalid header size %d\n", hsize);
        return -1;
    }

    width = read32(&bits);
    height = read32(&bits);

    if(read16(&bits) != 1){ /* planes */
        av_log(avctx, AV_LOG_ERROR, "invalid BMP header\n");
        return -1;
    }

    depth = read16(&bits);

    if(ihsize > 16)
        comp = read32(&bits);
    else
        comp = BMP_RGB;

    if(comp != BMP_RGB && comp != BMP_BITFIELDS){
        av_log(avctx, AV_LOG_ERROR, "BMP coding %d not supported\n", comp);
        return -1;
    }

    if(comp == BMP_BITFIELDS){
        skip_bits(&bits, 20 * 8);
        rgb[0] = read32(&bits);
        rgb[1] = read32(&bits);
        rgb[2] = read32(&bits);
    }

    avctx->codec_id = CODEC_ID_BMP;
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

    p->reference = 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type = FF_I_TYPE;
    p->key_frame = 1;

    buf += hsize;
    dsize = buf_size - hsize;

    n = avctx->width * (depth / 8);

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
            memcpy(ptr, buf, n);
            buf += n;
            ptr += linesize;
        }
        break;
    case 16:
        for(i = 0; i < avctx->height; i++){
            uint16_t *src = (uint16_t *) buf;
            uint16_t *dst = (uint16_t *) ptr;

            for(j = 0; j < avctx->width; j++)
                *dst++ = le2me_16(*src++);

            buf += n;
            ptr += linesize;
        }
        break;
    case 32:
        for(i = 0; i < avctx->height; i++){
            uint8_t *src = buf;
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

AVCodec bmp_decoder = {
    "bmp",
    CODEC_TYPE_VIDEO,
    CODEC_ID_BMP,
    sizeof(BMPContext),
    bmp_decode_init,
    NULL,
    NULL,
    bmp_decode_frame
};
