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
#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"

#include "encode.h"
#include "avcodec.h"
#include "hw_base_encode.h"

static int base_encode_pic_free(FFHWBaseEncodePicture *pic)
{
    av_frame_free(&pic->input_image);
    av_frame_free(&pic->recon_image);

    av_buffer_unref(&pic->opaque_ref);
    av_freep(&pic->codec_priv);
    av_freep(&pic->priv);
    av_free(pic);

    return 0;
}

static void hw_base_encode_add_ref(FFHWBaseEncodePicture *pic,
                                   FFHWBaseEncodePicture *target,
                                   int is_ref, int in_dpb, int prev)
{
    int refs = 0;

    if (is_ref) {
        av_assert0(pic != target);
        av_assert0(pic->nb_refs[0] < MAX_PICTURE_REFERENCES &&
                   pic->nb_refs[1] < MAX_PICTURE_REFERENCES);
        if (target->display_order < pic->display_order)
            pic->refs[0][pic->nb_refs[0]++] = target;
        else
            pic->refs[1][pic->nb_refs[1]++] = target;
        ++refs;
    }

    if (in_dpb) {
        av_assert0(pic->nb_dpb_pics < MAX_DPB_SIZE);
        pic->dpb[pic->nb_dpb_pics++] = target;
        ++refs;
    }

    if (prev) {
        av_assert0(!pic->prev);
        pic->prev = target;
        ++refs;
    }

    target->ref_count[0] += refs;
    target->ref_count[1] += refs;
}

static void hw_base_encode_remove_refs(FFHWBaseEncodePicture *pic, int level)
{
    int i;

    if (pic->ref_removed[level])
        return;

    for (i = 0; i < pic->nb_refs[0]; i++) {
        av_assert0(pic->refs[0][i]);
        --pic->refs[0][i]->ref_count[level];
        av_assert0(pic->refs[0][i]->ref_count[level] >= 0);
    }

    for (i = 0; i < pic->nb_refs[1]; i++) {
        av_assert0(pic->refs[1][i]);
        --pic->refs[1][i]->ref_count[level];
        av_assert0(pic->refs[1][i]->ref_count[level] >= 0);
    }

    for (i = 0; i < pic->nb_dpb_pics; i++) {
        av_assert0(pic->dpb[i]);
        --pic->dpb[i]->ref_count[level];
        av_assert0(pic->dpb[i]->ref_count[level] >= 0);
    }

    av_assert0(pic->prev || pic->type == FF_HW_PICTURE_TYPE_IDR);
    if (pic->prev) {
        --pic->prev->ref_count[level];
        av_assert0(pic->prev->ref_count[level] >= 0);
    }

    pic->ref_removed[level] = 1;
}

static void hw_base_encode_set_b_pictures(FFHWBaseEncodeContext *ctx,
                                          FFHWBaseEncodePicture *start,
                                          FFHWBaseEncodePicture *end,
                                          FFHWBaseEncodePicture *prev,
                                          int current_depth,
                                          FFHWBaseEncodePicture **last)
{
    FFHWBaseEncodePicture *pic, *next, *ref;
    int i, len;

    av_assert0(start && end && start != end && start->next != end);

    // If we are at the maximum depth then encode all pictures as
    // non-referenced B-pictures.  Also do this if there is exactly one
    // picture left, since there will be nothing to reference it.
    if (current_depth == ctx->max_b_depth || start->next->next == end) {
        for (pic = start->next; pic; pic = pic->next) {
            if (pic == end)
                break;
            pic->type    = FF_HW_PICTURE_TYPE_B;
            pic->b_depth = current_depth;

            hw_base_encode_add_ref(pic, start, 1, 1, 0);
            hw_base_encode_add_ref(pic, end,   1, 1, 0);
            hw_base_encode_add_ref(pic, prev,  0, 0, 1);

            for (ref = end->refs[1][0]; ref; ref = ref->refs[1][0])
                hw_base_encode_add_ref(pic, ref, 0, 1, 0);
        }
        *last = prev;

    } else {
        // Split the current list at the midpoint with a referenced
        // B-picture, then descend into each side separately.
        len = 0;
        for (pic = start->next; pic != end; pic = pic->next)
            ++len;
        for (pic = start->next, i = 1; 2 * i < len; pic = pic->next, i++);

        pic->type    = FF_HW_PICTURE_TYPE_B;
        pic->b_depth = current_depth;

        pic->is_reference = 1;

        hw_base_encode_add_ref(pic, pic,   0, 1, 0);
        hw_base_encode_add_ref(pic, start, 1, 1, 0);
        hw_base_encode_add_ref(pic, end,   1, 1, 0);
        hw_base_encode_add_ref(pic, prev,  0, 0, 1);

        for (ref = end->refs[1][0]; ref; ref = ref->refs[1][0])
            hw_base_encode_add_ref(pic, ref, 0, 1, 0);

        if (i > 1)
            hw_base_encode_set_b_pictures(ctx, start, pic, pic,
                                          current_depth + 1, &next);
        else
            next = pic;

        hw_base_encode_set_b_pictures(ctx, pic, end, next,
                                      current_depth + 1, last);
    }
}

