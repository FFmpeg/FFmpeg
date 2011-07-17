/*
 * PNG image format
 * Copyright (c) 2003 Fabrice Bellard
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
#include "dsputil.h"
#include "png.h"

/* TODO:
 * - add 2, 4 and 16 bit depth support
 */

#include <zlib.h>

//#define DEBUG

#define IOBUF_SIZE 4096

typedef struct PNGEncContext {
    DSPContext dsp;

    uint8_t *bytestream;
    uint8_t *bytestream_start;
    uint8_t *bytestream_end;
    AVFrame picture;

    int filter_type;

    z_stream zstream;
    uint8_t buf[IOBUF_SIZE];
} PNGEncContext;

static void png_get_interlaced_row(uint8_t *dst, int row_size,
                                   int bits_per_pixel, int pass,
                                   const uint8_t *src, int width)
{
    int x, mask, dst_x, j, b, bpp;
    uint8_t *d;
    const uint8_t *s;

    mask = ff_png_pass_mask[pass];
    switch(bits_per_pixel) {
    case 1:
        memset(dst, 0, row_size);
        dst_x = 0;
        for(x = 0; x < width; x++) {
            j = (x & 7);
            if ((mask << j) & 0x80) {
                b = (src[x >> 3] >> (7 - j)) & 1;
                dst[dst_x >> 3] |= b << (7 - (dst_x & 7));
                dst_x++;
            }
        }
        break;
    default:
        bpp = bits_per_pixel >> 3;
        d = dst;
        s = src;
        for(x = 0; x < width; x++) {
            j = x & 7;
            if ((mask << j) & 0x80) {
                memcpy(d, s, bpp);
                d += bpp;
            }
            s += bpp;
        }
        break;
    }
}

static void sub_png_paeth_prediction(uint8_t *dst, uint8_t *src, uint8_t *top, int w, int bpp)
{
    int i;
    for(i = 0; i < w; i++) {
        int a, b, c, p, pa, pb, pc;

        a = src[i - bpp];
        b = top[i];
        c = top[i - bpp];

        p = b - c;
        pc = a - c;

        pa = abs(p);
        pb = abs(pc);
        pc = abs(p + pc);

        if (pa <= pb && pa <= pc)
            p = a;
        else if (pb <= pc)
            p = b;
        else
            p = c;
        dst[i] = src[i] - p;
    }
}

static void png_filter_row(DSPContext *dsp, uint8_t *dst, int filter_type,
                           uint8_t *src, uint8_t *top, int size, int bpp)
{
    int i;

    switch(filter_type) {
    case PNG_FILTER_VALUE_NONE:
        memcpy(dst, src, size);
        break;
    case PNG_FILTER_VALUE_SUB:
        dsp->diff_bytes(dst, src, src-bpp, size);
        memcpy(dst, src, bpp);
        break;
    case PNG_FILTER_VALUE_UP:
        dsp->diff_bytes(dst, src, top, size);
        break;
    case PNG_FILTER_VALUE_AVG:
        for(i = 0; i < bpp; i++)
            dst[i] = src[i] - (top[i] >> 1);
        for(; i < size; i++)
            dst[i] = src[i] - ((src[i-bpp] + top[i]) >> 1);
        break;
    case PNG_FILTER_VALUE_PAETH:
        for(i = 0; i < bpp; i++)
            dst[i] = src[i] - top[i];
        sub_png_paeth_prediction(dst+i, src+i, top+i, size-i, bpp);
        break;
    }
}

static uint8_t *png_choose_filter(PNGEncContext *s, uint8_t *dst,
                                  uint8_t *src, uint8_t *top, int size, int bpp)
{
    int pred = s->filter_type;
    assert(bpp || !pred);
    if(!top && pred)
        pred = PNG_FILTER_VALUE_SUB;
    if(pred == PNG_FILTER_VALUE_MIXED) {
        int i;
        int cost, bcost = INT_MAX;
        uint8_t *buf1 = dst, *buf2 = dst + size + 16;
        for(pred=0; pred<5; pred++) {
            png_filter_row(&s->dsp, buf1+1, pred, src, top, size, bpp);
            buf1[0] = pred;
            cost = 0;
            for(i=0; i<=size; i++)
                cost += abs((int8_t)buf1[i]);
            if(cost < bcost) {
                bcost = cost;
                FFSWAP(uint8_t*, buf1, buf2);
            }
        }
        return buf2;
    } else {
        png_filter_row(&s->dsp, dst+1, pred, src, top, size, bpp);
        dst[0] = pred;
        return dst;
    }
}

static void convert_from_rgb32(uint8_t *dst, const uint8_t *src, int width)
{
    uint8_t *d;
    int j;
    unsigned int v;

    d = dst;
    for(j = 0; j < width; j++) {
        v = ((const uint32_t *)src)[j];
        d[0] = v >> 16;
        d[1] = v >> 8;
        d[2] = v;
        d[3] = v >> 24;
        d += 4;
    }
}

