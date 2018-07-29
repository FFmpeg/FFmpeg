/*
 * Quicktime Animation (RLE) Video Decoder
 * Copyright (C) 2004 The FFmpeg project
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
 * QT RLE Video Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information about the QT RLE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * The QT RLE decoder has seven modes of operation:
 * 1, 2, 4, 8, 16, 24, and 32 bits per pixel. For modes 1, 2, 4, and 8
 * the decoder outputs PAL8 colorspace data. 16-bit data yields RGB555
 * data. 24-bit data is RGB24 and 32-bit data is RGB32.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

typedef struct QtrleContext {
    AVCodecContext *avctx;
    AVFrame *frame;

    GetByteContext g;
    uint32_t pal[256];
} QtrleContext;

#define CHECK_PIXEL_PTR(n)                                                            \
    if ((pixel_ptr + n > pixel_limit) || (pixel_ptr + n < 0)) {                       \
        av_log (s->avctx, AV_LOG_ERROR, "Problem: pixel_ptr = %d, pixel_limit = %d\n",\
                pixel_ptr + n, pixel_limit);                                          \
        return;                                                                       \
    }                                                                                 \

static void qtrle_decode_1bpp(QtrleContext *s, int row_ptr, int lines_to_change)
{
    int rle_code;
    int pixel_ptr;
    int row_inc = s->frame->linesize[0];
    uint8_t pi0, pi1;  /* 2 8-pixel values */
    uint8_t *rgb = s->frame->data[0];
    int pixel_limit = s->frame->linesize[0] * s->avctx->height;
    int skip;
    /* skip & 0x80 appears to mean 'start a new line', which can be interpreted
     * as 'go to next line' during the decoding of a frame but is 'go to first
     * line' at the beginning. Since we always interpret it as 'go to next line'
     * in the decoding loop (which makes code simpler/faster), the first line
     * would not be counted, so we count one more.
     * See: https://trac.ffmpeg.org/ticket/226
     * In the following decoding loop, row_ptr will be the position of the
     * current row. */

    row_ptr  -= row_inc;
    pixel_ptr = row_ptr;
    lines_to_change++;
    while (lines_to_change) {
        skip     =              bytestream2_get_byte(&s->g);
        rle_code = (int8_t)bytestream2_get_byte(&s->g);
        if (rle_code == 0)
            break;
        if(skip & 0x80) {
            lines_to_change--;
            row_ptr += row_inc;
            pixel_ptr = row_ptr + 2 * 8 * (skip & 0x7f);
        } else
            pixel_ptr += 2 * 8 * skip;
        CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */

        if(rle_code == -1)
            continue;

        if (rle_code < 0) {
            /* decode the run length code */
            rle_code = -rle_code;
            /* get the next 2 bytes from the stream, treat them as groups
             * of 8 pixels, and output them rle_code times */

            pi0 = bytestream2_get_byte(&s->g);
            pi1 = bytestream2_get_byte(&s->g);
            CHECK_PIXEL_PTR(rle_code * 2 * 8);

            while (rle_code--) {
                rgb[pixel_ptr++] = (pi0 >> 7) & 0x01;
                rgb[pixel_ptr++] = (pi0 >> 6) & 0x01;
                rgb[pixel_ptr++] = (pi0 >> 5) & 0x01;
                rgb[pixel_ptr++] = (pi0 >> 4) & 0x01;
                rgb[pixel_ptr++] = (pi0 >> 3) & 0x01;
                rgb[pixel_ptr++] = (pi0 >> 2) & 0x01;
                rgb[pixel_ptr++] = (pi0 >> 1) & 0x01;
                rgb[pixel_ptr++] =  pi0       & 0x01;
                rgb[pixel_ptr++] = (pi1 >> 7) & 0x01;
                rgb[pixel_ptr++] = (pi1 >> 6) & 0x01;
                rgb[pixel_ptr++] = (pi1 >> 5) & 0x01;
                rgb[pixel_ptr++] = (pi1 >> 4) & 0x01;
                rgb[pixel_ptr++] = (pi1 >> 3) & 0x01;
                rgb[pixel_ptr++] = (pi1 >> 2) & 0x01;
                rgb[pixel_ptr++] = (pi1 >> 1) & 0x01;
                rgb[pixel_ptr++] =  pi1       & 0x01;
            }
        } else {
            /* copy the same pixel directly to output 2 times */
            rle_code *= 2;
            CHECK_PIXEL_PTR(rle_code * 8);

            while (rle_code--) {
                int x = bytestream2_get_byte(&s->g);
                rgb[pixel_ptr++] = (x >> 7) & 0x01;
                rgb[pixel_ptr++] = (x >> 6) & 0x01;
                rgb[pixel_ptr++] = (x >> 5) & 0x01;
                rgb[pixel_ptr++] = (x >> 4) & 0x01;
                rgb[pixel_ptr++] = (x >> 3) & 0x01;
                rgb[pixel_ptr++] = (x >> 2) & 0x01;
                rgb[pixel_ptr++] = (x >> 1) & 0x01;
                rgb[pixel_ptr++] =  x       & 0x01;
            }
        }
    }
}