static void hw_base_encode_add_next_prev(FFHWBaseEncodeContext *ctx,
                                         FFHWBaseEncodePicture *pic)
{
    int i;

    if (!pic)
        return;

    if (pic->type == FF_HW_PICTURE_TYPE_IDR) {
        for (i = 0; i < ctx->nb_next_prev; i++) {
            --ctx->next_prev[i]->ref_count[0];
            ctx->next_prev[i] = NULL;
        }
        ctx->next_prev[0] = pic;
        ++pic->ref_count[0];
        ctx->nb_next_prev = 1;

        return;
    }

    if (ctx->nb_next_prev < ctx->ref_l0) {
        ctx->next_prev[ctx->nb_next_prev++] = pic;
        ++pic->ref_count[0];
    } else {
        --ctx->next_prev[0]->ref_count[0];
        for (i = 0; i < ctx->ref_l0 - 1; i++)
            ctx->next_prev[i] = ctx->next_prev[i + 1];
        ctx->next_prev[i] = pic;
        ++pic->ref_count[0];
    }
}

static int hw_base_encode_pick_next(AVCodecContext *avctx,
                                    FFHWBaseEncodeContext *ctx,
                                    FFHWBaseEncodePicture **pic_out)
{
    FFHWBaseEncodePicture *pic = NULL, *prev = NULL, *next, *start;
    int i, b_counter, closed_gop_end;

    // If there are any B-frames already queued, the next one to encode
    // is the earliest not-yet-issued frame for which all references are
    // available.
    for (pic = ctx->pic_start; pic; pic = pic->next) {
        if (pic->encode_issued)
            continue;
        if (pic->type != FF_HW_PICTURE_TYPE_B)
            continue;
        for (i = 0; i < pic->nb_refs[0]; i++) {
            if (!pic->refs[0][i]->encode_issued)
                break;
        }
        if (i != pic->nb_refs[0])
            continue;

        for (i = 0; i < pic->nb_refs[1]; i++) {
            if (!pic->refs[1][i]->encode_issued)
                break;
        }
        if (i == pic->nb_refs[1])
            break;
    }

    if (pic) {
        av_log(avctx, AV_LOG_DEBUG, "Pick B-picture at depth %d to "
               "encode next.\n", pic->b_depth);
        *pic_out = pic;
        return 0;
    }

    // Find the B-per-Pth available picture to become the next picture
    // on the top layer.
    start = NULL;
    b_counter = 0;
    closed_gop_end = ctx->closed_gop ||
                     ctx->idr_counter == ctx->gop_per_idr;
    for (pic = ctx->pic_start; pic; pic = next) {
        next = pic->next;
        if (pic->encode_issued) {
            start = pic;
            continue;
        }
        // If the next available picture is force-IDR, encode it to start
        // a new GOP immediately.
        if (pic->force_idr)
            break;
        if (b_counter == ctx->b_per_p)
            break;
        // If this picture ends a closed GOP or starts a new GOP then it
        // needs to be in the top layer.
        if (ctx->gop_counter + b_counter + closed_gop_end >= ctx->gop_size)
            break;
        // If the picture after this one is force-IDR, we need to encode
        // this one in the top layer.
        if (next && next->force_idr)
            break;
        ++b_counter;
    }

    // At the end of the stream the last picture must be in the top layer.
    if (!pic && ctx->end_of_stream) {
        --b_counter;
        pic = ctx->pic_end;
        if (pic->encode_complete)
            return AVERROR_EOF;
        else if (pic->encode_issued)
            return AVERROR(EAGAIN);
    }

    if (!pic) {
        av_log(avctx, AV_LOG_DEBUG, "Pick nothing to encode next - "
               "need more input for reference pictures.\n");
        return AVERROR(EAGAIN);
    }
    if (ctx->input_order <= ctx->decode_delay && !ctx->end_of_stream) {
        av_log(avctx, AV_LOG_DEBUG, "Pick nothing to encode next - "
               "need more input for timestamps.\n");
        return AVERROR(EAGAIN);
    }

    if (pic->force_idr) {
        av_log(avctx, AV_LOG_DEBUG, "Pick forced IDR-picture to "
               "encode next.\n");
        pic->type = FF_HW_PICTURE_TYPE_IDR;
        ctx->idr_counter = 1;
        ctx->gop_counter = 1;

    } else if (ctx->gop_counter + b_counter >= ctx->gop_size) {
        if (ctx->idr_counter == ctx->gop_per_idr) {
            av_log(avctx, AV_LOG_DEBUG, "Pick new-GOP IDR-picture to "
                   "encode next.\n");
            pic->type = FF_HW_PICTURE_TYPE_IDR;
            ctx->idr_counter = 1;
        } else {
            av_log(avctx, AV_LOG_DEBUG, "Pick new-GOP I-picture to "
                   "encode next.\n");
            pic->type = FF_HW_PICTURE_TYPE_I;
            ++ctx->idr_counter;
        }
        ctx->gop_counter = 1;

    } else {
        if (ctx->gop_counter + b_counter + closed_gop_end == ctx->gop_size) {
            av_log(avctx, AV_LOG_DEBUG, "Pick group-end P-picture to "
                   "encode next.\n");
        } else {
            av_log(avctx, AV_LOG_DEBUG, "Pick normal P-picture to "
                   "encode next.\n");
        }
        pic->type = FF_HW_PICTURE_TYPE_P;
        av_assert0(start);
        ctx->gop_counter += 1 + b_counter;
    }
    pic->is_reference = 1;
    *pic_out = pic;

    hw_base_encode_add_ref(pic, pic, 0, 1, 0);
    if (pic->type != FF_HW_PICTURE_TYPE_IDR) {
        // TODO: apply both previous and forward multi reference for all vaapi encoders.
        // And L0/L1 reference frame number can be set dynamically through query
        // VAConfigAttribEncMaxRefFrames attribute.
        if (avctx->codec_id == AV_CODEC_ID_AV1) {
            for (i = 0; i < ctx->nb_next_prev; i++)
                hw_base_encode_add_ref(pic, ctx->next_prev[i],
                                       pic->type == FF_HW_PICTURE_TYPE_P,
                                       b_counter > 0, 0);
        } else
            hw_base_encode_add_ref(pic, start,
                                   pic->type == FF_HW_PICTURE_TYPE_P,
                                   b_counter > 0, 0);

        hw_base_encode_add_ref(pic, ctx->next_prev[ctx->nb_next_prev - 1], 0, 0, 1);
    }

    if (b_counter > 0) {
        hw_base_encode_set_b_pictures(ctx, start, pic, pic, 1,
                                      &prev);
    } else {
        prev = pic;
    }
    hw_base_encode_add_next_prev(ctx, prev);

    return 0;
}

