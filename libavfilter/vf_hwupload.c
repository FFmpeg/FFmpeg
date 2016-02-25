/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/buffer.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_internal.h"
#include "libavutil/log.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct HWUploadContext {
    const AVClass *class;

    AVBufferRef       *hwdevice_ref;
    AVHWDeviceContext *hwdevice;

    AVBufferRef       *hwframes_ref;
    AVHWFramesContext *hwframes;
} HWUploadContext;

static int hwupload_query_formats(AVFilterContext *avctx)
{
    HWUploadContext *ctx = avctx->priv;
    AVHWFramesConstraints *constraints = NULL;
    const enum AVPixelFormat *input_pix_fmts, *output_pix_fmts;
    AVFilterFormats *input_formats = NULL;
    int err, i;

    if (!avctx->hw_device_ctx) {
        av_log(ctx, AV_LOG_ERROR, "A hardware device reference is required "
               "to upload frames to.\n");
        return AVERROR(EINVAL);
    }

    ctx->hwdevice_ref = av_buffer_ref(avctx->hw_device_ctx);
    if (!ctx->hwdevice_ref)
        return AVERROR(ENOMEM);
    ctx->hwdevice = (AVHWDeviceContext*)ctx->hwdevice_ref->data;

    constraints = av_hwdevice_get_hwframe_constraints(ctx->hwdevice_ref, NULL);
    if (!constraints) {
        err = AVERROR(EINVAL);
        goto fail;
    }

    input_pix_fmts  = constraints->valid_sw_formats;
    output_pix_fmts = constraints->valid_hw_formats;

    input_formats = ff_make_format_list(output_pix_fmts);
    if (!input_formats) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    if (input_pix_fmts) {
        for (i = 0; input_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
            err = ff_add_format(&input_formats, input_pix_fmts[i]);
            if (err < 0) {
                ff_formats_unref(&input_formats);
                goto fail;
            }
        }
    }

    ff_formats_ref(input_formats, &avctx->inputs[0]->out_formats);

    ff_formats_ref(ff_make_format_list(output_pix_fmts),
                   &avctx->outputs[0]->in_formats);

    av_hwframe_constraints_free(&constraints);
    return 0;

fail:
    av_buffer_unref(&ctx->hwdevice_ref);
    av_hwframe_constraints_free(&constraints);
    return err;
}

static int hwupload_config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    AVFilterLink   *inlink = avctx->inputs[0];
    HWUploadContext   *ctx = avctx->priv;
    int err;

    av_buffer_unref(&ctx->hwframes_ref);

    if (inlink->format == outlink->format) {
        // The input is already a hardware format, so we just want to
        // pass through the input frames in their own hardware context.
        if (!inlink->hw_frames_ctx) {
            av_log(ctx, AV_LOG_ERROR, "No input hwframe context.\n");
            return AVERROR(EINVAL);
        }

        outlink->hw_frames_ctx = av_buffer_ref(inlink->hw_frames_ctx);
        if (!outlink->hw_frames_ctx)
            return AVERROR(ENOMEM);

        return 0;
    }

    ctx->hwframes_ref = av_hwframe_ctx_alloc(ctx->hwdevice_ref);
    if (!ctx->hwframes_ref)
        return AVERROR(ENOMEM);

    ctx->hwframes = (AVHWFramesContext*)ctx->hwframes_ref->data;

    av_log(ctx, AV_LOG_DEBUG, "Surface format is %s.\n",
           av_get_pix_fmt_name(inlink->format));

    ctx->hwframes->format    = outlink->format;
    ctx->hwframes->sw_format = inlink->format;
    ctx->hwframes->width     = inlink->w;
    ctx->hwframes->height    = inlink->h;

    err = av_hwframe_ctx_init(ctx->hwframes_ref);
    if (err < 0)
        goto fail;

    outlink->hw_frames_ctx = av_buffer_ref(ctx->hwframes_ref);
    if (!outlink->hw_frames_ctx) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    return 0;

fail:
    av_buffer_unref(&ctx->hwframes_ref);
    return err;
}

static int hwupload_filter_frame(AVFilterLink *link, AVFrame *input)
{
    AVFilterContext *avctx = link->dst;
    AVFilterLink  *outlink = avctx->outputs[0];
    HWUploadContext   *ctx = avctx->priv;
    AVFrame *output = NULL;
    int err;

    if (input->format == outlink->format)
        return ff_filter_frame(outlink, input);

    output = av_frame_alloc();
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = av_hwframe_get_buffer(ctx->hwframes_ref, output, 0);
    if (err < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate frame to upload to.\n");
        goto fail;
    }

    output->width  = input->width;
    output->height = input->height;

    err = av_hwframe_transfer_data(output, input, 0);
    if (err < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to upload frame: %d.\n", err);
        goto fail;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0)
        goto fail;

    av_frame_free(&input);

    return ff_filter_frame(outlink, output);

fail:
    av_frame_free(&input);
    av_frame_free(&output);
    return err;
}

static av_cold void hwupload_uninit(AVFilterContext *avctx)
{
    HWUploadContext *ctx = avctx->priv;

    av_buffer_unref(&ctx->hwframes_ref);
    av_buffer_unref(&ctx->hwdevice_ref);
}

static const AVClass hwupload_class = {
    .class_name = "hwupload",
    .item_name  = av_default_item_name,
    .option     = NULL,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad hwupload_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = hwupload_filter_frame,
    },
    { NULL }
};

static const AVFilterPad hwupload_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = hwupload_config_output,
    },
    { NULL }
};

AVFilter ff_vf_hwupload = {
    .name          = "hwupload",
    .description   = NULL_IF_CONFIG_SMALL("Upload a normal frame to a hardware frame"),
    .uninit        = hwupload_uninit,
    .query_formats = hwupload_query_formats,
    .priv_size     = sizeof(HWUploadContext),
    .priv_class    = &hwupload_class,
    .inputs        = hwupload_inputs,
    .outputs       = hwupload_outputs,
};
