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

#include <string.h>

#define BITSTREAM_READER_LE

#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "copy_block.h"
#include "decode.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "jpegquanttables.h"

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
    int dct;
    int rgb;
    unsigned flags;
    unsigned fflags;

    uint8_t *output;
    unsigned padded_output_size;
    unsigned output_size;

    MotionVector *mvectors;
    unsigned      mvectors_size;

    VLC vlc;

    AVFrame *prev_frame;

    int luma_quant_matrix[64];
    int chroma_quant_matrix[64];

    uint8_t permutated_scantable[64];
    DECLARE_ALIGNED(32, int16_t, block)[64];

    int16_t *wblocks;
    unsigned wblocks_size;

    int      *map;
    unsigned  map_size;

    IDCTDSPContext idsp;
} AGMContext;

static int read_code(GetBitContext *gb, int *oskip, int *level, int *map, int mode)
{
    int len = 0, skip = 0, max;

    if (get_bits_left(gb) < 2)
        return AVERROR_INVALIDDATA;

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

static int decode_intra_blocks(AGMContext *s, GetBitContext *gb,
                               const int *quant_matrix, int *skip, int *dc_level)
{
    const uint8_t *scantable = s->permutated_scantable;
    int level, ret, map = 0;

    memset(s->wblocks, 0, s->wblocks_size);

    for (int i = 0; i < 64; i++) {
        int16_t *block = s->wblocks + scantable[i];

        for (int j = 0; j < s->blocks_w;) {
            if (*skip > 0) {
                int rskip;

                rskip = FFMIN(*skip, s->blocks_w - j);
                j += rskip;
                if (i == 0) {
                    for (int k = 0; k < rskip; k++)
                        block[64 * k] = *dc_level * quant_matrix[0];
                }
                block += rskip * 64;
                *skip -= rskip;
            } else {
                ret = read_code(gb, skip, &level, &map, s->flags & 1);
                if (ret < 0)
                    return ret;

                if (i == 0)
                    *dc_level += level;

                block[0] = (i == 0 ? *dc_level : level) * quant_matrix[i];
                block += 64;
                j++;
            }
        }
    }

    return 0;
}

static int decode_inter_blocks(AGMContext *s, GetBitContext *gb,
                               const int *quant_matrix, int *skip,
                               int *map)
{
    const uint8_t *scantable = s->permutated_scantable;
    int level, ret;

    memset(s->wblocks, 0, s->wblocks_size);
    memset(s->map, 0, s->map_size);

    for (int i = 0; i < 64; i++) {
        int16_t *block = s->wblocks + scantable[i];

        for (int j = 0; j < s->blocks_w;) {
            if (*skip > 0) {
                int rskip;

                rskip = FFMIN(*skip, s->blocks_w - j);
                j += rskip;
                block += rskip * 64;
                *skip -= rskip;
            } else {
                ret = read_code(gb, skip, &level, &map[j], s->flags & 1);
                if (ret < 0)
                    return ret;

                block[0] = level * quant_matrix[i];
                block += 64;
                j++;
            }
        }
    }

    return 0;
}

static int decode_intra_block(AGMContext *s, GetBitContext *gb,
                              const int *quant_matrix, int *skip, int *dc_level)
{
    const uint8_t *scantable = s->permutated_scantable;
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
    const int offset = s->plus ? 0 : 1024;

    if ((ret = init_get_bits8(gb, s->gbyte.buffer, size)) < 0)
        return ret;

    if (s->flags & 1) {
        av_fast_padded_malloc(&s->wblocks, &s->wblocks_size,
                              64 * s->blocks_w * sizeof(*s->wblocks));
        if (!s->wblocks)
            return AVERROR(ENOMEM);

        for (int y = 0; y < s->blocks_h; y++) {
            ret = decode_intra_blocks(s, gb, quant_matrix, &skip, &dc_level);
            if (ret < 0)
                return ret;

            for (int x = 0; x < s->blocks_w; x++) {
                s->wblocks[64 * x] += offset;
                s->idsp.idct_put(frame->data[plane] + (s->blocks_h - 1 - y) * 8 * frame->linesize[plane] + x * 8,
                                 frame->linesize[plane], s->wblocks + 64 * x);
            }
        }
    } else {
        for (int y = 0; y < s->blocks_h; y++) {
            for (int x = 0; x < s->blocks_w; x++) {
                ret = decode_intra_block(s, gb, quant_matrix, &skip, &dc_level);
                if (ret < 0)
                    return ret;

                s->idsp.idct_put(frame->data[plane] + (s->blocks_h - 1 - y) * 8 * frame->linesize[plane] + x * 8,
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

static int decode_inter_block(AGMContext *s, GetBitContext *gb,
                              const int *quant_matrix, int *skip,
                              int *map)
{
    const uint8_t *scantable = s->permutated_scantable;
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

    if (s->flags == 3) {
        av_fast_padded_malloc(&s->wblocks, &s->wblocks_size,
                              64 * s->blocks_w * sizeof(*s->wblocks));
        if (!s->wblocks)
            return AVERROR(ENOMEM);

        av_fast_padded_malloc(&s->map, &s->map_size,
                              s->blocks_w * sizeof(*s->map));
        if (!s->map)
            return AVERROR(ENOMEM);

        for (int y = 0; y < s->blocks_h; y++) {
            ret = decode_inter_blocks(s, gb, quant_matrix, &skip, s->map);
            if (ret < 0)
                return ret;

            for (int x = 0; x < s->blocks_w; x++) {
                int shift = plane == 0;
                int mvpos = (y >> shift) * (s->blocks_w >> shift) + (x >> shift);
                int orig_mv_x = s->mvectors[mvpos].x;
                int mv_x = s->mvectors[mvpos].x / (1 + !shift);
                int mv_y = s->mvectors[mvpos].y / (1 + !shift);
                int h = s->avctx->coded_height >> !shift;
                int w = s->avctx->coded_width  >> !shift;
                int map = s->map[x];

                if (orig_mv_x >= -32) {
                    if (y * 8 + mv_y < 0 || y * 8 + mv_y + 8 > h ||
                        x * 8 + mv_x < 0 || x * 8 + mv_x + 8 > w)
                        return AVERROR_INVALIDDATA;

                    copy_block8(frame->data[plane] + (s->blocks_h - 1 - y) * 8 * frame->linesize[plane] + x * 8,
                                prev->data[plane] + ((s->blocks_h - 1 - y) * 8 - mv_y) * prev->linesize[plane] + (x * 8 + mv_x),
                                frame->linesize[plane], prev->linesize[plane], 8);
                    if (map) {
                        s->idsp.idct(s->wblocks + x * 64);
                        for (int i = 0; i < 64; i++)
                            s->wblocks[i + x * 64] = (s->wblocks[i + x * 64] + 1) & 0xFFFC;
                        s->idsp.add_pixels_clamped(&s->wblocks[x*64], frame->data[plane] + (s->blocks_h - 1 - y) * 8 * frame->linesize[plane] + x * 8,
                                                   frame->linesize[plane]);
                    }
                } else if (map) {
                    s->idsp.idct_put(frame->data[plane] + (s->blocks_h - 1 - y) * 8 * frame->linesize[plane] + x * 8,
                                     frame->linesize[plane], s->wblocks + x * 64);
                }
            }
        }
    } else if (s->flags & 2) {
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

                ret = decode_inter_block(s, gb, quant_matrix, &skip, &map);
                if (ret < 0)
                    return ret;

                if (orig_mv_x >= -32) {
                    if (y * 8 + mv_y < 0 || y * 8 + mv_y + 8 > h ||
                        x * 8 + mv_x < 0 || x * 8 + mv_x + 8 > w)
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
    } else if (s->flags & 1) {
        av_fast_padded_malloc(&s->wblocks, &s->wblocks_size,
                              64 * s->blocks_w * sizeof(*s->wblocks));
        if (!s->wblocks)
            return AVERROR(ENOMEM);

        av_fast_padded_malloc(&s->map, &s->map_size,
                              s->blocks_w * sizeof(*s->map));
        if (!s->map)
            return AVERROR(ENOMEM);

        for (int y = 0; y < s->blocks_h; y++) {
            ret = decode_inter_blocks(s, gb, quant_matrix, &skip, s->map);
            if (ret < 0)
                return ret;

            for (int x = 0; x < s->blocks_w; x++) {
                if (!s->map[x])
                    continue;
                s->idsp.idct_add(frame->data[plane] + (s->blocks_h - 1 - y) * 8 * frame->linesize[plane] + x * 8,
                                 frame->linesize[plane], s->wblocks + 64 * x);
            }
        }
    } else {
        for (int y = 0; y < s->blocks_h; y++) {
            for (int x = 0; x < s->blocks_w; x++) {
                int map = 0;

                ret = decode_inter_block(s, gb, quant_matrix, &skip, &map);
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
                luma[i]   = FFMAX(1, ff_mjpeg_std_luminance_quant_tbl  [(i & 7) * 8 + (i >> 3)] * f);
                chroma[i] = FFMAX(1, ff_mjpeg_std_chrominance_quant_tbl[(i & 7) * 8 + (i >> 3)] * f);
            }
        } else {
            for (int i = 0; i < 64; i++) {
                luma[i]   = FFMAX(1, 255.0 - (255 - ff_mjpeg_std_luminance_quant_tbl  [(i & 7) * 8 + (i >> 3)]) * f);
                chroma[i] = FFMAX(1, 255.0 - (255 - ff_mjpeg_std_chrominance_quant_tbl[(i & 7) * 8 + (i >> 3)]) * f);
            }
        }
    }

    for (int i = 0; i < 64; i++) {
        int pos = ff_zigzag_direct[i];

        s->luma_quant_matrix[i]   = luma[pos]   * ((pos / 8) & 1 ? -1 : 1);
        s->chroma_quant_matrix[i] = chroma[pos] * ((pos / 8) & 1 ? -1 : 1);
    }
}

static int decode_raw_intra_rgb(AVCodecContext *avctx, GetByteContext *gbyte, AVFrame *frame)
{
    uint8_t *dst = frame->data[0] + (avctx->height - 1) * frame->linesize[0];
    uint8_t r = 0, g = 0, b = 0;

    if (bytestream2_get_bytes_left(gbyte) < 3 * avctx->width * avctx->height)
        return AVERROR_INVALIDDATA;

    for (int y = 0; y < avctx->height; y++) {
        for (int x = 0; x < avctx->width; x++) {
            dst[x*3+0] = bytestream2_get_byteu(gbyte) + r;
            r = dst[x*3+0];
            dst[x*3+1] = bytestream2_get_byteu(gbyte) + g;
            g = dst[x*3+1];
            dst[x*3+2] = bytestream2_get_byteu(gbyte) + b;
            b = dst[x*3+2];
        }
        dst -= frame->linesize[0];
    }

    return 0;
}

av_always_inline static int fill_pixels(uint8_t **y0, uint8_t **y1,
                       uint8_t **u, uint8_t **v,
                       int ylinesize, int ulinesize, int vlinesize,
                       uint8_t *fill,
                       int *nx, int *ny, int *np, int w, int h)
{
    uint8_t *y0dst = *y0;
    uint8_t *y1dst = *y1;
    uint8_t *udst = *u;
    uint8_t *vdst = *v;
    int x = *nx, y = *ny, pos = *np;

    if (pos == 0) {
        y0dst[2*x+0] += fill[0];
        y0dst[2*x+1] += fill[1];
        y1dst[2*x+0] += fill[2];
        y1dst[2*x+1] += fill[3];
        pos++;
    } else if (pos == 1) {
        udst[x] += fill[0];
        vdst[x] += fill[1];
        x++;
        if (x >= w) {
            x = 0;
            y++;
            if (y >= h)
                return 1;
            y0dst -= 2*ylinesize;
            y1dst -= 2*ylinesize;
            udst  -=   ulinesize;
            vdst  -=   vlinesize;
        }
        y0dst[2*x+0] += fill[2];
        y0dst[2*x+1] += fill[3];
        pos++;
    } else if (pos == 2) {
        y1dst[2*x+0] += fill[0];
        y1dst[2*x+1] += fill[1];
        udst[x]      += fill[2];
        vdst[x]      += fill[3];
        x++;
        if (x >= w) {
            x = 0;
            y++;
            if (y >= h)
                return 1;
            y0dst -= 2*ylinesize;
            y1dst -= 2*ylinesize;
            udst  -=   ulinesize;
            vdst  -=   vlinesize;
        }
        pos = 0;
    }

    *y0 = y0dst;
    *y1 = y1dst;
    *u = udst;
    *v = vdst;
    *np = pos;
    *nx = x;
    *ny = y;

    return 0;
}

static int decode_runlen_rgb(AVCodecContext *avctx, GetByteContext *gbyte, AVFrame *frame)
{
    uint8_t *dst = frame->data[0] + (avctx->height - 1) * frame->linesize[0];
    int runlen, y = 0, x = 0;
    uint8_t fill[4];
    unsigned code;

    while (bytestream2_get_bytes_left(gbyte) > 0) {
        code = bytestream2_peek_le32(gbyte);
        runlen = code & 0xFFFFFF;

        if (code >> 24 == 0x77) {
            bytestream2_skip(gbyte, 4);

            for (int i = 0; i < 4; i++)
                fill[i] = bytestream2_get_byte(gbyte);

            while (runlen > 0) {
                runlen--;

                for (int i = 0; i < 4; i++) {
                    dst[x] += fill[i];
                    x++;
                    if (x >= frame->width * 3) {
                        x = 0;
                        y++;
                        dst -= frame->linesize[0];
                        if (y >= frame->height)
                            return 0;
                    }
                }
            }
        } else {
            for (int i = 0; i < 4; i++)
                fill[i] = bytestream2_get_byte(gbyte);

            for (int i = 0; i < 4; i++) {
                dst[x] += fill[i];
                x++;
                if (x >= frame->width * 3) {
                    x = 0;
                    y++;
                    dst -= frame->linesize[0];
                    if (y >= frame->height)
                        return 0;
                }
            }
        }
    }

    return 0;
}

static int decode_runlen(AVCodecContext *avctx, GetByteContext *gbyte, AVFrame *frame)
{
    uint8_t *y0dst = frame->data[0] + (avctx->height - 1) * frame->linesize[0];
    uint8_t *y1dst = y0dst - frame->linesize[0];
    uint8_t *udst = frame->data[1] + ((avctx->height >> 1) - 1) * frame->linesize[1];
    uint8_t *vdst = frame->data[2] + ((avctx->height >> 1) - 1) * frame->linesize[2];
    int runlen, y = 0, x = 0, pos = 0;
    uint8_t fill[4];
    unsigned code;

    while (bytestream2_get_bytes_left(gbyte) > 0) {
        code = bytestream2_peek_le32(gbyte);
        runlen = code & 0xFFFFFF;

        if (code >> 24 == 0x77) {
            bytestream2_skip(gbyte, 4);

            for (int i = 0; i < 4; i++)
                fill[i] = bytestream2_get_byte(gbyte);

            while (runlen > 0) {
                runlen--;

                if (fill_pixels(&y0dst, &y1dst, &udst, &vdst,
                                frame->linesize[0],
                                frame->linesize[1],
                                frame->linesize[2],
                                fill, &x, &y, &pos,
                                avctx->width / 2,
                                avctx->height / 2))
                    return 0;
            }
        } else {
            for (int i = 0; i < 4; i++)
                fill[i] = bytestream2_get_byte(gbyte);

            if (fill_pixels(&y0dst, &y1dst, &udst, &vdst,
                            frame->linesize[0],
                            frame->linesize[1],
                            frame->linesize[2],
                            fill, &x, &y, &pos,
                            avctx->width / 2,
                            avctx->height / 2))
                return 0;
        }
    }

    return 0;
}

static int decode_raw_intra(AVCodecContext *avctx, GetByteContext *gbyte, AVFrame *frame)
{
    uint8_t *y0dst = frame->data[0] + (avctx->height - 1) * frame->linesize[0];
    uint8_t *y1dst = y0dst - frame->linesize[0];
    uint8_t *udst = frame->data[1] + ((avctx->height >> 1) - 1) * frame->linesize[1];
    uint8_t *vdst = frame->data[2] + ((avctx->height >> 1) - 1) * frame->linesize[2];
    uint8_t ly0 = 0, ly1 = 0, ly2 = 0, ly3 = 0, lu = 0, lv = 0;

    for (int y = 0; y < avctx->height / 2; y++) {
        for (int x = 0; x < avctx->width / 2; x++) {
            y0dst[x*2+0] = bytestream2_get_byte(gbyte) + ly0;
            ly0 = y0dst[x*2+0];
            y0dst[x*2+1] = bytestream2_get_byte(gbyte) + ly1;
            ly1 = y0dst[x*2+1];
            y1dst[x*2+0] = bytestream2_get_byte(gbyte) + ly2;
            ly2 = y1dst[x*2+0];
            y1dst[x*2+1] = bytestream2_get_byte(gbyte) + ly3;
            ly3 = y1dst[x*2+1];
            udst[x] = bytestream2_get_byte(gbyte) + lu;
            lu = udst[x];
            vdst[x] = bytestream2_get_byte(gbyte) + lv;
            lv = vdst[x];
        }

        y0dst -= 2*frame->linesize[0];
        y1dst -= 2*frame->linesize[0];
        udst  -= frame->linesize[1];
        vdst  -= frame->linesize[2];
    }

    return 0;
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
    int nb_mvs = ((avctx->coded_height + 15) >> 4) * ((avctx->coded_width + 15) >> 4);
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

typedef struct Node {
    int parent;
    int child[2];
} Node;

static void get_tree_codes(uint32_t *codes, Node *nodes, int idx, uint32_t pfx, int bitpos)
{
    if (idx < 256 && idx >= 0) {
        codes[idx] = pfx;
    } else if (idx >= 0) {
        get_tree_codes(codes, nodes, nodes[idx].child[0], pfx + (0 << bitpos), bitpos + 1);
        get_tree_codes(codes, nodes, nodes[idx].child[1], pfx + (1U << bitpos), bitpos + 1);
    }
}

static int make_new_tree(const uint8_t *bitlens, uint32_t *codes)
{
    int zlcount = 0, curlen, idx, nindex, last, llast;
    int blcounts[32] = { 0 };
    int syms[8192];
    Node nodes[512];
    int node_idx[1024];
    int old_idx[512];

    for (int i = 0; i < 256; i++) {
        int bitlen = bitlens[i];
        int blcount = blcounts[bitlen];

        zlcount += bitlen < 1;
        syms[(bitlen << 8) + blcount] = i;
        blcounts[bitlen]++;
    }

    for (int i = 0; i < 512; i++) {
        nodes[i].child[0] = -1;
        nodes[i].child[1] = -1;
    }

    for (int i = 0; i < 256; i++) {
        node_idx[i] = 257 + i;
    }

    curlen = 1;
    node_idx[512] = 256;
    last = 255;
    nindex = 1;

    for (curlen = 1; curlen < 32; curlen++) {
        if (blcounts[curlen] > 0) {
            int max_zlcount = zlcount + blcounts[curlen];

            for (int i = 0; zlcount < 256 && zlcount < max_zlcount; zlcount++, i++) {
                int p = node_idx[nindex - 1 + 512];
                int ch = syms[256 * curlen + i];

                if (nindex <= 0)
                    return AVERROR_INVALIDDATA;

                if (nodes[p].child[0] == -1) {
                    nodes[p].child[0] = ch;
                } else {
                    nodes[p].child[1] = ch;
                    nindex--;
                }
                nodes[ch].parent = p;
            }
        }
        llast = last - 1;
        idx = 0;
        while (nindex > 0) {
            int p, ch;

            last = llast - idx;
            p = node_idx[nindex - 1 + 512];
            ch = node_idx[last];
            if (nodes[p].child[0] == -1) {
                nodes[p].child[0] = ch;
            } else {
                nodes[p].child[1] = ch;
                nindex--;
            }
            old_idx[idx] = ch;
            nodes[ch].parent = p;
            if (idx == llast)
                goto next;
            idx++;
            if (nindex <= 0) {
                for (int i = 0; i < idx; i++)
                    node_idx[512 + i] = old_idx[i];
            }
        }
        nindex = idx;
    }

next:

    get_tree_codes(codes, nodes, 256, 0, 0);
    return 0;
}

static int build_huff(const uint8_t *bitlen, VLC *vlc)
{
    uint32_t new_codes[256];
    uint8_t bits[256];
    uint8_t symbols[256];
    uint32_t codes[256];
    int nb_codes = 0;

    int ret = make_new_tree(bitlen, new_codes);
    if (ret < 0)
        return ret;

    for (int i = 0; i < 256; i++) {
        if (bitlen[i]) {
            bits[nb_codes] = bitlen[i];
            codes[nb_codes] = new_codes[i];
            symbols[nb_codes] = i;
            nb_codes++;
        }
    }

    ff_vlc_free(vlc);
    return ff_vlc_init_sparse(vlc, 13, nb_codes,
                              bits, 1, 1,
                              codes, 4, 4,
                              symbols, 1, 1,
                              VLC_INIT_LE);
}

static int decode_huffman2(AVCodecContext *avctx, int header, int size)
{
    AGMContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    uint8_t lens[256];
    int ret, x, len;

    if ((ret = init_get_bits8(gb, s->gbyte.buffer,
                              bytestream2_get_bytes_left(&s->gbyte))) < 0)
        return ret;

    s->output_size = get_bits_long(gb, 32);

    if (s->output_size > avctx->width * avctx->height * 9LL + 10000)
        return AVERROR_INVALIDDATA;

    av_fast_padded_malloc(&s->output, &s->padded_output_size, s->output_size);
    if (!s->output)
        return AVERROR(ENOMEM);

    x = get_bits(gb, 1);
    len = 4 + get_bits(gb, 1);
    if (x) {
        int cb[8] = { 0 };
        int count = get_bits(gb, 3) + 1;

        for (int i = 0; i < count; i++)
            cb[i] = get_bits(gb, len);

        for (int i = 0; i < 256; i++) {
            int idx = get_bits(gb, 3);
            lens[i] = cb[idx];
        }
    } else {
        for (int i = 0; i < 256; i++)
            lens[i] = get_bits(gb, len);
    }

    if ((ret = build_huff(lens, &s->vlc)) < 0)
        return ret;

    x = 0;
    while (get_bits_left(gb) > 0 && x < s->output_size) {
        int val = get_vlc2(gb, s->vlc.table, s->vlc.bits, 3);
        if (val < 0)
            return AVERROR_INVALIDDATA;
        s->output[x++] = val;
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    AGMContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    GetByteContext *gbyte = &s->gbyte;
    int w, h, width, height, header;
    unsigned compressed_size;
    long skip;
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

    s->key_frame = (avpkt->flags & AV_PKT_FLAG_KEY);
    if (s->key_frame)
        frame->flags |= AV_FRAME_FLAG_KEY;
    else
        frame->flags &= ~AV_FRAME_FLAG_KEY;
    frame->pict_type = s->key_frame ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    if (!s->key_frame) {
        if (!s->prev_frame->data[0]) {
            av_log(avctx, AV_LOG_ERROR, "Missing reference frame.\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if (header) {
        if (avctx->codec_tag == MKTAG('A', 'G', 'M', '0') ||
            avctx->codec_tag == MKTAG('A', 'G', 'M', '1'))
            return AVERROR_PATCHWELCOME;
        else
            ret = decode_huffman2(avctx, header, (avpkt->size - s->bitstream_size) - 8);
        if (ret < 0)
            return ret;
        bytestream2_init(gbyte, s->output, s->output_size);
    } else if (!s->dct) {
        bytestream2_skip(gbyte, 4);
    }

    if (s->dct) {
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
        if (header) {
            compressed_size = s->output_size;
            skip = 8LL;
        } else {
            compressed_size = avpkt->size;
            skip = 32LL;
        }
        if (s->size[0] < 0 || s->size[1] < 0 || s->size[2] < 0 ||
            skip + s->size[0] + s->size[1] + s->size[2] > compressed_size) {
            return AVERROR_INVALIDDATA;
        }
    }

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    if (frame->flags & AV_FRAME_FLAG_KEY) {
        if (!s->dct && !s->rgb)
            ret = decode_raw_intra(avctx, gbyte, frame);
        else if (!s->dct && s->rgb)
            ret = decode_raw_intra_rgb(avctx, gbyte, frame);
        else
            ret = decode_intra(avctx, gb, frame);
    } else {
        if (s->prev_frame-> width != frame->width ||
            s->prev_frame->height != frame->height)
            return AVERROR_INVALIDDATA;

        if (!(s->flags & 2)) {
            ret = av_frame_copy(frame, s->prev_frame);
            if (ret < 0)
                return ret;
        }

        if (s->dct) {
            ret = decode_inter(avctx, gb, frame, s->prev_frame);
        } else if (!s->dct && !s->rgb) {
            ret = decode_runlen(avctx, gbyte, frame);
        } else {
            ret = decode_runlen_rgb(avctx, gbyte, frame);
        }
    }
    if (ret < 0)
        return ret;

    if ((ret = av_frame_replace(s->prev_frame, frame)) < 0)
        return ret;

    frame->crop_top  = avctx->coded_height - avctx->height;
    frame->crop_left = avctx->coded_width  - avctx->width;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    AGMContext *s = avctx->priv_data;

    s->rgb = avctx->codec_tag == MKTAG('A', 'G', 'M', '4');
    avctx->pix_fmt = s->rgb ? AV_PIX_FMT_BGR24 : AV_PIX_FMT_YUV420P;
    s->avctx = avctx;
    s->plus = avctx->codec_tag == MKTAG('A', 'G', 'M', '3') ||
              avctx->codec_tag == MKTAG('A', 'G', 'M', '7');

    s->dct = avctx->codec_tag != MKTAG('A', 'G', 'M', '4') &&
             avctx->codec_tag != MKTAG('A', 'G', 'M', '5');

    if (!s->rgb && !s->dct) {
        if ((avctx->width & 1) || (avctx->height & 1))
            return AVERROR_INVALIDDATA;
    }

    avctx->idct_algo = FF_IDCT_SIMPLE;
    ff_idctdsp_init(&s->idsp, avctx);
    ff_permute_scantable(s->permutated_scantable, ff_zigzag_direct,
                         s->idsp.idct_permutation);

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

    ff_vlc_free(&s->vlc);
    av_frame_free(&s->prev_frame);
    av_freep(&s->mvectors);
    s->mvectors_size = 0;
    av_freep(&s->wblocks);
    s->wblocks_size = 0;
    av_freep(&s->output);
    s->padded_output_size = 0;
    av_freep(&s->map);
    s->map_size = 0;

    return 0;
}

const FFCodec ff_agm_decoder = {
    .p.name           = "agm",
    CODEC_LONG_NAME("Amuse Graphics Movie"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_AGM,
    .p.capabilities   = AV_CODEC_CAP_DR1,
    .priv_data_size   = sizeof(AGMContext),
    .init             = decode_init,
    .close            = decode_close,
    FF_CODEC_DECODE_CB(decode_frame),
    .flush            = decode_flush,
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP |
                        FF_CODEC_CAP_EXPORTS_CROPPING,
};
