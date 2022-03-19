/*
 * Vidvox Hap decoder
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
 * Copyright (C) 2015 Tom Butterworth <bangnoise@gmail.com>
 *
 * HapQA and HAPAlphaOnly added by Jokyo Images
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
 * Hap decoder
 *
 * Fourcc: Hap1, Hap5, HapY, HapA, HapM
 *
 * https://github.com/Vidvox/hap/blob/master/documentation/HapVideoDRAFT.md
 */

#include <stdint.h>

#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "hap.h"
#include "snappy.h"
#include "texturedsp.h"
#include "thread.h"

static int hap_parse_decode_instructions(HapContext *ctx, int size)
{
    GetByteContext *gbc = &ctx->gbc;
    int section_size;
    enum HapSectionType section_type;
    int is_first_table = 1, had_offsets = 0, had_compressors = 0, had_sizes = 0;
    int i, ret;

    while (size > 0) {
        int stream_remaining = bytestream2_get_bytes_left(gbc);
        ret = ff_hap_parse_section_header(gbc, &section_size, &section_type);
        if (ret != 0)
            return ret;

        size -= stream_remaining - bytestream2_get_bytes_left(gbc);

        switch (section_type) {
            case HAP_ST_COMPRESSOR_TABLE:
                ret = ff_hap_set_chunk_count(ctx, section_size, is_first_table);
                if (ret != 0)
                    return ret;
                for (i = 0; i < section_size; i++) {
                    ctx->chunks[i].compressor = bytestream2_get_byte(gbc) << 4;
                }
                had_compressors = 1;
                is_first_table = 0;
                break;
            case HAP_ST_SIZE_TABLE:
                ret = ff_hap_set_chunk_count(ctx, section_size / 4, is_first_table);
                if (ret != 0)
                    return ret;
                for (i = 0; i < section_size / 4; i++) {
                    ctx->chunks[i].compressed_size = bytestream2_get_le32(gbc);
                }
                had_sizes = 1;
                is_first_table = 0;
                break;
            case HAP_ST_OFFSET_TABLE:
                ret = ff_hap_set_chunk_count(ctx, section_size / 4, is_first_table);
                if (ret != 0)
                    return ret;
                for (i = 0; i < section_size / 4; i++) {
                    ctx->chunks[i].compressed_offset = bytestream2_get_le32(gbc);
                }
                had_offsets = 1;
                is_first_table = 0;
                break;
            default:
                break;
        }
        size -= section_size;
    }

    if (!had_sizes || !had_compressors)
        return AVERROR_INVALIDDATA;

    /* The offsets table is optional. If not present than calculate offsets by
     * summing the sizes of preceding chunks. */
    if (!had_offsets) {
        size_t running_size = 0;
        for (i = 0; i < ctx->chunk_count; i++) {
            ctx->chunks[i].compressed_offset = running_size;
            if (ctx->chunks[i].compressed_size > UINT32_MAX - running_size)
                return AVERROR_INVALIDDATA;
            running_size += ctx->chunks[i].compressed_size;
        }
    }

    return 0;
}

static int hap_can_use_tex_in_place(HapContext *ctx)
{
    int i;
    size_t running_offset = 0;
    for (i = 0; i < ctx->chunk_count; i++) {
        if (ctx->chunks[i].compressed_offset != running_offset
            || ctx->chunks[i].compressor != HAP_COMP_NONE)
            return 0;
        running_offset += ctx->chunks[i].compressed_size;
    }
    return 1;
}