static int hw_base_encode_clear_old(AVCodecContext *avctx, FFHWBaseEncodeContext *ctx)
{
    FFHWBaseEncodePicture *pic, *prev, *next;

    av_assert0(ctx->pic_start);

    // Remove direct references once each picture is complete.
    for (pic = ctx->pic_start; pic; pic = pic->next) {
        if (pic->encode_complete && pic->next)
            hw_base_encode_remove_refs(pic, 0);
    }

    // Remove indirect references once a picture has no direct references.
    for (pic = ctx->pic_start; pic; pic = pic->next) {
        if (pic->encode_complete && pic->ref_count[0] == 0)
            hw_base_encode_remove_refs(pic, 1);
    }

    // Clear out all complete pictures with no remaining references.
    prev = NULL;
    for (pic = ctx->pic_start; pic; pic = next) {
        next = pic->next;
        if (pic->encode_complete && pic->ref_count[1] == 0) {
            av_assert0(pic->ref_removed[0] && pic->ref_removed[1]);
            if (prev)
                prev->next = next;
            else
                ctx->pic_start = next;
            ctx->op->free(avctx, pic);
            base_encode_pic_free(pic);
        } else {
            prev = pic;
        }
    }

    return 0;
}

static int hw_base_encode_check_frame(FFHWBaseEncodeContext *ctx,
                                      const AVFrame *frame)
{
    if ((frame->crop_top  || frame->crop_bottom ||
         frame->crop_left || frame->crop_right) && !ctx->crop_warned) {
        av_log(ctx->log_ctx, AV_LOG_WARNING, "Cropping information on input "
               "frames ignored due to lack of API support.\n");
        ctx->crop_warned = 1;
    }

    if (!ctx->roi_allowed) {
        AVFrameSideData *sd =
            av_frame_get_side_data(frame, AV_FRAME_DATA_REGIONS_OF_INTEREST);

        if (sd && !ctx->roi_warned) {
            av_log(ctx->log_ctx, AV_LOG_WARNING, "ROI side data on input "
                   "frames ignored due to lack of driver support.\n");
            ctx->roi_warned = 1;
        }
    }

    return 0;
}

