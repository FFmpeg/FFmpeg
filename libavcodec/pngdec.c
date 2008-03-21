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
#include "dsputil.h"

/* TODO:
 * - add 2, 4 and 16 bit depth support
 */

#include <zlib.h>

//#define DEBUG

typedef struct PNGDecContext {
    DSPContext dsp;

    const uint8_t *bytestream;
    const uint8_t *bytestream_start;
    const uint8_t *bytestream_end;
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
   is the original image width. We also do pixel format conversion at
   this stage */
static void png_put_interlaced_row(uint8_t *dst, int width,
                                   int bits_per_pixel, int pass,
                                   int color_type, const uint8_t *src)
{
    int x, mask, dsp_mask, j, src_x, b, bpp;
    uint8_t *d;
    const uint8_t *s;

    mask = ff_png_pass_mask[pass];
    dsp_mask = png_pass_dsp_mask[pass];
    switch(bits_per_pixel) {
    case 1:
        /* we must initialize the line to zero before writing to it */
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

void ff_add_png_paeth_prediction(uint8_t *dst, uint8_t *src, uint8_t *top, int w, int bpp)
{
    int i;
    for(i = 0; i < w; i++) {
        int a, b, c, p, pa, pb, pc;

        a = dst[i - bpp];
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
        dst[i] = p + src[i];
    }
}

#define UNROLL1(bpp, op) {\
                 r = dst[0];\
    if(bpp >= 2) g = dst[1];\
    if(bpp >= 3) b = dst[2];\
    if(bpp >= 4) a = dst[3];\
    for(; i < size; i+=bpp) {\
        dst[i+0] = r = op(r, src[i+0], last[i+0]);\
        if(bpp == 1) continue;\
        dst[i+1] = g = op(g, src[i+1], last[i+1]);\
        if(bpp == 2) continue;\
        dst[i+2] = b = op(b, src[i+2], last[i+2]);\
        if(bpp == 3) continue;\
        dst[i+3] = a = op(a, src[i+3], last[i+3]);\
    }\
}

#define UNROLL_FILTER(op)\
         if(bpp == 1) UNROLL1(1, op)\
    else if(bpp == 2) UNROLL1(2, op)\
    else if(bpp == 3) UNROLL1(3, op)\
    else if(bpp == 4) UNROLL1(4, op)\

/* NOTE: 'dst' can be equal to 'last' */
static void png_filter_row(DSPContext *dsp, uint8_t *dst, int filter_type,
                           uint8_t *src, uint8_t *last, int size, int bpp)
{
    int i, p, r, g, b, a;

    switch(filter_type) {
    case PNG_FILTER_VALUE_NONE:
        memcpy(dst, src, size);
        break;
    case PNG_FILTER_VALUE_SUB:
        for(i = 0; i < bpp; i++) {
            dst[i] = src[i];
        }
        if(bpp == 4) {
            p = *(int*)dst;
            for(; i < size; i+=bpp) {
                int s = *(int*)(src+i);
                p = ((s&0x7f7f7f7f) + (p&0x7f7f7f7f)) ^ ((s^p)&0x80808080);
                *(int*)(dst+i) = p;
            }
        } else {
#define OP_SUB(x,s,l) x+s
            UNROLL_FILTER(OP_SUB);
        }
        break;
    case PNG_FILTER_VALUE_UP:
        dsp->add_bytes_l2(dst, src, last, size);
        break;
    case PNG_FILTER_VALUE_AVG:
        for(i = 0; i < bpp; i++) {
            p = (last[i] >> 1);
            dst[i] = p + src[i];
        }
#define OP_AVG(x,s,l) (((x + l) >> 1) + s) & 0xff
        UNROLL_FILTER(OP_AVG);
        break;
    case PNG_FILTER_VALUE_PAETH:
        for(i = 0; i < bpp; i++) {
            p = last[i];
            dst[i] = p + src[i];
        }
        if(bpp > 1 && size > 4) {
            // would write off the end of the array if we let it process the last pixel with bpp=3
            int w = bpp==4 ? size : size-3;
            dsp->add_png_paeth_prediction(dst+i, src+i, last+i, w-i, bpp);
            i = w;
        }
        ff_add_png_paeth_prediction(dst+i, src+i, last+i, size-i, bpp);
        break;
    }
}

static av_always_inline void convert_to_rgb32_loco(uint8_t *dst, const uint8_t *src, int width, int loco)
{
    int j;
    unsigned int r, g, b, a;

    for(j = 0;j < width; j++) {
        r = src[0];
        g = src[1];
        b = src[2];
        a = src[3];
        if(loco) {
            r = (r+g)&0xff;
            b = (b+g)&0xff;
        }
        *(uint32_t *)dst = (a << 24) | (r << 16) | (g << 8) | b;
        dst += 4;
        src += 4;
    }
}

static void convert_to_rgb32(uint8_t *dst, const uint8_t *src, int width, int loco)
{
    if(loco)
        convert_to_rgb32_loco(dst, src, width, 1);
    else
        convert_to_rgb32_loco(dst, src, width, 0);
}

static void deloco_rgb24(uint8_t *dst, int size)
{
    int i;
    for(i=0; i<size; i+=3) {
        int g = dst[i+1];
        dst[i+0] += g;
        dst[i+2] += g;
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
            convert_to_rgb32(ptr, s->tmp_row, s->width, s->filter_type == PNG_FILTER_TYPE_LOCO);
            FFSWAP(uint8_t*, s->last_row, s->tmp_row);
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
        for(;;) {
            ptr = s->image_buf + s->image_linesize * s->y;
            if ((ff_png_pass_ymask[s->pass] << (s->y & 7)) & 0x80) {
                /* if we already read one row, it is time to stop to
                   wait for the next one */
                if (got_line)
                    break;
                png_filter_row(&s->dsp, s->tmp_row, s->crow_buf[0], s->crow_buf + 1,
                               s->last_row, s->pass_row_size, s->bpp);
                FFSWAP(uint8_t*, s->last_row, s->tmp_row);
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
    the_end: ;
    }
}

static int png_decode_idat(PNGDecContext *s, int length)
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
                        const uint8_t *buf, int buf_size)
{
    PNGDecContext * const s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    uint32_t tag, length;
    int ret, crc;

    s->bytestream_start=
    s->bytestream= buf;
    s->bytestream_end= buf + buf_size;

    /* check signature */
    if (memcmp(s->bytestream, ff_pngsig, 8) != 0 &&
        memcmp(s->bytestream, ff_mngsig, 8) != 0)
        return -1;
    s->bytestream+= 8;
    s->y=
    s->state=0;
//    memset(s, 0, sizeof(PNGDecContext));
    /* init the zlib */
    s->zstream.zalloc = ff_png_zalloc;
    s->zstream.zfree = ff_png_zfree;
    s->zstream.opaque = NULL;
    ret = inflateInit(&s->zstream);
    if (ret != Z_OK)
        return -1;
    for(;;) {
        int tag32;
        if (s->bytestream >= s->bytestream_end)
            goto fail;
        length = bytestream_get_be32(&s->bytestream);
        if (length > 0x7fffffff)
            goto fail;
        tag32 = bytestream_get_be32(&s->bytestream);
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
            s->width = bytestream_get_be32(&s->bytestream);
            s->height = bytestream_get_be32(&s->bytestream);
            if(avcodec_check_dimensions(avctx, s->width, s->height)){
                s->width= s->height= 0;
                goto fail;
            }
            s->bit_depth = *s->bytestream++;
            s->color_type = *s->bytestream++;
            s->compression_type = *s->bytestream++;
            s->filter_type = *s->bytestream++;
            s->interlace_type = *s->bytestream++;
            crc = bytestream_get_be32(&s->bytestream);
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

                s->channels = ff_png_get_nb_channels(s->color_type);
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
                    s->pass_row_size = ff_png_pass_row_size(s->pass,
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
            crc = bytestream_get_be32(&s->bytestream);
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
                crc = bytestream_get_be32(&s->bytestream);
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
                crc = bytestream_get_be32(&s->bytestream);
            }
            break;
        case MKTAG('I', 'E', 'N', 'D'):
            if (!(s->state & PNG_ALLIMAGE))
                goto fail;
            crc = bytestream_get_be32(&s->bytestream);
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

static av_cold int png_dec_init(AVCodecContext *avctx){
    PNGDecContext *s = avctx->priv_data;

    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame= (AVFrame*)&s->picture;
    dsputil_init(&s->dsp, avctx);

    return 0;
}

AVCodec png_decoder = {
    "png",
    CODEC_TYPE_VIDEO,
    CODEC_ID_PNG,
    sizeof(PNGDecContext),
    png_dec_init,
    NULL,
    NULL, //decode_end,
    decode_frame,
    0 /*CODEC_CAP_DR1*/ /*| CODEC_CAP_DRAW_HORIZ_BAND*/,
    NULL
};
