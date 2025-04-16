/*
 * DVB subtitle encoding
 * Copyright (c) 2005 Fabrice Bellard
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
#include "codec_internal.h"
#include "libavutil/colorspace.h"

typedef struct DVBSubtitleContext {
    int object_version;
} DVBSubtitleContext;

#define PUTBITS2(val)\
{\
    bitbuf |= (val) << bitcnt;\
    bitcnt -= 2;\
    if (bitcnt < 0) {\
        bitcnt = 6;\
        *q++ = bitbuf;\
        bitbuf = 0;\
    }\
}

static int dvb_encode_rle2(uint8_t **pq, int buf_size,
                           const uint8_t *bitmap, int linesize,
                           int w, int h)
{
    uint8_t *q, *line_begin;
    unsigned int bitbuf;
    int bitcnt;
    int x, y, len, x1, v, color;

    q = *pq;

    for(y = 0; y < h; y++) {
        // Worst case line is 3 bits per value + 4 bytes overhead
        if (buf_size * 8 < w * 3 + 32)
            return AVERROR_BUFFER_TOO_SMALL;
        line_begin = q;
        *q++ = 0x10;
        bitbuf = 0;
        bitcnt = 6;

        x = 0;
        while (x < w) {
            x1 = x;
            color = bitmap[x1++];
            while (x1 < w && bitmap[x1] == color)
                x1++;
            len = x1 - x;
            if (color == 0 && len == 2) {
                PUTBITS2(0);
                PUTBITS2(0);
                PUTBITS2(1);
            } else if (len >= 3 && len <= 10) {
                v = len - 3;
                PUTBITS2(0);
                PUTBITS2((v >> 2) | 2);
                PUTBITS2(v & 3);
                PUTBITS2(color);
            } else if (len >= 12 && len <= 27) {
                v = len - 12;
                PUTBITS2(0);
                PUTBITS2(0);
                PUTBITS2(2);
                PUTBITS2(v >> 2);
                PUTBITS2(v & 3);
                PUTBITS2(color);
            } else if (len >= 29) {
                /* length = 29 ... 284 */
                if (len > 284)
                    len = 284;
                v = len - 29;
                PUTBITS2(0);
                PUTBITS2(0);
                PUTBITS2(3);
                PUTBITS2((v >> 6));
                PUTBITS2((v >> 4) & 3);
                PUTBITS2((v >> 2) & 3);
                PUTBITS2(v & 3);
                PUTBITS2(color);
            } else {
                PUTBITS2(color);
                if (color == 0) {
                    PUTBITS2(1);
                }
                len = 1;
            }
            x += len;
        }
        /* end of line */
        PUTBITS2(0);
        PUTBITS2(0);
        PUTBITS2(0);
        if (bitcnt != 6) {
            *q++ = bitbuf;
        }
        *q++ = 0xf0;
        bitmap += linesize;
        buf_size -= q - line_begin;
    }
    len = q - *pq;
    *pq = q;
    return len;
}

#define PUTBITS4(val)\
{\
    bitbuf |= (val) << bitcnt;\
    bitcnt -= 4;\
    if (bitcnt < 0) {\
        bitcnt = 4;\
        *q++ = bitbuf;\
        bitbuf = 0;\
    }\
}

