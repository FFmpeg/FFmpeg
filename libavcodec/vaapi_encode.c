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

#include "config_components.h"

#include <inttypes.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/pixdesc.h"

#include "vaapi_encode.h"
#include "encode.h"
#include "avcodec.h"

const AVCodecHWConfigInternal *const ff_vaapi_encode_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(VAAPI, VAAPI),
    NULL,
};

static const char * const picture_type_name[] = { "IDR", "I", "P", "B" };

static int vaapi_encode_make_packed_header(AVCodecContext *avctx,
                                           VAAPIEncodePicture *pic,
                                           int type, char *data, size_t bit_len)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAStatus vas;
    VABufferID param_buffer, data_buffer;
    VABufferID *tmp;
    VAEncPackedHeaderParameterBuffer params = {
        .type = type,
        .bit_length = bit_len,
        .has_emulation_bytes = 1,
    };

    tmp = av_realloc_array(pic->param_buffers, sizeof(*tmp), pic->nb_param_buffers + 2);
    if (!tmp)
        return AVERROR(ENOMEM);
    pic->param_buffers = tmp;

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
    VABufferID *tmp;
    VABufferID buffer;

    tmp = av_realloc_array(pic->param_buffers, sizeof(*tmp), pic->nb_param_buffers + 1);
    if (!tmp)
        return AVERROR(ENOMEM);
    pic->param_buffers = tmp;

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

static int vaapi_encode_make_misc_param_buffer(AVCodecContext *avctx,
                                               VAAPIEncodePicture *pic,
                                               int type,
                                               const void *data, size_t len)
{
    // Construct the buffer on the stack - 1KB is much larger than any
    // current misc parameter buffer type (the largest is EncQuality at
    // 224 bytes).
    uint8_t buffer[1024];
    VAEncMiscParameterBuffer header = {
        .type = type,
    };
    size_t buffer_size = sizeof(header) + len;
    av_assert0(buffer_size <= sizeof(buffer));

    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), data, len);

    return vaapi_encode_make_param_buffer(avctx, pic,
                                          VAEncMiscParameterBufferType,
                                          buffer, buffer_size);
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

#if VA_CHECK_VERSION(1, 9, 0)
    if (ctx->has_sync_buffer_func) {
        vas = vaSyncBuffer(ctx->hwctx->display,
                           pic->output_buffer,
                           VA_TIMEOUT_INFINITE);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to sync to output buffer completion: "
                   "%d (%s).\n", vas, vaErrorStr(vas));
            return AVERROR(EIO);
        }
    } else
#endif
    { // If vaSyncBuffer is not implemented, try old version API.
        vas = vaSyncSurface(ctx->hwctx->display, pic->input_surface);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to sync to picture completion: "
                "%d (%s).\n", vas, vaErrorStr(vas));
            return AVERROR(EIO);
        }
    }

    // Input is definitely finished with now.
    av_frame_free(&pic->input_image);

    pic->encode_complete = 1;
    return 0;
}

static int vaapi_encode_make_row_slice(AVCodecContext *avctx,
                                       VAAPIEncodePicture *pic)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodeSlice *slice;
    int i, rounding;

    for (i = 0; i < pic->nb_slices; i++)
        pic->slices[i].row_size = ctx->slice_size;

    rounding = ctx->slice_block_rows - ctx->nb_slices * ctx->slice_size;
    if (rounding > 0) {
        // Place rounding error at top and bottom of frame.
        av_assert0(rounding < pic->nb_slices);
        // Some Intel drivers contain a bug where the encoder will fail
        // if the last slice is smaller than the one before it.  Since
        // that's straightforward to avoid here, just do so.
        if (rounding <= 2) {
            for (i = 0; i < rounding; i++)
                ++pic->slices[i].row_size;
        } else {
            for (i = 0; i < (rounding + 1) / 2; i++)
                ++pic->slices[pic->nb_slices - i - 1].row_size;
            for (i = 0; i < rounding / 2; i++)
                ++pic->slices[i].row_size;
        }
    } else if (rounding < 0) {
        // Remove rounding error from last slice only.
        av_assert0(rounding < ctx->slice_size);
        pic->slices[pic->nb_slices - 1].row_size += rounding;
    }

    for (i = 0; i < pic->nb_slices; i++) {
        slice = &pic->slices[i];
        slice->index = i;
        if (i == 0) {
            slice->row_start   = 0;
            slice->block_start = 0;
        } else {
            const VAAPIEncodeSlice *prev = &pic->slices[i - 1];
            slice->row_start   = prev->row_start   + prev->row_size;
            slice->block_start = prev->block_start + prev->block_size;
        }
        slice->block_size  = slice->row_size * ctx->slice_block_cols;

        av_log(avctx, AV_LOG_DEBUG, "Slice %d: %d-%d (%d rows), "
               "%d-%d (%d blocks).\n", i, slice->row_start,
               slice->row_start + slice->row_size - 1, slice->row_size,
               slice->block_start, slice->block_start + slice->block_size - 1,
               slice->block_size);
    }

    return 0;
}

