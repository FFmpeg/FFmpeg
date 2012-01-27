/*
 * Quicktime Animation (RLE) Video Decoder
 * Copyright (C) 2004 the ffmpeg project
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

#include "libavutil/intreadwrite.h"
#include "avcodec.h"

typedef struct QtrleContext {

    AVCodecContext *avctx;
    AVFrame frame;

    const unsigned char *buf;
    int size;

    uint32_t pal[256];
} QtrleContext;

#define CHECK_STREAM_PTR(n) \
  if ((stream_ptr + n) > s->size) { \
    av_log (s->avctx, AV_LOG_INFO, "Problem: stream_ptr out of bounds (%d >= %d)\n", \
      stream_ptr + n, s->size); \
    return; \
  }

#define CHECK_PIXEL_PTR(n) \
  if ((pixel_ptr + n > pixel_limit) || (pixel_ptr + n < 0)) { \
    av_log (s->avctx, AV_LOG_INFO, "Problem: pixel_ptr = %d, pixel_limit = %d\n", \
      pixel_ptr + n, pixel_limit); \
    return; \
  } \

static void qtrle_decode_1bpp(QtrleContext *s, int stream_ptr, int row_ptr, int lines_to_change)
{
    int rle_code;
    int pixel_ptr = 0;
    int row_inc = s->frame.linesize[0];
    unsigned char pi0, pi1;  /* 2 8-pixel values */
    unsigned char *rgb = s->frame.data[0];
    int pixel_limit = s->frame.linesize[0] * s->avctx->height;
    int skip;

    while (lines_to_change) {
        CHECK_STREAM_PTR(2);
        skip = s->buf[stream_ptr++];
        rle_code = (signed char)s->buf[stream_ptr++];
        if (rle_code == 0)
            break;
        if(skip & 0x80) {
            lines_to_change--;
            row_ptr += row_inc;
            pixel_ptr = row_ptr + 2 * (skip & 0x7f);
        } else
            pixel_ptr += 2 * skip;
        CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */

        if (rle_code < 0) {
            /* decode the run length code */
            rle_code = -rle_code;
            /* get the next 2 bytes from the stream, treat them as groups
             * of 8 pixels, and output them rle_code times */
            CHECK_STREAM_PTR(2);
            pi0 = s->buf[stream_ptr++];
            pi1 = s->buf[stream_ptr++];
            CHECK_PIXEL_PTR(rle_code * 2);

            while (rle_code--) {
                rgb[pixel_ptr++] = pi0;
                rgb[pixel_ptr++] = pi1;
            }
        } else {
            /* copy the same pixel directly to output 2 times */
            rle_code *= 2;
            CHECK_STREAM_PTR(rle_code);
            CHECK_PIXEL_PTR(rle_code);

            while (rle_code--)
                rgb[pixel_ptr++] = s->buf[stream_ptr++];
        }
    }
}

