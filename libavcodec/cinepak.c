/*
 * Cinepak Video Decoder
 * Copyright (c) 2003 The FFmpeg Project
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
 * @author Ewald Snel <ewald@rambo.its.tudelft.nl>
 *
 * @see For more information on the Cinepak algorithm, visit:
 *   http://www.csse.monash.edu.au/~timf/
 * @see For more information on the quirky data inside Sega FILM/CPK files, visit:
 *   http://wiki.multimedia.cx/index.php?title=Sega_FILM
 *
 * Cinepak colorspace support (c) 2013 Rl, Aetey Global Technologies AB
 * @author Cinepak colorspace, Rl, Aetey Global Technologies AB
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "internal.h"


typedef uint8_t cvid_codebook[12];

#define MAX_STRIPS      32

typedef struct cvid_strip {
    uint16_t          id;
    uint16_t          x1, y1;
    uint16_t          x2, y2;
    cvid_codebook     v4_codebook[256];
    cvid_codebook     v1_codebook[256];
} cvid_strip;

typedef struct CinepakContext {

    AVCodecContext *avctx;
    AVFrame *frame;

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
    uint8_t *p;

    /* check if this chunk contains 4- or 6-element vectors */
    n    = (chunk_id & 0x04) ? 4 : 6;
    flag = 0;
    mask = 0;

    p = codebook[0];
    for (i=0; i < 256; i++) {
        if ((chunk_id & 0x01) && !(mask >>= 1)) {
            if ((data + 4) > eod)
                break;

            flag  = AV_RB32 (data);
            data += 4;
            mask  = 0x80000000;
        }

        if (!(chunk_id & 0x01) || (flag & mask)) {
            int k, kk;

            if ((data + n) > eod)
                break;

            for (k = 0; k < 4; ++k) {
                int r = *data++;
                for (kk = 0; kk < 3; ++kk)
                    *p++ = r;
            }
            if (n == 6) {
                int r, g, b, u, v;
                u = *(int8_t *)data++;
                v = *(int8_t *)data++;
                p -= 12;
                for(k=0; k<4; ++k) {
                    r = *p++ + v*2;
                    g = *p++ - (u/2) - v;
                    b = *p   + u*2;
                    p -= 2;
                    *p++ = av_clip_uint8(r);
                    *p++ = av_clip_uint8(g);
                    *p++ = av_clip_uint8(b);
                }
            }
        } else {
            p += 12;
        }
    }
}