static int vaapi_encode_make_tile_slice(AVCodecContext *avctx,
                                        VAAPIEncodePicture *pic)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodeSlice *slice;
    int i, j, index;

    for (i = 0; i < ctx->tile_cols; i++) {
        for (j = 0; j < ctx->tile_rows; j++) {
            index        = j * ctx->tile_cols + i;
            slice        = &pic->slices[index];
            slice->index = index;

            pic->slices[index].block_start = ctx->col_bd[i] +
                                             ctx->row_bd[j] * ctx->slice_block_cols;
            pic->slices[index].block_size  = ctx->row_height[j] * ctx->col_width[i];

            av_log(avctx, AV_LOG_DEBUG, "Slice %2d: (%2d, %2d) start at: %4d "
               "width:%2d height:%2d (%d blocks).\n", index, ctx->col_bd[i],
               ctx->row_bd[j], slice->block_start, ctx->col_width[i],
               ctx->row_height[j], slice->block_size);
        }
    }

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
    av_unused AVFrameSideData *sd;

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

    av_assert0(!pic->encode_issued);
    for (i = 0; i < pic->nb_refs; i++) {
        av_assert0(pic->refs[i]);
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

    if (pic->type == PICTURE_TYPE_IDR && ctx->codec->init_sequence_params) {
        err = vaapi_encode_make_param_buffer(avctx, pic,
                                             VAEncSequenceParameterBufferType,
                                             ctx->codec_sequence_params,
                                             ctx->codec->sequence_params_size);
        if (err < 0)
            goto fail;
    }

    if (pic->type == PICTURE_TYPE_IDR) {
        for (i = 0; i < ctx->nb_global_params; i++) {
            err = vaapi_encode_make_misc_param_buffer(avctx, pic,
                                                      ctx->global_params_type[i],
                                                      ctx->global_params[i],
                                                      ctx->global_params_size[i]);
            if (err < 0)
                goto fail;
        }
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

#if VA_CHECK_VERSION(1, 5, 0)
    if (ctx->max_frame_size) {
        err = vaapi_encode_make_misc_param_buffer(avctx, pic,
                                                  VAEncMiscParameterTypeMaxFrameSize,
                                                  &ctx->mfs_params,
                                                  sizeof(ctx->mfs_params));
        if (err < 0)
            goto fail;
    }
#endif

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

    if (pic->nb_slices == 0)
        pic->nb_slices = ctx->nb_slices;
    if (pic->nb_slices > 0) {
        pic->slices = av_calloc(pic->nb_slices, sizeof(*pic->slices));
        if (!pic->slices) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        if (ctx->tile_rows && ctx->tile_cols)
            vaapi_encode_make_tile_slice(avctx, pic);
        else
            vaapi_encode_make_row_slice(avctx, pic);
    }

    for (i = 0; i < pic->nb_slices; i++) {
        slice = &pic->slices[i];

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

#if VA_CHECK_VERSION(1, 0, 0)
    sd = av_frame_get_side_data(pic->input_image,
                                AV_FRAME_DATA_REGIONS_OF_INTEREST);
    if (sd && ctx->roi_allowed) {
        const AVRegionOfInterest *roi;
        uint32_t roi_size;
        VAEncMiscParameterBufferROI param_roi;
        int nb_roi, i, v;

        roi = (const AVRegionOfInterest*)sd->data;
        roi_size = roi->self_size;
        av_assert0(roi_size && sd->size % roi_size == 0);
        nb_roi = sd->size / roi_size;
        if (nb_roi > ctx->roi_max_regions) {
            if (!ctx->roi_warned) {
                av_log(avctx, AV_LOG_WARNING, "More ROIs set than "
                       "supported by driver (%d > %d).\n",
                       nb_roi, ctx->roi_max_regions);
                ctx->roi_warned = 1;
            }
            nb_roi = ctx->roi_max_regions;
        }

        pic->roi = av_calloc(nb_roi, sizeof(*pic->roi));
        if (!pic->roi) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        // For overlapping regions, the first in the array takes priority.
        for (i = 0; i < nb_roi; i++) {
            roi = (const AVRegionOfInterest*)(sd->data + roi_size * i);

            av_assert0(roi->qoffset.den != 0);
            v = roi->qoffset.num * ctx->roi_quant_range / roi->qoffset.den;
            av_log(avctx, AV_LOG_DEBUG, "ROI: (%d,%d)-(%d,%d) -> %+d.\n",
                   roi->top, roi->left, roi->bottom, roi->right, v);

            pic->roi[i] = (VAEncROI) {
                .roi_rectangle = {
                    .x      = roi->left,
                    .y      = roi->top,
                    .width  = roi->right  - roi->left,
                    .height = roi->bottom - roi->top,
                },
                .roi_value = av_clip_int8(v),
            };
        }

        param_roi = (VAEncMiscParameterBufferROI) {
            .num_roi      = nb_roi,
            .max_delta_qp = INT8_MAX,
            .min_delta_qp = INT8_MIN,
            .roi          = pic->roi,
            .roi_flags.bits.roi_value_is_qp_delta = 1,
        };

        err = vaapi_encode_make_misc_param_buffer(avctx, pic,
                                                  VAEncMiscParameterTypeROI,
                                                  &param_roi,
                                                  sizeof(param_roi));
        if (err < 0)
            goto fail;
    }
#endif

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
        if (CONFIG_VAAPI_1 || ctx->hwctx->driver_quirks &
            AV_VAAPI_DRIVER_QUIRK_RENDER_PARAM_BUFFERS)
            goto fail;
        else
            goto fail_at_end;
    }

    if (CONFIG_VAAPI_1 || ctx->hwctx->driver_quirks &
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

    return 0;

fail_with_picture:
    vaEndPicture(ctx->hwctx->display, ctx->va_context);
fail:
    for(i = 0; i < pic->nb_param_buffers; i++)
        vaDestroyBuffer(ctx->hwctx->display, pic->param_buffers[i]);
    if (pic->slices) {
        for (i = 0; i < pic->nb_slices; i++)
            av_freep(&pic->slices[i].codec_slice_params);
    }
fail_at_end:
    av_freep(&pic->codec_picture_params);
    av_freep(&pic->param_buffers);
    av_freep(&pic->slices);
    av_freep(&pic->roi);
    av_frame_free(&pic->recon_image);
    av_buffer_unref(&pic->output_buffer_ref);
    pic->output_buffer = VA_INVALID_ID;
    return err;
}

static int vaapi_encode_output(AVCodecContext *avctx,
                               VAAPIEncodePicture *pic, AVPacket *pkt)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VACodedBufferSegment *buf_list, *buf;
    VAStatus vas;
    int total_size = 0;
    uint8_t *ptr;
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

    for (buf = buf_list; buf; buf = buf->next)
        total_size += buf->size;

    err = ff_get_encode_buffer(avctx, pkt, total_size, 0);
    ptr = pkt->data;

    if (err < 0)
        goto fail_mapped;

    for (buf = buf_list; buf; buf = buf->next) {
        av_log(avctx, AV_LOG_DEBUG, "Output buffer: %u bytes "
               "(status %08x).\n", buf->size, buf->status);

        memcpy(ptr, buf->buf, buf->size);
        ptr += buf->size;
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

static VAAPIEncodePicture *vaapi_encode_alloc(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic;

    pic = av_mallocz(sizeof(*pic));
    if (!pic)
        return NULL;

    if (ctx->codec->picture_priv_data_size > 0) {
        pic->priv_data = av_mallocz(ctx->codec->picture_priv_data_size);
        if (!pic->priv_data) {
            av_freep(&pic);
            return NULL;
        }
    }

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

    if (pic->slices) {
        for (i = 0; i < pic->nb_slices; i++)
            av_freep(&pic->slices[i].codec_slice_params);
    }
    av_freep(&pic->codec_picture_params);

    av_frame_free(&pic->input_image);
    av_frame_free(&pic->recon_image);

    av_freep(&pic->param_buffers);
    av_freep(&pic->slices);
    // Output buffer should already be destroyed.
    av_assert0(pic->output_buffer == VA_INVALID_ID);

    av_freep(&pic->priv_data);
    av_freep(&pic->codec_picture_params);
    av_freep(&pic->roi);

    av_free(pic);

    return 0;
}

static void vaapi_encode_add_ref(AVCodecContext *avctx,
                                 VAAPIEncodePicture *pic,
                                 VAAPIEncodePicture *target,
                                 int is_ref, int in_dpb, int prev)
{
    int refs = 0;

    if (is_ref) {
        av_assert0(pic != target);
        av_assert0(pic->nb_refs < MAX_PICTURE_REFERENCES);
        pic->refs[pic->nb_refs++] = target;
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

static void vaapi_encode_remove_refs(AVCodecContext *avctx,
                                     VAAPIEncodePicture *pic,
                                     int level)
{
    int i;

    if (pic->ref_removed[level])
        return;

    for (i = 0; i < pic->nb_refs; i++) {
        av_assert0(pic->refs[i]);
        --pic->refs[i]->ref_count[level];
        av_assert0(pic->refs[i]->ref_count[level] >= 0);
    }

    for (i = 0; i < pic->nb_dpb_pics; i++) {
        av_assert0(pic->dpb[i]);
        --pic->dpb[i]->ref_count[level];
        av_assert0(pic->dpb[i]->ref_count[level] >= 0);
    }

    av_assert0(pic->prev || pic->type == PICTURE_TYPE_IDR);
    if (pic->prev) {
        --pic->prev->ref_count[level];
        av_assert0(pic->prev->ref_count[level] >= 0);
    }

    pic->ref_removed[level] = 1;
}

static void vaapi_encode_set_b_pictures(AVCodecContext *avctx,
                                        VAAPIEncodePicture *start,
                                        VAAPIEncodePicture *end,
                                        VAAPIEncodePicture *prev,
                                        int current_depth,
                                        VAAPIEncodePicture **last)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic, *next, *ref;
    int i, len;

    av_assert0(start && end && start != end && start->next != end);

    // If we are at the maximum depth then encode all pictures as
    // non-referenced B-pictures.  Also do this if there is exactly one
    // picture left, since there will be nothing to reference it.
    if (current_depth == ctx->max_b_depth || start->next->next == end) {
        for (pic = start->next; pic; pic = pic->next) {
            if (pic == end)
                break;
            pic->type    = PICTURE_TYPE_B;
            pic->b_depth = current_depth;

            vaapi_encode_add_ref(avctx, pic, start, 1, 1, 0);
            vaapi_encode_add_ref(avctx, pic, end,   1, 1, 0);
            vaapi_encode_add_ref(avctx, pic, prev,  0, 0, 1);

            for (ref = end->refs[1]; ref; ref = ref->refs[1])
                vaapi_encode_add_ref(avctx, pic, ref, 0, 1, 0);
        }
        *last = prev;

    } else {
        // Split the current list at the midpoint with a referenced
        // B-picture, then descend into each side separately.
        len = 0;
        for (pic = start->next; pic != end; pic = pic->next)
            ++len;
        for (pic = start->next, i = 1; 2 * i < len; pic = pic->next, i++);

        pic->type    = PICTURE_TYPE_B;
        pic->b_depth = current_depth;

        pic->is_reference = 1;

        vaapi_encode_add_ref(avctx, pic, pic,   0, 1, 0);
        vaapi_encode_add_ref(avctx, pic, start, 1, 1, 0);
        vaapi_encode_add_ref(avctx, pic, end,   1, 1, 0);
        vaapi_encode_add_ref(avctx, pic, prev,  0, 0, 1);

        for (ref = end->refs[1]; ref; ref = ref->refs[1])
            vaapi_encode_add_ref(avctx, pic, ref, 0, 1, 0);

        if (i > 1)
            vaapi_encode_set_b_pictures(avctx, start, pic, pic,
                                        current_depth + 1, &next);
        else
            next = pic;

        vaapi_encode_set_b_pictures(avctx, pic, end, next,
                                    current_depth + 1, last);
    }
}

static int vaapi_encode_pick_next(AVCodecContext *avctx,
                                  VAAPIEncodePicture **pic_out)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic = NULL, *next, *start;
    int i, b_counter, closed_gop_end;

    // If there are any B-frames already queued, the next one to encode
    // is the earliest not-yet-issued frame for which all references are
    // available.
    for (pic = ctx->pic_start; pic; pic = pic->next) {
        if (pic->encode_issued)
            continue;
        if (pic->type != PICTURE_TYPE_B)
            continue;
        for (i = 0; i < pic->nb_refs; i++) {
            if (!pic->refs[i]->encode_issued)
                break;
        }
        if (i == pic->nb_refs)
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
        pic->type = PICTURE_TYPE_IDR;
        ctx->idr_counter = 1;
        ctx->gop_counter = 1;

    } else if (ctx->gop_counter + b_counter >= ctx->gop_size) {
        if (ctx->idr_counter == ctx->gop_per_idr) {
            av_log(avctx, AV_LOG_DEBUG, "Pick new-GOP IDR-picture to "
                   "encode next.\n");
            pic->type = PICTURE_TYPE_IDR;
            ctx->idr_counter = 1;
        } else {
            av_log(avctx, AV_LOG_DEBUG, "Pick new-GOP I-picture to "
                   "encode next.\n");
            pic->type = PICTURE_TYPE_I;
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
        pic->type = PICTURE_TYPE_P;
        av_assert0(start);
        ctx->gop_counter += 1 + b_counter;
    }
    pic->is_reference = 1;
    *pic_out = pic;

    vaapi_encode_add_ref(avctx, pic, pic, 0, 1, 0);
    if (pic->type != PICTURE_TYPE_IDR) {
        vaapi_encode_add_ref(avctx, pic, start,
                             pic->type == PICTURE_TYPE_P,
                             b_counter > 0, 0);
        vaapi_encode_add_ref(avctx, pic, ctx->next_prev, 0, 0, 1);
    }
    if (ctx->next_prev)
        --ctx->next_prev->ref_count[0];

    if (b_counter > 0) {
        vaapi_encode_set_b_pictures(avctx, start, pic, pic, 1,
                                    &ctx->next_prev);
    } else {
        ctx->next_prev = pic;
    }
    ++ctx->next_prev->ref_count[0];
    return 0;
}

static int vaapi_encode_clear_old(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic, *prev, *next;

    av_assert0(ctx->pic_start);

    // Remove direct references once each picture is complete.
    for (pic = ctx->pic_start; pic; pic = pic->next) {
        if (pic->encode_complete && pic->next)
            vaapi_encode_remove_refs(avctx, pic, 0);
    }

    // Remove indirect references once a picture has no direct references.
    for (pic = ctx->pic_start; pic; pic = pic->next) {
        if (pic->encode_complete && pic->ref_count[0] == 0)
            vaapi_encode_remove_refs(avctx, pic, 1);
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
            vaapi_encode_free(avctx, pic);
        } else {
            prev = pic;
        }
    }

    return 0;
}

static int vaapi_encode_check_frame(AVCodecContext *avctx,
                                    const AVFrame *frame)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;

    if ((frame->crop_top  || frame->crop_bottom ||
         frame->crop_left || frame->crop_right) && !ctx->crop_warned) {
        av_log(avctx, AV_LOG_WARNING, "Cropping information on input "
               "frames ignored due to lack of API support.\n");
        ctx->crop_warned = 1;
    }

    if (!ctx->roi_allowed) {
        AVFrameSideData *sd =
            av_frame_get_side_data(frame, AV_FRAME_DATA_REGIONS_OF_INTEREST);

        if (sd && !ctx->roi_warned) {
            av_log(avctx, AV_LOG_WARNING, "ROI side data on input "
                   "frames ignored due to lack of driver support.\n");
            ctx->roi_warned = 1;
        }
    }

    return 0;
}

static int vaapi_encode_send_frame(AVCodecContext *avctx, AVFrame *frame)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic;
    int err;

    if (frame) {
        av_log(avctx, AV_LOG_DEBUG, "Input frame: %ux%u (%"PRId64").\n",
               frame->width, frame->height, frame->pts);

        err = vaapi_encode_check_frame(avctx, frame);
        if (err < 0)
            return err;

        pic = vaapi_encode_alloc(avctx);
        if (!pic)
            return AVERROR(ENOMEM);

        pic->input_image = av_frame_alloc();
        if (!pic->input_image) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        if (ctx->input_order == 0 || frame->pict_type == AV_PICTURE_TYPE_I)
            pic->force_idr = 1;

        pic->input_surface = (VASurfaceID)(uintptr_t)frame->data[3];
        pic->pts = frame->pts;

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

    } else {
        ctx->end_of_stream = 1;

        // Fix timestamps if we hit end-of-stream before the initial decode
        // delay has elapsed.
        if (ctx->input_order < ctx->decode_delay)
            ctx->dts_pts_diff = ctx->pic_end->pts - ctx->first_pts;
    }

    return 0;

fail:
    vaapi_encode_free(avctx, pic);
    return err;
}

int ff_vaapi_encode_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic;
    AVFrame *frame = ctx->frame;
    int err;

    err = ff_encode_get_frame(avctx, frame);
    if (err < 0 && err != AVERROR_EOF)
        return err;

    if (err == AVERROR_EOF)
        frame = NULL;

    err = vaapi_encode_send_frame(avctx, frame);
    if (err < 0)
        return err;

    if (!ctx->pic_start) {
        if (ctx->end_of_stream)
            return AVERROR_EOF;
        else
            return AVERROR(EAGAIN);
    }

    if (ctx->has_sync_buffer_func) {
        pic = NULL;

        if (av_fifo_can_write(ctx->encode_fifo)) {
            err = vaapi_encode_pick_next(avctx, &pic);
            if (!err) {
                av_assert0(pic);
                pic->encode_order = ctx->encode_order +
                    av_fifo_can_read(ctx->encode_fifo);
                err = vaapi_encode_issue(avctx, pic);
                if (err < 0) {
                    av_log(avctx, AV_LOG_ERROR, "Encode failed: %d.\n", err);
                    return err;
                }
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
        pic = NULL;
        err = vaapi_encode_pick_next(avctx, &pic);
        if (err < 0)
            return err;
        av_assert0(pic);

        pic->encode_order = ctx->encode_order++;

        err = vaapi_encode_issue(avctx, pic);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Encode failed: %d.\n", err);
            return err;
        }
    }

    err = vaapi_encode_output(avctx, pic, pkt);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Output failed: %d.\n", err);
        return err;
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
    av_log(avctx, AV_LOG_DEBUG, "Output packet: pts %"PRId64" dts %"PRId64".\n",
           pkt->pts, pkt->dts);

    ctx->output_order = pic->encode_order;
    vaapi_encode_clear_old(avctx);

    return 0;
}


static av_cold void vaapi_encode_add_global_param(AVCodecContext *avctx, int type,
                                                  void *buffer, size_t size)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;

    av_assert0(ctx->nb_global_params < MAX_GLOBAL_PARAMS);

    ctx->global_params_type[ctx->nb_global_params] = type;
    ctx->global_params     [ctx->nb_global_params] = buffer;
    ctx->global_params_size[ctx->nb_global_params] = size;

    ++ctx->nb_global_params;
}

