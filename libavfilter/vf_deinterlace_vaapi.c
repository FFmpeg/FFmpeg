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

#include <string.h>

#include <va/va.h>
#include <va/va_vpp.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vaapi.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define MAX_REFERENCES 8

typedef struct DeintVAAPIContext {
    const AVClass     *class;

    AVVAAPIDeviceContext *hwctx;
    AVBufferRef       *device_ref;

    int                mode;
    int                field_rate;
    int                auto_enable;

    int                valid_ids;
    VAConfigID         va_config;
    VAContextID        va_context;

    AVBufferRef       *input_frames_ref;
    AVHWFramesContext *input_frames;

    AVBufferRef       *output_frames_ref;
    AVHWFramesContext *output_frames;
    int                output_height;
    int                output_width;

    VAProcFilterCapDeinterlacing
                       deint_caps[VAProcDeinterlacingCount];
    int             nb_deint_caps;
    VAProcPipelineCaps pipeline_caps;

    int                queue_depth;
    int                queue_count;
    AVFrame           *frame_queue[MAX_REFERENCES];
    int                extra_delay_for_timestamps;

    VABufferID         filter_buffer;
} DeintVAAPIContext;

static const char *deint_vaapi_mode_name(int mode)
{
    switch (mode) {
#define D(name) case VAProcDeinterlacing ## name: return #name
        D(Bob);
        D(Weave);
        D(MotionAdaptive);
        D(MotionCompensated);
#undef D
    default:
        return "Invalid";
    }
}

static int deint_vaapi_query_formats(AVFilterContext *avctx)
{
    enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_VAAPI, AV_PIX_FMT_NONE,
    };
    int err;

    if ((err = ff_formats_ref(ff_make_format_list(pix_fmts),
                              &avctx->inputs[0]->out_formats)) < 0)
        return err;
    if ((err = ff_formats_ref(ff_make_format_list(pix_fmts),
                              &avctx->outputs[0]->in_formats)) < 0)
        return err;

    return 0;
}

static int deint_vaapi_pipeline_uninit(AVFilterContext *avctx)
{
    DeintVAAPIContext *ctx = avctx->priv;
    int i;

    for (i = 0; i < ctx->queue_count; i++)
        av_frame_free(&ctx->frame_queue[i]);
    ctx->queue_count = 0;

    if (ctx->filter_buffer != VA_INVALID_ID) {
        vaDestroyBuffer(ctx->hwctx->display, ctx->filter_buffer);
        ctx->filter_buffer = VA_INVALID_ID;
    }

    if (ctx->va_context != VA_INVALID_ID) {
        vaDestroyContext(ctx->hwctx->display, ctx->va_context);
        ctx->va_context = VA_INVALID_ID;
    }

    if (ctx->va_config != VA_INVALID_ID) {
        vaDestroyConfig(ctx->hwctx->display, ctx->va_config);
        ctx->va_config = VA_INVALID_ID;
    }

    av_buffer_unref(&ctx->device_ref);
    ctx->hwctx = NULL;

    return 0;
}

static int deint_vaapi_config_input(AVFilterLink *inlink)
{
    AVFilterContext   *avctx = inlink->dst;
    DeintVAAPIContext *ctx = avctx->priv;

    deint_vaapi_pipeline_uninit(avctx);

    if (!inlink->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "A hardware frames reference is "
               "required to associate the processing device.\n");
        return AVERROR(EINVAL);
    }

    ctx->input_frames_ref = av_buffer_ref(inlink->hw_frames_ctx);
    ctx->input_frames = (AVHWFramesContext*)ctx->input_frames_ref->data;

    return 0;
}

