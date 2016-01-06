/*
 * Resolume DXV decoder
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
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

#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "lzf.h"
#include "texturedsp.h"
#include "thread.h"

typedef struct DXVContext {
    TextureDSPContext texdsp;
    GetByteContext gbc;

    uint8_t *tex_data;  // Compressed texture
    int tex_rat;        // Compression ratio
    int tex_step;       // Distance between blocks
    int64_t tex_size;   // Texture size

    /* Optimal number of slices for parallel decoding */
    int slice_count;

    /* Pointer to the selected decompression function */
    int (*tex_funct)(uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
} DXVContext;

static int decompress_texture_thread(AVCodecContext *avctx, void *arg,
                                     int slice, int thread_nb)
{
    DXVContext *ctx = avctx->priv_data;
    AVFrame *frame = arg;
    const uint8_t *d = ctx->tex_data;
    int w_block = avctx->coded_width / TEXTURE_BLOCK_W;
    int h_block = avctx->coded_height / TEXTURE_BLOCK_H;
    int x, y;
    int start_slice, end_slice;
    int base_blocks_per_slice = h_block / ctx->slice_count;
    int remainder_blocks = h_block % ctx->slice_count;

    /* When the frame height (in blocks) doesn't divide evenly between the
     * number of slices, spread the remaining blocks evenly between the first
     * operations */
    start_slice = slice * base_blocks_per_slice;
    /* Add any extra blocks (one per slice) that have been added
     * before this slice */
    start_slice += FFMIN(slice, remainder_blocks);

    end_slice = start_slice + base_blocks_per_slice;
    /* Add an extra block if there are remainder blocks to be accounted for */
    if (slice < remainder_blocks)
        end_slice++;

    for (y = start_slice; y < end_slice; y++) {
        uint8_t *p = frame->data[0] + y * frame->linesize[0] * TEXTURE_BLOCK_H;
        int off  = y * w_block;
        for (x = 0; x < w_block; x++) {
            ctx->tex_funct(p + x * 16, frame->linesize[0],
                           d + (off + x) * ctx->tex_step);
        }
    }

    return 0;
}

/* This scheme addresses already decoded elements depending on 2-bit status:
 *   0 -> copy new element
 *   1 -> copy one element from position -x
 *   2 -> copy one element from position -(get_byte() + 2) * x
 *   3 -> copy one element from position -(get_16le() + 0x102) * x
 * x is always 2 for dxt1 and 4 for dxt5. */
#define CHECKPOINT(x)                                                         \
    do {                                                                      \
        if (state == 0) {                                                     \
            value = bytestream2_get_le32(gbc);                                \
            state = 16;                                                       \
        }                                                                     \
        op = value & 0x3;                                                     \
        value >>= 2;                                                          \
        state--;                                                              \
        switch (op) {                                                         \
        case 1:                                                               \
            idx = x;                                                          \
            break;                                                            \
        case 2:                                                               \
            idx = (bytestream2_get_byte(gbc) + 2) * x;                        \
            if (idx > pos) {                                                  \
                av_log(avctx, AV_LOG_ERROR, "idx %d > %d\n", idx, pos);       \
                return AVERROR_INVALIDDATA;                                   \
            }                                                                 \
            break;                                                            \
        case 3:                                                               \
            idx = (bytestream2_get_le16(gbc) + 0x102) * x;                    \
            if (idx > pos) {                                                  \
                av_log(avctx, AV_LOG_ERROR, "idx %d > %d\n", idx, pos);       \
                return AVERROR_INVALIDDATA;                                   \
            }                                                                 \
            break;                                                            \
        }                                                                     \
    } while(0)

static int dxv_decompress_dxt1(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    uint32_t value, prev, op;
    int idx = 0, state = 0;
    int pos = 2;

    /* Copy the first two elements */
    AV_WL32(ctx->tex_data, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data + 4, bytestream2_get_le32(gbc));

    /* Process input until the whole texture has been filled */
    while (pos < ctx->tex_size / 4) {
        CHECKPOINT(2);

        /* Copy two elements from a previous offset or from the input buffer */
        if (op) {
            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        } else {
            CHECKPOINT(2);

            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            CHECKPOINT(2);

            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        }
    }

    return 0;
}

