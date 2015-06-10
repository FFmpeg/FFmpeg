/*
 * PNG image format
 * Copyright (c) 2003 Fabrice Bellard
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

//#define DEBUG

#include "libavutil/bprint.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "apng.h"
#include "png.h"
#include "pngdsp.h"
#include "thread.h"

#include <zlib.h>

typedef struct PNGDecContext {
    PNGDSPContext dsp;
    AVCodecContext *avctx;

    GetByteContext gb;
    ThreadFrame previous_picture;
    ThreadFrame last_picture;
    ThreadFrame picture;

    int state;
    int width, height;
    int cur_w, cur_h;
    int last_w, last_h;
    int x_offset, y_offset;
    int last_x_offset, last_y_offset;
    uint8_t dispose_op, blend_op;
    uint8_t last_dispose_op;
    int bit_depth;
    int color_type;
    int compression_type;
    int interlace_type;
    int filter_type;
    int channels;
    int bits_per_pixel;
    int bpp;
    int has_trns;

    uint8_t *image_buf;
    int image_linesize;
    uint32_t palette[256];
    uint8_t *crow_buf;
    uint8_t *last_row;
    unsigned int last_row_size;
    uint8_t *tmp_row;
    unsigned int tmp_row_size;
    uint8_t *buffer;
    int buffer_size;
    int pass;
    int crow_size; /* compressed row size (include filter type) */
    int row_size; /* decompressed row size */
    int pass_row_size; /* decompress row size of the current pass */
    int y;
    z_stream zstream;
} PNGDecContext;

/* Mask to determine which pixels are valid in a pass */
static const uint8_t png_pass_mask[NB_PASSES] = {
    0x01, 0x01, 0x11, 0x11, 0x55, 0x55, 0xff,
};

/* Mask to determine which y pixels can be written in a pass */
static const uint8_t png_pass_dsp_ymask[NB_PASSES] = {
    0xff, 0xff, 0x0f, 0xff, 0x33, 0xff, 0x55,
};

/* Mask to determine which pixels to overwrite while displaying */
static const uint8_t png_pass_dsp_mask[NB_PASSES] = {
    0xff, 0x0f, 0xff, 0x33, 0xff, 0x55, 0xff
};

/* NOTE: we try to construct a good looking image at each pass. width
 * is the original image width. We also do pixel format conversion at
 * this stage */
static void png_put_interlaced_row(uint8_t *dst, int width,
                                   int bits_per_pixel, int pass,
                                   int color_type, const uint8_t *src)
{
    int x, mask, dsp_mask, j, src_x, b, bpp;
    uint8_t *d;
    const uint8_t *s;

    mask     = png_pass_mask[pass];
    dsp_mask = png_pass_dsp_mask[pass];

    switch (bits_per_pixel) {
    case 1:
        src_x = 0;
        for (x = 0; x < width; x++) {
            j = (x & 7);
            if ((dsp_mask << j) & 0x80) {
                b = (src[src_x >> 3] >> (7 - (src_x & 7))) & 1;
                dst[x >> 3] &= 0xFF7F>>j;
                dst[x >> 3] |= b << (7 - j);
            }
            if ((mask << j) & 0x80)
                src_x++;
        }
        break;
    case 2:
        src_x = 0;
        for (x = 0; x < width; x++) {
            int j2 = 2 * (x & 3);
            j = (x & 7);
            if ((dsp_mask << j) & 0x80) {
                b = (src[src_x >> 2] >> (6 - 2*(src_x & 3))) & 3;
                dst[x >> 2] &= 0xFF3F>>j2;
                dst[x >> 2] |= b << (6 - j2);
            }
            if ((mask << j) & 0x80)
                src_x++;
        }
        break;
    case 4:
        src_x = 0;
        for (x = 0; x < width; x++) {
            int j2 = 4*(x&1);
            j = (x & 7);
            if ((dsp_mask << j) & 0x80) {
                b = (src[src_x >> 1] >> (4 - 4*(src_x & 1))) & 15;
                dst[x >> 1] &= 0xFF0F>>j2;
                dst[x >> 1] |= b << (4 - j2);
            }
            if ((mask << j) & 0x80)
                src_x++;
        }
        break;
    default:
        bpp = bits_per_pixel >> 3;
        d   = dst;
        s   = src;
            for (x = 0; x < width; x++) {
                j = x & 7;
                if ((dsp_mask << j) & 0x80) {
                    memcpy(d, s, bpp);
                }
                d += bpp;
                if ((mask << j) & 0x80)
                    s += bpp;
            }
        break;
    }
}

