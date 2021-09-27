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

#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "opencl.h"
#include "opencl_source.h"
#include "video.h"

enum XFadeTransitions {
    CUSTOM,
    FADE,
    WIPELEFT,
    WIPERIGHT,
    WIPEUP,
    WIPEDOWN,
    SLIDELEFT,
    SLIDERIGHT,
    SLIDEUP,
    SLIDEDOWN,
    NB_TRANSITIONS,
};

typedef struct XFadeOpenCLContext {
    OpenCLFilterContext ocf;

    int              transition;
    const char      *source_file;
    const char      *kernel_name;
    int64_t          duration;
    int64_t          offset;

    int              initialised;
    cl_kernel        kernel;
    cl_command_queue command_queue;

    int              nb_planes;

    int64_t          duration_pts;
    int64_t          offset_pts;
    int64_t          first_pts;
    int64_t          last_pts;
    int64_t          pts;
    int              xfade_is_over;
    int              need_second;
    int              eof[2];
    AVFrame         *xf[2];
} XFadeOpenCLContext;

static int xfade_opencl_load(AVFilterContext *avctx,
                             enum AVPixelFormat main_format,
                             enum AVPixelFormat xfade_format)
{
    XFadeOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    const AVPixFmtDescriptor *main_desc;
    int err, main_planes;
    const char *kernel_name;

    main_desc = av_pix_fmt_desc_get(main_format);
    if (main_format != xfade_format) {
        av_log(avctx, AV_LOG_ERROR, "Input formats are not same.\n");
        return AVERROR(EINVAL);
    }

    main_planes = 0;
    for (int i = 0; i < main_desc->nb_components; i++)
        main_planes = FFMAX(main_planes,
                            main_desc->comp[i].plane + 1);

    ctx->nb_planes = main_planes;

    if (ctx->transition == CUSTOM) {
        err = ff_opencl_filter_load_program_from_file(avctx, ctx->source_file);
    } else {
        err = ff_opencl_filter_load_program(avctx, &ff_opencl_source_xfade, 1);
    }
    if (err < 0)
        return err;

    ctx->command_queue = clCreateCommandQueue(ctx->ocf.hwctx->context,
                                              ctx->ocf.hwctx->device_id,
                                              0, &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create OpenCL "
                     "command queue %d.\n", cle);

    switch (ctx->transition) {
    case CUSTOM:     kernel_name = ctx->kernel_name; break;
    case FADE:       kernel_name = "fade";           break;
    case WIPELEFT:   kernel_name = "wipeleft";       break;
    case WIPERIGHT:  kernel_name = "wiperight";      break;
    case WIPEUP:     kernel_name = "wipeup";         break;
    case WIPEDOWN:   kernel_name = "wipedown";       break;
    case SLIDELEFT:  kernel_name = "slideleft";      break;
    case SLIDERIGHT: kernel_name = "slideright";     break;
    case SLIDEUP:    kernel_name = "slideup";        break;
    case SLIDEDOWN:  kernel_name = "slidedown";      break;
    default:
        err = AVERROR_BUG;
        goto fail;
    }

    ctx->kernel = clCreateKernel(ctx->ocf.program, kernel_name, &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create kernel %d.\n", cle);

    ctx->initialised = 1;

    return 0;

fail:
    if (ctx->command_queue)
        clReleaseCommandQueue(ctx->command_queue);
    if (ctx->kernel)
        clReleaseKernel(ctx->kernel);
    return err;
}

static int xfade_frame(AVFilterContext *avctx, AVFrame *a, AVFrame *b)
{
    AVFilterLink *outlink = avctx->outputs[0];
    XFadeOpenCLContext *ctx = avctx->priv;
    AVFrame *output;
    cl_int cle;
    cl_float progress = av_clipf(1.f - ((cl_float)(ctx->pts - ctx->first_pts - ctx->offset_pts) / ctx->duration_pts), 0.f, 1.f);
    size_t global_work[2];
    int kernel_arg = 0;
    int err, plane;

    if (!ctx->initialised) {
        AVHWFramesContext *main_fc =
            (AVHWFramesContext*)a->hw_frames_ctx->data;
        AVHWFramesContext *xfade_fc =
            (AVHWFramesContext*)b->hw_frames_ctx->data;

        err = xfade_opencl_load(avctx, main_fc->sw_format,
                                xfade_fc->sw_format);
        if (err < 0)
            return err;
    }

    output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    for (plane = 0; plane < ctx->nb_planes; plane++) {
        cl_mem mem;
        kernel_arg = 0;

        mem = (cl_mem)output->data[plane];
        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_mem, &mem);
        kernel_arg++;

        mem = (cl_mem)ctx->xf[0]->data[plane];
        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_mem, &mem);
        kernel_arg++;

