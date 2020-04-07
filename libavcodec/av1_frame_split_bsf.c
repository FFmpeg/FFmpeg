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

/**
 * @file
 * This bitstream filter splits AV1 Temporal Units into packets containing
 * just one frame, plus any leading and trailing OBUs that may be present at
 * the beginning or end, respectively.
 *
 * Temporal Units already containing only one frame will be passed through
 * unchanged. When splitting can't be performed, the Temporal Unit will be
 * passed through containing only the remaining OBUs starting from the first
 * one after the last successfully split frame.
 */

#include "libavutil/avassert.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "cbs.h"
#include "cbs_av1.h"

typedef struct AV1FSplitContext {
    AVPacket *buffer_pkt;
    CodedBitstreamContext *cbc;
    CodedBitstreamFragment temporal_unit;

    int nb_frames;
    int cur_frame;
    int cur_frame_idx;
    int last_frame_idx;
} AV1FSplitContext;

static int av1_frame_split_filter(AVBSFContext *ctx, AVPacket *out)
{
    AV1FSplitContext *s = ctx->priv_data;
    CodedBitstreamFragment *td = &s->temporal_unit;
    int i, ret;
    int split = !!s->buffer_pkt->data;

    if (!s->buffer_pkt->data) {
        int nb_frames = 0;

        ret = ff_bsf_get_packet_ref(ctx, s->buffer_pkt);
        if (ret < 0)
            return ret;

        ret = ff_cbs_read_packet(s->cbc, td, s->buffer_pkt);
        if (ret < 0) {
            av_log(ctx, AV_LOG_WARNING, "Failed to parse temporal unit.\n");
            goto passthrough;
        }

        for (i = 0; i < td->nb_units; i++) {
            CodedBitstreamUnit *unit = &td->units[i];

            if (unit->type == AV1_OBU_FRAME ||
                unit->type == AV1_OBU_FRAME_HEADER)
                nb_frames++;
            else if (unit->type == AV1_OBU_TILE_LIST) {
                av_log(ctx, AV_LOG_VERBOSE, "Large scale tiles are unsupported.\n");
                goto passthrough;
            }
        }
        if (nb_frames > 1) {
            s->cur_frame = 0;
            s->cur_frame_idx = s->last_frame_idx = 0;
            s->nb_frames = nb_frames;
            split = 1;
        }
    }

    if (split) {
        AV1RawFrameHeader *frame = NULL;
        int cur_frame_type = -1, size = 0;

        for (i = s->cur_frame_idx; i < td->nb_units; i++) {
            CodedBitstreamUnit *unit = &td->units[i];

            size += unit->data_size;
            if (unit->type == AV1_OBU_FRAME) {
                AV1RawOBU *obu = unit->content;

                if (frame) {
                    av_log(ctx, AV_LOG_WARNING, "Frame OBU found when Tile data for a "
                                                "previous frame was expected.\n");
                    goto passthrough;
                }

                frame = &obu->obu.frame.header;
                cur_frame_type = obu->header.obu_type;
                s->last_frame_idx = s->cur_frame_idx;
                s->cur_frame_idx  = i + 1;
                s->cur_frame++;

                // split here unless it's the last frame, in which case
                // include every trailing OBU
                if (s->cur_frame < s->nb_frames)
                    break;
            } else if (unit->type == AV1_OBU_FRAME_HEADER) {
                AV1RawOBU *obu = unit->content;

                if (frame) {
                    av_log(ctx, AV_LOG_WARNING, "Frame Header OBU found when Tile data for a "
                                                "previous frame was expected.\n");
                    goto passthrough;
                }

                frame = &obu->obu.frame_header;
                cur_frame_type = obu->header.obu_type;
                s->last_frame_idx = s->cur_frame_idx;
                s->cur_frame++;

                // split here if show_existing_frame unless it's the last
                // frame, in which case include every trailing OBU
                if (frame->show_existing_frame &&
                    s->cur_frame < s->nb_frames) {
                    s->cur_frame_idx = i + 1;
                    break;
                }
            } else if (unit->type == AV1_OBU_TILE_GROUP) {
                AV1RawOBU *obu = unit->content;
                AV1RawTileGroup *group = &obu->obu.tile_group;

                if (!frame || cur_frame_type != AV1_OBU_FRAME_HEADER) {
                    av_log(ctx, AV_LOG_WARNING, "Unexpected Tile Group OBU found before a "
                                                "Frame Header.\n");
                    goto passthrough;
                }

                if ((group->tg_end == (frame->tile_cols * frame->tile_rows) - 1) &&
                    // include every trailing OBU with the last frame
                    s->cur_frame < s->nb_frames) {
                    s->cur_frame_idx = i + 1;
                    break;
                }
            }
        }
        av_assert0(frame && s->cur_frame <= s->nb_frames);

        ret = av_packet_ref(out, s->buffer_pkt);
        if (ret < 0)
            goto fail;

        out->data = (uint8_t *)td->units[s->last_frame_idx].data;
        out->size = size;

        // skip the frame in the buffer packet if it's split successfully, so it's not present
        // if the packet is passed through in case of failure when splitting another frame.
        s->buffer_pkt->data += size;
        s->buffer_pkt->size -= size;

        if (!frame->show_existing_frame && !frame->show_frame)
            out->pts = AV_NOPTS_VALUE;

        if (s->cur_frame == s->nb_frames) {
            av_packet_unref(s->buffer_pkt);
            ff_cbs_fragment_reset(s->cbc, td);
        }

        return 0;
    }

passthrough:
    av_packet_move_ref(out, s->buffer_pkt);

    ret = 0;
fail:
    if (ret < 0) {
        av_packet_unref(out);
        av_packet_unref(s->buffer_pkt);
    }
    ff_cbs_fragment_reset(s->cbc, td);

    return ret;
}