void ff_add_png_paeth_prediction(uint8_t *dst, uint8_t *src, uint8_t *top,
                                 int w, int bpp)
{
    int i;
    for (i = 0; i < w; i++) {
        int a, b, c, p, pa, pb, pc;

        a = dst[i - bpp];
        b = top[i];
        c = top[i - bpp];

        p  = b - c;
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
}

#define UNROLL1(bpp, op)                                                      \
    {                                                                         \
        r = dst[0];                                                           \
        if (bpp >= 2)                                                         \
            g = dst[1];                                                       \
        if (bpp >= 3)                                                         \
            b = dst[2];                                                       \
        if (bpp >= 4)                                                         \
            a = dst[3];                                                       \
        for (; i <= size - bpp; i += bpp) {                                   \
            dst[i + 0] = r = op(r, src[i + 0], last[i + 0]);                  \
            if (bpp == 1)                                                     \
                continue;                                                     \
            dst[i + 1] = g = op(g, src[i + 1], last[i + 1]);                  \
            if (bpp == 2)                                                     \
                continue;                                                     \
            dst[i + 2] = b = op(b, src[i + 2], last[i + 2]);                  \
            if (bpp == 3)                                                     \
                continue;                                                     \
            dst[i + 3] = a = op(a, src[i + 3], last[i + 3]);                  \
        }                                                                     \
    }

#define UNROLL_FILTER(op)                                                     \
    if (bpp == 1) {                                                           \
        UNROLL1(1, op)                                                        \
    } else if (bpp == 2) {                                                    \
        UNROLL1(2, op)                                                        \
    } else if (bpp == 3) {                                                    \
        UNROLL1(3, op)                                                        \
    } else if (bpp == 4) {                                                    \
        UNROLL1(4, op)                                                        \
    }                                                                         \
    for (; i < size; i++) {                                                   \
        dst[i] = op(dst[i - bpp], src[i], last[i]);                           \
    }

/* NOTE: 'dst' can be equal to 'last' */
static void png_filter_row(PNGDSPContext *dsp, uint8_t *dst, int filter_type,
                           uint8_t *src, uint8_t *last, int size, int bpp)
{
    int i, p, r, g, b, a;

    switch (filter_type) {
    case PNG_FILTER_VALUE_NONE:
        memcpy(dst, src, size);
        break;
    case PNG_FILTER_VALUE_SUB:
        for (i = 0; i < bpp; i++)
            dst[i] = src[i];
        if (bpp == 4) {
            p = *(int *)dst;
            for (; i < size; i += bpp) {
                unsigned s = *(int *)(src + i);
                p = ((s & 0x7f7f7f7f) + (p & 0x7f7f7f7f)) ^ ((s ^ p) & 0x80808080);
                *(int *)(dst + i) = p;
            }
        } else {
#define OP_SUB(x, s, l) ((x) + (s))
            UNROLL_FILTER(OP_SUB);
        }
        break;
    case PNG_FILTER_VALUE_UP:
        dsp->add_bytes_l2(dst, src, last, size);
        break;
    case PNG_FILTER_VALUE_AVG:
        for (i = 0; i < bpp; i++) {
            p      = (last[i] >> 1);
            dst[i] = p + src[i];
        }
#define OP_AVG(x, s, l) (((((x) + (l)) >> 1) + (s)) & 0xff)
        UNROLL_FILTER(OP_AVG);
        break;
    case PNG_FILTER_VALUE_PAETH:
        for (i = 0; i < bpp; i++) {
            p      = last[i];
            dst[i] = p + src[i];
        }
        if (bpp > 2 && size > 4) {
            /* would write off the end of the array if we let it process
             * the last pixel with bpp=3 */
            int w = (bpp & 3) ? size - 3 : size;

            if (w > i) {
                dsp->add_paeth_prediction(dst + i, src + i, last + i, size - i, bpp);
                i = w;
            }
        }
        ff_add_png_paeth_prediction(dst + i, src + i, last + i, size - i, bpp);
        break;
    }
}

/* This used to be called "deloco" in FFmpeg
 * and is actually an inverse reversible colorspace transformation */
#define YUV2RGB(NAME, TYPE) \
static void deloco_ ## NAME(TYPE *dst, int size, int alpha) \
{ \
    int i; \
    for (i = 0; i < size; i += 3 + alpha) { \
        int g = dst [i + 1]; \
        dst[i + 0] += g; \
        dst[i + 2] += g; \
    } \
}

YUV2RGB(rgb8, uint8_t)
YUV2RGB(rgb16, uint16_t)

/* process exactly one decompressed row */
static void png_handle_row(PNGDecContext *s)
{
    uint8_t *ptr, *last_row;
    int got_line;

    if (!s->interlace_type) {
        ptr = s->image_buf + s->image_linesize * (s->y + s->y_offset) + s->x_offset * s->bpp;
        if (s->y == 0)
            last_row = s->last_row;
        else
            last_row = ptr - s->image_linesize;

        png_filter_row(&s->dsp, ptr, s->crow_buf[0], s->crow_buf + 1,
                       last_row, s->row_size, s->bpp);
        /* loco lags by 1 row so that it doesn't interfere with top prediction */
        if (s->filter_type == PNG_FILTER_TYPE_LOCO && s->y > 0) {
            if (s->bit_depth == 16) {
                deloco_rgb16((uint16_t *)(ptr - s->image_linesize), s->row_size / 2,
                             s->color_type == PNG_COLOR_TYPE_RGB_ALPHA);
            } else {
                deloco_rgb8(ptr - s->image_linesize, s->row_size,
                            s->color_type == PNG_COLOR_TYPE_RGB_ALPHA);
            }
        }
        s->y++;
        if (s->y == s->cur_h) {
            s->state |= PNG_ALLIMAGE;
            if (s->filter_type == PNG_FILTER_TYPE_LOCO) {
                if (s->bit_depth == 16) {
                    deloco_rgb16((uint16_t *)ptr, s->row_size / 2,
                                 s->color_type == PNG_COLOR_TYPE_RGB_ALPHA);
                } else {
                    deloco_rgb8(ptr, s->row_size,
                                s->color_type == PNG_COLOR_TYPE_RGB_ALPHA);
                }
            }
        }
    } else {
        got_line = 0;
        for (;;) {
            ptr = s->image_buf + s->image_linesize * (s->y + s->y_offset) + s->x_offset * s->bpp;
            if ((ff_png_pass_ymask[s->pass] << (s->y & 7)) & 0x80) {
                /* if we already read one row, it is time to stop to
                 * wait for the next one */
                if (got_line)
                    break;
                png_filter_row(&s->dsp, s->tmp_row, s->crow_buf[0], s->crow_buf + 1,
                               s->last_row, s->pass_row_size, s->bpp);
                FFSWAP(uint8_t *, s->last_row, s->tmp_row);
                FFSWAP(unsigned int, s->last_row_size, s->tmp_row_size);
                got_line = 1;
            }
            if ((png_pass_dsp_ymask[s->pass] << (s->y & 7)) & 0x80) {
                png_put_interlaced_row(ptr, s->cur_w, s->bits_per_pixel, s->pass,
                                       s->color_type, s->last_row);
            }
            s->y++;
            if (s->y == s->cur_h) {
                memset(s->last_row, 0, s->row_size);
                for (;;) {
                    if (s->pass == NB_PASSES - 1) {
                        s->state |= PNG_ALLIMAGE;
                        goto the_end;
                    } else {
                        s->pass++;
                        s->y = 0;
                        s->pass_row_size = ff_png_pass_row_size(s->pass,
                                                                s->bits_per_pixel,
                                                                s->cur_w);
                        s->crow_size = s->pass_row_size + 1;
                        if (s->pass_row_size != 0)
                            break;
                        /* skip pass if empty row */
                    }
                }
            }
        }
the_end:;
    }
}

static int png_decode_idat(PNGDecContext *s, int length)
{
    int ret;
    s->zstream.avail_in = FFMIN(length, bytestream2_get_bytes_left(&s->gb));
    s->zstream.next_in  = (unsigned char *)s->gb.buffer;
    bytestream2_skip(&s->gb, length);

    /* decode one line if possible */
    while (s->zstream.avail_in > 0) {
        ret = inflate(&s->zstream, Z_PARTIAL_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            av_log(s->avctx, AV_LOG_ERROR, "inflate returned error %d\n", ret);
            return AVERROR_EXTERNAL;
        }
        if (s->zstream.avail_out == 0) {
            if (!(s->state & PNG_ALLIMAGE)) {
                png_handle_row(s);
            }
            s->zstream.avail_out = s->crow_size;
            s->zstream.next_out  = s->crow_buf;
        }
        if (ret == Z_STREAM_END && s->zstream.avail_in > 0) {
            av_log(NULL, AV_LOG_WARNING,
                   "%d undecompressed bytes left in buffer\n", s->zstream.avail_in);
            return 0;
        }
    }
    return 0;
}

static int decode_zbuf(AVBPrint *bp, const uint8_t *data,
                       const uint8_t *data_end)
{
    z_stream zstream;
    unsigned char *buf;
    unsigned buf_size;
    int ret;

    zstream.zalloc = ff_png_zalloc;
    zstream.zfree  = ff_png_zfree;
    zstream.opaque = NULL;
    if (inflateInit(&zstream) != Z_OK)
        return AVERROR_EXTERNAL;
    zstream.next_in  = (unsigned char *)data;
    zstream.avail_in = data_end - data;
    av_bprint_init(bp, 0, -1);

    while (zstream.avail_in > 0) {
        av_bprint_get_buffer(bp, 1, &buf, &buf_size);
        if (!buf_size) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        zstream.next_out  = buf;
        zstream.avail_out = buf_size;
        ret = inflate(&zstream, Z_PARTIAL_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            ret = AVERROR_EXTERNAL;
            goto fail;
        }
        bp->len += zstream.next_out - buf;
        if (ret == Z_STREAM_END)
            break;
    }
    inflateEnd(&zstream);
    bp->str[bp->len] = 0;
    return 0;

fail:
    inflateEnd(&zstream);
    av_bprint_finalize(bp, NULL);
    return ret;
}

static uint8_t *iso88591_to_utf8(const uint8_t *in, size_t size_in)
{
    size_t extra = 0, i;
    uint8_t *out, *q;

    for (i = 0; i < size_in; i++)
        extra += in[i] >= 0x80;
    if (size_in == SIZE_MAX || extra > SIZE_MAX - size_in - 1)
        return NULL;
    q = out = av_malloc(size_in + extra + 1);
    if (!out)
        return NULL;
    for (i = 0; i < size_in; i++) {
        if (in[i] >= 0x80) {
            *(q++) = 0xC0 | (in[i] >> 6);
            *(q++) = 0x80 | (in[i] & 0x3F);
        } else {
            *(q++) = in[i];
        }
    }
    *(q++) = 0;
    return out;
}

static int decode_text_chunk(PNGDecContext *s, uint32_t length, int compressed,
                             AVDictionary **dict)
{
    int ret, method;
    const uint8_t *data        = s->gb.buffer;
    const uint8_t *data_end    = data + length;
    const uint8_t *keyword     = data;
    const uint8_t *keyword_end = memchr(keyword, 0, data_end - keyword);
    uint8_t *kw_utf8 = NULL, *text, *txt_utf8 = NULL;
    unsigned text_len;
    AVBPrint bp;

    if (!keyword_end)
        return AVERROR_INVALIDDATA;
    data = keyword_end + 1;

    if (compressed) {
        if (data == data_end)
            return AVERROR_INVALIDDATA;
        method = *(data++);
        if (method)
            return AVERROR_INVALIDDATA;
        if ((ret = decode_zbuf(&bp, data, data_end)) < 0)
            return ret;
        text_len = bp.len;
        av_bprint_finalize(&bp, (char **)&text);
        if (!text)
            return AVERROR(ENOMEM);
    } else {
        text = (uint8_t *)data;
        text_len = data_end - text;
    }

    kw_utf8  = iso88591_to_utf8(keyword, keyword_end - keyword);
    txt_utf8 = iso88591_to_utf8(text, text_len);
    if (text != data)
        av_free(text);
    if (!(kw_utf8 && txt_utf8)) {
        av_free(kw_utf8);
        av_free(txt_utf8);
        return AVERROR(ENOMEM);
    }

    av_dict_set(dict, kw_utf8, txt_utf8,
                AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);
    return 0;
}

static int decode_ihdr_chunk(AVCodecContext *avctx, PNGDecContext *s,
                             uint32_t length)
{
    if (length != 13)
        return AVERROR_INVALIDDATA;

    if (s->state & PNG_IDAT) {
        av_log(avctx, AV_LOG_ERROR, "IHDR after IDAT\n");
        return AVERROR_INVALIDDATA;
    }

    s->width  = bytestream2_get_be32(&s->gb);
    s->height = bytestream2_get_be32(&s->gb);
    if (av_image_check_size(s->width, s->height, 0, avctx)) {
        s->width = s->height = 0;
        av_log(avctx, AV_LOG_ERROR, "Invalid image size\n");
        return AVERROR_INVALIDDATA;
    }
    if (s->cur_w == 0 && s->cur_h == 0) {
        // Only set cur_w/h if update_thread_context() has not set it
        s->cur_w = s->width;
        s->cur_h = s->height;
    }
    s->bit_depth        = bytestream2_get_byte(&s->gb);
    s->color_type       = bytestream2_get_byte(&s->gb);
    s->compression_type = bytestream2_get_byte(&s->gb);
    s->filter_type      = bytestream2_get_byte(&s->gb);
    s->interlace_type   = bytestream2_get_byte(&s->gb);
    bytestream2_skip(&s->gb, 4); /* crc */
    s->state |= PNG_IHDR;
    if (avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_DEBUG, "width=%d height=%d depth=%d color_type=%d "
                "compression_type=%d filter_type=%d interlace_type=%d\n",
                s->width, s->height, s->bit_depth, s->color_type,
                s->compression_type, s->filter_type, s->interlace_type);

    return 0;
}

static int decode_phys_chunk(AVCodecContext *avctx, PNGDecContext *s)
{
    if (s->state & PNG_IDAT) {
        av_log(avctx, AV_LOG_ERROR, "pHYs after IDAT\n");
        return AVERROR_INVALIDDATA;
    }
    avctx->sample_aspect_ratio.num = bytestream2_get_be32(&s->gb);
    avctx->sample_aspect_ratio.den = bytestream2_get_be32(&s->gb);
    if (avctx->sample_aspect_ratio.num < 0 || avctx->sample_aspect_ratio.den < 0)
        avctx->sample_aspect_ratio = (AVRational){ 0, 1 };
    bytestream2_skip(&s->gb, 1); /* unit specifier */
    bytestream2_skip(&s->gb, 4); /* crc */

    return 0;
}

static int decode_idat_chunk(AVCodecContext *avctx, PNGDecContext *s,
                             uint32_t length, AVFrame *p)
{
    int ret;

    if (!(s->state & PNG_IHDR)) {
        av_log(avctx, AV_LOG_ERROR, "IDAT without IHDR\n");
        return AVERROR_INVALIDDATA;
    }
    if (!(s->state & PNG_IDAT)) {
        /* init image info */
        avctx->width  = s->width;
        avctx->height = s->height;

        s->channels       = ff_png_get_nb_channels(s->color_type);
        s->bits_per_pixel = s->bit_depth * s->channels;
        s->bpp            = (s->bits_per_pixel + 7) >> 3;
        s->row_size       = (s->cur_w * s->bits_per_pixel + 7) >> 3;

        if ((s->bit_depth == 2 || s->bit_depth == 4 || s->bit_depth == 8) &&
                s->color_type == PNG_COLOR_TYPE_RGB) {
            avctx->pix_fmt = AV_PIX_FMT_RGB24;
        } else if ((s->bit_depth == 2 || s->bit_depth == 4 || s->bit_depth == 8) &&
                s->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
            avctx->pix_fmt = AV_PIX_FMT_RGBA;
        } else if ((s->bit_depth == 2 || s->bit_depth == 4 || s->bit_depth == 8) &&
                s->color_type == PNG_COLOR_TYPE_GRAY) {
            avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        } else if (s->bit_depth == 16 &&
                s->color_type == PNG_COLOR_TYPE_GRAY) {
            avctx->pix_fmt = AV_PIX_FMT_GRAY16BE;
        } else if (s->bit_depth == 16 &&
                s->color_type == PNG_COLOR_TYPE_RGB) {
            avctx->pix_fmt = AV_PIX_FMT_RGB48BE;
        } else if (s->bit_depth == 16 &&
                s->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
            avctx->pix_fmt = AV_PIX_FMT_RGBA64BE;
        } else if ((s->bits_per_pixel == 1 || s->bits_per_pixel == 2 || s->bits_per_pixel == 4 || s->bits_per_pixel == 8) &&
                s->color_type == PNG_COLOR_TYPE_PALETTE) {
            avctx->pix_fmt = AV_PIX_FMT_PAL8;
        } else if (s->bit_depth == 1 && s->bits_per_pixel == 1 && avctx->codec_id != AV_CODEC_ID_APNG) {
            avctx->pix_fmt = AV_PIX_FMT_MONOBLACK;
        } else if (s->bit_depth == 8 &&
                s->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
            avctx->pix_fmt = AV_PIX_FMT_YA8;
        } else if (s->bit_depth == 16 &&
                s->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
            avctx->pix_fmt = AV_PIX_FMT_YA16BE;
        } else {
            av_log(avctx, AV_LOG_ERROR, "unsupported bit depth %d "
                    "and color type %d\n",
                    s->bit_depth, s->color_type);
            return AVERROR_INVALIDDATA;
        }

        if ((ret = ff_thread_get_buffer(avctx, &s->picture, AV_GET_BUFFER_FLAG_REF)) < 0)
            return ret;
        ff_thread_finish_setup(avctx);

        p->pict_type        = AV_PICTURE_TYPE_I;
        p->key_frame        = 1;
        p->interlaced_frame = !!s->interlace_type;

        /* compute the compressed row size */
        if (!s->interlace_type) {
            s->crow_size = s->row_size + 1;
        } else {
            s->pass          = 0;
            s->pass_row_size = ff_png_pass_row_size(s->pass,
                    s->bits_per_pixel,
                    s->cur_w);
            s->crow_size = s->pass_row_size + 1;
        }
        ff_dlog(avctx, "row_size=%d crow_size =%d\n",
                s->row_size, s->crow_size);
        s->image_buf      = p->data[0];
        s->image_linesize = p->linesize[0];
        /* copy the palette if needed */
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8)
            memcpy(p->data[1], s->palette, 256 * sizeof(uint32_t));
        /* empty row is used if differencing to the first row */
        av_fast_padded_mallocz(&s->last_row, &s->last_row_size, s->row_size);
        if (!s->last_row)
            return AVERROR_INVALIDDATA;
        if (s->interlace_type ||
                s->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
            av_fast_padded_malloc(&s->tmp_row, &s->tmp_row_size, s->row_size);
            if (!s->tmp_row)
                return AVERROR_INVALIDDATA;
        }
        /* compressed row */
        av_fast_padded_malloc(&s->buffer, &s->buffer_size, s->row_size + 16);
        if (!s->buffer)
            return AVERROR(ENOMEM);

        /* we want crow_buf+1 to be 16-byte aligned */
        s->crow_buf          = s->buffer + 15;
        s->zstream.avail_out = s->crow_size;
        s->zstream.next_out  = s->crow_buf;
    }
    s->state |= PNG_IDAT;
    if ((ret = png_decode_idat(s, length)) < 0)
        return ret;
    bytestream2_skip(&s->gb, 4); /* crc */

    return 0;
}

