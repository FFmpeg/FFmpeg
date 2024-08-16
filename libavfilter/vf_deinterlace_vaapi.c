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

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"
#include "vaapi_vpp.h"

#define MAX_REFERENCES 8

typedef struct DeintVAAPIContext {
    VAAPIVPPContext vpp_ctx; // must be the first field

    int                mode;
    int                field_rate;
    int                auto_enable;

    VAProcFilterCapDeinterlacing
                       deint_caps[VAProcDeinterlacingCount];
    int             nb_deint_caps;
    VAProcPipelineCaps pipeline_caps;

    int                queue_depth;
    int                queue_count;
    AVFrame           *frame_queue[MAX_REFERENCES];
    int                extra_delay_for_timestamps;

    int                eof;
    int                prev_pts;
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

static void deint_vaapi_pipeline_uninit(AVFilterContext *avctx)
{
    DeintVAAPIContext *ctx   = avctx->priv;
    int i;

    for (i = 0; i < ctx->queue_count; i++)
        av_frame_free(&ctx->frame_queue[i]);
    ctx->queue_count = 0;

    ff_vaapi_vpp_pipeline_uninit(avctx);
}

static int deint_vaapi_build_filter_params(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    DeintVAAPIContext *ctx   = avctx->priv;
    VAStatus vas;
    VAProcFilterParameterBufferDeinterlacing params;
    int i;

    ctx->nb_deint_caps = VAProcDeinterlacingCount;
    vas = vaQueryVideoProcFilterCaps(vpp_ctx->hwctx->display,
                                     vpp_ctx->va_context,
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
            return AVERROR(EINVAL);
        }
    }

    params.type      = VAProcFilterDeinterlacing;
    params.algorithm = ctx->mode;
    params.flags     = 0;

    vas = ff_vaapi_vpp_make_param_buffers(avctx,
                                          VAProcFilterParameterBufferType,
                                          &params,
                                          sizeof(params),
                                          1);
    if (vas)
        return vas;

    vas = vaQueryVideoProcPipelineCaps(vpp_ctx->hwctx->display,
                                       vpp_ctx->va_context,
                                       &vpp_ctx->filter_buffers[0], 1,
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
    FilterLink     *outl = ff_filter_link(outlink);
    AVFilterLink *inlink = outlink->src->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    AVFilterContext *avctx = outlink->src;
    DeintVAAPIContext *ctx = avctx->priv;
    int err;

    err = ff_vaapi_vpp_config_output(outlink);
    if (err < 0)
        return err;
    outlink->time_base  = av_mul_q(inlink->time_base,
                                   (AVRational) { 1, ctx->field_rate });
    outl->frame_rate = av_mul_q(inl->frame_rate,
                                (AVRational) { ctx->field_rate, 1 });

    return 0;
}

static int deint_vaapi_filter_frame(AVFilterLink *inlink, AVFrame *input_frame)
{
    AVFilterContext   *avctx = inlink->dst;
    AVFilterLink    *outlink = avctx->outputs[0];
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    DeintVAAPIContext *ctx   = avctx->priv;
    AVFrame *output_frame    = NULL;
    VASurfaceID input_surface;
    VASurfaceID backward_references[MAX_REFERENCES];
    VASurfaceID forward_references[MAX_REFERENCES];
    VAProcPipelineParameterBuffer params;
    VAProcFilterParameterBufferDeinterlacing *filter_params;
    VAStatus vas;
    void *filter_params_addr = NULL;
    int err, i, field, current_frame_index;

    // NULL frame is used to flush the queue in field mode
    if (input_frame)
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
    if (!input_frame)
        return 0;

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
        output_frame = ff_get_video_buffer(outlink, vpp_ctx->output_width,
                                           vpp_ctx->output_height);
        if (!output_frame) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        err = av_frame_copy_props(output_frame, input_frame);
        if (err < 0)
            goto fail;

        err = ff_vaapi_vpp_init_params(avctx, &params,
                                       input_frame, output_frame);
        if (err < 0)
            goto fail;

        if (!ctx->auto_enable || (input_frame->flags & AV_FRAME_FLAG_INTERLACED)) {
            vas = vaMapBuffer(vpp_ctx->hwctx->display, vpp_ctx->filter_buffers[0],
                              &filter_params_addr);
            if (vas != VA_STATUS_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Failed to map filter parameter "
                       "buffer: %d (%s).\n", vas, vaErrorStr(vas));
                err = AVERROR(EIO);
                goto fail;
            }
            filter_params = filter_params_addr;
            filter_params->flags = 0;
            if (input_frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) {
                filter_params->flags |= field ? VA_DEINTERLACING_BOTTOM_FIELD : 0;
            } else {
                filter_params->flags |= VA_DEINTERLACING_BOTTOM_FIELD_FIRST;
                filter_params->flags |= field ? 0 : VA_DEINTERLACING_BOTTOM_FIELD;
            }
            filter_params_addr = NULL;
            vas = vaUnmapBuffer(vpp_ctx->hwctx->display, vpp_ctx->filter_buffers[0]);
            if (vas != VA_STATUS_SUCCESS)
                av_log(avctx, AV_LOG_ERROR, "Failed to unmap filter parameter "
                       "buffer: %d (%s).\n", vas, vaErrorStr(vas));

            params.filters     = &vpp_ctx->filter_buffers[0];
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

        err = ff_vaapi_vpp_render_picture(avctx, &params, output_frame);
        if (err < 0)
            goto fail;

        if (ctx->field_rate == 2) {
            if (field == 0)
                output_frame->pts = 2 * input_frame->pts;
            else if (ctx->eof)
                output_frame->pts = 3 * input_frame->pts - ctx->prev_pts;
            else
                output_frame->pts = input_frame->pts +
                    ctx->frame_queue[current_frame_index + 1]->pts;
        }
#if FF_API_INTERLACED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        output_frame->interlaced_frame = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        output_frame->flags &= ~AV_FRAME_FLAG_INTERLACED;

        av_log(avctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
               av_get_pix_fmt_name(output_frame->format),
               output_frame->width, output_frame->height, output_frame->pts);

        err = ff_filter_frame(outlink, output_frame);
        if (err < 0)
            break;
    }

    ctx->prev_pts = input_frame->pts;

    return err;

fail:
    if (filter_params_addr)
        vaUnmapBuffer(vpp_ctx->hwctx->display, vpp_ctx->filter_buffers[0]);
    av_frame_free(&output_frame);
    return err;
}

