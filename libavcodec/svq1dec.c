/*
 * SVQ1 decoder
 * ported to MPlayer by Arpi <arpi@thot.banki.hu>
 * ported to libavcodec by Nick Kurshev <nickols_k@mail.ru>
 *
 * Copyright (c) 2002 The Xine Project
 * Copyright (c) 2002 The FFmpeg Project
 *
 * SVQ1 Encoder (c) 2004 Mike Melanson <melanson@pcisys.net>
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
 * Sorenson Vector Quantizer #1 (SVQ1) video codec.
 * For more information of the SVQ1 algorithm, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include "avcodec.h"
#include "get_bits.h"
#include "h263.h"
#include "hpeldsp.h"
#include "internal.h"
#include "mathops.h"
#include "svq1.h"

#undef NDEBUG
#include <assert.h>

static VLC svq1_block_type;
static VLC svq1_motion_component;
static VLC svq1_intra_multistage[6];
static VLC svq1_inter_multistage[6];
static VLC svq1_intra_mean;
static VLC svq1_inter_mean;

/* motion vector (prediction) */
typedef struct svq1_pmv_s {
    int x;
    int y;
} svq1_pmv;

typedef struct SVQ1Context {
    HpelDSPContext hdsp;
    GetBitContext gb;
    AVFrame *prev;

    uint8_t *pkt_swapped;
    int pkt_swapped_allocated;

    int width;
    int height;
    int frame_code;
    int nonref;         // 1 if the current frame won't be referenced
} SVQ1Context;

static const uint8_t string_table[256] = {
    0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54,
    0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
    0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06,
    0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
    0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0,
    0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
    0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2,
    0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
    0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9,
    0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
    0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B,
    0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
    0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D,
    0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
    0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F,
    0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
    0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB,
    0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
    0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9,
    0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
    0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F,
    0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
    0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D,
    0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
    0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26,
    0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
    0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74,
    0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
    0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82,
    0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
    0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0,
    0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9
};

#define SVQ1_PROCESS_VECTOR()                                           \
    for (; level > 0; i++) {                                            \
        /* process next depth */                                        \
        if (i == m) {                                                   \
            m = n;                                                      \
            if (--level == 0)                                           \
                break;                                                  \
        }                                                               \
        /* divide block if next bit set */                              \
        if (!get_bits1(bitbuf))                                         \
            break;                                                      \
        /* add child nodes */                                           \
        list[n++] = list[i];                                            \
        list[n++] = list[i] + (((level & 1) ? pitch : 1) << ((level >> 1) + 1));\
    }

#define SVQ1_ADD_CODEBOOK()                                             \
    /* add codebook entries to vector */                                \
    for (j = 0; j < stages; j++) {                                      \
        n3  = codebook[entries[j]] ^ 0x80808080;                        \
        n1 += (n3 & 0xFF00FF00) >> 8;                                   \
        n2 +=  n3 & 0x00FF00FF;                                         \
    }                                                                   \
                                                                        \
    /* clip to [0..255] */                                              \
    if (n1 & 0xFF00FF00) {                                              \
        n3  = (n1 >> 15  & 0x00010001 | 0x01000100) - 0x00010001;       \
        n1 += 0x7F007F00;                                               \
        n1 |= (~n1 >> 15 & 0x00010001 | 0x01000100) - 0x00010001;       \
        n1 &= n3 & 0x00FF00FF;                                          \
    }                                                                   \
                                                                        \
    if (n2 & 0xFF00FF00) {                                              \
        n3  = (n2 >> 15  & 0x00010001 | 0x01000100) - 0x00010001;       \
        n2 += 0x7F007F00;                                               \
        n2 |= (~n2 >> 15 & 0x00010001 | 0x01000100) - 0x00010001;       \
        n2 &= n3 & 0x00FF00FF;                                          \
    }