static int cinepak_decode_vectors (CinepakContext *s, cvid_strip *strip,
                                   int chunk_id, int size, const uint8_t *data)
{
    const uint8_t   *eod = (data + size);
    uint32_t         flag, mask;
    uint8_t         *cb0, *cb1, *cb2, *cb3;
    int             x, y;
    char            *ip0, *ip1, *ip2, *ip3;

    flag = 0;
    mask = 0;

    for (y=strip->y1; y < strip->y2; y+=4) {

/* take care of y dimension not being multiple of 4, such streams exist */
        ip0 = ip1 = ip2 = ip3 = s->frame->data[0] +
          (s->palette_video?strip->x1:strip->x1*3) + (y * s->frame->linesize[0]);
        if(s->avctx->height - y > 1) {
            ip1 = ip0 + s->frame->linesize[0];
            if(s->avctx->height - y > 2) {
                ip2 = ip1 + s->frame->linesize[0];
                if(s->avctx->height - y > 3) {
                    ip3 = ip2 + s->frame->linesize[0];
                }
            }
        }
/* to get the correct picture for not-multiple-of-4 cases let us fill
 * each block from the bottom up, thus possibly overwriting the top line
 * more than once but ending with the correct data in place
 * (instead of in-loop checking) */

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
                    uint8_t *p;
                    if (data >= eod)
                        return AVERROR_INVALIDDATA;

                    p = strip->v1_codebook[*data++];
                    if (s->palette_video) {
                        ip3[0] = ip3[1] = ip2[0] = ip2[1] = p[6];
                        ip3[2] = ip3[3] = ip2[2] = ip2[3] = p[9];
                        ip1[0] = ip1[1] = ip0[0] = ip0[1] = p[0];
                        ip1[2] = ip1[3] = ip0[2] = ip0[3] = p[3];
                    } else {
                        p += 6;
                        memcpy(ip3 + 0, p, 3); memcpy(ip3 + 3, p, 3);
                        memcpy(ip2 + 0, p, 3); memcpy(ip2 + 3, p, 3);
                        p += 3; /* ... + 9 */
                        memcpy(ip3 + 6, p, 3); memcpy(ip3 + 9, p, 3);
                        memcpy(ip2 + 6, p, 3); memcpy(ip2 + 9, p, 3);
                        p -= 9; /* ... + 0 */
                        memcpy(ip1 + 0, p, 3); memcpy(ip1 + 3, p, 3);
                        memcpy(ip0 + 0, p, 3); memcpy(ip0 + 3, p, 3);
                        p += 3; /* ... + 3 */
                        memcpy(ip1 + 6, p, 3); memcpy(ip1 + 9, p, 3);
                        memcpy(ip0 + 6, p, 3); memcpy(ip0 + 9, p, 3);
                    }

                } else if (flag & mask) {
                    if ((data + 4) > eod)
                        return AVERROR_INVALIDDATA;

                    cb0 = strip->v4_codebook[*data++];
                    cb1 = strip->v4_codebook[*data++];
                    cb2 = strip->v4_codebook[*data++];
                    cb3 = strip->v4_codebook[*data++];
                    if (s->palette_video) {
                        uint8_t *p;
                        p = ip3;
                        *p++ = cb2[6];
                        *p++ = cb2[9];
                        *p++ = cb3[6];
                        *p   = cb3[9];
                        p = ip2;
                        *p++ = cb2[0];
                        *p++ = cb2[3];
                        *p++ = cb3[0];
                        *p   = cb3[3];
                        p = ip1;
                        *p++ = cb0[6];
                        *p++ = cb0[9];
                        *p++ = cb1[6];
                        *p   = cb1[9];
                        p = ip0;
                        *p++ = cb0[0];
                        *p++ = cb0[3];
                        *p++ = cb1[0];
                        *p   = cb1[3];
                    } else {
                        memcpy(ip3 + 0, cb2 + 6, 6);
                        memcpy(ip3 + 6, cb3 + 6, 6);
                        memcpy(ip2 + 0, cb2 + 0, 6);
                        memcpy(ip2 + 6, cb3 + 0, 6);
                        memcpy(ip1 + 0, cb0 + 6, 6);
                        memcpy(ip1 + 6, cb1 + 6, 6);
                        memcpy(ip0 + 0, cb0 + 0, 6);
                        memcpy(ip0 + 6, cb1 + 0, 6);
                    }

                }
            }

            if (s->palette_video) {
                ip0 += 4;  ip1 += 4;
                ip2 += 4;  ip3 += 4;
            } else {
                ip0 += 12;  ip1 += 12;
                ip2 += 12;  ip3 += 12;
            }
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
            avpriv_request_sample(s->avctx, "encoded_buf_size 0");
            return AVERROR_PATCHWELCOME;
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

    s->frame->key_frame = 0;

    for (i=0; i < num_strips; i++) {
        if ((s->data + 12) > eod)
            return AVERROR_INVALIDDATA;

        s->strips[i].id = s->data[0];
/* zero y1 means "relative to the previous stripe" */
        if (!(s->strips[i].y1 = AV_RB16 (&s->data[4])))
            s->strips[i].y2 = (s->strips[i].y1 = y0) + AV_RB16 (&s->data[8]);
        else
            s->strips[i].y2 = AV_RB16 (&s->data[8]);
        s->strips[i].x1 = AV_RB16 (&s->data[6]);
        s->strips[i].x2 = AV_RB16 (&s->data[10]);

        if (s->strips[i].id == 0x10)
            s->frame->key_frame = 1;

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
        avctx->pix_fmt = AV_PIX_FMT_RGB24;
    } else {
        s->palette_video = 1;
        avctx->pix_fmt = AV_PIX_FMT_PAL8;
    }

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int cinepak_decode_frame(AVCodecContext *avctx,
                                void *data, int *got_frame,
                                AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int ret = 0, buf_size = avpkt->size;
    CinepakContext *s = avctx->priv_data;

    s->data = buf;
    s->size = buf_size;

    if ((ret = ff_reget_buffer(avctx, s->frame)) < 0)
        return ret;

    if (s->palette_video) {
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, NULL);
        if (pal) {
            s->frame->palette_has_changed = 1;
            memcpy(s->pal, pal, AVPALETTE_SIZE);
        }
    }

    if ((ret = cinepak_decode(s)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "cinepak_decode failed\n");
    }

    if (s->palette_video)
        memcpy (s->frame->data[1], s->pal, AVPALETTE_SIZE);

    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;

    *got_frame = 1;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int cinepak_decode_end(AVCodecContext *avctx)
{
    CinepakContext *s = avctx->priv_data;

    av_frame_free(&s->frame);

    return 0;
}

AVCodec ff_cinepak_decoder = {
    .name           = "cinepak",
    .long_name      = NULL_IF_CONFIG_SMALL("Cinepak"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CINEPAK,
    .priv_data_size = sizeof(CinepakContext),
    .init           = cinepak_decode_init,
    .close          = cinepak_decode_end,
    .decode         = cinepak_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