static inline void qtrle_decode_2n4bpp(QtrleContext *s, int stream_ptr,
                             int row_ptr, int lines_to_change, int bpp)
{
    int rle_code, i;
    int pixel_ptr;
    int row_inc = s->frame.linesize[0];
    unsigned char pi[16];  /* 16 palette indices */
    unsigned char *rgb = s->frame.data[0];
    int pixel_limit = s->frame.linesize[0] * s->avctx->height;
    int num_pixels = (bpp == 4) ? 8 : 16;

    while (lines_to_change--) {
        CHECK_STREAM_PTR(2);
        pixel_ptr = row_ptr + (num_pixels * (s->buf[stream_ptr++] - 1));

        while ((rle_code = (signed char)s->buf[stream_ptr++]) != -1) {
            if (rle_code == 0) {
                /* there's another skip code in the stream */
                CHECK_STREAM_PTR(1);
                pixel_ptr += (num_pixels * (s->buf[stream_ptr++] - 1));
                CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */
            } else if (rle_code < 0) {
                /* decode the run length code */
                rle_code = -rle_code;
                /* get the next 4 bytes from the stream, treat them as palette
                 * indexes, and output them rle_code times */
                CHECK_STREAM_PTR(4);
                for (i = num_pixels-1; i >= 0; i--) {
                    pi[num_pixels-1-i] = (s->buf[stream_ptr] >> ((i*bpp) & 0x07)) & ((1<<bpp)-1);
                    stream_ptr+= ((i & ((num_pixels>>2)-1)) == 0);
                }
                CHECK_PIXEL_PTR(rle_code * num_pixels);
                while (rle_code--) {
                    for (i = 0; i < num_pixels; i++)
                        rgb[pixel_ptr++] = pi[i];
                }
            } else {
                /* copy the same pixel directly to output 4 times */
                rle_code *= 4;
                CHECK_STREAM_PTR(rle_code);
                CHECK_PIXEL_PTR(rle_code*(num_pixels>>2));
                while (rle_code--) {
                    if(bpp == 4) {
                        rgb[pixel_ptr++] = ((s->buf[stream_ptr]) >> 4) & 0x0f;
                        rgb[pixel_ptr++] = (s->buf[stream_ptr++]) & 0x0f;
                    } else {
                        rgb[pixel_ptr++] = ((s->buf[stream_ptr]) >> 6) & 0x03;
                        rgb[pixel_ptr++] = ((s->buf[stream_ptr]) >> 4) & 0x03;
                        rgb[pixel_ptr++] = ((s->buf[stream_ptr]) >> 2) & 0x03;
                        rgb[pixel_ptr++] = (s->buf[stream_ptr++]) & 0x03;
                    }
                }
            }
        }
        row_ptr += row_inc;
    }
}

static void qtrle_decode_8bpp(QtrleContext *s, int stream_ptr, int row_ptr, int lines_to_change)
{
    int rle_code;
    int pixel_ptr;
    int row_inc = s->frame.linesize[0];
    unsigned char pi1, pi2, pi3, pi4;  /* 4 palette indexes */
    unsigned char *rgb = s->frame.data[0];
    int pixel_limit = s->frame.linesize[0] * s->avctx->height;

    while (lines_to_change--) {
        CHECK_STREAM_PTR(2);
        pixel_ptr = row_ptr + (4 * (s->buf[stream_ptr++] - 1));

        while ((rle_code = (signed char)s->buf[stream_ptr++]) != -1) {
            if (rle_code == 0) {
                /* there's another skip code in the stream */
                CHECK_STREAM_PTR(1);
                pixel_ptr += (4 * (s->buf[stream_ptr++] - 1));
                CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */
            } else if (rle_code < 0) {
                /* decode the run length code */
                rle_code = -rle_code;
                /* get the next 4 bytes from the stream, treat them as palette
                 * indexes, and output them rle_code times */
                CHECK_STREAM_PTR(4);
                pi1 = s->buf[stream_ptr++];
                pi2 = s->buf[stream_ptr++];
                pi3 = s->buf[stream_ptr++];
                pi4 = s->buf[stream_ptr++];

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
                CHECK_STREAM_PTR(rle_code);
                CHECK_PIXEL_PTR(rle_code);

                while (rle_code--) {
                    rgb[pixel_ptr++] = s->buf[stream_ptr++];
                }
            }
        }
        row_ptr += row_inc;
    }
}

static void qtrle_decode_16bpp(QtrleContext *s, int stream_ptr, int row_ptr, int lines_to_change)
{
    int rle_code;
    int pixel_ptr;
    int row_inc = s->frame.linesize[0];
    unsigned short rgb16;
    unsigned char *rgb = s->frame.data[0];
    int pixel_limit = s->frame.linesize[0] * s->avctx->height;

    while (lines_to_change--) {
        CHECK_STREAM_PTR(2);
        pixel_ptr = row_ptr + (s->buf[stream_ptr++] - 1) * 2;

        while ((rle_code = (signed char)s->buf[stream_ptr++]) != -1) {
            if (rle_code == 0) {
                /* there's another skip code in the stream */
                CHECK_STREAM_PTR(1);
                pixel_ptr += (s->buf[stream_ptr++] - 1) * 2;
                CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */
            } else if (rle_code < 0) {
                /* decode the run length code */
                rle_code = -rle_code;
                CHECK_STREAM_PTR(2);
                rgb16 = AV_RB16(&s->buf[stream_ptr]);
                stream_ptr += 2;

                CHECK_PIXEL_PTR(rle_code * 2);

                while (rle_code--) {
                    *(unsigned short *)(&rgb[pixel_ptr]) = rgb16;
                    pixel_ptr += 2;
                }
            } else {
                CHECK_STREAM_PTR(rle_code * 2);
                CHECK_PIXEL_PTR(rle_code * 2);

                /* copy pixels directly to output */
                while (rle_code--) {
                    rgb16 = AV_RB16(&s->buf[stream_ptr]);
                    stream_ptr += 2;
                    *(unsigned short *)(&rgb[pixel_ptr]) = rgb16;
                    pixel_ptr += 2;
                }
            }
        }
        row_ptr += row_inc;
    }
}

