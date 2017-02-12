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

#include <inttypes.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/log.h"
#include "libavutil/pixdesc.h"

#include "vaapi_encode.h"
#include "avcodec.h"

static const char *picture_type_name[] = { "IDR", "I", "P", "B" };

static int vaapi_encode_make_packed_header(AVCodecContext *avctx,
                                           VAAPIEncodePicture *pic,
                                           int type, char *data, size_t bit_len)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAStatus vas;
    VABufferID param_buffer, data_buffer;
    VAEncPackedHeaderParameterBuffer params = {
        .type = type,
        .bit_length = bit_len,
        .has_emulation_bytes = 1,
    };

    av_assert0(pic->nb_param_buffers + 2 <= MAX_PARAM_BUFFERS);

    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         VAEncPackedHeaderParameterBufferType,
                         sizeof(params), 1, &params, &param_buffer);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create parameter buffer "
               "for packed header (type %d): %d (%s).\n",
               type, vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }
    pic->param_buffers[pic->nb_param_buffers++] = param_buffer;

    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         VAEncPackedHeaderDataBufferType,
                         (bit_len + 7) / 8, 1, data, &data_buffer);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create data buffer "
               "for packed header (type %d): %d (%s).\n",
               type, vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }
    pic->param_buffers[pic->nb_param_buffers++] = data_buffer;

    av_log(avctx, AV_LOG_DEBUG, "Packed header buffer (%d) is %#x/%#x "
           "(%zu bits).\n", type, param_buffer, data_buffer, bit_len);
    return 0;
}

static int vaapi_encode_make_param_buffer(AVCodecContext *avctx,
                                          VAAPIEncodePicture *pic,
                                          int type, char *data, size_t len)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAStatus vas;
    VABufferID buffer;

    av_assert0(pic->nb_param_buffers + 1 <= MAX_PARAM_BUFFERS);

    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         type, len, 1, data, &buffer);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create parameter buffer "
               "(type %d): %d (%s).\n", type, vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }
    pic->param_buffers[pic->nb_param_buffers++] = buffer;

    av_log(avctx, AV_LOG_DEBUG, "Param buffer (%d) is %#x.\n",
           type, buffer);
    return 0;
}

static int vaapi_encode_wait(AVCodecContext *avctx,
                             VAAPIEncodePicture *pic)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAStatus vas;

    av_assert0(pic->encode_issued);

    if (pic->encode_complete) {
        // Already waited for this picture.
        return 0;
    }

    av_log(avctx, AV_LOG_DEBUG, "Sync to pic %"PRId64"/%"PRId64" "
           "(input surface %#x).\n", pic->display_order,
           pic->encode_order, pic->input_surface);

    vas = vaSyncSurface(ctx->hwctx->display, pic->input_surface);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to sync to picture completion: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    // Input is definitely finished with now.
    av_frame_free(&pic->input_image);

    pic->encode_complete = 1;
    return 0;
}