        mem = (cl_mem)ctx->xf[1]->data[plane];
        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_mem, &mem);
        kernel_arg++;

        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_float, &progress);
        kernel_arg++;

        err = ff_opencl_filter_work_size_from_image(avctx, global_work,
                                                    output, plane, 0);
        if (err < 0)
            goto fail;

        cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel, 2, NULL,
                                     global_work, NULL, 0, NULL, NULL);
        CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue xfade kernel "
                         "for plane %d: %d.\n", plane, cle);
    }

    cle = clFinish(ctx->command_queue);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to finish command queue: %d.\n", cle);

    err = av_frame_copy_props(output, ctx->xf[0]);
    if (err < 0)
        goto fail;

    output->pts = ctx->pts;

    return ff_filter_frame(outlink, output);

fail:
    av_frame_free(&output);
    return err;
}

static int xfade_opencl_config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    XFadeOpenCLContext *ctx = avctx->priv;
    AVFilterLink *inlink0 = avctx->inputs[0];
    AVFilterLink *inlink1 = avctx->inputs[1];
    int err;

    err = ff_opencl_filter_config_output(outlink);
    if (err < 0)
        return err;

    if (inlink0->w != inlink1->w || inlink0->h != inlink1->h) {
        av_log(avctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (size %dx%d)\n",
               avctx->input_pads[0].name, inlink0->w, inlink0->h,
               avctx->input_pads[1].name, inlink1->w, inlink1->h);
        return AVERROR(EINVAL);
    }

    if (inlink0->time_base.num != inlink1->time_base.num ||
        inlink0->time_base.den != inlink1->time_base.den) {
        av_log(avctx, AV_LOG_ERROR, "First input link %s timebase "
               "(%d/%d) do not match the corresponding "
               "second input link %s timebase (%d/%d)\n",
               avctx->input_pads[0].name, inlink0->time_base.num, inlink0->time_base.den,
               avctx->input_pads[1].name, inlink1->time_base.num, inlink1->time_base.den);
        return AVERROR(EINVAL);
    }

    ctx->first_pts = ctx->last_pts = ctx->pts = AV_NOPTS_VALUE;

    outlink->time_base = inlink0->time_base;
    outlink->sample_aspect_ratio = inlink0->sample_aspect_ratio;
    outlink->frame_rate = inlink0->frame_rate;

    if (ctx->duration)
        ctx->duration_pts = av_rescale_q(ctx->duration, AV_TIME_BASE_Q, outlink->time_base);
    if (ctx->offset)
        ctx->offset_pts = av_rescale_q(ctx->offset, AV_TIME_BASE_Q, outlink->time_base);

    return 0;
}

