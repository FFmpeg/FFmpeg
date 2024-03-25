/*
 * Resolume DXV encoder
 * Copyright (C) 2024 Connor Worley <connorbworley@gmail.com>
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

#include <stdint.h>

#include "libavutil/crc.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "bytestream.h"
#include "codec_internal.h"
#include "dxv.h"
#include "encode.h"
#include "texturedsp.h"

#define DXV_HEADER_LENGTH 12

/*
 * DXV uses LZ-like back-references to avoid copying words that have already
 * appeared in the decompressed stream. Using a simple hash table (HT)
 * significantly speeds up the lookback process while encoding.
 */
#define LOOKBACK_HT_ELEMS 0x40000
#define LOOKBACK_WORDS    0x20202

typedef struct HTEntry {
    uint32_t key;
    uint32_t pos;
} HTEntry;

static void ht_init(HTEntry *ht)
{
    for (size_t i = 0; i < LOOKBACK_HT_ELEMS; i++) {
        ht[i].pos = -1;
    }
}

static uint32_t ht_lookup_and_upsert(HTEntry *ht, const AVCRC *hash_ctx,
                                    uint32_t key, uint32_t pos)
{
    uint32_t ret = -1;
    size_t hash = av_crc(hash_ctx, 0, (uint8_t*)&key, 4) % LOOKBACK_HT_ELEMS;
    for (size_t i = hash; i < hash + LOOKBACK_HT_ELEMS; i++) {
        size_t wrapped_index = i % LOOKBACK_HT_ELEMS;
        HTEntry *entry = &ht[wrapped_index];
        if (entry->key == key || entry->pos == -1) {
            ret = entry->pos;
            entry->key = key;
            entry->pos = pos;
            break;
        }
    }
    return ret;
}

static void ht_delete(HTEntry *ht, const AVCRC *hash_ctx,
                      uint32_t key, uint32_t pos)
{
    HTEntry *removed_entry = NULL;
    size_t removed_hash;
    size_t hash = av_crc(hash_ctx, 0, (uint8_t*)&key, 4) % LOOKBACK_HT_ELEMS;

    for (size_t i = hash; i < hash + LOOKBACK_HT_ELEMS; i++) {
        size_t wrapped_index = i % LOOKBACK_HT_ELEMS;
        HTEntry *entry = &ht[wrapped_index];
        if (entry->pos == -1)
            return;
        if (removed_entry) {
            size_t candidate_hash = av_crc(hash_ctx, 0, (uint8_t*)&entry->key, 4) % LOOKBACK_HT_ELEMS;
            if ((wrapped_index > removed_hash && (candidate_hash <= removed_hash || candidate_hash > wrapped_index)) ||
                (wrapped_index < removed_hash && (candidate_hash <= removed_hash && candidate_hash > wrapped_index))) {
                *removed_entry = *entry;
                entry->pos = -1;
                removed_entry = entry;
                removed_hash = wrapped_index;
            }
        } else if (entry->key == key) {
            if (entry->pos <= pos) {
                entry->pos = -1;
                removed_entry = entry;
                removed_hash = wrapped_index;
            } else {
                return;
            }
        }
    }
}

typedef struct DXVEncContext {
    AVClass *class;

    PutByteContext pbc;

    uint8_t *tex_data;   // Compressed texture
    int64_t tex_size;    // Texture size

    /* Optimal number of slices for parallel decoding */
    int slice_count;

    TextureDSPThreadContext enc;

    DXVTextureFormat tex_fmt;
    int (*compress_tex)(AVCodecContext *avctx);

    const AVCRC *crc_ctx;

    HTEntry color_lookback_ht[LOOKBACK_HT_ELEMS];
    HTEntry lut_lookback_ht[LOOKBACK_HT_ELEMS];
} DXVEncContext;

/* Converts an index offset value to a 2-bit opcode and pushes it to a stream.
 * Inverse of CHECKPOINT in dxv.c.  */
