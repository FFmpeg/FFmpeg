/*
 * VMware Screen Codec (VMnc) decoder
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

/**
 * @file
 * VMware Screen Codec (VMnc) decoder
 * As Alex Beregszaszi discovered, this is effectively RFB data dump
 */

#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "bytestream.h"

enum EncTypes {
    MAGIC_WMVd = 0x574D5664,
    MAGIC_WMVe,
    MAGIC_WMVf,
    MAGIC_WMVg,
    MAGIC_WMVh,
    MAGIC_WMVi,
    MAGIC_WMVj
};

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
    AVFrame *pic;

    int bpp;
    int bpp2;
    int bigendian;
    uint8_t pal[768];
    int width, height;
    GetByteContext gb;

    /* cursor data */
    int cur_w, cur_h;
    int cur_x, cur_y;
    int cur_hx, cur_hy;
    uint8_t *curbits, *curmask;
    uint8_t *screendta;
} VmncContext;

/* read pixel value from stream */
static av_always_inline int vmnc_get_pixel(GetByteContext *gb, int bpp, int be)
{
    switch (bpp * 2 + be) {
    case 2:
    case 3:
        return bytestream2_get_byte(gb);
    case 4:
        return bytestream2_get_le16(gb);
    case 5:
        return bytestream2_get_be16(gb);
    case 8:
        return bytestream2_get_le32(gb);
    case 9:
        return bytestream2_get_be32(gb);
    default: return 0;
    }
}

static void load_cursor(VmncContext *c)
{
    int i, j, p;
    const int bpp   = c->bpp2;
    uint8_t *dst8   =             c->curbits;
    uint16_t *dst16 = (uint16_t *)c->curbits;
    uint32_t *dst32 = (uint32_t *)c->curbits;

    for (j = 0; j < c->cur_h; j++) {
        for (i = 0; i < c->cur_w; i++) {
            p = vmnc_get_pixel(&c->gb, bpp, c->bigendian);
            if (bpp == 1)
                *dst8++ = p;
            if (bpp == 2)
                *dst16++ = p;
            if (bpp == 4)
                *dst32++ = p;
        }
    }
    dst8  =            c->curmask;
    dst16 = (uint16_t*)c->curmask;
    dst32 = (uint32_t*)c->curmask;
    for (j = 0; j < c->cur_h; j++) {
        for (i = 0; i < c->cur_w; i++) {
            p = vmnc_get_pixel(&c->gb, bpp, c->bigendian);
            if (bpp == 1)
                *dst8++ = p;
            if (bpp == 2)
                *dst16++ = p;
            if (bpp == 4)
                *dst32++ = p;
        }
    }
}

static void put_cursor(uint8_t *dst, int stride, VmncContext *c, int dx, int dy)
{
    int i, j;
    int w, h, x, y;
    w = c->cur_w;
    if (c->width < c->cur_x + c->cur_w)
        w = c->width - c->cur_x;
    h = c->cur_h;
    if (c->height < c->cur_y + c->cur_h)
        h = c->height - c->cur_y;
    x = c->cur_x;
    y = c->cur_y;
    if (x < 0) {
        w += x;
        x  = 0;
    }
    if (y < 0) {
        h += y;
        y  = 0;
    }

    if ((w < 1) || (h < 1))
        return;
    dst += x * c->bpp2 + y * stride;

    if (c->bpp2 == 1) {
        uint8_t *cd = c->curbits, *msk = c->curmask;
        for (j = 0; j < h; j++) {
            for (i = 0; i < w; i++)
                dst[i] = (dst[i] & cd[i]) ^ msk[i];
            msk += c->cur_w;
            cd  += c->cur_w;
            dst += stride;
        }
    } else if (c->bpp2 == 2) {
        uint16_t *cd = (uint16_t*)c->curbits, *msk = (uint16_t*)c->curmask;
        uint16_t *dst2;
        for (j = 0; j < h; j++) {
            dst2 = (uint16_t*)dst;
            for (i = 0; i < w; i++)
                dst2[i] = (dst2[i] & cd[i]) ^ msk[i];
            msk += c->cur_w;
            cd  += c->cur_w;
            dst += stride;
        }
    } else if (c->bpp2 == 4) {
        uint32_t *cd = (uint32_t*)c->curbits, *msk = (uint32_t*)c->curmask;
        uint32_t *dst2;
        for (j = 0; j < h; j++) {
            dst2 = (uint32_t*)dst;
            for (i = 0; i < w; i++)
                dst2[i] = (dst2[i] & cd[i]) ^ msk[i];
            msk += c->cur_w;
            cd  += c->cur_w;
            dst += stride;
        }
    }
}