static int xfade_opencl_activate(AVFilterContext *avctx)
{
    XFadeOpenCLContext *ctx = avctx->priv;
    AVFilterLink *outlink = avctx->outputs[0];
    AVFrame *in = NULL;
    int ret = 0, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, avctx);

    if (ctx->xfade_is_over) {
        ret = ff_inlink_consume_frame(avctx->inputs[1], &in);
        if (ret < 0) {
            return ret;
        } else if (ret > 0) {
            in->pts = (in->pts - ctx->last_pts) + ctx->pts;
            return ff_filter_frame(outlink, in);
        } else if (ff_inlink_acknowledge_status(avctx->inputs[1], &status, &pts)) {
            ff_outlink_set_status(outlink, status, ctx->pts);
            return 0;
        } else if (!ret) {
            if (ff_outlink_frame_wanted(outlink)) {
                ff_inlink_request_frame(avctx->inputs[1]);
                return 0;
            }
        }
    }

    if (ff_inlink_queued_frames(avctx->inputs[0]) > 0) {
        ctx->xf[0] = ff_inlink_peek_frame(avctx->inputs[0], 0);
        if (ctx->xf[0]) {
            if (ctx->first_pts == AV_NOPTS_VALUE) {
                ctx->first_pts = ctx->xf[0]->pts;
            }
            ctx->pts = ctx->xf[0]->pts;
            if (ctx->first_pts + ctx->offset_pts > ctx->xf[0]->pts) {
                ctx->xf[0] = NULL;
                ctx->need_second = 0;
                ff_inlink_consume_frame(avctx->inputs[0], &in);
                return ff_filter_frame(outlink, in);
            }

            ctx->need_second = 1;
        }
    }

    if (ctx->xf[0] && ff_inlink_queued_frames(avctx->inputs[1]) > 0) {
        ff_inlink_consume_frame(avctx->inputs[0], &ctx->xf[0]);
        ff_inlink_consume_frame(avctx->inputs[1], &ctx->xf[1]);

        ctx->last_pts = ctx->xf[1]->pts;
        ctx->pts = ctx->xf[0]->pts;
        if (ctx->xf[0]->pts - (ctx->first_pts + ctx->offset_pts) > ctx->duration_pts)
            ctx->xfade_is_over = 1;
        ret = xfade_frame(avctx, ctx->xf[0], ctx->xf[1]);
        av_frame_free(&ctx->xf[0]);
        av_frame_free(&ctx->xf[1]);
        return ret;
    }

    if (ff_inlink_queued_frames(avctx->inputs[0]) > 0 &&
        ff_inlink_queued_frames(avctx->inputs[1]) > 0) {
        ff_filter_set_ready(avctx, 100);
        return 0;
    }

    if (ff_outlink_frame_wanted(outlink)) {
        if (!ctx->eof[0] && ff_outlink_get_status(avctx->inputs[0])) {
            ctx->eof[0] = 1;
            ctx->xfade_is_over = 1;
        }
        if (!ctx->eof[1] && ff_outlink_get_status(avctx->inputs[1])) {
            ctx->eof[1] = 1;
        }
        if (!ctx->eof[0] && !ctx->xf[0])
            ff_inlink_request_frame(avctx->inputs[0]);
        if (!ctx->eof[1] && (ctx->need_second || ctx->eof[0]))
            ff_inlink_request_frame(avctx->inputs[1]);
        if (ctx->eof[0] && ctx->eof[1] && (
            ff_inlink_queued_frames(avctx->inputs[0]) <= 0 ||
            ff_inlink_queued_frames(avctx->inputs[1]) <= 0))
            ff_outlink_set_status(outlink, AVERROR_EOF, AV_NOPTS_VALUE);
        return 0;
    }

    return FFERROR_NOT_READY;
}

static av_cold void xfade_opencl_uninit(AVFilterContext *avctx)
{
    XFadeOpenCLContext *ctx = avctx->priv;
    cl_int cle;

    if (ctx->kernel) {
        cle = clReleaseKernel(ctx->kernel);
        if (cle != CL_SUCCESS)
            av_log(avctx, AV_LOG_ERROR, "Failed to release "
                   "kernel: %d.\n", cle);
    }

    if (ctx->command_queue) {
        cle = clReleaseCommandQueue(ctx->command_queue);
        if (cle != CL_SUCCESS)
            av_log(avctx, AV_LOG_ERROR, "Failed to release "
                   "command queue: %d.\n", cle);
    }

    ff_opencl_filter_uninit(avctx);
}

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    XFadeOpenCLContext *s = inlink->dst->priv;

    return s->xfade_is_over || !s->need_second ?
        ff_null_get_video_buffer   (inlink, w, h) :
        ff_default_get_video_buffer(inlink, w, h);
}

