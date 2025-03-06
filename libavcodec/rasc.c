/*
 * RemotelyAnywhere Screen Capture decoder
 *
 * Copyright (c) 2018 Paul B Mahol
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

#include <stdio.h>
#include <string.h>

#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "zlib_wrapper.h"

#include <zlib.h>

#define KBND MKTAG('K', 'B', 'N', 'D')
#define FINT MKTAG('F', 'I', 'N', 'T')
#define INIT MKTAG('I', 'N', 'I', 'T')
#define BNDL MKTAG('B', 'N', 'D', 'L')
#define KFRM MKTAG('K', 'F', 'R', 'M')
#define DLTA MKTAG('D', 'L', 'T', 'A')
#define MOUS MKTAG('M', 'O', 'U', 'S')
#define MPOS MKTAG('M', 'P', 'O', 'S')
#define MOVE MKTAG('M', 'O', 'V', 'E')
#define EMPT MKTAG('E', 'M', 'P', 'T')

typedef struct RASCContext {
    AVClass        *class;
    int             skip_cursor;
    GetByteContext  gb;
    uint8_t        *delta;
    int             delta_size;
    uint8_t        *cursor;
    int             cursor_size;
    unsigned        cursor_w;
    unsigned        cursor_h;
    unsigned        cursor_x;
    unsigned        cursor_y;
    int             stride;
    int             bpp;
    AVFrame        *frame;
    AVFrame        *frame1;
    AVFrame        *frame2;
    FFZStream       zstream;
} RASCContext;

static void clear_plane(AVCodecContext *avctx, AVFrame *frame)
{
    RASCContext *s = avctx->priv_data;
    uint8_t *dst = frame->data[0];

    if (!dst)
        return;

    for (int y = 0; y < avctx->height; y++) {
        memset(dst, 0, avctx->width * s->bpp);
        dst += frame->linesize[0];
    }
}

static void copy_plane(AVCodecContext *avctx, AVFrame *src, AVFrame *dst)
{
    RASCContext *s = avctx->priv_data;
    uint8_t *srcp = src->data[0];
    uint8_t *dstp = dst->data[0];

    for (int y = 0; y < avctx->height; y++) {
        memcpy(dstp, srcp, s->stride);
        srcp += src->linesize[0];
        dstp += dst->linesize[0];
    }
}

static int init_frames(AVCodecContext *avctx)
{
    RASCContext *s = avctx->priv_data;
    int ret;

    av_frame_unref(s->frame1);
    av_frame_unref(s->frame2);
    if ((ret = ff_get_buffer(avctx, s->frame1, 0)) < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, s->frame2, 0)) < 0)
        return ret;

    clear_plane(avctx, s->frame2);
    clear_plane(avctx, s->frame1);

    return 0;
}

static int decode_fint(AVCodecContext *avctx,
                       const AVPacket *avpkt, unsigned size)
{
    RASCContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    unsigned w, h, fmt;
    int ret;

    if (bytestream2_peek_le32(gb) != 0x65) {
        if (!s->frame2->data[0] || !s->frame1->data[0])
            return AVERROR_INVALIDDATA;

        clear_plane(avctx, s->frame2);
        clear_plane(avctx, s->frame1);
        return 0;
    }
    if (bytestream2_get_bytes_left(gb) < 72)
        return AVERROR_INVALIDDATA;

    bytestream2_skip(gb, 8);
    w = bytestream2_get_le32(gb);
    h = bytestream2_get_le32(gb);
    bytestream2_skip(gb, 30);
    fmt = bytestream2_get_le16(gb);
    bytestream2_skip(gb, 24);

    switch (fmt) {
    case 8:  s->stride = FFALIGN(w, 4);
             s->bpp    = 1;
             fmt = AV_PIX_FMT_PAL8; break;
    case 16: s->stride = w * 2;
             s->bpp    = 2;
             fmt = AV_PIX_FMT_RGB555LE; break;
    case 32: s->stride = w * 4;
             s->bpp    = 4;
             fmt = AV_PIX_FMT_BGR0; break;
    default: return AVERROR_INVALIDDATA;
    }

    ret = ff_set_dimensions(avctx, w, h);
    if (ret < 0)
        return ret;
    avctx->width  = w;
    avctx->height = h;
    avctx->pix_fmt = fmt;

    ret = init_frames(avctx);
    if (ret < 0)
        return ret;

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        uint32_t *pal = (uint32_t *)s->frame2->data[1];

        for (int i = 0; i < 256; i++)
            pal[i] = bytestream2_get_le32(gb) | 0xFF000000u;
    }

    return 0;
}

static int decode_zlib(AVCodecContext *avctx, const AVPacket *avpkt,
                       unsigned size, unsigned uncompressed_size)
{
    RASCContext *s = avctx->priv_data;
    z_stream *const zstream = &s->zstream.zstream;
    GetByteContext *gb = &s->gb;
    int zret;

    zret = inflateReset(zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", zret);
        return AVERROR_EXTERNAL;
    }

    av_fast_padded_malloc(&s->delta, &s->delta_size, uncompressed_size);
    if (!s->delta)
        return AVERROR(ENOMEM);

    zstream->next_in  = avpkt->data + bytestream2_tell(gb);
    zstream->avail_in = FFMIN(size, bytestream2_get_bytes_left(gb));

    zstream->next_out  = s->delta;
    zstream->avail_out = s->delta_size;

    zret = inflate(zstream, Z_FINISH);
    if (zret != Z_STREAM_END) {
        av_log(avctx, AV_LOG_ERROR,
               "Inflate failed with return code: %d.\n", zret);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int decode_move(AVCodecContext *avctx,
                       const AVPacket *avpkt, unsigned size)
{
    RASCContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    GetByteContext mc;
    unsigned pos, compression, nb_moves;
    unsigned uncompressed_size;
    int ret;

    pos = bytestream2_tell(gb);
    bytestream2_skip(gb, 8);
    nb_moves = bytestream2_get_le32(gb);
    bytestream2_skip(gb, 8);
    compression = bytestream2_get_le32(gb);

    if (nb_moves > INT32_MAX / 16 || nb_moves > avctx->width * avctx->height)
        return AVERROR_INVALIDDATA;

    uncompressed_size = 16 * nb_moves;

    if (compression == 1) {
        ret = decode_zlib(avctx, avpkt,
                          size - (bytestream2_tell(gb) - pos),
                          uncompressed_size);
        if (ret < 0)
            return ret;
        bytestream2_init(&mc, s->delta, uncompressed_size);
    } else if (compression == 0) {
        bytestream2_init(&mc, avpkt->data + bytestream2_tell(gb),
                         bytestream2_get_bytes_left(gb));
    } else if (compression == 2) {
        avpriv_request_sample(avctx, "compression %d", compression);
        return AVERROR_PATCHWELCOME;
    } else {
        return AVERROR_INVALIDDATA;
    }

    if (bytestream2_get_bytes_left(&mc) < uncompressed_size)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < nb_moves; i++) {
        int type, start_x, start_y, end_x, end_y, mov_x, mov_y;
        uint8_t *e2, *b1, *b2;
        int w, h;

        type = bytestream2_get_le16(&mc);
        start_x = bytestream2_get_le16(&mc);
        start_y = bytestream2_get_le16(&mc);
        end_x = bytestream2_get_le16(&mc);
        end_y = bytestream2_get_le16(&mc);
        mov_x = bytestream2_get_le16(&mc);
        mov_y = bytestream2_get_le16(&mc);
        bytestream2_skip(&mc, 2);

        if (start_x >= avctx->width || start_y >= avctx->height ||
            end_x >= avctx->width || end_y >= avctx->height ||
            mov_x >= avctx->width || mov_y >= avctx->height) {
            continue;
        }

        if (start_x >= end_x || start_y >= end_y)
            continue;

        w = end_x - start_x;
        h = end_y - start_y;

        if (mov_x + w > avctx->width || mov_y + h > avctx->height)
            continue;

        if (!s->frame2->data[0] || !s->frame1->data[0])
            return AVERROR_INVALIDDATA;

        b1 = s->frame1->data[0] + s->frame1->linesize[0] * (start_y + h - 1) + start_x * s->bpp;
        b2 = s->frame2->data[0] + s->frame2->linesize[0] * (start_y + h - 1) + start_x * s->bpp;
        e2 = s->frame2->data[0] + s->frame2->linesize[0] * (mov_y + h - 1) + mov_x * s->bpp;

        if (type == 2) {
            for (int j = 0; j < h; j++) {
                memcpy(b1, b2, w * s->bpp);
                b1 -= s->frame1->linesize[0];
                b2 -= s->frame2->linesize[0];
            }
        } else if (type == 1) {
            for (int j = 0; j < h; j++) {
                memset(b2, 0, w * s->bpp);
                b2 -= s->frame2->linesize[0];
            }
        } else if (type == 0) {
            uint8_t *buffer;

            av_fast_padded_malloc(&s->delta, &s->delta_size, w * h * s->bpp);
            buffer = s->delta;
            if (!buffer)
                return AVERROR(ENOMEM);

            for (int j = 0; j < h; j++) {
                memcpy(buffer + j * w * s->bpp, e2, w * s->bpp);
                e2 -= s->frame2->linesize[0];
            }

            for (int j = 0; j < h; j++) {
                memcpy(b2, buffer + j * w * s->bpp, w * s->bpp);
                b2 -= s->frame2->linesize[0];
            }
        } else {
            return AVERROR_INVALIDDATA;
        }
    }

    bytestream2_skip(gb, size - (bytestream2_tell(gb) - pos));

    return 0;
}

#define NEXT_LINE                        \
    if (cx >= w * s->bpp) {              \
        cx = 0;                          \
        cy--;                            \
        b1 -= s->frame1->linesize[0];    \
        b2 -= s->frame2->linesize[0];    \
    }                                    \
    len--;

static int decode_dlta(AVCodecContext *avctx,
                       const AVPacket *avpkt, unsigned size)
{
    RASCContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    GetByteContext dc;
    unsigned uncompressed_size, pos;
    unsigned x, y, w, h;
    int ret, cx, cy, compression;
    uint8_t *b1, *b2;

    pos = bytestream2_tell(gb);
    bytestream2_skip(gb, 12);
    uncompressed_size = bytestream2_get_le32(gb);
    x = bytestream2_get_le32(gb);
    y = bytestream2_get_le32(gb);
    w = bytestream2_get_le32(gb);
    h = bytestream2_get_le32(gb);

    if (x >= avctx->width || y >= avctx->height ||
        w > avctx->width || h > avctx->height)
        return AVERROR_INVALIDDATA;

    if (x + w > avctx->width || y + h > avctx->height)
        return AVERROR_INVALIDDATA;

    bytestream2_skip(gb, 4);
    compression = bytestream2_get_le32(gb);

    if (compression == 1) {
        if (w * h * s->bpp * 3 < uncompressed_size)
            return AVERROR_INVALIDDATA;
        ret = decode_zlib(avctx, avpkt, size, uncompressed_size);
        if (ret < 0)
            return ret;
        bytestream2_init(&dc, s->delta, uncompressed_size);
    } else if (compression == 0) {
        if (bytestream2_get_bytes_left(gb) < uncompressed_size)
            return AVERROR_INVALIDDATA;
        bytestream2_init(&dc, avpkt->data + bytestream2_tell(gb),
                         uncompressed_size);
    } else if (compression == 2) {
        avpriv_request_sample(avctx, "compression %d", compression);
        return AVERROR_PATCHWELCOME;
    } else {
        return AVERROR_INVALIDDATA;
    }

    if (!s->frame2->data[0] || !s->frame1->data[0])
        return AVERROR_INVALIDDATA;

    b1  = s->frame1->data[0] + s->frame1->linesize[0] * (int)(y + h - 1) + ((int)x) * s->bpp;
    b2  = s->frame2->data[0] + s->frame2->linesize[0] * (int)(y + h - 1) + ((int)x) * s->bpp;
    cx = 0, cy = h;
    while (bytestream2_get_bytes_left(&dc) > 0) {
        int type = bytestream2_get_byte(&dc);
        int len = bytestream2_get_byte(&dc);
        unsigned fill;

        switch (type) {
        case 1:
            while (len > 0 && cy > 0) {
                cx++;
                NEXT_LINE
            }
            break;
        case 2:
            while (len > 0 && cy > 0) {
                int v0 = b1[cx];
                int v1 = b2[cx];

                b2[cx] = v0;
                b1[cx] = v1;
                cx++;
                NEXT_LINE
            }
            break;
        case 3:
            while (len > 0 && cy > 0) {
                fill = bytestream2_get_byte(&dc);
                b1[cx] = b2[cx];
                b2[cx] = fill;
                cx++;
                NEXT_LINE
            }
            break;
        case 4:
            fill = bytestream2_get_byte(&dc);
            while (len > 0 && cy > 0) {
                AV_WL32(b1 + cx, AV_RL32(b2 + cx));
                AV_WL32(b2 + cx, fill);
                cx++;
                NEXT_LINE
            }
            break;
        case 7:
            fill = bytestream2_get_le32(&dc);
            while (len > 0 && cy > 0) {
                AV_WL32(b1 + cx, AV_RL32(b2 + cx));
                AV_WL32(b2 + cx, fill);
                cx += 4;
                NEXT_LINE
            }
            break;
        case 10:
            while (len > 0 && cy > 0) {
                cx += 4;
                NEXT_LINE
            }
            break;
        case 12:
            while (len > 0 && cy > 0) {
                unsigned v0, v1;

                v0 = AV_RL32(b2 + cx);
                v1 = AV_RL32(b1 + cx);
                AV_WL32(b2 + cx, v1);
                AV_WL32(b1 + cx, v0);
                cx += 4;
                NEXT_LINE
            }
            break;
        case 13:
            while (len > 0 && cy > 0) {
                fill = bytestream2_get_le32(&dc);
                AV_WL32(b1 + cx, AV_RL32(b2 + cx));
                AV_WL32(b2 + cx, fill);
                cx += 4;
                NEXT_LINE
            }
            break;
        default:
            avpriv_request_sample(avctx, "runlen %d", type);
            return AVERROR_INVALIDDATA;
        }
    }

    bytestream2_skip(gb, size - (bytestream2_tell(gb) - pos));

    return 0;
}

static int decode_kfrm(AVCodecContext *avctx,
                       const AVPacket *avpkt, unsigned size)
{
    RASCContext *s = avctx->priv_data;
    z_stream *const zstream = &s->zstream.zstream;
    GetByteContext *gb = &s->gb;
    uint8_t *dst;
    unsigned pos;
    int zret, ret;

    pos = bytestream2_tell(gb);
    if (bytestream2_peek_le32(gb) == 0x65) {
        ret = decode_fint(avctx, avpkt, size);
        if (ret < 0)
            return ret;
    }

    if (!s->frame2->data[0])
        return AVERROR_INVALIDDATA;

    zret = inflateReset(zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", zret);
        return AVERROR_EXTERNAL;
    }

    zstream->next_in  = avpkt->data + bytestream2_tell(gb);
    zstream->avail_in = bytestream2_get_bytes_left(gb);

    dst = s->frame2->data[0] + (avctx->height - 1) * s->frame2->linesize[0];
    for (int i = 0; i < avctx->height; i++) {
        zstream->next_out  = dst;
        zstream->avail_out = s->stride;

        zret = inflate(zstream, Z_SYNC_FLUSH);
        if (zret != Z_OK && zret != Z_STREAM_END) {
            av_log(avctx, AV_LOG_ERROR,
                   "Inflate failed with return code: %d.\n", zret);
            return AVERROR_INVALIDDATA;
        }

        dst -= s->frame2->linesize[0];
    }

    dst = s->frame1->data[0] + (avctx->height - 1) * s->frame1->linesize[0];
    for (int i = 0; i < avctx->height; i++) {
        zstream->next_out  = dst;
        zstream->avail_out = s->stride;

        zret = inflate(zstream, Z_SYNC_FLUSH);
        if (zret != Z_OK && zret != Z_STREAM_END) {
            av_log(avctx, AV_LOG_ERROR,
                   "Inflate failed with return code: %d.\n", zret);
            return AVERROR_INVALIDDATA;
        }

        dst -= s->frame1->linesize[0];
    }

    bytestream2_skip(gb, size - (bytestream2_tell(gb) - pos));

    return 0;
}

static int decode_mous(AVCodecContext *avctx,
                       const AVPacket *avpkt, unsigned size)
{
    RASCContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    unsigned w, h, pos, uncompressed_size;
    int ret;

    pos = bytestream2_tell(gb);
    bytestream2_skip(gb, 8);
    w = bytestream2_get_le32(gb);
    h = bytestream2_get_le32(gb);
    bytestream2_skip(gb, 12);
    uncompressed_size = bytestream2_get_le32(gb);

    if (w > avctx->width || h > avctx->height)
        return AVERROR_INVALIDDATA;

    if (uncompressed_size != 3 * w * h)
        return AVERROR_INVALIDDATA;

    av_fast_padded_malloc(&s->cursor, &s->cursor_size, uncompressed_size);
    if (!s->cursor)
        return AVERROR(ENOMEM);

    ret = decode_zlib(avctx, avpkt,
                      size - (bytestream2_tell(gb) - pos),
                      uncompressed_size);
    if (ret < 0)
        return ret;
    memcpy(s->cursor, s->delta, uncompressed_size);

    bytestream2_skip(gb, size - (bytestream2_tell(gb) - pos));

    s->cursor_w = w;
    s->cursor_h = h;

    return 0;
}

static int decode_mpos(AVCodecContext *avctx,
                       const AVPacket *avpkt, unsigned size)
{
    RASCContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    unsigned pos;

    pos = bytestream2_tell(gb);
    bytestream2_skip(gb, 8);
    s->cursor_x = bytestream2_get_le32(gb);
    s->cursor_y = bytestream2_get_le32(gb);

    bytestream2_skip(gb, size - (bytestream2_tell(gb) - pos));

    return 0;
}

static void draw_cursor(AVCodecContext *avctx)
{
    RASCContext *s = avctx->priv_data;
    uint8_t *dst, *pal;

    if (!s->cursor)
        return;

    if (s->cursor_x >= avctx->width || s->cursor_y >= avctx->height)
        return;

    if (s->cursor_x + s->cursor_w > avctx->width ||
        s->cursor_y + s->cursor_h > avctx->height)
        return;

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        pal = s->frame->data[1];
        for (int i = 0; i < s->cursor_h; i++) {
            for (int j = 0; j < s->cursor_w; j++) {
                int cr = s->cursor[3 * s->cursor_w * (s->cursor_h - i - 1) + 3 * j + 0];
                int cg = s->cursor[3 * s->cursor_w * (s->cursor_h - i - 1) + 3 * j + 1];
                int cb = s->cursor[3 * s->cursor_w * (s->cursor_h - i - 1) + 3 * j + 2];
                int best = INT_MAX;
                int index = 0;
                int dist;

                if (cr == s->cursor[0] && cg == s->cursor[1] && cb == s->cursor[2])
                    continue;

                dst = s->frame->data[0] + s->frame->linesize[0] * (int)(s->cursor_y + i) + (int)(s->cursor_x + j);
                for (int k = 0; k < 256; k++) {
                    int pr = pal[k * 4 + 0];
                    int pg = pal[k * 4 + 1];
                    int pb = pal[k * 4 + 2];

                    dist = FFABS(cr - pr) + FFABS(cg - pg) + FFABS(cb - pb);
                    if (dist < best) {
                        best = dist;
                        index = k;
                    }
                }
                dst[0] = index;
            }
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_RGB555LE) {
        for (int i = 0; i < s->cursor_h; i++) {
            for (int j = 0; j < s->cursor_w; j++) {
                int cr = s->cursor[3 * s->cursor_w * (s->cursor_h - i - 1) + 3 * j + 0];
                int cg = s->cursor[3 * s->cursor_w * (s->cursor_h - i - 1) + 3 * j + 1];
                int cb = s->cursor[3 * s->cursor_w * (s->cursor_h - i - 1) + 3 * j + 2];

                if (cr == s->cursor[0] && cg == s->cursor[1] && cb == s->cursor[2])
                    continue;

                cr >>= 3; cg >>=3; cb >>= 3;
                dst = s->frame->data[0] + s->frame->linesize[0] * (int)(s->cursor_y + i) + 2 * (s->cursor_x + j);
                AV_WL16(dst, cr | cg << 5 | cb << 10);
            }
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_BGR0) {
        for (int i = 0; i < s->cursor_h; i++) {
            for (int j = 0; j < s->cursor_w; j++) {
                int cr = s->cursor[3 * s->cursor_w * (s->cursor_h - i - 1) + 3 * j + 0];
                int cg = s->cursor[3 * s->cursor_w * (s->cursor_h - i - 1) + 3 * j + 1];
                int cb = s->cursor[3 * s->cursor_w * (s->cursor_h - i - 1) + 3 * j + 2];

                if (cr == s->cursor[0] && cg == s->cursor[1] && cb == s->cursor[2])
                    continue;

                dst = s->frame->data[0] + s->frame->linesize[0] * (int)(s->cursor_y + i) + 4 * (s->cursor_x + j);
                dst[0] = cb;
                dst[1] = cg;
                dst[2] = cr;
            }
        }
    }
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    RASCContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    int ret, intra = 0;

    bytestream2_init(gb, avpkt->data, avpkt->size);

    if (bytestream2_peek_le32(gb) == EMPT)
        return avpkt->size;

    s->frame = frame;

    while (bytestream2_get_bytes_left(gb) > 0) {
        unsigned type, size = 0;

        if (bytestream2_get_bytes_left(gb) < 8)
            return AVERROR_INVALIDDATA;

        type = bytestream2_get_le32(gb);
        if (type == KBND || type == BNDL) {
            intra = type == KBND;
            type = bytestream2_get_le32(gb);
        }

        size = bytestream2_get_le32(gb);
        if (bytestream2_get_bytes_left(gb) < size)
            return AVERROR_INVALIDDATA;

        switch (type) {
        case FINT:
        case INIT:
            ret = decode_fint(avctx, avpkt, size);
            break;
        case KFRM:
            ret = decode_kfrm(avctx, avpkt, size);
            break;
        case DLTA:
            ret = decode_dlta(avctx, avpkt, size);
            break;
        case MOVE:
            ret = decode_move(avctx, avpkt, size);
            break;
        case MOUS:
            ret = decode_mous(avctx, avpkt, size);
            break;
        case MPOS:
            ret = decode_mpos(avctx, avpkt, size);
            break;
        default:
            bytestream2_skip(gb, size);
            ret = 0;
        }

        if (ret < 0)
            return ret;
    }

    if (!s->frame2->data[0] || !s->frame1->data[0])
        return AVERROR_INVALIDDATA;

    if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    copy_plane(avctx, s->frame2, s->frame);
    if (avctx->pix_fmt == AV_PIX_FMT_PAL8)
        memcpy(s->frame->data[1], s->frame2->data[1], 1024);
    if (!s->skip_cursor)
        draw_cursor(avctx);

    if (intra)
        s->frame->flags |= AV_FRAME_FLAG_KEY;
    else
        s->frame->flags &= ~AV_FRAME_FLAG_KEY;
    s->frame->pict_type = intra ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    RASCContext *s = avctx->priv_data;

    s->frame1 = av_frame_alloc();
    s->frame2 = av_frame_alloc();
    if (!s->frame1 || !s->frame2)
        return AVERROR(ENOMEM);

    return ff_inflate_init(&s->zstream, avctx);
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    RASCContext *s = avctx->priv_data;

    av_freep(&s->cursor);
    s->cursor_size = 0;
    av_freep(&s->delta);
    s->delta_size = 0;
    av_frame_free(&s->frame1);
    av_frame_free(&s->frame2);
    ff_inflate_end(&s->zstream);

    return 0;
}

static av_cold void decode_flush(AVCodecContext *avctx)
{
    RASCContext *s = avctx->priv_data;

    clear_plane(avctx, s->frame1);
    clear_plane(avctx, s->frame2);
}

static const AVOption options[] = {
{ "skip_cursor", "skip the cursor", offsetof(RASCContext, skip_cursor), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM },
{ NULL },
};

static const AVClass rasc_decoder_class = {
    .class_name = "rasc decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_rasc_decoder = {
    .p.name           = "rasc",
    CODEC_LONG_NAME("RemotelyAnywhere Screen Capture"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_RASC,
    .priv_data_size   = sizeof(RASCContext),
    .init             = decode_init,
    .close            = decode_close,
    FF_CODEC_DECODE_CB(decode_frame),
    .flush            = decode_flush,
    .p.capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
    .p.priv_class     = &rasc_decoder_class,
};