static void png_write_chunk(uint8_t **f, uint32_t tag,
                            const uint8_t *buf, int length)
{
    uint32_t crc;
    uint8_t tagbuf[4];

    bytestream_put_be32(f, length);
    crc = crc32(0, Z_NULL, 0);
    AV_WL32(tagbuf, tag);
    crc = crc32(crc, tagbuf, 4);
    bytestream_put_be32(f, av_bswap32(tag));
    if (length > 0) {
        crc = crc32(crc, buf, length);
        memcpy(*f, buf, length);
        *f += length;
    }
    bytestream_put_be32(f, crc);
}

/* XXX: do filtering */
static int png_write_row(PNGEncContext *s, const uint8_t *data, int size)
{
    int ret;

    s->zstream.avail_in = size;
    s->zstream.next_in = (uint8_t *)data;
    while (s->zstream.avail_in > 0) {
        ret = deflate(&s->zstream, Z_NO_FLUSH);
        if (ret != Z_OK)
            return -1;
        if (s->zstream.avail_out == 0) {
            if(s->bytestream_end - s->bytestream > IOBUF_SIZE + 100)
                png_write_chunk(&s->bytestream, MKTAG('I', 'D', 'A', 'T'), s->buf, IOBUF_SIZE);
            s->zstream.avail_out = IOBUF_SIZE;
            s->zstream.next_out = s->buf;
        }
    }
    return 0;
}

