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

/* TODO:
 * - add 2, 4 and 16 bit depth support
 * - use filters when generating a png (better compression)
 */

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
#define PNG_PLTE      0x0008

#define NB_PASSES 7

#define IOBUF_SIZE 4096

typedef struct PNGContext {
    uint8_t *bytestream;
    uint8_t *bytestream_start;
    uint8_t *bytestream_end;
    AVFrame picture;

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
    uint32_t palette[256];
    uint8_t *crow_buf;
    uint8_t *last_row;
    uint8_t *tmp_row;
    int pass;
    int crow_size; /* compressed row size (include filter type) */
    int row_size; /* decompressed row size */
    int pass_row_size; /* decompress row size of the current pass */
    int y;
    z_stream zstream;
    uint8_t buf[IOBUF_SIZE];
} PNGContext;

static unsigned int get32(uint8_t **b){
    (*b) += 4;
    return ((*b)[-4]<<24) + ((*b)[-3]<<16) + ((*b)[-2]<<8) + (*b)[-1];
}

#ifdef CONFIG_ENCODERS
static void put32(uint8_t **b, unsigned int v){
    *(*b)++= v>>24;
    *(*b)++= v>>16;
    *(*b)++= v>>8;
    *(*b)++= v;
}
#endif

static const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};

/* Mask to determine which y pixels are valid in a pass */
static const uint8_t png_pass_ymask[NB_PASSES] = {
    0x80, 0x80, 0x08, 0x88, 0x22, 0xaa, 0x55,
};

/* Mask to determine which y pixels can be written in a pass */
static const uint8_t png_pass_dsp_ymask[NB_PASSES] = {
    0xff, 0xff, 0x0f, 0xcc, 0x33, 0xff, 0x55,
};

/* minimum x value */
static const uint8_t png_pass_xmin[NB_PASSES] = {
    0, 4, 0, 2, 0, 1, 0
};

/* x shift to get row width */
static const uint8_t png_pass_xshift[NB_PASSES] = {
    3, 3, 2, 2, 1, 1, 0
};

/* Mask to determine which pixels are valid in a pass */
static const uint8_t png_pass_mask[NB_PASSES] = {
    0x80, 0x08, 0x88, 0x22, 0xaa, 0x55, 0xff
};

/* Mask to determine which pixels to overwrite while displaying */
static const uint8_t png_pass_dsp_mask[NB_PASSES] = {
    0xff, 0x0f, 0xff, 0x33, 0xff, 0x55, 0xff
};
#if 0
static int png_probe(AVProbeData *pd)
{
    if (pd->buf_size >= 8 &&
        memcmp(pd->buf, pngsig, 8) == 0)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}
#endif
static void *png_zalloc(void *opaque, unsigned int items, unsigned int size)
{
    if(items >= UINT_MAX / size)
        return NULL;
    return av_malloc(items * size);
}

static void png_zfree(void *opaque, void *ptr)
{
    av_free(ptr);
}

static int png_get_nb_channels(int color_type)
{
    int channels;
    channels = 1;
    if ((color_type & (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_PALETTE)) ==
        PNG_COLOR_MASK_COLOR)
        channels = 3;
    if (color_type & PNG_COLOR_MASK_ALPHA)
        channels++;
    return channels;
}

/* compute the row size of an interleaved pass */
static int png_pass_row_size(int pass, int bits_per_pixel, int width)
{
    int shift, xmin, pass_width;

    xmin = png_pass_xmin[pass];
    if (width <= xmin)
        return 0;
    shift = png_pass_xshift[pass];
    pass_width = (width - xmin + (1 << shift) - 1) >> shift;
    return (pass_width * bits_per_pixel + 7) >> 3;
}

/* NOTE: we try to construct a good looking image at each pass. width
   is the original image width. We also do pixel format convertion at
   this stage */
