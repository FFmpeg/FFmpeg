/*
 * Amuse Graphics Movie decoder
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
#include <stdlib.h>
#include <string.h>

#define BITSTREAM_READER_LE

#include "avcodec.h"
#include "bytestream.h"
#include "copy_block.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "internal.h"

static const uint8_t unscaled_luma[64] = {
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19,
    26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56,
    68,109,103, 77, 24, 35, 55, 64, 81,104,113, 92,
    49, 64, 78, 87,103,121,120,101, 72, 92, 95, 98,
    112,100,103,99
};

static const uint8_t unscaled_chroma[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66,
    99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99
};

typedef struct MotionVector {
    int16_t x, y;
} MotionVector;

typedef struct AGMContext {
    const AVClass  *class;
    AVCodecContext *avctx;
    GetBitContext   gb;
    GetByteContext  gbyte;

    int key_frame;
    int bitstream_size;
    int compression;
    int blocks_w;
    int blocks_h;
    int size[3];
    int plus;
    unsigned flags;
    unsigned fflags;

    MotionVector *mvectors;
    int           mvectors_size;

    AVFrame *prev_frame;

    int luma_quant_matrix[64];
    int chroma_quant_matrix[64];

    ScanTable scantable;
    DECLARE_ALIGNED(32, int16_t, block)[64];
    IDCTDSPContext idsp;
} AGMContext;

static int read_code(GetBitContext *gb, int *oskip, int *level, int *map, int mode)
{
    int len = 0, skip = 0, max;

    if (show_bits(gb, 2)) {
        switch (show_bits(gb, 4)) {
        case 1:
        case 9:
            len = 1;
            skip = 3;
            break;
        case 2:
            len = 3;
            skip = 4;
            break;
        case 3:
            len = 7;
            skip = 4;
            break;
        case 5:
        case 13:
            len = 2;
            skip = 3;
            break;
        case 6:
            len = 4;
            skip = 4;
            break;
        case 7:
            len = 8;
            skip = 4;
            break;
        case 10:
            len = 5;
            skip = 4;
            break;
        case 11:
            len = 9;
            skip = 4;
            break;
        case 14:
            len = 6;
            skip = 4;
            break;
        case 15:
            len = ((show_bits(gb, 5) & 0x10) | 0xA0) >> 4;
            skip = 5;
            break;
        default:
            return AVERROR_INVALIDDATA;
        }

        skip_bits(gb, skip);
        *level = get_bits(gb, len);
        *map = 1;
        *oskip = 0;
        max = 1 << (len - 1);
        if (*level < max)
            *level = -(max + *level);
    } else if (show_bits(gb, 3) & 4) {
        skip_bits(gb, 3);
        if (mode == 1) {
            if (show_bits(gb, 4)) {
                if (show_bits(gb, 4) == 1) {
                    skip_bits(gb, 4);
                    *oskip = get_bits(gb, 16);
                } else {
                    *oskip = get_bits(gb, 4);
                }
            } else {
                skip_bits(gb, 4);
                *oskip = get_bits(gb, 10);
            }
        } else if (mode == 0) {
            *oskip = get_bits(gb, 10);
        }
        *level = 0;
    } else {
        skip_bits(gb, 3);
        if (mode == 0)
            *oskip = get_bits(gb, 4);
        else if (mode == 1)
            *oskip = 0;
        *level = 0;
    }

    return 0;
}

static int decode_intra_block(AGMContext *s, GetBitContext *gb, int size,
                              const int *quant_matrix, int *skip, int *dc_level)
{
    const uint8_t *scantable = s->scantable.permutated;
    const int offset = s->plus ? 0 : 1024;
    int16_t *block = s->block;
    int level, ret, map = 0;

    memset(block, 0, sizeof(s->block));

    if (*skip > 0) {
        (*skip)--;
    } else {
        ret = read_code(gb, skip, &level, &map, s->flags & 1);
        if (ret < 0)
            return ret;
        *dc_level += level;
    }
    block[scantable[0]] = offset + *dc_level * quant_matrix[0];

    for (int i = 1; i < 64;) {
        if (*skip > 0) {
            int rskip;

            rskip = FFMIN(*skip, 64 - i);
            i += rskip;
            *skip -= rskip;
        } else {
            ret = read_code(gb, skip, &level, &map, s->flags & 1);
            if (ret < 0)
                return ret;

            block[scantable[i]] = level * quant_matrix[i];
            i++;
        }
    }

    return 0;
}

static int decode_intra_plane(AGMContext *s, GetBitContext *gb, int size,
                              const int *quant_matrix, AVFrame *frame,
                              int plane)
{
    int ret, skip = 0, dc_level = 0;

    if ((ret = init_get_bits8(gb, s->gbyte.buffer, size)) < 0)
        return ret;

    for (int y = 0; y < s->blocks_h; y++) {
        for (int x = 0; x < s->blocks_w; x++) {
            ret = decode_intra_block(s, gb, size, quant_matrix, &skip, &dc_level);
            if (ret < 0)
                return ret;

            s->idsp.idct_put(frame->data[plane] + (s->blocks_h - 1 - y) * 8 * frame->linesize[plane] + x * 8,
                             frame->linesize[plane], s->block);
        }
    }

    align_get_bits(gb);
    if (get_bits_left(gb) < 0)
        av_log(s->avctx, AV_LOG_WARNING, "overread\n");
    if (get_bits_left(gb) > 0)
        av_log(s->avctx, AV_LOG_WARNING, "underread: %d\n", get_bits_left(gb));

    return 0;
}

static int decode_inter_block(AGMContext *s, GetBitContext *gb, int size,
                              const int *quant_matrix, int *skip,
                              int *map)
{
    const uint8_t *scantable = s->scantable.permutated;
    int16_t *block = s->block;
    int level, ret;

    memset(block, 0, sizeof(s->block));

    for (int i = 0; i < 64;) {
        if (*skip > 0) {
            int rskip;

            rskip = FFMIN(*skip, 64 - i);
            i += rskip;
            *skip -= rskip;
        } else {
            ret = read_code(gb, skip, &level, map, s->flags & 1);
            if (ret < 0)
                return ret;

            block[scantable[i]] = level * quant_matrix[i];
            i++;
        }
    }

    return 0;
}

static int decode_inter_plane(AGMContext *s, GetBitContext *gb, int size,
                              const int *quant_matrix, AVFrame *frame,
                              AVFrame *prev, int plane)
{
    int ret, skip = 0;

    if ((ret = init_get_bits8(gb, s->gbyte.buffer, size)) < 0)
        return ret;

    if (s->flags & 2) {
        for (int y = 0; y < s->blocks_h; y++) {
            for (int x = 0; x < s->blocks_w; x++) {
                int shift = plane == 0;
                int mvpos = (y >> shift) * (s->blocks_w >> shift) + (x >> shift);
                int orig_mv_x = s->mvectors[mvpos].x;
                int mv_x = s->mvectors[mvpos].x / (1 + !shift);
                int mv_y = s->mvectors[mvpos].y / (1 + !shift);
                int h = s->avctx->coded_height >> !shift;
                int w = s->avctx->coded_width  >> !shift;
                int map = 0;

                ret = decode_inter_block(s, gb, size, quant_matrix, &skip, &map);
                if (ret < 0)
                    return ret;

                if (orig_mv_x >= -32) {
                    if (y * 8 + mv_y < 0 || y * 8 + mv_y >= h ||
                        x * 8 + mv_x < 0 || x * 8 + mv_x >= w)
                        return AVERROR_INVALIDDATA;

                    copy_block8(frame->data[plane] + (s->blocks_h - 1 - y) * 8 * frame->linesize[plane] + x * 8,
                                prev->data[plane] + ((s->blocks_h - 1 - y) * 8 - mv_y) * prev->linesize[plane] + (x * 8 + mv_x),
                                frame->linesize[plane], prev->linesize[plane], 8);
                    if (map) {
                        s->idsp.idct(s->block);
                        for (int i = 0; i < 64; i++)
                            s->block[i] = (s->block[i] + 1) & 0xFFFC;
                        s->idsp.add_pixels_clamped(s->block, frame->data[plane] + (s->blocks_h - 1 - y) * 8 * frame->linesize[plane] + x * 8,
                                                   frame->linesize[plane]);
                    }
                } else if (map) {
                    s->idsp.idct_put(frame->data[plane] + (s->blocks_h - 1 - y) * 8 * frame->linesize[plane] + x * 8,
                                     frame->linesize[plane], s->block);
                }
            }
        }
    } else {
        for (int y = 0; y < s->blocks_h; y++) {
            for (int x = 0; x < s->blocks_w; x++) {
                int map = 0;

                ret = decode_inter_block(s, gb, size, quant_matrix, &skip, &map);
                if (ret < 0)
                    return ret;

                if (!map)
                    continue;
                s->idsp.idct_add(frame->data[plane] + (s->blocks_h - 1 - y) * 8 * frame->linesize[plane] + x * 8,
                                 frame->linesize[plane], s->block);
            }
        }
    }

    align_get_bits(gb);
    if (get_bits_left(gb) < 0)
        av_log(s->avctx, AV_LOG_WARNING, "overread\n");
    if (get_bits_left(gb) > 0)
        av_log(s->avctx, AV_LOG_WARNING, "underread: %d\n", get_bits_left(gb));

    return 0;
}

static void compute_quant_matrix(AGMContext *s, double qscale)
{
    int luma[64], chroma[64];
    double f = 1.0 - fabs(qscale);

    if (!s->key_frame && (s->flags & 2)) {
        if (qscale >= 0.0) {
            for (int i = 0; i < 64; i++) {
                luma[i]   = FFMAX(1, 16 * f);
                chroma[i] = FFMAX(1, 16 * f);
            }
        } else {
            for (int i = 0; i < 64; i++) {
                luma[i]   = FFMAX(1, 16 - qscale * 32);
                chroma[i] = FFMAX(1, 16 - qscale * 32);
            }
        }
    } else {
        if (qscale >= 0.0) {
            for (int i = 0; i < 64; i++) {
                luma[i]   = FFMAX(1, unscaled_luma  [(i & 7) * 8 + (i >> 3)] * f);
                chroma[i] = FFMAX(1, unscaled_chroma[(i & 7) * 8 + (i >> 3)] * f);
            }
        } else {
            for (int i = 0; i < 64; i++) {
                luma[i]   = FFMAX(1, 255.0 - (255 - unscaled_luma  [(i & 7) * 8 + (i >> 3)]) * f);
                chroma[i] = FFMAX(1, 255.0 - (255 - unscaled_chroma[(i & 7) * 8 + (i >> 3)]) * f);
            }
        }
    }

    for (int i = 0; i < 64; i++) {
        int pos = ff_zigzag_direct[i];

        s->luma_quant_matrix[i]   = luma[pos]   * ((pos / 8) & 1 ? -1 : 1);
        s->chroma_quant_matrix[i] = chroma[pos] * ((pos / 8) & 1 ? -1 : 1);
    }
}

static int decode_intra(AVCodecContext *avctx, GetBitContext *gb, AVFrame *frame)
{
    AGMContext *s = avctx->priv_data;
    int ret;

    compute_quant_matrix(s, (2 * s->compression - 100) / 100.0);

    s->blocks_w = avctx->coded_width  >> 3;
    s->blocks_h = avctx->coded_height >> 3;

    ret = decode_intra_plane(s, gb, s->size[0], s->luma_quant_matrix, frame, 0);
    if (ret < 0)
        return ret;

    bytestream2_skip(&s->gbyte, s->size[0]);

    s->blocks_w = avctx->coded_width  >> 4;
    s->blocks_h = avctx->coded_height >> 4;

    ret = decode_intra_plane(s, gb, s->size[1], s->chroma_quant_matrix, frame, 2);
    if (ret < 0)
        return ret;

    bytestream2_skip(&s->gbyte, s->size[1]);

    s->blocks_w = avctx->coded_width  >> 4;
    s->blocks_h = avctx->coded_height >> 4;

    ret = decode_intra_plane(s, gb, s->size[2], s->chroma_quant_matrix, frame, 1);
    if (ret < 0)
        return ret;

    return 0;
}

static int decode_motion_vectors(AVCodecContext *avctx, GetBitContext *gb)
{
    AGMContext *s = avctx->priv_data;
    int nb_mvs = ((avctx->height + 15) >> 4) * ((avctx->width + 15) >> 4);
    int ret, skip = 0, value, map;

    av_fast_padded_malloc(&s->mvectors, &s->mvectors_size,
                          nb_mvs * sizeof(*s->mvectors));
    if (!s->mvectors)
        return AVERROR(ENOMEM);

    if ((ret = init_get_bits8(gb, s->gbyte.buffer, bytestream2_get_bytes_left(&s->gbyte) -
                                                   (s->size[0] + s->size[1] + s->size[2]))) < 0)
        return ret;

    memset(s->mvectors, 0, sizeof(*s->mvectors) * nb_mvs);

    for (int i = 0; i < nb_mvs; i++) {
        ret = read_code(gb, &skip, &value, &map, 1);
        if (ret < 0)
            return ret;
        s->mvectors[i].x = value;
        i += skip;
    }

    for (int i = 0; i < nb_mvs; i++) {
        ret = read_code(gb, &skip, &value, &map, 1);
        if (ret < 0)
            return ret;
        s->mvectors[i].y = value;
        i += skip;
    }

    if (get_bits_left(gb) <= 0)
        return AVERROR_INVALIDDATA;
    skip = (get_bits_count(gb) >> 3) + 1;
    bytestream2_skip(&s->gbyte, skip);

    return 0;
}

static int decode_inter(AVCodecContext *avctx, GetBitContext *gb,
                        AVFrame *frame, AVFrame *prev)
{
    AGMContext *s = avctx->priv_data;
    int ret;

    compute_quant_matrix(s, (2 * s->compression - 100) / 100.0);

    if (s->flags & 2) {
        ret = decode_motion_vectors(avctx, gb);
        if (ret < 0)
            return ret;
    }

    s->blocks_w = avctx->coded_width  >> 3;
    s->blocks_h = avctx->coded_height >> 3;

    ret = decode_inter_plane(s, gb, s->size[0], s->luma_quant_matrix, frame, prev, 0);
    if (ret < 0)
        return ret;

    bytestream2_skip(&s->gbyte, s->size[0]);

    s->blocks_w = avctx->coded_width  >> 4;
    s->blocks_h = avctx->coded_height >> 4;

    ret = decode_inter_plane(s, gb, s->size[1], s->chroma_quant_matrix, frame, prev, 2);
    if (ret < 0)
        return ret;

    bytestream2_skip(&s->gbyte, s->size[1]);

    s->blocks_w = avctx->coded_width  >> 4;
    s->blocks_h = avctx->coded_height >> 4;

    ret = decode_inter_plane(s, gb, s->size[2], s->chroma_quant_matrix, frame, prev, 1);
    if (ret < 0)
        return ret;

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame, AVPacket *avpkt)
{
    AGMContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    GetByteContext *gbyte = &s->gbyte;
    AVFrame *frame = data;
    int w, h, width, height, header;
    int ret;

    if (!avpkt->size)
        return 0;

    bytestream2_init(gbyte, avpkt->data, avpkt->size);

    header = bytestream2_get_le32(gbyte);
    s->fflags = bytestream2_get_le32(gbyte);
    s->bitstream_size = s->fflags & 0x1FFFFFFF;
    s->fflags >>= 29;
    av_log(avctx, AV_LOG_DEBUG, "fflags: %X\n", s->fflags);
    if (avpkt->size < s->bitstream_size + 8)
        return AVERROR_INVALIDDATA;

    s->key_frame = s->fflags & 0x1;
    frame->key_frame = s->key_frame;
    frame->pict_type = s->key_frame ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    if (header) {
        av_log(avctx, AV_LOG_ERROR, "header: %X\n", header);
        return AVERROR_PATCHWELCOME;
    }

    s->flags = 0;
    w = bytestream2_get_le32(gbyte);
    h = bytestream2_get_le32(gbyte);
    if (w == INT32_MIN || h == INT32_MIN)
        return AVERROR_INVALIDDATA;
    if (w < 0) {
        w = -w;
        s->flags |= 2;
    }
    if (h < 0) {
        h = -h;
        s->flags |= 1;
    }

    width  = avctx->width;
    height = avctx->height;
    if (w < width || h < height || w & 7 || h & 7)
        return AVERROR_INVALIDDATA;

    ret = ff_set_dimensions(avctx, w, h);
    if (ret < 0)
        return ret;
    avctx->width = width;
    avctx->height = height;

    s->compression = bytestream2_get_le32(gbyte);
    if (s->compression < 0 || s->compression > 100)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < 3; i++)
        s->size[i] = bytestream2_get_le32(gbyte);
    if (s->size[0] < 0 || s->size[1] < 0 || s->size[2] < 0 ||
        32LL + s->size[0] + s->size[1] + s->size[2] > avpkt->size)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    if (frame->key_frame) {
        ret = decode_intra(avctx, gb, frame);
    } else {
        if (!s->prev_frame->data[0]) {
            av_log(avctx, AV_LOG_ERROR, "Missing reference frame.\n");
            return AVERROR_INVALIDDATA;
        }

        if (!(s->flags & 2)) {
            ret = av_frame_copy(frame, s->prev_frame);
            if (ret < 0)
                return ret;
        }

        ret = decode_inter(avctx, gb, frame, s->prev_frame);
    }
    if (ret < 0)
        return ret;

    av_frame_unref(s->prev_frame);
    if ((ret = av_frame_ref(s->prev_frame, frame)) < 0)
        return ret;

    frame->crop_top  = avctx->coded_height - avctx->height;
    frame->crop_left = avctx->coded_width  - avctx->width;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    AGMContext *s = avctx->priv_data;

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    s->avctx = avctx;
    s->plus = avctx->codec_tag == MKTAG('A', 'G', 'M', '3');

    avctx->idct_algo = FF_IDCT_SIMPLE;
    ff_idctdsp_init(&s->idsp, avctx);
    ff_init_scantable(s->idsp.idct_permutation, &s->scantable, ff_zigzag_direct);

    s->prev_frame = av_frame_alloc();
    if (!s->prev_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static void decode_flush(AVCodecContext *avctx)
{
    AGMContext *s = avctx->priv_data;

    av_frame_unref(s->prev_frame);
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    AGMContext *s = avctx->priv_data;

    av_frame_free(&s->prev_frame);
    av_freep(&s->mvectors);
    s->mvectors_size = 0;

    return 0;
}

AVCodec ff_agm_decoder = {
    .name             = "agm",
    .long_name        = NULL_IF_CONFIG_SMALL("Amuse Graphics Movie"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_AGM,
    .priv_data_size   = sizeof(AGMContext),
    .init             = decode_init,
    .close            = decode_close,
    .decode           = decode_frame,
    .flush            = decode_flush,
    .capabilities     = AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE |
                        FF_CODEC_CAP_INIT_CLEANUP |
                        FF_CODEC_CAP_EXPORTS_CROPPING,
};
