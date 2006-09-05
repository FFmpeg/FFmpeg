/*
 * VMware Screen Codec (VMnc) decoder
 * Copyright (c) 2006 Konstantin Shishkov
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
 *
 */

/**
 * @file vmnc.c
 * VMware Screen Codec (VMnc) decoder
 * As Alex Beregszaszi discovered, this is effectively RFB data dump
 */

#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "avcodec.h"

#define MAGIC_WMVi 0x574D5669

enum HexTile_Flags {
    HT_RAW =  1, // tile is raw
    HT_BKG =  2, // background color is present
    HT_FG  =  4, // foreground color is present
    HT_SUB =  8, // subrects are present
    HT_CLR = 16  // each subrect has own color
};

/*
 * Decoder context
 */
typedef struct VmncContext {
    AVCodecContext *avctx;
    AVFrame pic;

    int bpp;
    int bpp2;
    int bigendian;
    uint8_t pal[768];
    int width, height;
} VmncContext;

/* read pixel value from stream */
static always_inline int vmnc_get_pixel(uint8_t* buf, int bpp, int be) {
    switch(bpp * 2 + be) {
    case 2:
    case 3: return *buf;
    case 4: return LE_16(buf);
    case 5: return BE_16(buf);
    case 8: return LE_32(buf);
    case 9: return BE_32(buf);
    default: return 0;
    }
}

/* fill rectangle with given colour */
static always_inline void paint_rect(uint8_t *dst, int dx, int dy, int w, int h, int color, int bpp, int stride)
{
    int i, j;
    dst += dx * bpp + dy * stride;
    if(bpp == 1){
        for(j = 0; j < h; j++) {
            memset(dst, color, w);
            dst += stride;
        }
    }else if(bpp == 2){
        uint16_t* dst2;
        for(j = 0; j < h; j++) {
            dst2 = (uint16_t*)dst;
            for(i = 0; i < w; i++) {
                *dst2++ = color;
            }
            dst += stride;
        }
    }else if(bpp == 4){
        uint32_t* dst2;
        for(j = 0; j < h; j++) {
            dst2 = (uint32_t*)dst;
            for(i = 0; i < w; i++) {
                dst2[i] = color;
            }
            dst += stride;
        }
    }
}

static always_inline void paint_raw(uint8_t *dst, int w, int h, uint8_t* src, int bpp, int be, int stride)
{
    int i, j, p;
    for(j = 0; j < h; j++) {
        for(i = 0; i < w; i++) {
            p = vmnc_get_pixel(src, bpp, be);
            src += bpp;
            memcpy(dst + i*bpp, &p, bpp);
        }
        dst += stride;
    }
}