#define OFFSET(x) offsetof(XFadeOpenCLContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption xfade_opencl_options[] = {
    { "transition", "set cross fade transition", OFFSET(transition), AV_OPT_TYPE_INT, {.i64=1}, 0, NB_TRANSITIONS-1, FLAGS, "transition" },
    {   "custom",    "custom transition",     0, AV_OPT_TYPE_CONST, {.i64=CUSTOM},    0, 0, FLAGS, "transition" },
    {   "fade",      "fade transition",       0, AV_OPT_TYPE_CONST, {.i64=FADE},      0, 0, FLAGS, "transition" },
    {   "wipeleft",  "wipe left transition",  0, AV_OPT_TYPE_CONST, {.i64=WIPELEFT},  0, 0, FLAGS, "transition" },
    {   "wiperight", "wipe right transition", 0, AV_OPT_TYPE_CONST, {.i64=WIPERIGHT}, 0, 0, FLAGS, "transition" },
    {   "wipeup",    "wipe up transition",    0, AV_OPT_TYPE_CONST, {.i64=WIPEUP},    0, 0, FLAGS, "transition" },
    {   "wipedown",  "wipe down transition",  0, AV_OPT_TYPE_CONST, {.i64=WIPEDOWN},  0, 0, FLAGS, "transition" },
    {   "slideleft",  "slide left transition",  0, AV_OPT_TYPE_CONST, {.i64=SLIDELEFT},  0, 0, FLAGS, "transition" },
    {   "slideright", "slide right transition", 0, AV_OPT_TYPE_CONST, {.i64=SLIDERIGHT}, 0, 0, FLAGS, "transition" },
    {   "slideup",    "slide up transition",    0, AV_OPT_TYPE_CONST, {.i64=SLIDEUP},    0, 0, FLAGS, "transition" },
    {   "slidedown",  "slide down transition",  0, AV_OPT_TYPE_CONST, {.i64=SLIDEDOWN},  0, 0, FLAGS, "transition" },
    { "source", "set OpenCL program source file for custom transition", OFFSET(source_file), AV_OPT_TYPE_STRING, {.str = NULL}, .flags = FLAGS },
    { "kernel", "set kernel name in program file for custom transition", OFFSET(kernel_name), AV_OPT_TYPE_STRING, {.str = NULL}, .flags = FLAGS },
    { "duration", "set cross fade duration", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64=1000000}, 0, 60000000, FLAGS },
    { "offset",   "set cross fade start relative to first input stream", OFFSET(offset), AV_OPT_TYPE_DURATION, {.i64=0}, INT64_MIN, INT64_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(xfade_opencl);

static const AVFilterPad xfade_opencl_inputs[] = {
    {
        .name             = "main",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_buffer.video = get_video_buffer,
        .config_props     = &ff_opencl_filter_config_input,
    },
    {
        .name             = "xfade",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_buffer.video = get_video_buffer,
        .config_props     = &ff_opencl_filter_config_input,
    },
};

static const AVFilterPad xfade_opencl_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = &xfade_opencl_config_output,
    },
};

const AVFilter ff_vf_xfade_opencl = {
    .name            = "xfade_opencl",
    .description     = NULL_IF_CONFIG_SMALL("Cross fade one video with another video."),
    .priv_size       = sizeof(XFadeOpenCLContext),
    .priv_class      = &xfade_opencl_class,
    .init            = &ff_opencl_filter_init,
    .uninit          = &xfade_opencl_uninit,
    .activate        = &xfade_opencl_activate,
    FILTER_INPUTS(xfade_opencl_inputs),
    FILTER_OUTPUTS(xfade_opencl_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_OPENCL),
    .flags_internal  = FF_FILTER_FLAG_HWFRAME_AWARE,
};