static int hw_base_encode_send_frame(AVCodecContext *avctx, FFHWBaseEncodeContext *ctx,
                                     AVFrame *frame)
{
    FFHWBaseEncodePicture *pic;
    int err;

    if (frame) {
        av_log(avctx, AV_LOG_DEBUG, "Input frame: %ux%u (%"PRId64").\n",
               frame->width, frame->height, frame->pts);

        err = hw_base_encode_check_frame(ctx, frame);
        if (err < 0)
            return err;

        pic = av_mallocz(sizeof(*pic));
        if (!pic)
            return AVERROR(ENOMEM);

        pic->input_image = av_frame_alloc();
        if (!pic->input_image) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        if (ctx->recon_frames_ref) {
            pic->recon_image = av_frame_alloc();
            if (!pic->recon_image) {
                err = AVERROR(ENOMEM);
                goto fail;
            }

            err = av_hwframe_get_buffer(ctx->recon_frames_ref, pic->recon_image, 0);
            if (err < 0) {
                err = AVERROR(ENOMEM);
                goto fail;
            }
        }

        pic->priv = av_mallocz(ctx->op->priv_size);
        if (!pic->priv) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        if (ctx->input_order == 0 || frame->pict_type == AV_PICTURE_TYPE_I)
            pic->force_idr = 1;

        pic->pts = frame->pts;
        pic->duration = frame->duration;

        if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
            err = av_buffer_replace(&pic->opaque_ref, frame->opaque_ref);
            if (err < 0)
                goto fail;

            pic->opaque = frame->opaque;
        }

        av_frame_move_ref(pic->input_image, frame);

        if (ctx->input_order == 0)
            ctx->first_pts = pic->pts;
        if (ctx->input_order == ctx->decode_delay)
            ctx->dts_pts_diff = pic->pts - ctx->first_pts;
        if (ctx->output_delay > 0)
            ctx->ts_ring[ctx->input_order %
                        (3 * ctx->output_delay + ctx->async_depth)] = pic->pts;

        pic->display_order = ctx->input_order;
        ++ctx->input_order;

        if (ctx->pic_start) {
            ctx->pic_end->next = pic;
            ctx->pic_end       = pic;
        } else {
            ctx->pic_start     = pic;
            ctx->pic_end       = pic;
        }

        err = ctx->op->init(avctx, pic);
        if (err < 0)
            goto fail;
    } else {
        ctx->end_of_stream = 1;

        // Fix timestamps if we hit end-of-stream before the initial decode
        // delay has elapsed.
        if (ctx->input_order <= ctx->decode_delay)
            ctx->dts_pts_diff = ctx->pic_end->pts - ctx->first_pts;
    }

    return 0;