#define SVQ1_CALC_CODEBOOK_ENTRIES(cbook)                               \
    codebook = (const uint32_t *)cbook[level];                          \
    if (stages > 0)                                                     \
        bit_cache = get_bits(bitbuf, 4 * stages);                       \
    /* calculate codebook entries for this vector */                    \
    for (j = 0; j < stages; j++) {                                      \
        entries[j] = (((bit_cache >> (4 * (stages - j - 1))) & 0xF) +   \
                      16 * j) << (level + 1);                           \
    }                                                                   \
    mean -= stages * 128;                                               \
    n4    = (mean << 16) + mean;

static int svq1_decode_block_intra(GetBitContext *bitbuf, uint8_t *pixels,
                                   int pitch)
{
    uint32_t bit_cache;
    uint8_t *list[63];
    uint32_t *dst;
    const uint32_t *codebook;
    int entries[6];
    int i, j, m, n;
    int mean, stages;
    unsigned x, y, width, height, level;
    uint32_t n1, n2, n3, n4;

    /* initialize list for breadth first processing of vectors */
    list[0] = pixels;

    /* recursively process vector */
    for (i = 0, m = 1, n = 1, level = 5; i < n; i++) {
        SVQ1_PROCESS_VECTOR();

        /* destination address and vector size */
        dst    = (uint32_t *)list[i];
        width  = 1 << ((4 + level) / 2);
        height = 1 << ((3 + level) / 2);

        /* get number of stages (-1 skips vector, 0 for mean only) */
        stages = get_vlc2(bitbuf, svq1_intra_multistage[level].table, 3, 3) - 1;

        if (stages == -1) {
            for (y = 0; y < height; y++)
                memset(&dst[y * (pitch / 4)], 0, width);
            continue;   /* skip vector */
        }

        if (stages > 0 && level >= 4) {
            av_dlog(NULL,
                    "Error (svq1_decode_block_intra): invalid vector: stages=%i level=%i\n",
                    stages, level);
            return AVERROR_INVALIDDATA;  /* invalid vector */
        }

        mean = get_vlc2(bitbuf, svq1_intra_mean.table, 8, 3);

        if (stages == 0) {
            for (y = 0; y < height; y++)
                memset(&dst[y * (pitch / 4)], mean, width);
        } else {
            SVQ1_CALC_CODEBOOK_ENTRIES(ff_svq1_intra_codebooks);

            for (y = 0; y < height; y++) {
                for (x = 0; x < width / 4; x++, codebook++) {
                    n1 = n4;
                    n2 = n4;
                    SVQ1_ADD_CODEBOOK()
                    /* store result */
                    dst[x] = n1 << 8 | n2;
                }
                dst += pitch / 4;
            }
        }
    }

    return 0;
}

static int svq1_decode_block_non_intra(GetBitContext *bitbuf, uint8_t *pixels,
                                       int pitch)
{
    uint32_t bit_cache;
    uint8_t *list[63];
    uint32_t *dst;
    const uint32_t *codebook;
    int entries[6];
    int i, j, m, n;
    int mean, stages;
    int x, y, width, height, level;
    uint32_t n1, n2, n3, n4;

    /* initialize list for breadth first processing of vectors */
    list[0] = pixels;

    /* recursively process vector */
    for (i = 0, m = 1, n = 1, level = 5; i < n; i++) {
        SVQ1_PROCESS_VECTOR();

        /* destination address and vector size */
        dst    = (uint32_t *)list[i];
        width  = 1 << ((4 + level) / 2);
        height = 1 << ((3 + level) / 2);

        /* get number of stages (-1 skips vector, 0 for mean only) */
        stages = get_vlc2(bitbuf, svq1_inter_multistage[level].table, 3, 2) - 1;

        if (stages == -1)
            continue;           /* skip vector */

        if ((stages > 0) && (level >= 4)) {
            av_dlog(NULL,
                    "Error (svq1_decode_block_non_intra): invalid vector: stages=%i level=%i\n",
                    stages, level);
            return AVERROR_INVALIDDATA;  /* invalid vector */
        }

        mean = get_vlc2(bitbuf, svq1_inter_mean.table, 9, 3) - 256;

        SVQ1_CALC_CODEBOOK_ENTRIES(ff_svq1_inter_codebooks);

        for (y = 0; y < height; y++) {
            for (x = 0; x < width / 4; x++, codebook++) {
                n3 = dst[x];
                /* add mean value to vector */
                n1 = n4 + ((n3 & 0xFF00FF00) >> 8);
                n2 = n4 +  (n3 & 0x00FF00FF);
                SVQ1_ADD_CODEBOOK()
                /* store result */
                dst[x] = n1 << 8 | n2;
            }
            dst += pitch / 4;
        }
    }
    return 0;
}

