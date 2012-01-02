/*
 * Targa (.tga) image decoder
 * Copyright (c) 2006 Konstantin Shishkov
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

#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "targa.h"

typedef struct TargaContext {
    AVFrame picture;

    int width, height;
    int bpp;
    int color_type;
    int compression_type;
} TargaContext;

#define CHECK_BUFFER_SIZE(buf, buf_end, needed, where) \
    if(needed > buf_end - buf){ \
        av_log(avctx, AV_LOG_ERROR, "Problem: unexpected end of data while reading " where "\n"); \
        return -1; \
    } \

static int targa_decode_rle(AVCodecContext *avctx, TargaContext *s, const uint8_t *src, int src_size, uint8_t *dst, int w, int h, int stride, int bpp)
{
    int i, x, y;
    int depth = (bpp + 1) >> 3;
    int type, count;
    int diff;
    const uint8_t *src_end = src + src_size;

    diff = stride - w * depth;
    x = y = 0;
    while(y < h){
        CHECK_BUFFER_SIZE(src, src_end, 1, "image type");
        type = *src++;
        count = (type & 0x7F) + 1;
        type &= 0x80;
        if((x + count > w) && (x + count + 1 > (h - y) * w)){
            av_log(avctx, AV_LOG_ERROR, "Packet went out of bounds: position (%i,%i) size %i\n", x, y, count);
            return -1;
        }
        if(type){
            CHECK_BUFFER_SIZE(src, src_end, depth, "image data");
        }else{
            CHECK_BUFFER_SIZE(src, src_end, count * depth, "image data");
        }
        for(i = 0; i < count; i++){
            switch(depth){
            case 1:
                *dst = *src;
                break;
            case 2:
                *((uint16_t*)dst) = AV_RL16(src);
                break;
            case 3:
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                break;
            case 4:
                *((uint32_t*)dst) = AV_RL32(src);
                break;
            }
            dst += depth;
            if(!type)
                src += depth;

            x++;
            if(x == w){
                x = 0;
                y++;
                dst += diff;
            }
        }
        if(type)
            src += depth;
    }
    return src_size;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    const uint8_t *buf_end = avpkt->data + avpkt->size;
    TargaContext * const s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    uint8_t *dst;
    int stride;
    int idlen, compr, y, w, h, bpp, flags;
    int first_clr, colors, csize;

    /* parse image header */
    CHECK_BUFFER_SIZE(buf, buf_end, 18, "header");
    idlen = *buf++;
    buf++; /* pal */
    compr = *buf++;
    first_clr = AV_RL16(buf); buf += 2;
    colors = AV_RL16(buf); buf += 2;
    csize = *buf++;
    buf += 2; /* x */
    y = AV_RL16(buf); buf += 2;
    w = AV_RL16(buf); buf += 2;
    h = AV_RL16(buf); buf += 2;
    bpp = *buf++;
    flags = *buf++;
    //skip identifier if any
    CHECK_BUFFER_SIZE(buf, buf_end, idlen, "identifiers");
    buf += idlen;
    s->bpp = bpp;
    s->width = w;
    s->height = h;
    switch(s->bpp){
    case 8:
        avctx->pix_fmt = ((compr & (~TGA_RLE)) == TGA_BW) ? PIX_FMT_GRAY8 : PIX_FMT_PAL8;
        break;
    case 15:
        avctx->pix_fmt = PIX_FMT_RGB555;
        break;
    case 16:
        avctx->pix_fmt = PIX_FMT_RGB555;
        break;
    case 24:
        avctx->pix_fmt = PIX_FMT_BGR24;
        break;
    case 32:
        avctx->pix_fmt = PIX_FMT_RGB32;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Bit depth %i is not supported\n", s->bpp);
        return -1;
    }

    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    if(av_image_check_size(w, h, 0, avctx))
        return -1;
    if(w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    if(flags & 0x20){
        dst = p->data[0];
        stride = p->linesize[0];
    }else{ //image is upside-down
        dst = p->data[0] + p->linesize[0] * (h - 1);
        stride = -p->linesize[0];
    }

    if(colors){
        int pal_size, pal_sample_size;
        if((colors + first_clr) > 256){
            av_log(avctx, AV_LOG_ERROR, "Incorrect palette: %i colors with offset %i\n", colors, first_clr);
            return -1;
        }
        switch (csize) {
        case 24: pal_sample_size = 3; break;
        case 16:
        case 15: pal_sample_size = 2; break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Palette entry size %i bits is not supported\n", csize);
            return -1;
        }
        pal_size = colors * pal_sample_size;
        CHECK_BUFFER_SIZE(buf, buf_end, pal_size, "color table");
        if(avctx->pix_fmt != PIX_FMT_PAL8)//should not occur but skip palette anyway
            buf += pal_size;
        else{
            int t;
            uint32_t *pal = ((uint32_t *)p->data[1]) + first_clr;

            switch (pal_sample_size) {
            case 3:
                /* RGB24 */
                for (t = 0; t < colors; t++)
                    *pal++ = bytestream_get_le24(&buf);
                break;
            case 2:
                /* RGB555 */
                for (t = 0; t < colors; t++) {
                    uint32_t v = bytestream_get_le16(&buf);
                    v = ((v & 0x7C00) <<  9) |
                        ((v & 0x03E0) <<  6) |
                        ((v & 0x001F) <<  3);
                    /* left bit replication */
                    v |= (v & 0xE0E0E0U) >> 5;
                    *pal++ = v;
                }
                break;
            }
            p->palette_has_changed = 1;
        }
    }
    if((compr & (~TGA_RLE)) == TGA_NODATA)
        memset(p->data[0], 0, p->linesize[0] * s->height);
    else{
        if(compr & TGA_RLE){
            int res = targa_decode_rle(avctx, s, buf, buf_end - buf, dst, avctx->width, avctx->height, stride, bpp);
            if (res < 0)
                return -1;
            buf += res;
        }else{
            size_t img_size = s->width * ((s->bpp + 1) >> 3);
            CHECK_BUFFER_SIZE(buf, buf_end, img_size, "image data");
            for(y = 0; y < s->height; y++){
#if HAVE_BIGENDIAN
                int x;
                if((s->bpp + 1) >> 3 == 2){
                    uint16_t *dst16 = (uint16_t*)dst;
                    for(x = 0; x < s->width; x++)
                        dst16[x] = AV_RL16(buf + x * 2);
                }else if((s->bpp + 1) >> 3 == 4){
                    uint32_t *dst32 = (uint32_t*)dst;
                    for(x = 0; x < s->width; x++)
                        dst32[x] = AV_RL32(buf + x * 4);
                }else
#endif
                    memcpy(dst, buf, img_size);

                dst += stride;
                buf += img_size;
            }
        }
    }

    *picture= *(AVFrame*)&s->picture;
    *data_size = sizeof(AVPicture);

    return avpkt->size;
}

static av_cold int targa_init(AVCodecContext *avctx){
    TargaContext *s = avctx->priv_data;

    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame= (AVFrame*)&s->picture;

    return 0;
}

static av_cold int targa_end(AVCodecContext *avctx){
    TargaContext *s = avctx->priv_data;

    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

AVCodec ff_targa_decoder = {
    .name           = "targa",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_TARGA,
    .priv_data_size = sizeof(TargaContext),
    .init           = targa_init,
    .close          = targa_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Truevision Targa image"),
};
