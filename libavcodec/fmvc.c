/*
 * FM Screen Capture Codec decoder
 *
 * Copyright (c) 2017 Paul B Mahol
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
#include <stdlib.h>
#include <string.h>

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

#define BLOCK_HEIGHT 112u
#define BLOCK_WIDTH  84u

typedef struct InterBlock {
    int      w, h;
    int      size;
    int      xor;
} InterBlock;

typedef struct FMVCContext {
    GetByteContext  gb;
    PutByteContext  pb;
    uint8_t        *buffer;
    size_t          buffer_size;
    uint8_t        *pbuffer;
    size_t          pbuffer_size;
    int             stride;
    int             bpp;
    int             yb, xb;
    InterBlock     *blocks;
    int             nb_blocks;
} FMVCContext;

static int decode_type2(GetByteContext *gb, PutByteContext *pb)
{
    unsigned repeat = 0, first = 1, opcode = 0;
    int i, len, pos;

    while (bytestream2_get_bytes_left(gb) > 0) {
        GetByteContext gbc;

        while (bytestream2_get_bytes_left(gb) > 0) {
            if (first) {
                first = 0;
                if (bytestream2_peek_byte(gb) > 17) {
                    len = bytestream2_get_byte(gb) - 17;
                    if (len < 4) {
                        do {
                            bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                            --len;
                        } while (len);
                        opcode = bytestream2_peek_byte(gb);
                        continue;
                    } else {
                        do {
                            bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                            --len;
                        } while (len);
                        opcode = bytestream2_peek_byte(gb);
                        if (opcode < 0x10) {
                            bytestream2_skip(gb, 1);
                            pos = - (opcode >> 2) - 4 * bytestream2_get_byte(gb) - 2049;

                            bytestream2_init(&gbc, pb->buffer_start, pb->buffer_end - pb->buffer_start);
                            bytestream2_seek(&gbc, bytestream2_tell_p(pb) + pos, SEEK_SET);

                            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                            len = opcode & 3;
                            if (!len) {
                                repeat = 1;
                            } else {
                                do {
                                    bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                                    --len;
                                } while (len);
                                opcode = bytestream2_peek_byte(gb);
                            }
                            continue;
                        }
                    }
                    repeat = 0;
                }
                repeat = 1;
            }
            if (repeat) {
                repeat = 0;
                opcode = bytestream2_peek_byte(gb);
                if (opcode < 0x10) {
                    bytestream2_skip(gb, 1);
                    if (!opcode) {
                        if (!bytestream2_peek_byte(gb)) {
                            do {
                                bytestream2_skip(gb, 1);
                                opcode += 255;
                            } while (!bytestream2_peek_byte(gb) && bytestream2_get_bytes_left(gb) > 0);
                        }
                        opcode += bytestream2_get_byte(gb) + 15;
                    }
                    bytestream2_put_le32(pb, bytestream2_get_le32(gb));
                    for (i = opcode - 1; i > 0; --i)
                        bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                    opcode = bytestream2_peek_byte(gb);
                    if (opcode < 0x10) {
                        bytestream2_skip(gb, 1);
                        pos = - (opcode >> 2) - 4 * bytestream2_get_byte(gb) - 2049;

                        bytestream2_init(&gbc, pb->buffer_start, pb->buffer_end - pb->buffer_start);
                        bytestream2_seek(&gbc, bytestream2_tell_p(pb) + pos, SEEK_SET);

                        bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                        bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                        bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                        len = opcode & 3;
                        if (!len) {
                            repeat = 1;
                        } else {
                            do {
                                bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                                --len;
                            } while (len);
                            opcode = bytestream2_peek_byte(gb);
                        }
                        continue;
                    }
                }
            }

            if (opcode >= 0x40) {
                bytestream2_skip(gb, 1);
                pos = - ((opcode >> 2) & 7) - 1 - 8 * bytestream2_get_byte(gb);
                len = (opcode >> 5) - 1;

                bytestream2_init(&gbc, pb->buffer_start, pb->buffer_end - pb->buffer_start);
                bytestream2_seek(&gbc, bytestream2_tell_p(pb) + pos, SEEK_SET);

                bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                do {
                    bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                    --len;
                } while (len);

                len = opcode & 3;

                if (!len) {
                    repeat = 1;
                } else {
                    do {
                        bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                        --len;
                    } while (len);
                    opcode = bytestream2_peek_byte(gb);
                }
                continue;
            } else if (opcode < 0x20) {
                break;
            }
            len = opcode & 0x1F;
            bytestream2_skip(gb, 1);
            if (!len) {
                if (!bytestream2_peek_byte(gb)) {
                    do {
                        bytestream2_skip(gb, 1);
                        len += 255;
                    } while (!bytestream2_peek_byte(gb) && bytestream2_get_bytes_left(gb) > 0);
                }
                len += bytestream2_get_byte(gb) + 31;
            }
            i = bytestream2_get_le16(gb);
            pos = - (i >> 2) - 1;

            bytestream2_init(&gbc, pb->buffer_start, pb->buffer_end - pb->buffer_start);
            bytestream2_seek(&gbc, bytestream2_tell_p(pb) + pos, SEEK_SET);

            if (len < 6 || bytestream2_tell_p(pb) - bytestream2_tell(&gbc) < 4) {
                bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                do {
                    bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                    --len;
                } while (len);
            } else {
                bytestream2_put_le32(pb, bytestream2_get_le32(&gbc));
                for (len = len - 2; len; --len)
                    bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
            }
            len = i & 3;
            if (!len) {
                repeat = 1;
            } else {
                do {
                    bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                    --len;
                } while (len);
                opcode = bytestream2_peek_byte(gb);
            }
        }
        bytestream2_skip(gb, 1);
        if (opcode < 0x10) {
            pos = -(opcode >> 2) - 1 - 4 * bytestream2_get_byte(gb);

            bytestream2_init(&gbc, pb->buffer_start, pb->buffer_end - pb->buffer_start);
            bytestream2_seek(&gbc, bytestream2_tell_p(pb) + pos, SEEK_SET);

            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
            len = opcode & 3;
            if (!len) {
                repeat = 1;
            } else {
                do {
                    bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                    --len;
                } while (len);
                opcode = bytestream2_peek_byte(gb);
            }
            continue;
        }
        len = opcode & 7;
        if (!len) {
            if (!bytestream2_peek_byte(gb)) {
                do {
                    bytestream2_skip(gb, 1);
                    len += 255;
                } while (!bytestream2_peek_byte(gb) && bytestream2_get_bytes_left(gb) > 0);
            }
            len += bytestream2_get_byte(gb) + 7;
        }
        i = bytestream2_get_le16(gb);
        pos = bytestream2_tell_p(pb) - 2048 * (opcode & 8);
        pos = pos - (i >> 2);
        if (pos == bytestream2_tell_p(pb))
            break;

        pos = pos - 0x4000;
        bytestream2_init(&gbc, pb->buffer_start, pb->buffer_end - pb->buffer_start);
        bytestream2_seek(&gbc, pos, SEEK_SET);

        if (len < 6 || bytestream2_tell_p(pb) - bytestream2_tell(&gbc) < 4) {
            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
            do {
                bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                --len;
            } while (len);
        } else {
            bytestream2_put_le32(pb, bytestream2_get_le32(&gbc));
            for (len = len - 2; len; --len)
                bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
        }

        len = i & 3;
        if (!len) {
            repeat = 1;
        } else {
            do {
                bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                --len;
            } while (len);
            opcode = bytestream2_peek_byte(gb);
        }
    }

    return 0;
}

static int decode_type1(GetByteContext *gb, PutByteContext *pb)
{
    unsigned opcode = 0, len;
    int high = 0;
    int i, pos;

    while (bytestream2_get_bytes_left(gb) > 0) {
        GetByteContext gbc;

        while (bytestream2_get_bytes_left(gb) > 0) {
            while (bytestream2_get_bytes_left(gb) > 0) {
                opcode = bytestream2_get_byte(gb);
                high = opcode >= 0x20;
                if (high)
                    break;
                if (opcode)
                    break;
                opcode = bytestream2_get_byte(gb);
                if (opcode < 0xF8) {
                    opcode = opcode + 32;
                    break;
                }
                i = opcode - 0xF8;
                if (i) {
                    len = 256;
                    do {
                        len *= 2;
                        --i;
                    } while (i);
                } else {
                    len = 280;
                }
                do {
                    bytestream2_put_le32(pb, bytestream2_get_le32(gb));
                    bytestream2_put_le32(pb, bytestream2_get_le32(gb));
                    len -= 8;
                } while (len && bytestream2_get_bytes_left(gb) > 0);
            }

            if (!high) {
                do {
                    bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                    --opcode;
                } while (opcode && bytestream2_get_bytes_left(gb) > 0);

                while (bytestream2_get_bytes_left(gb) > 0) {
                    GetByteContext gbc;

                    opcode = bytestream2_get_byte(gb);
                    if (opcode >= 0x20)
                        break;
                    bytestream2_init(&gbc, pb->buffer_start, pb->buffer_end - pb->buffer_start);

                    pos = -(opcode | 32 * bytestream2_get_byte(gb)) - 1;
                    bytestream2_seek(&gbc, bytestream2_tell_p(pb) + pos, SEEK_SET);
                    bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                    bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                    bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                    bytestream2_put_byte(pb, bytestream2_get_byte(gb));
                }
            }
            high = 0;
            if (opcode < 0x40)
                break;
            bytestream2_init(&gbc, pb->buffer_start, pb->buffer_end - pb->buffer_start);
            pos = (-((opcode & 0x1F) | 32 * bytestream2_get_byte(gb)) - 1);
            bytestream2_seek(&gbc, bytestream2_tell_p(pb) + pos, SEEK_SET);
            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
            len = (opcode >> 5) - 1;
            do {
                bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
                --len;
            } while (len && bytestream2_get_bytes_left(&gbc) > 0);
        }
        len = opcode & 0x1F;
        if (!len) {
            if (!bytestream2_peek_byte(gb)) {
                do {
                    bytestream2_skip(gb, 1);
                    len += 255;
                } while (!bytestream2_peek_byte(gb) && bytestream2_get_bytes_left(gb) > 0);
            }
            len += bytestream2_get_byte(gb) + 31;
        }
        pos = -bytestream2_get_byte(gb);
        bytestream2_init(&gbc, pb->buffer_start, pb->buffer_end - pb->buffer_start);
        bytestream2_seek(&gbc, bytestream2_tell_p(pb) + pos - (bytestream2_get_byte(gb) << 8), SEEK_SET);
        if (bytestream2_tell_p(pb) == bytestream2_tell(&gbc))
            break;
        if (len < 5 || bytestream2_tell_p(pb) - bytestream2_tell(&gbc) < 4) {
            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
        } else {
            bytestream2_put_le32(pb, bytestream2_get_le32(&gbc));
            len--;
        }
        do {
            bytestream2_put_byte(pb, bytestream2_get_byte(&gbc));
            len--;
        } while (len && bytestream2_get_bytes_left(&gbc) > 0);
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    FMVCContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    PutByteContext *pb = &s->pb;
    AVFrame *frame = data;
    int ret, y, x;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    bytestream2_init(gb, avpkt->data, avpkt->size);
    bytestream2_skip(gb, 2);

    frame->key_frame = !!bytestream2_get_le16(gb);
    frame->pict_type = frame->key_frame ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    if (frame->key_frame) {
        const uint8_t *src;
        int type, size;
        uint8_t *dst;

        type = bytestream2_get_le16(gb);
        size = bytestream2_get_le16(gb);
        if (size > bytestream2_get_bytes_left(gb))
            return AVERROR_INVALIDDATA;

        bytestream2_init_writer(pb, s->buffer, s->buffer_size);
        if (type == 1) {
            decode_type1(gb, pb);
        } else if (type == 2){
            decode_type2(gb, pb);
        } else {
            avpriv_report_missing_feature(avctx, "compression %d", type);
            return AVERROR_PATCHWELCOME;
        }

        src = s->buffer;
        dst = frame->data[0] + (avctx->height - 1) * frame->linesize[0];
        for (y = 0; y < avctx->height; y++) {
            memcpy(dst, src, avctx->width * s->bpp);
            dst -= frame->linesize[0];
            src += s->stride * 4;
        }
    } else {
        int block, nb_blocks, type, k, l;
        uint8_t *ssrc, *ddst;
        const uint32_t *src;
        uint32_t *dst;

        for (block = 0; block < s->nb_blocks; block++)
            s->blocks[block].xor = 0;

        nb_blocks = bytestream2_get_le16(gb);
        if (nb_blocks > s->nb_blocks)
            return AVERROR_INVALIDDATA;

        bytestream2_init_writer(pb, s->pbuffer, s->pbuffer_size);

        type = bytestream2_get_le16(gb);
        for (block = 0; block < nb_blocks; block++) {
            int size, offset, start = 0;

            offset = bytestream2_get_le16(gb);
            if (offset >= s->nb_blocks)
                return AVERROR_INVALIDDATA;

            size = bytestream2_get_le16(gb);
            if (size > bytestream2_get_bytes_left(gb))
                return AVERROR_INVALIDDATA;

            start = bytestream2_tell_p(pb);
            if (type == 1) {
                decode_type1(gb, pb);
            } else if (type == 2){
                decode_type2(gb, pb);
            } else {
                avpriv_report_missing_feature(avctx, "compression %d", type);
                return AVERROR_PATCHWELCOME;
            }

            if (s->blocks[offset].size * 4 != bytestream2_tell_p(pb) - start)
                return AVERROR_INVALIDDATA;

            s->blocks[offset].xor = 1;
        }

        src = (const uint32_t *)s->pbuffer;
        dst = (uint32_t *)s->buffer;

        for (block = 0, y = 0; y < s->yb; y++) {
            int block_h = s->blocks[block].h;
            uint32_t *rect = dst;

            for (x = 0; x < s->xb; x++) {
                int block_w = s->blocks[block].w;
                uint32_t *row = dst;

                block_h = s->blocks[block].h;
                if (s->blocks[block].xor) {
                    for (k = 0; k < block_h; k++) {
                        uint32_t *column = dst;
                        for (l = 0; l < block_w; l++) {
                            *dst++ ^= *src++;
                        }
                        dst = &column[s->stride];
                    }
                }
                dst = &row[block_w];
                ++block;
            }
            dst = &rect[block_h * s->stride];
        }

        ssrc = s->buffer;
        ddst = frame->data[0] + (avctx->height - 1) * frame->linesize[0];
        for (y = 0; y < avctx->height; y++) {
            memcpy(ddst, ssrc, avctx->width * s->bpp);
            ddst -= frame->linesize[0];
            ssrc += s->stride * 4;
        }
    }

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    FMVCContext *s = avctx->priv_data;
    int i, j, m, block = 0, h = BLOCK_HEIGHT, w = BLOCK_WIDTH;

    switch (avctx->bits_per_coded_sample) {
    case 16: avctx->pix_fmt = AV_PIX_FMT_RGB555; break;
    case 24: avctx->pix_fmt = AV_PIX_FMT_BGR24;  break;
    case 32: avctx->pix_fmt = AV_PIX_FMT_BGRA;   break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported bitdepth %i\n", avctx->bits_per_coded_sample);
        return AVERROR_INVALIDDATA;
    }

    s->stride = (avctx->width * avctx->bits_per_coded_sample + 31) / 32;
    s->xb = s->stride / BLOCK_WIDTH;
    m = s->stride % BLOCK_WIDTH;
    if (m) {
        if (m < 37) {
            w = m + BLOCK_WIDTH;
        } else {
            w = m;
            s->xb++;
        }
    }

    s->yb = avctx->height / BLOCK_HEIGHT;
    m = avctx->height % BLOCK_HEIGHT;
    if (m) {
        if (m < 49) {
            h = m + BLOCK_HEIGHT;
        } else {
            h = m;
            s->yb++;
        }
    }

    s->nb_blocks = s->xb * s->yb;
    if (!s->nb_blocks)
        return AVERROR_INVALIDDATA;

    s->blocks = av_calloc(s->nb_blocks, sizeof(*s->blocks));
    if (!s->blocks)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->yb; i++) {
        for (j = 0; j < s->xb; j++) {
            if (i != (s->yb - 1) || j != (s->xb - 1)) {
                if (i == s->yb - 1) {
                    s->blocks[block].w = BLOCK_WIDTH;
                    s->blocks[block].h = h;
                    s->blocks[block].size = BLOCK_WIDTH * h;
                } else if (j == s->xb - 1) {
                    s->blocks[block].w = w;
                    s->blocks[block].h = BLOCK_HEIGHT;
                    s->blocks[block].size = BLOCK_HEIGHT * w;
                } else {
                    s->blocks[block].w = BLOCK_WIDTH;
                    s->blocks[block].h = BLOCK_HEIGHT;
                    s->blocks[block].size = BLOCK_WIDTH * BLOCK_HEIGHT;
                }
            } else {
                s->blocks[block].w = w;
                s->blocks[block].h = h;
                s->blocks[block].size = w * h;
            }
            block++;
        }
    }

    s->bpp = avctx->bits_per_coded_sample >> 3;
    s->buffer_size = avctx->width * avctx->height * 4;
    s->pbuffer_size = avctx->width * avctx->height * 4;
    s->buffer = av_mallocz(s->buffer_size);
    s->pbuffer = av_mallocz(s->pbuffer_size);
    if (!s->buffer || !s->pbuffer)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    FMVCContext *s = avctx->priv_data;

    av_freep(&s->buffer);
    av_freep(&s->pbuffer);
    av_freep(&s->blocks);

    return 0;
}

AVCodec ff_fmvc_decoder = {
    .name             = "fmvc",
    .long_name        = NULL_IF_CONFIG_SMALL("FM Screen Capture Codec"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_FMVC,
    .priv_data_size   = sizeof(FMVCContext),
    .init             = decode_init,
    .close            = decode_close,
    .decode           = decode_frame,
    .capabilities     = AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE |
                        FF_CODEC_CAP_INIT_CLEANUP,
};