typedef struct VAAPIEncodeRTFormat {
    const char *name;
    unsigned int value;
    int depth;
    int nb_components;
    int log2_chroma_w;
    int log2_chroma_h;
} VAAPIEncodeRTFormat;

static const VAAPIEncodeRTFormat vaapi_encode_rt_formats[] = {
    { "YUV400",    VA_RT_FORMAT_YUV400,        8, 1,      },
    { "YUV420",    VA_RT_FORMAT_YUV420,        8, 3, 1, 1 },
    { "YUV422",    VA_RT_FORMAT_YUV422,        8, 3, 1, 0 },
#if VA_CHECK_VERSION(1, 2, 0)
    { "YUV422_10", VA_RT_FORMAT_YUV422_10,    10, 3, 1, 0 },
#endif
    { "YUV444",    VA_RT_FORMAT_YUV444,        8, 3, 0, 0 },
    { "YUV411",    VA_RT_FORMAT_YUV411,        8, 3, 2, 0 },
#if VA_CHECK_VERSION(0, 38, 1)
    { "YUV420_10", VA_RT_FORMAT_YUV420_10BPP, 10, 3, 1, 1 },
#endif
};

static const VAEntrypoint vaapi_encode_entrypoints_normal[] = {
    VAEntrypointEncSlice,
    VAEntrypointEncPicture,
#if VA_CHECK_VERSION(0, 39, 2)
    VAEntrypointEncSliceLP,
#endif
    0
};
#if VA_CHECK_VERSION(0, 39, 2)
static const VAEntrypoint vaapi_encode_entrypoints_low_power[] = {
    VAEntrypointEncSliceLP,
    0
};
#endif

