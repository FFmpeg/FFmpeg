/*
 * Cinepak Video Decoder
 * Copyright (C) 2003 the ffmpeg project
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
 * Cinepak video decoder
 * by Ewald Snel <ewald@rambo.its.tudelft.nl>
 * For more information on the Cinepak algorithm, visit:
 *   http://www.csse.monash.edu.au/~timf/
 * For more information on the quirky data inside Sega FILM/CPK files, visit:
 *   http://wiki.multimedia.cx/index.php?title=Sega_FILM
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"


typedef struct {
    uint8_t  y0, y1, y2, y3;
    uint8_t  u, v;
} cvid_codebook;

#define MAX_STRIPS      32

typedef struct {
    uint16_t          id;
    uint16_t          x1, y1;
    uint16_t          x2, y2;
    cvid_codebook     v4_codebook[256];
    cvid_codebook     v1_codebook[256];
} cvid_strip;

typedef struct CinepakContext {

    AVCodecContext *avctx;
    AVFrame frame;

    const unsigned char *data;
    int size;

    int width, height;

    int palette_video;
    cvid_strip strips[MAX_STRIPS];

    int sega_film_skip_bytes;

    uint32_t pal[256];
} CinepakContext;

static void cinepak_decode_codebook (cvid_codebook *codebook,
                                     int chunk_id, int size, const uint8_t *data)
{
    const uint8_t *eod = (data + size);
    uint32_t flag, mask;
    int      i, n;

    /* check if this chunk contains 4- or 6-element vectors */
    n    = (chunk_id & 0x04) ? 4 : 6;
    flag = 0;
    mask = 0;

    for (i=0; i < 256; i++) {
        if ((chunk_id & 0x01) && !(mask >>= 1)) {
            if ((data + 4) > eod)
                break;

            flag  = AV_RB32 (data);
            data += 4;
            mask  = 0x80000000;
        }

        if (!(chunk_id & 0x01) || (flag & mask)) {
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

static int cinepak_decode_vectors (CinepakContext *s, cvid_strip *strip,
                                   int chunk_id, int size, const uint8_t *data)
{
    const uint8_t   *eod = (data + size);
    uint32_t         flag, mask;
    cvid_codebook   *codebook;
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
            if ((chunk_id & 0x01) && !(mask >>= 1)) {
                if ((data + 4) > eod)
                    return AVERROR_INVALIDDATA;

                flag  = AV_RB32 (data);
                data += 4;
                mask  = 0x80000000;
            }

            if (!(chunk_id & 0x01) || (flag & mask)) {
                if (!(chunk_id & 0x02) && !(mask >>= 1)) {
                    if ((data + 4) > eod)
                        return AVERROR_INVALIDDATA;

                    flag  = AV_RB32 (data);
                    data += 4;
                    mask  = 0x80000000;
                }

                if ((chunk_id & 0x02) || (~flag & mask)) {
                    if (data >= eod)
                        return AVERROR_INVALIDDATA;

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
                        return AVERROR_INVALIDDATA;

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
                                 cvid_strip *strip, const uint8_t *data, int size)
{
    const uint8_t *eod = (data + size);
    int      chunk_id, chunk_size;

    /* coordinate sanity checks */
    if (strip->x2 > s->width   ||
        strip->y2 > s->height  ||
        strip->x1 >= strip->x2 || strip->y1 >= strip->y2)
        return AVERROR_INVALIDDATA;

    while ((data + 4) <= eod) {
        chunk_id   = data[0];
        chunk_size = AV_RB24 (&data[1]) - 4;
        if(chunk_size < 0)
            return AVERROR_INVALIDDATA;

        data      += 4;
        chunk_size = ((data + chunk_size) > eod) ? (eod - data) : chunk_size;

        switch (chunk_id) {

        case 0x20:
        case 0x21:
        case 0x24:
        case 0x25:
            cinepak_decode_codebook (strip->v4_codebook, chunk_id,
                chunk_size, data);
            break;

        case 0x22:
        case 0x23:
        case 0x26:
        case 0x27:
            cinepak_decode_codebook (strip->v1_codebook, chunk_id,
                chunk_size, data);
            break;

        case 0x30:
        case 0x31:
        case 0x32:
            return cinepak_decode_vectors (s, strip, chunk_id,
                chunk_size, data);
        }

        data += chunk_size;
    }

    return AVERROR_INVALIDDATA;
}

static int cinepak_decode (CinepakContext *s)
{
    const uint8_t  *eod = (s->data + s->size);
    int           i, result, strip_size, frame_flags, num_strips;
    int           y0 = 0;
    int           encoded_buf_size;

    if (s->size < 10)
        return AVERROR_INVALIDDATA;

    frame_flags = s->data[0];
    num_strips  = AV_RB16 (&s->data[8]);
    encoded_buf_size = AV_RB24(&s->data[1]);

    /* if this is the first frame, check for deviant Sega FILM data */
    if (s->sega_film_skip_bytes == -1) {
        if (!encoded_buf_size) {
            av_log_ask_for_sample(s->avctx, "encoded_buf_size is 0");
            return AVERROR_INVALIDDATA;
        }
        if (encoded_buf_size != s->size && (s->size % encoded_buf_size) != 0) {
            /* If the encoded frame size differs from the frame size as indicated
             * by the container file, this data likely comes from a Sega FILM/CPK file.
             * If the frame header is followed by the bytes FE 00 00 06 00 00 then
             * this is probably one of the two known files that have 6 extra bytes
             * after the frame header. Else, assume 2 extra bytes. The container
             * size also cannot be a multiple of the encoded size. */
            if (s->size >= 16 &&
                (s->data[10] == 0xFE) &&
                (s->data[11] == 0x00) &&
                (s->data[12] == 0x00) &&
                (s->data[13] == 0x06) &&
                (s->data[14] == 0x00) &&
                (s->data[15] == 0x00))
                s->sega_film_skip_bytes = 6;
            else
                s->sega_film_skip_bytes = 2;
        } else
            s->sega_film_skip_bytes = 0;
    }

    s->data += 10 + s->sega_film_skip_bytes;

    num_strips = FFMIN(num_strips, MAX_STRIPS);

    s->frame.key_frame = 0;

    for (i=0; i < num_strips; i++) {
        if ((s->data + 12) > eod)
            return AVERROR_INVALIDDATA;

        s->strips[i].id = s->data[0];
        s->strips[i].y1 = y0;
        s->strips[i].x1 = 0;
        s->strips[i].y2 = y0 + AV_RB16 (&s->data[8]);
        s->strips[i].x2 = s->avctx->width;

        if (s->strips[i].id == 0x10)
            s->frame.key_frame = 1;

        strip_size = AV_RB24 (&s->data[1]) - 12;
        if (strip_size < 0)
            return AVERROR_INVALIDDATA;
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

static av_cold int cinepak_decode_init(AVCodecContext *avctx)
{
    CinepakContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->width = (avctx->width + 3) & ~3;
    s->height = (avctx->height + 3) & ~3;
    s->sega_film_skip_bytes = -1;  /* uninitialized state */

    // check for paletted data
    if (avctx->bits_per_coded_sample != 8) {
        s->palette_video = 0;
        avctx->pix_fmt = PIX_FMT_YUV420P;
    } else {
        s->palette_video = 1;
        avctx->pix_fmt = PIX_FMT_PAL8;
    }

    avcodec_get_frame_defaults(&s->frame);
    s->frame.data[0] = NULL;

    return 0;
}

static int cinepak_decode_frame(AVCodecContext *avctx,
                                void *data, int *data_size,
                                AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int ret = 0, buf_size = avpkt->size;
    CinepakContext *s = avctx->priv_data;

    s->data = buf;
    s->size = buf_size;

    s->frame.reference = 3;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE |
                            FF_BUFFER_HINTS_REUSABLE;
    if ((ret = avctx->reget_buffer(avctx, &s->frame))) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return ret;
    }

    if (s->palette_video) {
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, NULL);
        if (pal) {
            s->frame.palette_has_changed = 1;
            memcpy(s->pal, pal, AVPALETTE_SIZE);
        }
    }

    cinepak_decode(s);

    if (s->palette_video)
        memcpy (s->frame.data[1], s->pal, AVPALETTE_SIZE);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int cinepak_decode_end(AVCodecContext *avctx)
{
    CinepakContext *s = avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec ff_cinepak_decoder = {
    .name           = "cinepak",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_CINEPAK,
    .priv_data_size = sizeof(CinepakContext),
    .init           = cinepak_decode_init,
    .close          = cinepak_decode_end,
    .decode         = cinepak_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Cinepak"),
};