static int deint_vaapi_build_filter_params(AVFilterContext *avctx)
{
    DeintVAAPIContext *ctx = avctx->priv;
    VAStatus vas;
    VAProcFilterParameterBufferDeinterlacing params;
    int i;

    ctx->nb_deint_caps = VAProcDeinterlacingCount;
    vas = vaQueryVideoProcFilterCaps(ctx->hwctx->display,
                                     ctx->va_context,
                                     VAProcFilterDeinterlacing,
                                     &ctx->deint_caps,
                                     &ctx->nb_deint_caps);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query deinterlacing "
               "caps: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    if (ctx->mode == VAProcDeinterlacingNone) {
        for (i = 0; i < ctx->nb_deint_caps; i++) {
            if (ctx->deint_caps[i].type > ctx->mode)
                ctx->mode = ctx->deint_caps[i].type;
        }
        av_log(avctx, AV_LOG_VERBOSE, "Picking %d (%s) as default "
               "deinterlacing mode.\n", ctx->mode,
               deint_vaapi_mode_name(ctx->mode));
    } else {
        for (i = 0; i < ctx->nb_deint_caps; i++) {
            if (ctx->deint_caps[i].type == ctx->mode)
                break;
        }
        if (i >= ctx->nb_deint_caps) {
            av_log(avctx, AV_LOG_ERROR, "Deinterlacing mode %d (%s) is "
                   "not supported.\n", ctx->mode,
                   deint_vaapi_mode_name(ctx->mode));
        }
    }

    params.type      = VAProcFilterDeinterlacing;
    params.algorithm = ctx->mode;
    params.flags     = 0;

    av_assert0(ctx->filter_buffer == VA_INVALID_ID);
    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         VAProcFilterParameterBufferType,
                         sizeof(params), 1, &params,
                         &ctx->filter_buffer);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create deinterlace "
               "parameter buffer: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    vas = vaQueryVideoProcPipelineCaps(ctx->hwctx->display,
                                       ctx->va_context,
                                       &ctx->filter_buffer, 1,
                                       &ctx->pipeline_caps);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query pipeline "
               "caps: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    ctx->extra_delay_for_timestamps = ctx->field_rate == 2 &&
        ctx->pipeline_caps.num_backward_references == 0;

    ctx->queue_depth = ctx->pipeline_caps.num_backward_references +
                       ctx->pipeline_caps.num_forward_references +
                       ctx->extra_delay_for_timestamps + 1;
    if (ctx->queue_depth > MAX_REFERENCES) {
        av_log(avctx, AV_LOG_ERROR, "Pipeline requires too many "
               "references (%u forward, %u back).\n",
               ctx->pipeline_caps.num_forward_references,
               ctx->pipeline_caps.num_backward_references);
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int deint_vaapi_config_output(AVFilterLink *outlink)
{
    AVFilterContext    *avctx = outlink->src;
    AVFilterLink      *inlink = avctx->inputs[0];
    DeintVAAPIContext    *ctx = avctx->priv;
    AVVAAPIHWConfig *hwconfig = NULL;
    AVHWFramesConstraints *constraints = NULL;
    AVVAAPIFramesContext *va_frames;
    VAStatus vas;
    int err;

    deint_vaapi_pipeline_uninit(avctx);

    av_assert0(ctx->input_frames);
    ctx->device_ref = av_buffer_ref(ctx->input_frames->device_ref);
    ctx->hwctx = ((AVHWDeviceContext*)ctx->device_ref->data)->hwctx;

    ctx->output_width  = ctx->input_frames->width;
    ctx->output_height = ctx->input_frames->height;

    av_assert0(ctx->va_config == VA_INVALID_ID);
    vas = vaCreateConfig(ctx->hwctx->display, VAProfileNone,
                         VAEntrypointVideoProc, 0, 0, &ctx->va_config);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create processing pipeline "
               "config: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

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

    if (ctx->output_width  < constraints->min_width  ||
        ctx->output_height < constraints->min_height ||
        ctx->output_width  > constraints->max_width  ||
        ctx->output_height > constraints->max_height) {
        av_log(avctx, AV_LOG_ERROR, "Hardware does not support "
               "deinterlacing to size %dx%d "
               "(constraints: width %d-%d height %d-%d).\n",
               ctx->output_width, ctx->output_height,
               constraints->min_width,  constraints->max_width,
               constraints->min_height, constraints->max_height);
        err = AVERROR(EINVAL);
        goto fail;
    }

    ctx->output_frames_ref = av_hwframe_ctx_alloc(ctx->device_ref);
    if (!ctx->output_frames_ref) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create HW frame context "
               "for output.\n");
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->output_frames = (AVHWFramesContext*)ctx->output_frames_ref->data;

    ctx->output_frames->format    = AV_PIX_FMT_VAAPI;
    ctx->output_frames->sw_format = ctx->input_frames->sw_format;
    ctx->output_frames->width     = ctx->output_width;
    ctx->output_frames->height    = ctx->output_height;

    // The number of output frames we need is determined by what follows
    // the filter.  If it's an encoder with complex frame reference
    // structures then this could be very high.
    ctx->output_frames->initial_pool_size = 10;

    err = av_hwframe_ctx_init(ctx->output_frames_ref);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise VAAPI frame "
               "context for output: %d\n", err);
        goto fail;
    }

    va_frames = ctx->output_frames->hwctx;

    av_assert0(ctx->va_context == VA_INVALID_ID);
    vas = vaCreateContext(ctx->hwctx->display, ctx->va_config,
                          ctx->output_width, ctx->output_height, 0,
                          va_frames->surface_ids, va_frames->nb_surfaces,
                          &ctx->va_context);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create processing pipeline "
               "context: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    err = deint_vaapi_build_filter_params(avctx);
    if (err < 0)
        goto fail;

    outlink->w = inlink->w;
    outlink->h = inlink->h;

    outlink->time_base  = av_mul_q(inlink->time_base,
                                   (AVRational) { 1, ctx->field_rate });
    outlink->frame_rate = av_mul_q(inlink->frame_rate,
                                   (AVRational) { ctx->field_rate, 1 });

    outlink->hw_frames_ctx = av_buffer_ref(ctx->output_frames_ref);
    if (!outlink->hw_frames_ctx) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    av_freep(&hwconfig);
    av_hwframe_constraints_free(&constraints);
    return 0;