static int decode_plte_chunk(AVCodecContext *avctx, PNGDecContext *s,
                             uint32_t length)
{
    int n, i, r, g, b;

    if ((length % 3) != 0 || length > 256 * 3)
        return AVERROR_INVALIDDATA;
    /* read the palette */
    n = length / 3;
    for (i = 0; i < n; i++) {
        r = bytestream2_get_byte(&s->gb);
        g = bytestream2_get_byte(&s->gb);
        b = bytestream2_get_byte(&s->gb);
        s->palette[i] = (0xFFU << 24) | (r << 16) | (g << 8) | b;
    }
    for (; i < 256; i++)
        s->palette[i] = (0xFFU << 24);
    s->state |= PNG_PLTE;
    bytestream2_skip(&s->gb, 4);     /* crc */

    return 0;
}

static int decode_trns_chunk(AVCodecContext *avctx, PNGDecContext *s,
                             uint32_t length)
{
    int v, i;

    /* read the transparency. XXX: Only palette mode supported */
    if (s->color_type != PNG_COLOR_TYPE_PALETTE ||
            length > 256 ||
            !(s->state & PNG_PLTE))
        return AVERROR_INVALIDDATA;
    for (i = 0; i < length; i++) {
        v = bytestream2_get_byte(&s->gb);
        s->palette[i] = (s->palette[i] & 0x00ffffff) | (v << 24);
    }
    bytestream2_skip(&s->gb, 4);     /* crc */

    s->has_trns = 1;

    return 0;
}

