/*
 * NotchLC decoder
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BITSTREAM_READER_LE
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "internal.h"
#include "lzf.h"
#include "thread.h"

typedef struct NotchLCContext {
    unsigned compressed_size;
    unsigned format;

    uint8_t *uncompressed_buffer;
    unsigned uncompressed_size;

    uint8_t *lzf_buffer;
    int64_t lzf_size;

    unsigned texture_size_x;
    unsigned texture_size_y;
    unsigned y_data_row_offsets;
    unsigned uv_offset_data_offset;
    unsigned y_control_data_offset;
    unsigned a_control_word_offset;
    unsigned y_data_offset;
    unsigned uv_data_offset;
    unsigned y_data_size;
    unsigned a_data_offset;
    unsigned uv_count_offset;
    unsigned a_count_size;
    unsigned data_end;

    GetByteContext gb;
    PutByteContext pb;
} NotchLCContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = AV_PIX_FMT_YUVA444P12;
    avctx->color_range = AVCOL_RANGE_JPEG;
    avctx->colorspace = AVCOL_SPC_RGB;
    avctx->color_primaries = AVCOL_PRI_BT709;
    avctx->color_trc = AVCOL_TRC_IEC61966_2_1;

    return 0;
}

#define HISTORY_SIZE (64 * 1024)

static int lz4_decompress(AVCodecContext *avctx,
                          GetByteContext *gb,
                          PutByteContext *pb)
{
    unsigned reference_pos, match_length, delta, pos = 0;
    uint8_t history[64 * 1024];

    while (bytestream2_get_bytes_left(gb) > 0) {
        uint8_t token = bytestream2_get_byte(gb);
        unsigned num_literals = token >> 4;

        if (num_literals == 15) {
            unsigned char current;
            do {
                current = bytestream2_get_byte(gb);
                num_literals += current;
            } while (current == 255);
        }

        if (pos + num_literals < HISTORY_SIZE) {
            bytestream2_get_buffer(gb, history + pos, num_literals);
            pos += num_literals;
        } else {
            while (num_literals-- > 0) {
                history[pos++] = bytestream2_get_byte(gb);
                if (pos == HISTORY_SIZE) {
                    bytestream2_put_buffer(pb, history, HISTORY_SIZE);
                    pos = 0;
                }
            }
        }

        if (bytestream2_get_bytes_left(gb) <= 0)
            break;

        delta = bytestream2_get_le16(gb);
        if (delta == 0)
            return 0;
        match_length = 4 + (token & 0x0F);
        if (match_length == 4 + 0x0F) {
            uint8_t current;

            do {
                current = bytestream2_get_byte(gb);
                match_length += current;
            } while (current == 255);
        }
        reference_pos = (pos >= delta) ? (pos - delta) : (HISTORY_SIZE + pos - delta);
        if (pos + match_length < HISTORY_SIZE && reference_pos + match_length < HISTORY_SIZE) {
            if (pos >= reference_pos + match_length || reference_pos >= pos + match_length) {
                memcpy(history + pos, history + reference_pos, match_length);
                pos += match_length;
            } else {
                while (match_length-- > 0)
                    history[pos++] = history[reference_pos++];
            }
        } else {
            while (match_length-- > 0) {
                history[pos++] = history[reference_pos++];
                if (pos == HISTORY_SIZE) {
                    bytestream2_put_buffer(pb, history, HISTORY_SIZE);
                    pos = 0;
                }
                reference_pos %= HISTORY_SIZE;
            }
        }
    }

    bytestream2_put_buffer(pb, history, pos);

    return bytestream2_tell_p(pb);
}

static int decode_blocks(AVCodecContext *avctx, AVFrame *p, ThreadFrame *frame,
                         unsigned uncompressed_size)
{
    NotchLCContext *s = avctx->priv_data;
    GetByteContext rgb, dgb, *gb = &s->gb;
    GetBitContext bit;
    int ylinesize, ulinesize, vlinesize, alinesize;
    uint16_t *dsty, *dstu, *dstv, *dsta;
    int ret;

    s->texture_size_x = bytestream2_get_le32(gb);
    s->texture_size_y = bytestream2_get_le32(gb);

    ret = ff_set_dimensions(avctx, s->texture_size_x, s->texture_size_y);
    if (ret < 0)
        return ret;

    s->uv_offset_data_offset = bytestream2_get_le32(gb);
    if (s->uv_offset_data_offset >= UINT_MAX / 4)
        return AVERROR_INVALIDDATA;
    s->uv_offset_data_offset *= 4;
    if (s->uv_offset_data_offset >= uncompressed_size)
        return AVERROR_INVALIDDATA;

    s->y_control_data_offset = bytestream2_get_le32(gb);
    if (s->y_control_data_offset >= UINT_MAX / 4)
        return AVERROR_INVALIDDATA;
    s->y_control_data_offset *= 4;
    if (s->y_control_data_offset >= uncompressed_size)
        return AVERROR_INVALIDDATA;

    s->a_control_word_offset = bytestream2_get_le32(gb);
    if (s->a_control_word_offset >= UINT_MAX / 4)
        return AVERROR_INVALIDDATA;
    s->a_control_word_offset *= 4;
    if (s->a_control_word_offset >= uncompressed_size)
        return AVERROR_INVALIDDATA;

    s->uv_data_offset = bytestream2_get_le32(gb);
    if (s->uv_data_offset >= UINT_MAX / 4)
        return AVERROR_INVALIDDATA;
    s->uv_data_offset *= 4;
    if (s->uv_data_offset >= uncompressed_size)
        return AVERROR_INVALIDDATA;

    s->y_data_size = bytestream2_get_le32(gb);
    if (s->y_data_size >= UINT_MAX / 4)
        return AVERROR_INVALIDDATA;

    s->a_data_offset = bytestream2_get_le32(gb);
    if (s->a_data_offset >= UINT_MAX / 4)
        return AVERROR_INVALIDDATA;
    s->a_data_offset *= 4;
    if (s->a_data_offset >= uncompressed_size)
        return AVERROR_INVALIDDATA;

    s->a_count_size = bytestream2_get_le32(gb);
    if (s->a_count_size >= UINT_MAX / 4)
        return AVERROR_INVALIDDATA;
    s->a_count_size *= 4;
    if (s->a_count_size >= uncompressed_size)
        return AVERROR_INVALIDDATA;

    s->data_end = bytestream2_get_le32(gb);
    if (s->data_end > uncompressed_size)
        return AVERROR_INVALIDDATA;

    s->y_data_row_offsets = bytestream2_tell(gb);
    if (s->data_end <= s->y_data_size)
        return AVERROR_INVALIDDATA;
    s->y_data_offset = s->data_end - s->y_data_size;
    if (s->y_data_offset <= s->a_data_offset)
        return AVERROR_INVALIDDATA;
    s->uv_count_offset = s->y_data_offset - s->a_data_offset;

    if ((ret = ff_thread_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    rgb = *gb;
    dgb = *gb;
    bytestream2_seek(&rgb, s->y_data_row_offsets, SEEK_SET);
    bytestream2_seek(gb, s->y_control_data_offset, SEEK_SET);

    if (bytestream2_get_bytes_left(gb) < (avctx->height + 3) / 4 * ((avctx->width + 3) / 4) * 4)
        return AVERROR_INVALIDDATA;

    dsty = (uint16_t *)p->data[0];
    dsta = (uint16_t *)p->data[3];
    ylinesize = p->linesize[0] / 2;
    alinesize = p->linesize[3] / 2;

    for (int y = 0; y < avctx->height; y += 4) {
        const unsigned row_offset = bytestream2_get_le32(&rgb);

        bytestream2_seek(&dgb, s->y_data_offset + row_offset, SEEK_SET);

        init_get_bits8(&bit, dgb.buffer, bytestream2_get_bytes_left(&dgb));
        for (int x = 0; x < avctx->width; x += 4) {
            unsigned item = bytestream2_get_le32(gb);
            unsigned y_min = item & 4095;
            unsigned y_max = (item >> 12) & 4095;
            unsigned y_diff = y_max - y_min;
            unsigned control[4];

            control[0] = (item >> 24) & 3;
            control[1] = (item >> 26) & 3;
            control[2] = (item >> 28) & 3;
            control[3] = (item >> 30) & 3;

            for (int i = 0; i < 4; i++) {
                const int nb_bits = control[i] + 1;
                const int div = (1 << nb_bits) - 1;
                const int add = div - 1;

                dsty[x + i * ylinesize + 0] = av_clip_uintp2(y_min + ((y_diff * get_bits(&bit, nb_bits) + add) / div), 12);
                dsty[x + i * ylinesize + 1] = av_clip_uintp2(y_min + ((y_diff * get_bits(&bit, nb_bits) + add) / div), 12);
                dsty[x + i * ylinesize + 2] = av_clip_uintp2(y_min + ((y_diff * get_bits(&bit, nb_bits) + add) / div), 12);
                dsty[x + i * ylinesize + 3] = av_clip_uintp2(y_min + ((y_diff * get_bits(&bit, nb_bits) + add) / div), 12);
            }
        }

        dsty += 4 * ylinesize;
    }

    rgb = *gb;
    dgb = *gb;
    bytestream2_seek(gb, s->a_control_word_offset, SEEK_SET);
    if (s->uv_count_offset == s->a_control_word_offset) {
        for (int y = 0; y < avctx->height; y++) {
            for (int x = 0; x < avctx->width; x++)
                dsta[x] = 4095;
            dsta += alinesize;
        }
    } else {
        if (bytestream2_get_bytes_left(gb) < (avctx->height + 15) / 16 * ((avctx->width + 15) / 16) * 8)
            return AVERROR_INVALIDDATA;

        for (int y = 0; y < avctx->height; y += 16) {
            for (int x = 0; x < avctx->width; x += 16) {
                unsigned m = bytestream2_get_le32(gb);
                unsigned offset = bytestream2_get_le32(gb);
                unsigned alpha0, alpha1;
                uint64_t control;

                if (offset >= UINT_MAX / 4)
                    return AVERROR_INVALIDDATA;
                offset = offset * 4 + s->uv_data_offset + s->a_data_offset;
                if (offset >= s->data_end)
                    return AVERROR_INVALIDDATA;

                bytestream2_seek(&dgb, offset, SEEK_SET);
                control = bytestream2_get_le64(&dgb);
                alpha0 = control & 0xFF;
                alpha1 = (control >> 8) & 0xFF;
                control = control >> 16;

                for (int by = 0; by < 4; by++) {
                    for (int bx = 0; bx < 4; bx++) {
                        switch (m & 3) {
                        case 0:
                            for (int i = 0; i < 4; i++) {
                                for (int j = 0; j < 4; j++) {
                                    dsta[x + (i + by * 4) * alinesize + bx * 4 + j] = 0;
                                }
                            }
                            break;
                        case 1:
                            for (int i = 0; i < 4; i++) {
                                for (int j = 0; j < 4; j++) {
                                    dsta[x + (i + by * 4) * alinesize + bx * 4 + j] = 4095;
                                }
                            }
                            break;
                        case 2:
                            for (int i = 0; i < 4; i++) {
                                for (int j = 0; j < 4; j++) {
                                    dsta[x + (i + by * 4) * alinesize + bx * 4 + j] = (alpha0 + (alpha1 - alpha0) * (control & 7)) << 4;
                                }
                            }
                            break;
                        default:
                            return AVERROR_INVALIDDATA;
                        }

                        control >>= 3;
                        m >>= 2;
                    }
                }
            }

            dsta += 16 * alinesize;
        }
    }

    bytestream2_seek(&rgb, s->uv_offset_data_offset, SEEK_SET);

    dstu = (uint16_t *)p->data[1];
    dstv = (uint16_t *)p->data[2];
    ulinesize = p->linesize[1] / 2;
    vlinesize = p->linesize[2] / 2;

    for (int y = 0; y < avctx->height; y += 16) {
        for (int x = 0; x < avctx->width; x += 16) {
            unsigned offset = bytestream2_get_le32(&rgb) * 4;
            int u[16][16] = { 0 }, v[16][16] = { 0 };
            int u0, v0, u1, v1, udif, vdif;
            unsigned escape, is8x8, loc;

            bytestream2_seek(&dgb, s->uv_data_offset + offset, SEEK_SET);

            is8x8 = bytestream2_get_le16(&dgb);
            escape = bytestream2_get_le16(&dgb);

            if (escape == 0 && is8x8 == 0) {
                u0 = bytestream2_get_byte(&dgb);
                v0 = bytestream2_get_byte(&dgb);
                u1 = bytestream2_get_byte(&dgb);
                v1 = bytestream2_get_byte(&dgb);
                loc = bytestream2_get_le32(&dgb);
                u0 = (u0 << 4) | (u0 & 0xF);
                v0 = (v0 << 4) | (v0 & 0xF);
                u1 = (u1 << 4) | (u1 & 0xF);
                v1 = (v1 << 4) | (v1 & 0xF);
                udif = u1 - u0;
                vdif = v1 - v0;

                for (int i = 0; i < 16; i += 4) {
                    for (int j = 0; j < 16; j += 4) {
                        for (int ii = 0; ii < 4; ii++) {
                            for (int jj = 0; jj < 4; jj++) {
                                u[i + ii][j + jj] = u0 + ((udif * (int)(loc & 3) + 2) / 3);
                                v[i + ii][j + jj] = v0 + ((vdif * (int)(loc & 3) + 2) / 3);
                            }
                        }

                        loc >>= 2;
                    }
                }
            } else {
                for (int i = 0; i < 16; i += 8) {
                    for (int j = 0; j < 16; j += 8) {
                        if (is8x8 & 1) {
                            u0 = bytestream2_get_byte(&dgb);
                            v0 = bytestream2_get_byte(&dgb);
                            u1 = bytestream2_get_byte(&dgb);
                            v1 = bytestream2_get_byte(&dgb);
                            loc = bytestream2_get_le32(&dgb);
                            u0 = (u0 << 4) | (u0 & 0xF);
                            v0 = (v0 << 4) | (v0 & 0xF);
                            u1 = (u1 << 4) | (u1 & 0xF);
                            v1 = (v1 << 4) | (v1 & 0xF);
                            udif = u1 - u0;
                            vdif = v1 - v0;

                            for (int ii = 0; ii < 8; ii += 2) {
                                for (int jj = 0; jj < 8; jj += 2) {
                                    for (int iii = 0; iii < 2; iii++) {
                                        for (int jjj = 0; jjj < 2; jjj++) {
                                            u[i + ii + iii][j + jj + jjj] = u0 + ((udif * (int)(loc & 3) + 2) / 3);
                                            v[i + ii + iii][j + jj + jjj] = v0 + ((vdif * (int)(loc & 3) + 2) / 3);
                                        }
                                    }

                                    loc >>= 2;
                                }
                            }
                        } else if (escape) {
                            for (int ii = 0; ii < 8; ii += 4) {
                                for (int jj = 0; jj < 8; jj += 4) {
                                    u0 = bytestream2_get_byte(&dgb);
                                    v0 = bytestream2_get_byte(&dgb);
                                    u1 = bytestream2_get_byte(&dgb);
                                    v1 = bytestream2_get_byte(&dgb);
                                    loc = bytestream2_get_le32(&dgb);
                                    u0 = (u0 << 4) | (u0 & 0xF);
                                    v0 = (v0 << 4) | (v0 & 0xF);
                                    u1 = (u1 << 4) | (u1 & 0xF);
                                    v1 = (v1 << 4) | (v1 & 0xF);
                                    udif = u1 - u0;
                                    vdif = v1 - v0;

                                    for (int iii = 0; iii < 4; iii++) {
                                        for (int jjj = 0; jjj < 4; jjj++) {
                                            u[i + ii + iii][j + jj + jjj] = u0 + ((udif * (int)(loc & 3) + 2) / 3);
                                            v[i + ii + iii][j + jj + jjj] = v0 + ((vdif * (int)(loc & 3) + 2) / 3);

                                            loc >>= 2;
                                        }
                                    }
                                }
                            }
                        }

                        is8x8 >>= 1;
                    }
                }
            }

            for (int i = 0; i < 16; i++) {
                for (int j = 0; j < 16; j++) {
                    dstu[x + i * ulinesize + j] = u[i][j];
                    dstv[x + i * vlinesize + j] = v[i][j];
                }
            }
        }

        dstu += 16 * ulinesize;
        dstv += 16 * vlinesize;
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    NotchLCContext *s = avctx->priv_data;
    ThreadFrame frame = { .f = data };
    GetByteContext *gb = &s->gb;
    PutByteContext *pb = &s->pb;
    unsigned uncompressed_size;
    AVFrame *p = data;
    int ret;

    if (avpkt->size <= 40)
        return AVERROR_INVALIDDATA;

    bytestream2_init(gb, avpkt->data, avpkt->size);

    if (bytestream2_get_le32(gb) != MKBETAG('N','L','C','1'))
        return AVERROR_INVALIDDATA;

    uncompressed_size = bytestream2_get_le32(gb);
    s->compressed_size = bytestream2_get_le32(gb);
    s->format = bytestream2_get_le32(gb);

    if (s->format > 2)
        return AVERROR_PATCHWELCOME;

    if (s->format == 0) {
        ret = ff_lzf_uncompress(gb, &s->lzf_buffer, &s->lzf_size);
        if (ret < 0)
            return ret;

        if (uncompressed_size > s->lzf_size)
            return AVERROR_INVALIDDATA;

        bytestream2_init(gb, s->lzf_buffer, uncompressed_size);
    } else if (s->format == 1) {
        av_fast_padded_malloc(&s->uncompressed_buffer, &s->uncompressed_size,
                              uncompressed_size);
        if (!s->uncompressed_buffer)
            return AVERROR(ENOMEM);

        bytestream2_init_writer(pb, s->uncompressed_buffer, s->uncompressed_size);

        ret = lz4_decompress(avctx, gb, pb);
        if (ret != uncompressed_size)
            return AVERROR_INVALIDDATA;

        bytestream2_init(gb, s->uncompressed_buffer, uncompressed_size);
    }

    ret = decode_blocks(avctx, p, &frame, uncompressed_size);
    if (ret < 0)
        return ret;

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    NotchLCContext *s = avctx->priv_data;

    av_freep(&s->uncompressed_buffer);
    s->uncompressed_size = 0;
    av_freep(&s->lzf_buffer);
    s->lzf_size = 0;

    return 0;
}

AVCodec ff_notchlc_decoder = {
    .name             = "notchlc",
    .long_name        = NULL_IF_CONFIG_SMALL("NotchLC"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_NOTCHLC,
    .priv_data_size   = sizeof(NotchLCContext),
    .init             = decode_init,
    .close            = decode_end,
    .decode           = decode_frame,
    .capabilities     = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
};