static int svq1_decode_motion_vector(GetBitContext *bitbuf, svq1_pmv *mv,
                                     svq1_pmv **pmv)
{
    int diff;
    int i;

    for (i = 0; i < 2; i++) {
        /* get motion code */
        diff = get_vlc2(bitbuf, svq1_motion_component.table, 7, 2);
        if (diff < 0)
            return AVERROR_INVALIDDATA;
        else if (diff) {
            if (get_bits1(bitbuf))
                diff = -diff;
        }

        /* add median of motion vector predictors and clip result */
        if (i == 1)
            mv->y = sign_extend(diff + mid_pred(pmv[0]->y, pmv[1]->y, pmv[2]->y), 6);
        else
            mv->x = sign_extend(diff + mid_pred(pmv[0]->x, pmv[1]->x, pmv[2]->x), 6);
    }

    return 0;
}

static void svq1_skip_block(uint8_t *current, uint8_t *previous,
                            int pitch, int x, int y)
{
    uint8_t *src;
    uint8_t *dst;
    int i;

    src = &previous[x + y * pitch];
    dst = current;

    for (i = 0; i < 16; i++) {
        memcpy(dst, src, 16);
        src += pitch;
        dst += pitch;
    }
}

static int svq1_motion_inter_block(HpelDSPContext *hdsp, GetBitContext *bitbuf,
                                   uint8_t *current, uint8_t *previous,
                                   int pitch, svq1_pmv *motion, int x, int y,
                                   int width, int height)
{
    uint8_t *src;
    uint8_t *dst;
    svq1_pmv mv;
    svq1_pmv *pmv[3];
    int result;

    /* predict and decode motion vector */
    pmv[0] = &motion[0];
    if (y == 0) {
        pmv[1] =
        pmv[2] = pmv[0];
    } else {
        pmv[1] = &motion[x / 8 + 2];
        pmv[2] = &motion[x / 8 + 4];
    }

    result = svq1_decode_motion_vector(bitbuf, &mv, pmv);
    if (result)
        return result;

    motion[0].x         =
    motion[x / 8 + 2].x =
    motion[x / 8 + 3].x = mv.x;
    motion[0].y         =
    motion[x / 8 + 2].y =
    motion[x / 8 + 3].y = mv.y;

    mv.x = av_clip(mv.x, -2 * x, 2 * (width  - x - 16));
    mv.y = av_clip(mv.y, -2 * y, 2 * (height - y - 16));

    src = &previous[(x + (mv.x >> 1)) + (y + (mv.y >> 1)) * pitch];
    dst = current;

    hdsp->put_pixels_tab[0][(mv.y & 1) << 1 | (mv.x & 1)](dst, src, pitch, 16);

    return 0;
}