fail:
    ctx->op->free(avctx, pic);
    base_encode_pic_free(pic);
    return err;
}

int ff_hw_base_encode_set_output_property(FFHWBaseEncodeContext *ctx,
                                          AVCodecContext *avctx,
                                          FFHWBaseEncodePicture *pic,
                                          AVPacket *pkt, int flag_no_delay)
{
    if (pic->type == FF_HW_PICTURE_TYPE_IDR)
        pkt->flags |= AV_PKT_FLAG_KEY;

    pkt->pts = pic->pts;
    pkt->duration = pic->duration;

    // for no-delay encoders this is handled in generic codec
    if (avctx->codec->capabilities & AV_CODEC_CAP_DELAY &&
        avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
        pkt->opaque          = pic->opaque;
        pkt->opaque_ref      = pic->opaque_ref;
        pic->opaque_ref = NULL;
    }

    if (flag_no_delay) {
        pkt->dts = pkt->pts;
        return 0;
    }

    if (ctx->output_delay == 0) {
        pkt->dts = pkt->pts;
    } else if (pic->encode_order < ctx->decode_delay) {
        if (ctx->ts_ring[pic->encode_order] < INT64_MIN + ctx->dts_pts_diff)
            pkt->dts = INT64_MIN;
        else
            pkt->dts = ctx->ts_ring[pic->encode_order] - ctx->dts_pts_diff;
    } else {
        pkt->dts = ctx->ts_ring[(pic->encode_order - ctx->decode_delay) %
                                (3 * ctx->output_delay + ctx->async_depth)];
    }

    return 0;
}

int ff_hw_base_encode_receive_packet(FFHWBaseEncodeContext *ctx,
                                     AVCodecContext *avctx, AVPacket *pkt)
{
    FFHWBaseEncodePicture *pic = NULL;
    AVFrame *frame = ctx->frame;
    int err;

    av_assert0(ctx->op && ctx->op->init && ctx->op->issue &&
               ctx->op->output && ctx->op->free);

start:
    /** if no B frame before repeat P frame, sent repeat P frame out. */
    if (ctx->tail_pkt->size) {
        for (FFHWBaseEncodePicture *tmp = ctx->pic_start; tmp; tmp = tmp->next) {
            if (tmp->type == FF_HW_PICTURE_TYPE_B && tmp->pts < ctx->tail_pkt->pts)
                break;
            else if (!tmp->next) {
                av_packet_move_ref(pkt, ctx->tail_pkt);
                goto end;
            }
        }
    }

    err = ff_encode_get_frame(avctx, frame);
    if (err == AVERROR_EOF) {
        frame = NULL;
    } else if (err < 0)
        return err;

    err = hw_base_encode_send_frame(avctx, ctx, frame);
    if (err < 0)
        return err;

    if (!ctx->pic_start) {
        if (ctx->end_of_stream)
            return AVERROR_EOF;
        else
            return AVERROR(EAGAIN);
    }

    if (ctx->async_encode) {
        if (av_fifo_can_write(ctx->encode_fifo)) {
            err = hw_base_encode_pick_next(avctx, ctx, &pic);
            if (!err) {
                av_assert0(pic);
                pic->encode_order = ctx->encode_order +
                    av_fifo_can_read(ctx->encode_fifo);
                err = ctx->op->issue(avctx, pic);
                if (err < 0) {
                    av_log(avctx, AV_LOG_ERROR, "Encode failed: %s.\n", av_err2str(err));
                    return err;
                }
                pic->encode_issued = 1;
                av_fifo_write(ctx->encode_fifo, &pic, 1);
            }
        }

        if (!av_fifo_can_read(ctx->encode_fifo))
            return err;

        // More frames can be buffered
        if (av_fifo_can_write(ctx->encode_fifo) && !ctx->end_of_stream)
            return AVERROR(EAGAIN);

        av_fifo_read(ctx->encode_fifo, &pic, 1);
        ctx->encode_order = pic->encode_order + 1;
    } else {
        err = hw_base_encode_pick_next(avctx, ctx, &pic);
        if (err < 0)
            return err;
        av_assert0(pic);

        pic->encode_order = ctx->encode_order++;

        err = ctx->op->issue(avctx, pic);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Encode failed: %s.\n", av_err2str(err));
            return err;
        }

        pic->encode_issued = 1;
    }

    err = ctx->op->output(avctx, pic, pkt);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Output failed: %d.\n", err);
        return err;
    }

    ctx->output_order = pic->encode_order;
    hw_base_encode_clear_old(avctx, ctx);

    /** loop to get an available pkt in encoder flushing. */
    if (ctx->end_of_stream && !pkt->size)
        goto start;

