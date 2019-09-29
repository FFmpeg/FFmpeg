/*
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

#include "libavutil/avassert.h"
#include "libavutil/intmath.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "bsf.h"
#include "get_bits.h"
#include "put_bits.h"

#define FRAME_SLOTS 8

typedef struct VP9RawReorderFrame {
    AVPacket    *packet;
    int          needs_output;
    int          needs_display;

    int64_t      pts;
    int64_t      sequence;
    unsigned int slots;

    unsigned int profile;

    unsigned int show_existing_frame;
    unsigned int frame_to_show;

    unsigned int frame_type;
    unsigned int show_frame;
    unsigned int refresh_frame_flags;
} VP9RawReorderFrame;

typedef struct VP9RawReorderContext {
    int64_t sequence;
    VP9RawReorderFrame *slot[FRAME_SLOTS];
    VP9RawReorderFrame *next_frame;
} VP9RawReorderContext;

static void vp9_raw_reorder_frame_free(VP9RawReorderFrame **frame)
{
    if (*frame)
        av_packet_free(&(*frame)->packet);
    av_freep(frame);
}

static void vp9_raw_reorder_clear_slot(VP9RawReorderContext *ctx, int s)
{
    if (ctx->slot[s]) {
        ctx->slot[s]->slots &= ~(1 << s);
        if (ctx->slot[s]->slots == 0)
            vp9_raw_reorder_frame_free(&ctx->slot[s]);
        else
            ctx->slot[s] = NULL;
    }
}

static int vp9_raw_reorder_frame_parse(AVBSFContext *bsf, VP9RawReorderFrame *frame)
{
    GetBitContext bc;
    int err;

    unsigned int frame_marker;
    unsigned int profile_low_bit, profile_high_bit, reserved_zero;
    unsigned int error_resilient_mode;
    unsigned int frame_sync_code;

    err = init_get_bits(&bc, frame->packet->data, 8 * frame->packet->size);
    if (err)
        return err;

    frame_marker = get_bits(&bc, 2);
    if (frame_marker != 2) {
        av_log(bsf, AV_LOG_ERROR, "Invalid frame marker: %u.\n",
               frame_marker);
        return AVERROR_INVALIDDATA;
    }

    profile_low_bit  = get_bits1(&bc);
    profile_high_bit = get_bits1(&bc);
    frame->profile = (profile_high_bit << 1) | profile_low_bit;
    if (frame->profile == 3) {
        reserved_zero = get_bits1(&bc);
        if (reserved_zero != 0) {
            av_log(bsf, AV_LOG_ERROR, "Profile reserved_zero bit set: "
                   "unsupported profile or invalid bitstream.\n");
            return AVERROR_INVALIDDATA;
        }
    }

    frame->show_existing_frame = get_bits1(&bc);
    if (frame->show_existing_frame) {
        frame->frame_to_show = get_bits(&bc, 3);
        return 0;
    }

    frame->frame_type = get_bits1(&bc);
    frame->show_frame = get_bits1(&bc);
    error_resilient_mode = get_bits1(&bc);

    if (frame->frame_type == 0) {
        frame_sync_code = get_bits(&bc, 24);
        if (frame_sync_code != 0x498342) {
            av_log(bsf, AV_LOG_ERROR, "Invalid frame sync code: %06x.\n",
                   frame_sync_code);
            return AVERROR_INVALIDDATA;
        }
        frame->refresh_frame_flags = 0xff;
    } else {
        unsigned int intra_only;

        if (frame->show_frame == 0)
            intra_only = get_bits1(&bc);
        else
            intra_only = 0;
        if (error_resilient_mode == 0) {
            // reset_frame_context
            skip_bits(&bc, 2);
        }
        if (intra_only) {
            frame_sync_code = get_bits(&bc, 24);
            if (frame_sync_code != 0x498342) {
                av_log(bsf, AV_LOG_ERROR, "Invalid frame sync code: "
                       "%06x.\n", frame_sync_code);
                return AVERROR_INVALIDDATA;
            }
            if (frame->profile > 0) {
                unsigned int color_space;
                if (frame->profile >= 2) {
                    // ten_or_twelve_bit
                    skip_bits(&bc, 1);
                }
                color_space = get_bits(&bc, 3);
                if (color_space != 7 /* CS_RGB */) {
                    // color_range
                    skip_bits(&bc, 1);
                    if (frame->profile == 1 || frame->profile == 3) {
                        // subsampling
                        skip_bits(&bc, 3);
                    }
                } else {
                    if (frame->profile == 1 || frame->profile == 3)
                        skip_bits(&bc, 1);
                }
            }
            frame->refresh_frame_flags = get_bits(&bc, 8);
        } else {
            frame->refresh_frame_flags = get_bits(&bc, 8);
        }
    }

    return 0;
}