static int svq1_motion_inter_4v_block(HpelDSPContext *hdsp, GetBitContext *bitbuf,
                                      uint8_t *current, uint8_t *previous,
                                      int pitch, svq1_pmv *motion, int x, int y,
                                      int width, int height)
{
    uint8_t *src;
    uint8_t *dst;
    svq1_pmv mv;
    svq1_pmv *pmv[4];
    int i, result;

    /* predict and decode motion vector (0) */
    pmv[0] = &motion[0];
    if (y == 0) {
        pmv[1] =
        pmv[2] = pmv[0];
    } else {
        pmv[1] = &motion[(x / 8) + 2];
        pmv[2] = &motion[(x / 8) + 4];
    }

    result = svq1_decode_motion_vector(bitbuf, &mv, pmv);
    if (result)
        return result;

    /* predict and decode motion vector (1) */
    pmv[0] = &mv;
    if (y == 0) {
        pmv[1] =
        pmv[2] = pmv[0];
    } else {
        pmv[1] = &motion[(x / 8) + 3];
    }
    result = svq1_decode_motion_vector(bitbuf, &motion[0], pmv);
    if (result)
        return result;

    /* predict and decode motion vector (2) */
    pmv[1] = &motion[0];
    pmv[2] = &motion[(x / 8) + 1];

    result = svq1_decode_motion_vector(bitbuf, &motion[(x / 8) + 2], pmv);
    if (result)
        return result;

    /* predict and decode motion vector (3) */
    pmv[2] = &motion[(x / 8) + 2];
    pmv[3] = &motion[(x / 8) + 3];

    result = svq1_decode_motion_vector(bitbuf, pmv[3], pmv);
    if (result)
        return result;

    /* form predictions */
    for (i = 0; i < 4; i++) {
        int mvx = pmv[i]->x + (i  & 1) * 16;
        int mvy = pmv[i]->y + (i >> 1) * 16;

        // FIXME: clipping or padding?
        mvx = av_clip(mvx, -2 * x, 2 * (width  - x - 8));
        mvy = av_clip(mvy, -2 * y, 2 * (height - y - 8));

        src = &previous[(x + (mvx >> 1)) + (y + (mvy >> 1)) * pitch];
        dst = current;

        hdsp->put_pixels_tab[1][((mvy & 1) << 1) | (mvx & 1)](dst, src, pitch, 8);

        /* select next block */
        if (i & 1)
            current += 8 * (pitch - 1);
        else
            current += 8;
    }

    return 0;
}

static int svq1_decode_delta_block(AVCodecContext *avctx, HpelDSPContext *hdsp,
                                   GetBitContext *bitbuf,
                                   uint8_t *current, uint8_t *previous,
                                   int pitch, svq1_pmv *motion, int x, int y,
                                   int width, int height)
{
    uint32_t block_type;
    int result = 0;

    /* get block type */
    block_type = get_vlc2(bitbuf, svq1_block_type.table, 2, 2);

    /* reset motion vectors */
    if (block_type == SVQ1_BLOCK_SKIP || block_type == SVQ1_BLOCK_INTRA) {
        motion[0].x         =
        motion[0].y         =
        motion[x / 8 + 2].x =
        motion[x / 8 + 2].y =
        motion[x / 8 + 3].x =
        motion[x / 8 + 3].y = 0;
    }

    switch (block_type) {
    case SVQ1_BLOCK_SKIP:
        svq1_skip_block(current, previous, pitch, x, y);
        break;

    case SVQ1_BLOCK_INTER:
        result = svq1_motion_inter_block(hdsp, bitbuf, current, previous,
                                         pitch, motion, x, y, width, height);

        if (result != 0) {
            av_dlog(avctx, "Error in svq1_motion_inter_block %i\n", result);
            break;
        }
        result = svq1_decode_block_non_intra(bitbuf, current, pitch);
        break;

    case SVQ1_BLOCK_INTER_4V:
        result = svq1_motion_inter_4v_block(hdsp, bitbuf, current, previous,
                                            pitch, motion, x, y, width, height);

        if (result != 0) {
            av_dlog(avctx, "Error in svq1_motion_inter_4v_block %i\n", result);
            break;
        }
        result = svq1_decode_block_non_intra(bitbuf, current, pitch);
        break;

    case SVQ1_BLOCK_INTRA:
        result = svq1_decode_block_intra(bitbuf, current, pitch);
        break;
    }

    return result;
}