static av_cold int vaapi_encode_profile_entrypoint(AVCodecContext *avctx)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAProfile    *va_profiles    = NULL;
    VAEntrypoint *va_entrypoints = NULL;
    VAStatus vas;
    const VAEntrypoint *usable_entrypoints;
    const VAAPIEncodeProfile *profile;
    const AVPixFmtDescriptor *desc;
    VAConfigAttrib rt_format_attr;
    const VAAPIEncodeRTFormat *rt_format;
    const char *profile_string, *entrypoint_string;
    int i, j, n, depth, err;


    if (ctx->low_power) {
#if VA_CHECK_VERSION(0, 39, 2)
        usable_entrypoints = vaapi_encode_entrypoints_low_power;
#else
        av_log(avctx, AV_LOG_ERROR, "Low-power encoding is not "
               "supported with this VAAPI version.\n");
        return AVERROR(EINVAL);
#endif
    } else {
        usable_entrypoints = vaapi_encode_entrypoints_normal;
    }

    desc = av_pix_fmt_desc_get(ctx->input_frames->sw_format);
    if (!desc) {
        av_log(avctx, AV_LOG_ERROR, "Invalid input pixfmt (%d).\n",
               ctx->input_frames->sw_format);
        return AVERROR(EINVAL);
    }
    depth = desc->comp[0].depth;
    for (i = 1; i < desc->nb_components; i++) {
        if (desc->comp[i].depth != depth) {
            av_log(avctx, AV_LOG_ERROR, "Invalid input pixfmt (%s).\n",
                   desc->name);
            return AVERROR(EINVAL);
        }
    }
    av_log(avctx, AV_LOG_VERBOSE, "Input surface format is %s.\n",
           desc->name);

    n = vaMaxNumProfiles(ctx->hwctx->display);
    va_profiles = av_malloc_array(n, sizeof(VAProfile));
    if (!va_profiles) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    vas = vaQueryConfigProfiles(ctx->hwctx->display, va_profiles, &n);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query profiles: %d (%s).\n",
               vas, vaErrorStr(vas));
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    av_assert0(ctx->codec->profiles);
    for (i = 0; (ctx->codec->profiles[i].av_profile !=
                 FF_PROFILE_UNKNOWN); i++) {
        profile = &ctx->codec->profiles[i];
        if (depth               != profile->depth ||
            desc->nb_components != profile->nb_components)
            continue;
        if (desc->nb_components > 1 &&
            (desc->log2_chroma_w != profile->log2_chroma_w ||
             desc->log2_chroma_h != profile->log2_chroma_h))
            continue;
        if (avctx->profile != profile->av_profile &&
            avctx->profile != FF_PROFILE_UNKNOWN)
            continue;

#if VA_CHECK_VERSION(1, 0, 0)
        profile_string = vaProfileStr(profile->va_profile);
#else
        profile_string = "(no profile names)";
#endif

        for (j = 0; j < n; j++) {
            if (va_profiles[j] == profile->va_profile)
                break;
        }
        if (j >= n) {
            av_log(avctx, AV_LOG_VERBOSE, "Compatible profile %s (%d) "
                   "is not supported by driver.\n", profile_string,
                   profile->va_profile);
            continue;
        }

        ctx->profile = profile;
        break;
    }
    if (!ctx->profile) {
        av_log(avctx, AV_LOG_ERROR, "No usable encoding profile found.\n");
        err = AVERROR(ENOSYS);
        goto fail;
    }

    avctx->profile  = profile->av_profile;
    ctx->va_profile = profile->va_profile;
    av_log(avctx, AV_LOG_VERBOSE, "Using VAAPI profile %s (%d).\n",
           profile_string, ctx->va_profile);

    n = vaMaxNumEntrypoints(ctx->hwctx->display);
    va_entrypoints = av_malloc_array(n, sizeof(VAEntrypoint));
    if (!va_entrypoints) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    vas = vaQueryConfigEntrypoints(ctx->hwctx->display, ctx->va_profile,
                                   va_entrypoints, &n);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query entrypoints for "
               "profile %s (%d): %d (%s).\n", profile_string,
               ctx->va_profile, vas, vaErrorStr(vas));
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    for (i = 0; i < n; i++) {
        for (j = 0; usable_entrypoints[j]; j++) {
            if (va_entrypoints[i] == usable_entrypoints[j])
                break;
        }
        if (usable_entrypoints[j])
            break;
    }
    if (i >= n) {
        av_log(avctx, AV_LOG_ERROR, "No usable encoding entrypoint found "
               "for profile %s (%d).\n", profile_string, ctx->va_profile);
        err = AVERROR(ENOSYS);
        goto fail;
    }

    ctx->va_entrypoint = va_entrypoints[i];
#if VA_CHECK_VERSION(1, 0, 0)
    entrypoint_string = vaEntrypointStr(ctx->va_entrypoint);
#else
    entrypoint_string = "(no entrypoint names)";
#endif
    av_log(avctx, AV_LOG_VERBOSE, "Using VAAPI entrypoint %s (%d).\n",
           entrypoint_string, ctx->va_entrypoint);

    for (i = 0; i < FF_ARRAY_ELEMS(vaapi_encode_rt_formats); i++) {
        rt_format = &vaapi_encode_rt_formats[i];
        if (rt_format->depth         == depth &&
            rt_format->nb_components == profile->nb_components &&
            rt_format->log2_chroma_w == profile->log2_chroma_w &&
            rt_format->log2_chroma_h == profile->log2_chroma_h)
            break;
    }
    if (i >= FF_ARRAY_ELEMS(vaapi_encode_rt_formats)) {
        av_log(avctx, AV_LOG_ERROR, "No usable render target format "
               "found for profile %s (%d) entrypoint %s (%d).\n",
               profile_string, ctx->va_profile,
               entrypoint_string, ctx->va_entrypoint);
        err = AVERROR(ENOSYS);
        goto fail;
    }

    rt_format_attr = (VAConfigAttrib) { VAConfigAttribRTFormat };
    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile, ctx->va_entrypoint,
                                &rt_format_attr, 1);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query RT format "
               "config attribute: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    if (rt_format_attr.value == VA_ATTRIB_NOT_SUPPORTED) {
        av_log(avctx, AV_LOG_VERBOSE, "RT format config attribute not "
               "supported by driver: assuming surface RT format %s "
               "is valid.\n", rt_format->name);
    } else if (!(rt_format_attr.value & rt_format->value)) {
        av_log(avctx, AV_LOG_ERROR, "Surface RT format %s not supported "
               "by driver for encoding profile %s (%d) entrypoint %s (%d).\n",
               rt_format->name, profile_string, ctx->va_profile,
               entrypoint_string, ctx->va_entrypoint);
        err = AVERROR(ENOSYS);
        goto fail;
    } else {
        av_log(avctx, AV_LOG_VERBOSE, "Using VAAPI render target "
               "format %s (%#x).\n", rt_format->name, rt_format->value);
        ctx->config_attributes[ctx->nb_config_attributes++] =
            (VAConfigAttrib) {
            .type  = VAConfigAttribRTFormat,
            .value = rt_format->value,
        };
    }

    err = 0;
fail:
    av_freep(&va_profiles);
    av_freep(&va_entrypoints);
    return err;
}

static const VAAPIEncodeRCMode vaapi_encode_rc_modes[] = {
    //                                  Bitrate   Quality
    //                                     | Maxrate | HRD/VBV
    { 0 }, //                              |    |    |    |
    { RC_MODE_CQP,  "CQP",  1, VA_RC_CQP,  0,   0,   1,   0 },
    { RC_MODE_CBR,  "CBR",  1, VA_RC_CBR,  1,   0,   0,   1 },
    { RC_MODE_VBR,  "VBR",  1, VA_RC_VBR,  1,   1,   0,   1 },
#if VA_CHECK_VERSION(1, 1, 0)
    { RC_MODE_ICQ,  "ICQ",  1, VA_RC_ICQ,  0,   0,   1,   0 },
#else
    { RC_MODE_ICQ,  "ICQ",  0 },
#endif
#if VA_CHECK_VERSION(1, 3, 0)
    { RC_MODE_QVBR, "QVBR", 1, VA_RC_QVBR, 1,   1,   1,   1 },
    { RC_MODE_AVBR, "AVBR", 0, VA_RC_AVBR, 1,   0,   0,   0 },
#else
    { RC_MODE_QVBR, "QVBR", 0 },
    { RC_MODE_AVBR, "AVBR", 0 },
#endif
};