/* fill rectangle with given color */
static av_always_inline void paint_rect(uint8_t *dst, int dx, int dy,
                                        int w, int h, int color,
                                        int bpp, int stride)
{
    int i, j;
    dst += dx * bpp + dy * stride;
    if (bpp == 1) {
        for (j = 0; j < h; j++) {
            memset(dst, color, w);
            dst += stride;
        }
    } else if (bpp == 2) {
        uint16_t *dst2;
        for (j = 0; j < h; j++) {
            dst2 = (uint16_t*)dst;
            for (i = 0; i < w; i++)
                *dst2++ = color;
            dst += stride;
        }
    } else if (bpp == 4) {
        uint32_t *dst2;
        for (j = 0; j < h; j++) {
            dst2 = (uint32_t*)dst;
            for (i = 0; i < w; i++)
                dst2[i] = color;
            dst += stride;
        }
    }
}

static av_always_inline void paint_raw(uint8_t *dst, int w, int h,
                                       GetByteContext *gb, int bpp,
                                       int be, int stride)
{
    int i, j, p;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            p = vmnc_get_pixel(gb, bpp, be);
            switch (bpp) {
            case 1:
                dst[i] = p;
                break;
            case 2:
                ((uint16_t*)dst)[i] = p;
                break;
            case 4:
                ((uint32_t*)dst)[i] = p;
                break;
            }
        }
        dst += stride;
    }
}

static int decode_hextile(VmncContext *c, uint8_t* dst, GetByteContext *gb,
                          int w, int h, int stride)
{
    int i, j, k;
    int bg = 0, fg = 0, rects, color, flags, xy, wh;
    const int bpp = c->bpp2;
    uint8_t *dst2;
    int bw = 16, bh = 16;

    for (j = 0; j < h; j += 16) {
        dst2 = dst;
        bw   = 16;
        if (j + 16 > h)
            bh = h - j;
        for (i = 0; i < w; i += 16, dst2 += 16 * bpp) {
            if (bytestream2_get_bytes_left(gb) <= 0) {
                av_log(c->avctx, AV_LOG_ERROR, "Premature end of data!\n");
                return AVERROR_INVALIDDATA;
            }
            if (i + 16 > w)
                bw = w - i;
            flags = bytestream2_get_byte(gb);
            if (flags & HT_RAW) {
                if (bytestream2_get_bytes_left(gb) < bw * bh * bpp) {
                    av_log(c->avctx, AV_LOG_ERROR, "Premature end of data!\n");
                    return AVERROR_INVALIDDATA;
                }
                paint_raw(dst2, bw, bh, gb, bpp, c->bigendian, stride);
            } else {
                if (flags & HT_BKG)
                    bg = vmnc_get_pixel(gb, bpp, c->bigendian);
                if (flags & HT_FG)
                    fg = vmnc_get_pixel(gb, bpp, c->bigendian);
                rects = 0;
                if (flags & HT_SUB)
                    rects = bytestream2_get_byte(gb);
                color = !!(flags & HT_CLR);

                paint_rect(dst2, 0, 0, bw, bh, bg, bpp, stride);

                if (bytestream2_get_bytes_left(gb) < rects * (color * bpp + 2)) {
                    av_log(c->avctx, AV_LOG_ERROR, "Premature end of data!\n");
                    return AVERROR_INVALIDDATA;
                }
                for (k = 0; k < rects; k++) {
                    int rect_x, rect_y, rect_w, rect_h;
                    if (color)
                        fg = vmnc_get_pixel(gb, bpp, c->bigendian);
                    xy = bytestream2_get_byte(gb);
                    wh = bytestream2_get_byte(gb);

                    rect_x = xy >> 4;
                    rect_y = xy & 0xF;
                    rect_w = (wh >> 4) + 1;
                    rect_h = (wh & 0xF) + 1;

                    if (rect_x + rect_w > w - i || rect_y + rect_h > h - j) {
                        av_log(c->avctx, AV_LOG_ERROR, "Rectangle outside picture\n");
                        return AVERROR_INVALIDDATA;
                    }

                    paint_rect(dst2, rect_x, rect_y,
                               rect_w, rect_h, fg, bpp, stride);
                }
            }
        }
        dst += stride * 16;
    }
    return 0;
}

static void reset_buffers(VmncContext *c)
{
    av_freep(&c->curbits);
    av_freep(&c->curmask);
    av_freep(&c->screendta);
    c->cur_w = c->cur_h = 0;
    c->cur_hx = c->cur_hy = 0;

}

