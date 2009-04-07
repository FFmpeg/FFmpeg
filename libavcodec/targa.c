/*
 * Targa (.tga) image decoder
 * Copyright (c) 2006 Konstantin Shishkov
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

enum TargaCompr{
    TGA_NODATA = 0, // no image data
    TGA_PAL    = 1, // palettized
    TGA_RGB    = 2, // true-color
    TGA_BW     = 3, // black & white or grayscale
    TGA_RLE    = 8, // flag pointing that data is RLE-coded
};

typedef struct TargaContext {
    AVFrame picture;

    int width, height;
    int bpp;
    int color_type;
    int compression_type;
} TargaContext;

static void targa_decode_rle(AVCodecContext *avctx, TargaContext *s, const uint8_t *src, uint8_t *dst, int w, int h, int stride, int bpp)
{
    int i, x, y;
    int depth = (bpp + 1) >> 3;
    int type, count;
    int diff;

    diff = stride - w * depth;
    x = y = 0;
    while(y < h){
        type = *src++;
        count = (type & 0x7F) + 1;
        type &= 0x80;
        if((x + count > w) && (x + count + 1 > (h - y) * w)){
            av_log(avctx, AV_LOG_ERROR, "Packet went out of bounds: position (%i,%i) size %i\n", x, y, count);
            return;
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
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    TargaContext * const s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    uint8_t *dst;
    int stride;
    int idlen, pal, compr, x, y, w, h, bpp, flags;
    int first_clr, colors, csize;

    /* parse image header */
    idlen = *buf++;
    pal = *buf++;
    compr = *buf++;
    first_clr = AV_RL16(buf); buf += 2;
    colors = AV_RL16(buf); buf += 2;
    csize = *buf++;
    x = AV_RL16(buf); buf += 2;
    y = AV_RL16(buf); buf += 2;
    w = AV_RL16(buf); buf += 2;
    h = AV_RL16(buf); buf += 2;
    bpp = *buf++;
    flags = *buf++;
    //skip identifier if any
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

    if(avcodec_check_dimensions(avctx, w, h))
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

    if(avctx->pix_fmt == PIX_FMT_PAL8 && avctx->palctrl){
        memcpy(p->data[1], avctx->palctrl->palette, AVPALETTE_SIZE);
        if(avctx->palctrl->palette_changed){
            p->palette_has_changed = 1;
            avctx->palctrl->palette_changed = 0;
        }
    }
    if(colors){
        if((colors + first_clr) > 256){
            av_log(avctx, AV_LOG_ERROR, "Incorrect palette: %i colors with offset %i\n", colors, first_clr);
            return -1;
        }
        if(csize != 24){
            av_log(avctx, AV_LOG_ERROR, "Palette entry size %i bits is not supported\n", csize);
            return -1;
        }
        if(avctx->pix_fmt != PIX_FMT_PAL8)//should not occur but skip palette anyway
            buf += colors * ((csize + 1) >> 3);
        else{
            int r, g, b, t;
            int32_t *pal = ((int32_t*)p->data[1]) + first_clr;
            for(t = 0; t < colors; t++){
                r = *buf++;
                g = *buf++;
                b = *buf++;
                *pal++ = (b << 16) | (g << 8) | r;
            }
            p->palette_has_changed = 1;
            avctx->palctrl->palette_changed = 0;
        }
    }
    if((compr & (~TGA_RLE)) == TGA_NODATA)
        memset(p->data[0], 0, p->linesize[0] * s->height);
    else{
        if(compr & TGA_RLE)
            targa_decode_rle(avctx, s, buf, dst, avctx->width, avctx->height, stride, bpp);
        else{
            for(y = 0; y < s->height; y++){
#ifdef WORDS_BIGENDIAN
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
                    memcpy(dst, buf, s->width * ((s->bpp + 1) >> 3));

                dst += stride;
                buf += s->width * ((s->bpp + 1) >> 3);
            }
        }
    }

    *picture= *(AVFrame*)&s->picture;
    *data_size = sizeof(AVPicture);

    return buf_size;
}

static av_cold int targa_init(AVCodecContext *avctx){
    TargaContext *s = avctx->priv_data;

    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame= (AVFrame*)&s->picture;
    s->picture.data[0] = NULL;

    return 0;
}

static av_cold int targa_end(AVCodecContext *avctx){
    TargaContext *s = avctx->priv_data;

    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

AVCodec targa_decoder = {
    "targa",
    CODEC_TYPE_VIDEO,
    CODEC_ID_TARGA,
    sizeof(TargaContext),
    targa_init,
    NULL,
    targa_end,
    decode_frame,
    0,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("Truevision Targa image"),
};