static void handle_small_bpp(PNGDecContext *s, AVFrame *p)
{
    if (s->bits_per_pixel == 1 && s->color_type == PNG_COLOR_TYPE_PALETTE) {
        int i, j, k;
        uint8_t *pd = p->data[0];
        for (j = 0; j < s->height; j++) {
            i = s->width / 8;
            for (k = 7; k >= 1; k--)
                if ((s->width&7) >= k)
                    pd[8*i + k - 1] = (pd[i]>>8-k) & 1;
            for (i--; i >= 0; i--) {
                pd[8*i + 7]=  pd[i]     & 1;
                pd[8*i + 6]= (pd[i]>>1) & 1;
                pd[8*i + 5]= (pd[i]>>2) & 1;
                pd[8*i + 4]= (pd[i]>>3) & 1;
                pd[8*i + 3]= (pd[i]>>4) & 1;
                pd[8*i + 2]= (pd[i]>>5) & 1;
                pd[8*i + 1]= (pd[i]>>6) & 1;
                pd[8*i + 0]=  pd[i]>>7;
            }
            pd += s->image_linesize;
        }
    } else if (s->bits_per_pixel == 2) {
        int i, j;
        uint8_t *pd = p->data[0];
        for (j = 0; j < s->height; j++) {
            i = s->width / 4;
            if (s->color_type == PNG_COLOR_TYPE_PALETTE) {
                if ((s->width&3) >= 3) pd[4*i + 2]= (pd[i] >> 2) & 3;
                if ((s->width&3) >= 2) pd[4*i + 1]= (pd[i] >> 4) & 3;
                if ((s->width&3) >= 1) pd[4*i + 0]=  pd[i] >> 6;
                for (i--; i >= 0; i--) {
                    pd[4*i + 3]=  pd[i]     & 3;
                    pd[4*i + 2]= (pd[i]>>2) & 3;
                    pd[4*i + 1]= (pd[i]>>4) & 3;
                    pd[4*i + 0]=  pd[i]>>6;
                }
            } else {
                if ((s->width&3) >= 3) pd[4*i + 2]= ((pd[i]>>2) & 3)*0x55;
                if ((s->width&3) >= 2) pd[4*i + 1]= ((pd[i]>>4) & 3)*0x55;
                if ((s->width&3) >= 1) pd[4*i + 0]= ( pd[i]>>6     )*0x55;
                for (i--; i >= 0; i--) {
                    pd[4*i + 3]= ( pd[i]     & 3)*0x55;
                    pd[4*i + 2]= ((pd[i]>>2) & 3)*0x55;
                    pd[4*i + 1]= ((pd[i]>>4) & 3)*0x55;
                    pd[4*i + 0]= ( pd[i]>>6     )*0x55;
                }
            }
            pd += s->image_linesize;
        }
    } else if (s->bits_per_pixel == 4) {
        int i, j;
        uint8_t *pd = p->data[0];
        for (j = 0; j < s->height; j++) {
            i = s->width/2;
            if (s->color_type == PNG_COLOR_TYPE_PALETTE) {
                if (s->width&1) pd[2*i+0]= pd[i]>>4;
                for (i--; i >= 0; i--) {
                    pd[2*i + 1] = pd[i] & 15;
                    pd[2*i + 0] = pd[i] >> 4;
                }
            } else {
                if (s->width & 1) pd[2*i + 0]= (pd[i] >> 4) * 0x11;
                for (i--; i >= 0; i--) {
                    pd[2*i + 1] = (pd[i] & 15) * 0x11;
                    pd[2*i + 0] = (pd[i] >> 4) * 0x11;
                }
            }
            pd += s->image_linesize;
        }
    }
}