static int hap_parse_frame_header(AVCodecContext *avctx)
{
    HapContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    int section_size;
    enum HapSectionType section_type;
    const char *compressorstr;
    int i, ret;

    ret = ff_hap_parse_section_header(gbc, &ctx->texture_section_size, &section_type);
    if (ret != 0)
        return ret;

    if ((avctx->codec_tag == MKTAG('H','a','p','1') && (section_type & 0x0F) != HAP_FMT_RGBDXT1) ||
        (avctx->codec_tag == MKTAG('H','a','p','5') && (section_type & 0x0F) != HAP_FMT_RGBADXT5) ||
        (avctx->codec_tag == MKTAG('H','a','p','Y') && (section_type & 0x0F) != HAP_FMT_YCOCGDXT5) ||
        (avctx->codec_tag == MKTAG('H','a','p','A') && (section_type & 0x0F) != HAP_FMT_RGTC1) ||
        ((avctx->codec_tag == MKTAG('H','a','p','M') && (section_type & 0x0F) != HAP_FMT_RGTC1) &&
                                                        (section_type & 0x0F) != HAP_FMT_YCOCGDXT5)) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid texture format %#04x.\n", section_type & 0x0F);
        return AVERROR_INVALIDDATA;
    }

    switch (section_type & 0xF0) {
        case HAP_COMP_NONE:
        case HAP_COMP_SNAPPY:
            ret = ff_hap_set_chunk_count(ctx, 1, 1);
            if (ret == 0) {
                ctx->chunks[0].compressor = section_type & 0xF0;
                ctx->chunks[0].compressed_offset = 0;
                ctx->chunks[0].compressed_size = ctx->texture_section_size;
            }
            if (ctx->chunks[0].compressor == HAP_COMP_NONE) {
                compressorstr = "none";
            } else {
                compressorstr = "snappy";
            }
            break;
        case HAP_COMP_COMPLEX:
            ret = ff_hap_parse_section_header(gbc, &section_size, &section_type);
            if (ret == 0 && section_type != HAP_ST_DECODE_INSTRUCTIONS)
                ret = AVERROR_INVALIDDATA;
            if (ret == 0)
                ret = hap_parse_decode_instructions(ctx, section_size);
            compressorstr = "complex";
            break;
        default:
            ret = AVERROR_INVALIDDATA;
            break;
    }

    if (ret != 0)
        return ret;

    /* Check the frame is valid and read the uncompressed chunk sizes */
    ctx->tex_size = 0;
    for (i = 0; i < ctx->chunk_count; i++) {
        HapChunk *chunk = &ctx->chunks[i];

        /* Check the compressed buffer is valid */
        if (chunk->compressed_offset + (uint64_t)chunk->compressed_size > bytestream2_get_bytes_left(gbc))
            return AVERROR_INVALIDDATA;

        /* Chunks are unpacked sequentially, ctx->tex_size is the uncompressed
         * size thus far */
        chunk->uncompressed_offset = ctx->tex_size;

        /* Fill out uncompressed size */
        if (chunk->compressor == HAP_COMP_SNAPPY) {
            GetByteContext gbc_tmp;
            int64_t uncompressed_size;
            bytestream2_init(&gbc_tmp, gbc->buffer + chunk->compressed_offset,
                             chunk->compressed_size);
            uncompressed_size = ff_snappy_peek_uncompressed_length(&gbc_tmp);
            if (uncompressed_size < 0) {
                return uncompressed_size;
            }
            chunk->uncompressed_size = uncompressed_size;
        } else if (chunk->compressor == HAP_COMP_NONE) {
            chunk->uncompressed_size = chunk->compressed_size;
        } else {
            return AVERROR_INVALIDDATA;
        }
        ctx->tex_size += chunk->uncompressed_size;
    }

    av_log(avctx, AV_LOG_DEBUG, "%s compressor\n", compressorstr);

    return ret;
}

static int decompress_chunks_thread(AVCodecContext *avctx, void *arg,
                                    int chunk_nb, int thread_nb)
{
    HapContext *ctx = avctx->priv_data;

    HapChunk *chunk = &ctx->chunks[chunk_nb];
    GetByteContext gbc;
    uint8_t *dst = ctx->tex_buf + chunk->uncompressed_offset;

    bytestream2_init(&gbc, ctx->gbc.buffer + chunk->compressed_offset, chunk->compressed_size);

    if (chunk->compressor == HAP_COMP_SNAPPY) {
        int ret;
        int64_t uncompressed_size = ctx->tex_size;

        /* Uncompress the frame */
        ret = ff_snappy_uncompress(&gbc, dst, &uncompressed_size);
        if (ret < 0) {
             av_log(avctx, AV_LOG_ERROR, "Snappy uncompress error\n");
             return ret;
        }
    } else if (chunk->compressor == HAP_COMP_NONE) {
        bytestream2_get_buffer(&gbc, dst, chunk->compressed_size);
    }

    return 0;
}

static int hap_decode(AVCodecContext *avctx, AVFrame *frame,
                      int *got_frame, AVPacket *avpkt)
{
    HapContext *ctx = avctx->priv_data;
    int ret, i, t;
    int section_size;
    enum HapSectionType section_type;
    int start_texture_section = 0;

    bytestream2_init(&ctx->gbc, avpkt->data, avpkt->size);

    /* check for multi texture header */
    if (ctx->texture_count == 2) {
        ret = ff_hap_parse_section_header(&ctx->gbc, &section_size, &section_type);
        if (ret != 0)
            return ret;
        if ((section_type & 0x0F) != 0x0D) {
            av_log(avctx, AV_LOG_ERROR, "Invalid section type in 2 textures mode %#04x.\n", section_type);
            return AVERROR_INVALIDDATA;
        }
        start_texture_section = 4;
    }

    /* Get the output frame ready to receive data */
    ret = ff_thread_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    for (t = 0; t < ctx->texture_count; t++) {
        bytestream2_seek(&ctx->gbc, start_texture_section, SEEK_SET);

        /* Check for section header */
        ret = hap_parse_frame_header(avctx);
        if (ret < 0)
            return ret;

        if (ctx->tex_size != (avctx->coded_width  / TEXTURE_BLOCK_W)
            *(avctx->coded_height / TEXTURE_BLOCK_H)
            *ctx->dec[t].tex_ratio) {
            av_log(avctx, AV_LOG_ERROR, "uncompressed size mismatches\n");
            return AVERROR_INVALIDDATA;
        }

        start_texture_section += ctx->texture_section_size + 4;

        /* Unpack the DXT texture */
        if (hap_can_use_tex_in_place(ctx)) {
            int tex_size;
            /* Only DXTC texture compression in a contiguous block */
            ctx->dec[t].tex_data.in = ctx->gbc.buffer;
            tex_size = FFMIN(ctx->texture_section_size, bytestream2_get_bytes_left(&ctx->gbc));
            if (tex_size < (avctx->coded_width  / TEXTURE_BLOCK_W)
                *(avctx->coded_height / TEXTURE_BLOCK_H)
                *ctx->dec[t].tex_ratio) {
                av_log(avctx, AV_LOG_ERROR, "Insufficient data\n");
                return AVERROR_INVALIDDATA;
            }
        } else {
            /* Perform the second-stage decompression */
            ret = av_reallocp(&ctx->tex_buf, ctx->tex_size);
            if (ret < 0)
                return ret;

            avctx->execute2(avctx, decompress_chunks_thread, NULL,
                            ctx->chunk_results, ctx->chunk_count);

            for (i = 0; i < ctx->chunk_count; i++) {
                if (ctx->chunk_results[i] < 0)
                    return ctx->chunk_results[i];
            }

            ctx->dec[t].tex_data.in = ctx->tex_buf;
        }

        ctx->dec[t].frame_data.out = frame->data[0];
        ctx->dec[t].stride = frame->linesize[0];
        avctx->execute2(avctx, ff_texturedsp_decompress_thread, &ctx->dec[t], NULL, ctx->dec[t].slice_count);
    }

    /* Frame is ready to be output */
    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;
    *got_frame = 1;

    return avpkt->size;
}

