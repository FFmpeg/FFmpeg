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

#include "bsf.h"
#include "bsf_internal.h"
#include "cbs.h"
#include "cbs_av1.h"

typedef struct AV1FMergeContext {
    CodedBitstreamContext *cbc;
    CodedBitstreamFragment frag[2];
    AVPacket *pkt, *in;
    int idx;
} AV1FMergeContext;

static void av1_frame_merge_flush(AVBSFContext *bsf)
{
    AV1FMergeContext *ctx = bsf->priv_data;

    ff_cbs_fragment_reset(ctx->cbc, &ctx->frag[0]);
    ff_cbs_fragment_reset(ctx->cbc, &ctx->frag[1]);
    av_packet_unref(ctx->in);
    av_packet_unref(ctx->pkt);
}

static int av1_frame_merge_filter(AVBSFContext *bsf, AVPacket *out)
{
    AV1FMergeContext *ctx = bsf->priv_data;
    CodedBitstreamFragment *frag = &ctx->frag[ctx->idx], *tu = &ctx->frag[!ctx->idx];
    AVPacket *in = ctx->in, *buffer_pkt = ctx->pkt;
    int err, i;

    err = ff_bsf_get_packet_ref(bsf, in);
    if (err < 0) {
        if (err == AVERROR_EOF && tu->nb_units > 0)
            goto eof;
        return err;
    }

    err = ff_cbs_read_packet(ctx->cbc, frag, in);
    if (err < 0) {
        av_log(bsf, AV_LOG_ERROR, "Failed to read packet.\n");
        goto fail;
    }

    if (frag->nb_units == 0) {
        av_log(bsf, AV_LOG_ERROR, "No OBU in packet.\n");
        err = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (tu->nb_units == 0 && frag->units[0].type != AV1_OBU_TEMPORAL_DELIMITER) {
        av_log(bsf, AV_LOG_ERROR, "Missing Temporal Delimiter.\n");
        err = AVERROR_INVALIDDATA;
        goto fail;
    }

    for (i = 1; i < frag->nb_units; i++) {
        if (frag->units[i].type == AV1_OBU_TEMPORAL_DELIMITER) {
            av_log(bsf, AV_LOG_ERROR, "Temporal Delimiter in the middle of a packet.\n");
            err = AVERROR_INVALIDDATA;
            goto fail;
        }
    }

    if (tu->nb_units > 0 && frag->units[0].type == AV1_OBU_TEMPORAL_DELIMITER) {
eof:
        err = ff_cbs_write_packet(ctx->cbc, buffer_pkt, tu);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Failed to write packet.\n");
            goto fail;
        }
        av_packet_move_ref(out, buffer_pkt);

        // Swap fragment index, to avoid copying fragment references.
        ctx->idx = !ctx->idx;
    } else {
        for (i = 0; i < frag->nb_units; i++) {
            err = ff_cbs_insert_unit_content(ctx->cbc, tu, -1, frag->units[i].type,
                                             frag->units[i].content, frag->units[i].content_ref);
            if (err < 0)
                goto fail;
        }

        err = AVERROR(EAGAIN);
    }

    // Buffer packets with timestamps. There should be at most one per TU, be it split or not.
    if (!buffer_pkt->data && in->pts != AV_NOPTS_VALUE)
        av_packet_move_ref(buffer_pkt, in);
    else
        av_packet_unref(in);

    ff_cbs_fragment_reset(ctx->cbc, &ctx->frag[ctx->idx]);

fail:
    if (err < 0 && err != AVERROR(EAGAIN))
        av1_frame_merge_flush(bsf);

    return err;
}

static int av1_frame_merge_init(AVBSFContext *bsf)
{
    AV1FMergeContext *ctx = bsf->priv_data;

    ctx->in  = av_packet_alloc();
    ctx->pkt = av_packet_alloc();
    if (!ctx->in || !ctx->pkt)
        return AVERROR(ENOMEM);

    return ff_cbs_init(&ctx->cbc, AV_CODEC_ID_AV1, bsf);
}

static void av1_frame_merge_close(AVBSFContext *bsf)
{
    AV1FMergeContext *ctx = bsf->priv_data;

    ff_cbs_fragment_free(ctx->cbc, &ctx->frag[0]);
    ff_cbs_fragment_free(ctx->cbc, &ctx->frag[1]);
    av_packet_free(&ctx->in);
    av_packet_free(&ctx->pkt);
    ff_cbs_close(&ctx->cbc);
}

static const enum AVCodecID av1_frame_merge_codec_ids[] = {
    AV_CODEC_ID_AV1, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_av1_frame_merge_bsf = {
    .name           = "av1_frame_merge",
    .priv_data_size = sizeof(AV1FMergeContext),
    .init           = av1_frame_merge_init,
    .flush          = av1_frame_merge_flush,
    .close          = av1_frame_merge_close,
    .filter         = av1_frame_merge_filter,
    .codec_ids      = av1_frame_merge_codec_ids,
};
