/*
 * Argonaut Games Video decoder
 * Copyright (c) 2020 Paul B Mahol
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

#include <string.h>

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

typedef struct ArgoContext {
    GetByteContext gb;

    int bpp;
    int key;
    int mv0[128][2];
    int mv1[16][2];
    uint32_t pal[256];
    AVFrame *frame;
} ArgoContext;

static int decode_pal8(AVCodecContext *avctx, uint32_t *pal)
{
    ArgoContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    int start, count;

    start = bytestream2_get_le16(gb);
    count = bytestream2_get_le16(gb);

    if (start + count > 256)
        return AVERROR_INVALIDDATA;

    if (bytestream2_get_bytes_left(gb) < 3 * count)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < count; i++)
        pal[start + i] = (0xFFU << 24) | bytestream2_get_be24u(gb);

    return 0;
}

static int decode_avcf(AVCodecContext *avctx, AVFrame *frame)
{
    ArgoContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    const int l = frame->linesize[0];
    const uint8_t *map = gb->buffer;
    uint8_t *dst = frame->data[0];

    if (bytestream2_get_bytes_left(gb) < 1024 + (frame->width / 2) * (frame->height / 2))
        return AVERROR_INVALIDDATA;

    bytestream2_skipu(gb, 1024);
    for (int y = 0; y < frame->height; y += 2) {
        for (int x = 0; x < frame->width; x += 2) {
            int index = bytestream2_get_byteu(gb);
            const uint8_t *block = map + index * 4;

            dst[x+0]   = block[0];
            dst[x+1]   = block[1];
            dst[x+l]   = block[2];
            dst[x+l+1] = block[3];
        }

        dst += frame->linesize[0] * 2;
    }

    return 0;
}

static int decode_alcd(AVCodecContext *avctx, AVFrame *frame)
{
    ArgoContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    GetByteContext sb;
    const int l = frame->linesize[0];
    const uint8_t *map = gb->buffer;
    uint8_t *dst = frame->data[0];
    uint8_t codes = 0;
    int count = 0;

    if (bytestream2_get_bytes_left(gb) < 1024 + (((frame->width / 2) * (frame->height / 2) + 7) >> 3))
        return AVERROR_INVALIDDATA;

    bytestream2_skipu(gb, 1024);
    sb = *gb;
    bytestream2_skipu(gb, ((frame->width / 2) * (frame->height / 2) + 7) >> 3);

    for (int y = 0; y < frame->height; y += 2) {
        for (int x = 0; x < frame->width; x += 2) {
            const uint8_t *block;
            int index;

            if (count == 0) {
                codes = bytestream2_get_byteu(&sb);
                count = 8;
            }

            if (codes & 0x80) {
                index = bytestream2_get_byte(gb);
                block = map + index * 4;

                dst[x+0]   = block[0];
                dst[x+1]   = block[1];
                dst[x+l]   = block[2];
                dst[x+l+1] = block[3];
            }

            codes <<= 1;
            count--;
        }

        dst += frame->linesize[0] * 2;
    }

    return 0;
}

static int decode_mad1(AVCodecContext *avctx, AVFrame *frame)
{
    ArgoContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    const int w = frame->width;
    const int h = frame->height;
    const int l = frame->linesize[0];

    while (bytestream2_get_bytes_left(gb) > 0) {
        int size, type, pos, dy;
        uint8_t *dst;

        type = bytestream2_get_byte(gb);
        if (type == 0xFF)
            break;

        switch (type) {
        case 8:
            dst = frame->data[0];
            for (int y = 0; y < h; y += 8) {
                for (int x = 0; x < w; x += 8) {
                    int fill = bytestream2_get_byte(gb);
                    uint8_t *ddst = dst + x;

                    for (int by = 0; by < 8; by++) {
                        memset(ddst, fill, 8);
                        ddst += l;
                    }
                }

                dst += 8 * l;
            }
            break;
        case 7:
            while (bytestream2_get_bytes_left(gb) > 0) {
                int bsize = bytestream2_get_byte(gb);
                uint8_t *src;
                int count;

                if (!bsize)
                    break;

                count = bytestream2_get_be16(gb);
                while (count > 0) {
                    int mvx, mvy, a, b, c, mx, my;
                    int bsize_w, bsize_h;

                    bsize_w = bsize_h = bsize;
                    if (bytestream2_get_bytes_left(gb) < 4)
                        return AVERROR_INVALIDDATA;
                    mvx = bytestream2_get_byte(gb) * bsize;
                    mvy = bytestream2_get_byte(gb) * bsize;
                    a = bytestream2_get_byte(gb);
                    b = bytestream2_get_byte(gb);
                    c = ((a & 0x3F) << 8) + b;
                    mx = mvx + (c  & 0x7F) - 64;
                    my = mvy + (c >>    7) - 64;

                    if (mvy < 0 || mvy >= h)
                        return AVERROR_INVALIDDATA;

                    if (mvx < 0 || mvx >= w)
                        return AVERROR_INVALIDDATA;

                    if (my < 0 || my >= h)
                        return AVERROR_INVALIDDATA;

                    if (mx < 0 || mx >= w)
                        return AVERROR_INVALIDDATA;

                    dst = frame->data[0] + mvx + l * mvy;
                    src = frame->data[0] + mx  + l * my;

                    bsize_w = FFMIN3(bsize_w, w - mvx, w - mx);
                    bsize_h = FFMIN3(bsize_h, h - mvy, h - my);

                    if (mvy >= my && (mvy != my || mvx >= mx)) {
                        src += (bsize_h - 1) * l;
                        dst += (bsize_h - 1) * l;
                        for (int by = 0; by < bsize_h; by++) {
                            memmove(dst, src, bsize_w);
                            src -= l;
                            dst -= l;
                        }
                    } else {
                        for (int by = 0; by < bsize_h; by++) {
                            memmove(dst, src, bsize_w);
                            src += l;
                            dst += l;
                        }
                    }

                    count--;
                }
            }
            break;
        case 6:
            dst = frame->data[0];
            if (bytestream2_get_bytes_left(gb) < w * h)
                return AVERROR_INVALIDDATA;
            for (int y = 0; y < h; y++) {
                bytestream2_get_bufferu(gb, dst, w);
                dst += l;
            }
            break;
        case 5:
            dst = frame->data[0];
            for (int y = 0; y < h; y += 2) {
                for (int x = 0; x < w; x += 2) {
                    int fill = bytestream2_get_byte(gb);
                    uint8_t *ddst = dst + x;

                    fill = (fill << 8) | fill;
                    for (int by = 0; by < 2; by++) {
                            AV_WN16(ddst, fill);

                        ddst += l;
                    }
                }

                dst += 2 * l;
            }
            break;
        case 3:
            size = bytestream2_get_le16(gb);
            if (size > 0) {
                int x = bytestream2_get_byte(gb) * 4;
                int y = bytestream2_get_byte(gb) * 4;
                int count = bytestream2_get_byte(gb);
                int fill = bytestream2_get_byte(gb);

                av_log(avctx, AV_LOG_DEBUG, "%d %d %d %d\n", x, y, count, fill);
                for (int i = 0; i < count; i++)
                    ;
                return AVERROR_PATCHWELCOME;
            }
            break;
        case 2:
            dst = frame->data[0];
            pos = 0;
            dy  = 0;
            while (bytestream2_get_bytes_left(gb) > 0) {
                int count = bytestream2_get_byteu(gb);
                int skip = count & 0x3F;

                count = count >> 6;
                if (skip == 0x3F) {
                    pos += 0x3E;
                    while (pos >= w) {
                        pos -= w;
                        dst += l;
                        dy++;
                        if (dy >= h)
                            return 0;
                    }
                } else {
                    pos += skip;
                    while (pos >= w) {
                        pos -= w;
                        dst += l;
                        dy++;
                        if (dy >= h)
                            return 0;
                    }
                    while (count >= 0) {
                        int bits = bytestream2_get_byte(gb);

                        for (int i = 0; i < 4; i++) {
                            switch (bits & 3) {
                            case 0:
                                break;
                            case 1:
                                if (dy < 1 && !pos)
                                    return AVERROR_INVALIDDATA;
                                else
                                    dst[pos] = pos ? dst[pos - 1] : dst[-l + w - 1];
                                break;
                            case 2:
                                if (dy < 1)
                                    return AVERROR_INVALIDDATA;
                                dst[pos] = dst[pos - l];
                                break;
                            case 3:
                                dst[pos] = bytestream2_get_byte(gb);
                                break;
                            }

                            pos++;
                            if (pos >= w) {
                                pos -= w;
                                dst += l;
                                dy++;
                                if (dy >= h)
                                    return 0;
                            }
                            bits >>= 2;
                        }
                        count--;
                    }
                }
            }
            break;
        default:
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static int decode_mad1_24(AVCodecContext *avctx, AVFrame *frame)
{
    ArgoContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    const int w = frame->width;
    const int h = frame->height;
    const int l = frame->linesize[0] / 4;

    while (bytestream2_get_bytes_left(gb) > 0) {
        int osize, type, pos, dy, di, bcode, value, v14;
        const uint8_t *bits;
        uint32_t *dst;

        type = bytestream2_get_byte(gb);
        if (type == 0xFF)
            return 0;

        switch (type) {
        case 8:
            dst = (uint32_t *)frame->data[0];
            for (int y = 0; y + 12 <= h; y += 12) {
                for (int x = 0; x + 12 <= w; x += 12) {
                    int fill = bytestream2_get_be24(gb);
                    uint32_t *dstp = dst + x;

                    for (int by = 0; by < 12; by++) {
                        for (int bx = 0; bx < 12; bx++)
                            dstp[bx] = fill;

                        dstp += l;
                    }
                }

                dst += 12 * l;
            }
            break;
        case 7:
            while (bytestream2_get_bytes_left(gb) > 0) {
                int bsize = bytestream2_get_byte(gb);
                uint32_t *src;
                int count;

                if (!bsize)
                    break;

                count = bytestream2_get_be16(gb);
                while (count > 0) {
                    int mvx, mvy, a, b, c, mx, my;
                    int bsize_w, bsize_h;

                    bsize_w = bsize_h = bsize;
                    if (bytestream2_get_bytes_left(gb) < 4)
                        return AVERROR_INVALIDDATA;
                    mvx = bytestream2_get_byte(gb) * bsize;
                    mvy = bytestream2_get_byte(gb) * bsize;
                    a = bytestream2_get_byte(gb);
                    b = bytestream2_get_byte(gb);
                    c = ((a & 0x3F) << 8) + b;
                    mx = mvx + (c  & 0x7F) - 64;
                    my = mvy + (c >>    7) - 64;

                    if (mvy < 0 || mvy >= h)
                        return AVERROR_INVALIDDATA;

                    if (mvx < 0 || mvx >= w)
                        return AVERROR_INVALIDDATA;

                    if (my < 0 || my >= h)
                        return AVERROR_INVALIDDATA;

                    if (mx < 0 || mx >= w)
                        return AVERROR_INVALIDDATA;

                    dst = (uint32_t *)frame->data[0] + mvx + l * mvy;
                    src = (uint32_t *)frame->data[0] + mx  + l * my;

                    bsize_w = FFMIN3(bsize_w, w - mvx, w - mx);
                    bsize_h = FFMIN3(bsize_h, h - mvy, h - my);

                    if (mvy >= my && (mvy != my || mvx >= mx)) {
                        src += (bsize_h - 1) * l;
                        dst += (bsize_h - 1) * l;
                        for (int by = 0; by < bsize_h; by++) {
                            memmove(dst, src, bsize_w * 4);
                            src -= l;
                            dst -= l;
                        }
                    } else {
                        for (int by = 0; by < bsize_h; by++) {
                            memmove(dst, src, bsize_w * 4);
                            src += l;
                            dst += l;
                        }
                    }

                    count--;
                }
            }
            break;
        case 12:
            osize = ((h + 3) / 4) * ((w + 3) / 4) + 7;
            bits = gb->buffer;
            di   = 0;
            bcode = v14 = 0;
            if (bytestream2_get_bytes_left(gb) < osize >> 3)
                return AVERROR_INVALIDDATA;
            bytestream2_skip(gb, osize >> 3);
            for (int x = 0; x < w; x += 4) {
                for (int y = 0; y < h; y += 4) {
                    int astate = 0;

                    if (bits[di >> 3] & (1 << (di & 7))) {
                        int codes = bytestream2_get_byte(gb);

                        for (int count = 0; count < 4; count++) {
                            uint32_t *src = (uint32_t *)frame->data[0];
                            size_t src_size = l * (h - 1) + (w - 1);
                            int nv, v, code = codes & 3;

                            pos = x;
                            dy  = y + count;
                            dst = (uint32_t *)frame->data[0] + pos + dy * l;
                            if (code & 1)
                                bcode = bytestream2_get_byte(gb);
                            if (code == 3) {
                                for (int j = 0; j < 4; j++) {
                                    switch (bcode & 3) {
                                    case 0:
                                        break;
                                    case 1:
                                        if (dy < 1 && !pos)
                                            return AVERROR_INVALIDDATA;
                                        dst[0] = dst[-1];
                                        break;
                                    case 2:
                                        if (dy < 1)
                                            return AVERROR_INVALIDDATA;
                                        dst[0] = dst[-l];
                                        break;
                                    case 3:
                                        if (astate) {
                                            nv = value >> 4;
                                        } else {
                                            value = bytestream2_get_byte(gb);
                                            nv = value & 0xF;
                                        }
                                        astate ^= 1;
                                        dst[0] = src[av_clip(l * (dy + s->mv1[nv][1]) + pos +
                                                             s->mv1[nv][0], 0, src_size)];
                                        break;
                                    }

                                    bcode >>= 2;
                                    dst++;
                                    pos++;
                                }
                            } else if (code) {
                                if (code == 1)
                                    v14 = bcode;
                                else
                                    bcode = v14;
                                for (int j = 0; j < 4; j++) {
                                    switch (bcode & 3) {
                                    case 0:
                                        break;
                                    case 1:
                                        if (dy < 1 && !pos)
                                            return AVERROR_INVALIDDATA;
                                        dst[0] = dst[-1];
                                        break;
                                    case 2:
                                        if (dy < 1)
                                            return AVERROR_INVALIDDATA;
                                        dst[0] = dst[-l];
                                        break;
                                    case 3:
                                        v = bytestream2_get_byte(gb);
                                        if (v < 128) {
                                            dst[0] = src[av_clip(l * (dy + s->mv0[v][1]) + pos +
                                                                 s->mv0[v][0], 0, src_size)];
                                        } else {
                                            dst[0] = ((v & 0x7F) << 17) | bytestream2_get_be16(gb);
                                        }
                                        break;
                                    }

                                    bcode >>= 2;
                                    dst++;
                                    pos++;
                                }
                            }

                            codes >>= 2;
                        }
                    }

                    di++;
                }
            }
            break;
        default:
            return AVERROR_INVALIDDATA;
        }
    }

    return AVERROR_INVALIDDATA;
}

static int decode_rle(AVCodecContext *avctx, AVFrame *frame)
{
    ArgoContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    const int w = frame->width;
    const int h = frame->height;
    const int l = frame->linesize[0];
    uint8_t *dst = frame->data[0];
    int pos = 0, y = 0;

    while (bytestream2_get_bytes_left(gb) > 0) {
        int count = bytestream2_get_byte(gb);
        int pixel = bytestream2_get_byte(gb);

        if (!count) {
            pos += pixel;
            while (pos >= w) {
                pos -= w;
                y++;
                if (y >= h)
                    return 0;
            }
        } else {
            while (count > 0) {
                dst[pos + y * l] = pixel;
                count--;
                pos++;
                if (pos >= w) {
                    pos = 0;
                    y++;
                    if (y >= h)
                        return 0;
                }
            }
        }
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                        int *got_frame, AVPacket *avpkt)
{
    ArgoContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    AVFrame *frame = s->frame;
    uint32_t chunk;
    int ret;

    if (avpkt->size < 4)
        return AVERROR_INVALIDDATA;

    bytestream2_init(gb, avpkt->data, avpkt->size);

    if ((ret = ff_reget_buffer(avctx, frame, 0)) < 0)
        return ret;

    chunk = bytestream2_get_be32(gb);
    switch (chunk) {
    case MKBETAG('P', 'A', 'L', '8'):
        for (int y = 0; y < frame->height; y++)
            memset(frame->data[0] + y * frame->linesize[0], 0, frame->width * s->bpp);
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8)
            memset(frame->data[1], 0, AVPALETTE_SIZE);
        return decode_pal8(avctx, s->pal);
    case MKBETAG('M', 'A', 'D', '1'):
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8)
            ret = decode_mad1(avctx, frame);
        else
            ret = decode_mad1_24(avctx, frame);
        break;
    case MKBETAG('A', 'V', 'C', 'F'):
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
            s->key = 1;
            ret = decode_avcf(avctx, frame);
            break;
        }
    case MKBETAG('A', 'L', 'C', 'D'):
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
            s->key = 0;
            ret = decode_alcd(avctx, frame);
            break;
        }
    case MKBETAG('R', 'L', 'E', 'F'):
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
            s->key = 1;
            ret = decode_rle(avctx, frame);
            break;
        }
    case MKBETAG('R', 'L', 'E', 'D'):
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
            s->key = 0;
            ret = decode_rle(avctx, frame);
            break;
        }
    default:
        av_log(avctx, AV_LOG_DEBUG, "unknown chunk 0x%X\n", chunk);
        break;
    }

    if (ret < 0)
        return ret;

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8)
        memcpy(frame->data[1], s->pal, AVPALETTE_SIZE);

    if ((ret = av_frame_ref(rframe, s->frame)) < 0)
        return ret;

    frame->pict_type = s->key ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;
    if (s->key)
        frame->flags |= AV_FRAME_FLAG_KEY;
    else
        frame->flags &= ~AV_FRAME_FLAG_KEY;
    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    ArgoContext *s = avctx->priv_data;

    switch (avctx->bits_per_coded_sample) {
    case  8: s->bpp = 1;
             avctx->pix_fmt = AV_PIX_FMT_PAL8; break;
    case 24: s->bpp = 4;
             avctx->pix_fmt = AV_PIX_FMT_BGR0; break;
    default: avpriv_request_sample(s, "depth == %u", avctx->bits_per_coded_sample);
             return AVERROR_PATCHWELCOME;
    }

    if (avctx->width % 2 || avctx->height % 2) {
        avpriv_request_sample(s, "Odd dimensions\n");
        return AVERROR_PATCHWELCOME;
    }

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    for (int n = 0, i = -4; i < 4; i++) {
        for (int j = -14; j < 2; j++) {
            s->mv0[n][0] = j;
            s->mv0[n++][1] = i;
        }
    }

    for (int n = 0, i = -5; i <= 1; i += 2) {
        int j = -5;

        while (j <= 1) {
            s->mv1[n][0] = j;
            s->mv1[n++][1] = i;
            j += 2;
        }
    }

    return 0;
}

static void decode_flush(AVCodecContext *avctx)
{
    ArgoContext *s = avctx->priv_data;

    av_frame_unref(s->frame);
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    ArgoContext *s = avctx->priv_data;

    av_frame_free(&s->frame);

    return 0;
}

const FFCodec ff_argo_decoder = {
    .p.name         = "argo",
    CODEC_LONG_NAME("Argonaut Games Video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_ARGO,
    .priv_data_size = sizeof(ArgoContext),
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .flush          = decode_flush,
    .close          = decode_close,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