static int vaapi_encode_issue(AVCodecContext *avctx,
                              VAAPIEncodePicture *pic)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodeSlice *slice;
    VAStatus vas;
    int err, i;
    char data[MAX_PARAM_BUFFER_SIZE];
    size_t bit_len;

    av_log(avctx, AV_LOG_DEBUG, "Issuing encode for pic %"PRId64"/%"PRId64" "
           "as type %s.\n", pic->display_order, pic->encode_order,
           picture_type_name[pic->type]);
    if (pic->nb_refs == 0) {
        av_log(avctx, AV_LOG_DEBUG, "No reference pictures.\n");
    } else {
        av_log(avctx, AV_LOG_DEBUG, "Refers to:");
        for (i = 0; i < pic->nb_refs; i++) {
            av_log(avctx, AV_LOG_DEBUG, " %"PRId64"/%"PRId64,
                   pic->refs[i]->display_order, pic->refs[i]->encode_order);
        }
        av_log(avctx, AV_LOG_DEBUG, ".\n");
    }

    av_assert0(pic->input_available && !pic->encode_issued);
    for (i = 0; i < pic->nb_refs; i++) {
        av_assert0(pic->refs[i]);
        // If we are serialised then the references must have already
        // completed.  If not, they must have been issued but need not
        // have completed yet.
        if (ctx->issue_mode == ISSUE_MODE_SERIALISE_EVERYTHING)
            av_assert0(pic->refs[i]->encode_complete);
        else
            av_assert0(pic->refs[i]->encode_issued);
    }

    av_log(avctx, AV_LOG_DEBUG, "Input surface is %#x.\n", pic->input_surface);

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
    pic->recon_surface = (VASurfaceID)(uintptr_t)pic->recon_image->data[3];
    av_log(avctx, AV_LOG_DEBUG, "Recon surface is %#x.\n", pic->recon_surface);

    pic->output_buffer_ref = av_buffer_pool_get(ctx->output_buffer_pool);
    if (!pic->output_buffer_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    pic->output_buffer = (VABufferID)(uintptr_t)pic->output_buffer_ref->data;
    av_log(avctx, AV_LOG_DEBUG, "Output buffer is %#x.\n",
           pic->output_buffer);

    if (ctx->codec->picture_params_size > 0) {
        pic->codec_picture_params = av_malloc(ctx->codec->picture_params_size);
        if (!pic->codec_picture_params)
            goto fail;
        memcpy(pic->codec_picture_params, ctx->codec_picture_params,
               ctx->codec->picture_params_size);
    } else {
        av_assert0(!ctx->codec_picture_params);
    }

    pic->nb_param_buffers = 0;

    if (pic->encode_order == 0) {
        // Global parameter buffers are set on the first picture only.

        for (i = 0; i < ctx->nb_global_params; i++) {
            err = vaapi_encode_make_param_buffer(avctx, pic,
                                                 VAEncMiscParameterBufferType,
                                                 (char*)ctx->global_params[i],
                                                 ctx->global_params_size[i]);
            if (err < 0)
                goto fail;
        }
    }

    if (pic->type == PICTURE_TYPE_IDR && ctx->codec->init_sequence_params) {
        err = vaapi_encode_make_param_buffer(avctx, pic,
                                             VAEncSequenceParameterBufferType,
                                             ctx->codec_sequence_params,
                                             ctx->codec->sequence_params_size);
        if (err < 0)
            goto fail;
    }

    if (ctx->codec->init_picture_params) {
        err = ctx->codec->init_picture_params(avctx, pic);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to initialise picture "
                   "parameters: %d.\n", err);
            goto fail;
        }
        err = vaapi_encode_make_param_buffer(avctx, pic,
                                             VAEncPictureParameterBufferType,
                                             pic->codec_picture_params,
                                             ctx->codec->picture_params_size);
        if (err < 0)
            goto fail;
    }

    if (pic->type == PICTURE_TYPE_IDR) {
        if (ctx->va_packed_headers & VA_ENC_PACKED_HEADER_SEQUENCE &&
            ctx->codec->write_sequence_header) {
            bit_len = 8 * sizeof(data);
            err = ctx->codec->write_sequence_header(avctx, data, &bit_len);
            if (err < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to write per-sequence "
                       "header: %d.\n", err);
                goto fail;
            }
            err = vaapi_encode_make_packed_header(avctx, pic,
                                                  ctx->codec->sequence_header_type,
                                                  data, bit_len);
            if (err < 0)
                goto fail;
        }
    }

    if (ctx->va_packed_headers & VA_ENC_PACKED_HEADER_PICTURE &&
        ctx->codec->write_picture_header) {
        bit_len = 8 * sizeof(data);
        err = ctx->codec->write_picture_header(avctx, pic, data, &bit_len);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to write per-picture "
                   "header: %d.\n", err);
            goto fail;
        }
        err = vaapi_encode_make_packed_header(avctx, pic,
                                              ctx->codec->picture_header_type,
                                              data, bit_len);
        if (err < 0)
            goto fail;
    }

    if (ctx->codec->write_extra_buffer) {
        for (i = 0;; i++) {
            size_t len = sizeof(data);
            int type;
            err = ctx->codec->write_extra_buffer(avctx, pic, i, &type,
                                                 data, &len);
            if (err == AVERROR_EOF)
                break;
            if (err < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to write extra "
                       "buffer %d: %d.\n", i, err);
                goto fail;
            }

            err = vaapi_encode_make_param_buffer(avctx, pic, type,
                                                 data, len);
            if (err < 0)
                goto fail;
        }
    }

    if (ctx->va_packed_headers & VA_ENC_PACKED_HEADER_MISC &&
        ctx->codec->write_extra_header) {
        for (i = 0;; i++) {
            int type;
            bit_len = 8 * sizeof(data);
            err = ctx->codec->write_extra_header(avctx, pic, i, &type,
                                                 data, &bit_len);
            if (err == AVERROR_EOF)
                break;
            if (err < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to write extra "
                       "header %d: %d.\n", i, err);
                goto fail;
            }

            err = vaapi_encode_make_packed_header(avctx, pic, type,
                                                  data, bit_len);
            if (err < 0)
                goto fail;
        }
    }

    av_assert0(pic->nb_slices <= MAX_PICTURE_SLICES);
    for (i = 0; i < pic->nb_slices; i++) {
        slice = av_mallocz(sizeof(*slice));
        if (!slice) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        slice->index = i;
        pic->slices[i] = slice;

        if (ctx->codec->slice_params_size > 0) {
            slice->codec_slice_params = av_mallocz(ctx->codec->slice_params_size);
            if (!slice->codec_slice_params) {
                err = AVERROR(ENOMEM);
                goto fail;
            }
        }

        if (ctx->codec->init_slice_params) {
            err = ctx->codec->init_slice_params(avctx, pic, slice);
            if (err < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to initialise slice "
                       "parameters: %d.\n", err);
                goto fail;
            }
        }

        if (ctx->va_packed_headers & VA_ENC_PACKED_HEADER_SLICE &&
            ctx->codec->write_slice_header) {
            bit_len = 8 * sizeof(data);
            err = ctx->codec->write_slice_header(avctx, pic, slice,
                                                 data, &bit_len);
            if (err < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to write per-slice "
                       "header: %d.\n", err);
                goto fail;
            }
            err = vaapi_encode_make_packed_header(avctx, pic,
                                                  ctx->codec->slice_header_type,
                                                  data, bit_len);
            if (err < 0)
                goto fail;
        }

        if (ctx->codec->init_slice_params) {
            err = vaapi_encode_make_param_buffer(avctx, pic,
                                                 VAEncSliceParameterBufferType,
                                                 slice->codec_slice_params,
                                                 ctx->codec->slice_params_size);
            if (err < 0)
                goto fail;
        }
    }

    vas = vaBeginPicture(ctx->hwctx->display, ctx->va_context,
                         pic->input_surface);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to begin picture encode issue: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail_with_picture;
    }

    vas = vaRenderPicture(ctx->hwctx->display, ctx->va_context,
                          pic->param_buffers, pic->nb_param_buffers);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to upload encode parameters: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail_with_picture;
    }

    vas = vaEndPicture(ctx->hwctx->display, ctx->va_context);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to end picture encode issue: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        // vaRenderPicture() has been called here, so we should not destroy
        // the parameter buffers unless separate destruction is required.
        if (ctx->hwctx->driver_quirks &
            AV_VAAPI_DRIVER_QUIRK_RENDER_PARAM_BUFFERS)
            goto fail;
        else
            goto fail_at_end;
    }

    if (ctx->hwctx->driver_quirks &
        AV_VAAPI_DRIVER_QUIRK_RENDER_PARAM_BUFFERS) {
        for (i = 0; i < pic->nb_param_buffers; i++) {
            vas = vaDestroyBuffer(ctx->hwctx->display,
                                  pic->param_buffers[i]);
            if (vas != VA_STATUS_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Failed to destroy "
                       "param buffer %#x: %d (%s).\n",
                       pic->param_buffers[i], vas, vaErrorStr(vas));
                // And ignore.
            }
        }
    }

    pic->encode_issued = 1;

    if (ctx->issue_mode == ISSUE_MODE_SERIALISE_EVERYTHING)
        return vaapi_encode_wait(avctx, pic);
    else
        return 0;