end:
    if (pkt->size)
        av_log(avctx, AV_LOG_DEBUG, "Output packet: pts %"PRId64", dts %"PRId64", "
               "size %d bytes.\n", pkt->pts, pkt->dts, pkt->size);

    return 0;
}

int ff_hw_base_init_gop_structure(FFHWBaseEncodeContext *ctx, AVCodecContext *avctx,
                                  uint32_t ref_l0, uint32_t ref_l1,
                                  int flags, int prediction_pre_only)
{
    ctx->ref_l0 = FFMIN(ref_l0, MAX_PICTURE_REFERENCES);
    ctx->ref_l1 = FFMIN(ref_l1, MAX_PICTURE_REFERENCES);

    if (flags & FF_HW_FLAG_INTRA_ONLY || avctx->gop_size <= 1) {
        av_log(avctx, AV_LOG_VERBOSE, "Using intra frames only.\n");
        ctx->gop_size = 1;
    } else if (ref_l0 < 1) {
        av_log(avctx, AV_LOG_ERROR, "Driver does not support any "
               "reference frames.\n");
        return AVERROR(EINVAL);
    } else if (!(flags & FF_HW_FLAG_B_PICTURES) || ref_l1 < 1 ||
               avctx->max_b_frames < 1 || prediction_pre_only) {
        if (ctx->p_to_gpb)
           av_log(avctx, AV_LOG_VERBOSE, "Using intra and B-frames "
                  "(supported references: %d / %d).\n",
                  ref_l0, ref_l1);
        else
            av_log(avctx, AV_LOG_VERBOSE, "Using intra and P-frames "
                   "(supported references: %d / %d).\n", ref_l0, ref_l1);
        ctx->gop_size = avctx->gop_size;
        ctx->p_per_i  = INT_MAX;
        ctx->b_per_p  = 0;
    } else {
       if (ctx->p_to_gpb)
           av_log(avctx, AV_LOG_VERBOSE, "Using intra and B-frames "
                  "(supported references: %d / %d).\n",
                  ref_l0, ref_l1);
       else
           av_log(avctx, AV_LOG_VERBOSE, "Using intra, P- and B-frames "
                  "(supported references: %d / %d).\n", ref_l0, ref_l1);
        ctx->gop_size = avctx->gop_size;
        ctx->p_per_i  = INT_MAX;
        ctx->b_per_p  = avctx->max_b_frames;
        if (flags & FF_HW_FLAG_B_PICTURE_REFERENCES) {
            ctx->max_b_depth = FFMIN(ctx->desired_b_depth,
                                     av_log2(ctx->b_per_p) + 1);
        } else {
            ctx->max_b_depth = 1;
        }
    }

    if (flags & FF_HW_FLAG_NON_IDR_KEY_PICTURES) {
        ctx->closed_gop  = !!(avctx->flags & AV_CODEC_FLAG_CLOSED_GOP);
        ctx->gop_per_idr = ctx->idr_interval + 1;
    } else {
        ctx->closed_gop  = 1;
        ctx->gop_per_idr = 1;
    }

    return 0;
}

