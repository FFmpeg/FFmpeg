/*
 * Cinepak Video Decoder
 * Copyright (C) 2003 the ffmpeg project
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
 *
 */

/**
 * @file cinepak.c
 * Cinepak video decoder
 * by Ewald Snel <ewald@rambo.its.tudelft.nl>
 * For more information on the Cinepak algorithm, visit:
 *   http://www.csse.monash.edu.au/~timf/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"


typedef struct {
    uint8_t  y0, y1, y2, y3;
    uint8_t  u, v;
} cvid_codebook_t;

#define MAX_STRIPS      32

typedef struct {
    uint16_t          id;
    uint16_t          x1, y1;
    uint16_t          x2, y2;
    cvid_codebook_t   v4_codebook[256];
    cvid_codebook_t   v1_codebook[256];
} cvid_strip_t;

typedef struct CinepakContext {

    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame frame;

    unsigned char *data;
    int size;

    int width, height;

    int palette_video;
    cvid_strip_t strips[MAX_STRIPS];

} CinepakContext;

static void cinepak_decode_codebook (cvid_codebook_t *codebook,
                                     int chunk_id, int size, uint8_t *data)
{
    uint8_t *eod = (data + size);
    uint32_t flag, mask;
    int      i, n;

    /* check if this chunk contains 4- or 6-element vectors */
    n    = (chunk_id & 0x0400) ? 4 : 6;
    flag = 0;
    mask = 0;

    for (i=0; i < 256; i++) {
        if ((chunk_id & 0x0100) && !(mask >>= 1)) {
            if ((data + 4) > eod)
                break;

            flag  = BE_32 (data);
            data += 4;
            mask  = 0x80000000;
        }

        if (!(chunk_id & 0x0100) || (flag & mask)) {
            if ((data + n) > eod)
                break;

            if (n == 6) {
                codebook[i].y0 = *data++;
                codebook[i].y1 = *data++;
                codebook[i].y2 = *data++;
                codebook[i].y3 = *data++;
                codebook[i].u  = 128 + *data++;
                codebook[i].v  = 128 + *data++;
            } else {
                /* this codebook type indicates either greyscale or 
                 * palettized video; if palettized, U & V components will
                 * not be used so it is safe to set them to 128 for the
                 * benefit of greyscale rendering in YUV420P */
                codebook[i].y0 = *data++;
                codebook[i].y1 = *data++;
                codebook[i].y2 = *data++;
                codebook[i].y3 = *data++;
                codebook[i].u  = 128;
                codebook[i].v  = 128;
            }
        }
    }
}