fail_with_picture:
    vaEndPicture(ctx->hwctx->display, ctx->va_context);
fail:
    for(i = 0; i < pic->nb_param_buffers; i++)
        vaDestroyBuffer(ctx->hwctx->display, pic->param_buffers[i]);
fail_at_end:
    av_freep(&pic->codec_picture_params);
    av_frame_free(&pic->recon_image);
    return err;
}

static int vaapi_encode_output(AVCodecContext *avctx,
                               VAAPIEncodePicture *pic, AVPacket *pkt)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VACodedBufferSegment *buf_list, *buf;
    VAStatus vas;
    int err;

    err = vaapi_encode_wait(avctx, pic);
    if (err < 0)
        return err;

    buf_list = NULL;
    vas = vaMapBuffer(ctx->hwctx->display, pic->output_buffer,
                      (void**)&buf_list);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to map output buffers: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    for (buf = buf_list; buf; buf = buf->next) {
        av_log(avctx, AV_LOG_DEBUG, "Output buffer: %u bytes "
               "(status %08x).\n", buf->size, buf->status);

        err = av_new_packet(pkt, buf->size);
        if (err < 0)
            goto fail_mapped;

        memcpy(pkt->data, buf->buf, buf->size);
    }

    if (pic->type == PICTURE_TYPE_IDR)
        pkt->flags |= AV_PKT_FLAG_KEY;

    pkt->pts = pic->pts;

    vas = vaUnmapBuffer(ctx->hwctx->display, pic->output_buffer);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to unmap output buffers: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    av_buffer_unref(&pic->output_buffer_ref);
    pic->output_buffer = VA_INVALID_ID;

    av_log(avctx, AV_LOG_DEBUG, "Output read for pic %"PRId64"/%"PRId64".\n",
           pic->display_order, pic->encode_order);
    return 0;

fail_mapped:
    vaUnmapBuffer(ctx->hwctx->display, pic->output_buffer);
fail:
    av_buffer_unref(&pic->output_buffer_ref);
    pic->output_buffer = VA_INVALID_ID;
    return err;
}

static int vaapi_encode_discard(AVCodecContext *avctx,
                                VAAPIEncodePicture *pic)
{
    vaapi_encode_wait(avctx, pic);

    if (pic->output_buffer_ref) {
        av_log(avctx, AV_LOG_DEBUG, "Discard output for pic "
               "%"PRId64"/%"PRId64".\n",
               pic->display_order, pic->encode_order);

        av_buffer_unref(&pic->output_buffer_ref);
        pic->output_buffer = VA_INVALID_ID;
    }

    return 0;
}

static VAAPIEncodePicture *vaapi_encode_alloc(void)
{
    VAAPIEncodePicture *pic;

    pic = av_mallocz(sizeof(*pic));
    if (!pic)
        return NULL;

    pic->input_surface = VA_INVALID_ID;
    pic->recon_surface = VA_INVALID_ID;
    pic->output_buffer = VA_INVALID_ID;

    return pic;
}

static int vaapi_encode_free(AVCodecContext *avctx,
                             VAAPIEncodePicture *pic)
{
    int i;

    if (pic->encode_issued)
        vaapi_encode_discard(avctx, pic);

    for (i = 0; i < pic->nb_slices; i++) {
        av_freep(&pic->slices[i]->priv_data);
        av_freep(&pic->slices[i]->codec_slice_params);
        av_freep(&pic->slices[i]);
    }
    av_freep(&pic->codec_picture_params);

    av_frame_free(&pic->input_image);
    av_frame_free(&pic->recon_image);

    // Output buffer should already be destroyed.
    av_assert0(pic->output_buffer == VA_INVALID_ID);

    av_freep(&pic->priv_data);
    av_freep(&pic->codec_picture_params);

    av_free(pic);

    return 0;
}

static int vaapi_encode_step(AVCodecContext *avctx,
                             VAAPIEncodePicture *target)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic;
    int i, err;

    if (ctx->issue_mode == ISSUE_MODE_SERIALISE_EVERYTHING ||
        ctx->issue_mode == ISSUE_MODE_MINIMISE_LATENCY) {
        // These two modes are equivalent, except that we wait for
        // immediate completion on each operation if serialised.

        if (!target) {
            // No target, nothing to do yet.
            return 0;
        }

        if (target->encode_complete) {
            // Already done.
            return 0;
        }

        pic = target;
        for (i = 0; i < pic->nb_refs; i++) {
            if (!pic->refs[i]->encode_complete) {
                err = vaapi_encode_step(avctx, pic->refs[i]);
                if (err < 0)
                    return err;
            }
        }

        err = vaapi_encode_issue(avctx, pic);
        if (err < 0)
            return err;

    } else if (ctx->issue_mode == ISSUE_MODE_MAXIMISE_THROUGHPUT) {
        int activity;

        // Run through the list of all available pictures repeatedly
        // and issue the first one found which has all dependencies
        // available (including previously-issued but not necessarily
        // completed pictures).
        do {
            activity = 0;
            for (pic = ctx->pic_start; pic; pic = pic->next) {
                if (!pic->input_available || pic->encode_issued)
                    continue;
                for (i = 0; i < pic->nb_refs; i++) {
                    if (!pic->refs[i]->encode_issued)
                        break;
                }
                if (i < pic->nb_refs)
                    continue;
                err = vaapi_encode_issue(avctx, pic);
                if (err < 0)
                    return err;
                activity = 1;
                // Start again from the beginning of the list,
                // because issuing this picture may have satisfied
                // forward dependencies of earlier ones.
                break;
            }
        } while(activity);

        // If we had a defined target for this step then it will
        // always have been issued by now.
        if (target) {
            av_assert0(target->encode_issued && "broken dependencies?");
        }

    } else {
        av_assert0(0);
    }

    return 0;
}