static int decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                        int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    VmncContext * const c = avctx->priv_data;
    GetByteContext *gb = &c->gb;
    uint8_t *outptr;
    int dx, dy, w, h, depth, enc, chunks, res, size_left, ret;

    bytestream2_init(gb, buf, buf_size);
    bytestream2_skip(gb, 2);
    chunks = bytestream2_get_be16(gb);
    if (12LL * chunks > bytestream2_get_bytes_left(gb))
        return AVERROR_INVALIDDATA;

    if ((ret = ff_reget_buffer(avctx, c->pic, 0)) < 0)
        return ret;

    c->pic->flags &= ~AV_FRAME_FLAG_KEY;
    c->pic->pict_type = AV_PICTURE_TYPE_P;

    // restore screen after cursor
    if (c->screendta) {
        int i;
        w = c->cur_w;
        if (c->width < c->cur_x + w)
            w = c->width - c->cur_x;
        h = c->cur_h;
        if (c->height < c->cur_y + h)
            h = c->height - c->cur_y;
        dx = c->cur_x;
        if (dx < 0) {
            w += dx;
            dx = 0;
        }
        dy = c->cur_y;
        if (dy < 0) {
            h += dy;
            dy = 0;
        }
        if ((w > 0) && (h > 0)) {
            outptr = c->pic->data[0] + dx * c->bpp2 + dy * c->pic->linesize[0];
            for (i = 0; i < h; i++) {
                memcpy(outptr, c->screendta + i * c->cur_w * c->bpp2,
                       w * c->bpp2);
                outptr += c->pic->linesize[0];
            }
        }
    }

    while (chunks--) {
        if (bytestream2_get_bytes_left(gb) < 12) {
            av_log(avctx, AV_LOG_ERROR, "Premature end of data!\n");
            return -1;
        }
        dx  = bytestream2_get_be16(gb);
        dy  = bytestream2_get_be16(gb);
        w   = bytestream2_get_be16(gb);
        h   = bytestream2_get_be16(gb);
        enc = bytestream2_get_be32(gb);
        if ((dx + w > c->width) || (dy + h > c->height)) {
            av_log(avctx, AV_LOG_ERROR,
                    "Incorrect frame size: %ix%i+%ix%i of %ix%i\n",
                    w, h, dx, dy, c->width, c->height);
            return AVERROR_INVALIDDATA;
        }
        outptr = c->pic->data[0] + dx * c->bpp2 + dy * c->pic->linesize[0];
        size_left = bytestream2_get_bytes_left(gb);
        switch (enc) {
        case MAGIC_WMVd: // cursor
            if (w*(int64_t)h*c->bpp2 > INT_MAX/2 - 2) {
                av_log(avctx, AV_LOG_ERROR, "dimensions too large\n");
                return AVERROR_INVALIDDATA;
            }
            if (size_left < 2 + w * h * c->bpp2 * 2) {
                av_log(avctx, AV_LOG_ERROR,
                       "Premature end of data! (need %i got %i)\n",
                       2 + w * h * c->bpp2 * 2, size_left);
                return AVERROR_INVALIDDATA;
            }
            bytestream2_skip(gb, 2);
            c->cur_w  = w;
            c->cur_h  = h;
            c->cur_hx = dx;
            c->cur_hy = dy;
            if ((c->cur_hx > c->cur_w) || (c->cur_hy > c->cur_h)) {
                av_log(avctx, AV_LOG_ERROR,
                       "Cursor hot spot is not in image: "
                       "%ix%i of %ix%i cursor size\n",
                       c->cur_hx, c->cur_hy, c->cur_w, c->cur_h);
                c->cur_hx = c->cur_hy = 0;
            }
            if (c->cur_w * c->cur_h >= INT_MAX / c->bpp2) {
                reset_buffers(c);
                return AVERROR(EINVAL);
            } else {
                int screen_size = c->cur_w * c->cur_h * c->bpp2;
                if ((ret = av_reallocp(&c->curbits, screen_size)) < 0 ||
                    (ret = av_reallocp(&c->curmask, screen_size)) < 0 ||
                    (ret = av_reallocp(&c->screendta, screen_size)) < 0) {
                    reset_buffers(c);
                    return ret;
                }
            }
            load_cursor(c);
            break;
        case MAGIC_WMVe: // unknown
            bytestream2_skip(gb, 2);
            break;
        case MAGIC_WMVf: // update cursor position
            c->cur_x = dx - c->cur_hx;
            c->cur_y = dy - c->cur_hy;
            break;
        case MAGIC_WMVg: // unknown
            bytestream2_skip(gb, 10);
            break;
        case MAGIC_WMVh: // unknown
            bytestream2_skip(gb, 4);
            break;
        case MAGIC_WMVi: // ServerInitialization struct
            c->pic->flags |= AV_FRAME_FLAG_KEY;
            c->pic->pict_type = AV_PICTURE_TYPE_I;
            depth = bytestream2_get_byte(gb);
            if (depth != c->bpp) {
                av_log(avctx, AV_LOG_INFO,
                       "Depth mismatch. Container %i bpp, "
                       "Frame data: %i bpp\n",
                       c->bpp, depth);
            }
            bytestream2_skip(gb, 1);
            c->bigendian = bytestream2_get_byte(gb);
            if (c->bigendian & (~1)) {
                av_log(avctx, AV_LOG_INFO,
                       "Invalid header: bigendian flag = %i\n", c->bigendian);
                return AVERROR_INVALIDDATA;
            }
            //skip the rest of pixel format data
            bytestream2_skip(gb, 13);
            break;
        case MAGIC_WMVj: // unknown
            bytestream2_skip(gb, 2);
            break;
        case 0x00000000: // raw rectangle data
            if (size_left < w * h * c->bpp2) {
                av_log(avctx, AV_LOG_ERROR,
                       "Premature end of data! (need %i got %i)\n",
                       w * h * c->bpp2, size_left);
                return AVERROR_INVALIDDATA;
            }
            paint_raw(outptr, w, h, gb, c->bpp2, c->bigendian,
                      c->pic->linesize[0]);
            break;
        case 0x00000005: // HexTile encoded rectangle
            res = decode_hextile(c, outptr, gb, w, h, c->pic->linesize[0]);
            if (res < 0)
                return res;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unsupported block type 0x%08X\n", enc);
            chunks = 0; // leave chunks decoding loop
        }
    }
    if (c->screendta) {
        int i;
        // save screen data before painting cursor
        w = c->cur_w;
        if (c->width < c->cur_x + w)
            w = c->width - c->cur_x;
        h = c->cur_h;
        if (c->height < c->cur_y + h)
            h = c->height - c->cur_y;
        dx = c->cur_x;
        if (dx < 0) {
            w += dx;
            dx = 0;
        }
        dy = c->cur_y;
        if (dy < 0) {
            h += dy;
            dy = 0;
        }
        if ((w > 0) && (h > 0)) {
            outptr = c->pic->data[0] + dx * c->bpp2 + dy * c->pic->linesize[0];
            for (i = 0; i < h; i++) {
                memcpy(c->screendta + i * c->cur_w * c->bpp2, outptr,
                       w * c->bpp2);
                outptr += c->pic->linesize[0];
            }
            outptr = c->pic->data[0];
            put_cursor(outptr, c->pic->linesize[0], c, c->cur_x, c->cur_y);
        }
    }
    *got_frame = 1;
    if ((ret = av_frame_ref(rframe, c->pic)) < 0)
        return ret;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    VmncContext * const c = avctx->priv_data;

    c->avctx  = avctx;
    c->width  = avctx->width;
    c->height = avctx->height;
    c->bpp    = avctx->bits_per_coded_sample;

    switch (c->bpp) {
    case 8:
        avctx->pix_fmt = AV_PIX_FMT_PAL8;
        break;
    case 16:
        avctx->pix_fmt = AV_PIX_FMT_RGB555;
        break;
    case 24:
        /* 24 bits is not technically supported, but some clients might
         * mistakenly set it, so let's assume they actually meant 32 bits */
        c->bpp = 32;
    case 32:
        avctx->pix_fmt = AV_PIX_FMT_0RGB32;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported bitdepth %i\n", c->bpp);
        return AVERROR_INVALIDDATA;
    }
    c->bpp2 = c->bpp / 8;

    c->pic = av_frame_alloc();
    if (!c->pic)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    VmncContext * const c = avctx->priv_data;

    av_frame_free(&c->pic);

    av_freep(&c->curbits);
    av_freep(&c->curmask);
    av_freep(&c->screendta);
    return 0;
}

const FFCodec ff_vmnc_decoder = {
    .p.name         = "vmnc",
    CODEC_LONG_NAME("VMware Screen Codec / VMware Video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VMNC,
    .priv_data_size = sizeof(VmncContext),
    .init           = decode_init,
    .close          = decode_end,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