fail:
    av_buffer_unref(&ctx->output_frames_ref);
    av_freep(&hwconfig);
    av_hwframe_constraints_free(&constraints);
    return err;
}

static int vaapi_proc_colour_standard(enum AVColorSpace av_cs)
{
    switch(av_cs) {
#define CS(av, va) case AVCOL_SPC_ ## av: return VAProcColorStandard ## va;
        CS(BT709,     BT709);
        CS(BT470BG,   BT470BG);
        CS(SMPTE170M, SMPTE170M);
        CS(SMPTE240M, SMPTE240M);
#undef CS
    default:
        return VAProcColorStandardNone;
    }
}

static int deint_vaapi_filter_frame(AVFilterLink *inlink, AVFrame *input_frame)
{
    AVFilterContext   *avctx = inlink->dst;
    AVFilterLink    *outlink = avctx->outputs[0];
    DeintVAAPIContext *ctx = avctx->priv;
    AVFrame *output_frame = NULL;
    VASurfaceID input_surface, output_surface;
    VASurfaceID backward_references[MAX_REFERENCES];
    VASurfaceID forward_references[MAX_REFERENCES];
    VAProcPipelineParameterBuffer params;
    VAProcFilterParameterBufferDeinterlacing *filter_params;
    VARectangle input_region;
    VABufferID params_id;
    VAStatus vas;
    void *filter_params_addr = NULL;
    int err, i, field, current_frame_index;

    av_log(avctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input_frame->format),
           input_frame->width, input_frame->height, input_frame->pts);

    if (ctx->queue_count < ctx->queue_depth) {
        ctx->frame_queue[ctx->queue_count++] = input_frame;
        if (ctx->queue_count < ctx->queue_depth) {
            // Need more reference surfaces before we can continue.
            return 0;
        }
    } else {
        av_frame_free(&ctx->frame_queue[0]);
        for (i = 0; i + 1 < ctx->queue_count; i++)
            ctx->frame_queue[i] = ctx->frame_queue[i + 1];
        ctx->frame_queue[i] = input_frame;
    }

    current_frame_index = ctx->pipeline_caps.num_forward_references;

    input_frame = ctx->frame_queue[current_frame_index];
    input_surface = (VASurfaceID)(uintptr_t)input_frame->data[3];
    for (i = 0; i < ctx->pipeline_caps.num_forward_references; i++)
        forward_references[i] = (VASurfaceID)(uintptr_t)
            ctx->frame_queue[current_frame_index - i - 1]->data[3];
    for (i = 0; i < ctx->pipeline_caps.num_backward_references; i++)
        backward_references[i] = (VASurfaceID)(uintptr_t)
            ctx->frame_queue[current_frame_index + i + 1]->data[3];

    av_log(avctx, AV_LOG_DEBUG, "Using surface %#x for "
           "deinterlace input.\n", input_surface);
    av_log(avctx, AV_LOG_DEBUG, "Backward references:");
    for (i = 0; i < ctx->pipeline_caps.num_backward_references; i++)
        av_log(avctx, AV_LOG_DEBUG, " %#x", backward_references[i]);
    av_log(avctx, AV_LOG_DEBUG, "\n");
    av_log(avctx, AV_LOG_DEBUG, "Forward  references:");
    for (i = 0; i < ctx->pipeline_caps.num_forward_references; i++)
        av_log(avctx, AV_LOG_DEBUG, " %#x", forward_references[i]);
    av_log(avctx, AV_LOG_DEBUG, "\n");

    for (field = 0; field < ctx->field_rate; field++) {
        output_frame = ff_get_video_buffer(outlink, ctx->output_width,
                                           ctx->output_height);
        if (!output_frame) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        output_surface = (VASurfaceID)(uintptr_t)output_frame->data[3];
        av_log(avctx, AV_LOG_DEBUG, "Using surface %#x for "
               "deinterlace output.\n", output_surface);

        memset(&params, 0, sizeof(params));

        input_region = (VARectangle) {
            .x      = 0,
            .y      = 0,
            .width  = input_frame->width,
            .height = input_frame->height,
        };

        params.surface = input_surface;
        params.surface_region = &input_region;
        params.surface_color_standard =
            vaapi_proc_colour_standard(input_frame->colorspace);

        params.output_region = NULL;
        params.output_background_color = 0xff000000;
        params.output_color_standard = params.surface_color_standard;

        params.pipeline_flags = 0;
        params.filter_flags   = VA_FRAME_PICTURE;

        if (!ctx->auto_enable || input_frame->interlaced_frame) {
            vas = vaMapBuffer(ctx->hwctx->display, ctx->filter_buffer,
                              &filter_params_addr);
            if (vas != VA_STATUS_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Failed to map filter parameter "
                       "buffer: %d (%s).\n", vas, vaErrorStr(vas));
                err = AVERROR(EIO);
                goto fail;
            }
            filter_params = filter_params_addr;
            filter_params->flags = 0;
            if (input_frame->top_field_first) {
                filter_params->flags |= field ? VA_DEINTERLACING_BOTTOM_FIELD : 0;
            } else {
                filter_params->flags |= VA_DEINTERLACING_BOTTOM_FIELD_FIRST;
                filter_params->flags |= field ? 0 : VA_DEINTERLACING_BOTTOM_FIELD;
            }
            filter_params_addr = NULL;
            vas = vaUnmapBuffer(ctx->hwctx->display, ctx->filter_buffer);
            if (vas != VA_STATUS_SUCCESS)
                av_log(avctx, AV_LOG_ERROR, "Failed to unmap filter parameter "
                       "buffer: %d (%s).\n", vas, vaErrorStr(vas));

            params.filters     = &ctx->filter_buffer;
            params.num_filters = 1;

            params.forward_references = forward_references;
            params.num_forward_references =
                ctx->pipeline_caps.num_forward_references;
            params.backward_references = backward_references;
            params.num_backward_references =
                ctx->pipeline_caps.num_backward_references;

        } else {
            params.filters     = NULL;
            params.num_filters = 0;
        }

        vas = vaBeginPicture(ctx->hwctx->display,
                             ctx->va_context, output_surface);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to attach new picture: "
                   "%d (%s).\n", vas, vaErrorStr(vas));
            err = AVERROR(EIO);
            goto fail;
        }

        vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                             VAProcPipelineParameterBufferType,
                             sizeof(params), 1, &params, &params_id);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to create parameter buffer: "
                   "%d (%s).\n", vas, vaErrorStr(vas));
            err = AVERROR(EIO);
            goto fail_after_begin;
        }
        av_log(avctx, AV_LOG_DEBUG, "Pipeline parameter buffer is %#x.\n",
               params_id);

        vas = vaRenderPicture(ctx->hwctx->display, ctx->va_context,
                              &params_id, 1);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to render parameter buffer: "
                   "%d (%s).\n", vas, vaErrorStr(vas));
            err = AVERROR(EIO);
            goto fail_after_begin;
        }

        vas = vaEndPicture(ctx->hwctx->display, ctx->va_context);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to start picture processing: "
                   "%d (%s).\n", vas, vaErrorStr(vas));
            err = AVERROR(EIO);
            goto fail_after_render;
        }

        if (CONFIG_VAAPI_1 || ctx->hwctx->driver_quirks &
            AV_VAAPI_DRIVER_QUIRK_RENDER_PARAM_BUFFERS) {
            vas = vaDestroyBuffer(ctx->hwctx->display, params_id);
            if (vas != VA_STATUS_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Failed to free parameter buffer: "
                       "%d (%s).\n", vas, vaErrorStr(vas));
                // And ignore.
            }
        }

        err = av_frame_copy_props(output_frame, input_frame);
        if (err < 0)
            goto fail;

        if (ctx->field_rate == 2) {
            if (field == 0)
                output_frame->pts = 2 * input_frame->pts;
            else
                output_frame->pts = input_frame->pts +
                    ctx->frame_queue[current_frame_index + 1]->pts;
        }
        output_frame->interlaced_frame = 0;

        av_log(avctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
               av_get_pix_fmt_name(output_frame->format),
               output_frame->width, output_frame->height, output_frame->pts);

        err = ff_filter_frame(outlink, output_frame);
        if (err < 0)
            break;
    }

    return err;