static int vaapi_encode_get_next(AVCodecContext *avctx,
                                 VAAPIEncodePicture **pic_out)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *start, *end, *pic;
    int i;

    for (pic = ctx->pic_start; pic; pic = pic->next) {
        if (pic->next)
            av_assert0(pic->display_order + 1 == pic->next->display_order);
        if (pic->display_order == ctx->input_order) {
            *pic_out = pic;
            return 0;
        }
    }

    pic = vaapi_encode_alloc();
    if (!pic)
        return AVERROR(ENOMEM);

    if (ctx->input_order == 0 || ctx->force_idr ||
        ctx->gop_counter >= avctx->gop_size) {
        pic->type = PICTURE_TYPE_IDR;
        ctx->force_idr = 0;
        ctx->gop_counter = 1;
        ctx->p_counter = 0;
    } else if (ctx->p_counter >= ctx->p_per_i) {
        pic->type = PICTURE_TYPE_I;
        ++ctx->gop_counter;
        ctx->p_counter = 0;
    } else {
        pic->type = PICTURE_TYPE_P;
        pic->refs[0] = ctx->pic_end;
        pic->nb_refs = 1;
        ++ctx->gop_counter;
        ++ctx->p_counter;
    }
    start = end = pic;

    if (pic->type != PICTURE_TYPE_IDR) {
        // If that was not an IDR frame, add B-frames display-before and
        // encode-after it, but not exceeding the GOP size.

        for (i = 0; i < ctx->b_per_p &&
             ctx->gop_counter < avctx->gop_size; i++) {
            pic = vaapi_encode_alloc();
            if (!pic)
                goto fail;

            pic->type = PICTURE_TYPE_B;
            pic->refs[0] = ctx->pic_end;
            pic->refs[1] = end;
            pic->nb_refs = 2;

            pic->next = start;
            pic->display_order = ctx->input_order + ctx->b_per_p - i - 1;
            pic->encode_order  = pic->display_order + 1;
            start = pic;

            ++ctx->gop_counter;
        }
    }

    if (ctx->input_order == 0) {
        pic->display_order = 0;
        pic->encode_order  = 0;

        ctx->pic_start = ctx->pic_end = pic;

    } else {
        for (i = 0, pic = start; pic; i++, pic = pic->next) {
            pic->display_order = ctx->input_order + i;
            if (end->type == PICTURE_TYPE_IDR)
                pic->encode_order = ctx->input_order + i;
            else if (pic == end)
                pic->encode_order = ctx->input_order;
            else
                pic->encode_order = ctx->input_order + i + 1;
        }

        av_assert0(ctx->pic_end);
        ctx->pic_end->next = start;
        ctx->pic_end = end;
    }
    *pic_out = start;

    av_log(avctx, AV_LOG_DEBUG, "Pictures:");
    for (pic = ctx->pic_start; pic; pic = pic->next) {
        av_log(avctx, AV_LOG_DEBUG, " %s (%"PRId64"/%"PRId64")",
               picture_type_name[pic->type],
               pic->display_order, pic->encode_order);
    }
    av_log(avctx, AV_LOG_DEBUG, "\n");

    return 0;

fail:
    while (start) {
        pic = start->next;
        vaapi_encode_free(avctx, start);
        start = pic;
    }
    return AVERROR(ENOMEM);
}

static int vaapi_encode_truncate_gop(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic, *last_pic, *next;

    // Find the last picture we actually have input for.
    for (pic = ctx->pic_start; pic; pic = pic->next) {
        if (!pic->input_available)
            break;
        last_pic = pic;
    }

    if (pic) {
        av_assert0(last_pic);

        if (last_pic->type == PICTURE_TYPE_B) {
            // Some fixing up is required.  Change the type of this
            // picture to P, then modify preceding B references which
            // point beyond it to point at it instead.

            last_pic->type = PICTURE_TYPE_P;
            last_pic->encode_order = last_pic->refs[1]->encode_order;

            for (pic = ctx->pic_start; pic != last_pic; pic = pic->next) {
                if (pic->type == PICTURE_TYPE_B &&
                    pic->refs[1] == last_pic->refs[1])
                    pic->refs[1] = last_pic;
            }

            last_pic->nb_refs = 1;
            last_pic->refs[1] = NULL;
        } else {
            // We can use the current structure (no references point
            // beyond the end), but there are unused pics to discard.
        }

        // Discard all following pics, they will never be used.
        for (pic = last_pic->next; pic; pic = next) {
            next = pic->next;
            vaapi_encode_free(avctx, pic);
        }

        last_pic->next = NULL;
        ctx->pic_end = last_pic;

    } else {
        // Input is available for all pictures, so we don't need to
        // mangle anything.
    }

    av_log(avctx, AV_LOG_DEBUG, "Pictures ending truncated GOP:");
    for (pic = ctx->pic_start; pic; pic = pic->next) {
        av_log(avctx, AV_LOG_DEBUG, " %s (%"PRId64"/%"PRId64")",
               picture_type_name[pic->type],
               pic->display_order, pic->encode_order);
    }
    av_log(avctx, AV_LOG_DEBUG, "\n");

    return 0;
}