#define PUSH_OP(x)                                                            \
    do {                                                                      \
        if (state == 16) {                                                    \
            if (bytestream2_get_bytes_left_p(pbc) < 4) {                      \
                return AVERROR_INVALIDDATA;                                   \
            }                                                                 \
            value = pbc->buffer;                                              \
            bytestream2_put_le32(pbc, 0);                                     \
            state = 0;                                                        \
        }                                                                     \
        if (idx >= 0x102 * x) {                                               \
            op = 3;                                                           \
            bytestream2_put_le16(pbc, (idx / x) - 0x102);                     \
        } else if (idx >= 2 * x) {                                            \
            op = 2;                                                           \
            bytestream2_put_byte(pbc, (idx / x) - 2);                         \
        } else if (idx == x) {                                                \
            op = 1;                                                           \
        } else {                                                              \
            op = 0;                                                           \
        }                                                                     \
        AV_WL32(value, AV_RL32(value) | (op << (state * 2)));                 \
        state++;                                                              \
    } while (0)

static int dxv_compress_dxt1(AVCodecContext *avctx)
{
    DXVEncContext *ctx = avctx->priv_data;
    PutByteContext *pbc = &ctx->pbc;
    void *value;
    uint32_t color, lut, idx, color_idx, lut_idx, prev_pos, state = 16, pos = 2, op = 0;

    ht_init(ctx->color_lookback_ht);
    ht_init(ctx->lut_lookback_ht);

    bytestream2_put_le32(pbc, AV_RL32(ctx->tex_data));
    bytestream2_put_le32(pbc, AV_RL32(ctx->tex_data + 4));

    ht_lookup_and_upsert(ctx->color_lookback_ht, ctx->crc_ctx, AV_RL32(ctx->tex_data), 0);
    ht_lookup_and_upsert(ctx->lut_lookback_ht, ctx->crc_ctx, AV_RL32(ctx->tex_data + 4), 1);

    while (pos + 2 <= ctx->tex_size / 4) {
        idx = 0;

        color = AV_RL32(ctx->tex_data + pos * 4);
        prev_pos = ht_lookup_and_upsert(ctx->color_lookback_ht, ctx->crc_ctx, color, pos);
        color_idx = prev_pos != -1 ? pos - prev_pos : 0;
        if (pos >= LOOKBACK_WORDS) {
            uint32_t old_pos = pos - LOOKBACK_WORDS;
            uint32_t old_color = AV_RL32(ctx->tex_data + old_pos * 4);
            ht_delete(ctx->color_lookback_ht, ctx->crc_ctx, old_color, old_pos);
        }
        pos++;

        lut = AV_RL32(ctx->tex_data + pos * 4);
        if (color_idx && lut == AV_RL32(ctx->tex_data + (pos - color_idx) * 4)) {
            idx = color_idx;
        } else {
            idx = 0;
            prev_pos = ht_lookup_and_upsert(ctx->lut_lookback_ht, ctx->crc_ctx, lut, pos);
            lut_idx = prev_pos != -1 ? pos - prev_pos : 0;
        }
        if (pos >= LOOKBACK_WORDS) {
            uint32_t old_pos = pos - LOOKBACK_WORDS;
            uint32_t old_lut = AV_RL32(ctx->tex_data + old_pos * 4);
            ht_delete(ctx->lut_lookback_ht, ctx->crc_ctx, old_lut, old_pos);
        }
        pos++;

        PUSH_OP(2);

        if (!idx) {
            idx = color_idx;
            PUSH_OP(2);
            if (!idx)
                bytestream2_put_le32(pbc,  color);

            idx = lut_idx;
            PUSH_OP(2);
            if (!idx)
                bytestream2_put_le32(pbc,  lut);
        }
    }

    return 0;
}