static inline void qtrle_decode_2n4bpp(QtrleContext *s, int row_ptr,
                                       int lines_to_change, int bpp)
{
    int rle_code, i;
    int pixel_ptr;
    int row_inc = s->frame->linesize[0];
    uint8_t pi[16];  /* 16 palette indices */
    uint8_t *rgb = s->frame->data[0];
    int pixel_limit = s->frame->linesize[0] * s->avctx->height;
    int num_pixels = (bpp == 4) ? 8 : 16;

    while (lines_to_change--) {
        pixel_ptr = row_ptr + (num_pixels * (bytestream2_get_byte(&s->g) - 1));
        CHECK_PIXEL_PTR(0);

        while ((rle_code = (int8_t)bytestream2_get_byte(&s->g)) != -1) {
            if (bytestream2_get_bytes_left(&s->g) < 1)
                return;
            if (rle_code == 0) {
                /* there's another skip code in the stream */
                pixel_ptr += (num_pixels * (bytestream2_get_byte(&s->g) - 1));
                CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */
            } else if (rle_code < 0) {
                /* decode the run length code */
                rle_code = -rle_code;
                /* get the next 4 bytes from the stream, treat them as palette
                 * indexes, and output them rle_code times */
                for (i = num_pixels-1; i >= 0; i--) {
                    pi[num_pixels-1-i] = (bytestream2_peek_byte(&s->g) >> ((i*bpp) & 0x07)) & ((1<<bpp)-1);
                    bytestream2_skip(&s->g, ((i & ((num_pixels>>2)-1)) == 0));
                }
                CHECK_PIXEL_PTR(rle_code * num_pixels);
                while (rle_code--) {
                    memcpy(&rgb[pixel_ptr], &pi, num_pixels);
                    pixel_ptr += num_pixels;
                }
            } else {
                /* copy the same pixel directly to output 4 times */
                rle_code *= 4;
                CHECK_PIXEL_PTR(rle_code*(num_pixels>>2));
                while (rle_code--) {
                    if(bpp == 4) {
                        int x = bytestream2_get_byte(&s->g);
                        rgb[pixel_ptr++] = (x >> 4) & 0x0f;
                        rgb[pixel_ptr++] =  x       & 0x0f;
                    } else {
                        int x = bytestream2_get_byte(&s->g);
                        rgb[pixel_ptr++] = (x >> 6) & 0x03;
                        rgb[pixel_ptr++] = (x >> 4) & 0x03;
                        rgb[pixel_ptr++] = (x >> 2) & 0x03;
                        rgb[pixel_ptr++] =  x       & 0x03;
                    }
                }
            }
        }
        row_ptr += row_inc;
    }
}

static void qtrle_decode_8bpp(QtrleContext *s, int row_ptr, int lines_to_change)
{
    int rle_code;
    int pixel_ptr;
    int row_inc = s->frame->linesize[0];
    uint8_t pi1, pi2, pi3, pi4;  /* 4 palette indexes */
    uint8_t *rgb = s->frame->data[0];
    int pixel_limit = s->frame->linesize[0] * s->avctx->height;

    while (lines_to_change--) {
        pixel_ptr = row_ptr + (4 * (bytestream2_get_byte(&s->g) - 1));
        CHECK_PIXEL_PTR(0);

        while ((rle_code = (int8_t)bytestream2_get_byte(&s->g)) != -1) {
            if (bytestream2_get_bytes_left(&s->g) < 1)
                return;
            if (rle_code == 0) {
                /* there's another skip code in the stream */
                pixel_ptr += (4 * (bytestream2_get_byte(&s->g) - 1));
                CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */
            } else if (rle_code < 0) {
                /* decode the run length code */
                rle_code = -rle_code;
                /* get the next 4 bytes from the stream, treat them as palette
                 * indexes, and output them rle_code times */
                pi1 = bytestream2_get_byte(&s->g);
                pi2 = bytestream2_get_byte(&s->g);
                pi3 = bytestream2_get_byte(&s->g);
                pi4 = bytestream2_get_byte(&s->g);

                CHECK_PIXEL_PTR(rle_code * 4);

                while (rle_code--) {
                    rgb[pixel_ptr++] = pi1;
                    rgb[pixel_ptr++] = pi2;
                    rgb[pixel_ptr++] = pi3;
                    rgb[pixel_ptr++] = pi4;
                }
            } else {
                /* copy the same pixel directly to output 4 times */
                rle_code *= 4;
                CHECK_PIXEL_PTR(rle_code);

                bytestream2_get_buffer(&s->g, &rgb[pixel_ptr], rle_code);
                pixel_ptr += rle_code;
            }
        }
        row_ptr += row_inc;
    }
}