int ff_hw_base_get_recon_format(FFHWBaseEncodeContext *ctx, const void *hwconfig,
                                enum AVPixelFormat *fmt)
{
    AVHWFramesConstraints *constraints = NULL;
    enum AVPixelFormat recon_format;
    int err, i;

    constraints = av_hwdevice_get_hwframe_constraints(ctx->device_ref,
                                                      hwconfig);
    if (!constraints) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    // Probably we can use the input surface format as the surface format
    // of the reconstructed frames.  If not, we just pick the first (only?)
    // format in the valid list and hope that it all works.
    recon_format = AV_PIX_FMT_NONE;
    if (constraints->valid_sw_formats) {
        for (i = 0; constraints->valid_sw_formats[i] != AV_PIX_FMT_NONE; i++) {
            if (ctx->input_frames->sw_format ==
                constraints->valid_sw_formats[i]) {
                recon_format = ctx->input_frames->sw_format;
                break;
            }
        }
        if (recon_format == AV_PIX_FMT_NONE) {
            // No match.  Just use the first in the supported list and
            // hope for the best.
            recon_format = constraints->valid_sw_formats[0];
        }
    } else {
        // No idea what to use; copy input format.
        recon_format = ctx->input_frames->sw_format;
    }
    av_log(ctx->log_ctx, AV_LOG_DEBUG, "Using %s as format of "
           "reconstructed frames.\n", av_get_pix_fmt_name(recon_format));

    if (ctx->surface_width  < constraints->min_width  ||
        ctx->surface_height < constraints->min_height ||
        ctx->surface_width  > constraints->max_width ||
        ctx->surface_height > constraints->max_height) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Hardware does not support encoding at "
               "size %dx%d (constraints: width %d-%d height %d-%d).\n",
               ctx->surface_width, ctx->surface_height,
               constraints->min_width,  constraints->max_width,
               constraints->min_height, constraints->max_height);
        err = AVERROR(EINVAL);
        goto fail;
    }

    *fmt = recon_format;
    err = 0;
fail:
    av_hwframe_constraints_free(&constraints);
    return err;
}

int ff_hw_base_encode_init(AVCodecContext *avctx, FFHWBaseEncodeContext *ctx)
{
    ctx->log_ctx = (void *)avctx;

    ctx->frame = av_frame_alloc();
    if (!ctx->frame)
        return AVERROR(ENOMEM);

    if (!avctx->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "A hardware frames reference is "
               "required to associate the encoding device.\n");
        return AVERROR(EINVAL);
    }

    ctx->input_frames_ref = av_buffer_ref(avctx->hw_frames_ctx);
    if (!ctx->input_frames_ref)
        return AVERROR(ENOMEM);

    ctx->input_frames = (AVHWFramesContext *)ctx->input_frames_ref->data;

    ctx->device_ref = av_buffer_ref(ctx->input_frames->device_ref);
    if (!ctx->device_ref)
        return AVERROR(ENOMEM);

    ctx->device = (AVHWDeviceContext *)ctx->device_ref->data;

    ctx->tail_pkt = av_packet_alloc();
    if (!ctx->tail_pkt)
        return AVERROR(ENOMEM);

    return 0;
}

int ff_hw_base_encode_close(FFHWBaseEncodeContext *ctx)
{
    for (FFHWBaseEncodePicture *pic = ctx->pic_start, *next_pic = pic; pic; pic = next_pic) {
        next_pic = pic->next;
        base_encode_pic_free(pic);
    }

    av_fifo_freep2(&ctx->encode_fifo);

    av_frame_free(&ctx->frame);
    av_packet_free(&ctx->tail_pkt);

    av_buffer_unref(&ctx->device_ref);
    av_buffer_unref(&ctx->input_frames_ref);
    av_buffer_unref(&ctx->recon_frames_ref);

    return 0;
}
