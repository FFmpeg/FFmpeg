/*
 * Resolume DXV encoder
 * Copyright (C) 2024 Emma Worley <emma@emma.gg>
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

#include "libavcodec/hashtable.h"
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
 * Resolume will refuse to display frames that are not padded to 16x16 pixels.
 */
#define DXV_ALIGN(x) FFALIGN(x, 16)

/*
 * DXV uses LZ-like back-references to avoid copying words that have already
 * appeared in the decompressed stream. Using a simple hash table (HT)
 * significantly speeds up the lookback process while encoding.
 */
#define LOOKBACK_HT_ELEMS 0x20202
#define LOOKBACK_WORDS    0x20202

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

    FFHashtableContext *color_ht;
    FFHashtableContext *lut_ht;
    FFHashtableContext *combo_ht;
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
    uint32_t idx, combo_idx, prev_pos, old_pos, state = 16, pos = 0, op = 0;

    ff_hashtable_clear(ctx->color_ht);
    ff_hashtable_clear(ctx->lut_ht);
    ff_hashtable_clear(ctx->combo_ht);

    ff_hashtable_set(ctx->combo_ht, ctx->tex_data, &pos);

    bytestream2_put_le32(pbc, AV_RL32(ctx->tex_data));
    ff_hashtable_set(ctx->color_ht, ctx->tex_data, &pos);
    pos++;
    bytestream2_put_le32(pbc, AV_RL32(ctx->tex_data + 4));
    ff_hashtable_set(ctx->lut_ht, ctx->tex_data + 4, &pos);
    pos++;

    while (pos + 2 <= ctx->tex_size / 4) {
        combo_idx = ff_hashtable_get(ctx->combo_ht, ctx->tex_data + pos * 4, &prev_pos) ? pos - prev_pos : 0;
        idx = combo_idx;
        PUSH_OP(2);
        if (pos >= LOOKBACK_WORDS) {
            old_pos = pos - LOOKBACK_WORDS;
            if (ff_hashtable_get(ctx->combo_ht, ctx->tex_data + old_pos * 4, &prev_pos) && prev_pos <= old_pos)
                ff_hashtable_delete(ctx->combo_ht, ctx->tex_data + old_pos * 4);
        }
        ff_hashtable_set(ctx->combo_ht, ctx->tex_data + pos * 4, &pos);

        if (!combo_idx) {
            idx = ff_hashtable_get(ctx->color_ht, ctx->tex_data + pos * 4, &prev_pos) ? pos - prev_pos : 0;
            PUSH_OP(2);
            if (!idx)
                bytestream2_put_le32(pbc, AV_RL32(ctx->tex_data + pos * 4));
        }
        if (pos >= LOOKBACK_WORDS) {
            old_pos = pos - LOOKBACK_WORDS;
            if (ff_hashtable_get(ctx->color_ht, ctx->tex_data + old_pos * 4, &prev_pos) && prev_pos <= old_pos)
                ff_hashtable_delete(ctx->color_ht, ctx->tex_data + old_pos * 4);
        }
        ff_hashtable_set(ctx->color_ht, ctx->tex_data + pos * 4, &pos);
        pos++;

        if (!combo_idx) {
            idx = ff_hashtable_get(ctx->lut_ht, ctx->tex_data + pos * 4, &prev_pos) ? pos - prev_pos : 0;
            PUSH_OP(2);
            if (!idx)
                bytestream2_put_le32(pbc, AV_RL32(ctx->tex_data + pos * 4));
        }
        if (pos >= LOOKBACK_WORDS) {
            old_pos = pos - LOOKBACK_WORDS;
            if (ff_hashtable_get(ctx->lut_ht, ctx->tex_data + old_pos * 4, &prev_pos) && prev_pos <= old_pos)
                ff_hashtable_delete(ctx->lut_ht, ctx->tex_data + old_pos * 4);
        }
        ff_hashtable_set(ctx->lut_ht, ctx->tex_data + pos * 4, &pos);
        pos++;
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
        uint8_t *safe_data[4] = {frame->data[0], 0, 0, 0};
        int safe_linesize[4] = {frame->linesize[0], 0, 0, 0};

        if (avctx->width != DXV_ALIGN(avctx->width) || avctx->height != DXV_ALIGN(avctx->height)) {
            ret = av_image_alloc(
                safe_data,
                safe_linesize,
                DXV_ALIGN(avctx->width),
                DXV_ALIGN(avctx->height),
                avctx->pix_fmt,
                1);
            if (ret < 0)
                return ret;

            av_image_copy2(
                safe_data,
                safe_linesize,
                frame->data,
                frame->linesize,
                avctx->pix_fmt,
                avctx->width,
                avctx->height);

            if (avctx->width != DXV_ALIGN(avctx->width)) {
                for (int y = 0; y < avctx->height; y++) {
                    memset(safe_data[0] + y * safe_linesize[0] + frame->linesize[0], 0, safe_linesize[0] - frame->linesize[0]);
                }
            }
            if (avctx->height != DXV_ALIGN(avctx->height)) {
                for (int y = avctx->height; y < DXV_ALIGN(avctx->height); y++) {
                    memset(safe_data[0] + y * safe_linesize[0], 0, safe_linesize[0]);
                }
            }
        }

        ctx->enc.tex_data.out = ctx->tex_data;
        ctx->enc.frame_data.in = safe_data[0];
        ctx->enc.stride = safe_linesize[0];
        ctx->enc.width  = DXV_ALIGN(avctx->width);
        ctx->enc.height = DXV_ALIGN(avctx->height);
        ff_texturedsp_exec_compress_threads(avctx, &ctx->enc);

        if (safe_data[0] != frame->data[0])
            av_freep(&safe_data[0]);
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
    ctx->tex_size = DXV_ALIGN(avctx->width) / TEXTURE_BLOCK_W *
                    DXV_ALIGN(avctx->height) / TEXTURE_BLOCK_H *
                    ctx->enc.tex_ratio;
    ctx->enc.slice_count = av_clip(avctx->thread_count, 1, DXV_ALIGN(avctx->height) / TEXTURE_BLOCK_H);

    ctx->tex_data = av_malloc(ctx->tex_size);
    if (!ctx->tex_data) {
        return AVERROR(ENOMEM);
    }

    ret = ff_hashtable_alloc(&ctx->color_ht, sizeof(uint32_t), sizeof(uint32_t), LOOKBACK_HT_ELEMS);
    if (ret < 0)
        return ret;
    ret = ff_hashtable_alloc(&ctx->lut_ht, sizeof(uint32_t), sizeof(uint32_t), LOOKBACK_HT_ELEMS);
    if (ret < 0)
        return ret;
    ret = ff_hashtable_alloc(&ctx->combo_ht, sizeof(uint64_t), sizeof(uint32_t), LOOKBACK_HT_ELEMS);
    if (ret < 0)
        return ret;

    return 0;
}

static av_cold int dxv_close(AVCodecContext *avctx)
{
    DXVEncContext *ctx = avctx->priv_data;

    av_freep(&ctx->tex_data);

    ff_hashtable_freep(&ctx->color_ht);
    ff_hashtable_freep(&ctx->lut_ht);
    ff_hashtable_freep(&ctx->combo_ht);

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
    CODEC_PIXFMTS(AV_PIX_FMT_RGBA),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