static int decode_fctl_chunk(AVCodecContext *avctx, PNGDecContext *s,
                             uint32_t length)
{
    uint32_t sequence_number;

    if (length != 26)
        return AVERROR_INVALIDDATA;

    s->last_w = s->cur_w;
    s->last_h = s->cur_h;
    s->last_x_offset = s->x_offset;
    s->last_y_offset = s->y_offset;
    s->last_dispose_op = s->dispose_op;

    sequence_number = bytestream2_get_be32(&s->gb);
    s->cur_w        = bytestream2_get_be32(&s->gb);
    s->cur_h        = bytestream2_get_be32(&s->gb);
    s->x_offset     = bytestream2_get_be32(&s->gb);
    s->y_offset     = bytestream2_get_be32(&s->gb);
    bytestream2_skip(&s->gb, 4); /* delay_num (2), delay_den (2) */
    s->dispose_op   = bytestream2_get_byte(&s->gb);
    s->blend_op     = bytestream2_get_byte(&s->gb);
    bytestream2_skip(&s->gb, 4); /* crc */

    if (sequence_number == 0 &&
        (s->cur_w != s->width ||
         s->cur_h != s->height ||
         s->x_offset != 0 ||
         s->y_offset != 0) ||
        s->cur_w <= 0 || s->cur_h <= 0 ||
        s->x_offset < 0 || s->y_offset < 0 ||
        s->cur_w > s->width - s->x_offset|| s->cur_h > s->height - s->y_offset)
            return AVERROR_INVALIDDATA;

    if (sequence_number == 0 && s->dispose_op == APNG_DISPOSE_OP_PREVIOUS) {
        // No previous frame to revert to for the first frame
        // Spec says to just treat it as a APNG_DISPOSE_OP_BACKGROUND
        s->dispose_op = APNG_DISPOSE_OP_BACKGROUND;
    }

    if (s->dispose_op == APNG_BLEND_OP_OVER && !s->has_trns && (
            avctx->pix_fmt == AV_PIX_FMT_RGB24 ||
            avctx->pix_fmt == AV_PIX_FMT_RGB48BE ||
            avctx->pix_fmt == AV_PIX_FMT_PAL8 ||
            avctx->pix_fmt == AV_PIX_FMT_GRAY8 ||
            avctx->pix_fmt == AV_PIX_FMT_GRAY16BE ||
            avctx->pix_fmt == AV_PIX_FMT_MONOBLACK
        )) {
        // APNG_DISPOSE_OP_OVER is the same as APNG_DISPOSE_OP_SOURCE when there is no alpha channel
        s->dispose_op = APNG_BLEND_OP_SOURCE;
    }

    return 0;
}