static void svq1_parse_string(GetBitContext *bitbuf, uint8_t out[257])
{
    uint8_t seed;
    int i;

    out[0] = get_bits(bitbuf, 8);
    seed   = string_table[out[0]];

    for (i = 1; i <= out[0]; i++) {
        out[i] = get_bits(bitbuf, 8) ^ seed;
        seed   = string_table[out[i] ^ seed];
    }
    out[i] = 0;
}

static int svq1_decode_frame_header(AVCodecContext *avctx, AVFrame *frame)
{
    SVQ1Context *s = avctx->priv_data;
    GetBitContext *bitbuf = &s->gb;
    int frame_size_code;
    int width  = s->width;
    int height = s->height;

    skip_bits(bitbuf, 8); /* temporal_reference */

    /* frame type */
    s->nonref = 0;
    switch (get_bits(bitbuf, 2)) {
    case 0:
        frame->pict_type = AV_PICTURE_TYPE_I;
        break;
    case 2:
        s->nonref = 1;
    case 1:
        frame->pict_type = AV_PICTURE_TYPE_P;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid frame type.\n");
        return AVERROR_INVALIDDATA;
    }

    if (frame->pict_type == AV_PICTURE_TYPE_I) {
        /* unknown fields */
        if (s->frame_code == 0x50 || s->frame_code == 0x60) {
            int csum = get_bits(bitbuf, 16);

            csum = ff_svq1_packet_checksum(bitbuf->buffer,
                                           bitbuf->size_in_bits >> 3,
                                           csum);

            av_dlog(avctx, "%s checksum (%02x) for packet data\n",
                    (csum == 0) ? "correct" : "incorrect", csum);
        }

        if ((s->frame_code ^ 0x10) >= 0x50) {
            uint8_t msg[257];

            svq1_parse_string(bitbuf, msg);

            av_log(avctx, AV_LOG_INFO,
                   "embedded message:\n%s\n", ((char *)msg) + 1);
        }

        skip_bits(bitbuf, 2);
        skip_bits(bitbuf, 2);
        skip_bits1(bitbuf);

        /* load frame size */
        frame_size_code = get_bits(bitbuf, 3);

        if (frame_size_code == 7) {
            /* load width, height (12 bits each) */
            width  = get_bits(bitbuf, 12);
            height = get_bits(bitbuf, 12);

            if (!width || !height)
                return AVERROR_INVALIDDATA;
        } else {
            /* get width, height from table */
            width  = ff_svq1_frame_size_table[frame_size_code][0];
            height = ff_svq1_frame_size_table[frame_size_code][1];
        }
    }

    /* unknown fields */
    if (get_bits1(bitbuf)) {
        skip_bits1(bitbuf);    /* use packet checksum if (1) */
        skip_bits1(bitbuf);    /* component checksums after image data if (1) */

        if (get_bits(bitbuf, 2) != 0)
            return AVERROR_INVALIDDATA;
    }

    if (get_bits1(bitbuf)) {
        skip_bits1(bitbuf);
        skip_bits(bitbuf, 4);
        skip_bits1(bitbuf);
        skip_bits(bitbuf, 2);

        if (skip_1stop_8data_bits(bitbuf) < 0)
            return AVERROR_INVALIDDATA;
    }

    s->width  = width;
    s->height = height;
    return 0;
}