static int vaapi_encode_clear_old(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic, *old;
    int i;

    while (ctx->pic_start != ctx->pic_end) {
        old = ctx->pic_start;
        if (old->encode_order > ctx->output_order)
            break;

        for (pic = old->next; pic; pic = pic->next) {
            if (pic->encode_complete)
                continue;
            for (i = 0; i < pic->nb_refs; i++) {
                if (pic->refs[i] == old) {
                    // We still need this picture because it's referred to
                    // directly by a later one, so it and all following
                    // pictures have to stay.
                    return 0;
                }
            }
        }

        pic = ctx->pic_start;
        ctx->pic_start = pic->next;
        vaapi_encode_free(avctx, pic);
    }

    return 0;
}

int ff_vaapi_encode2(AVCodecContext *avctx, AVPacket *pkt,
                     const AVFrame *input_image, int *got_packet)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic;
    int err;

    if (input_image) {
        av_log(avctx, AV_LOG_DEBUG, "Encode frame: %ux%u (%"PRId64").\n",
               input_image->width, input_image->height, input_image->pts);

        if (input_image->pict_type == AV_PICTURE_TYPE_I) {
            err = vaapi_encode_truncate_gop(avctx);
            if (err < 0)
                goto fail;
            ctx->force_idr = 1;
        }

        err = vaapi_encode_get_next(avctx, &pic);
        if (err) {
            av_log(avctx, AV_LOG_ERROR, "Input setup failed: %d.\n", err);
            return err;
        }

        pic->input_image = av_frame_alloc();
        if (!pic->input_image) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        err = av_frame_ref(pic->input_image, input_image);
        if (err < 0)
            goto fail;
        pic->input_surface = (VASurfaceID)(uintptr_t)input_image->data[3];
        pic->pts = input_image->pts;

        if (ctx->input_order == 0)
            ctx->first_pts = pic->pts;
        if (ctx->input_order == ctx->decode_delay)
            ctx->dts_pts_diff = pic->pts - ctx->first_pts;
        if (ctx->output_delay > 0)
            ctx->ts_ring[ctx->input_order % (3 * ctx->output_delay)] = pic->pts;

        pic->input_available = 1;

    } else {
        if (!ctx->end_of_stream) {
            err = vaapi_encode_truncate_gop(avctx);
            if (err < 0)
                goto fail;
            ctx->end_of_stream = 1;
        }
    }

    ++ctx->input_order;
    ++ctx->output_order;
    av_assert0(ctx->output_order + ctx->output_delay + 1 == ctx->input_order);

    for (pic = ctx->pic_start; pic; pic = pic->next)
        if (pic->encode_order == ctx->output_order)
            break;

    // pic can be null here if we don't have a specific target in this
    // iteration.  We might still issue encodes if things can be overlapped,
    // even though we don't intend to output anything.

    err = vaapi_encode_step(avctx, pic);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Encode failed: %d.\n", err);
        goto fail;
    }

    if (!pic) {
        *got_packet = 0;
    } else {
        err = vaapi_encode_output(avctx, pic, pkt);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Output failed: %d.\n", err);
            goto fail;
        }

        if (ctx->output_delay == 0) {
            pkt->dts = pkt->pts;
        } else if (ctx->output_order < ctx->decode_delay) {
            if (ctx->ts_ring[ctx->output_order] < INT64_MIN + ctx->dts_pts_diff)
                pkt->dts = INT64_MIN;
            else
                pkt->dts = ctx->ts_ring[ctx->output_order] - ctx->dts_pts_diff;
        } else {
            pkt->dts = ctx->ts_ring[(ctx->output_order - ctx->decode_delay) %
                                    (3 * ctx->output_delay)];
        }

        *got_packet = 1;
    }

    err = vaapi_encode_clear_old(avctx);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "List clearing failed: %d.\n", err);
        goto fail;
    }

    return 0;

fail:
    // Unclear what to clean up on failure.  There are probably some things we
    // could do usefully clean up here, but for now just leave them for uninit()
    // to do instead.
    return err;
}