static int dxv_decompress_dxt5(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    uint32_t value, op;
    int idx, prev, state = 0;
    int pos = 4;
    int run = 0;
    int probe, check;

    /* Copy the first four elements */
    AV_WL32(ctx->tex_data +  0, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data +  4, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data +  8, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data + 12, bytestream2_get_le32(gbc));

    /* Process input until the whole texture has been filled */
    while (pos < ctx->tex_size / 4) {
        if (run) {
            run--;

            prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
            prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        } else {
            if (state == 0) {
                value = bytestream2_get_le32(gbc);
                state = 16;
            }
            op = value & 0x3;
            value >>= 2;
            state--;

            switch (op) {
            case 0:
                /* Long copy */
                check = bytestream2_get_byte(gbc) + 1;
                if (check == 256) {
                    do {
                        probe = bytestream2_get_le16(gbc);
                        check += probe;
                    } while (probe == 0xFFFF);
                }
                while (check && pos < ctx->tex_size / 4) {
                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    check--;
                }

                /* Restart (or exit) the loop */
                continue;
                break;
            case 1:
                /* Load new run value */
                run = bytestream2_get_byte(gbc);
                if (run == 255) {
                    do {
                        probe = bytestream2_get_le16(gbc);
                        run += probe;
                    } while (probe == 0xFFFF);
                }

                /* Copy two dwords from previous data */
                prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;

                prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;
                break;
            case 2:
                /* Copy two dwords from a previous index */
                idx = 8 + bytestream2_get_le16(gbc);
                if (idx > pos) {
                    av_log(avctx, AV_LOG_ERROR, "idx %d > %d\n", idx, pos);
                    return AVERROR_INVALIDDATA;
                }
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;

                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;
                break;
            case 3:
                /* Copy two dwords from input */
                prev = bytestream2_get_le32(gbc);
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;

                prev = bytestream2_get_le32(gbc);
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;
                break;
            }
        }

        CHECKPOINT(4);

        /* Copy two elements from a previous offset or from the input buffer */
        if (op) {
            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        } else {
            CHECKPOINT(4);

            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            CHECKPOINT(4);

            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        }
    }

    return 0;
}

static int dxv_decompress_lzf(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    return ff_lzf_uncompress(&ctx->gbc, &ctx->tex_data, &ctx->tex_size);
}

static int dxv_decompress_raw(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;

    bytestream2_get_buffer(gbc, ctx->tex_data, ctx->tex_size);
    return 0;
}