/* some DVB decoders only implement 4 bits/pixel */
static int dvb_encode_rle4(uint8_t **pq, int buf_size,
                           const uint8_t *bitmap, int linesize,
                           int w, int h)
{
    uint8_t *q, *line_begin;
    unsigned int bitbuf;
    int bitcnt;
    int x, y, len, x1, v, color;

    q = *pq;

    for(y = 0; y < h; y++) {
        // Worst case line is 6 bits per value, + 4 bytes overhead
        if (buf_size * 8 < w * 6 + 32)
            return AVERROR_BUFFER_TOO_SMALL;
        line_begin = q;
        *q++ = 0x11;
        bitbuf = 0;
        bitcnt = 4;

        x = 0;
        while (x < w) {
            x1 = x;
            color = bitmap[x1++];
            while (x1 < w && bitmap[x1] == color)
                x1++;
            len = x1 - x;
            if (color == 0 && len == 2) {
                PUTBITS4(0);
                PUTBITS4(0xd);
            } else if (color == 0 && (len >= 3 && len <= 9)) {
                PUTBITS4(0);
                PUTBITS4(len - 2);
            } else if (len >= 4 && len <= 7) {
                PUTBITS4(0);
                PUTBITS4(8 + len - 4);
                PUTBITS4(color);
            } else if (len >= 9 && len <= 24) {
                PUTBITS4(0);
                PUTBITS4(0xe);
                PUTBITS4(len - 9);
                PUTBITS4(color);
            } else if (len >= 25) {
                if (len > 280)
                    len = 280;
                v = len - 25;
                PUTBITS4(0);
                PUTBITS4(0xf);
                PUTBITS4(v >> 4);
                PUTBITS4(v & 0xf);
                PUTBITS4(color);
            } else {
                PUTBITS4(color);
                if (color == 0) {
                    PUTBITS4(0xc);
                }
                len = 1;
            }
            x += len;
        }
        /* end of line */
        PUTBITS4(0);
        PUTBITS4(0);
        if (bitcnt != 4) {
            *q++ = bitbuf;
        }
        *q++ = 0xf0;
        bitmap += linesize;
        buf_size -= q - line_begin;
    }
    len = q - *pq;
    *pq = q;
    return len;
}

static int dvb_encode_rle8(uint8_t **pq, int buf_size,
                           const uint8_t *bitmap, int linesize,
                           int w, int h)
{
    uint8_t *q, *line_begin;
    int x, y, len, x1, color;

    q = *pq;

    for (y = 0; y < h; y++) {
        // Worst case line is 12 bits per value, + 3 bytes overhead
        if (buf_size * 8 < w * 12 + 24)
            return AVERROR_BUFFER_TOO_SMALL;
        line_begin = q;
        *q++ = 0x12;

        x = 0;
        while (x < w) {
            x1 = x;
            color = bitmap[x1++];
            while (x1 < w && bitmap[x1] == color)
                x1++;
            len = x1 - x;
            if (len == 1 && color) {
                // 00000001 to 11111111           1 pixel in colour x
                *q++ = color;
            } else {
                if (color == 0x00) {
                    // 00000000 0LLLLLLL          L pixels (1-127) in colour 0 (L > 0)
                    len = FFMIN(len, 127);
                    *q++ = 0x00;
                    *q++ = len;
                } else if (len > 2) {
                    // 00000000 1LLLLLLL CCCCCCCC L pixels (3-127) in colour C (L > 2)
                    len = FFMIN(len, 127);
                    *q++ = 0x00;
                    *q++ = 0x80+len;
                    *q++ = color;
                }
                else if (len == 2) {
                    *q++ = color;
                    *q++ = color;
                } else {
                    *q++ = color;
                    len = 1;
                }
            }
            x += len;
        }
        /* end of line */
        // 00000000 end of 8-bit/pixel_code_string
        *q++ = 0x00;
        *q++ = 0xf0;
        bitmap += linesize;
        buf_size -= q - line_begin;
    }
    len = q - *pq;
    *pq = q;
    return len;
}