static int vp9_raw_reorder_make_output(AVBSFContext *bsf,
                                   AVPacket *out,
                                   VP9RawReorderFrame *last_frame)
{
    VP9RawReorderContext *ctx = bsf->priv_data;
    VP9RawReorderFrame *next_output = last_frame,
                      *next_display = last_frame, *frame;
    int s, err;

    for (s = 0; s < FRAME_SLOTS; s++) {
        frame = ctx->slot[s];
        if (!frame)
            continue;
        if (frame->needs_output && (!next_output ||
            frame->sequence < next_output->sequence))
            next_output = frame;
        if (frame->needs_display && (!next_display ||
            frame->pts < next_display->pts))
            next_display = frame;
    }

    if (!next_output && !next_display)
        return AVERROR_EOF;

    if (!next_display || (next_output &&
        next_output->sequence < next_display->sequence))
        frame = next_output;
    else
        frame = next_display;

    if (frame->needs_output && frame->needs_display &&
        next_output == next_display) {
        av_log(bsf, AV_LOG_DEBUG, "Output and display frame "
               "%"PRId64" (%"PRId64") in order.\n",
               frame->sequence, frame->pts);

        av_packet_move_ref(out, frame->packet);

        frame->needs_output = frame->needs_display = 0;
    } else if (frame->needs_output) {
        if (frame->needs_display) {
            av_log(bsf, AV_LOG_DEBUG, "Output frame %"PRId64" "
                   "(%"PRId64") for later display.\n",
                   frame->sequence, frame->pts);
        } else {
            av_log(bsf, AV_LOG_DEBUG, "Output unshown frame "
                   "%"PRId64" (%"PRId64") to keep order.\n",
                   frame->sequence, frame->pts);
        }

        av_packet_move_ref(out, frame->packet);
        out->pts = out->dts;

        frame->needs_output = 0;
    } else {
        PutBitContext pb;

        av_assert0(!frame->needs_output && frame->needs_display);

        if (frame->slots == 0) {
            av_log(bsf, AV_LOG_ERROR, "Attempting to display frame "
                   "which is no longer available?\n");
            frame->needs_display = 0;
            return AVERROR_INVALIDDATA;
        }

        s = ff_ctz(frame->slots);
        av_assert0(s < FRAME_SLOTS);

        av_log(bsf, AV_LOG_DEBUG, "Display frame %"PRId64" "
               "(%"PRId64") from slot %d.\n",
               frame->sequence, frame->pts, s);

        err = av_new_packet(out, 2);
        if (err < 0)
            return err;

        init_put_bits(&pb, out->data, 2);

        // frame_marker
        put_bits(&pb, 2, 2);
        // profile_low_bit
        put_bits(&pb, 1, frame->profile & 1);
        // profile_high_bit
        put_bits(&pb, 1, (frame->profile >> 1) & 1);
        if (frame->profile == 3) {
            // reserved_zero
            put_bits(&pb, 1, 0);
        }
        // show_existing_frame
        put_bits(&pb, 1, 1);
        // frame_to_show_map_idx
        put_bits(&pb, 3, s);

        while (put_bits_count(&pb) < 16)
            put_bits(&pb, 1, 0);

        flush_put_bits(&pb);
        out->pts = out->dts = frame->pts;

        frame->needs_display = 0;
    }

    return 0;
}