static av_cold int vaapi_encode_config_attributes(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAStatus vas;
    int i, n, err;
    VAProfile    *profiles    = NULL;
    VAEntrypoint *entrypoints = NULL;
    VAConfigAttrib attr[] = {
        { VAConfigAttribRTFormat         },
        { VAConfigAttribRateControl      },
        { VAConfigAttribEncMaxRefFrames  },
        { VAConfigAttribEncPackedHeaders },
    };

    n = vaMaxNumProfiles(ctx->hwctx->display);
    profiles = av_malloc_array(n, sizeof(VAProfile));
    if (!profiles) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    vas = vaQueryConfigProfiles(ctx->hwctx->display, profiles, &n);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Failed to query profiles: %d (%s).\n",
               vas, vaErrorStr(vas));
        err = AVERROR(ENOSYS);
        goto fail;
    }
    for (i = 0; i < n; i++) {
        if (profiles[i] == ctx->va_profile)
            break;
    }
    if (i >= n) {
        av_log(ctx, AV_LOG_ERROR, "Encoding profile not found (%d).\n",
               ctx->va_profile);
        err = AVERROR(ENOSYS);
        goto fail;
    }

    n = vaMaxNumEntrypoints(ctx->hwctx->display);
    entrypoints = av_malloc_array(n, sizeof(VAEntrypoint));
    if (!entrypoints) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    vas = vaQueryConfigEntrypoints(ctx->hwctx->display, ctx->va_profile,
                                   entrypoints, &n);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Failed to query entrypoints for "
               "profile %u: %d (%s).\n", ctx->va_profile,
               vas, vaErrorStr(vas));
        err = AVERROR(ENOSYS);
        goto fail;
    }
    for (i = 0; i < n; i++) {
        if (entrypoints[i] == ctx->va_entrypoint)
            break;
    }
    if (i >= n) {
        av_log(ctx, AV_LOG_ERROR, "Encoding entrypoint not found "
               "(%d / %d).\n", ctx->va_profile, ctx->va_entrypoint);
        err = AVERROR(ENOSYS);
        goto fail;
    }

    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile, ctx->va_entrypoint,
                                attr, FF_ARRAY_ELEMS(attr));
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to fetch config "
               "attributes: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EINVAL);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(attr); i++) {
        if (attr[i].value == VA_ATTRIB_NOT_SUPPORTED) {
            // Unfortunately we have to treat this as "don't know" and hope
            // for the best, because the Intel MJPEG encoder returns this
            // for all the interesting attributes.
            continue;
        }
        switch (attr[i].type) {
        case VAConfigAttribRTFormat:
            if (!(ctx->va_rt_format & attr[i].value)) {
                av_log(avctx, AV_LOG_ERROR, "Surface RT format %#x "
                       "is not supported (mask %#x).\n",
                       ctx->va_rt_format, attr[i].value);
                err = AVERROR(EINVAL);
                goto fail;
            }
            ctx->config_attributes[ctx->nb_config_attributes++] =
                (VAConfigAttrib) {
                .type  = VAConfigAttribRTFormat,
                .value = ctx->va_rt_format,
            };
            break;
        case VAConfigAttribRateControl:
            // Hack for backward compatibility: CBR was the only
            // usable RC mode for a long time, so old drivers will
            // only have it.  Normal default options may now choose
            // VBR and then fail, however, so override it here with
            // CBR if that is the only supported mode.
            if (ctx->va_rc_mode == VA_RC_VBR &&
                !(attr[i].value & VA_RC_VBR) &&
                (attr[i].value & VA_RC_CBR)) {
                av_log(avctx, AV_LOG_WARNING, "VBR rate control is "
                       "not supported with this driver version; "
                       "using CBR instead.\n");
                ctx->va_rc_mode = VA_RC_CBR;
            }
            if (!(ctx->va_rc_mode & attr[i].value)) {
                av_log(avctx, AV_LOG_ERROR, "Rate control mode %#x "
                       "is not supported (mask: %#x).\n",
                       ctx->va_rc_mode, attr[i].value);
                err = AVERROR(EINVAL);
                goto fail;
            }
            ctx->config_attributes[ctx->nb_config_attributes++] =
                (VAConfigAttrib) {
                .type  = VAConfigAttribRateControl,
                .value = ctx->va_rc_mode,
            };
            break;
        case VAConfigAttribEncMaxRefFrames:
        {
            unsigned int ref_l0 = attr[i].value & 0xffff;
            unsigned int ref_l1 = (attr[i].value >> 16) & 0xffff;

            if (avctx->gop_size > 1 && ref_l0 < 1) {
                av_log(avctx, AV_LOG_ERROR, "P frames are not "
                       "supported (%#x).\n", attr[i].value);
                err = AVERROR(EINVAL);
                goto fail;
            }
            if (avctx->max_b_frames > 0 && ref_l1 < 1) {
                av_log(avctx, AV_LOG_ERROR, "B frames are not "
                       "supported (%#x).\n", attr[i].value);
                err = AVERROR(EINVAL);
                goto fail;
            }
        }
        break;
        case VAConfigAttribEncPackedHeaders:
            if (ctx->va_packed_headers & ~attr[i].value) {
                // This isn't fatal, but packed headers are always
                // preferable because they are under our control.
                // When absent, the driver is generating them and some
                // features may not work (e.g. VUI or SEI in H.264).
                av_log(avctx, AV_LOG_WARNING, "Warning: some packed "
                       "headers are not supported (want %#x, got %#x).\n",
                       ctx->va_packed_headers, attr[i].value);
                ctx->va_packed_headers &= attr[i].value;
            }
            ctx->config_attributes[ctx->nb_config_attributes++] =
                (VAConfigAttrib) {
                .type  = VAConfigAttribEncPackedHeaders,
                .value = ctx->va_packed_headers,
            };
            break;
        default:
            av_assert0(0 && "Unexpected config attribute.");
        }
    }

    err = 0;
fail:
    av_freep(&profiles);
    av_freep(&entrypoints);
    return err;
}