static int svq1_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    SVQ1Context     *s = avctx->priv_data;
    AVFrame       *cur = data;
    uint8_t *current;
    int result, i, x, y, width, height;
    svq1_pmv *pmv;

    /* initialize bit buffer */
    init_get_bits8(&s->gb, buf, buf_size);

    /* decode frame header */
    s->frame_code = get_bits(&s->gb, 22);

    if ((s->frame_code & ~0x70) || !(s->frame_code & 0x60))
        return AVERROR_INVALIDDATA;

    /* swap some header bytes (why?) */
    if (s->frame_code != 0x20) {
        uint32_t *src;

        if (buf_size < 9 * 4) {
            av_log(avctx, AV_LOG_ERROR, "Input packet too small\n");
            return AVERROR_INVALIDDATA;
        }

        av_fast_padded_malloc(&s->pkt_swapped, &s->pkt_swapped_allocated,
                       buf_size);
        if (!s->pkt_swapped)
            return AVERROR(ENOMEM);

        memcpy(s->pkt_swapped, buf, buf_size);
        buf = s->pkt_swapped;
        init_get_bits(&s->gb, buf, buf_size * 8);
        skip_bits(&s->gb, 22);

        src = (uint32_t *)(s->pkt_swapped + 4);

        if (buf_size < 36)
            return AVERROR_INVALIDDATA;

        for (i = 0; i < 4; i++)
            src[i] = ((src[i] << 16) | (src[i] >> 16)) ^ src[7 - i];
    }

    result = svq1_decode_frame_header(avctx, cur);
    if (result != 0) {
        av_dlog(avctx, "Error in svq1_decode_frame_header %i\n", result);
        return result;
    }

    result = ff_set_dimensions(avctx, s->width, s->height);
    if (result < 0)
        return result;

    if ((avctx->skip_frame >= AVDISCARD_NONREF && s->nonref) ||
        (avctx->skip_frame >= AVDISCARD_NONKEY &&
         cur->pict_type != AV_PICTURE_TYPE_I) ||
        avctx->skip_frame >= AVDISCARD_ALL)
        return buf_size;

    result = ff_get_buffer(avctx, cur, s->nonref ? 0 : AV_GET_BUFFER_FLAG_REF);
    if (result < 0)
        return result;

    pmv = av_malloc((FFALIGN(s->width, 16) / 8 + 3) * sizeof(*pmv));
    if (!pmv)
        return AVERROR(ENOMEM);

    /* decode y, u and v components */
    for (i = 0; i < 3; i++) {
        int linesize = cur->linesize[i];
        if (i == 0) {
            width    = FFALIGN(s->width,  16);
            height   = FFALIGN(s->height, 16);
        } else {
            if (avctx->flags & CODEC_FLAG_GRAY)
                break;
            width    = FFALIGN(s->width  / 4, 16);
            height   = FFALIGN(s->height / 4, 16);
        }

        current = cur->data[i];

        if (cur->pict_type == AV_PICTURE_TYPE_I) {
            /* keyframe */
            for (y = 0; y < height; y += 16) {
                for (x = 0; x < width; x += 16) {
                    result = svq1_decode_block_intra(&s->gb, &current[x],
                                                     linesize);
                    if (result) {
                        av_log(avctx, AV_LOG_ERROR,
                               "Error in svq1_decode_block %i (keyframe)\n",
                               result);
                        goto err;
                    }
                }
                current += 16 * linesize;
            }
        } else {
            /* delta frame */
            uint8_t *previous = s->prev->data[i];
            if (!previous ||
                s->prev->width != s->width || s->prev->height != s->height) {
                av_log(avctx, AV_LOG_ERROR, "Missing reference frame.\n");
                result = AVERROR_INVALIDDATA;
                goto err;
            }

            memset(pmv, 0, ((width / 8) + 3) * sizeof(svq1_pmv));

            for (y = 0; y < height; y += 16) {
                for (x = 0; x < width; x += 16) {
                    result = svq1_decode_delta_block(avctx, &s->hdsp,
                                                     &s->gb, &current[x],
                                                     previous, linesize,
                                                     pmv, x, y, width, height);
                    if (result != 0) {
                        av_dlog(avctx,
                                "Error in svq1_decode_delta_block %i\n",
                                result);
                        goto err;
                    }
                }

                pmv[0].x     =
                    pmv[0].y = 0;

                current += 16 * linesize;
            }
        }
    }

    if (!s->nonref) {
        av_frame_unref(s->prev);
        result = av_frame_ref(s->prev, cur);
        if (result < 0)
            goto err;
    }

    *got_frame = 1;
    result     = buf_size;

