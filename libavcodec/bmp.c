/*
 * BMP image format decoder
 * Copyright (c) 2005 Mans Rullgard
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
#include "bmp.h"
#include "msrledec.h"

static av_cold int bmp_decode_init(AVCodecContext *avctx){
    BMPContext *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame = &s->picture;

    return 0;
}

static int bmp_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
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
    GetByteContext gb;

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
        av_log(avctx, AV_LOG_ERROR, "not enough data (%d < %d), trying to decode anyway\n",
               buf_size, fsize);
        fsize = buf_size;
    }

    buf += 2; /* reserved1 */
    buf += 2; /* reserved2 */

    hsize = bytestream_get_le32(&buf); /* header size */
    ihsize = bytestream_get_le32(&buf);       /* more header size */
    if(ihsize + 14 > hsize){
        av_log(avctx, AV_LOG_ERROR, "invalid header size %d\n", hsize);
        return -1;
    }

    /* sometimes file size is set to some headers size, set a real size in that case */
    if(fsize == 14 || fsize == ihsize + 14)
        fsize = buf_size - 2;

    if(fsize <= hsize){
        av_log(avctx, AV_LOG_ERROR, "declared file size is less than header size (%d < %d)\n",
               fsize, hsize);
        return -1;
    }

    switch(ihsize){
    case  40: // windib v3
    case  64: // OS/2 v2
    case 108: // windib v4
    case 124: // windib v5
        width = bytestream_get_le32(&buf);
        height = bytestream_get_le32(&buf);
        break;
    case  12: // OS/2 v1
        width  = bytestream_get_le16(&buf);
        height = bytestream_get_le16(&buf);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "unsupported BMP file, patch welcome\n");
        return -1;
    }

    if(bytestream_get_le16(&buf) != 1){ /* planes */
        av_log(avctx, AV_LOG_ERROR, "invalid BMP header\n");
        return -1;
    }

    depth = bytestream_get_le16(&buf);

    if(ihsize == 40)
        comp = bytestream_get_le32(&buf);
    else
        comp = BMP_RGB;

    if(comp != BMP_RGB && comp != BMP_BITFIELDS && comp != BMP_RLE4 && comp != BMP_RLE8){
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
        else if (comp == BMP_BITFIELDS) {
            if (rgb[0] == 0xF800 && rgb[1] == 0x07E0 && rgb[2] == 0x001F)
               avctx->pix_fmt = PIX_FMT_RGB565;
            else if (rgb[0] == 0x7C00 && rgb[1] == 0x03E0 && rgb[2] == 0x001F)
               avctx->pix_fmt = PIX_FMT_RGB555;
            else if (rgb[0] == 0x0F00 && rgb[1] == 0x00F0 && rgb[2] == 0x000F)
               avctx->pix_fmt = PIX_FMT_RGB444;
            else {
               av_log(avctx, AV_LOG_ERROR, "Unknown bitfields %0X %0X %0X\n", rgb[0], rgb[1], rgb[2]);
               return AVERROR(EINVAL);
            }
        }
        break;
    case 8:
        if(hsize - ihsize - 14 > 0)
            avctx->pix_fmt = PIX_FMT_PAL8;
        else
            avctx->pix_fmt = PIX_FMT_GRAY8;
        break;
    case 1:
    case 4:
        if(hsize - ihsize - 14 > 0){
            avctx->pix_fmt = PIX_FMT_PAL8;
        }else{
            av_log(avctx, AV_LOG_ERROR, "Unknown palette for %d-colour BMP\n", 1<<depth);
            return -1;
        }
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
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    buf = buf0 + hsize;
    dsize = buf_size - hsize;

    /* Line size in file multiple of 4 */
    n = ((avctx->width * depth) / 8 + 3) & ~3;

    if(n * avctx->height > dsize && comp != BMP_RLE4 && comp != BMP_RLE8){
        av_log(avctx, AV_LOG_ERROR, "not enough data (%d < %d)\n",
               dsize, n * avctx->height);
        return -1;
    }

    // RLE may skip decoding some picture areas, so blank picture before decoding
    if(comp == BMP_RLE4 || comp == BMP_RLE8)
        memset(p->data[0], 0, avctx->height * p->linesize[0]);

    if(depth == 4 || depth == 8)
        memset(p->data[1], 0, 1024);

    if(height > 0){
        ptr = p->data[0] + (avctx->height - 1) * p->linesize[0];
        linesize = -p->linesize[0];
    } else {
        ptr = p->data[0];
        linesize = p->linesize[0];
    }

    if(avctx->pix_fmt == PIX_FMT_PAL8){
        int colors = 1 << depth;
        if(ihsize >= 36){
            int t;
            buf = buf0 + 46;
            t = bytestream_get_le32(&buf);
            if(t < 0 || t > (1 << depth)){
                av_log(avctx, AV_LOG_ERROR, "Incorrect number of colors - %X for bitdepth %d\n", t, depth);
            }else if(t){
                colors = t;
            }
        }
        buf = buf0 + 14 + ihsize; //palette location
        if((hsize-ihsize-14) < (colors << 2)){ // OS/2 bitmap, 3 bytes per palette entry
            for(i = 0; i < colors; i++)
                ((uint32_t*)p->data[1])[i] = bytestream_get_le24(&buf);
        }else{
            for(i = 0; i < colors; i++)
                ((uint32_t*)p->data[1])[i] = bytestream_get_le32(&buf);
        }
        buf = buf0 + hsize;
    }
    if(comp == BMP_RLE4 || comp == BMP_RLE8){
        if(height < 0){
            p->data[0] += p->linesize[0] * (avctx->height - 1);
            p->linesize[0] = -p->linesize[0];
        }
        bytestream2_init(&gb, buf, dsize);
        ff_msrle_decode(avctx, (AVPicture*)p, depth, &gb);
        if(height < 0){
            p->data[0] += p->linesize[0] * (avctx->height - 1);
            p->linesize[0] = -p->linesize[0];
        }
    }else{
        switch(depth){
        case 1:
            for (i = 0; i < avctx->height; i++) {
                int j;
                for (j = 0; j < n; j++) {
                    ptr[j*8+0] =  buf[j] >> 7;
                    ptr[j*8+1] = (buf[j] >> 6) & 1;
                    ptr[j*8+2] = (buf[j] >> 5) & 1;
                    ptr[j*8+3] = (buf[j] >> 4) & 1;
                    ptr[j*8+4] = (buf[j] >> 3) & 1;
                    ptr[j*8+5] = (buf[j] >> 2) & 1;
                    ptr[j*8+6] = (buf[j] >> 1) & 1;
                    ptr[j*8+7] =  buf[j]       & 1;
                }
                buf += n;
                ptr += linesize;
            }
            break;
        case 8:
        case 24:
            for(i = 0; i < avctx->height; i++){
                memcpy(ptr, buf, n);
                buf += n;
                ptr += linesize;
            }
            break;
        case 4:
            for(i = 0; i < avctx->height; i++){
                int j;
                for(j = 0; j < n; j++){
                    ptr[j*2+0] = (buf[j] >> 4) & 0xF;
                    ptr[j*2+1] = buf[j] & 0xF;
                }
                buf += n;
                ptr += linesize;
            }
            break;
        case 16:
            for(i = 0; i < avctx->height; i++){
                const uint16_t *src = (const uint16_t *) buf;
                uint16_t *dst = (uint16_t *) ptr;

                for(j = 0; j < avctx->width; j++)
                    *dst++ = av_le2ne16(*src++);

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

AVCodec ff_bmp_decoder = {
    .name           = "bmp",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_BMP,
    .priv_data_size = sizeof(BMPContext),
    .init           = bmp_decode_init,
    .close          = bmp_decode_end,
    .decode         = bmp_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("BMP image"),
};