static int cinepak_decode_vectors (CinepakContext *s, cvid_strip_t *strip,
                                   int chunk_id, int size, uint8_t *data)
{
    uint8_t         *eod = (data + size);
    uint32_t         flag, mask;
    cvid_codebook_t *codebook;
    unsigned int     x, y;
    uint32_t         iy[4];
    uint32_t         iu[2];
    uint32_t         iv[2];

    flag = 0;
    mask = 0;

    for (y=strip->y1; y < strip->y2; y+=4) {

        iy[0] = strip->x1 + (y * s->frame.linesize[0]);
        iy[1] = iy[0] + s->frame.linesize[0];
        iy[2] = iy[1] + s->frame.linesize[0];
        iy[3] = iy[2] + s->frame.linesize[0];
        iu[0] = (strip->x1/2) + ((y/2) * s->frame.linesize[1]);
        iu[1] = iu[0] + s->frame.linesize[1];
        iv[0] = (strip->x1/2) + ((y/2) * s->frame.linesize[2]);
        iv[1] = iv[0] + s->frame.linesize[2];

        for (x=strip->x1; x < strip->x2; x+=4) {
            if ((chunk_id & 0x0100) && !(mask >>= 1)) {
                if ((data + 4) > eod)
                    return -1;

                flag  = BE_32 (data);
                data += 4;
                mask  = 0x80000000;
            }

            if (!(chunk_id & 0x0100) || (flag & mask)) {
                if (!(chunk_id & 0x0200) && !(mask >>= 1)) {
                    if ((data + 4) > eod)
                        return -1;

                    flag  = BE_32 (data);
                    data += 4;
                    mask  = 0x80000000;
                }

                if ((chunk_id & 0x0200) || (~flag & mask)) {
                    if (data >= eod)
                        return -1;

                    codebook = &strip->v1_codebook[*data++];
                    s->frame.data[0][iy[0] + 0] = codebook->y0;
                    s->frame.data[0][iy[0] + 1] = codebook->y0;
                    s->frame.data[0][iy[1] + 0] = codebook->y0;
                    s->frame.data[0][iy[1] + 1] = codebook->y0;
                    if (!s->palette_video) {
                        s->frame.data[1][iu[0]] = codebook->u;
                        s->frame.data[2][iv[0]] = codebook->v;
                    }

                    s->frame.data[0][iy[0] + 2] = codebook->y1;
                    s->frame.data[0][iy[0] + 3] = codebook->y1;
                    s->frame.data[0][iy[1] + 2] = codebook->y1;
                    s->frame.data[0][iy[1] + 3] = codebook->y1;
                    if (!s->palette_video) {
                        s->frame.data[1][iu[0] + 1] = codebook->u;
                        s->frame.data[2][iv[0] + 1] = codebook->v;
                    }

                    s->frame.data[0][iy[2] + 0] = codebook->y2;
                    s->frame.data[0][iy[2] + 1] = codebook->y2;
                    s->frame.data[0][iy[3] + 0] = codebook->y2;
                    s->frame.data[0][iy[3] + 1] = codebook->y2;
                    if (!s->palette_video) {
                        s->frame.data[1][iu[1]] = codebook->u;
                        s->frame.data[2][iv[1]] = codebook->v;
                    }

                    s->frame.data[0][iy[2] + 2] = codebook->y3;
                    s->frame.data[0][iy[2] + 3] = codebook->y3;
                    s->frame.data[0][iy[3] + 2] = codebook->y3;
                    s->frame.data[0][iy[3] + 3] = codebook->y3;
                    if (!s->palette_video) {
                        s->frame.data[1][iu[1] + 1] = codebook->u;
                        s->frame.data[2][iv[1] + 1] = codebook->v;
                    }

                } else if (flag & mask) {
                    if ((data + 4) > eod)
                        return -1;

                    codebook = &strip->v4_codebook[*data++];
                    s->frame.data[0][iy[0] + 0] = codebook->y0;
                    s->frame.data[0][iy[0] + 1] = codebook->y1;
                    s->frame.data[0][iy[1] + 0] = codebook->y2;
                    s->frame.data[0][iy[1] + 1] = codebook->y3;
                    if (!s->palette_video) {
                        s->frame.data[1][iu[0]] = codebook->u;
                        s->frame.data[2][iv[0]] = codebook->v;
                    }

                    codebook = &strip->v4_codebook[*data++];
                    s->frame.data[0][iy[0] + 2] = codebook->y0;
                    s->frame.data[0][iy[0] + 3] = codebook->y1;
                    s->frame.data[0][iy[1] + 2] = codebook->y2;
                    s->frame.data[0][iy[1] + 3] = codebook->y3;
                    if (!s->palette_video) {
                        s->frame.data[1][iu[0] + 1] = codebook->u;
                        s->frame.data[2][iv[0] + 1] = codebook->v;
                    }

                    codebook = &strip->v4_codebook[*data++];
                    s->frame.data[0][iy[2] + 0] = codebook->y0;
                    s->frame.data[0][iy[2] + 1] = codebook->y1;
                    s->frame.data[0][iy[3] + 0] = codebook->y2;
                    s->frame.data[0][iy[3] + 1] = codebook->y3;
                    if (!s->palette_video) {
                        s->frame.data[1][iu[1]] = codebook->u;
                        s->frame.data[2][iv[1]] = codebook->v;
                    }

                    codebook = &strip->v4_codebook[*data++];
                    s->frame.data[0][iy[2] + 2] = codebook->y0;
                    s->frame.data[0][iy[2] + 3] = codebook->y1;
                    s->frame.data[0][iy[3] + 2] = codebook->y2;
                    s->frame.data[0][iy[3] + 3] = codebook->y3;
                    if (!s->palette_video) {
                        s->frame.data[1][iu[1] + 1] = codebook->u;
                        s->frame.data[2][iv[1] + 1] = codebook->v;
                    }

                }
            }

            iy[0] += 4;  iy[1] += 4;
            iy[2] += 4;  iy[3] += 4;
            iu[0] += 2;  iu[1] += 2;
            iv[0] += 2;  iv[1] += 2;
        }
    }

    return 0;
}