fail_after_begin:
    vaRenderPicture(ctx->hwctx->display, ctx->va_context, &params_id, 1);
fail_after_render:
    vaEndPicture(ctx->hwctx->display, ctx->va_context);
fail:
    if (filter_params_addr)
        vaUnmapBuffer(ctx->hwctx->display, ctx->filter_buffer);
    av_frame_free(&output_frame);
    return err;
}

static av_cold int deint_vaapi_init(AVFilterContext *avctx)
{
    DeintVAAPIContext *ctx = avctx->priv;

    ctx->va_config     = VA_INVALID_ID;
    ctx->va_context    = VA_INVALID_ID;
    ctx->filter_buffer = VA_INVALID_ID;
    ctx->valid_ids = 1;

    return 0;
}

static av_cold void deint_vaapi_uninit(AVFilterContext *avctx)
{
    DeintVAAPIContext *ctx = avctx->priv;

    if (ctx->valid_ids)
        deint_vaapi_pipeline_uninit(avctx);

    av_buffer_unref(&ctx->input_frames_ref);
    av_buffer_unref(&ctx->output_frames_ref);
    av_buffer_unref(&ctx->device_ref);
}

#define OFFSET(x) offsetof(DeintVAAPIContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption deint_vaapi_options[] = {
    { "mode", "Deinterlacing mode",
      OFFSET(mode), AV_OPT_TYPE_INT, { .i64 = VAProcDeinterlacingNone },
      VAProcDeinterlacingNone, VAProcDeinterlacingCount - 1, FLAGS, "mode" },
    { "default", "Use the highest-numbered (and therefore possibly most advanced) deinterlacing algorithm",
      0, AV_OPT_TYPE_CONST, { .i64 = VAProcDeinterlacingNone }, 0, 0, FLAGS, "mode" },
    { "bob", "Use the bob deinterlacing algorithm",
      0, AV_OPT_TYPE_CONST, { .i64 = VAProcDeinterlacingBob }, 0, 0, FLAGS, "mode" },
    { "weave", "Use the weave deinterlacing algorithm",
      0, AV_OPT_TYPE_CONST, { .i64 = VAProcDeinterlacingWeave }, 0, 0, FLAGS,  "mode" },
    { "motion_adaptive", "Use the motion adaptive deinterlacing algorithm",
      0, AV_OPT_TYPE_CONST, { .i64 = VAProcDeinterlacingMotionAdaptive }, 0, 0, FLAGS, "mode" },
    { "motion_compensated", "Use the motion compensated deinterlacing algorithm",
      0, AV_OPT_TYPE_CONST, { .i64 = VAProcDeinterlacingMotionCompensated }, 0, 0, FLAGS, "mode" },

    { "rate", "Generate output at frame rate or field rate",
      OFFSET(field_rate), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, 2, FLAGS, "rate" },
    { "frame", "Output at frame rate (one frame of output for each field-pair)",
      0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, "rate" },
    { "field", "Output at field rate (one frame of output for each field)",
      0, AV_OPT_TYPE_CONST, { .i64 = 2 }, 0, 0, FLAGS, "rate" },

    { "auto", "Only deinterlace fields, passing frames through unchanged",
      OFFSET(auto_enable), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS },

    { NULL },
};

static const AVClass deint_vaapi_class = {
    .class_name = "deinterlace_vaapi",
    .item_name  = av_default_item_name,
    .option     = deint_vaapi_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad deint_vaapi_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &deint_vaapi_filter_frame,
        .config_props = &deint_vaapi_config_input,
    },
    { NULL }
};

static const AVFilterPad deint_vaapi_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &deint_vaapi_config_output,
    },
    { NULL }
};

AVFilter ff_vf_deinterlace_vaapi = {
    .name           = "deinterlace_vaapi",
    .description    = NULL_IF_CONFIG_SMALL("Deinterlacing of VAAPI surfaces"),
    .priv_size      = sizeof(DeintVAAPIContext),
    .init           = &deint_vaapi_init,
    .uninit         = &deint_vaapi_uninit,
    .query_formats  = &deint_vaapi_query_formats,
    .inputs         = deint_vaapi_inputs,
    .outputs        = deint_vaapi_outputs,
    .priv_class     = &deint_vaapi_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
