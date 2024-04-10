/*
 * Copyright (c) 2020 John Stebbins <jstebbins.hb@gmail.com>
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
 * This bitstream filter merges PGS subtitle packets containing incomplete
 * set of segments into a single packet
 *
 * Packets already containing a complete set of segments will be passed through
 * unchanged.
 */

#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "bsf.h"
#include "bsf_internal.h"

enum PGSSegmentType {
    PALETTE_SEGMENT         = 0x14,
    OBJECT_SEGMENT          = 0x15,
    PRESENTATION_SEGMENT    = 0x16,
    WINDOW_SEGMENT          = 0x17,
    END_DISPLAY_SET_SEGMENT = 0x80,
};

typedef struct PGSMergeContext {
    AVPacket *buffer_pkt, *in;
    int presentation_found;
    int pkt_flags;
} PGSMergeContext;

static av_cold void frame_merge_flush(AVBSFContext *bsf)
{
    PGSMergeContext *ctx = bsf->priv_data;

    ctx->presentation_found = ctx->pkt_flags = 0;
    av_packet_unref(ctx->in);
    av_packet_unref(ctx->buffer_pkt);
}

static int frame_merge_output(PGSMergeContext *ctx, AVPacket *dst, AVPacket *src)
{
    if (!ctx->presentation_found)
        ctx->pkt_flags |= AV_PKT_FLAG_CORRUPT;
    ctx->presentation_found = 0;
    src->flags    |= ctx->pkt_flags;
    ctx->pkt_flags = 0;
    av_packet_move_ref(dst, src);
    return 0;
}

static int frame_merge_filter(AVBSFContext *bsf, AVPacket *out)
{
    PGSMergeContext *ctx = bsf->priv_data;
    AVPacket *in = ctx->in, *pkt = ctx->buffer_pkt;
    int ret, size, pos, display = 0, presentation = 0;
    unsigned int i;

    if (!in->data) {
        ret = ff_bsf_get_packet_ref(bsf, in);
        if (ret == AVERROR_EOF && pkt->data) {
            // Output remaining data
            ctx->pkt_flags |= AV_PKT_FLAG_CORRUPT;
            return frame_merge_output(ctx, out, pkt);
        }
        if (ret < 0)
            return ret;
    }
    if (!in->size) {
        av_packet_unref(in);
        return AVERROR(EAGAIN);
    }
    in->flags &= ~AV_PKT_FLAG_KEY; // Will be detected in the stream

    // Validate packet data and find display_end segment
    size = in->size;
    i = 0;
    while (i + 3 <= in->size) {
        uint8_t segment_type = in->data[i];
        int     segment_len  = AV_RB16(in->data + i + 1) + 3;

        if (i + segment_len > in->size)
            break; // Invalid, segments can't span packets
        if (segment_type == PRESENTATION_SEGMENT && ctx->presentation_found)
            break; // Invalid, there can be only one
        if (segment_type == PRESENTATION_SEGMENT) {
            uint8_t state;
            if (segment_len < 11)
                break; // Invalid presentation segment length
            ctx->presentation_found = presentation = 1;
            state = in->data[i + 10] & 0xc0;
            if (state)
                ctx->pkt_flags |= AV_PKT_FLAG_KEY;
            else
                ctx->pkt_flags &= ~AV_PKT_FLAG_KEY;
        }
        i += segment_len;
        if (segment_type == END_DISPLAY_SET_SEGMENT) {
            size    = i;
            display = 1;
            break;
        }
    }
    if (display && pkt->size == 0 && size == in->size) // passthrough
        return frame_merge_output(ctx, out, in);
    if (!display && i != in->size) {
        av_log(bsf, AV_LOG_WARNING, "Failed to parse PGS segments.\n");
        // force output what we have
        size    = in->size;
        display = 1;
        ctx->pkt_flags |= AV_PKT_FLAG_CORRUPT;
    }

    if (presentation) {
        ret = av_packet_copy_props(pkt, in);
        if (ret < 0)
            goto fail;
    }
    pos = pkt->size;
    ret = av_grow_packet(pkt, size);
    if (ret < 0)
        goto fail;
    memcpy(pkt->data + pos, in->data, size);

    if (size == in->size)
        av_packet_unref(in);
    else {
        in->data += size;
        in->size -= size;
    }

    if (display)
        return frame_merge_output(ctx, out, pkt);
    return AVERROR(EAGAIN);

fail:
    frame_merge_flush(bsf);
    return ret;
}

static av_cold int frame_merge_init(AVBSFContext *bsf)
{
    PGSMergeContext *ctx = bsf->priv_data;

    ctx->in  = av_packet_alloc();
    ctx->buffer_pkt = av_packet_alloc();
    if (!ctx->in || !ctx->buffer_pkt)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void frame_merge_close(AVBSFContext *bsf)
{
    PGSMergeContext *ctx = bsf->priv_data;

    av_packet_free(&ctx->in);
    av_packet_free(&ctx->buffer_pkt);
}

static const enum AVCodecID frame_merge_codec_ids[] = {
    AV_CODEC_ID_HDMV_PGS_SUBTITLE, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_pgs_frame_merge_bsf = {
    .p.name         = "pgs_frame_merge",
    .p.codec_ids    = frame_merge_codec_ids,
    .priv_data_size = sizeof(PGSMergeContext),
    .init           = frame_merge_init,
    .flush          = frame_merge_flush,
    .close          = frame_merge_close,
    .filter         = frame_merge_filter,
};