static int decode_hextile(VmncContext *c, uint8_t* dst, uint8_t* src, int w, int h, int stride)
{
    int i, j, k;
    int bg = 0, fg = 0, rects, color, flags, xy, wh;
    const int bpp = c->bpp2;
    uint8_t *dst2;
    int bw = 16, bh = 16;
    uint8_t *ssrc=src;

    for(j = 0; j < h; j += 16) {
        dst2 = dst;
        bw = 16;
        if(j + 16 > h) bh = h - j;
        for(i = 0; i < w; i += 16, dst2 += 16 * bpp) {
            if(i + 16 > w) bw = w - i;
            flags = *src++;
            if(flags & HT_RAW) {
                paint_raw(dst2, bw, bh, src, bpp, c->bigendian, stride);
            } else {
                if(flags & HT_BKG) {
                    bg = vmnc_get_pixel(src, bpp, c->bigendian); src += bpp;
                }
                if(flags & HT_FG) {
                    fg = vmnc_get_pixel(src, bpp, c->bigendian); src += bpp;
                }
                rects = 0;
                if(flags & HT_SUB)
                    rects = *src++;
                color = (flags & HT_CLR);

                paint_rect(dst2, 0, 0, bw, bh, bg, bpp, stride);

                for(k = 0; k < rects; k++) {
                    if(color) {
                        fg = vmnc_get_pixel(src, bpp, c->bigendian); src += bpp;
                    }
                    xy = *src++;
                    wh = *src++;
                    paint_rect(dst2, xy >> 4, xy & 0xF, (wh>>4)+1, (wh & 0xF)+1, fg, bpp, stride);
                }
            }
        }
        dst += stride * 16;
    }
    return src - ssrc;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, uint8_t *buf, int buf_size)
{
    VmncContext * const c = (VmncContext *)avctx->priv_data;
    uint8_t *outptr;
    uint8_t *src = buf;
    int t, dx, dy, w, h, enc, chunks, res;

    c->pic.reference = 1;
    c->pic.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if(avctx->reget_buffer(avctx, &c->pic) < 0){
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    t = BE_32(src);
    src += 4;

    chunks = t & 0xFF;
    if(chunks > 8) {
        av_log(avctx, AV_LOG_ERROR, "Frame decoding is not possible. Please report sample to developers.\n");
        return -1;
    }
    if(chunks == 8) {
        int w, h, depth;
        c->pic.key_frame = 1;
        c->pic.pict_type = FF_I_TYPE;

        /* parse ServerInitialization struct */
        src += 4;
        w = BE_16(src); src += 2;
        h = BE_16(src); src += 2;
        t = BE_32(src); src += 4;
        if(t != MAGIC_WMVi) {
            av_log(avctx, AV_LOG_INFO, "Invalid header: magic not found\n");
            return -1;
        }
        depth = *src++;
        if(depth != c->bpp) {
            av_log(avctx, AV_LOG_INFO, "Depth mismatch. Container %i bpp, Frame data: %i bpp\n", c->bpp, depth);
        }
        src++;
        c->bigendian = *src++;
        if(c->bigendian & (~1)) {
            av_log(avctx, AV_LOG_INFO, "Invalid header: bigendian flag = %i\n", c->bigendian);
            return -1;
        }
        //skip pixel format data
        src += 13;
        chunks = 1; // there should be one chunk with the whole frame, rest could be ignored
    } else {
        c->pic.key_frame = 0;
        c->pic.pict_type = FF_P_TYPE;
    }
    while(chunks--) {
        // decode FramebufferUpdate struct
        dx = BE_16(src); src += 2;
        dy = BE_16(src); src += 2;
        w  = BE_16(src); src += 2;
        h  = BE_16(src); src += 2;
        if((dx + w > c->width) || (dy + h > c->height)) {
            av_log(avctx, AV_LOG_ERROR, "Incorrect frame size: %ix%i+%ix%i of %ix%i\n", w, h, dx, dy, c->width, c->height);
            return -1;
        }
        enc = BE_32(src); src += 4;
        if(enc != 0x00000005) {
            av_log(avctx, AV_LOG_ERROR, "Only hextile decoding is supported for now\n");
            switch(enc) {
            case 0:
                av_log(avctx, AV_LOG_INFO, "And this is raw encoding\n");
                break;
            case 1:
                av_log(avctx, AV_LOG_INFO, "And this is CopyRect encoding\n");
                break;
            case 2:
                av_log(avctx, AV_LOG_INFO, "And this is RRE encoding\n");
                break;
            case 3:
                av_log(avctx, AV_LOG_INFO, "And this is CoRRE encoding\n");
                break;
            default:
                av_log(avctx, AV_LOG_INFO, "And this is unknown encoding (%i)\n", enc);
            }
            return -1;
        }
        outptr = c->pic.data[0] + dx * c->bpp2 + dy * c->pic.linesize[0];
        res = decode_hextile(c, outptr, src, w, h, c->pic.linesize[0]);
        if(res < 0)
            return -1;
        src += res;
    }
    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = c->pic;

    /* always report that the buffer was completely consumed */
    return buf_size;
}



/*
 *
 * Init VMnc decoder
 *
 */
static int decode_init(AVCodecContext *avctx)
{
    VmncContext * const c = (VmncContext *)avctx->priv_data;

    c->avctx = avctx;
    avctx->has_b_frames = 0;

    c->pic.data[0] = NULL;
    c->width = avctx->width;
    c->height = avctx->height;

    if (avcodec_check_dimensions(avctx, avctx->height, avctx->width) < 0) {
        return 1;
    }
    c->bpp = avctx->bits_per_sample;
    c->bpp2 = c->bpp/8;

    switch(c->bpp){
    case 8:
        avctx->pix_fmt = PIX_FMT_PAL8;
        break;
    case 16:
        avctx->pix_fmt = PIX_FMT_RGB555;
        break;
    case 32:
        avctx->pix_fmt = PIX_FMT_RGB32;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported bitdepth %i\n", c->bpp);
    }

    return 0;
}



/*
 *
 * Uninit VMnc decoder
 *
 */
static int decode_end(AVCodecContext *avctx)
{
    VmncContext * const c = (VmncContext *)avctx->priv_data;

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);

    return 0;
}

AVCodec vmnc_decoder = {
    "VMware video",
    CODEC_TYPE_VIDEO,
    CODEC_ID_VMNC,
    sizeof(VmncContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame
};