static int dxv_encode(AVCodecContext *avctx, AVPacket *pkt,
                      const AVFrame *frame, int *got_packet)
{
    DXVEncContext *ctx = avctx->priv_data;
    PutByteContext *pbc = &ctx->pbc;
    int ret;

    /* unimplemented: needs to depend on compression ratio of tex format */
    /* under DXT1, we need 3 words to encode load ops for 32 words.
     * the first 2 words of the texture do not need load ops. */
    ret = ff_alloc_packet(avctx, pkt, DXV_HEADER_LENGTH + ctx->tex_size + AV_CEIL_RSHIFT(ctx->tex_size - 8, 7) * 12);
    if (ret < 0)
        return ret;

    if (ctx->enc.tex_funct) {
        ctx->enc.tex_data.out = ctx->tex_data;
        ctx->enc.frame_data.in = frame->data[0];
        ctx->enc.stride = frame->linesize[0];
        ctx->enc.width  = avctx->width;
        ctx->enc.height = avctx->height;
        ff_texturedsp_exec_compress_threads(avctx, &ctx->enc);
    } else {
        /* unimplemented: YCoCg formats */
        return AVERROR_INVALIDDATA;
    }

    bytestream2_init_writer(pbc, pkt->data, pkt->size);

    bytestream2_put_le32(pbc, ctx->tex_fmt);
    bytestream2_put_byte(pbc, 4);
    bytestream2_put_byte(pbc, 0);
    bytestream2_put_byte(pbc, 0);
    bytestream2_put_byte(pbc, 0);
    /* Fill in compressed size later */
    bytestream2_skip_p(pbc, 4);

    ret = ctx->compress_tex(avctx);
    if (ret < 0)
        return ret;

    AV_WL32(pkt->data + 8, bytestream2_tell_p(pbc) - DXV_HEADER_LENGTH);
    av_shrink_packet(pkt, bytestream2_tell_p(pbc));

    *got_packet = 1;
    return 0;
}

static av_cold int dxv_init(AVCodecContext *avctx)
{
    DXVEncContext *ctx = avctx->priv_data;
    TextureDSPEncContext texdsp;
    int ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);

    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid image size %dx%d.\n",
               avctx->width, avctx->height);
        return ret;
    }

    if (avctx->width % TEXTURE_BLOCK_W || avctx->height % TEXTURE_BLOCK_H) {
        av_log(avctx,
               AV_LOG_ERROR,
               "Video size %dx%d is not multiple of "AV_STRINGIFY(TEXTURE_BLOCK_W)"x"AV_STRINGIFY(TEXTURE_BLOCK_H)".\n",
               avctx->width, avctx->height);
        return AVERROR_INVALIDDATA;
    }

    ff_texturedspenc_init(&texdsp);

    switch (ctx->tex_fmt) {
    case DXV_FMT_DXT1:
        ctx->compress_tex = dxv_compress_dxt1;
        ctx->enc.tex_funct = texdsp.dxt1_block;
        ctx->enc.tex_ratio = 8;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid format %08X\n", ctx->tex_fmt);
        return AVERROR_INVALIDDATA;
    }
    ctx->enc.raw_ratio = 16;
    ctx->tex_size = avctx->width  / TEXTURE_BLOCK_W *
                    avctx->height / TEXTURE_BLOCK_H *
                    ctx->enc.tex_ratio;
    ctx->enc.slice_count = av_clip(avctx->thread_count, 1, avctx->height / TEXTURE_BLOCK_H);

    ctx->tex_data = av_malloc(ctx->tex_size);
    if (!ctx->tex_data) {
        return AVERROR(ENOMEM);
    }

    ctx->crc_ctx = av_crc_get_table(AV_CRC_32_IEEE);
    if (!ctx->crc_ctx) {
        av_log(avctx, AV_LOG_ERROR, "Could not initialize CRC table.\n");
        return AVERROR_BUG;
    }

    return 0;
}

static av_cold int dxv_close(AVCodecContext *avctx)
{
    DXVEncContext *ctx = avctx->priv_data;

    av_freep(&ctx->tex_data);

    return 0;
}

#define OFFSET(x) offsetof(DXVEncContext, x)
#define FLAGS     AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "format", NULL, OFFSET(tex_fmt), AV_OPT_TYPE_INT, { .i64 = DXV_FMT_DXT1 }, DXV_FMT_DXT1, DXV_FMT_DXT1, FLAGS, .unit = "format" },
        { "dxt1", "DXT1 (Normal Quality, No Alpha)", 0, AV_OPT_TYPE_CONST, { .i64 = DXV_FMT_DXT1   }, 0, 0, FLAGS, .unit = "format" },
    { NULL },
};

static const AVClass dxvenc_class = {
    .class_name = "DXV encoder",
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_dxv_encoder = {
    .p.name         = "dxv",
    CODEC_LONG_NAME("Resolume DXV"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_DXV,
    .init           = dxv_init,
    FF_CODEC_ENCODE_CB(dxv_encode),
    .close          = dxv_close,
    .priv_data_size = sizeof(DXVEncContext),
    .p.capabilities = AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_SLICE_THREADS |
                      AV_CODEC_CAP_FRAME_THREADS,
    .p.priv_class   = &dxvenc_class,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE,
    },
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
