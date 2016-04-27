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

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/stereo3d.h"

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "png.h"
#include "pngdsp.h"

/* TODO:
 * - add 2, 4 and 16 bit depth support
 */

#include <zlib.h>

typedef struct PNGDecContext {
    PNGDSPContext dsp;

    GetByteContext gb;
    AVFrame *prev;

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
} PNGDecContext;

/* Mask to determine which y pixels can be written in a pass */
static const uint8_t png_pass_dsp_ymask[NB_PASSES] = {
    0xff, 0xff, 0x0f, 0xcc, 0x33, 0xff, 0x55,
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

    mask     = ff_png_pass_mask[pass];
    dsp_mask = png_pass_dsp_mask[pass];

    switch (bits_per_pixel) {
    case 1:
        /* we must initialize the line to zero before writing to it */
        if (pass == 0)
            memset(dst, 0, (width + 7) >> 3);
        src_x = 0;
        for (x = 0; x < width; x++) {
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
        d   = dst;
        s   = src;
        if (color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
            for (x = 0; x < width; x++) {
                j = x & 7;
                if ((dsp_mask << j) & 0x80) {
                    *(uint32_t *)d = (s[3] << 24) | (s[0] << 16) | (s[1] << 8) | s[2];
                }
                d += bpp;
                if ((mask << j) & 0x80)
                    s += bpp;
            }
        } else {
            for (x = 0; x < width; x++) {
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
        for (; i < size; i += bpp) {                                          \
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
    } else {                                                                  \
        for (; i < size; i += bpp) {                                          \
            int j;                                                            \
            for (j = 0; j < bpp; j++)                                         \
                dst[i + j] = op(dst[i + j - bpp], src[i + j], last[i + j]);   \
        }                                                                     \
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
                int s = *(int *)(src + i);
                p = ((s & 0x7f7f7f7f) + (p & 0x7f7f7f7f)) ^ ((s ^ p) & 0x80808080);
                *(int *)(dst + i) = p;
            }
        } else {
#define OP_SUB(x, s, l) x + s
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
#define OP_AVG(x, s, l) (((x + l) >> 1) + s) & 0xff
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
            int w = bpp == 4 ? size : size - 3;
            dsp->add_paeth_prediction(dst + i, src + i, last + i, w - i, bpp);
            i = w;
        }
        ff_add_png_paeth_prediction(dst + i, src + i, last + i, size - i, bpp);
        break;
    }
}

static av_always_inline void convert_to_rgb32_loco(uint8_t *dst,
                                                   const uint8_t *src,
                                                   int width, int loco)
{
    int j;
    unsigned int r, g, b, a;

    for (j = 0; j < width; j++) {
        r = src[0];
        g = src[1];
        b = src[2];
        a = src[3];
        if (loco) {
            r = (r + g) & 0xff;
            b = (b + g) & 0xff;
        }
        *(uint32_t *) dst = (a << 24) | (r << 16) | (g << 8) | b;
        dst += 4;
        src += 4;
    }
}

static void convert_to_rgb32(uint8_t *dst, const uint8_t *src,
                             int width, int loco)
{
    if (loco)
        convert_to_rgb32_loco(dst, src, width, 1);
    else
        convert_to_rgb32_loco(dst, src, width, 0);
}

static void deloco_rgb24(uint8_t *dst, int size)
{
    int i;
    for (i = 0; i < size; i += 3) {
        int g = dst[i + 1];
        dst[i + 0] += g;
        dst[i + 2] += g;
    }
}

/* process exactly one decompressed row */
static void png_handle_row(PNGDecContext *s)
{
    uint8_t *ptr, *last_row;
    int got_line;

    if (!s->interlace_type) {
        ptr = s->image_buf + s->image_linesize * s->y;
        /* need to swap bytes correctly for RGB_ALPHA */
        if (s->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
            png_filter_row(&s->dsp, s->tmp_row, s->crow_buf[0], s->crow_buf + 1,
                           s->last_row, s->row_size, s->bpp);
            convert_to_rgb32(ptr, s->tmp_row, s->width,
                             s->filter_type == PNG_FILTER_TYPE_LOCO);
            FFSWAP(uint8_t *, s->last_row, s->tmp_row);
        } else {
            /* in normal case, we avoid one copy */
            if (s->y == 0)
                last_row = s->last_row;
            else
                last_row = ptr - s->image_linesize;

            png_filter_row(&s->dsp, ptr, s->crow_buf[0], s->crow_buf + 1,
                           last_row, s->row_size, s->bpp);
        }
        /* loco lags by 1 row so that it doesn't interfere with top prediction */
        if (s->filter_type == PNG_FILTER_TYPE_LOCO &&
            s->color_type == PNG_COLOR_TYPE_RGB && s->y > 0)
            deloco_rgb24(ptr - s->image_linesize, s->row_size);
        s->y++;
        if (s->y == s->height) {
            s->state |= PNG_ALLIMAGE;
            if (s->filter_type == PNG_FILTER_TYPE_LOCO &&
                s->color_type == PNG_COLOR_TYPE_RGB)
                deloco_rgb24(ptr, s->row_size);
        }
    } else {
        got_line = 0;
        for (;;) {
            ptr = s->image_buf + s->image_linesize * s->y;
            if ((ff_png_pass_ymask[s->pass] << (s->y & 7)) & 0x80) {
                /* if we already read one row, it is time to stop to
                 * wait for the next one */
                if (got_line)
                    break;
                png_filter_row(&s->dsp, s->tmp_row, s->crow_buf[0], s->crow_buf + 1,
                               s->last_row, s->pass_row_size, s->bpp);
                FFSWAP(uint8_t *, s->last_row, s->tmp_row);
                got_line = 1;
            }
            if ((png_pass_dsp_ymask[s->pass] << (s->y & 7)) & 0x80) {
                /* NOTE: RGB32 is handled directly in png_put_interlaced_row */
                png_put_interlaced_row(ptr, s->width, s->bits_per_pixel, s->pass,
                                       s->color_type, s->last_row);
            }
            s->y++;
            if (s->y == s->height) {
                for (;;) {
                    if (s->pass == NB_PASSES - 1) {
                        s->state |= PNG_ALLIMAGE;
                        goto the_end;
                    } else {
                        s->pass++;
                        s->y = 0;
                        s->pass_row_size = ff_png_pass_row_size(s->pass,
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
the_end:;
    }
}

static int png_decode_idat(PNGDecContext *s, int length)
{
    int ret;
    s->zstream.avail_in = FFMIN(length, bytestream2_get_bytes_left(&s->gb));
    s->zstream.next_in  = s->gb.buffer;
    bytestream2_skip(&s->gb, length);

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

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    PNGDecContext *const s = avctx->priv_data;
    const uint8_t *buf     = avpkt->data;
    int buf_size           = avpkt->size;
    AVFrame *p             = data;
    uint8_t *crow_buf_base = NULL;
    uint32_t tag, length;
    int ret;

    /* check signature */
    if (buf_size < 8) {
        av_log(avctx, AV_LOG_ERROR, "Not enough data %d\n",
               buf_size);
        return AVERROR_INVALIDDATA;
    }
    if (memcmp(buf, ff_pngsig, 8) != 0 &&
        memcmp(buf, ff_mngsig, 8) != 0) {
        char signature[5 * 8 + 1] = { 0 };
        int i;
        for (i = 0; i < 8; i++) {
            av_strlcatf(signature + i * 5, sizeof(signature) - i * 5,
                        " 0x%02x", buf[i]);
        }
        av_log(avctx, AV_LOG_ERROR, "Invalid PNG signature %s\n",
               signature);
        return AVERROR_INVALIDDATA;
    }

    bytestream2_init(&s->gb, buf + 8, buf_size - 8);
    s->y = s->state = 0;

    /* init the zlib */
    s->zstream.zalloc = ff_png_zalloc;
    s->zstream.zfree  = ff_png_zfree;
    s->zstream.opaque = NULL;
    ret = inflateInit(&s->zstream);
    if (ret != Z_OK)
        return -1;
    for (;;) {
        if (bytestream2_get_bytes_left(&s->gb) <= 0)
            goto fail;
        length = bytestream2_get_be32(&s->gb);
        if (length > 0x7fffffff)
            goto fail;
        tag = bytestream2_get_le32(&s->gb);
        ff_dlog(avctx, "png: tag=%c%c%c%c length=%u\n",
                (tag & 0xff),
                ((tag >> 8) & 0xff),
                ((tag >> 16) & 0xff),
                ((tag >> 24) & 0xff), length);
        switch (tag) {
        case MKTAG('I', 'H', 'D', 'R'):
            if (length != 13)
                goto fail;
            s->width  = bytestream2_get_be32(&s->gb);
            s->height = bytestream2_get_be32(&s->gb);
            if (av_image_check_size(s->width, s->height, 0, avctx)) {
                s->width = s->height = 0;
                goto fail;
            }
            s->bit_depth        = bytestream2_get_byte(&s->gb);
            s->color_type       = bytestream2_get_byte(&s->gb);
            s->compression_type = bytestream2_get_byte(&s->gb);
            s->filter_type      = bytestream2_get_byte(&s->gb);
            s->interlace_type   = bytestream2_get_byte(&s->gb);
            bytestream2_skip(&s->gb, 4); /* crc */
            s->state |= PNG_IHDR;
            ff_dlog(avctx, "width=%d height=%d depth=%d color_type=%d "
                           "compression_type=%d filter_type=%d interlace_type=%d\n",
                    s->width, s->height, s->bit_depth, s->color_type,
                    s->compression_type, s->filter_type, s->interlace_type);
            break;
        case MKTAG('I', 'D', 'A', 'T'):
            if (!(s->state & PNG_IHDR))
                goto fail;
            if (!(s->state & PNG_IDAT)) {
                /* init image info */
                avctx->width  = s->width;
                avctx->height = s->height;

                s->channels       = ff_png_get_nb_channels(s->color_type);
                s->bits_per_pixel = s->bit_depth * s->channels;
                s->bpp            = (s->bits_per_pixel + 7) >> 3;
                s->row_size       = (avctx->width * s->bits_per_pixel + 7) >> 3;

                if (s->bit_depth == 8 &&
                    s->color_type == PNG_COLOR_TYPE_RGB) {
                    avctx->pix_fmt = AV_PIX_FMT_RGB24;
                } else if (s->bit_depth == 8 &&
                           s->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
                    avctx->pix_fmt = AV_PIX_FMT_RGB32;
                } else if (s->bit_depth == 8 &&
                           s->color_type == PNG_COLOR_TYPE_GRAY) {
                    avctx->pix_fmt = AV_PIX_FMT_GRAY8;
                } else if (s->bit_depth == 16 &&
                           s->color_type == PNG_COLOR_TYPE_GRAY) {
                    avctx->pix_fmt = AV_PIX_FMT_GRAY16BE;
                } else if (s->bit_depth == 16 &&
                           s->color_type == PNG_COLOR_TYPE_RGB) {
                    avctx->pix_fmt = AV_PIX_FMT_RGB48BE;
                } else if (s->bit_depth == 1 &&
                           s->color_type == PNG_COLOR_TYPE_GRAY) {
                    avctx->pix_fmt = AV_PIX_FMT_MONOBLACK;
                } else if (s->bit_depth == 8 &&
                           s->color_type == PNG_COLOR_TYPE_PALETTE) {
                    avctx->pix_fmt = AV_PIX_FMT_PAL8;
                } else if (s->bit_depth == 8 &&
                           s->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
                    avctx->pix_fmt = AV_PIX_FMT_YA8;
                } else if (s->bit_depth == 16 &&
                           s->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
                    avctx->pix_fmt = AV_PIX_FMT_YA16BE;
                } else {
                    goto fail;
                }

                if (ff_get_buffer(avctx, p, AV_GET_BUFFER_FLAG_REF) < 0) {
                    av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
                    goto fail;
                }
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
                                                            s->width);
                    s->crow_size = s->pass_row_size + 1;
                }
                ff_dlog(avctx, "row_size=%d crow_size =%d\n",
                        s->row_size, s->crow_size);
                s->image_buf      = p->data[0];
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
                crow_buf_base = av_malloc(s->row_size + 16);
                if (!crow_buf_base)
                    goto fail;

                /* we want crow_buf+1 to be 16-byte aligned */
                s->crow_buf          = crow_buf_base + 15;
                s->zstream.avail_out = s->crow_size;
                s->zstream.next_out  = s->crow_buf;
            }
            s->state |= PNG_IDAT;
            if (png_decode_idat(s, length) < 0)
                goto fail;
            bytestream2_skip(&s->gb, 4); /* crc */
            break;
        case MKTAG('P', 'L', 'T', 'E'):
        {
            int n, i, r, g, b;

            if ((length % 3) != 0 || length > 256 * 3)
                goto skip_tag;
            /* read the palette */
            n = length / 3;
            for (i = 0; i < n; i++) {
                r = bytestream2_get_byte(&s->gb);
                g = bytestream2_get_byte(&s->gb);
                b = bytestream2_get_byte(&s->gb);
                s->palette[i] = (0xff << 24) | (r << 16) | (g << 8) | b;
            }
            for (; i < 256; i++)
                s->palette[i] = (0xff << 24);
            s->state |= PNG_PLTE;
            bytestream2_skip(&s->gb, 4);     /* crc */
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
            for (i = 0; i < length; i++) {
                v = bytestream2_get_byte(&s->gb);
                s->palette[i] = (s->palette[i] & 0x00ffffff) | (v << 24);
            }
            bytestream2_skip(&s->gb, 4);     /* crc */
        }
        break;
        case MKTAG('s', 'T', 'E', 'R'): {
            int mode = bytestream2_get_byte(&s->gb);
            AVStereo3D *stereo3d = av_stereo3d_create_side_data(p);
            if (!stereo3d)
                goto the_end;

            if (mode == 0 || mode == 1) {
                stereo3d->type  = AV_STEREO3D_SIDEBYSIDE;
                stereo3d->flags = mode ? 0 : AV_STEREO3D_FLAG_INVERT;
            } else {
                 av_log(avctx, AV_LOG_WARNING,
                        "Unknown value in sTER chunk (%d)\n", mode);
            }
            bytestream2_skip(&s->gb, 4); /* crc */
            break;
        }
        case MKTAG('I', 'E', 'N', 'D'):
            if (!(s->state & PNG_ALLIMAGE))
                goto fail;
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
    /* handle P-frames only if a predecessor frame is available */
    if (s->prev->data[0]) {
        if (!(avpkt->flags & AV_PKT_FLAG_KEY)) {
            int i, j;
            uint8_t *pd      = p->data[0];
            uint8_t *pd_last = s->prev->data[0];

            for (j = 0; j < s->height; j++) {
                for (i = 0; i < s->width * s->bpp; i++)
                    pd[i] += pd_last[i];
                pd      += s->image_linesize;
                pd_last += s->image_linesize;
            }
        }
    }

    av_frame_unref(s->prev);
    if ((ret = av_frame_ref(s->prev, p)) < 0)
        goto fail;

    *got_frame = 1;

    ret = bytestream2_tell(&s->gb);
the_end:
    inflateEnd(&s->zstream);
    av_free(crow_buf_base);
    s->crow_buf = NULL;
    av_freep(&s->last_row);
    av_freep(&s->tmp_row);
    return ret;
fail:
    ret = -1;
    goto the_end;
}

static av_cold int png_dec_init(AVCodecContext *avctx)
{
    PNGDecContext *s = avctx->priv_data;

    avctx->color_range = AVCOL_RANGE_JPEG;

    s->prev = av_frame_alloc();
    if (!s->prev)
        return AVERROR(ENOMEM);

    ff_pngdsp_init(&s->dsp);

    return 0;
}

static av_cold int png_dec_end(AVCodecContext *avctx)
{
    PNGDecContext *s = avctx->priv_data;

    av_frame_free(&s->prev);

    return 0;
}

AVCodec ff_png_decoder = {
    .name           = "png",
    .long_name      = NULL_IF_CONFIG_SMALL("PNG (Portable Network Graphics) image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PNG,
    .priv_data_size = sizeof(PNGDecContext),
    .init           = png_dec_init,
    .close          = png_dec_end,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 /*| AV_CODEC_CAP_DRAW_HORIZ_BAND*/,
};