static void handle_p_frame_png(PNGDecContext *s, AVFrame *p)
{
    int i, j;
    uint8_t *pd      = p->data[0];
    uint8_t *pd_last = s->last_picture.f->data[0];
    int ls = FFMIN(av_image_get_linesize(p->format, s->width, 0), s->width * s->bpp);

    ff_thread_await_progress(&s->last_picture, INT_MAX, 0);
    for (j = 0; j < s->height; j++) {
        for (i = 0; i < ls; i++)
            pd[i] += pd_last[i];
        pd      += s->image_linesize;
        pd_last += s->image_linesize;
    }
}

// divide by 255 and round to nearest
// apply a fast variant: (X+127)/255 = ((X+127)*257+257)>>16 = ((X+128)*257)>>16
#define FAST_DIV255(x) ((((x) + 128) * 257) >> 16)

static int handle_p_frame_apng(AVCodecContext *avctx, PNGDecContext *s,
                               AVFrame *p)
{
    size_t x, y;
    uint8_t *buffer = av_malloc(s->image_linesize * s->height);

    if (!buffer)
        return AVERROR(ENOMEM);

    if (s->blend_op == APNG_BLEND_OP_OVER &&
        avctx->pix_fmt != AV_PIX_FMT_RGBA &&
        avctx->pix_fmt != AV_PIX_FMT_GRAY8A &&
        avctx->pix_fmt != AV_PIX_FMT_PAL8) {
        avpriv_request_sample(avctx, "Blending with pixel format %s",
                              av_get_pix_fmt_name(avctx->pix_fmt));
        return AVERROR_PATCHWELCOME;
    }

    // Copy the previous frame to the buffer
    ff_thread_await_progress(&s->last_picture, INT_MAX, 0);
    memcpy(buffer, s->last_picture.f->data[0], s->image_linesize * s->height);

    // Do the disposal operation specified by the last frame on the frame
    if (s->last_dispose_op == APNG_DISPOSE_OP_BACKGROUND) {
        for (y = s->last_y_offset; y < s->last_y_offset + s->last_h; ++y)
            memset(buffer + s->image_linesize * y + s->bpp * s->last_x_offset, 0, s->bpp * s->last_w);
    } else if (s->last_dispose_op == APNG_DISPOSE_OP_PREVIOUS) {
        ff_thread_await_progress(&s->previous_picture, INT_MAX, 0);
        for (y = s->last_y_offset; y < s->last_y_offset + s->last_h; ++y) {
            size_t row_start = s->image_linesize * y + s->bpp * s->last_x_offset;
            memcpy(buffer + row_start, s->previous_picture.f->data[0] + row_start, s->bpp * s->last_w);
        }
    }

    // Perform blending
    if (s->blend_op == APNG_BLEND_OP_SOURCE) {
        for (y = s->y_offset; y < s->y_offset + s->cur_h; ++y) {
            size_t row_start = s->image_linesize * y + s->bpp * s->x_offset;
            memcpy(buffer + row_start, p->data[0] + row_start, s->bpp * s->cur_w);
        }
    } else { // APNG_BLEND_OP_OVER
        for (y = s->y_offset; y < s->y_offset + s->cur_h; ++y) {
            uint8_t *foreground = p->data[0] + s->image_linesize * y + s->bpp * s->x_offset;
            uint8_t *background = buffer + s->image_linesize * y + s->bpp * s->x_offset;
            for (x = s->x_offset; x < s->x_offset + s->cur_w; ++x, foreground += s->bpp, background += s->bpp) {
                size_t b;
                uint8_t foreground_alpha, background_alpha, output_alpha;
                uint8_t output[4];

                // Since we might be blending alpha onto alpha, we use the following equations:
                // output_alpha = foreground_alpha + (1 - foreground_alpha) * background_alpha
                // output = (foreground_alpha * foreground + (1 - foreground_alpha) * background_alpha * background) / output_alpha

                switch (avctx->pix_fmt) {
                case AV_PIX_FMT_RGBA:
                    foreground_alpha = foreground[3];
                    background_alpha = background[3];
                    break;

                case AV_PIX_FMT_GRAY8A:
                    foreground_alpha = foreground[1];
                    background_alpha = background[1];
                    break;

                case AV_PIX_FMT_PAL8:
                    foreground_alpha = s->palette[foreground[0]] >> 24;
                    background_alpha = s->palette[background[0]] >> 24;
                    break;
                }

                if (foreground_alpha == 0)
                    continue;

                if (foreground_alpha == 255) {
                    memcpy(background, foreground, s->bpp);
                    continue;
                }

                if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
                    // TODO: Alpha blending with PAL8 will likely need the entire image converted over to RGBA first
                    avpriv_request_sample(avctx, "Alpha blending palette samples");
                    background[0] = foreground[0];
                    continue;
                }

                output_alpha = foreground_alpha + FAST_DIV255((255 - foreground_alpha) * background_alpha);

                for (b = 0; b < s->bpp - 1; ++b) {
                    if (output_alpha == 0) {
                        output[b] = 0;
                    } else if (background_alpha == 255) {
                        output[b] = FAST_DIV255(foreground_alpha * foreground[b] + (255 - foreground_alpha) * background[b]);
                    } else {
                        output[b] = (255 * foreground_alpha * foreground[b] + (255 - foreground_alpha) * background_alpha * background[b]) / (255 * output_alpha);
                    }
                }
                output[b] = output_alpha;
                memcpy(background, output, s->bpp);
            }
        }
    }

    // Copy blended buffer into the frame and free
    memcpy(p->data[0], buffer, s->image_linesize * s->height);
    av_free(buffer);

    return 0;
}