static void qtrle_decode_24bpp(QtrleContext *s, int stream_ptr, int row_ptr, int lines_to_change)
{
    int rle_code;
    int pixel_ptr;
    int row_inc = s->frame.linesize[0];
    unsigned char r, g, b;
    unsigned char *rgb = s->frame.data[0];
    int pixel_limit = s->frame.linesize[0] * s->avctx->height;

    while (lines_to_change--) {
        CHECK_STREAM_PTR(2);
        pixel_ptr = row_ptr + (s->buf[stream_ptr++] - 1) * 3;

        while ((rle_code = (signed char)s->buf[stream_ptr++]) != -1) {
            if (rle_code == 0) {
                /* there's another skip code in the stream */
                CHECK_STREAM_PTR(1);
                pixel_ptr += (s->buf[stream_ptr++] - 1) * 3;
                CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */
            } else if (rle_code < 0) {
                /* decode the run length code */
                rle_code = -rle_code;
                CHECK_STREAM_PTR(3);
                r = s->buf[stream_ptr++];
                g = s->buf[stream_ptr++];
                b = s->buf[stream_ptr++];

                CHECK_PIXEL_PTR(rle_code * 3);

                while (rle_code--) {
                    rgb[pixel_ptr++] = r;
                    rgb[pixel_ptr++] = g;
                    rgb[pixel_ptr++] = b;
                }
            } else {
                CHECK_STREAM_PTR(rle_code * 3);
                CHECK_PIXEL_PTR(rle_code * 3);

                /* copy pixels directly to output */
                while (rle_code--) {
                    rgb[pixel_ptr++] = s->buf[stream_ptr++];
                    rgb[pixel_ptr++] = s->buf[stream_ptr++];
                    rgb[pixel_ptr++] = s->buf[stream_ptr++];
                }
            }
        }
        row_ptr += row_inc;
    }
}