static av_cold int vaapi_encode_init_rate_control(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    int rc_bits_per_second;
    int rc_target_percentage;
    int rc_window_size;
    int hrd_buffer_size;
    int hrd_initial_buffer_fullness;
    int fr_num, fr_den;

    if (avctx->bit_rate > INT32_MAX) {
        av_log(avctx, AV_LOG_ERROR, "Target bitrate of 2^31 bps or "
               "higher is not supported.\n");
        return AVERROR(EINVAL);
    }

    if (avctx->rc_buffer_size)
        hrd_buffer_size = avctx->rc_buffer_size;
    else
        hrd_buffer_size = avctx->bit_rate;
    if (avctx->rc_initial_buffer_occupancy)
        hrd_initial_buffer_fullness = avctx->rc_initial_buffer_occupancy;
    else
        hrd_initial_buffer_fullness = hrd_buffer_size * 3 / 4;

    if (ctx->va_rc_mode == VA_RC_CBR) {
        rc_bits_per_second   = avctx->bit_rate;
        rc_target_percentage = 100;
        rc_window_size       = 1000;
    } else {
        if (avctx->rc_max_rate < avctx->bit_rate) {
            // Max rate is unset or invalid, just use the normal bitrate.
            rc_bits_per_second   = avctx->bit_rate;
            rc_target_percentage = 100;
        } else {
            rc_bits_per_second   = avctx->rc_max_rate;
            rc_target_percentage = (avctx->bit_rate * 100) / rc_bits_per_second;
        }
        rc_window_size = (hrd_buffer_size * 1000) / avctx->bit_rate;
    }

    ctx->rc_params.misc.type = VAEncMiscParameterTypeRateControl;
    ctx->rc_params.rc = (VAEncMiscParameterRateControl) {
        .bits_per_second   = rc_bits_per_second,
        .target_percentage = rc_target_percentage,
        .window_size       = rc_window_size,
        .initial_qp        = 0,
        .min_qp            = (avctx->qmin > 0 ? avctx->qmin : 0),
        .basic_unit_size   = 0,
    };
    ctx->global_params[ctx->nb_global_params] =
        &ctx->rc_params.misc;
    ctx->global_params_size[ctx->nb_global_params++] =
        sizeof(ctx->rc_params);

    ctx->hrd_params.misc.type = VAEncMiscParameterTypeHRD;
    ctx->hrd_params.hrd = (VAEncMiscParameterHRD) {
        .initial_buffer_fullness = hrd_initial_buffer_fullness,
        .buffer_size             = hrd_buffer_size,
    };
    ctx->global_params[ctx->nb_global_params] =
        &ctx->hrd_params.misc;
    ctx->global_params_size[ctx->nb_global_params++] =
        sizeof(ctx->hrd_params);

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
        av_reduce(&fr_num, &fr_den,
                  avctx->framerate.num, avctx->framerate.den, 65535);
    else
        av_reduce(&fr_num, &fr_den,
                  avctx->time_base.den, avctx->time_base.num, 65535);

    ctx->fr_params.misc.type = VAEncMiscParameterTypeFrameRate;
    ctx->fr_params.fr.framerate = (unsigned int)fr_den << 16 | fr_num;

#if VA_CHECK_VERSION(0, 40, 0)
    ctx->global_params[ctx->nb_global_params] =
        &ctx->fr_params.misc;
    ctx->global_params_size[ctx->nb_global_params++] =
        sizeof(ctx->fr_params);
#endif

    return 0;
}

static void vaapi_encode_free_output_buffer(void *opaque,
                                            uint8_t *data)
{
    AVCodecContext   *avctx = opaque;
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VABufferID buffer_id;

    buffer_id = (VABufferID)(uintptr_t)data;

    vaDestroyBuffer(ctx->hwctx->display, buffer_id);

    av_log(avctx, AV_LOG_DEBUG, "Freed output buffer %#x\n", buffer_id);
}

static AVBufferRef *vaapi_encode_alloc_output_buffer(void *opaque,
                                                     int size)
{
    AVCodecContext   *avctx = opaque;
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VABufferID buffer_id;
    VAStatus vas;
    AVBufferRef *ref;

    // The output buffer size is fixed, so it needs to be large enough
    // to hold the largest possible compressed frame.  We assume here
    // that the uncompressed frame plus some header data is an upper
    // bound on that.
    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         VAEncCodedBufferType,
                         3 * ctx->surface_width * ctx->surface_height +
                         (1 << 16), 1, 0, &buffer_id);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create bitstream "
               "output buffer: %d (%s).\n", vas, vaErrorStr(vas));
        return NULL;
    }

    av_log(avctx, AV_LOG_DEBUG, "Allocated output buffer %#x\n", buffer_id);

    ref = av_buffer_create((uint8_t*)(uintptr_t)buffer_id,
                           sizeof(buffer_id),
                           &vaapi_encode_free_output_buffer,
                           avctx, AV_BUFFER_FLAG_READONLY);
    if (!ref) {
        vaDestroyBuffer(ctx->hwctx->display, buffer_id);
        return NULL;
    }

    return ref;
}

static av_cold int vaapi_encode_create_recon_frames(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    AVVAAPIHWConfig *hwconfig = NULL;
    AVHWFramesConstraints *constraints = NULL;
    enum AVPixelFormat recon_format;
    int err, i;

    hwconfig = av_hwdevice_hwconfig_alloc(ctx->device_ref);
    if (!hwconfig) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    hwconfig->config_id = ctx->va_config;

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
    av_log(avctx, AV_LOG_DEBUG, "Using %s as format of "
           "reconstructed frames.\n", av_get_pix_fmt_name(recon_format));

    if (ctx->surface_width  < constraints->min_width  ||
        ctx->surface_height < constraints->min_height ||
        ctx->surface_width  > constraints->max_width ||
        ctx->surface_height > constraints->max_height) {
        av_log(avctx, AV_LOG_ERROR, "Hardware does not support encoding at "
               "size %dx%d (constraints: width %d-%d height %d-%d).\n",
               ctx->surface_width, ctx->surface_height,
               constraints->min_width,  constraints->max_width,
               constraints->min_height, constraints->max_height);
        err = AVERROR(EINVAL);
        goto fail;
    }

    av_freep(&hwconfig);
    av_hwframe_constraints_free(&constraints);

    ctx->recon_frames_ref = av_hwframe_ctx_alloc(ctx->device_ref);
    if (!ctx->recon_frames_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    ctx->recon_frames = (AVHWFramesContext*)ctx->recon_frames_ref->data;

    ctx->recon_frames->format    = AV_PIX_FMT_VAAPI;
    ctx->recon_frames->sw_format = recon_format;
    ctx->recon_frames->width     = ctx->surface_width;
    ctx->recon_frames->height    = ctx->surface_height;
    // At most three IDR/I/P frames and two runs of B frames can be in
    // flight at any one time.
    ctx->recon_frames->initial_pool_size = 3 + 2 * avctx->max_b_frames;

    err = av_hwframe_ctx_init(ctx->recon_frames_ref);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise reconstructed "
               "frame context: %d.\n", err);
        goto fail;
    }

    err = 0;
  fail:
    av_freep(&hwconfig);
    av_hwframe_constraints_free(&constraints);
    return err;
}