static int decode_frame_common(AVCodecContext *avctx, PNGDecContext *s,
                               AVFrame *p, AVPacket *avpkt)
{
    AVDictionary *metadata  = NULL;
    uint32_t tag, length;
    int decode_next_dat = 0;
    int ret;

    for (;;) {
        length = bytestream2_get_bytes_left(&s->gb);
        if (length <= 0) {
            if (CONFIG_APNG_DECODER && avctx->codec_id == AV_CODEC_ID_APNG && length == 0) {
                if (!(s->state & PNG_IDAT))
                    return 0;
                else
                    goto exit_loop;
            }
            av_log(avctx, AV_LOG_ERROR, "%d bytes left\n", length);
            if (   s->state & PNG_ALLIMAGE
                && avctx->strict_std_compliance <= FF_COMPLIANCE_NORMAL)
                goto exit_loop;
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        length = bytestream2_get_be32(&s->gb);
        if (length > 0x7fffffff || length > bytestream2_get_bytes_left(&s->gb)) {
            av_log(avctx, AV_LOG_ERROR, "chunk too big\n");
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        tag = bytestream2_get_le32(&s->gb);
        if (avctx->debug & FF_DEBUG_STARTCODE)
            av_log(avctx, AV_LOG_DEBUG, "png: tag=%c%c%c%c length=%u\n",
                (tag & 0xff),
                ((tag >> 8) & 0xff),
                ((tag >> 16) & 0xff),
                ((tag >> 24) & 0xff), length);
        switch (tag) {
        case MKTAG('I', 'H', 'D', 'R'):
            if ((ret = decode_ihdr_chunk(avctx, s, length)) < 0)
                goto fail;
            break;
        case MKTAG('p', 'H', 'Y', 's'):
            if ((ret = decode_phys_chunk(avctx, s)) < 0)
                goto fail;
            break;
        case MKTAG('f', 'c', 'T', 'L'):
            if (!CONFIG_APNG_DECODER || avctx->codec_id != AV_CODEC_ID_APNG)
                goto skip_tag;
            if ((ret = decode_fctl_chunk(avctx, s, length)) < 0)
                goto fail;
            decode_next_dat = 1;
            break;
        case MKTAG('f', 'd', 'A', 'T'):
            if (!CONFIG_APNG_DECODER || avctx->codec_id != AV_CODEC_ID_APNG)
                goto skip_tag;
            if (!decode_next_dat) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            bytestream2_get_be32(&s->gb);
            length -= 4;
            /* fallthrough */
        case MKTAG('I', 'D', 'A', 'T'):
            if (CONFIG_APNG_DECODER && avctx->codec_id == AV_CODEC_ID_APNG && !decode_next_dat)
                goto skip_tag;
            if ((ret = decode_idat_chunk(avctx, s, length, p)) < 0)
                goto fail;
            break;
        case MKTAG('P', 'L', 'T', 'E'):
            if (decode_plte_chunk(avctx, s, length) < 0)
                goto skip_tag;
            break;
        case MKTAG('t', 'R', 'N', 'S'):
            if (decode_trns_chunk(avctx, s, length) < 0)
                goto skip_tag;
            break;
        case MKTAG('t', 'E', 'X', 't'):
            if (decode_text_chunk(s, length, 0, &metadata) < 0)
                av_log(avctx, AV_LOG_WARNING, "Broken tEXt chunk\n");
            bytestream2_skip(&s->gb, length + 4);
            break;
        case MKTAG('z', 'T', 'X', 't'):
            if (decode_text_chunk(s, length, 1, &metadata) < 0)
                av_log(avctx, AV_LOG_WARNING, "Broken zTXt chunk\n");
            bytestream2_skip(&s->gb, length + 4);
            break;
        case MKTAG('I', 'E', 'N', 'D'):
            if (!(s->state & PNG_ALLIMAGE))
                av_log(avctx, AV_LOG_ERROR, "IEND without all image\n");
            if (!(s->state & (PNG_ALLIMAGE|PNG_IDAT))) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            bytestream2_skip(&s->gb, 4); /* crc */
            goto exit_loop;
        default:
            /* skip tag */
skip_tag:
            bytestream2_skip(&s->gb, length + 4);
            break;
        }
    }
exit_loop:

    if (s->bits_per_pixel <= 4)
        handle_small_bpp(s, p);

    /* handle p-frames only if a predecessor frame is available */
    if (s->last_picture.f->data[0]) {
        if (   !(avpkt->flags & AV_PKT_FLAG_KEY) && avctx->codec_tag != AV_RL32("MPNG")
            && s->last_picture.f->width == p->width
            && s->last_picture.f->height== p->height
            && s->last_picture.f->format== p->format
         ) {
            if (CONFIG_PNG_DECODER && avctx->codec_id != AV_CODEC_ID_APNG)
                handle_p_frame_png(s, p);
            else if (CONFIG_APNG_DECODER &&
                     avctx->codec_id == AV_CODEC_ID_APNG &&
                     (ret = handle_p_frame_apng(avctx, s, p)) < 0)
                goto fail;
        }
    }
    ff_thread_report_progress(&s->picture, INT_MAX, 0);

    av_frame_set_metadata(p, metadata);
    metadata   = NULL;
    return 0;

fail:
    av_dict_free(&metadata);
    ff_thread_report_progress(&s->picture, INT_MAX, 0);
    return ret;
}

#if CONFIG_PNG_DECODER
static int decode_frame_png(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    PNGDecContext *const s = avctx->priv_data;
    const uint8_t *buf     = avpkt->data;
    int buf_size           = avpkt->size;
    AVFrame *p;
    int64_t sig;
    int ret;

    ff_thread_release_buffer(avctx, &s->last_picture);
    FFSWAP(ThreadFrame, s->picture, s->last_picture);
    p = s->picture.f;

    bytestream2_init(&s->gb, buf, buf_size);

    /* check signature */
    sig = bytestream2_get_be64(&s->gb);
    if (sig != PNGSIG &&
        sig != MNGSIG) {
        av_log(avctx, AV_LOG_ERROR, "Invalid PNG signature (%d).\n", buf_size);
        return AVERROR_INVALIDDATA;
    }

    s->y = s->state = 0;

    /* init the zlib */
    s->zstream.zalloc = ff_png_zalloc;
    s->zstream.zfree  = ff_png_zfree;
    s->zstream.opaque = NULL;
    ret = inflateInit(&s->zstream);
    if (ret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "inflateInit returned error %d\n", ret);
        return AVERROR_EXTERNAL;
    }

    if ((ret = decode_frame_common(avctx, s, p, avpkt)) < 0)
        goto the_end;

    if ((ret = av_frame_ref(data, s->picture.f)) < 0)
        return ret;

    *got_frame = 1;

    ret = bytestream2_tell(&s->gb);
the_end:
    inflateEnd(&s->zstream);
    s->crow_buf = NULL;
    return ret;
}
#endif

#if CONFIG_APNG_DECODER
static int decode_frame_apng(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    PNGDecContext *const s = avctx->priv_data;
    int ret;
    AVFrame *p;
    ThreadFrame tmp;

    ff_thread_release_buffer(avctx, &s->previous_picture);
    tmp = s->previous_picture;
    s->previous_picture = s->last_picture;
    s->last_picture = s->picture;
    s->picture = tmp;
    p = s->picture.f;

    if (!(s->state & PNG_IHDR)) {
        if (!avctx->extradata_size)
            return AVERROR_INVALIDDATA;

        /* only init fields, there is no zlib use in extradata */
        s->zstream.zalloc = ff_png_zalloc;
        s->zstream.zfree  = ff_png_zfree;

        bytestream2_init(&s->gb, avctx->extradata, avctx->extradata_size);
        if ((ret = decode_frame_common(avctx, s, p, avpkt)) < 0)
            goto end;
    }

    /* reset state for a new frame */
    if ((ret = inflateInit(&s->zstream)) != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "inflateInit returned error %d\n", ret);
        ret = AVERROR_EXTERNAL;
        goto end;
    }
    s->y = 0;
    s->state &= ~(PNG_IDAT | PNG_ALLIMAGE);
    bytestream2_init(&s->gb, avpkt->data, avpkt->size);
    if ((ret = decode_frame_common(avctx, s, p, avpkt)) < 0)
        goto end;

    if (!(s->state & PNG_ALLIMAGE))
        av_log(avctx, AV_LOG_WARNING, "Frame did not contain a complete image\n");
    if (!(s->state & (PNG_ALLIMAGE|PNG_IDAT))) {
        ret = AVERROR_INVALIDDATA;
        goto end;
    }
    if ((ret = av_frame_ref(data, s->picture.f)) < 0)
        goto end;

    *got_frame = 1;
    ret = bytestream2_tell(&s->gb);

end:
    inflateEnd(&s->zstream);
    return ret;
}
#endif