static void qtrle_decode_32bpp(QtrleContext *s, int stream_ptr, int row_ptr, int lines_to_change)
{
    int rle_code;
    int pixel_ptr;
    int row_inc = s->frame.linesize[0];
    unsigned int argb;
    unsigned char *rgb = s->frame.data[0];
    int pixel_limit = s->frame.linesize[0] * s->avctx->height;

    while (lines_to_change--) {
        CHECK_STREAM_PTR(2);
        pixel_ptr = row_ptr + (s->buf[stream_ptr++] - 1) * 4;

        while ((rle_code = (signed char)s->buf[stream_ptr++]) != -1) {
            if (rle_code == 0) {
                /* there's another skip code in the stream */
                CHECK_STREAM_PTR(1);
                pixel_ptr += (s->buf[stream_ptr++] - 1) * 4;
                CHECK_PIXEL_PTR(0);  /* make sure pixel_ptr is positive */
            } else if (rle_code < 0) {
                /* decode the run length code */
                rle_code = -rle_code;
                CHECK_STREAM_PTR(4);
                argb = AV_RB32(s->buf + stream_ptr);
                stream_ptr += 4;

                CHECK_PIXEL_PTR(rle_code * 4);

                while (rle_code--) {
                    AV_WN32A(rgb + pixel_ptr, argb);
                    pixel_ptr += 4;
                }
            } else {
                CHECK_STREAM_PTR(rle_code * 4);
                CHECK_PIXEL_PTR(rle_code * 4);

                /* copy pixels directly to output */
                while (rle_code--) {
                    argb = AV_RB32(s->buf + stream_ptr);
                    AV_WN32A(rgb + pixel_ptr, argb);
                    stream_ptr += 4;
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
    case 33:
        avctx->pix_fmt = PIX_FMT_MONOWHITE;
        break;

    case 2:
    case 4:
    case 8:
    case 34:
    case 36:
    case 40:
        avctx->pix_fmt = PIX_FMT_PAL8;
        break;

    case 16:
        avctx->pix_fmt = PIX_FMT_RGB555;
        break;

    case 24:
        avctx->pix_fmt = PIX_FMT_RGB24;
        break;

    case 32:
        avctx->pix_fmt = PIX_FMT_RGB32;
        break;

    default:
        av_log (avctx, AV_LOG_ERROR, "Unsupported colorspace: %d bits/sample?\n",
            avctx->bits_per_coded_sample);
        break;
    }

    s->frame.data[0] = NULL;

    return 0;
}

static int qtrle_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    QtrleContext *s = avctx->priv_data;
    int header, start_line;
    int stream_ptr, height, row_ptr;
    int has_palette = 0;

    s->buf = buf;
    s->size = buf_size;

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE |
                            FF_BUFFER_HINTS_REUSABLE | FF_BUFFER_HINTS_READABLE;
    if (avctx->reget_buffer(avctx, &s->frame)) {
        av_log (s->avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    /* check if this frame is even supposed to change */
    if (s->size < 8)
        goto done;

    /* start after the chunk size */
    stream_ptr = 4;

    /* fetch the header */
    header = AV_RB16(&s->buf[stream_ptr]);
    stream_ptr += 2;

    /* if a header is present, fetch additional decoding parameters */
    if (header & 0x0008) {
        if(s->size < 14)
            goto done;
        start_line = AV_RB16(&s->buf[stream_ptr]);
        stream_ptr += 4;
        height = AV_RB16(&s->buf[stream_ptr]);
        stream_ptr += 4;
    } else {
        start_line = 0;
        height = s->avctx->height;
    }
    row_ptr = s->frame.linesize[0] * start_line;

    switch (avctx->bits_per_coded_sample) {
    case 1:
    case 33:
        qtrle_decode_1bpp(s, stream_ptr, row_ptr, height);
        break;

    case 2:
    case 34:
        qtrle_decode_2n4bpp(s, stream_ptr, row_ptr, height, 2);
        has_palette = 1;
        break;

    case 4:
    case 36:
        qtrle_decode_2n4bpp(s, stream_ptr, row_ptr, height, 4);
        has_palette = 1;
        break;

    case 8:
    case 40:
        qtrle_decode_8bpp(s, stream_ptr, row_ptr, height);
        has_palette = 1;
        break;

    case 16:
        qtrle_decode_16bpp(s, stream_ptr, row_ptr, height);
        break;

    case 24:
        qtrle_decode_24bpp(s, stream_ptr, row_ptr, height);
        break;

    case 32:
        qtrle_decode_32bpp(s, stream_ptr, row_ptr, height);
        break;

    default:
        av_log (s->avctx, AV_LOG_ERROR, "Unsupported colorspace: %d bits/sample?\n",
            avctx->bits_per_coded_sample);
        break;
    }

    if(has_palette) {
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, NULL);

        if (pal) {
            s->frame.palette_has_changed = 1;
            memcpy(s->pal, pal, AVPALETTE_SIZE);
        }

        /* make the palette available on the way out */
        memcpy(s->frame.data[1], s->pal, AVPALETTE_SIZE);
    }

done:
    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int qtrle_decode_end(AVCodecContext *avctx)
{
    QtrleContext *s = avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec ff_qtrle_decoder = {
    .name           = "qtrle",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_QTRLE,
    .priv_data_size = sizeof(QtrleContext),
    .init           = qtrle_decode_init,
    .close          = qtrle_decode_end,
    .decode         = qtrle_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("QuickTime Animation (RLE) video"),
};
