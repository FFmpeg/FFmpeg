/*
 * QuickTime Graphics (SMC) Video Encoder
 * Copyright (c) 2021 The FFmpeg project
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
 * @file smcenc.c
 * QT SMC Video Encoder by Paul B. Mahol
 */

#include "libavutil/common.h"

#include "avcodec.h"
#include "encode.h"
#include "internal.h"
#include "bytestream.h"

#define CPAIR 2
#define CQUAD 4
#define COCTET 8

#define COLORS_PER_TABLE 256

typedef struct SMCContext {
    AVFrame *prev_frame;    // buffer for previous source frame

    uint8_t mono_value;
    int nb_distinct;
    int next_nb_distinct;
    uint8_t distinct_values[16];
    uint8_t next_distinct_values[16];

    uint8_t color_pairs[COLORS_PER_TABLE][CPAIR];
    uint8_t color_quads[COLORS_PER_TABLE][CQUAD];
    uint8_t color_octets[COLORS_PER_TABLE][COCTET];

    int key_frame;
} SMCContext;

#define ADVANCE_BLOCK(pixel_ptr, row_ptr, nb_blocks) \
{ \
    for (int block = 0; block < nb_blocks && pixel_ptr && row_ptr; block++) { \
        pixel_ptr += 4; \
        if (pixel_ptr - row_ptr >= width) \
        { \
            row_ptr += stride * 4; \
            pixel_ptr = row_ptr; \
        } \
    } \
}

static int smc_cmp_values(const void *a, const void *b)
{
    const uint8_t *aa = a, *bb = b;

    return FFDIFFSIGN(aa[0], bb[0]);
}

static int count_distinct_items(const uint8_t *block_values,
                                uint8_t *distinct_values,
                                int size)
{
    int n = 1;

    distinct_values[0] = block_values[0];
    for (int i = 1; i < size; i++) {
        if (block_values[i] != block_values[i-1]) {
            distinct_values[n] = block_values[i];
            n++;
        }
    }

    return n;
}

#define CACHE_PAIR(x) \
    (s->color_pairs[i][0] == distinct_values[x] || \
     s->color_pairs[i][1] == distinct_values[x])

#define CACHE_QUAD(x) \
    (s->color_quads[i][0] == distinct_values[x] || \
     s->color_quads[i][1] == distinct_values[x] || \
     s->color_quads[i][2] == distinct_values[x] || \
     s->color_quads[i][3] == distinct_values[x])

#define CACHE_OCTET(x) \
    (s->color_octets[i][0] == distinct_values[x] || \
     s->color_octets[i][1] == distinct_values[x] || \
     s->color_octets[i][2] == distinct_values[x] || \
     s->color_octets[i][3] == distinct_values[x] || \
     s->color_octets[i][4] == distinct_values[x] || \
     s->color_octets[i][5] == distinct_values[x] || \
     s->color_octets[i][6] == distinct_values[x] || \
     s->color_octets[i][7] == distinct_values[x])