static av_cold int vaapi_encode_init_rate_control(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    uint32_t supported_va_rc_modes;
    const VAAPIEncodeRCMode *rc_mode;
    int64_t rc_bits_per_second;
    int     rc_target_percentage;
    int     rc_window_size;
    int     rc_quality;
    int64_t hrd_buffer_size;
    int64_t hrd_initial_buffer_fullness;
    int fr_num, fr_den;
    VAConfigAttrib rc_attr = { VAConfigAttribRateControl };
    VAStatus vas;
    char supported_rc_modes_string[64];

    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile, ctx->va_entrypoint,
                                &rc_attr, 1);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query rate control "
               "config attribute: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR_EXTERNAL;
    }
    if (rc_attr.value == VA_ATTRIB_NOT_SUPPORTED) {
        av_log(avctx, AV_LOG_VERBOSE, "Driver does not report any "
               "supported rate control modes: assuming CQP only.\n");
        supported_va_rc_modes = VA_RC_CQP;
        strcpy(supported_rc_modes_string, "unknown");
    } else {
        char *str = supported_rc_modes_string;
        size_t len = sizeof(supported_rc_modes_string);
        int i, first = 1, res;

        supported_va_rc_modes = rc_attr.value;
        for (i = 0; i < FF_ARRAY_ELEMS(vaapi_encode_rc_modes); i++) {
            rc_mode = &vaapi_encode_rc_modes[i];
            if (supported_va_rc_modes & rc_mode->va_mode) {
                res = snprintf(str, len, "%s%s",
                               first ? "" : ", ", rc_mode->name);
                first = 0;
                if (res < 0) {
                    *str = 0;
                    break;
                }
                len -= res;
                str += res;
                if (len == 0)
                    break;
            }
        }

        av_log(avctx, AV_LOG_DEBUG, "Driver supports RC modes %s.\n",
               supported_rc_modes_string);
    }

    // Rate control mode selection:
    // * If the user has set a mode explicitly with the rc_mode option,
    //   use it and fail if it is not available.
    // * If an explicit QP option has been set, use CQP.
    // * If the codec is CQ-only, use CQP.
    // * If the QSCALE avcodec option is set, use CQP.
    // * If bitrate and quality are both set, try QVBR.
    // * If quality is set, try ICQ, then CQP.
    // * If bitrate and maxrate are set and have the same value, try CBR.
    // * If a bitrate is set, try AVBR, then VBR, then CBR.
    // * If no bitrate is set, try ICQ, then CQP.

#define TRY_RC_MODE(mode, fail) do { \
        rc_mode = &vaapi_encode_rc_modes[mode]; \
        if (!(rc_mode->va_mode & supported_va_rc_modes)) { \
            if (fail) { \
                av_log(avctx, AV_LOG_ERROR, "Driver does not support %s " \
                       "RC mode (supported modes: %s).\n", rc_mode->name, \
                       supported_rc_modes_string); \
                return AVERROR(EINVAL); \
            } \
            av_log(avctx, AV_LOG_DEBUG, "Driver does not support %s " \
                   "RC mode.\n", rc_mode->name); \
            rc_mode = NULL; \
        } else { \
            goto rc_mode_found; \
        } \
    } while (0)

    if (ctx->explicit_rc_mode)
        TRY_RC_MODE(ctx->explicit_rc_mode, 1);

    if (ctx->explicit_qp)
        TRY_RC_MODE(RC_MODE_CQP, 1);

    if (ctx->codec->flags & FLAG_CONSTANT_QUALITY_ONLY)
        TRY_RC_MODE(RC_MODE_CQP, 1);

    if (avctx->flags & AV_CODEC_FLAG_QSCALE)
        TRY_RC_MODE(RC_MODE_CQP, 1);

    if (avctx->bit_rate > 0 && avctx->global_quality > 0)
        TRY_RC_MODE(RC_MODE_QVBR, 0);

    if (avctx->global_quality > 0) {
        TRY_RC_MODE(RC_MODE_ICQ, 0);
        TRY_RC_MODE(RC_MODE_CQP, 0);
    }

    if (avctx->bit_rate > 0 && avctx->rc_max_rate == avctx->bit_rate)
        TRY_RC_MODE(RC_MODE_CBR, 0);

    if (avctx->bit_rate > 0) {
        TRY_RC_MODE(RC_MODE_AVBR, 0);
        TRY_RC_MODE(RC_MODE_VBR, 0);
        TRY_RC_MODE(RC_MODE_CBR, 0);
    } else {
        TRY_RC_MODE(RC_MODE_ICQ, 0);
        TRY_RC_MODE(RC_MODE_CQP, 0);
    }

    av_log(avctx, AV_LOG_ERROR, "Driver does not support any "
           "RC mode compatible with selected options "
           "(supported modes: %s).\n", supported_rc_modes_string);
    return AVERROR(EINVAL);

rc_mode_found:
    if (rc_mode->bitrate) {
        if (avctx->bit_rate <= 0) {
            av_log(avctx, AV_LOG_ERROR, "Bitrate must be set for %s "
                   "RC mode.\n", rc_mode->name);
            return AVERROR(EINVAL);
        }

        if (rc_mode->mode == RC_MODE_AVBR) {
            // For maximum confusion AVBR is hacked into the existing API
            // by overloading some of the fields with completely different
            // meanings.

            // Target percentage does not apply in AVBR mode.
            rc_bits_per_second = avctx->bit_rate;

            // Accuracy tolerance range for meeting the specified target
            // bitrate.  It's very unclear how this is actually intended
            // to work - since we do want to get the specified bitrate,
            // set the accuracy to 100% for now.
            rc_target_percentage = 100;

            // Convergence period in frames.  The GOP size reflects the
            // user's intended block size for cutting, so reusing that
            // as the convergence period seems a reasonable default.
            rc_window_size = avctx->gop_size > 0 ? avctx->gop_size : 60;

        } else if (rc_mode->maxrate) {
            if (avctx->rc_max_rate > 0) {
                if (avctx->rc_max_rate < avctx->bit_rate) {
                    av_log(avctx, AV_LOG_ERROR, "Invalid bitrate settings: "
                           "bitrate (%"PRId64") must not be greater than "
                           "maxrate (%"PRId64").\n", avctx->bit_rate,
                           avctx->rc_max_rate);
                    return AVERROR(EINVAL);
                }
                rc_bits_per_second   = avctx->rc_max_rate;
                rc_target_percentage = (avctx->bit_rate * 100) /
                                       avctx->rc_max_rate;
            } else {
                // We only have a target bitrate, but this mode requires
                // that a maximum rate be supplied as well.  Since the
                // user does not want this to be a constraint, arbitrarily
                // pick a maximum rate of double the target rate.
                rc_bits_per_second   = 2 * avctx->bit_rate;
                rc_target_percentage = 50;
            }
        } else {
            if (avctx->rc_max_rate > avctx->bit_rate) {
                av_log(avctx, AV_LOG_WARNING, "Max bitrate is ignored "
                       "in %s RC mode.\n", rc_mode->name);
            }
            rc_bits_per_second   = avctx->bit_rate;
            rc_target_percentage = 100;
        }
    } else {
        rc_bits_per_second   = 0;
        rc_target_percentage = 100;
    }

    if (rc_mode->quality) {
        if (ctx->explicit_qp) {
            rc_quality = ctx->explicit_qp;
        } else if (avctx->global_quality > 0) {
            rc_quality = avctx->global_quality;
        } else {
            rc_quality = ctx->codec->default_quality;
            av_log(avctx, AV_LOG_WARNING, "No quality level set; "
                   "using default (%d).\n", rc_quality);
        }
    } else {
        rc_quality = 0;
    }

    if (rc_mode->hrd) {
        if (avctx->rc_buffer_size)
            hrd_buffer_size = avctx->rc_buffer_size;
        else if (avctx->rc_max_rate > 0)
            hrd_buffer_size = avctx->rc_max_rate;
        else
            hrd_buffer_size = avctx->bit_rate;
        if (avctx->rc_initial_buffer_occupancy) {
            if (avctx->rc_initial_buffer_occupancy > hrd_buffer_size) {
                av_log(avctx, AV_LOG_ERROR, "Invalid RC buffer settings: "
                       "must have initial buffer size (%d) <= "
                       "buffer size (%"PRId64").\n",
                       avctx->rc_initial_buffer_occupancy, hrd_buffer_size);
                return AVERROR(EINVAL);
            }
            hrd_initial_buffer_fullness = avctx->rc_initial_buffer_occupancy;
        } else {
            hrd_initial_buffer_fullness = hrd_buffer_size * 3 / 4;
        }

        rc_window_size = (hrd_buffer_size * 1000) / rc_bits_per_second;
    } else {
        if (avctx->rc_buffer_size || avctx->rc_initial_buffer_occupancy) {
            av_log(avctx, AV_LOG_WARNING, "Buffering settings are ignored "
                   "in %s RC mode.\n", rc_mode->name);
        }

        hrd_buffer_size             = 0;
        hrd_initial_buffer_fullness = 0;

        if (rc_mode->mode != RC_MODE_AVBR) {
            // Already set (with completely different meaning) for AVBR.
            rc_window_size = 1000;
        }
    }

    if (rc_bits_per_second          > UINT32_MAX ||
        hrd_buffer_size             > UINT32_MAX ||
        hrd_initial_buffer_fullness > UINT32_MAX) {
        av_log(avctx, AV_LOG_ERROR, "RC parameters of 2^32 or "
               "greater are not supported by VAAPI.\n");
        return AVERROR(EINVAL);
    }

    ctx->rc_mode     = rc_mode;
    ctx->rc_quality  = rc_quality;
    ctx->va_rc_mode  = rc_mode->va_mode;
    ctx->va_bit_rate = rc_bits_per_second;

    av_log(avctx, AV_LOG_VERBOSE, "RC mode: %s.\n", rc_mode->name);
    if (rc_attr.value == VA_ATTRIB_NOT_SUPPORTED) {
        // This driver does not want the RC mode attribute to be set.
    } else {
        ctx->config_attributes[ctx->nb_config_attributes++] =
            (VAConfigAttrib) {
            .type  = VAConfigAttribRateControl,
            .value = ctx->va_rc_mode,
        };
    }

    if (rc_mode->quality)
        av_log(avctx, AV_LOG_VERBOSE, "RC quality: %d.\n", rc_quality);

    if (rc_mode->va_mode != VA_RC_CQP) {
        if (rc_mode->mode == RC_MODE_AVBR) {
            av_log(avctx, AV_LOG_VERBOSE, "RC target: %"PRId64" bps "
                   "converging in %d frames with %d%% accuracy.\n",
                   rc_bits_per_second, rc_window_size,
                   rc_target_percentage);
        } else if (rc_mode->bitrate) {
            av_log(avctx, AV_LOG_VERBOSE, "RC target: %d%% of "
                   "%"PRId64" bps over %d ms.\n", rc_target_percentage,
                   rc_bits_per_second, rc_window_size);
        }

        ctx->rc_params = (VAEncMiscParameterRateControl) {
            .bits_per_second    = rc_bits_per_second,
            .target_percentage  = rc_target_percentage,
            .window_size        = rc_window_size,
            .initial_qp         = 0,
            .min_qp             = (avctx->qmin > 0 ? avctx->qmin : 0),
            .basic_unit_size    = 0,
#if VA_CHECK_VERSION(1, 1, 0)
            .ICQ_quality_factor = av_clip(rc_quality, 1, 51),
            .max_qp             = (avctx->qmax > 0 ? avctx->qmax : 0),
#endif
#if VA_CHECK_VERSION(1, 3, 0)
            .quality_factor     = rc_quality,
#endif
        };
        vaapi_encode_add_global_param(avctx,
                                      VAEncMiscParameterTypeRateControl,
                                      &ctx->rc_params,
                                      sizeof(ctx->rc_params));
    }

    if (rc_mode->hrd) {
        av_log(avctx, AV_LOG_VERBOSE, "RC buffer: %"PRId64" bits, "
               "initial fullness %"PRId64" bits.\n",
               hrd_buffer_size, hrd_initial_buffer_fullness);

        ctx->hrd_params = (VAEncMiscParameterHRD) {
            .initial_buffer_fullness = hrd_initial_buffer_fullness,
            .buffer_size             = hrd_buffer_size,
        };
        vaapi_encode_add_global_param(avctx,
                                      VAEncMiscParameterTypeHRD,
                                      &ctx->hrd_params,
                                      sizeof(ctx->hrd_params));
    }

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
        av_reduce(&fr_num, &fr_den,
                  avctx->framerate.num, avctx->framerate.den, 65535);
    else
        av_reduce(&fr_num, &fr_den,
                  avctx->time_base.den, avctx->time_base.num, 65535);

    av_log(avctx, AV_LOG_VERBOSE, "RC framerate: %d/%d (%.2f fps).\n",
           fr_num, fr_den, (double)fr_num / fr_den);

    ctx->fr_params = (VAEncMiscParameterFrameRate) {
        .framerate = (unsigned int)fr_den << 16 | fr_num,
    };