static void png_put_interlaced_row(uint8_t *dst, int width,
                                   int bits_per_pixel, int pass,
                                   int color_type, const uint8_t *src)
{
    int x, mask, dsp_mask, j, src_x, b, bpp;
    uint8_t *d;
    const uint8_t *s;

    mask = png_pass_mask[pass];
    dsp_mask = png_pass_dsp_mask[pass];
    switch(bits_per_pixel) {
    case 1:
        /* we must intialize the line to zero before writing to it */
        if (pass == 0)
            memset(dst, 0, (width + 7) >> 3);
        src_x = 0;
        for(x = 0; x < width; x++) {
            j = (x & 7);
            if ((dsp_mask << j) & 0x80) {
                b = (src[src_x >> 3] >> (7 - (src_x & 7))) & 1;
                dst[x >> 3] |= b << (7 - j);
            }
            if ((mask << j) & 0x80)
                src_x++;
        }
        break;
    default:
        bpp = bits_per_pixel >> 3;
        d = dst;
        s = src;
        if (color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
            for(x = 0; x < width; x++) {
                j = x & 7;
                if ((dsp_mask << j) & 0x80) {
                    *(uint32_t *)d = (s[3] << 24) | (s[0] << 16) | (s[1] << 8) | s[2];
                }
                d += bpp;
                if ((mask << j) & 0x80)
                    s += bpp;
            }
        } else {
            for(x = 0; x < width; x++) {
                j = x & 7;
                if ((dsp_mask << j) & 0x80) {
                    memcpy(d, s, bpp);
                }
                d += bpp;
                if ((mask << j) & 0x80)
                    s += bpp;
            }
        }
        break;
    }
}

