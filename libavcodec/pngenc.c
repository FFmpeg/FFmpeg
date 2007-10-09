/*
 * PNG image format
 * Copyright (c) 2003 Fabrice Bellard.
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
#include "png.h"

/* TODO:
 * - add 2, 4 and 16 bit depth support
 * - use filters when generating a png (better compression)
 */

#include <zlib.h>

//#define DEBUG

#define IOBUF_SIZE 4096

typedef struct PNGEncContext {
    uint8_t *bytestream;
    uint8_t *bytestream_start;
    uint8_t *bytestream_end;
    AVFrame picture;

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
    bytestream_put_be32(f, bswap_32(tag));
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
    AVFrame * const p= (AVFrame*)&s->picture;
    int bit_depth, color_type, y, len, row_size, ret, is_progressive;
    int bits_per_pixel, pass_row_size;
    int compression_level;
    uint8_t *ptr;
    uint8_t *crow_buf = NULL;
    uint8_t *tmp_buf = NULL;

    *p = *pict;
    p->pict_type= FF_I_TYPE;
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
    crow_buf = av_malloc(row_size + 1);
    if (!crow_buf)
        goto fail;
    if (is_progressive) {
        tmp_buf = av_malloc(row_size + 1);
        if (!tmp_buf)
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
        uint8_t *ptr1;
        int pass;

        for(pass = 0; pass < NB_PASSES; pass++) {
            /* NOTE: a pass is completely omited if no pixels would be
               output */
            pass_row_size = ff_png_pass_row_size(pass, bits_per_pixel, avctx->width);
            if (pass_row_size > 0) {
                for(y = 0; y < avctx->height; y++) {
                    if ((ff_png_pass_ymask[pass] << (y & 7)) & 0x80) {
                        ptr = p->data[0] + y * p->linesize[0];
                        if (color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
                            convert_from_rgb32(tmp_buf, ptr, avctx->width);
                            ptr1 = tmp_buf;
                        } else {
                            ptr1 = ptr;
                        }
                        png_get_interlaced_row(crow_buf + 1, pass_row_size,
                                               bits_per_pixel, pass,
                                               ptr1, avctx->width);
                        crow_buf[0] = PNG_FILTER_VALUE_NONE;
                        png_write_row(s, crow_buf, pass_row_size + 1);
                    }
                }
            }
        }
    } else {
        for(y = 0; y < avctx->height; y++) {
            ptr = p->data[0] + y * p->linesize[0];
            if (color_type == PNG_COLOR_TYPE_RGB_ALPHA)
                convert_from_rgb32(crow_buf + 1, ptr, avctx->width);
            else
                memcpy(crow_buf + 1, ptr, row_size);
            crow_buf[0] = PNG_FILTER_VALUE_NONE;
            png_write_row(s, crow_buf, row_size + 1);
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
    av_free(crow_buf);
    av_free(tmp_buf);
    deflateEnd(&s->zstream);
    return ret;
 fail:
    ret = -1;
    goto the_end;
}

static int png_enc_init(AVCodecContext *avctx){
    PNGEncContext *s = avctx->priv_data;

    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame= (AVFrame*)&s->picture;

    return 0;
}

AVCodec png_encoder = {
    "png",
    CODEC_TYPE_VIDEO,
    CODEC_ID_PNG,
    sizeof(PNGEncContext),
    png_enc_init,
    encode_frame,
    NULL, //encode_end,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_RGB24, PIX_FMT_RGB32, PIX_FMT_PAL8, PIX_FMT_GRAY8, PIX_FMT_MONOBLACK, -1},
};