#if VA_CHECK_VERSION(0, 40, 0)
    vaapi_encode_add_global_param(avctx,
                                  VAEncMiscParameterTypeFrameRate,
                                  &ctx->fr_params,
                                  sizeof(ctx->fr_params));
#endif

    return 0;
}

static av_cold int vaapi_encode_init_max_frame_size(AVCodecContext *avctx)
{
#if VA_CHECK_VERSION(1, 5, 0)
    VAAPIEncodeContext  *ctx = avctx->priv_data;
    VAConfigAttrib      attr = { VAConfigAttribMaxFrameSize };
    VAStatus vas;

    if (ctx->va_rc_mode == VA_RC_CQP) {
        ctx->max_frame_size = 0;
        av_log(avctx, AV_LOG_ERROR, "Max frame size is invalid in CQP rate "
               "control mode.\n");
        return AVERROR(EINVAL);
    }

    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile,
                                ctx->va_entrypoint,
                                &attr, 1);
    if (vas != VA_STATUS_SUCCESS) {
        ctx->max_frame_size = 0;
        av_log(avctx, AV_LOG_ERROR, "Failed to query max frame size "
               "config attribute: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR_EXTERNAL;
    }

    if (attr.value == VA_ATTRIB_NOT_SUPPORTED) {
        ctx->max_frame_size = 0;
        av_log(avctx, AV_LOG_ERROR, "Max frame size attribute "
               "is not supported.\n");
        return AVERROR(EINVAL);
    } else {
        VAConfigAttribValMaxFrameSize attr_mfs;
        attr_mfs.value = attr.value;
        // Prefer to use VAEncMiscParameterTypeMaxFrameSize for max frame size.
        if (!attr_mfs.bits.max_frame_size && attr_mfs.bits.multiple_pass) {
            ctx->max_frame_size = 0;
            av_log(avctx, AV_LOG_ERROR, "Driver only supports multiple pass "
                   "max frame size which has not been implemented in FFmpeg.\n");
            return AVERROR(EINVAL);
        }

        ctx->mfs_params = (VAEncMiscParameterBufferMaxFrameSize){
            .max_frame_size = ctx->max_frame_size * 8,
        };

        av_log(avctx, AV_LOG_VERBOSE, "Set max frame size: %d bytes.\n",
               ctx->max_frame_size);
    }
#else
    av_log(avctx, AV_LOG_ERROR, "The max frame size option is not supported with "
           "this VAAPI version.\n");
    return AVERROR(EINVAL);
#endif

    return 0;
}

static av_cold int vaapi_encode_init_gop_structure(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAStatus vas;
    VAConfigAttrib attr = { VAConfigAttribEncMaxRefFrames };
    uint32_t ref_l0, ref_l1;
    int prediction_pre_only;

    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile,
                                ctx->va_entrypoint,
                                &attr, 1);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query reference frames "
               "attribute: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR_EXTERNAL;
    }

    if (attr.value == VA_ATTRIB_NOT_SUPPORTED) {
        ref_l0 = ref_l1 = 0;
    } else {
        ref_l0 = attr.value       & 0xffff;
        ref_l1 = attr.value >> 16 & 0xffff;
    }

    ctx->p_to_gpb = 0;
    prediction_pre_only = 0;

#if VA_CHECK_VERSION(1, 9, 0)
    if (!(ctx->codec->flags & FLAG_INTRA_ONLY ||
        avctx->gop_size <= 1)) {
        attr = (VAConfigAttrib) { VAConfigAttribPredictionDirection };
        vas = vaGetConfigAttributes(ctx->hwctx->display,
                                    ctx->va_profile,
                                    ctx->va_entrypoint,
                                    &attr, 1);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_WARNING, "Failed to query prediction direction "
                   "attribute: %d (%s).\n", vas, vaErrorStr(vas));
            return AVERROR_EXTERNAL;
        } else if (attr.value == VA_ATTRIB_NOT_SUPPORTED) {
            av_log(avctx, AV_LOG_VERBOSE, "Driver does not report any additional "
                   "prediction constraints.\n");
        } else {
            if (((ref_l0 > 0 || ref_l1 > 0) && !(attr.value & VA_PREDICTION_DIRECTION_PREVIOUS)) ||
                ((ref_l1 == 0) && (attr.value & (VA_PREDICTION_DIRECTION_FUTURE | VA_PREDICTION_DIRECTION_BI_NOT_EMPTY)))) {
                av_log(avctx, AV_LOG_ERROR, "Driver report incorrect prediction "
                       "direction attribute.\n");
                return AVERROR_EXTERNAL;
            }

            if (!(attr.value & VA_PREDICTION_DIRECTION_FUTURE)) {
                if (ref_l0 > 0 && ref_l1 > 0) {
                    prediction_pre_only = 1;
                    av_log(avctx, AV_LOG_VERBOSE, "Driver only support same reference "
                           "lists for B-frames.\n");
                }
            }

            if (attr.value & VA_PREDICTION_DIRECTION_BI_NOT_EMPTY) {
                if (ref_l0 > 0 && ref_l1 > 0) {
                    ctx->p_to_gpb = 1;
                    av_log(avctx, AV_LOG_VERBOSE, "Driver does not support P-frames, "
                           "replacing them with B-frames.\n");
                }
            }
        }
    }