static int update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    PNGDecContext *psrc = src->priv_data;
    PNGDecContext *pdst = dst->priv_data;
    int ret;

    if (dst == src)
        return 0;

    ff_thread_release_buffer(dst, &pdst->picture);
    if (psrc->picture.f->data[0] &&
        (ret = ff_thread_ref_frame(&pdst->picture, &psrc->picture)) < 0)
        return ret;
    if (CONFIG_APNG_DECODER && dst->codec_id == AV_CODEC_ID_APNG) {
        pdst->cur_w = psrc->cur_w;
        pdst->cur_h = psrc->cur_h;
        pdst->x_offset = psrc->x_offset;
        pdst->y_offset = psrc->y_offset;
        pdst->dispose_op = psrc->dispose_op;

        ff_thread_release_buffer(dst, &pdst->last_picture);
        if (psrc->last_picture.f->data[0])
            return ff_thread_ref_frame(&pdst->last_picture, &psrc->last_picture);
    }

    return 0;
}

static av_cold int png_dec_init(AVCodecContext *avctx)
{
    PNGDecContext *s = avctx->priv_data;

    avctx->color_range = AVCOL_RANGE_JPEG;

    s->avctx = avctx;
    s->previous_picture.f = av_frame_alloc();
    s->last_picture.f = av_frame_alloc();
    s->picture.f = av_frame_alloc();
    if (!s->previous_picture.f || !s->last_picture.f || !s->picture.f) {
        av_frame_free(&s->previous_picture.f);
        av_frame_free(&s->last_picture.f);
        av_frame_free(&s->picture.f);
        return AVERROR(ENOMEM);
    }

    if (!avctx->internal->is_copy) {
        avctx->internal->allocate_progress = 1;
        ff_pngdsp_init(&s->dsp);
    }

    return 0;
}

static av_cold int png_dec_end(AVCodecContext *avctx)
{
    PNGDecContext *s = avctx->priv_data;

    ff_thread_release_buffer(avctx, &s->previous_picture);
    av_frame_free(&s->previous_picture.f);
    ff_thread_release_buffer(avctx, &s->last_picture);
    av_frame_free(&s->last_picture.f);
    ff_thread_release_buffer(avctx, &s->picture);
    av_frame_free(&s->picture.f);
    av_freep(&s->buffer);
    s->buffer_size = 0;
    av_freep(&s->last_row);
    s->last_row_size = 0;
    av_freep(&s->tmp_row);
    s->tmp_row_size = 0;

    return 0;
}

#if CONFIG_APNG_DECODER
AVCodec ff_apng_decoder = {
    .name           = "apng",
    .long_name      = NULL_IF_CONFIG_SMALL("APNG (Animated Portable Network Graphics) image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_APNG,
    .priv_data_size = sizeof(PNGDecContext),
    .init           = png_dec_init,
    .close          = png_dec_end,
    .decode         = decode_frame_apng,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(png_dec_init),
    .update_thread_context = ONLY_IF_THREADS_ENABLED(update_thread_context),
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_FRAME_THREADS /*| CODEC_CAP_DRAW_HORIZ_BAND*/,
};
#endif

#if CONFIG_PNG_DECODER
AVCodec ff_png_decoder = {
    .name           = "png",
    .long_name      = NULL_IF_CONFIG_SMALL("PNG (Portable Network Graphics) image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PNG,
    .priv_data_size = sizeof(PNGDecContext),
    .init           = png_dec_init,
    .close          = png_dec_end,
    .decode         = decode_frame_png,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(png_dec_init),
    .update_thread_context = ONLY_IF_THREADS_ENABLED(update_thread_context),
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_FRAME_THREADS /*| CODEC_CAP_DRAW_HORIZ_BAND*/,
};
#endif