err:
    av_free(pmv);
    return result;
}

static av_cold int svq1_decode_init(AVCodecContext *avctx)
{
    SVQ1Context *s = avctx->priv_data;
    int i;
    int offset = 0;

    s->prev = av_frame_alloc();
    if (!s->prev)
        return AVERROR(ENOMEM);

    s->width            = avctx->width  + 3 & ~3;
    s->height           = avctx->height + 3 & ~3;
    avctx->pix_fmt      = AV_PIX_FMT_YUV410P;

    ff_hpeldsp_init(&s->hdsp, avctx->flags);

    INIT_VLC_STATIC(&svq1_block_type, 2, 4,
                    &ff_svq1_block_type_vlc[0][1], 2, 1,
                    &ff_svq1_block_type_vlc[0][0], 2, 1, 6);

    INIT_VLC_STATIC(&svq1_motion_component, 7, 33,
                    &ff_mvtab[0][1], 2, 1,
                    &ff_mvtab[0][0], 2, 1, 176);

    for (i = 0; i < 6; i++) {
        static const uint8_t sizes[2][6] = { { 14, 10, 14, 18, 16, 18 },
                                             { 10, 10, 14, 14, 14, 16 } };
        static VLC_TYPE table[168][2];
        svq1_intra_multistage[i].table           = &table[offset];
        svq1_intra_multistage[i].table_allocated = sizes[0][i];
        offset                                  += sizes[0][i];
        init_vlc(&svq1_intra_multistage[i], 3, 8,
                 &ff_svq1_intra_multistage_vlc[i][0][1], 2, 1,
                 &ff_svq1_intra_multistage_vlc[i][0][0], 2, 1,
                 INIT_VLC_USE_NEW_STATIC);
        svq1_inter_multistage[i].table           = &table[offset];
        svq1_inter_multistage[i].table_allocated = sizes[1][i];
        offset                                  += sizes[1][i];
        init_vlc(&svq1_inter_multistage[i], 3, 8,
                 &ff_svq1_inter_multistage_vlc[i][0][1], 2, 1,
                 &ff_svq1_inter_multistage_vlc[i][0][0], 2, 1,
                 INIT_VLC_USE_NEW_STATIC);
    }

    INIT_VLC_STATIC(&svq1_intra_mean, 8, 256,
                    &ff_svq1_intra_mean_vlc[0][1], 4, 2,
                    &ff_svq1_intra_mean_vlc[0][0], 4, 2, 632);

    INIT_VLC_STATIC(&svq1_inter_mean, 9, 512,
                    &ff_svq1_inter_mean_vlc[0][1], 4, 2,
                    &ff_svq1_inter_mean_vlc[0][0], 4, 2, 1434);

    return 0;
}

static av_cold int svq1_decode_end(AVCodecContext *avctx)
{
    SVQ1Context *s = avctx->priv_data;

    av_frame_free(&s->prev);
    av_freep(&s->pkt_swapped);
    s->pkt_swapped_allocated = 0;

    return 0;
}

static void svq1_flush(AVCodecContext *avctx)
{
    SVQ1Context *s = avctx->priv_data;

    av_frame_unref(s->prev);
}

AVCodec ff_svq1_decoder = {
    .name           = "svq1",
    .long_name      = NULL_IF_CONFIG_SMALL("Sorenson Vector Quantizer 1 / Sorenson Video 1 / SVQ1"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SVQ1,
    .priv_data_size = sizeof(SVQ1Context),
    .init           = svq1_decode_init,
    .close          = svq1_decode_end,
    .decode         = svq1_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = svq1_flush,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV410P,
                                                     AV_PIX_FMT_NONE },
};
