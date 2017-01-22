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

#include "config.h"

#include "libavutil/avassert.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/log.h"

#include "ffmpeg.h"


static AVClass vaapi_class = {
    .class_name = "vaapi",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

#define DEFAULT_SURFACES 20

typedef struct VAAPIDecoderContext {
    const AVClass *class;

    AVBufferRef       *device_ref;
    AVHWDeviceContext *device;
    AVBufferRef       *frames_ref;
    AVHWFramesContext *frames;

    // The output need not have the same format, width and height as the
    // decoded frames - the copy for non-direct-mapped access is actually
    // a whole vpp instance which can do arbitrary scaling and format
    // conversion.
    enum AVPixelFormat output_format;
} VAAPIDecoderContext;


static int vaapi_get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    InputStream *ist = avctx->opaque;
    VAAPIDecoderContext *ctx = ist->hwaccel_ctx;
    int err;

    err = av_hwframe_get_buffer(ctx->frames_ref, frame, 0);
    if (err < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate decoder surface.\n");
    } else {
        av_log(ctx, AV_LOG_DEBUG, "Decoder given surface %#x.\n",
               (unsigned int)(uintptr_t)frame->data[3]);
    }
    return err;
}

static int vaapi_retrieve_data(AVCodecContext *avctx, AVFrame *input)
{
    InputStream *ist = avctx->opaque;
    VAAPIDecoderContext *ctx = ist->hwaccel_ctx;
    AVFrame *output = 0;
    int err;

    av_assert0(input->format == AV_PIX_FMT_VAAPI);

    if (ctx->output_format == AV_PIX_FMT_VAAPI) {
        // Nothing to do.
        return 0;
    }

    av_log(ctx, AV_LOG_DEBUG, "Retrieve data from surface %#x.\n",
           (unsigned int)(uintptr_t)input->data[3]);

    output = av_frame_alloc();
    if (!output)
        return AVERROR(ENOMEM);

    output->format = ctx->output_format;

    err = av_hwframe_transfer_data(output, input, 0);
    if (err < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to transfer data to "
               "output frame: %d.\n", err);
        goto fail;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0) {
        av_frame_unref(output);
        goto fail;
    }

    av_frame_unref(input);
    av_frame_move_ref(input, output);
    av_frame_free(&output);

    return 0;

fail:
    if (output)
        av_frame_free(&output);
    return err;
}

static void vaapi_decode_uninit(AVCodecContext *avctx)
{
    InputStream *ist = avctx->opaque;
    VAAPIDecoderContext *ctx = ist->hwaccel_ctx;

    if (ctx) {
        av_buffer_unref(&ctx->frames_ref);
        av_buffer_unref(&ctx->device_ref);
        av_free(ctx);
    }

    av_buffer_unref(&ist->hw_frames_ctx);

    ist->hwaccel_ctx           = NULL;
    ist->hwaccel_uninit        = NULL;
    ist->hwaccel_get_buffer    = NULL;
    ist->hwaccel_retrieve_data = NULL;
}

int vaapi_decode_init(AVCodecContext *avctx)
{
    InputStream *ist = avctx->opaque;
    VAAPIDecoderContext *ctx;
    int err;
    int loglevel = (ist->hwaccel_id != HWACCEL_VAAPI ? AV_LOG_VERBOSE
                                                     : AV_LOG_ERROR);

    if (ist->hwaccel_ctx)
        vaapi_decode_uninit(avctx);

    // We have -hwaccel without -vaapi_device, so just initialise here with
    // the device passed as -hwaccel_device (if -vaapi_device was passed, it
    // will always have been called before now).
    if (!hw_device_ctx) {
        err = vaapi_device_init(ist->hwaccel_device);
        if (err < 0)
            return err;
    }

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);
    ctx->class = &vaapi_class;
    ist->hwaccel_ctx = ctx;

    ctx->device_ref = av_buffer_ref(hw_device_ctx);
    ctx->device = (AVHWDeviceContext*)ctx->device_ref->data;

    ctx->output_format = ist->hwaccel_output_format;
    avctx->pix_fmt = ctx->output_format;

    ctx->frames_ref = av_hwframe_ctx_alloc(ctx->device_ref);
    if (!ctx->frames_ref) {
        av_log(ctx, loglevel, "Failed to create VAAPI frame context.\n");
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->frames = (AVHWFramesContext*)ctx->frames_ref->data;

    ctx->frames->format = AV_PIX_FMT_VAAPI;
    ctx->frames->width  = avctx->coded_width;
    ctx->frames->height = avctx->coded_height;

    // It would be nice if we could query the available formats here,
    // but unfortunately we don't have a VAConfigID to do it with.
    // For now, just assume an NV12 format (or P010 if 10-bit).
    ctx->frames->sw_format = (avctx->sw_pix_fmt == AV_PIX_FMT_YUV420P10 ?
                              AV_PIX_FMT_P010 : AV_PIX_FMT_NV12);

    // For frame-threaded decoding, at least one additional surface
    // is needed for each thread.
    ctx->frames->initial_pool_size = DEFAULT_SURFACES;
    if (avctx->active_thread_type & FF_THREAD_FRAME)
        ctx->frames->initial_pool_size += avctx->thread_count;

    err = av_hwframe_ctx_init(ctx->frames_ref);
    if (err < 0) {
        av_log(ctx, loglevel, "Failed to initialise VAAPI frame "
               "context: %d\n", err);
        goto fail;
    }

    ist->hw_frames_ctx = av_buffer_ref(ctx->frames_ref);
    if (!ist->hw_frames_ctx) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ist->hwaccel_uninit        = &vaapi_decode_uninit;
    ist->hwaccel_get_buffer    = &vaapi_get_buffer;
    ist->hwaccel_retrieve_data = &vaapi_retrieve_data;

    return 0;

fail:
    vaapi_decode_uninit(avctx);
    return err;
}

static AVClass *vaapi_log = &vaapi_class;

av_cold int vaapi_device_init(const char *device)
{
    int err;

    av_buffer_unref(&hw_device_ctx);

    err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI,
                                 device, NULL, 0);
    if (err < 0) {
        av_log(&vaapi_log, AV_LOG_ERROR, "Failed to create a VAAPI device\n");
        return err;
    }

    return 0;
}