av_cold int ff_vaapi_encode_init(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    AVVAAPIFramesContext *recon_hwctx = NULL;
    VAStatus vas;
    int err;

    if (!avctx->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "A hardware frames reference is "
               "required to associate the encoding device.\n");
        return AVERROR(EINVAL);
    }

    ctx->codec_options = ctx->codec_options_data;

    ctx->va_config  = VA_INVALID_ID;
    ctx->va_context = VA_INVALID_ID;

    ctx->priv_data = av_mallocz(ctx->codec->priv_data_size);
    if (!ctx->priv_data) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->input_frames_ref = av_buffer_ref(avctx->hw_frames_ctx);
    if (!ctx->input_frames_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    ctx->input_frames = (AVHWFramesContext*)ctx->input_frames_ref->data;

    ctx->device_ref = av_buffer_ref(ctx->input_frames->device_ref);
    if (!ctx->device_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    ctx->device = (AVHWDeviceContext*)ctx->device_ref->data;
    ctx->hwctx = ctx->device->hwctx;

    err = vaapi_encode_config_attributes(avctx);
    if (err < 0)
        goto fail;

    vas = vaCreateConfig(ctx->hwctx->display,
                         ctx->va_profile, ctx->va_entrypoint,
                         ctx->config_attributes, ctx->nb_config_attributes,
                         &ctx->va_config);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create encode pipeline "
               "configuration: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    err = vaapi_encode_create_recon_frames(avctx);
    if (err < 0)
        goto fail;

    recon_hwctx = ctx->recon_frames->hwctx;
    vas = vaCreateContext(ctx->hwctx->display, ctx->va_config,
                          ctx->surface_width, ctx->surface_height,
                          VA_PROGRESSIVE,
                          recon_hwctx->surface_ids,
                          recon_hwctx->nb_surfaces,
                          &ctx->va_context);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create encode pipeline "
               "context: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    ctx->output_buffer_pool =
        av_buffer_pool_init2(sizeof(VABufferID), avctx,
                             &vaapi_encode_alloc_output_buffer, NULL);
    if (!ctx->output_buffer_pool) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (ctx->va_rc_mode & ~VA_RC_CQP) {
        err = vaapi_encode_init_rate_control(avctx);
        if (err < 0)
            goto fail;
    }

    if (ctx->codec->configure) {
        err = ctx->codec->configure(avctx);
        if (err < 0)
            goto fail;
    }

    ctx->input_order  = 0;
    ctx->output_delay = avctx->max_b_frames;
    ctx->decode_delay = 1;
    ctx->output_order = - ctx->output_delay - 1;

    // Currently we never generate I frames, only IDR.
    ctx->p_per_i = ((avctx->gop_size + avctx->max_b_frames) /
                    (avctx->max_b_frames + 1));
    ctx->b_per_p = avctx->max_b_frames;

    if (ctx->codec->sequence_params_size > 0) {
        ctx->codec_sequence_params =
            av_mallocz(ctx->codec->sequence_params_size);
        if (!ctx->codec_sequence_params) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
    }
    if (ctx->codec->picture_params_size > 0) {
        ctx->codec_picture_params =
            av_mallocz(ctx->codec->picture_params_size);
        if (!ctx->codec_picture_params) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (ctx->codec->init_sequence_params) {
        err = ctx->codec->init_sequence_params(avctx);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Codec sequence initialisation "
                   "failed: %d.\n", err);
            goto fail;
        }
    }

    // This should be configurable somehow.  (Needs testing on a machine
    // where it actually overlaps properly, though.)
    ctx->issue_mode = ISSUE_MODE_MAXIMISE_THROUGHPUT;

    if (ctx->va_packed_headers & VA_ENC_PACKED_HEADER_SEQUENCE &&
        ctx->codec->write_sequence_header) {
        char data[MAX_PARAM_BUFFER_SIZE];
        size_t bit_len = 8 * sizeof(data);

        err = ctx->codec->write_sequence_header(avctx, data, &bit_len);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to write sequence header "
                   "for extradata: %d.\n", err);
            goto fail;
        } else {
            avctx->extradata_size = (bit_len + 7) / 8;
            avctx->extradata = av_mallocz(avctx->extradata_size +
                                          AV_INPUT_BUFFER_PADDING_SIZE);
            if (!avctx->extradata) {
                err = AVERROR(ENOMEM);
                goto fail;
            }
            memcpy(avctx->extradata, data, avctx->extradata_size);
        }
    }

    return 0;

fail:
    ff_vaapi_encode_close(avctx);
    return err;
}

av_cold int ff_vaapi_encode_close(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic, *next;

    for (pic = ctx->pic_start; pic; pic = next) {
        next = pic->next;
        vaapi_encode_free(avctx, pic);
    }

    if (ctx->va_context != VA_INVALID_ID) {
        vaDestroyContext(ctx->hwctx->display, ctx->va_context);
        ctx->va_context = VA_INVALID_ID;
    }

    if (ctx->va_config != VA_INVALID_ID) {
        vaDestroyConfig(ctx->hwctx->display, ctx->va_config);
        ctx->va_config = VA_INVALID_ID;
    }

    av_buffer_pool_uninit(&ctx->output_buffer_pool);

    av_freep(&ctx->codec_sequence_params);
    av_freep(&ctx->codec_picture_params);

    av_buffer_unref(&ctx->recon_frames_ref);
    av_buffer_unref(&ctx->input_frames_ref);
    av_buffer_unref(&ctx->device_ref);

    av_freep(&ctx->priv_data);

    return 0;
}