static int vp9_raw_reorder_filter(AVBSFContext *bsf, AVPacket *out)
{
    VP9RawReorderContext *ctx = bsf->priv_data;
    VP9RawReorderFrame *frame;
    AVPacket *in;
    int err, s;

    if (ctx->next_frame) {
        frame = ctx->next_frame;

    } else {
        err = ff_bsf_get_packet(bsf, &in);
        if (err < 0) {
            if (err == AVERROR_EOF)
                return vp9_raw_reorder_make_output(bsf, out, NULL);
            return err;
        }

        if ((in->data[in->size - 1] & 0xe0) == 0xc0) {
            av_log(bsf, AV_LOG_ERROR, "Input in superframes is not "
                   "supported.\n");
            av_packet_free(&in);
            return AVERROR(ENOSYS);
        }

        frame = av_mallocz(sizeof(*frame));
        if (!frame) {
            av_packet_free(&in);
            return AVERROR(ENOMEM);
        }

        frame->packet   = in;
        frame->pts      = in->pts;
        frame->sequence = ++ctx->sequence;
        err = vp9_raw_reorder_frame_parse(bsf, frame);
        if (err) {
            av_log(bsf, AV_LOG_ERROR, "Failed to parse input "
                   "frame: %d.\n", err);
            goto fail;
        }

        frame->needs_output  = 1;
        frame->needs_display = frame->pts != AV_NOPTS_VALUE;

        if (frame->show_existing_frame)
            av_log(bsf, AV_LOG_DEBUG, "Show frame %"PRId64" "
                   "(%"PRId64"): show %u.\n", frame->sequence,
                   frame->pts, frame->frame_to_show);
        else
            av_log(bsf, AV_LOG_DEBUG, "New frame %"PRId64" "
                   "(%"PRId64"): type %u show %u refresh %02x.\n",
                   frame->sequence, frame->pts, frame->frame_type,
                   frame->show_frame, frame->refresh_frame_flags);

        ctx->next_frame = frame;
    }

    for (s = 0; s < FRAME_SLOTS; s++) {
        if (!(frame->refresh_frame_flags & (1 << s)))
            continue;
        if (ctx->slot[s] && ctx->slot[s]->needs_display &&
            ctx->slot[s]->slots == (1 << s)) {
            // We are overwriting this slot, which is last reference
            // to the frame previously present in it.  In order to be
            // a valid stream, that frame must already have been
            // displayed before the pts of the current frame.
            err = vp9_raw_reorder_make_output(bsf, out, ctx->slot[s]);
            if (err < 0) {
                av_log(bsf, AV_LOG_ERROR, "Failed to create "
                       "output overwriting slot %d: %d.\n",
                       s, err);
                // Clear the slot anyway, so we don't end up
                // in an infinite loop.
                vp9_raw_reorder_clear_slot(ctx, s);
                return AVERROR_INVALIDDATA;
            }
            return 0;
        }
        vp9_raw_reorder_clear_slot(ctx, s);
    }

    for (s = 0; s < FRAME_SLOTS; s++) {
        if (!(frame->refresh_frame_flags & (1 << s)))
            continue;
        ctx->slot[s] = frame;
    }
    frame->slots = frame->refresh_frame_flags;

    if (!frame->refresh_frame_flags) {
        err = vp9_raw_reorder_make_output(bsf, out, frame);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Failed to create output "
                   "for transient frame.\n");
            ctx->next_frame = NULL;
            return AVERROR_INVALIDDATA;
        }
        if (!frame->needs_display) {
            vp9_raw_reorder_frame_free(&frame);
            ctx->next_frame = NULL;
        }
        return 0;
    }

    ctx->next_frame = NULL;
    return AVERROR(EAGAIN);

fail:
    vp9_raw_reorder_frame_free(&frame);
    return err;
}

static void vp9_raw_reorder_flush(AVBSFContext *bsf)
{
    VP9RawReorderContext *ctx = bsf->priv_data;

    for (int s = 0; s < FRAME_SLOTS; s++)
        vp9_raw_reorder_clear_slot(ctx, s);
    ctx->next_frame = NULL;
    ctx->sequence = 0;
}

static void vp9_raw_reorder_close(AVBSFContext *bsf)
{
    VP9RawReorderContext *ctx = bsf->priv_data;
    int s;

    for (s = 0; s < FRAME_SLOTS; s++)
        vp9_raw_reorder_clear_slot(ctx, s);
}

static const enum AVCodecID vp9_raw_reorder_codec_ids[] = {
    AV_CODEC_ID_VP9, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_vp9_raw_reorder_bsf = {
    .name           = "vp9_raw_reorder",
    .priv_data_size = sizeof(VP9RawReorderContext),
    .close          = &vp9_raw_reorder_close,
    .flush          = &vp9_raw_reorder_flush,
    .filter         = &vp9_raw_reorder_filter,
    .codec_ids      = vp9_raw_reorder_codec_ids,
};
