/*
 * PNG image format
 * Copyright (c) 2003 Fabrice Bellard.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"

#include <zlib.h>

//#define DEBUG

#define PNG_COLOR_MASK_PALETTE    1
#define PNG_COLOR_MASK_COLOR      2
#define PNG_COLOR_MASK_ALPHA      4

#define PNG_COLOR_TYPE_GRAY 0
#define PNG_COLOR_TYPE_PALETTE  (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_PALETTE)
#define PNG_COLOR_TYPE_RGB        (PNG_COLOR_MASK_COLOR)
#define PNG_COLOR_TYPE_RGB_ALPHA  (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_ALPHA)
#define PNG_COLOR_TYPE_GRAY_ALPHA (PNG_COLOR_MASK_ALPHA)

#define PNG_FILTER_VALUE_NONE  0
#define PNG_FILTER_VALUE_SUB   1
#define PNG_FILTER_VALUE_UP    2
#define PNG_FILTER_VALUE_AVG   3
#define PNG_FILTER_VALUE_PAETH 4

#define PNG_IHDR      0x0001
#define PNG_IDAT      0x0002
#define PNG_ALLIMAGE  0x0004

#define IOBUF_SIZE 4096

typedef struct PNGDecodeState {
    int state;
    int width, height;
    int bit_depth;
    int color_type;
    int compression_type;
    int interlace_type;
    int filter_type;
    int channels;
    int bits_per_pixel;
    int bpp;
    
    uint8_t *image_buf;
    int image_linesize;
    uint8_t *crow_buf;
    uint8_t *empty_row;
    int crow_size; /* compressed row size (include filter type) */
    int row_size; /* decompressed row size */
    int y;
    z_stream zstream;
} PNGDecodeState;

const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};