static int deint_vaapi_request_frame(AVFilterLink *link)
{
    AVFilterContext *avctx = link->src;
    DeintVAAPIContext *ctx   = avctx->priv;
    int ret;

    if (ctx->eof)
        return AVERROR_EOF;

    ret = ff_request_frame(link->src->inputs[0]);
    if (ret == AVERROR_EOF && ctx->extra_delay_for_timestamps) {
        ctx->eof = 1;
        deint_vaapi_filter_frame(link->src->inputs[0], NULL);
    } else if (ret < 0)
        return ret;

    return 0;
}

static av_cold int deint_vaapi_init(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;

    ff_vaapi_vpp_ctx_init(avctx);
    vpp_ctx->pipeline_uninit     = deint_vaapi_pipeline_uninit;
    vpp_ctx->build_filter_params = deint_vaapi_build_filter_params;
    vpp_ctx->output_format       = AV_PIX_FMT_NONE;

    return 0;
}

#define OFFSET(x) offsetof(DeintVAAPIContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption deint_vaapi_options[] = {
    { "mode", "Deinterlacing mode",
      OFFSET(mode), AV_OPT_TYPE_INT, { .i64 = VAProcDeinterlacingNone },
      VAProcDeinterlacingNone, VAProcDeinterlacingCount - 1, FLAGS, .unit = "mode" },
    { "default", "Use the highest-numbered (and therefore possibly most advanced) deinterlacing algorithm",
      0, AV_OPT_TYPE_CONST, { .i64 = VAProcDeinterlacingNone }, 0, 0, FLAGS, .unit = "mode" },
    { "bob", "Use the bob deinterlacing algorithm",
      0, AV_OPT_TYPE_CONST, { .i64 = VAProcDeinterlacingBob }, 0, 0, FLAGS, .unit = "mode" },
    { "weave", "Use the weave deinterlacing algorithm",
      0, AV_OPT_TYPE_CONST, { .i64 = VAProcDeinterlacingWeave }, 0, 0, FLAGS,  .unit = "mode" },
    { "motion_adaptive", "Use the motion adaptive deinterlacing algorithm",
      0, AV_OPT_TYPE_CONST, { .i64 = VAProcDeinterlacingMotionAdaptive }, 0, 0, FLAGS, .unit = "mode" },
    { "motion_compensated", "Use the motion compensated deinterlacing algorithm",
      0, AV_OPT_TYPE_CONST, { .i64 = VAProcDeinterlacingMotionCompensated }, 0, 0, FLAGS, .unit = "mode" },

    { "rate", "Generate output at frame rate or field rate",
      OFFSET(field_rate), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, 2, FLAGS, .unit = "rate" },
    { "frame", "Output at frame rate (one frame of output for each field-pair)",
      0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, .unit = "rate" },
    { "field", "Output at field rate (one frame of output for each field)",
      0, AV_OPT_TYPE_CONST, { .i64 = 2 }, 0, 0, FLAGS, .unit = "rate" },

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
        .config_props = &ff_vaapi_vpp_config_input,
    },
};

static const AVFilterPad deint_vaapi_outputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .request_frame  = &deint_vaapi_request_frame,
        .config_props   = &deint_vaapi_config_output,
    },
};

const AVFilter ff_vf_deinterlace_vaapi = {
    .name           = "deinterlace_vaapi",
    .description    = NULL_IF_CONFIG_SMALL("Deinterlacing of VAAPI surfaces"),
    .priv_size      = sizeof(DeintVAAPIContext),
    .init           = &deint_vaapi_init,
    .uninit         = &ff_vaapi_vpp_ctx_uninit,
    FILTER_INPUTS(deint_vaapi_inputs),
    FILTER_OUTPUTS(deint_vaapi_outputs),
    FILTER_QUERY_FUNC(&ff_vaapi_vpp_query_formats),
    .priv_class     = &deint_vaapi_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