static int dxv_decode(AVCodecContext *avctx, void *data,
                      int *got_frame, AVPacket *avpkt)
{
    DXVContext *ctx = avctx->priv_data;
    ThreadFrame tframe;
    GetByteContext *gbc = &ctx->gbc;
    int (*decompress_tex)(AVCodecContext *avctx);
    const char *msgcomp, *msgtext;
    uint32_t tag;
    int version_major, version_minor = 0;
    int size = 0, old_type = 0;
    int ret;

    bytestream2_init(gbc, avpkt->data, avpkt->size);

    tag = bytestream2_get_le32(gbc);
    switch (tag) {
    case MKBETAG('D', 'X', 'T', '1'):
        decompress_tex = dxv_decompress_dxt1;
        ctx->tex_funct = ctx->texdsp.dxt1_block;
        ctx->tex_rat   = 8;
        ctx->tex_step  = 8;
        msgcomp = "DXTR1";
        msgtext = "DXT1";
        break;
    case MKBETAG('D', 'X', 'T', '5'):
        decompress_tex = dxv_decompress_dxt5;
        ctx->tex_funct = ctx->texdsp.dxt5_block;
        ctx->tex_rat   = 4;
        ctx->tex_step  = 16;
        msgcomp = "DXTR5";
        msgtext = "DXT5";
        break;
    case MKBETAG('Y', 'C', 'G', '6'):
    case MKBETAG('Y', 'G', '1', '0'):
        avpriv_report_missing_feature(avctx, "Tag 0x%08X", tag);
        return AVERROR_PATCHWELCOME;
    default:
        /* Old version does not have a real header, just size and type. */
        size = tag & 0x00FFFFFF;
        old_type = tag >> 24;
        version_major = (old_type & 0x0F) - 1;

        if (old_type & 0x80) {
            msgcomp = "RAW";
            decompress_tex = dxv_decompress_raw;
        } else {
            msgcomp = "LZF";
            decompress_tex = dxv_decompress_lzf;
        }

        if (old_type & 0x40) {
            msgtext = "DXT5";

            ctx->tex_funct = ctx->texdsp.dxt5_block;
            ctx->tex_step  = 16;
        } else if (old_type & 0x20 || version_major == 1) {
            msgtext = "DXT1";

            ctx->tex_funct = ctx->texdsp.dxt1_block;
            ctx->tex_step  = 8;
        } else {
            av_log(avctx, AV_LOG_ERROR, "Unsupported header (0x%08X)\n.", tag);
            return AVERROR_INVALIDDATA;
        }
        ctx->tex_rat = 1;
        break;
    }

    /* New header is 12 bytes long. */
    if (!old_type) {
        version_major = bytestream2_get_byte(gbc) - 1;
        version_minor = bytestream2_get_byte(gbc);

        /* Encoder copies texture data when compression is not advantageous. */
        if (bytestream2_get_byte(gbc)) {
            msgcomp = "RAW";
            ctx->tex_rat = 1;
            decompress_tex = dxv_decompress_raw;
        }

        bytestream2_skip(gbc, 1); // unknown
        size = bytestream2_get_le32(gbc);
    }
    av_log(avctx, AV_LOG_DEBUG,
           "%s compression with %s texture (version %d.%d)\n",
           msgcomp, msgtext, version_major, version_minor);

    if (size != bytestream2_get_bytes_left(gbc)) {
        av_log(avctx, AV_LOG_ERROR,
               "Incomplete or invalid file (header %d, left %d).\n",
               size, bytestream2_get_bytes_left(gbc));
        return AVERROR_INVALIDDATA;
    }

    ctx->tex_size = avctx->coded_width * avctx->coded_height * 4 / ctx->tex_rat;
    ret = av_reallocp(&ctx->tex_data, ctx->tex_size);
    if (ret < 0)
        return ret;

    /* Decompress texture out of the intermediate compression. */
    ret = decompress_tex(avctx);
    if (ret < 0)
        return ret;

    tframe.f = data;
    ret = ff_thread_get_buffer(avctx, &tframe, 0);
    if (ret < 0)
        return ret;

    /* Now decompress the texture with the standard functions. */
    avctx->execute2(avctx, decompress_texture_thread,
                    tframe.f, NULL, ctx->slice_count);

    /* Frame is ready to be output. */
    tframe.f->pict_type = AV_PICTURE_TYPE_I;
    tframe.f->key_frame = 1;
    *got_frame = 1;

    return avpkt->size;
}

static int dxv_init(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    int ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);

    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid image size %dx%d.\n",
               avctx->width, avctx->height);
        return ret;
    }

    /* Codec requires 16x16 alignment. */
    avctx->coded_width  = FFALIGN(avctx->width,  16);
    avctx->coded_height = FFALIGN(avctx->height, 16);

    ff_texturedsp_init(&ctx->texdsp);
    avctx->pix_fmt = AV_PIX_FMT_RGBA;

    ctx->slice_count = av_clip(avctx->thread_count, 1,
                               avctx->coded_height / TEXTURE_BLOCK_H);

    return 0;
}

static int dxv_close(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;

    av_freep(&ctx->tex_data);

    return 0;
}

AVCodec ff_dxv_decoder = {
    .name           = "dxv",
    .long_name      = NULL_IF_CONFIG_SMALL("Resolume DXV"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DXV,
    .init           = dxv_init,
    .decode         = dxv_decode,
    .close          = dxv_close,
    .priv_data_size = sizeof(DXVContext),
    .capabilities   = AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_SLICE_THREADS |
                      AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};