#endif

    if (ctx->codec->flags & FLAG_INTRA_ONLY ||
        avctx->gop_size <= 1) {
        av_log(avctx, AV_LOG_VERBOSE, "Using intra frames only.\n");
        ctx->gop_size = 1;
    } else if (ref_l0 < 1) {
        av_log(avctx, AV_LOG_ERROR, "Driver does not support any "
               "reference frames.\n");
        return AVERROR(EINVAL);
    } else if (!(ctx->codec->flags & FLAG_B_PICTURES) ||
               ref_l1 < 1 || avctx->max_b_frames < 1 ||
               prediction_pre_only) {
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
        if (ctx->codec->flags & FLAG_B_PICTURE_REFERENCES) {
            ctx->max_b_depth = FFMIN(ctx->desired_b_depth,
                                     av_log2(ctx->b_per_p) + 1);
        } else {
            ctx->max_b_depth = 1;
        }
    }

    if (ctx->codec->flags & FLAG_NON_IDR_KEY_PICTURES) {
        ctx->closed_gop  = !!(avctx->flags & AV_CODEC_FLAG_CLOSED_GOP);
        ctx->gop_per_idr = ctx->idr_interval + 1;
    } else {
        ctx->closed_gop  = 1;
        ctx->gop_per_idr = 1;
    }

    return 0;
}

static av_cold int vaapi_encode_init_row_slice_structure(AVCodecContext *avctx,
                                                         uint32_t slice_structure)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    int req_slices;

    // For fixed-size slices currently we only support whole rows, making
    // rectangular slices.  This could be extended to arbitrary runs of
    // blocks, but since slices tend to be a conformance requirement and
    // most cases (such as broadcast or bluray) want rectangular slices
    // only it would need to be gated behind another option.
    if (avctx->slices > ctx->slice_block_rows) {
        av_log(avctx, AV_LOG_WARNING, "Not enough rows to use "
               "configured number of slices (%d < %d); using "
               "maximum.\n", ctx->slice_block_rows, avctx->slices);
        req_slices = ctx->slice_block_rows;
    } else {
        req_slices = avctx->slices;
    }
    if (slice_structure & VA_ENC_SLICE_STRUCTURE_ARBITRARY_ROWS ||
        slice_structure & VA_ENC_SLICE_STRUCTURE_ARBITRARY_MACROBLOCKS) {
        ctx->nb_slices  = req_slices;
        ctx->slice_size = ctx->slice_block_rows / ctx->nb_slices;
    } else if (slice_structure & VA_ENC_SLICE_STRUCTURE_POWER_OF_TWO_ROWS) {
        int k;
        for (k = 1;; k *= 2) {
            if (2 * k * (req_slices - 1) + 1 >= ctx->slice_block_rows)
                break;
        }
        ctx->nb_slices  = (ctx->slice_block_rows + k - 1) / k;
        ctx->slice_size = k;
#if VA_CHECK_VERSION(1, 0, 0)
    } else if (slice_structure & VA_ENC_SLICE_STRUCTURE_EQUAL_ROWS) {
        ctx->nb_slices  = ctx->slice_block_rows;
        ctx->slice_size = 1;
#endif
    } else {
        av_log(avctx, AV_LOG_ERROR, "Driver does not support any usable "
               "slice structure modes (%#x).\n", slice_structure);
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold int vaapi_encode_init_tile_slice_structure(AVCodecContext *avctx,
                                                          uint32_t slice_structure)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    int i, req_tiles;

    if (!(slice_structure & VA_ENC_SLICE_STRUCTURE_ARBITRARY_MACROBLOCKS ||
         (slice_structure & VA_ENC_SLICE_STRUCTURE_ARBITRARY_ROWS &&
          ctx->tile_cols == 1))) {
        av_log(avctx, AV_LOG_ERROR, "Supported slice structure (%#x) doesn't work for "
               "current tile requirement.\n", slice_structure);
        return AVERROR(EINVAL);
    }

    if (ctx->tile_rows > ctx->slice_block_rows ||
        ctx->tile_cols > ctx->slice_block_cols) {
        av_log(avctx, AV_LOG_WARNING, "Not enough block rows/cols (%d x %d) "
               "for configured number of tile (%d x %d); ",
               ctx->slice_block_rows, ctx->slice_block_cols,
               ctx->tile_rows, ctx->tile_cols);
        ctx->tile_rows = ctx->tile_rows > ctx->slice_block_rows ?
                                          ctx->slice_block_rows : ctx->tile_rows;
        ctx->tile_cols = ctx->tile_cols > ctx->slice_block_cols ?
                                          ctx->slice_block_cols : ctx->tile_cols;
        av_log(avctx, AV_LOG_WARNING, "using allowed maximum (%d x %d).\n",
               ctx->tile_rows, ctx->tile_cols);
    }

    req_tiles = ctx->tile_rows * ctx->tile_cols;

    // Tile slice is not allowed to cross the boundary of a tile due to
    // the constraints of media-driver. Currently we support one slice
    // per tile. This could be extended to multiple slices per tile.
    if (avctx->slices != req_tiles)
        av_log(avctx, AV_LOG_WARNING, "The number of requested slices "
               "mismatches with configured number of tile (%d != %d); "
               "using requested tile number for slice.\n",
               avctx->slices, req_tiles);

    ctx->nb_slices = req_tiles;

    // Default in uniform spacing
    // 6-3, 6-5
    for (i = 0; i < ctx->tile_cols; i++) {
        ctx->col_width[i] = ( i + 1 ) * ctx->slice_block_cols / ctx->tile_cols -
                                    i * ctx->slice_block_cols / ctx->tile_cols;
        ctx->col_bd[i + 1]  = ctx->col_bd[i] + ctx->col_width[i];
    }
    // 6-4, 6-6
    for (i = 0; i < ctx->tile_rows; i++) {
        ctx->row_height[i] = ( i + 1 ) * ctx->slice_block_rows / ctx->tile_rows -
                                     i * ctx->slice_block_rows / ctx->tile_rows;
        ctx->row_bd[i + 1] = ctx->row_bd[i] + ctx->row_height[i];
    }

    av_log(avctx, AV_LOG_VERBOSE, "Encoding pictures with %d x %d tile.\n",
           ctx->tile_rows, ctx->tile_cols);

    return 0;
}

static av_cold int vaapi_encode_init_slice_structure(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAConfigAttrib attr[3] = { { VAConfigAttribEncMaxSlices },
                               { VAConfigAttribEncSliceStructure },
#if VA_CHECK_VERSION(1, 1, 0)
                               { VAConfigAttribEncTileSupport },
#endif
                             };
    VAStatus vas;
    uint32_t max_slices, slice_structure;
    int ret;

    if (!(ctx->codec->flags & FLAG_SLICE_CONTROL)) {
        if (avctx->slices > 0) {
            av_log(avctx, AV_LOG_WARNING, "Multiple slices were requested "
                   "but this codec does not support controlling slices.\n");
        }
        return 0;
    }

    av_assert0(ctx->slice_block_height > 0 && ctx->slice_block_width > 0);

    ctx->slice_block_rows = (avctx->height + ctx->slice_block_height - 1) /
                             ctx->slice_block_height;
    ctx->slice_block_cols = (avctx->width  + ctx->slice_block_width  - 1) /
                             ctx->slice_block_width;

    if (avctx->slices <= 1 && !ctx->tile_rows && !ctx->tile_cols) {
        ctx->nb_slices  = 1;
        ctx->slice_size = ctx->slice_block_rows;
        return 0;
    }

    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile,
                                ctx->va_entrypoint,
                                attr, FF_ARRAY_ELEMS(attr));
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query slice "
               "attributes: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR_EXTERNAL;
    }
    max_slices      = attr[0].value;
    slice_structure = attr[1].value;
    if (max_slices      == VA_ATTRIB_NOT_SUPPORTED ||
        slice_structure == VA_ATTRIB_NOT_SUPPORTED) {
        av_log(avctx, AV_LOG_ERROR, "Driver does not support encoding "
               "pictures as multiple slices.\n.");
        return AVERROR(EINVAL);
    }

    if (ctx->tile_rows && ctx->tile_cols) {
#if VA_CHECK_VERSION(1, 1, 0)
        uint32_t tile_support = attr[2].value;
        if (tile_support == VA_ATTRIB_NOT_SUPPORTED) {
            av_log(avctx, AV_LOG_ERROR, "Driver does not support encoding "
                   "pictures as multiple tiles.\n.");
            return AVERROR(EINVAL);
        }
#else
        av_log(avctx, AV_LOG_ERROR, "Tile encoding option is "
            "not supported with this VAAPI version.\n");
        return AVERROR(EINVAL);
#endif
    }

    if (ctx->tile_rows && ctx->tile_cols)
        ret = vaapi_encode_init_tile_slice_structure(avctx, slice_structure);
    else
        ret = vaapi_encode_init_row_slice_structure(avctx, slice_structure);
    if (ret < 0)
        return ret;

    if (ctx->nb_slices > avctx->slices) {
        av_log(avctx, AV_LOG_WARNING, "Slice count rounded up to "
               "%d (from %d) due to driver constraints on slice "
               "structure.\n", ctx->nb_slices, avctx->slices);
    }
    if (ctx->nb_slices > max_slices) {
        av_log(avctx, AV_LOG_ERROR, "Driver does not support "
               "encoding with %d slices (max %"PRIu32").\n",
               ctx->nb_slices, max_slices);
        return AVERROR(EINVAL);
    }

    av_log(avctx, AV_LOG_VERBOSE, "Encoding pictures with %d slices.\n",
           ctx->nb_slices);
    return 0;
}