static void smc_encode_stream(SMCContext *s, const AVFrame *frame,
                              PutByteContext *pb)
{
    const uint8_t *src_pixels = (const uint8_t *)frame->data[0];
    const int stride = frame->linesize[0];
    const uint8_t *prev_pixels = (const uint8_t *)s->prev_frame->data[0];
    uint8_t *distinct_values = s->distinct_values;
    const uint8_t *pixel_ptr, *row_ptr;
    const int width = frame->width;
    uint8_t block_values[16];
    int block_counter = 0;
    int color_pair_index = 0;
    int color_quad_index = 0;
    int color_octet_index = 0;
    int color_table_index;  /* indexes to color pair, quad, or octet tables */
    int total_blocks;

    memset(s->color_pairs, 0, sizeof(s->color_pairs));
    memset(s->color_quads, 0, sizeof(s->color_quads));
    memset(s->color_octets, 0, sizeof(s->color_octets));

    /* Number of 4x4 blocks in frame. */
    total_blocks = ((frame->width + 3) / 4) * ((frame->height + 3) / 4);

    pixel_ptr = row_ptr = src_pixels;

    while (block_counter < total_blocks) {
        const uint8_t *xpixel_ptr = pixel_ptr;
        const uint8_t *xrow_ptr = row_ptr;
        int intra_skip_blocks = 0;
        int inter_skip_blocks = 0;
        int coded_distinct = 0;
        int coded_blocks = 0;
        int cache_index;
        int distinct = 0;
        int blocks = 0;

        while (prev_pixels && s->key_frame == 0 && block_counter + inter_skip_blocks < total_blocks) {
            int compare = 0;

            for (int y = 0; y < 4; y++) {
                const ptrdiff_t offset = pixel_ptr - src_pixels;
                const uint8_t *prev_pixel_ptr = prev_pixels + offset;

                compare |= memcmp(prev_pixel_ptr + y * stride, pixel_ptr + y * stride, 4);
                if (compare)
                    break;
            }

            if (compare)
                break;

            if (inter_skip_blocks >= 256)
                break;
            inter_skip_blocks++;

            ADVANCE_BLOCK(pixel_ptr, row_ptr, 1)
        }

        pixel_ptr = xpixel_ptr;
        row_ptr = xrow_ptr;

        while (block_counter > 0 && block_counter + intra_skip_blocks < total_blocks) {
            const ptrdiff_t offset = pixel_ptr - src_pixels;
            const int sy = offset / stride;
            const int sx = offset % stride;
            const int ny = sx < 4 ? sy - 4 : sy;
            const int nx = sx < 4 ? width - 4 : sx - 4;
            const uint8_t *old_pixel_ptr = src_pixels + nx + ny * stride;
            int compare = 0;

            for (int y = 0; y < 4; y++) {
                compare |= memcmp(old_pixel_ptr + y * stride, pixel_ptr + y * stride, 4);
                if (compare)
                    break;
            }

            if (compare)
                break;

            if (intra_skip_blocks >= 256)
                break;
            intra_skip_blocks++;
            ADVANCE_BLOCK(pixel_ptr, row_ptr, 1)
        }

        pixel_ptr = xpixel_ptr;
        row_ptr = xrow_ptr;

        while (block_counter + coded_blocks < total_blocks && coded_blocks < 256) {
            for (int y = 0; y < 4; y++)
                memcpy(block_values + y * 4, pixel_ptr + y * stride, 4);

            qsort(block_values, 16, sizeof(block_values[0]), smc_cmp_values);
            s->next_nb_distinct = count_distinct_items(block_values, s->next_distinct_values, 16);
            if (coded_blocks == 0) {
                memcpy(distinct_values, s->next_distinct_values, sizeof(s->distinct_values));
                s->nb_distinct = s->next_nb_distinct;
            } else {
                if (s->next_nb_distinct != s->nb_distinct ||
                    memcmp(distinct_values, s->next_distinct_values, s->nb_distinct)) {
                    break;
                }
            }
            s->mono_value = block_values[0];

            coded_distinct = s->nb_distinct;
            ADVANCE_BLOCK(pixel_ptr, row_ptr, 1)
            coded_blocks++;
            if (coded_distinct > 1 && coded_blocks >= 16)
                break;
        }

        pixel_ptr = xpixel_ptr;
        row_ptr = xrow_ptr;

        blocks = coded_blocks;
        distinct = coded_distinct;

        if (intra_skip_blocks > 0 && intra_skip_blocks >= inter_skip_blocks &&
            intra_skip_blocks > 0) {
            distinct = 17;
            blocks = intra_skip_blocks;
        }

        if (intra_skip_blocks > 16 && intra_skip_blocks >= inter_skip_blocks &&
            intra_skip_blocks > 0) {
            distinct = 18;
            blocks = intra_skip_blocks;
        }

        if (inter_skip_blocks > 0 && inter_skip_blocks > intra_skip_blocks &&
            inter_skip_blocks > 0) {
            distinct = 19;
            blocks = inter_skip_blocks;
        }

        if (inter_skip_blocks > 16 && inter_skip_blocks > intra_skip_blocks &&
            inter_skip_blocks > 0) {
            distinct = 20;
            blocks = inter_skip_blocks;
        }

        switch (distinct) {
        case 1:
            if (blocks <= 16) {
                bytestream2_put_byte(pb, 0x60 | (blocks - 1));
            } else {
                bytestream2_put_byte(pb, 0x70);
                bytestream2_put_byte(pb, blocks - 1);
            }
            bytestream2_put_byte(pb, s->mono_value);
            ADVANCE_BLOCK(pixel_ptr, row_ptr, blocks)
            break;
        case 2:
            cache_index = -1;
            for (int i = 0; i < COLORS_PER_TABLE; i++) {
                if (CACHE_PAIR(0) &&
                    CACHE_PAIR(1)) {
                    cache_index = i;
                    break;
                }
            }

            if (cache_index >= 0) {
                bytestream2_put_byte(pb, 0x90 | (blocks - 1));
                bytestream2_put_byte(pb, cache_index);
                color_table_index = cache_index;
            } else {
                bytestream2_put_byte(pb, 0x80 | (blocks - 1));

                color_table_index = color_pair_index;
                for (int i = 0; i < CPAIR; i++) {
                    s->color_pairs[color_table_index][i] = distinct_values[i];
                    bytestream2_put_byte(pb, distinct_values[i]);
                }

                color_pair_index++;
                if (color_pair_index == COLORS_PER_TABLE)
                    color_pair_index = 0;
            }

            for (int i = 0; i < blocks; i++) {
                uint8_t value = s->color_pairs[color_table_index][1];
                uint16_t flags = 0;
                int shift = 15;

                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++) {
                        flags |= (value == pixel_ptr[x + y * stride]) << shift;
                        shift--;
                    }
                }

                bytestream2_put_be16(pb, flags);

                ADVANCE_BLOCK(pixel_ptr, row_ptr, 1)
            }
            break;
        case 3:
        case 4:
            cache_index = -1;
            for (int i = 0; i < COLORS_PER_TABLE; i++) {
                if (CACHE_QUAD(0) &&
                    CACHE_QUAD(1) &&
                    CACHE_QUAD(2) &&
                    CACHE_QUAD(3)) {
                    cache_index = i;
                    break;
                }
            }

            if (cache_index >= 0) {
                bytestream2_put_byte(pb, 0xB0 | (blocks - 1));
                bytestream2_put_byte(pb, cache_index);
                color_table_index = cache_index;
            } else {
                bytestream2_put_byte(pb, 0xA0 | (blocks - 1));

                color_table_index = color_quad_index;
                for (int i = 0; i < CQUAD; i++) {
                    s->color_quads[color_table_index][i] = distinct_values[i];
                    bytestream2_put_byte(pb, distinct_values[i]);
                }

                color_quad_index++;
                if (color_quad_index == COLORS_PER_TABLE)
                    color_quad_index = 0;
            }

            for (int i = 0; i < blocks; i++) {
                uint32_t flags = 0;
                uint8_t quad[4];
                int shift = 30;

                for (int k = 0; k < 4; k++)
                    quad[k] = s->color_quads[color_table_index][k];

                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++) {
                        int pixel = pixel_ptr[x + y * stride];
                        uint32_t idx = 0;

                        for (int w = 0; w < CQUAD; w++) {
                            if (quad[w] == pixel) {
                                idx = w;
                                break;
                            }
                        }

                        flags |= idx << shift;
                        shift -= 2;
                    }
                }

                bytestream2_put_be32(pb, flags);

                ADVANCE_BLOCK(pixel_ptr, row_ptr, 1)
            }
            break;
        case 5:
        case 6:
        case 7:
        case 8:
            cache_index = -1;
            for (int i = 0; i < COLORS_PER_TABLE; i++) {
                if (CACHE_OCTET(0) &&
                    CACHE_OCTET(1) &&
                    CACHE_OCTET(2) &&
                    CACHE_OCTET(3) &&
                    CACHE_OCTET(4) &&
                    CACHE_OCTET(5) &&
                    CACHE_OCTET(6) &&
                    CACHE_OCTET(7)) {
                    cache_index = i;
                    break;
                }
            }

            if (cache_index >= 0) {
                bytestream2_put_byte(pb, 0xD0 | (blocks - 1));
                bytestream2_put_byte(pb, cache_index);
                color_table_index = cache_index;
            } else {
                bytestream2_put_byte(pb, 0xC0 | (blocks - 1));

                color_table_index = color_octet_index;
                for (int i = 0; i < COCTET; i++) {
                    s->color_octets[color_table_index][i] = distinct_values[i];
                    bytestream2_put_byte(pb, distinct_values[i]);
                }

                color_octet_index++;
                if (color_octet_index == COLORS_PER_TABLE)
                    color_octet_index = 0;
            }

            for (int i = 0; i < blocks; i++) {
                uint64_t flags = 0;
                uint8_t octet[8];
                int shift = 45;

                for (int k = 0; k < 8; k++)
                    octet[k] = s->color_octets[color_table_index][k];

                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++) {
                        int pixel = pixel_ptr[x + y * stride];
                        uint64_t idx = 0;

                        for (int w = 0; w < COCTET; w++) {
                            if (octet[w] == pixel) {
                                idx = w;
                                break;
                            }
                        }

                        flags |= idx << shift;
                        shift -= 3;
                    }
                }

                bytestream2_put_be16(pb, ((flags >> 32) & 0xFFF0) | ((flags >> 8) & 0xF));
                bytestream2_put_be16(pb, ((flags >> 20) & 0xFFF0) | ((flags >> 4) & 0xF));
                bytestream2_put_be16(pb, ((flags >>  8) & 0xFFF0) | ((flags >> 0) & 0xF));

                ADVANCE_BLOCK(pixel_ptr, row_ptr, 1)
            }
            break;
        default:
            bytestream2_put_byte(pb, 0xE0 | (blocks - 1));
            for (int i = 0; i < blocks; i++) {
                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++)
                        bytestream2_put_byte(pb, pixel_ptr[x + y * stride]);
                }

                ADVANCE_BLOCK(pixel_ptr, row_ptr, 1)
            }
            break;
        case 17:
            bytestream2_put_byte(pb, 0x20 | (blocks - 1));
            ADVANCE_BLOCK(pixel_ptr, row_ptr, blocks)
            break;
        case 18:
            bytestream2_put_byte(pb, 0x30);
            bytestream2_put_byte(pb, blocks - 1);
            ADVANCE_BLOCK(pixel_ptr, row_ptr, blocks)
            break;
        case 19:
            bytestream2_put_byte(pb, 0x00 | (blocks - 1));
            ADVANCE_BLOCK(pixel_ptr, row_ptr, blocks)
            break;
        case 20:
            bytestream2_put_byte(pb, 0x10);
            bytestream2_put_byte(pb, blocks - 1);
            ADVANCE_BLOCK(pixel_ptr, row_ptr, blocks)
            break;
        }

        block_counter += blocks;
    }
}

