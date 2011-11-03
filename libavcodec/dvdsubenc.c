/*
 * DVD subtitle encoding
 * Copyright (c) 2005 Wolfram Gloger
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

#undef NDEBUG
#include <assert.h>

// ncnt is the nibble counter
#define PUTNIBBLE(val)\
do {\
    if (ncnt++ & 1)\
        *q++ = bitbuf | ((val) & 0x0f);\
    else\
        bitbuf = (val) << 4;\
} while(0)

static void dvd_encode_rle(uint8_t **pq,
                           const uint8_t *bitmap, int linesize,
                           int w, int h,
                           const int cmap[256])
{
    uint8_t *q;
    unsigned int bitbuf = 0;
    int ncnt;
    int x, y, len, color;

    q = *pq;

    for (y = 0; y < h; ++y) {
        ncnt = 0;
        for(x = 0; x < w; x += len) {
            color = bitmap[x];
            for (len=1; x+len < w; ++len)
                if (bitmap[x+len] != color)
                    break;
            color = cmap[color];
            assert(color < 4);
            if (len < 0x04) {
                PUTNIBBLE((len << 2)|color);
            } else if (len < 0x10) {
                PUTNIBBLE(len >> 2);
                PUTNIBBLE((len << 2)|color);
            } else if (len < 0x40) {
                PUTNIBBLE(0);
                PUTNIBBLE(len >> 2);
                PUTNIBBLE((len << 2)|color);
            } else if (x+len == w) {
                PUTNIBBLE(0);
                PUTNIBBLE(0);
                PUTNIBBLE(0);
                PUTNIBBLE(color);
            } else {
                if (len > 0xff)
                    len = 0xff;
                PUTNIBBLE(0);
                PUTNIBBLE(len >> 6);
                PUTNIBBLE(len >> 2);
                PUTNIBBLE((len << 2)|color);
            }
        }
        /* end of line */
        if (ncnt & 1)
            PUTNIBBLE(0);
        bitmap += linesize;
    }

    *pq = q;
}

static int encode_dvd_subtitles(uint8_t *outbuf, int outbuf_size,
                                const AVSubtitle *h)
{
    uint8_t *q, *qq;
    int object_id;
    int offset1[20], offset2[20];
    int i, imax, color, alpha, rects = h->num_rects;
    unsigned long hmax;
    unsigned long hist[256];
    int           cmap[256];

    if (rects == 0 || h->rects == NULL)
        return -1;
    if (rects > 20)
        rects = 20;

    // analyze bitmaps, compress to 4 colors
    for (i=0; i<256; ++i) {
        hist[i] = 0;
        cmap[i] = 0;
    }
    for (object_id = 0; object_id < rects; object_id++)
        for (i=0; i<h->rects[object_id]->w*h->rects[object_id]->h; ++i) {
            color = h->rects[object_id]->pict.data[0][i];
            // only count non-transparent pixels
            alpha = ((uint32_t*)h->rects[object_id]->pict.data[1])[color] >> 24;
            hist[color] += alpha;
        }
    for (color=3;; --color) {
        hmax = 0;
        imax = 0;
        for (i=0; i<256; ++i)
            if (hist[i] > hmax) {
                imax = i;
                hmax = hist[i];
            }
        if (hmax == 0)
            break;
        if (color == 0)
            color = 3;
        av_log(NULL, AV_LOG_DEBUG, "dvd_subtitle hist[%d]=%ld -> col %d\n",
               imax, hist[imax], color);
        cmap[imax] = color;
        hist[imax] = 0;
    }


    // encode data block
    q = outbuf + 4;
    for (object_id = 0; object_id < rects; object_id++) {
        offset1[object_id] = q - outbuf;
        // worst case memory requirement: 1 nibble per pixel..
        if ((q - outbuf) + h->rects[object_id]->w*h->rects[object_id]->h/2
            + 17*rects + 21 > outbuf_size) {
            av_log(NULL, AV_LOG_ERROR, "dvd_subtitle too big\n");
            return -1;
        }
        dvd_encode_rle(&q, h->rects[object_id]->pict.data[0],
                       h->rects[object_id]->w*2,
                       h->rects[object_id]->w, h->rects[object_id]->h >> 1,
                       cmap);
        offset2[object_id] = q - outbuf;
        dvd_encode_rle(&q, h->rects[object_id]->pict.data[0] + h->rects[object_id]->w,
                       h->rects[object_id]->w*2,
                       h->rects[object_id]->w, h->rects[object_id]->h >> 1,
                       cmap);
    }

    // set data packet size
    qq = outbuf + 2;
    bytestream_put_be16(&qq, q - outbuf);

    // send start display command
    bytestream_put_be16(&q, (h->start_display_time*90) >> 10);
    bytestream_put_be16(&q, (q - outbuf) /*- 2 */ + 8 + 12*rects + 2);
    *q++ = 0x03; // palette - 4 nibbles
    *q++ = 0x03; *q++ = 0x7f;
    *q++ = 0x04; // alpha - 4 nibbles
    *q++ = 0xf0; *q++ = 0x00;
    //*q++ = 0x0f; *q++ = 0xff;

    // XXX not sure if more than one rect can really be encoded..
    // 12 bytes per rect
    for (object_id = 0; object_id < rects; object_id++) {
        int x2 = h->rects[object_id]->x + h->rects[object_id]->w - 1;
        int y2 = h->rects[object_id]->y + h->rects[object_id]->h - 1;

        *q++ = 0x05;
        // x1 x2 -> 6 nibbles
        *q++ = h->rects[object_id]->x >> 4;
        *q++ = (h->rects[object_id]->x << 4) | ((x2 >> 8) & 0xf);
        *q++ = x2;
        // y1 y2 -> 6 nibbles
        *q++ = h->rects[object_id]->y >> 4;
        *q++ = (h->rects[object_id]->y << 4) | ((y2 >> 8) & 0xf);
        *q++ = y2;

        *q++ = 0x06;
        // offset1, offset2
        bytestream_put_be16(&q, offset1[object_id]);
        bytestream_put_be16(&q, offset2[object_id]);
    }
    *q++ = 0x01; // start command
    *q++ = 0xff; // terminating command

    // send stop display command last
    bytestream_put_be16(&q, (h->end_display_time*90) >> 10);
    bytestream_put_be16(&q, (q - outbuf) - 2 /*+ 4*/);
    *q++ = 0x02; // set end
    *q++ = 0xff; // terminating command

    qq = outbuf;
    bytestream_put_be16(&qq, q - outbuf);

    av_log(NULL, AV_LOG_DEBUG, "subtitle_packet size=%td\n", q - outbuf);
    return q - outbuf;
}

static int dvdsub_encode(AVCodecContext *avctx,
                         unsigned char *buf, int buf_size, void *data)
{
    //DVDSubtitleContext *s = avctx->priv_data;
    AVSubtitle *sub = data;
    int ret;

    ret = encode_dvd_subtitles(buf, buf_size, sub);
    return ret;
}

AVCodec ff_dvdsub_encoder = {
    .name           = "dvdsub",
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = CODEC_ID_DVD_SUBTITLE,
    .encode         = dvdsub_encode,
    .long_name = NULL_IF_CONFIG_SMALL("DVD subtitles"),
};