#ifdef CONFIG_ENCODERS
static void png_get_interlaced_row(uint8_t *dst, int row_size,
                                   int bits_per_pixel, int pass,
                                   const uint8_t *src, int width)
{
    int x, mask, dst_x, j, b, bpp;
    uint8_t *d;
    const uint8_t *s;

    mask = png_pass_mask[pass];
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
#endif

/* XXX: optimize */
/* NOTE: 'dst' can be equal to 'last' */
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

#ifdef CONFIG_ENCODERS
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
#endif

#ifdef CONFIG_DECODERS
static void convert_to_rgb32(uint8_t *dst, const uint8_t *src, int width)
{
    int j;
    unsigned int r, g, b, a;

    for(j = 0;j < width; j++) {
        r = src[0];
        g = src[1];
        b = src[2];
        a = src[3];
        *(uint32_t *)dst = (a << 24) | (r << 16) | (g << 8) | b;
        dst += 4;
        src += 4;
    }
}

/* process exactly one decompressed row */
static void png_handle_row(PNGContext *s)
{
    uint8_t *ptr, *last_row;
    int got_line;

    if (!s->interlace_type) {
        ptr = s->image_buf + s->image_linesize * s->y;
        /* need to swap bytes correctly for RGB_ALPHA */
        if (s->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
            png_filter_row(s->tmp_row, s->crow_buf[0], s->crow_buf + 1,
                           s->last_row, s->row_size, s->bpp);
            memcpy(s->last_row, s->tmp_row, s->row_size);
            convert_to_rgb32(ptr, s->tmp_row, s->width);
        } else {
            /* in normal case, we avoid one copy */
            if (s->y == 0)
                last_row = s->last_row;
            else
                last_row = ptr - s->image_linesize;

            png_filter_row(ptr, s->crow_buf[0], s->crow_buf + 1,
                           last_row, s->row_size, s->bpp);
        }
        s->y++;
        if (s->y == s->height) {
            s->state |= PNG_ALLIMAGE;
        }
    } else {
        got_line = 0;
        for(;;) {
            ptr = s->image_buf + s->image_linesize * s->y;
            if ((png_pass_ymask[s->pass] << (s->y & 7)) & 0x80) {
                /* if we already read one row, it is time to stop to
                   wait for the next one */
                if (got_line)
                    break;
                png_filter_row(s->tmp_row, s->crow_buf[0], s->crow_buf + 1,
                               s->last_row, s->pass_row_size, s->bpp);
                memcpy(s->last_row, s->tmp_row, s->pass_row_size);
                got_line = 1;
            }
            if ((png_pass_dsp_ymask[s->pass] << (s->y & 7)) & 0x80) {
                /* NOTE: RGB32 is handled directly in png_put_interlaced_row */
                png_put_interlaced_row(ptr, s->width, s->bits_per_pixel, s->pass,
                                       s->color_type, s->last_row);
            }
            s->y++;
            if (s->y == s->height) {
                for(;;) {
                    if (s->pass == NB_PASSES - 1) {
                        s->state |= PNG_ALLIMAGE;
                        goto the_end;
                    } else {
                        s->pass++;
                        s->y = 0;
                        s->pass_row_size = png_pass_row_size(s->pass,
                                                             s->bits_per_pixel,
                                                             s->width);
                        s->crow_size = s->pass_row_size + 1;
                        if (s->pass_row_size != 0)
                            break;
                        /* skip pass if empty row */
                    }
                }
            }
        }
    the_end: ;
    }
}

static int png_decode_idat(PNGContext *s, int length)
{
    int ret;
    s->zstream.avail_in = length;
    s->zstream.next_in = s->bytestream;
    s->bytestream += length;

    if(s->bytestream > s->bytestream_end)
        return -1;

    /* decode one line if possible */
    while (s->zstream.avail_in > 0) {
        ret = inflate(&s->zstream, Z_PARTIAL_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            return -1;
        }
        if (s->zstream.avail_out == 0) {
            if (!(s->state & PNG_ALLIMAGE)) {
                png_handle_row(s);
            }
            s->zstream.avail_out = s->crow_size;
            s->zstream.next_out = s->crow_buf;
        }
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    PNGContext * const s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    uint32_t tag, length;
    int ret, crc;

    s->bytestream_start=
    s->bytestream= buf;
    s->bytestream_end= buf + buf_size;

    /* check signature */
    if (memcmp(s->bytestream, pngsig, 8) != 0)
        return -1;
    s->bytestream+= 8;
    s->y=
    s->state=0;
//    memset(s, 0, sizeof(PNGContext));
    /* init the zlib */
    s->zstream.zalloc = png_zalloc;
    s->zstream.zfree = png_zfree;
    s->zstream.opaque = NULL;
    ret = inflateInit(&s->zstream);
    if (ret != Z_OK)
        return -1;
    for(;;) {
        int tag32;
        if (s->bytestream >= s->bytestream_end)
            goto fail;
        length = get32(&s->bytestream);
        if (length > 0x7fffffff)
            goto fail;
        tag32 = get32(&s->bytestream);
        tag = bswap_32(tag32);
#ifdef DEBUG
        av_log(avctx, AV_LOG_DEBUG, "png: tag=%c%c%c%c length=%u\n",
               (tag & 0xff),
               ((tag >> 8) & 0xff),
               ((tag >> 16) & 0xff),
               ((tag >> 24) & 0xff), length);
#endif
        switch(tag) {
        case MKTAG('I', 'H', 'D', 'R'):
            if (length != 13)
                goto fail;
            s->width = get32(&s->bytestream);
            s->height = get32(&s->bytestream);
            if(avcodec_check_dimensions(avctx, s->width, s->height)){
                s->width= s->height= 0;
                goto fail;
            }
            s->bit_depth = *s->bytestream++;
            s->color_type = *s->bytestream++;
            s->compression_type = *s->bytestream++;
            s->filter_type = *s->bytestream++;
            s->interlace_type = *s->bytestream++;
            crc = get32(&s->bytestream);
            s->state |= PNG_IHDR;
#ifdef DEBUG
            av_log(avctx, AV_LOG_DEBUG, "width=%d height=%d depth=%d color_type=%d compression_type=%d filter_type=%d interlace_type=%d\n",
                   s->width, s->height, s->bit_depth, s->color_type,
                   s->compression_type, s->filter_type, s->interlace_type);
#endif
            break;
        case MKTAG('I', 'D', 'A', 'T'):
            if (!(s->state & PNG_IHDR))
                goto fail;
            if (!(s->state & PNG_IDAT)) {
                /* init image info */
                avctx->width = s->width;
                avctx->height = s->height;

                s->channels = png_get_nb_channels(s->color_type);
                s->bits_per_pixel = s->bit_depth * s->channels;
                s->bpp = (s->bits_per_pixel + 7) >> 3;
                s->row_size = (avctx->width * s->bits_per_pixel + 7) >> 3;

                if (s->bit_depth == 8 &&
                    s->color_type == PNG_COLOR_TYPE_RGB) {
                    avctx->pix_fmt = PIX_FMT_RGB24;
                } else if (s->bit_depth == 8 &&
                           s->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
                    avctx->pix_fmt = PIX_FMT_RGB32;
                } else if (s->bit_depth == 8 &&
                           s->color_type == PNG_COLOR_TYPE_GRAY) {
                    avctx->pix_fmt = PIX_FMT_GRAY8;
                } else if (s->bit_depth == 16 &&
                           s->color_type == PNG_COLOR_TYPE_GRAY) {
                    avctx->pix_fmt = PIX_FMT_GRAY16BE;
                } else if (s->bit_depth == 1 &&
                           s->color_type == PNG_COLOR_TYPE_GRAY) {
                    avctx->pix_fmt = PIX_FMT_MONOBLACK;
                } else if (s->color_type == PNG_COLOR_TYPE_PALETTE) {
                    avctx->pix_fmt = PIX_FMT_PAL8;
                } else {
                    goto fail;
                }
                if(p->data[0])
                    avctx->release_buffer(avctx, p);

                p->reference= 0;
                if(avctx->get_buffer(avctx, p) < 0){
                    av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
                    goto fail;
                }
                p->pict_type= FF_I_TYPE;
                p->key_frame= 1;
                p->interlaced_frame = !!s->interlace_type;

                /* compute the compressed row size */
                if (!s->interlace_type) {
                    s->crow_size = s->row_size + 1;
                } else {
                    s->pass = 0;
                    s->pass_row_size = png_pass_row_size(s->pass,
                                                         s->bits_per_pixel,
                                                         s->width);
                    s->crow_size = s->pass_row_size + 1;
                }
#ifdef DEBUG
                av_log(avctx, AV_LOG_DEBUG, "row_size=%d crow_size =%d\n",
                       s->row_size, s->crow_size);
#endif
                s->image_buf = p->data[0];
                s->image_linesize = p->linesize[0];
                /* copy the palette if needed */
                if (s->color_type == PNG_COLOR_TYPE_PALETTE)
                    memcpy(p->data[1], s->palette, 256 * sizeof(uint32_t));
                /* empty row is used if differencing to the first row */
                s->last_row = av_mallocz(s->row_size);
                if (!s->last_row)
                    goto fail;
                if (s->interlace_type ||
                    s->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
                    s->tmp_row = av_malloc(s->row_size);
                    if (!s->tmp_row)
                        goto fail;
                }
                /* compressed row */
                s->crow_buf = av_malloc(s->row_size + 1);
                if (!s->crow_buf)
                    goto fail;
                s->zstream.avail_out = s->crow_size;
                s->zstream.next_out = s->crow_buf;
            }
            s->state |= PNG_IDAT;
            if (png_decode_idat(s, length) < 0)
                goto fail;
            /* skip crc */
            crc = get32(&s->bytestream);
            break;
        case MKTAG('P', 'L', 'T', 'E'):
            {
                int n, i, r, g, b;

                if ((length % 3) != 0 || length > 256 * 3)
                    goto skip_tag;
                /* read the palette */
                n = length / 3;
                for(i=0;i<n;i++) {
                    r = *s->bytestream++;
                    g = *s->bytestream++;
                    b = *s->bytestream++;
                    s->palette[i] = (0xff << 24) | (r << 16) | (g << 8) | b;
                }
                for(;i<256;i++) {
                    s->palette[i] = (0xff << 24);
                }
                s->state |= PNG_PLTE;
                crc = get32(&s->bytestream);
            }
            break;
        case MKTAG('t', 'R', 'N', 'S'):
            {
                int v, i;

                /* read the transparency. XXX: Only palette mode supported */
                if (s->color_type != PNG_COLOR_TYPE_PALETTE ||
                    length > 256 ||
                    !(s->state & PNG_PLTE))
                    goto skip_tag;
                for(i=0;i<length;i++) {
                    v = *s->bytestream++;
                    s->palette[i] = (s->palette[i] & 0x00ffffff) | (v << 24);
                }
                crc = get32(&s->bytestream);
            }
            break;
        case MKTAG('I', 'E', 'N', 'D'):
            if (!(s->state & PNG_ALLIMAGE))
                goto fail;
            crc = get32(&s->bytestream);
            goto exit_loop;
        default:
            /* skip tag */
        skip_tag:
            s->bytestream += length + 4;
            break;
        }
    }
 exit_loop:
    *picture= *(AVFrame*)&s->picture;
    *data_size = sizeof(AVPicture);

    ret = s->bytestream - s->bytestream_start;
 the_end:
    inflateEnd(&s->zstream);
    av_freep(&s->crow_buf);
    av_freep(&s->last_row);
    av_freep(&s->tmp_row);
    return ret;
 fail:
    ret = -1;
    goto the_end;
}
#endif

#ifdef CONFIG_ENCODERS
static void png_write_chunk(uint8_t **f, uint32_t tag,
                            const uint8_t *buf, int length)
{
    uint32_t crc;
    uint8_t tagbuf[4];

    put32(f, length);
    crc = crc32(0, Z_NULL, 0);
    tagbuf[0] = tag;
    tagbuf[1] = tag >> 8;
    tagbuf[2] = tag >> 16;
    tagbuf[3] = tag >> 24;
    crc = crc32(crc, tagbuf, 4);
    put32(f, bswap_32(tag));
    if (length > 0) {
        crc = crc32(crc, buf, length);
        memcpy(*f, buf, length);
        *f += length;
    }
    put32(f, crc);
}

/* XXX: use avcodec generic function ? */
static void to_be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

/* XXX: do filtering */
static int png_write_row(PNGContext *s, const uint8_t *data, int size)
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
#endif /* CONFIG_ENCODERS */

static int common_init(AVCodecContext *avctx){
    PNGContext *s = avctx->priv_data;

    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame= (AVFrame*)&s->picture;
//    s->avctx= avctx;

    return 0;
}

#ifdef CONFIG_ENCODERS
static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    PNGContext *s = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    int bit_depth, color_type, y, len, row_size, ret, is_progressive;
    int bits_per_pixel, pass_row_size;
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
    bits_per_pixel = png_get_nb_channels(color_type) * bit_depth;
    row_size = (avctx->width * bits_per_pixel + 7) >> 3;

    s->zstream.zalloc = png_zalloc;
    s->zstream.zfree = png_zfree;
    s->zstream.opaque = NULL;
    ret = deflateInit2(&s->zstream, Z_DEFAULT_COMPRESSION,
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
    memcpy(s->bytestream, pngsig, 8);
    s->bytestream += 8;

    to_be32(s->buf, avctx->width);
    to_be32(s->buf + 4, avctx->height);
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
            ptr[0] = v >> 16;
            ptr[1] = v >> 8;
            ptr[2] = v;
            ptr += 3;
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
            pass_row_size = png_pass_row_size(pass, bits_per_pixel, avctx->width);
            if (pass_row_size > 0) {
                for(y = 0; y < avctx->height; y++) {
                    if ((png_pass_ymask[pass] << (y & 7)) & 0x80) {
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
#endif

#ifdef CONFIG_PNG_DECODER
AVCodec png_decoder = {
    "png",
    CODEC_TYPE_VIDEO,
    CODEC_ID_PNG,
    sizeof(PNGContext),
    common_init,
    NULL,
    NULL, //decode_end,
    decode_frame,
    0 /*CODEC_CAP_DR1*/ /*| CODEC_CAP_DRAW_HORIZ_BAND*/,
    NULL
};
#endif

#ifdef CONFIG_PNG_ENCODER
AVCodec png_encoder = {
    "png",
    CODEC_TYPE_VIDEO,
    CODEC_ID_PNG,
    sizeof(PNGContext),
    common_init,
    encode_frame,
    NULL, //encode_end,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_RGB24, PIX_FMT_RGB32, PIX_FMT_PAL8, PIX_FMT_GRAY8, PIX_FMT_MONOBLACK, -1},
};
#endif // CONFIG_PNG_ENCODER
