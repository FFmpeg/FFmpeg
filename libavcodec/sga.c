/*
 * Copyright (c) 2021 Paul B Mahol
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

#include "libavutil/common.h"
#include "avcodec.h"
#include "get_bits.h"
#include "bytestream.h"
#include "internal.h"

#define PALDATA_FOLLOWS_TILEDATA 4
#define HAVE_COMPRESSED_TILEMAP 32
#define HAVE_TILEMAP 128

typedef struct SGAVideoContext {
    GetByteContext gb;

    int metadata_size;
    int tiledata_size;
    int tiledata_offset;
    int tilemapdata_size;
    int tilemapdata_offset;
    int paldata_size;
    int paldata_offset;
    int palmapdata_offset;
    int palmapdata_size;

    int flags;
    int nb_pal;
    int nb_tiles;
    int tiles_w, tiles_h;
    int shift;
    int plus;
    int swap;

    uint32_t pal[256];
    uint8_t *tileindex_data;
    unsigned tileindex_size;
    uint8_t *palmapindex_data;
    unsigned palmapindex_size;
    uint8_t uncompressed[65536];
} SGAVideoContext;

static av_cold int sga_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = AV_PIX_FMT_PAL8;
    return 0;
}

static int decode_palette(GetByteContext *gb, uint32_t *pal)
{
    GetBitContext gbit;

    if (bytestream2_get_bytes_left(gb) < 18)
        return AVERROR_INVALIDDATA;

    memset(pal, 0, 16 * sizeof(*pal));
    init_get_bits8(&gbit, gb->buffer, 18);

    for (int RGBIndex = 0; RGBIndex < 3; RGBIndex++) {
        for (int index = 0; index < 16; index++) {
            unsigned color = get_bits1(&gbit) << RGBIndex;
            pal[15 - index] |= color << (5 + 16);
        }
    }

    for (int RGBIndex = 0; RGBIndex < 3; RGBIndex++) {
        for (int index = 0; index < 16; index++) {
            unsigned color = get_bits1(&gbit) << RGBIndex;
            pal[15 - index] |= color << (5 + 8);
        }
    }

    for (int RGBIndex = 0; RGBIndex < 3; RGBIndex++) {
        for (int index = 0; index < 16; index++) {
            unsigned color = get_bits1(&gbit) << RGBIndex;
            pal[15 - index] |= color << (5 + 0);
        }
    }

    for (int index = 0; index < 16; index++)
        pal[index] = (0xFFU << 24) | pal[index] | (pal[index] >> 3);

    bytestream2_skip(gb, 18);

    return 0;
}

static int decode_index_palmap(SGAVideoContext *s, AVFrame *frame)
{
    const uint8_t *tt = s->tileindex_data;

    for (int y = 0; y < s->tiles_h; y++) {
        for (int x = 0; x < s->tiles_w; x++) {
            int pal_idx = s->palmapindex_data[y * s->tiles_w + x] * 16;
            uint8_t *dst = frame->data[0] + y * 8 * frame->linesize[0] + x * 8;

            for (int yy = 0; yy < 8; yy++) {
                for (int xx = 0; xx < 8; xx++)
                    dst[xx] = pal_idx + tt[xx];
                tt += 8;

                dst += frame->linesize[0];
            }
        }
    }

    return 0;
}

static int decode_index_tilemap(SGAVideoContext *s, AVFrame *frame)
{
    GetByteContext *gb = &s->gb;
    GetBitContext pm;

    bytestream2_seek(gb, s->tilemapdata_offset, SEEK_SET);
    if (bytestream2_get_bytes_left(gb) < s->tilemapdata_size)
        return AVERROR_INVALIDDATA;

    init_get_bits8(&pm, gb->buffer, s->tilemapdata_size);

    for (int y = 0; y < s->tiles_h; y++) {
        for (int x = 0; x < s->tiles_w; x++) {
            uint8_t tile[64];
            int tilemap = get_bits(&pm, 16);
            int flip_x = (tilemap >> 11) & 1;
            int flip_y = (tilemap >> 12) & 1;
            int tindex = av_clip((tilemap & 511) - 1, 0, s->nb_tiles - 1);
            const uint8_t *tt = s->tileindex_data + tindex * 64;
            int pal_idx = ((tilemap >> 13) & 3) * 16;
            uint8_t *dst = frame->data[0] + y * 8 * frame->linesize[0] + x * 8;

            if (!flip_x && !flip_y) {
                memcpy(tile, tt, 64);
            } else if (flip_x && flip_y) {
                for (int i = 0; i < 8; i++) {
                    for (int j = 0; j < 8; j++)
                        tile[i * 8 + j] = tt[(7 - i) * 8 + 7 - j];
                }
            } else if (flip_x) {
                for (int i = 0; i < 8; i++) {
                    for (int j = 0; j < 8; j++)
                        tile[i * 8 + j] = tt[i * 8 + 7 - j];
                }
            } else {
                for (int i = 0; i < 8; i++) {
                    for (int j = 0; j < 8; j++)
                        tile[i * 8 + j] = tt[(7 - i) * 8 + j];
                }
            }

            for (int yy = 0; yy < 8; yy++) {
                for (int xx = 0; xx < 8; xx++)
                    dst[xx] = pal_idx + tile[xx + yy * 8];

                dst += frame->linesize[0];
            }
        }
    }

    return 0;
}

static int decode_index(SGAVideoContext *s, AVFrame *frame)
{
    const uint8_t *src = s->tileindex_data;
    uint8_t *dst = frame->data[0];

    for (int y = 0; y < frame->height; y += 8) {
        for (int x = 0; x < frame->width; x += 8) {
            for (int yy = 0; yy < 8; yy++) {
                for (int xx = 0; xx < 8; xx++)
                    dst[x + xx + yy * frame->linesize[0]] = src[xx];
                src += 8;
            }
        }

        dst += 8 * frame->linesize[0];
    }

    return 0;
}

static int lzss_decompress(AVCodecContext *avctx,
                           GetByteContext *gb, uint8_t *dst,
                           int dst_size, int shift, int plus)
{
    int oi = 0;

    while (bytestream2_get_bytes_left(gb) > 0 && oi < dst_size) {
        uint16_t displace, header = bytestream2_get_be16(gb);
        int count, offset;

        for (int i = 0; i < 16; i++) {
            switch (header >> 15) {
            case 0:
                if (oi + 2 < dst_size) {
                    dst[oi++] = bytestream2_get_byte(gb);
                    dst[oi++] = bytestream2_get_byte(gb);
                }
                break;
            case 1:
                displace = bytestream2_get_be16(gb);
                count = displace >> shift;
                offset = displace & ((1 << shift) - 1);

                if (displace == 0) {
                    while (bytestream2_get_bytes_left(gb) > 0 &&
                           oi < dst_size)
                        dst[oi++] = bytestream2_get_byte(gb);
                    return oi;
                }

                count += plus;

                if (offset <= 0)
                    offset = 1;
                if (oi < offset || oi + count * 2 > dst_size)
                    return AVERROR_INVALIDDATA;
                for (int j = 0; j < count * 2; j++) {
                    dst[oi] = dst[oi - offset];
                    oi++;
                }
                break;
            }

            header <<= 1;
        }
    }

    return AVERROR_INVALIDDATA;
}

static int decode_palmapdata(AVCodecContext *avctx)
{
    SGAVideoContext *s = avctx->priv_data;
    const int bits = (s->nb_pal + 1) / 2;
    GetByteContext *gb = &s->gb;
    GetBitContext pm;

    bytestream2_seek(gb, s->palmapdata_offset, SEEK_SET);
    if (bytestream2_get_bytes_left(gb) < s->palmapdata_size)
        return AVERROR_INVALIDDATA;
    init_get_bits8(&pm, gb->buffer, s->palmapdata_size);

    for (int y = 0; y < s->tiles_h; y++) {
        uint8_t *dst = s->palmapindex_data + y * s->tiles_w;

        for (int x = 0; x < s->tiles_w; x++)
            dst[x] = get_bits(&pm, bits);

        dst += s->tiles_w;
    }

    return 0;
}

static int decode_tiledata(AVCodecContext *avctx)
{
    SGAVideoContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    GetBitContext tm;

    bytestream2_seek(gb, s->tiledata_offset, SEEK_SET);
    if (bytestream2_get_bytes_left(gb) < s->tiledata_size)
        return AVERROR_INVALIDDATA;
    init_get_bits8(&tm, gb->buffer, s->tiledata_size);

    for (int n = 0; n < s->nb_tiles; n++) {
        uint8_t *dst = s->tileindex_data + n * 64;

        for (int yy = 0; yy < 8; yy++) {
            for (int xx = 0; xx < 8; xx++)
                dst[xx] = get_bits(&tm, 4);

            dst += 8;
        }
    }

    for (int i = 0; i < s->nb_tiles && s->swap; i++) {
        uint8_t *dst = s->tileindex_data + i * 64;

        for (int j = 8; j < 64; j += 16) {
            for (int k = 0; k < 8; k += 2)
                FFSWAP(uint8_t, dst[j + k], dst[j+k+1]);
        }
    }

    return 0;
}

static int sga_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    SGAVideoContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    AVFrame *frame = data;
    int ret, type;

    if (avpkt->size <= 14)
        return AVERROR_INVALIDDATA;

    s->flags  = avpkt->data[8];
    s->nb_pal = avpkt->data[9];
    s->tiles_w = avpkt->data[10];
    s->tiles_h = avpkt->data[11];

    if (s->nb_pal > 4)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_set_dimensions(avctx,
                                 s->tiles_w * 8,
                                 s->tiles_h * 8)) < 0)
        return ret;

    av_fast_padded_malloc(&s->tileindex_data, &s->tileindex_size,
                          avctx->width * avctx->height);
    if (!s->tileindex_data)
        return AVERROR(ENOMEM);

    av_fast_padded_malloc(&s->palmapindex_data, &s->palmapindex_size,
                          s->tiles_w * s->tiles_h);
    if (!s->palmapindex_data)
        return AVERROR(ENOMEM);

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    bytestream2_init(gb, avpkt->data, avpkt->size);

    type = bytestream2_get_byte(gb);
    s->metadata_size = 12 + ((!!(s->flags & HAVE_TILEMAP)) * 2);
    s->nb_tiles = s->flags & HAVE_TILEMAP ? AV_RB16(avpkt->data + 12) : s->tiles_w * s->tiles_h;
    if (s->nb_tiles > s->tiles_w * s->tiles_h)
        return AVERROR_INVALIDDATA;

    av_log(avctx, AV_LOG_DEBUG, "type: %X flags: %X nb_tiles: %d\n", type, s->flags, s->nb_tiles);

    switch (type) {
    case 0xE7:
    case 0xCB:
    case 0xCD:
        s->swap = 1;
        s->shift = 12;
        s->plus = 1;
        break;
    case 0xC9:
        s->swap = 1;
        s->shift = 13;
        s->plus = 1;
        break;
    case 0xC8:
        s->swap = 1;
        s->shift = 13;
        s->plus = 0;
        break;
    case 0xC7:
        s->swap = 0;
        s->shift = 13;
        s->plus = 1;
        break;
    case 0xC6:
        s->swap = 0;
        s->shift = 13;
        s->plus = 0;
        break;
    }

    if (type == 0xE7) {
        int offset = s->metadata_size, left;
        int sizes[3];

        bytestream2_seek(gb, s->metadata_size, SEEK_SET);

        for (int i = 0; i < 3; i++)
            sizes[i] = bytestream2_get_be16(gb);

        for (int i = 0; i < 3; i++) {
            int size = sizes[i];
            int raw = size >> 15;

            size &= (1 << 15) - 1;

            if (raw) {
                if (bytestream2_get_bytes_left(gb) < size)
                    return AVERROR_INVALIDDATA;

                if (sizeof(s->uncompressed) - offset < size)
                    return AVERROR_INVALIDDATA;

                memcpy(s->uncompressed + offset, gb->buffer, size);
                bytestream2_skip(gb, size);
            } else {
                GetByteContext gb2;

                if (bytestream2_get_bytes_left(gb) < size)
                    return AVERROR_INVALIDDATA;

                bytestream2_init(&gb2, gb->buffer, size);
                ret = lzss_decompress(avctx, &gb2, s->uncompressed + offset,
                                      sizeof(s->uncompressed) - offset, s->shift, s->plus);
                if (ret < 0)
                    return ret;
                bytestream2_skip(gb, size);
                size = ret;
            }

            offset += size;
        }

        left = bytestream2_get_bytes_left(gb);
        if (sizeof(s->uncompressed) - offset < left)
            return AVERROR_INVALIDDATA;

        bytestream2_get_buffer(gb, s->uncompressed + offset, left);

        offset += left;
        bytestream2_init(gb, s->uncompressed, offset);
    }

    switch (type) {
    case 0xCD:
    case 0xCB:
    case 0xC9:
    case 0xC8:
    case 0xC7:
    case 0xC6:
        bytestream2_seek(gb, s->metadata_size, SEEK_SET);
        ret = lzss_decompress(avctx, gb, s->uncompressed + s->metadata_size,
                              sizeof(s->uncompressed) - s->metadata_size, s->shift, s->plus);
        if (ret < 0)
            return ret;
        bytestream2_init(gb, s->uncompressed, ret + s->metadata_size);
    case 0xE7:
    case 0xC1:
        s->tiledata_size = s->nb_tiles * 32;
        s->paldata_size = s->nb_pal * 18;
        s->tiledata_offset = s->flags & PALDATA_FOLLOWS_TILEDATA ? s->metadata_size : s->metadata_size + s->paldata_size;
        s->paldata_offset = s->flags & PALDATA_FOLLOWS_TILEDATA ? s->metadata_size + s->tiledata_size : s->metadata_size;
        s->palmapdata_offset = (s->flags & HAVE_TILEMAP) ? -1 : s->paldata_offset + s->paldata_size;
        s->palmapdata_size = (s->flags & HAVE_TILEMAP) || s->nb_pal < 2 ? 0 : (s->tiles_w * s->tiles_h * ((s->nb_pal + 1) / 2) + 7) / 8;
        s->tilemapdata_size = (s->flags & HAVE_TILEMAP) ? s->tiles_w * s->tiles_h * 2 : 0;
        s->tilemapdata_offset = (s->flags & HAVE_TILEMAP) ? s->paldata_offset + s->paldata_size: -1;

        bytestream2_seek(gb, s->paldata_offset, SEEK_SET);
        for (int n = 0; n < s->nb_pal; n++) {
            ret = decode_palette(gb, s->pal + 16 * n);
            if (ret < 0)
                return ret;
        }

        if (s->tiledata_size > 0) {
            ret = decode_tiledata(avctx);
            if (ret < 0)
                return ret;
        }

        if (s->palmapdata_size > 0) {
            ret = decode_palmapdata(avctx);
            if (ret < 0)
                return ret;
        }

        if (s->palmapdata_size > 0 && s->tiledata_size > 0) {
            ret = decode_index_palmap(s, frame);
            if (ret < 0)
                return ret;
        } else if (s->tilemapdata_size > 0 && s->tiledata_size > 0) {
            ret = decode_index_tilemap(s, frame);
            if (ret < 0)
                return ret;
        } else if (s->tiledata_size > 0) {
            ret = decode_index(s, frame);
            if (ret < 0)
                return ret;
        }
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown type: %X\n", type);
        return AVERROR_INVALIDDATA;
    }

    memcpy(frame->data[1], s->pal, AVPALETTE_SIZE);
    frame->palette_has_changed = 1;
    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int sga_decode_end(AVCodecContext *avctx)
{
    SGAVideoContext *s = avctx->priv_data;

    av_freep(&s->tileindex_data);
    s->tileindex_size = 0;

    av_freep(&s->palmapindex_data);
    s->palmapindex_size = 0;

    return 0;
}

const AVCodec ff_sga_decoder = {
    .name           = "sga",
    .long_name      = NULL_IF_CONFIG_SMALL("Digital Pictures SGA Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SGA_VIDEO,
    .priv_data_size = sizeof(SGAVideoContext),
    .init           = sga_decode_init,
    .decode         = sga_decode_frame,
    .close          = sga_decode_end,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
