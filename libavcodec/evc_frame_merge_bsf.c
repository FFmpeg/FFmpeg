/*
 * Copyright (c) 2019 James Almer <jamrial@gmail.com>
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
#include "get_bits.h"
#include "golomb.h"
#include "bsf.h"
#include "bsf_internal.h"
#include "avcodec.h"

#include "evc.h"
#include "evc_parse.h"

// Access unit data
typedef struct AccessUnitBuffer {
    uint8_t *data;      // the data buffer
    size_t data_size;   // size of data in bytes
    unsigned capacity;  // buffer capacity
} AccessUnitBuffer;

typedef struct EVCFMergeContext {
    AVPacket *in;
    EVCParserContext parser_ctx;
    AccessUnitBuffer au_buffer;
} EVCFMergeContext;

static int end_of_access_unit_found(EVCParserContext *parser_ctx)
{
    if (parser_ctx->profile == 0) { // BASELINE profile
        if (parser_ctx->nalu_type == EVC_NOIDR_NUT || parser_ctx->nalu_type == EVC_IDR_NUT)
            return 1;
    } else { // MAIN profile
        if (parser_ctx->nalu_type == EVC_NOIDR_NUT) {
            if (parser_ctx->poc.PicOrderCntVal != parser_ctx->poc.prevPicOrderCntVal)
                return 1;
        } else if (parser_ctx->nalu_type == EVC_IDR_NUT)
            return 1;
    }
    return 0;
}

static void evc_frame_merge_flush(AVBSFContext *bsf)
{
    EVCFMergeContext *ctx = bsf->priv_data;

    ff_evc_parse_free(&ctx->parser_ctx);
    av_packet_unref(ctx->in);
    ctx->au_buffer.data_size = 0;
}

static int evc_frame_merge_filter(AVBSFContext *bsf, AVPacket *out)
{
    EVCFMergeContext *ctx = bsf->priv_data;
    EVCParserContext *parser_ctx = &ctx->parser_ctx;

    AVPacket *in = ctx->in;

    size_t  nalu_size = 0;
    uint8_t *buffer, *nalu = NULL;
    int au_end_found = 0;
    int err;

    err = ff_bsf_get_packet_ref(bsf, in);
    if (err < 0)
        return err;

    nalu_size = evc_read_nal_unit_length(in->data, EVC_NALU_LENGTH_PREFIX_SIZE, bsf);
    if (nalu_size <= 0) {
        av_packet_unref(in);
        return AVERROR_INVALIDDATA;
    }

    nalu = in->data + EVC_NALU_LENGTH_PREFIX_SIZE;
    nalu_size = in->size - EVC_NALU_LENGTH_PREFIX_SIZE;

    // NAL unit parsing needed to determine if end of AU was found
    err = ff_evc_parse_nal_unit(parser_ctx, nalu, nalu_size, bsf);
    if (err < 0) {
        av_log(bsf, AV_LOG_ERROR, "NAL Unit parsing error\n");
        av_packet_unref(in);

        return err;
    }

    au_end_found = end_of_access_unit_found(parser_ctx);

    buffer = av_fast_realloc(ctx->au_buffer.data, &ctx->au_buffer.capacity,
                             ctx->au_buffer.data_size + in->size);
    if (!buffer) {
        av_freep(&ctx->au_buffer.data);
        return AVERROR(ENOMEM);
    }

    ctx->au_buffer.data = buffer;
    memcpy(ctx->au_buffer.data + ctx->au_buffer.data_size, in->data, in->size);

    ctx->au_buffer.data_size += in->size;

    av_packet_unref(in);

    if (au_end_found) {
        size_t data_size = ctx->au_buffer.data_size;

        ctx->au_buffer.data_size = 0;
        err = av_new_packet(out, data_size);
        if (err < 0)
            return err;

        memcpy(out->data, ctx->au_buffer.data, data_size);
    } else
        err = AVERROR(EAGAIN);

    if (err < 0 && err != AVERROR(EAGAIN))
        ctx->au_buffer.data_size = 0;

    return err;
}

static int evc_frame_merge_init(AVBSFContext *bsf)
{
    EVCFMergeContext *ctx = bsf->priv_data;

    ctx->in  = av_packet_alloc();
    if (!ctx->in)
        return AVERROR(ENOMEM);

    return 0;
}

static void evc_frame_merge_close(AVBSFContext *bsf)
{
    EVCFMergeContext *ctx = bsf->priv_data;

    av_packet_free(&ctx->in);
    ff_evc_parse_free(&ctx->parser_ctx);

    ctx->au_buffer.capacity = 0;
    av_freep(&ctx->au_buffer.data);
    ctx->au_buffer.data_size = 0;
}

static const enum AVCodecID evc_frame_merge_codec_ids[] = {
    AV_CODEC_ID_EVC, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_evc_frame_merge_bsf = {
    .p.name         = "evc_frame_merge",
    .p.codec_ids    = evc_frame_merge_codec_ids,
    .priv_data_size = sizeof(EVCFMergeContext),
    .init           = evc_frame_merge_init,
    .flush          = evc_frame_merge_flush,
    .close          = evc_frame_merge_close,
    .filter         = evc_frame_merge_filter,
};