static int smc_encode_init(AVCodecContext *avctx)
{
    SMCContext *s = avctx->priv_data;

    avctx->bits_per_coded_sample = 8;

    s->prev_frame = av_frame_alloc();
    if (!s->prev_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int smc_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *frame, int *got_packet)
{
    SMCContext *s = avctx->priv_data;
    const AVFrame *pict = frame;
    PutByteContext pb;
    uint8_t *pal;
    int ret;

    ret = ff_alloc_packet(avctx, pkt, 8LL * avctx->height * avctx->width);
    if (ret < 0)
        return ret;

    if (avctx->gop_size == 0 || !s->prev_frame->data[0] ||
        (avctx->frame_number % avctx->gop_size) == 0) {
        s->key_frame = 1;
    } else {
        s->key_frame = 0;
    }

    bytestream2_init_writer(&pb, pkt->data, pkt->size);

    bytestream2_put_be32(&pb, 0x00);

    pal = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE, AVPALETTE_SIZE);
    if (!pal)
        return AVERROR(ENOMEM);
    memcpy(pal, frame->data[1], AVPALETTE_SIZE);

    smc_encode_stream(s, pict, &pb);

    av_shrink_packet(pkt, bytestream2_tell_p(&pb));

    pkt->data[0] = 0x0;

    // write chunk length
    AV_WB24(pkt->data + 1, pkt->size);

    av_frame_unref(s->prev_frame);
    ret = av_frame_ref(s->prev_frame, frame);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "cannot add reference\n");
        return ret;
    }

    if (s->key_frame)
        pkt->flags |= AV_PKT_FLAG_KEY;

    *got_packet = 1;

    return 0;
}

static int smc_encode_end(AVCodecContext *avctx)
{
    SMCContext *s = (SMCContext *)avctx->priv_data;

    av_frame_free(&s->prev_frame);

    return 0;
}

const AVCodec ff_smc_encoder = {
    .name           = "smc",
    .long_name      = NULL_IF_CONFIG_SMALL("QuickTime Graphics (SMC)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SMC,
    .priv_data_size = sizeof(SMCContext),
    .init           = smc_encode_init,
    .encode2        = smc_encode_frame,
    .close          = smc_encode_end,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_PAL8,
                                                     AV_PIX_FMT_NONE},
};