static int dvbsub_encode(AVCodecContext *avctx, uint8_t *outbuf, int buf_size,
                         const AVSubtitle *h)
{
    DVBSubtitleContext *s = avctx->priv_data;
    uint8_t *q, *pseg_len;
    int page_id, region_id, clut_id, object_id, i, bpp_index, page_state;


    q = outbuf;

    page_id = 1;

    if (h->num_rects && !h->rects)
        return AVERROR(EINVAL);

    if (h->num_rects >= 256)
        return AVERROR(EINVAL);

    if (avctx->width > 0 && avctx->height > 0) {
        if (buf_size < 11)
            return AVERROR_BUFFER_TOO_SMALL;
        /* display definition segment */
        *q++ = 0x0f; /* sync_byte */
        *q++ = 0x14; /* segment_type */
        bytestream_put_be16(&q, page_id);
        pseg_len = q;
        q += 2; /* segment length */
        *q++ = 0x00; /* dds version number & display window flag */
        bytestream_put_be16(&q, avctx->width - 1); /* display width */
        bytestream_put_be16(&q, avctx->height - 1); /* display height */
        bytestream_put_be16(&pseg_len, q - pseg_len - 2);
        buf_size -= 11;
    }

    /* page composition segment */

    if (buf_size < 8 + h->num_rects * 6)
        return AVERROR_BUFFER_TOO_SMALL;
    *q++ = 0x0f; /* sync_byte */
    *q++ = 0x10; /* segment_type */
    bytestream_put_be16(&q, page_id);
    pseg_len = q;
    q += 2; /* segment length */
    *q++ = 30; /* page_timeout (seconds) */
    page_state = 2; /* mode change */
    /* page_version = 0 + page_state */
    *q++ = (s->object_version << 4) | (page_state << 2) | 3;

    for (region_id = 0; region_id < h->num_rects; region_id++) {
        *q++ = region_id;
        *q++ = 0xff; /* reserved */
        bytestream_put_be16(&q, h->rects[region_id]->x); /* left pos */
        bytestream_put_be16(&q, h->rects[region_id]->y); /* top pos */
    }

    bytestream_put_be16(&pseg_len, q - pseg_len - 2);
    buf_size -= 8 + h->num_rects * 6;

    if (h->num_rects) {
        for (clut_id = 0; clut_id < h->num_rects; clut_id++) {
            /* CLUT segment */

            if (h->rects[clut_id]->nb_colors <= 4U) {
                /* 2 bpp, some decoders do not support it correctly */
                bpp_index = 0;
            } else if (h->rects[clut_id]->nb_colors <= 16U) {
                /* 4 bpp, standard encoding */
                bpp_index = 1;
            } else if (h->rects[clut_id]->nb_colors <= 256U) {
                /* 8 bpp, standard encoding */
                bpp_index = 2;
            } else {
                return AVERROR(EINVAL);
            }

            if (buf_size < 6 + h->rects[clut_id]->nb_colors * 6)
                return AVERROR_BUFFER_TOO_SMALL;

            /* CLUT segment */
            *q++ = 0x0f; /* sync byte */
            *q++ = 0x12; /* CLUT definition segment */
            bytestream_put_be16(&q, page_id);
            pseg_len = q;
            q += 2; /* segment length */
            *q++ = clut_id;
            *q++ = (0 << 4) | 0xf; /* version = 0 */

            for(i = 0; i < h->rects[clut_id]->nb_colors; i++) {
                *q++ = i; /* clut_entry_id */
                *q++ = (1 << (7 - bpp_index)) | (0xf << 1) | 1; /* 2 bits/pixel full range */
                {
                    int a, r, g, b;
                    uint32_t x= ((uint32_t*)h->rects[clut_id]->data[1])[i];
                    a = (x >> 24) & 0xff;
                    r = (x >> 16) & 0xff;
                    g = (x >>  8) & 0xff;
                    b = (x >>  0) & 0xff;

                    *q++ = RGB_TO_Y_CCIR(r, g, b);
                    *q++ = RGB_TO_V_CCIR(r, g, b, 0);
                    *q++ = RGB_TO_U_CCIR(r, g, b, 0);
                    *q++ = 255 - a;
                }
            }

            bytestream_put_be16(&pseg_len, q - pseg_len - 2);
            buf_size -= 6 + h->rects[clut_id]->nb_colors * 6;
        }

        if (buf_size < h->num_rects * 22)
            return AVERROR_BUFFER_TOO_SMALL;
        for (region_id = 0; region_id < h->num_rects; region_id++) {

            /* region composition segment */

            if (h->rects[region_id]->nb_colors <= 4) {
                /* 2 bpp, some decoders do not support it correctly */
                bpp_index = 0;
            } else if (h->rects[region_id]->nb_colors <= 16) {
                /* 4 bpp, standard encoding */
                bpp_index = 1;
            } else if (h->rects[region_id]->nb_colors <= 256) {
                /* 8 bpp, standard encoding */
                bpp_index = 2;
            } else {
                return AVERROR(EINVAL);
            }

            *q++ = 0x0f; /* sync_byte */
            *q++ = 0x11; /* segment_type */
            bytestream_put_be16(&q, page_id);
            pseg_len = q;
            q += 2; /* segment length */
            *q++ = region_id;
            *q++ = (s->object_version << 4) | (0 << 3) | 0x07; /* version , no fill */
            bytestream_put_be16(&q, h->rects[region_id]->w); /* region width */
            bytestream_put_be16(&q, h->rects[region_id]->h); /* region height */
            *q++ = ((1 + bpp_index) << 5) | ((1 + bpp_index) << 2) | 0x03;
            *q++ = region_id; /* clut_id == region_id */
            *q++ = 0; /* 8 bit fill colors */
            *q++ = 0x03; /* 4 bit and 2 bit fill colors */

            bytestream_put_be16(&q, region_id); /* object_id == region_id */
            *q++ = (0 << 6) | (0 << 4);
            *q++ = 0;
            *q++ = 0xf0;
            *q++ = 0;

            bytestream_put_be16(&pseg_len, q - pseg_len - 2);
        }
        buf_size -= h->num_rects * 22;

        for (object_id = 0; object_id < h->num_rects; object_id++) {
            int (*dvb_encode_rle)(uint8_t **pq, int buf_size,
                                  const uint8_t *bitmap, int linesize,
                                  int w, int h);

            if (buf_size < 13)
                return AVERROR_BUFFER_TOO_SMALL;

            /* bpp_index maths */
            if (h->rects[object_id]->nb_colors <= 4) {
                /* 2 bpp, some decoders do not support it correctly */
                dvb_encode_rle = dvb_encode_rle2;
            } else if (h->rects[object_id]->nb_colors <= 16) {
                /* 4 bpp, standard encoding */
                dvb_encode_rle = dvb_encode_rle4;
            } else if (h->rects[object_id]->nb_colors <= 256) {
                /* 8 bpp, standard encoding */
                dvb_encode_rle = dvb_encode_rle8;
            } else {
                return AVERROR(EINVAL);
            }

            /* Object Data segment */
            *q++ = 0x0f; /* sync byte */
            *q++ = 0x13;
            bytestream_put_be16(&q, page_id);
            pseg_len = q;
            q += 2; /* segment length */

            bytestream_put_be16(&q, object_id);
            *q++ = (s->object_version << 4) | (0 << 2) | (0 << 1) | 1; /* version = 0,
                                                                       object_coding_method,
                                                                       non_modifying_color_flag */
            {
                uint8_t *ptop_field_len, *pbottom_field_len, *top_ptr, *bottom_ptr;
                int ret;

                ptop_field_len = q;
                q += 2;
                pbottom_field_len = q;
                q += 2;
                buf_size -= 13;

                top_ptr = q;
                ret = dvb_encode_rle(&q, buf_size,
                                     h->rects[object_id]->data[0],
                                     h->rects[object_id]->w * 2,
                                     h->rects[object_id]->w,
                                     h->rects[object_id]->h >> 1);
                if (ret < 0)
                    return ret;
                buf_size -= ret;
                bottom_ptr = q;
                ret = dvb_encode_rle(&q, buf_size,
                                     h->rects[object_id]->data[0] + h->rects[object_id]->w,
                                     h->rects[object_id]->w * 2,
                                     h->rects[object_id]->w,
                                     h->rects[object_id]->h >> 1);
                if (ret < 0)
                    return ret;
                buf_size -= ret;

                bytestream_put_be16(&ptop_field_len, bottom_ptr - top_ptr);
                bytestream_put_be16(&pbottom_field_len, q - bottom_ptr);
            }

            bytestream_put_be16(&pseg_len, q - pseg_len - 2);
        }
    }

    /* end of display set segment */

    if (buf_size < 6)
        return AVERROR_BUFFER_TOO_SMALL;
    *q++ = 0x0f; /* sync_byte */
    *q++ = 0x80; /* segment_type */
    bytestream_put_be16(&q, page_id);
    pseg_len = q;
    q += 2; /* segment length */

    bytestream_put_be16(&pseg_len, q - pseg_len - 2);
    buf_size -= 6;

    s->object_version = (s->object_version + 1) & 0xf;
    return q - outbuf;
}

const FFCodec ff_dvbsub_encoder = {
    .p.name         = "dvbsub",
    CODEC_LONG_NAME("DVB subtitles"),
    .p.type         = AVMEDIA_TYPE_SUBTITLE,
    .p.id           = AV_CODEC_ID_DVB_SUBTITLE,
    .priv_data_size = sizeof(DVBSubtitleContext),
    FF_CODEC_ENCODE_SUB_CB(dvbsub_encode),
};