static int cinepak_decode_strip (CinepakContext *s,
                                 cvid_strip_t *strip, uint8_t *data, int size)
{
    uint8_t *eod = (data + size);
    int      chunk_id, chunk_size;

    /* coordinate sanity checks */
    if (strip->x1 >= s->width  || strip->x2 > s->width  ||
        strip->y1 >= s->height || strip->y2 > s->height ||
        strip->x1 >= strip->x2 || strip->y1 >= strip->y2)
        return -1;

    while ((data + 4) <= eod) {
        chunk_id   = BE_16 (&data[0]);
        chunk_size = BE_16 (&data[2]) - 4;
        data      += 4;
        chunk_size = ((data + chunk_size) > eod) ? (eod - data) : chunk_size;

        switch (chunk_id) {

        case 0x2000:
        case 0x2100:
        case 0x2400:
        case 0x2500:
            cinepak_decode_codebook (strip->v4_codebook, chunk_id, 
                chunk_size, data);
            break;

        case 0x2200:
        case 0x2300:
        case 0x2600:
        case 0x2700:
            cinepak_decode_codebook (strip->v1_codebook, chunk_id, 
                chunk_size, data);
            break;

        case 0x3000:
        case 0x3100:
        case 0x3200:
            return cinepak_decode_vectors (s, strip, chunk_id, 
                chunk_size, data);
        }

        data += chunk_size;
    }

    return -1;
}

static int cinepak_decode (CinepakContext *s)
{
    uint8_t      *eod = (s->data + s->size);
    int           i, result, strip_size, frame_flags, num_strips;
    int           y0 = 0;

    if (s->size < 10)
        return -1;

    frame_flags = s->data[0];
    num_strips  = BE_16 (&s->data[8]);
    s->data    += 10;

    if (num_strips > MAX_STRIPS)
        num_strips = MAX_STRIPS;

    for (i=0; i < num_strips; i++) {
        if ((s->data + 12) > eod)
            return -1;

        s->strips[i].id = BE_16 (s->data);
        s->strips[i].y1 = y0;
        s->strips[i].x1 = 0;
        s->strips[i].y2 = y0 + BE_16 (&s->data[8]);
        s->strips[i].x2 = s->avctx->width;

        strip_size = BE_16 (&s->data[2]) - 12;
        s->data   += 12;
        strip_size = ((s->data + strip_size) > eod) ? (eod - s->data) : strip_size;

        if ((i > 0) && !(frame_flags & 0x01)) {
            memcpy (s->strips[i].v4_codebook, s->strips[i-1].v4_codebook,
                sizeof(s->strips[i].v4_codebook));
            memcpy (s->strips[i].v1_codebook, s->strips[i-1].v1_codebook,
                sizeof(s->strips[i].v1_codebook));
        }

        result = cinepak_decode_strip (s, &s->strips[i], s->data, strip_size);

        if (result != 0)
            return result;

        s->data += strip_size;
        y0    = s->strips[i].y2;
    }
    return 0;
}

static int cinepak_decode_init(AVCodecContext *avctx)
{
    CinepakContext *s = (CinepakContext *)avctx->priv_data;

    s->avctx = avctx;
    s->width = (avctx->width + 3) & ~3;
    s->height = (avctx->height + 3) & ~3;

    // check for paletted data
    if ((avctx->palctrl == NULL) || (avctx->bits_per_sample == 40)) {
        s->palette_video = 0;
        avctx->pix_fmt = PIX_FMT_YUV420P;
    } else {
        s->palette_video = 1;
        avctx->pix_fmt = PIX_FMT_PAL8;
    }

    avctx->has_b_frames = 0;
    dsputil_init(&s->dsp, avctx);

    s->frame.data[0] = NULL;

    return 0;
}

static int cinepak_decode_frame(AVCodecContext *avctx,
                                void *data, int *data_size,
                                uint8_t *buf, int buf_size)
{
    CinepakContext *s = (CinepakContext *)avctx->priv_data;

    s->data = buf;
    s->size = buf_size;

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE |
                            FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame)) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    cinepak_decode(s);

    if (s->palette_video) {
        memcpy (s->frame.data[1], avctx->palctrl->palette, AVPALETTE_SIZE);
        if (avctx->palctrl->palette_changed) {
            s->frame.palette_has_changed = 1;
            avctx->palctrl->palette_changed = 0;
        } else
            s->frame.palette_has_changed = 0;
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static int cinepak_decode_end(AVCodecContext *avctx)
{
    CinepakContext *s = (CinepakContext *)avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec cinepak_decoder = {
    "cinepak",
    CODEC_TYPE_VIDEO,
    CODEC_ID_CINEPAK,
    sizeof(CinepakContext),
    cinepak_decode_init,
    NULL,
    cinepak_decode_end,
    cinepak_decode_frame,
    CODEC_CAP_DR1,
};