static av_cold int vaapi_encode_init_packed_headers(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAStatus vas;
    VAConfigAttrib attr = { VAConfigAttribEncPackedHeaders };

    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile,
                                ctx->va_entrypoint,
                                &attr, 1);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query packed headers "
               "attribute: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR_EXTERNAL;
    }

    if (attr.value == VA_ATTRIB_NOT_SUPPORTED) {
        if (ctx->desired_packed_headers) {
            av_log(avctx, AV_LOG_WARNING, "Driver does not support any "
                   "packed headers (wanted %#x).\n",
                   ctx->desired_packed_headers);
        } else {
            av_log(avctx, AV_LOG_VERBOSE, "Driver does not support any "
                   "packed headers (none wanted).\n");
        }
        ctx->va_packed_headers = 0;
    } else {
        if (ctx->desired_packed_headers & ~attr.value) {
            av_log(avctx, AV_LOG_WARNING, "Driver does not support some "
                   "wanted packed headers (wanted %#x, found %#x).\n",
                   ctx->desired_packed_headers, attr.value);
        } else {
            av_log(avctx, AV_LOG_VERBOSE, "All wanted packed headers "
                   "available (wanted %#x, found %#x).\n",
                   ctx->desired_packed_headers, attr.value);
        }
        ctx->va_packed_headers = ctx->desired_packed_headers & attr.value;
    }

    if (ctx->va_packed_headers) {
        ctx->config_attributes[ctx->nb_config_attributes++] =
            (VAConfigAttrib) {
            .type  = VAConfigAttribEncPackedHeaders,
            .value = ctx->va_packed_headers,
        };
    }

    if ( (ctx->desired_packed_headers & VA_ENC_PACKED_HEADER_SEQUENCE) &&
        !(ctx->va_packed_headers      & VA_ENC_PACKED_HEADER_SEQUENCE) &&
         (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER)) {
        av_log(avctx, AV_LOG_WARNING, "Driver does not support packed "
               "sequence headers, but a global header is requested.\n");
        av_log(avctx, AV_LOG_WARNING, "No global header will be written: "
               "this may result in a stream which is not usable for some "
               "purposes (e.g. not muxable to some containers).\n");
    }

    return 0;
}

static av_cold int vaapi_encode_init_quality(AVCodecContext *avctx)
{
#if VA_CHECK_VERSION(0, 36, 0)
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAStatus vas;
    VAConfigAttrib attr = { VAConfigAttribEncQualityRange };
    int quality = avctx->compression_level;

    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile,
                                ctx->va_entrypoint,
                                &attr, 1);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query quality "
               "config attribute: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR_EXTERNAL;
    }

    if (attr.value == VA_ATTRIB_NOT_SUPPORTED) {
        if (quality != 0) {
            av_log(avctx, AV_LOG_WARNING, "Quality attribute is not "
                   "supported: will use default quality level.\n");
        }
    } else {
        if (quality > attr.value) {
            av_log(avctx, AV_LOG_WARNING, "Invalid quality level: "
                   "valid range is 0-%d, using %d.\n",
                   attr.value, attr.value);
            quality = attr.value;
        }

        ctx->quality_params = (VAEncMiscParameterBufferQualityLevel) {
            .quality_level = quality,
        };
        vaapi_encode_add_global_param(avctx,
                                      VAEncMiscParameterTypeQualityLevel,
                                      &ctx->quality_params,
                                      sizeof(ctx->quality_params));
    }
#else
    av_log(avctx, AV_LOG_WARNING, "The encode quality option is "
           "not supported with this VAAPI version.\n");
#endif

    return 0;
}

static av_cold int vaapi_encode_init_roi(AVCodecContext *avctx)
{
#if VA_CHECK_VERSION(1, 0, 0)
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAStatus vas;
    VAConfigAttrib attr = { VAConfigAttribEncROI };

    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile,
                                ctx->va_entrypoint,
                                &attr, 1);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query ROI "
               "config attribute: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR_EXTERNAL;
    }

    if (attr.value == VA_ATTRIB_NOT_SUPPORTED) {
        ctx->roi_allowed = 0;
    } else {
        VAConfigAttribValEncROI roi = {
            .value = attr.value,
        };

        ctx->roi_max_regions = roi.bits.num_roi_regions;
        ctx->roi_allowed = ctx->roi_max_regions > 0 &&
            (ctx->va_rc_mode == VA_RC_CQP ||
             roi.bits.roi_rc_qp_delta_support);
    }
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
                                                     size_t size)
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

    ctx->va_config  = VA_INVALID_ID;
    ctx->va_context = VA_INVALID_ID;

    /* If you add something that can fail above this av_frame_alloc(),
     * modify ff_vaapi_encode_close() accordingly. */
    ctx->frame = av_frame_alloc();
    if (!ctx->frame) {
        return AVERROR(ENOMEM);
    }

    if (!avctx->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "A hardware frames reference is "
               "required to associate the encoding device.\n");
        return AVERROR(EINVAL);
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

    err = vaapi_encode_profile_entrypoint(avctx);
    if (err < 0)
        goto fail;

    if (ctx->codec->get_encoder_caps) {
        err = ctx->codec->get_encoder_caps(avctx);
        if (err < 0)
            goto fail;
    } else {
        // Assume 16x16 blocks.
        ctx->surface_width  = FFALIGN(avctx->width,  16);
        ctx->surface_height = FFALIGN(avctx->height, 16);
        if (ctx->codec->flags & FLAG_SLICE_CONTROL) {
            ctx->slice_block_width  = 16;
            ctx->slice_block_height = 16;
        }
    }

    err = vaapi_encode_init_rate_control(avctx);
    if (err < 0)
        goto fail;

    err = vaapi_encode_init_gop_structure(avctx);
    if (err < 0)
        goto fail;

    err = vaapi_encode_init_slice_structure(avctx);
    if (err < 0)
        goto fail;

    err = vaapi_encode_init_packed_headers(avctx);
    if (err < 0)
        goto fail;

    err = vaapi_encode_init_roi(avctx);
    if (err < 0)
        goto fail;

    if (avctx->compression_level >= 0) {
        err = vaapi_encode_init_quality(avctx);
        if (err < 0)
            goto fail;
    }

    if (ctx->max_frame_size) {
        err = vaapi_encode_init_max_frame_size(avctx);
        if (err < 0)
            goto fail;
    }

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

    if (ctx->codec->configure) {
        err = ctx->codec->configure(avctx);
        if (err < 0)
            goto fail;
    }

    ctx->output_delay = ctx->b_per_p;
    ctx->decode_delay = ctx->max_b_depth;

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

    if (ctx->va_packed_headers & VA_ENC_PACKED_HEADER_SEQUENCE &&
        ctx->codec->write_sequence_header &&
        avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
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

#if VA_CHECK_VERSION(1, 9, 0)
    // check vaSyncBuffer function
    vas = vaSyncBuffer(ctx->hwctx->display, VA_INVALID_ID, 0);
    if (vas != VA_STATUS_ERROR_UNIMPLEMENTED) {
        ctx->has_sync_buffer_func = 1;
        ctx->encode_fifo = av_fifo_alloc2(ctx->async_depth,
                                          sizeof(VAAPIEncodePicture *),
                                          0);
        if (!ctx->encode_fifo)
            return AVERROR(ENOMEM);
    }
#endif

    return 0;

fail:
    return err;
}

av_cold int ff_vaapi_encode_close(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    VAAPIEncodePicture *pic, *next;

    /* We check ctx->frame to know whether ff_vaapi_encode_init()
     * has been called and va_config/va_context initialized. */
    if (!ctx->frame)
        return 0;

    for (pic = ctx->pic_start; pic; pic = next) {
        next = pic->next;
        vaapi_encode_free(avctx, pic);
    }

    av_buffer_pool_uninit(&ctx->output_buffer_pool);

    if (ctx->va_context != VA_INVALID_ID) {
        vaDestroyContext(ctx->hwctx->display, ctx->va_context);
        ctx->va_context = VA_INVALID_ID;
    }

    if (ctx->va_config != VA_INVALID_ID) {
        vaDestroyConfig(ctx->hwctx->display, ctx->va_config);
        ctx->va_config = VA_INVALID_ID;
    }

    av_frame_free(&ctx->frame);

    av_freep(&ctx->codec_sequence_params);
    av_freep(&ctx->codec_picture_params);
    av_fifo_freep2(&ctx->encode_fifo);

    av_buffer_unref(&ctx->recon_frames_ref);
    av_buffer_unref(&ctx->input_frames_ref);
    av_buffer_unref(&ctx->device_ref);

    return 0;
}