static const CodedBitstreamUnitType decompose_unit_types[] = {
    AV1_OBU_TEMPORAL_DELIMITER,
    AV1_OBU_SEQUENCE_HEADER,
    AV1_OBU_FRAME_HEADER,
    AV1_OBU_TILE_GROUP,
    AV1_OBU_FRAME,
};

static int av1_frame_split_init(AVBSFContext *ctx)
{
    AV1FSplitContext *s = ctx->priv_data;
    CodedBitstreamFragment *td = &s->temporal_unit;
    int ret;

    s->buffer_pkt = av_packet_alloc();
    if (!s->buffer_pkt)
        return AVERROR(ENOMEM);

    ret = ff_cbs_init(&s->cbc, AV_CODEC_ID_AV1, ctx);
    if (ret < 0)
        return ret;

    s->cbc->decompose_unit_types    = (CodedBitstreamUnitType*)decompose_unit_types;
    s->cbc->nb_decompose_unit_types = FF_ARRAY_ELEMS(decompose_unit_types);

    if (!ctx->par_in->extradata_size)
        return 0;

    ret = ff_cbs_read_extradata(s->cbc, td, ctx->par_in);
    if (ret < 0)
        av_log(ctx, AV_LOG_WARNING, "Failed to parse extradata.\n");

    ff_cbs_fragment_reset(s->cbc, td);

    return 0;
}

static void av1_frame_split_flush(AVBSFContext *ctx)
{
    AV1FSplitContext *s = ctx->priv_data;

    av_packet_unref(s->buffer_pkt);
    ff_cbs_fragment_reset(s->cbc, &s->temporal_unit);
}

static void av1_frame_split_close(AVBSFContext *ctx)
{
    AV1FSplitContext *s = ctx->priv_data;

    av_packet_free(&s->buffer_pkt);
    ff_cbs_fragment_free(s->cbc, &s->temporal_unit);
    ff_cbs_close(&s->cbc);
}

static const enum AVCodecID av1_frame_split_codec_ids[] = {
    AV_CODEC_ID_AV1, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_av1_frame_split_bsf = {
    .name           = "av1_frame_split",
    .priv_data_size = sizeof(AV1FSplitContext),
    .init           = av1_frame_split_init,
    .flush          = av1_frame_split_flush,
    .close          = av1_frame_split_close,
    .filter         = av1_frame_split_filter,
    .codec_ids      = av1_frame_split_codec_ids,
};