static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    PNGEncContext *s = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p= &s->picture;
    int bit_depth, color_type, y, len, row_size, ret, is_progressive;
    int bits_per_pixel, pass_row_size;
    int compression_level;
    uint8_t *ptr, *top;
    uint8_t *crow_base = NULL, *crow_buf, *crow;
    uint8_t *progressive_buf = NULL;
    uint8_t *rgba_buf = NULL;
    uint8_t *top_buf = NULL;

    *p = *pict;
    p->pict_type= AV_PICTURE_TYPE_I;
    p->key_frame= 1;

    s->bytestream_start=
    s->bytestream= buf;
    s->bytestream_end= buf+buf_size;

    is_progressive = !!(avctx->flags & CODEC_FLAG_INTERLACED_DCT);
    switch(avctx->pix_fmt) {
    case PIX_FMT_RGB32:
        bit_depth = 8;
        color_type = PNG_COLOR_TYPE_RGB_ALPHA;
        break;
    case PIX_FMT_RGB24:
        bit_depth = 8;
        color_type = PNG_COLOR_TYPE_RGB;
        break;
    case PIX_FMT_GRAY8:
        bit_depth = 8;
        color_type = PNG_COLOR_TYPE_GRAY;
        break;
    case PIX_FMT_MONOBLACK:
        bit_depth = 1;
        color_type = PNG_COLOR_TYPE_GRAY;
        break;
    case PIX_FMT_PAL8:
        bit_depth = 8;
        color_type = PNG_COLOR_TYPE_PALETTE;
        break;
    default:
        return -1;
    }
    bits_per_pixel = ff_png_get_nb_channels(color_type) * bit_depth;
    row_size = (avctx->width * bits_per_pixel + 7) >> 3;

    s->zstream.zalloc = ff_png_zalloc;
    s->zstream.zfree = ff_png_zfree;
    s->zstream.opaque = NULL;
    compression_level = avctx->compression_level == FF_COMPRESSION_DEFAULT ?
                            Z_DEFAULT_COMPRESSION :
                            av_clip(avctx->compression_level, 0, 9);
    ret = deflateInit2(&s->zstream, compression_level,
                       Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK)
        return -1;
    crow_base = av_malloc((row_size + 32) << (s->filter_type == PNG_FILTER_VALUE_MIXED));
    if (!crow_base)
        goto fail;
    crow_buf = crow_base + 15; // pixel data should be aligned, but there's a control byte before it
    if (is_progressive) {
        progressive_buf = av_malloc(row_size + 1);
        if (!progressive_buf)
            goto fail;
    }
    if (color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
        rgba_buf = av_malloc(row_size + 1);
        if (!rgba_buf)
            goto fail;
    }
    if (is_progressive || color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
        top_buf = av_malloc(row_size + 1);
        if (!top_buf)
            goto fail;
    }

    /* write png header */
    memcpy(s->bytestream, ff_pngsig, 8);
    s->bytestream += 8;

    AV_WB32(s->buf, avctx->width);
    AV_WB32(s->buf + 4, avctx->height);
    s->buf[8] = bit_depth;
    s->buf[9] = color_type;
    s->buf[10] = 0; /* compression type */
    s->buf[11] = 0; /* filter type */
    s->buf[12] = is_progressive; /* interlace type */

    png_write_chunk(&s->bytestream, MKTAG('I', 'H', 'D', 'R'), s->buf, 13);

    /* put the palette if needed */
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        int has_alpha, alpha, i;
        unsigned int v;
        uint32_t *palette;
        uint8_t *alpha_ptr;

        palette = (uint32_t *)p->data[1];
        ptr = s->buf;
        alpha_ptr = s->buf + 256 * 3;
        has_alpha = 0;
        for(i = 0; i < 256; i++) {
            v = palette[i];
            alpha = v >> 24;
            if (alpha && alpha != 0xff)
                has_alpha = 1;
            *alpha_ptr++ = alpha;
            bytestream_put_be24(&ptr, v);
        }
        png_write_chunk(&s->bytestream, MKTAG('P', 'L', 'T', 'E'), s->buf, 256 * 3);
        if (has_alpha) {
            png_write_chunk(&s->bytestream, MKTAG('t', 'R', 'N', 'S'), s->buf + 256 * 3, 256);
        }
    }

    /* now put each row */
    s->zstream.avail_out = IOBUF_SIZE;
    s->zstream.next_out = s->buf;
    if (is_progressive) {
        int pass;

        for(pass = 0; pass < NB_PASSES; pass++) {
            /* NOTE: a pass is completely omited if no pixels would be
               output */
            pass_row_size = ff_png_pass_row_size(pass, bits_per_pixel, avctx->width);
            if (pass_row_size > 0) {
                top = NULL;
                for(y = 0; y < avctx->height; y++) {
                    if ((ff_png_pass_ymask[pass] << (y & 7)) & 0x80) {
                        ptr = p->data[0] + y * p->linesize[0];
                        FFSWAP(uint8_t*, progressive_buf, top_buf);
                        if (color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
                            convert_from_rgb32(rgba_buf, ptr, avctx->width);
                            ptr = rgba_buf;
                        }
                        png_get_interlaced_row(progressive_buf, pass_row_size,
                                               bits_per_pixel, pass,
                                               ptr, avctx->width);
                        crow = png_choose_filter(s, crow_buf, progressive_buf, top, pass_row_size, bits_per_pixel>>3);
                        png_write_row(s, crow, pass_row_size + 1);
                        top = progressive_buf;
                    }
                }
            }
        }
    } else {
        top = NULL;
        for(y = 0; y < avctx->height; y++) {
            ptr = p->data[0] + y * p->linesize[0];
            if (color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
                FFSWAP(uint8_t*, rgba_buf, top_buf);
                convert_from_rgb32(rgba_buf, ptr, avctx->width);
                ptr = rgba_buf;
            }
            crow = png_choose_filter(s, crow_buf, ptr, top, row_size, bits_per_pixel>>3);
            png_write_row(s, crow, row_size + 1);
            top = ptr;
        }
    }
    /* compress last bytes */
    for(;;) {
        ret = deflate(&s->zstream, Z_FINISH);
        if (ret == Z_OK || ret == Z_STREAM_END) {
            len = IOBUF_SIZE - s->zstream.avail_out;
            if (len > 0 && s->bytestream_end - s->bytestream > len + 100) {
                png_write_chunk(&s->bytestream, MKTAG('I', 'D', 'A', 'T'), s->buf, len);
            }
            s->zstream.avail_out = IOBUF_SIZE;
            s->zstream.next_out = s->buf;
            if (ret == Z_STREAM_END)
                break;
        } else {
            goto fail;
        }
    }
    png_write_chunk(&s->bytestream, MKTAG('I', 'E', 'N', 'D'), NULL, 0);

    ret = s->bytestream - s->bytestream_start;
 the_end:
    av_free(crow_base);
    av_free(progressive_buf);
    av_free(rgba_buf);
    av_free(top_buf);
    deflateEnd(&s->zstream);
    return ret;
 fail:
    ret = -1;
    goto the_end;
}

static av_cold int png_enc_init(AVCodecContext *avctx){
    PNGEncContext *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame= &s->picture;
    dsputil_init(&s->dsp, avctx);

    s->filter_type = av_clip(avctx->prediction_method, PNG_FILTER_VALUE_NONE, PNG_FILTER_VALUE_MIXED);
    if(avctx->pix_fmt == PIX_FMT_MONOBLACK)
        s->filter_type = PNG_FILTER_VALUE_NONE;

    return 0;
}

AVCodec ff_png_encoder = {
    .name           = "png",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PNG,
    .priv_data_size = sizeof(PNGEncContext),
    .init           = png_enc_init,
    .encode         = encode_frame,
    .pix_fmts= (const enum PixelFormat[]){PIX_FMT_RGB24, PIX_FMT_RGB32, PIX_FMT_PAL8, PIX_FMT_GRAY8, PIX_FMT_MONOBLACK, PIX_FMT_NONE},
    .long_name= NULL_IF_CONFIG_SMALL("PNG image"),
};