static int png_probe(AVProbeData *pd)
{
    if (pd->buf_size >= 8 &&
        memcmp(pd->buf, pngsig, 8) == 0)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

void *png_zalloc(void *opaque, unsigned int items, unsigned int size)
{
    return av_malloc(items * size);
}

void png_zfree(void *opaque, void *ptr)
{
    av_free(ptr);
}

/* XXX: optimize */
static void png_filter_row(uint8_t *dst, int filter_type, 
                           uint8_t *src, uint8_t *last, int size, int bpp)
{
    int i, p;

    switch(filter_type) {
    case PNG_FILTER_VALUE_NONE:
        memcpy(dst, src, size);
        break;
    case PNG_FILTER_VALUE_SUB:
        for(i = 0; i < bpp; i++) {
            dst[i] = src[i];
        }
        for(i = bpp; i < size; i++) {
            p = dst[i - bpp];
            dst[i] = p + src[i];
        }
        break;
    case PNG_FILTER_VALUE_UP:
        for(i = 0; i < size; i++) {
            p = last[i];
            dst[i] = p + src[i];
        }
        break;
    case PNG_FILTER_VALUE_AVG:
        for(i = 0; i < bpp; i++) {
            p = (last[i] >> 1);
            dst[i] = p + src[i];
        }
        for(i = bpp; i < size; i++) {
            p = ((dst[i - bpp] + last[i]) >> 1);
            dst[i] = p + src[i];
        }
        break;
    case PNG_FILTER_VALUE_PAETH:
        for(i = 0; i < bpp; i++) {
            p = last[i];
            dst[i] = p + src[i];
        }
        for(i = bpp; i < size; i++) {
            int a, b, c, pa, pb, pc;

            a = dst[i - bpp];
            b = last[i];
            c = last[i - bpp];

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
            dst[i] = p + src[i];
        }
        break;
    }
}

static void png_handle_row(PNGDecodeState *s)
{
    uint8_t *ptr, *last_row;
    ptr = s->image_buf + s->image_linesize * s->y;
    if (s->y == 0)
        last_row = s->empty_row;
    else
        last_row = ptr - s->image_linesize;

    png_filter_row(ptr, s->crow_buf[0], s->crow_buf + 1, 
                   last_row, s->row_size, s->bpp);
}

static int png_decode_idat(PNGDecodeState *s, ByteIOContext *f, int length)
{
    uint8_t buf[IOBUF_SIZE];
    int buf_size;
    int ret;
    while (length > 0) {
        /* read the buffer */
        buf_size = IOBUF_SIZE;
        if (buf_size > length)
            buf_size = length;
        ret = get_buffer(f, buf, buf_size);
        if (ret != buf_size)
            return -1;
        s->zstream.avail_in = buf_size;
        s->zstream.next_in = buf;
        /* decode one line if possible */
        while (s->zstream.avail_in > 0) {
            ret = inflate(&s->zstream, Z_PARTIAL_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                return -1;
            }
            if (s->zstream.avail_out == 0) {
                if (s->y < s->height) {
                    png_handle_row(s);
                    s->y++;
                    if (s->y == s->height)
                        s->state |= PNG_ALLIMAGE;
                }
                s->zstream.avail_out = s->crow_size;
                s->zstream.next_out = s->crow_buf;
            }
        }
        length -= buf_size;
    }
    return 0;
}

static int png_read(ByteIOContext *f, 
                    int (*alloc_cb)(void *opaque, AVImageInfo *info), void *opaque)
{
    AVImageInfo info1, *info = &info1;
    PNGDecodeState s1, *s = &s1;
    uint32_t tag, length;
    int ret, crc;
    uint8_t buf[8];

    /* check signature */
    ret = get_buffer(f, buf, 8);
    if (ret != 8)
        return -1;
    if (memcmp(buf, pngsig, 8) != 0)
        return -1;
    memset(s, 0, sizeof(PNGDecodeState));
    /* init the zlib */
    s->zstream.zalloc = png_zalloc;
    s->zstream.zfree = png_zfree;
    s->zstream.opaque = NULL;
    ret = inflateInit(&s->zstream);
    if (ret != Z_OK)
        return -1;
    for(;;) {
        if (url_feof(f))
            goto fail;
        length = get_be32(f);
        if (length > 0x7fffffff)
            goto fail;
        tag = get_le32(f);
#ifdef DEBUG
        printf("png: tag=%c%c%c%c length=%u\n", 
               (tag & 0xff),
               ((tag >> 8) & 0xff),
               ((tag >> 16) & 0xff),
               ((tag >> 24) & 0xff), length);
#endif
        switch(tag) {
        case MKTAG('I', 'H', 'D', 'R'):
            if (length != 13)
                goto fail;
            s->width = get_be32(f);
            s->height = get_be32(f);
            s->bit_depth = get_byte(f);
            s->color_type = get_byte(f);
            s->compression_type = get_byte(f);
            s->filter_type = get_byte(f);
            s->interlace_type = get_byte(f);
            crc = get_be32(f);
            s->state |= PNG_IHDR;
#ifdef DEBUG
            printf("width=%d height=%d depth=%d color_type=%d compression_type=%d filter_type=%d interlace_type=%d\n", 
                   s->width, s->height, s->bit_depth, s->color_type, 
                   s->compression_type, s->filter_type, s->interlace_type);
#endif
            break;
        case MKTAG('I', 'D', 'A', 'T'):
            if (!(s->state & PNG_IHDR))
                goto fail;
            if (!(s->state & PNG_IDAT)) {
                /* init image info */
                info->width = s->width;
                info->height = s->height;

                s->channels = 1;
                if ((s->color_type & (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_PALETTE)) ==
                    PNG_COLOR_MASK_COLOR)
                    s->channels = 3;
                if (s->color_type & PNG_COLOR_MASK_ALPHA)
                    s->channels++;
                s->bits_per_pixel = s->bit_depth * s->channels;
                s->bpp = (s->bits_per_pixel + 7) >> 3;
                if (s->bit_depth == 8 && 
                    s->color_type == PNG_COLOR_TYPE_RGB) {
                    info->pix_fmt = PIX_FMT_RGB24;
                    s->row_size = s->width * 3;
                } else if (s->bit_depth == 8 && 
                           s->color_type == PNG_COLOR_TYPE_GRAY) {
                    info->pix_fmt = PIX_FMT_GRAY8;
                    s->row_size = s->width;
                } else if (s->bit_depth == 1 && 
                           s->color_type == PNG_COLOR_TYPE_GRAY) {
                    info->pix_fmt = PIX_FMT_MONOBLACK;
                    s->row_size = (s->width + 7) >> 3;
                } else {
                    goto fail;
                }
                /* compute the compressed row size */
                if (!s->interlace_type) {
                    s->crow_size = s->row_size + 1;
                } else {
                    /* XXX: handle interlacing */
                    goto fail;
                }
                ret = alloc_cb(opaque, info);
                if (ret) 
                    goto the_end;
#ifdef DEBUG
                printf("row_size=%d crow_size =%d\n", 
                       s->row_size, s->crow_size);
#endif
                s->image_buf = info->pict.data[0];
                s->image_linesize = info->pict.linesize[0];
                /* empty row is used if differencing to the first row */
                s->empty_row = av_mallocz(s->row_size);
                if (!s->empty_row)
                    goto fail;
                /* compressed row */
                s->crow_buf = av_malloc(s->crow_size);
                if (!s->crow_buf)
                    goto fail;
                s->zstream.avail_out = s->crow_size;
                s->zstream.next_out = s->crow_buf;
            }
            s->state |= PNG_IDAT;
            if (png_decode_idat(s, f, length) < 0)
                goto fail;
            /* skip crc */
            crc = get_be32(f);
            break;
        case MKTAG('I', 'E', 'N', 'D'):
            if (!(s->state & PNG_ALLIMAGE))
                goto fail;
            crc = get_be32(f);
            goto exit_loop;
        default:
            /* skip tag */
            url_fskip(f, length + 4);
            break;
        }
    }
 exit_loop:
    ret = 0;
 the_end:
    inflateEnd(&s->zstream);
    av_free(s->crow_buf);
    return ret;
 fail:
    ret = -1;
    goto the_end;
}

static void png_write_chunk(ByteIOContext *f, uint32_t tag,
                            const uint8_t *buf, int length)
{
    uint32_t crc;
    uint8_t tagbuf[4];

    put_be32(f, length);
    crc = crc32(0, Z_NULL, 0);
    tagbuf[0] = tag;
    tagbuf[1] = tag >> 8;
    tagbuf[2] = tag >> 16;
    tagbuf[3] = tag >> 24;
    crc = crc32(crc, tagbuf, 4);
    put_le32(f, tag);
    if (length > 0) {
        crc = crc32(crc, buf, length);
        put_buffer(f, buf, length);
    }
    put_be32(f, crc);
}

/* XXX: use avcodec generic function ? */
static void to_be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static int png_write(ByteIOContext *f, AVImageInfo *info)
{
    int bit_depth, color_type, y, len, row_size, ret;
    uint8_t *ptr;
    uint8_t buf[IOBUF_SIZE];
    uint8_t *crow_buf = NULL;
    z_stream zstream;
    
    switch(info->pix_fmt) {
    case PIX_FMT_RGB24:
        bit_depth = 8;
        color_type = PNG_COLOR_TYPE_RGB;
        row_size = info->width * 3;
        break;
    case PIX_FMT_GRAY8:
        bit_depth = 8;
        color_type = PNG_COLOR_TYPE_GRAY;
        row_size = info->width;
        break;
    case PIX_FMT_MONOBLACK:
        bit_depth = 1;
        color_type = PNG_COLOR_TYPE_GRAY;
        row_size = (info->width + 7) >> 3;
        break;
    default:
        return -1;
    }
    zstream.zalloc = png_zalloc;
    zstream.zfree = png_zfree;
    zstream.opaque = NULL;
    ret = deflateInit2(&zstream, Z_DEFAULT_COMPRESSION,
                       Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK)
        return -1;
    crow_buf = av_malloc(row_size + 1);
    if (!crow_buf)
        goto fail;

    /* write png header */
    put_buffer(f, pngsig, 8);
    
    to_be32(buf, info->width);
    to_be32(buf + 4, info->height);
    buf[8] = bit_depth;
    buf[9] = color_type;
    buf[10] = 0; /* compression type */
    buf[11] = 0; /* filter type */
    buf[12] = 0; /* interlace type */
    
    png_write_chunk(f, MKTAG('I', 'H', 'D', 'R'), buf, 13);

    /* now put each row */
    zstream.avail_out = IOBUF_SIZE;
    zstream.next_out = buf;
    for(y = 0;y < info->height; y++) {
        /* XXX: do filtering */
        ptr = info->pict.data[0] + y * info->pict.linesize[0];
        memcpy(crow_buf + 1, ptr, row_size);
        crow_buf[0] = PNG_FILTER_VALUE_NONE;
        zstream.avail_in = row_size + 1;
        zstream.next_in = crow_buf;
        while (zstream.avail_in > 0) {
            ret = deflate(&zstream, Z_NO_FLUSH);
            if (ret != Z_OK)
                goto fail;
            if (zstream.avail_out == 0) {
                png_write_chunk(f, MKTAG('I', 'D', 'A', 'T'), buf, IOBUF_SIZE);
                zstream.avail_out = IOBUF_SIZE;
                zstream.next_out = buf;
            }
        }
    }
    /* compress last bytes */
    for(;;) {
        ret = deflate(&zstream, Z_FINISH);
        if (ret == Z_OK || ret == Z_STREAM_END) {
            len = IOBUF_SIZE - zstream.avail_out;
            if (len > 0) {
                png_write_chunk(f, MKTAG('I', 'D', 'A', 'T'), buf, len);
            }
            zstream.avail_out = IOBUF_SIZE;
            zstream.next_out = buf;
            if (ret == Z_STREAM_END)
                break;
        } else {
            goto fail;
        }
    }
    png_write_chunk(f, MKTAG('I', 'E', 'N', 'D'), NULL, 0);

    put_flush_packet(f);
    ret = 0;
 the_end:
    av_free(crow_buf);
    deflateEnd(&zstream);
    return ret;
 fail:
    ret = -1;
    goto the_end;
}

AVImageFormat png_image_format = {
    "png",
    "png",
    png_probe,
    png_read,
    (1 << PIX_FMT_RGB24) | (1 << PIX_FMT_GRAY8) | (1 << PIX_FMT_MONOBLACK),
    png_write,
};