static av_cold int hap_init(AVCodecContext *avctx)
{
    HapContext *ctx = avctx->priv_data;
    const char *texture_name;
    int ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);

    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid video size %dx%d.\n",
               avctx->width, avctx->height);
        return ret;
    }

    /* Since codec is based on 4x4 blocks, size is aligned to 4 */
    avctx->coded_width  = FFALIGN(avctx->width,  TEXTURE_BLOCK_W);
    avctx->coded_height = FFALIGN(avctx->height, TEXTURE_BLOCK_H);

    ff_texturedsp_init(&ctx->dxtc);

    ctx->texture_count  = 1;
    ctx->dec[0].raw_ratio = 16;
    ctx->dec[0].slice_count = av_clip(avctx->thread_count, 1,
                                      avctx->coded_height / TEXTURE_BLOCK_H);

    switch (avctx->codec_tag) {
    case MKTAG('H','a','p','1'):
        texture_name = "DXT1";
        ctx->dec[0].tex_ratio = 8;
        ctx->dec[0].tex_funct = ctx->dxtc.dxt1_block;
        avctx->pix_fmt = AV_PIX_FMT_RGB0;
        break;
    case MKTAG('H','a','p','5'):
        texture_name = "DXT5";
        ctx->dec[0].tex_ratio = 16;
        ctx->dec[0].tex_funct = ctx->dxtc.dxt5_block;
        avctx->pix_fmt = AV_PIX_FMT_RGBA;
        break;
    case MKTAG('H','a','p','Y'):
        texture_name = "DXT5-YCoCg-scaled";
        ctx->dec[0].tex_ratio = 16;
        ctx->dec[0].tex_funct = ctx->dxtc.dxt5ys_block;
        avctx->pix_fmt = AV_PIX_FMT_RGB0;
        break;
    case MKTAG('H','a','p','A'):
        texture_name = "RGTC1";
        ctx->dec[0].tex_ratio = 8;
        ctx->dec[0].tex_funct = ctx->dxtc.rgtc1u_gray_block;
        ctx->dec[0].raw_ratio = 4;
        avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        break;
    case MKTAG('H','a','p','M'):
        texture_name  = "DXT5-YCoCg-scaled / RGTC1";
        ctx->dec[0].tex_ratio = 16;
        ctx->dec[1].tex_ratio = 8;
        ctx->dec[0].tex_funct = ctx->dxtc.dxt5ys_block;
        ctx->dec[1].tex_funct = ctx->dxtc.rgtc1u_alpha_block;
        ctx->dec[1].raw_ratio = 16;
        ctx->dec[1].slice_count = ctx->dec[0].slice_count;
        avctx->pix_fmt = AV_PIX_FMT_RGBA;
        ctx->texture_count = 2;
        break;
    default:
        return AVERROR_DECODER_NOT_FOUND;
    }

    av_log(avctx, AV_LOG_DEBUG, "%s texture\n", texture_name);

    return 0;
}

static av_cold int hap_close(AVCodecContext *avctx)
{
    HapContext *ctx = avctx->priv_data;

    ff_hap_free_context(ctx);

    return 0;
}

const FFCodec ff_hap_decoder = {
    .p.name         = "hap",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Vidvox Hap"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_HAP,
    .init           = hap_init,
    FF_CODEC_DECODE_CB(hap_decode),
    .close          = hap_close,
    .priv_data_size = sizeof(HapContext),
    .p.capabilities = AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_SLICE_THREADS |
                      AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .codec_tags     = (const uint32_t []){
        MKTAG('H','a','p','1'),
        MKTAG('H','a','p','5'),
        MKTAG('H','a','p','Y'),
        MKTAG('H','a','p','A'),
        MKTAG('H','a','p','M'),
        FF_CODEC_TAGS_END,
    },
};