static void qtrle_decode_16bpp(QtrleContext *s, int row_ptr, int lines_to_change)
{
    int rle_code;
    int pixel_ptr;
    int row_inc = s->frame->linesize[0];
    uint16_t rgb16;
    uint8_t *rgb = s->frame->data[0];
    int pixel_limit = s->frame->linesize[0] * s->avctx->height;

    while (lines_to_change--) {
        pixel_ptr = row_ptr + (bytestream2_get_byte(&s->g) - 1) * 2;
        CHECK_PIXEL_PTR(0);

        while ((rle_code = (int8_t)bytestream2_get_byte(&s->g)) != -1) {
            if (bytestream2_get_bytes_left(&s->g) < 1)
                return;
            if (rle_code == 0) {
                /* there's another skip code in the stream */
                pixel_ptr += (bytestream2_get_byte(&s->g) - 1) * 2;
                CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */
            } else if (rle_code < 0) {
                /* decode the run length code */
                rle_code = -rle_code;
                rgb16 = bytestream2_get_be16(&s->g);

                CHECK_PIXEL_PTR(rle_code * 2);

                while (rle_code--) {
                    *(uint16_t *)(&rgb[pixel_ptr]) = rgb16;
                    pixel_ptr += 2;
                }
            } else {
                CHECK_PIXEL_PTR(rle_code * 2);

                /* copy pixels directly to output */
                while (rle_code--) {
                    rgb16 = bytestream2_get_be16(&s->g);
                    *(uint16_t *)(&rgb[pixel_ptr]) = rgb16;
                    pixel_ptr += 2;
                }
            }
        }
        row_ptr += row_inc;
    }
}

static void qtrle_decode_24bpp(QtrleContext *s, int row_ptr, int lines_to_change)
{
    int rle_code;
    int pixel_ptr;
    int row_inc = s->frame->linesize[0];
    uint8_t r, g, b;
    uint8_t *rgb = s->frame->data[0];
    int pixel_limit = s->frame->linesize[0] * s->avctx->height;

    while (lines_to_change--) {
        pixel_ptr = row_ptr + (bytestream2_get_byte(&s->g) - 1) * 3;
        CHECK_PIXEL_PTR(0);

        while ((rle_code = (int8_t)bytestream2_get_byte(&s->g)) != -1) {
            if (bytestream2_get_bytes_left(&s->g) < 1)
                return;
            if (rle_code == 0) {
                /* there's another skip code in the stream */
                pixel_ptr += (bytestream2_get_byte(&s->g) - 1) * 3;
                CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */
            } else if (rle_code < 0) {
                /* decode the run length code */
                rle_code = -rle_code;
                r = bytestream2_get_byte(&s->g);
                g = bytestream2_get_byte(&s->g);
                b = bytestream2_get_byte(&s->g);

                CHECK_PIXEL_PTR(rle_code * 3);

                while (rle_code--) {
                    rgb[pixel_ptr++] = r;
                    rgb[pixel_ptr++] = g;
                    rgb[pixel_ptr++] = b;
                }
            } else {
                CHECK_PIXEL_PTR(rle_code * 3);

                /* copy pixels directly to output */
                while (rle_code--) {
                    rgb[pixel_ptr++] = bytestream2_get_byte(&s->g);
                    rgb[pixel_ptr++] = bytestream2_get_byte(&s->g);
                    rgb[pixel_ptr++] = bytestream2_get_byte(&s->g);
                }
            }
        }
        row_ptr += row_inc;
    }
}

static void qtrle_decode_32bpp(QtrleContext *s, int row_ptr, int lines_to_change)
{
    int rle_code;
    int pixel_ptr;
    int row_inc = s->frame->linesize[0];
    unsigned int argb;
    uint8_t *rgb = s->frame->data[0];
    int pixel_limit = s->frame->linesize[0] * s->avctx->height;

    while (lines_to_change--) {
        pixel_ptr = row_ptr + (bytestream2_get_byte(&s->g) - 1) * 4;
        CHECK_PIXEL_PTR(0);

        while ((rle_code = (int8_t)bytestream2_get_byte(&s->g)) != -1) {
            if (bytestream2_get_bytes_left(&s->g) < 1)
                return;
            if (rle_code == 0) {
                /* there's another skip code in the stream */
                pixel_ptr += (bytestream2_get_byte(&s->g) - 1) * 4;
                CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */
            } else if (rle_code < 0) {
                /* decode the run length code */
                rle_code = -rle_code;
                argb = bytestream2_get_be32(&s->g);

                CHECK_PIXEL_PTR(rle_code * 4);

                while (rle_code--) {
                    AV_WN32A(rgb + pixel_ptr, argb);
                    pixel_ptr += 4;
                }
            } else {
                CHECK_PIXEL_PTR(rle_code * 4);

                /* copy pixels directly to output */
                while (rle_code--) {
                    argb = bytestream2_get_be32(&s->g);
                    AV_WN32A(rgb + pixel_ptr, argb);
                    pixel_ptr  += 4;
                }
            }
        }
        row_ptr += row_inc;
    }
}

static av_cold int qtrle_decode_init(AVCodecContext *avctx)
{
    QtrleContext *s = avctx->priv_data;

    s->avctx = avctx;
    switch (avctx->bits_per_coded_sample) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 33:
    case 34:
    case 36:
    case 40:
        avctx->pix_fmt = AV_PIX_FMT_PAL8;
        break;

    case 16:
        avctx->pix_fmt = AV_PIX_FMT_RGB555;
        break;

    case 24:
        avctx->pix_fmt = AV_PIX_FMT_RGB24;
        break;

    case 32:
        avctx->pix_fmt = AV_PIX_FMT_RGB32;
        break;

    default:
        av_log (avctx, AV_LOG_ERROR, "Unsupported colorspace: %d bits/sample?\n",
            avctx->bits_per_coded_sample);
        return AVERROR_INVALIDDATA;
    }

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int qtrle_decode_frame(AVCodecContext *avctx,
                              void *data, int *got_frame,
                              AVPacket *avpkt)
{
    QtrleContext *s = avctx->priv_data;
    int header, start_line;
    int height, row_ptr;
    int has_palette = 0;
    int ret;

    bytestream2_init(&s->g, avpkt->data, avpkt->size);
    if ((ret = ff_reget_buffer(avctx, s->frame)) < 0)
        return ret;

    /* check if this frame is even supposed to change */
    if (avpkt->size < 8)
        goto done;

    /* start after the chunk size */
    bytestream2_seek(&s->g, 4, SEEK_SET);

    /* fetch the header */
    header = bytestream2_get_be16(&s->g);

    /* if a header is present, fetch additional decoding parameters */
    if (header & 0x0008) {
        if (avpkt->size < 14)
            goto done;
        start_line = bytestream2_get_be16(&s->g);
        bytestream2_skip(&s->g, 2);
        height     = bytestream2_get_be16(&s->g);
        bytestream2_skip(&s->g, 2);
        if (height > s->avctx->height - start_line)
            goto done;
    } else {
        start_line = 0;
        height     = s->avctx->height;
    }
    row_ptr = s->frame->linesize[0] * start_line;

    switch (avctx->bits_per_coded_sample) {
    case 1:
    case 33:
        qtrle_decode_1bpp(s, row_ptr, height);
        has_palette = 1;
        break;

    case 2:
    case 34:
        qtrle_decode_2n4bpp(s, row_ptr, height, 2);
        has_palette = 1;
        break;

    case 4:
    case 36:
        qtrle_decode_2n4bpp(s, row_ptr, height, 4);
        has_palette = 1;
        break;

    case 8:
    case 40:
        qtrle_decode_8bpp(s, row_ptr, height);
        has_palette = 1;
        break;

    case 16:
        qtrle_decode_16bpp(s, row_ptr, height);
        break;

    case 24:
        qtrle_decode_24bpp(s, row_ptr, height);
        break;

    case 32:
        qtrle_decode_32bpp(s, row_ptr, height);
        break;

    default:
        av_log (s->avctx, AV_LOG_ERROR, "Unsupported colorspace: %d bits/sample?\n",
            avctx->bits_per_coded_sample);
        break;
    }

    if(has_palette) {
        int size;
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, &size);

        if (pal && size == AVPALETTE_SIZE) {
            s->frame->palette_has_changed = 1;
            memcpy(s->pal, pal, AVPALETTE_SIZE);
        } else if (pal) {
            av_log(avctx, AV_LOG_ERROR, "Palette size %d is wrong\n", size);
        }

        /* make the palette available on the way out */
        memcpy(s->frame->data[1], s->pal, AVPALETTE_SIZE);
    }

done:
    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;
    *got_frame      = 1;

    /* always report that the buffer was completely consumed */
    return avpkt->size;
}

static av_cold int qtrle_decode_end(AVCodecContext *avctx)
{
    QtrleContext *s = avctx->priv_data;

    av_frame_free(&s->frame);

    return 0;
}

AVCodec ff_qtrle_decoder = {
    .name           = "qtrle",
    .long_name      = NULL_IF_CONFIG_SMALL("QuickTime Animation (RLE) video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_QTRLE,
    .priv_data_size = sizeof(QtrleContext),
    .init           = qtrle_decode_init,
    .close          = qtrle_decode_end,
    .decode         = qtrle_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
