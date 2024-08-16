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
#include <float.h>

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "opencl.h"
#include "opencl_source.h"
#include "video.h"
#include "transpose.h"

typedef struct TransposeOpenCLContext {
    OpenCLFilterContext ocf;
    int                   initialised;
    int passthrough;    ///< PassthroughType, landscape passthrough mode enabled
    int dir;            ///< TransposeDir
    cl_kernel             kernel;
    cl_command_queue      command_queue;
} TransposeOpenCLContext;

static int transpose_opencl_init(AVFilterContext *avctx)
{
    TransposeOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    int err;

    err = ff_opencl_filter_load_program(avctx, &ff_source_transpose_cl, 1);
    if (err < 0)
        goto fail;

    ctx->command_queue = clCreateCommandQueue(ctx->ocf.hwctx->context,
                                              ctx->ocf.hwctx->device_id,
                                              0, &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create OpenCL "
                     "command queue %d.\n", cle);

    ctx->kernel = clCreateKernel(ctx->ocf.program, "transpose", &cle);
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

static int transpose_opencl_config_output(AVFilterLink *outlink)
{
    FilterLink       *outl = ff_filter_link(outlink);
    AVFilterContext *avctx = outlink->src;
    TransposeOpenCLContext *s = avctx->priv;
    AVFilterLink *inlink = avctx->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    const AVPixFmtDescriptor *desc_in  = av_pix_fmt_desc_get(inlink->format);
    int ret;

    if ((inlink->w >= inlink->h &&
         s->passthrough == TRANSPOSE_PT_TYPE_LANDSCAPE) ||
        (inlink->w <= inlink->h &&
         s->passthrough == TRANSPOSE_PT_TYPE_PORTRAIT)) {
        if (inl->hw_frames_ctx) {
            outl->hw_frames_ctx = av_buffer_ref(inl->hw_frames_ctx);
            if (!outl->hw_frames_ctx)
                return AVERROR(ENOMEM);
        }
        av_log(avctx, AV_LOG_VERBOSE,
               "w:%d h:%d -> w:%d h:%d (passthrough mode)\n",
               inlink->w, inlink->h, inlink->w, inlink->h);

        return 0;
    } else {
        s->passthrough = TRANSPOSE_PT_TYPE_NONE;
    }

    if (desc_in->log2_chroma_w != desc_in->log2_chroma_h) {
        av_log(avctx, AV_LOG_ERROR, "Input format %s not supported.\n",
               desc_in->name);
        return AVERROR(EINVAL);
    }

    s->ocf.output_width = inlink->h;
    s->ocf.output_height = inlink->w;
    ret = ff_opencl_filter_config_output(outlink);
    if (ret < 0)
        return ret;

    if (inlink->sample_aspect_ratio.num)
        outlink->sample_aspect_ratio = av_div_q((AVRational) { 1, 1 },
                                                inlink->sample_aspect_ratio);
    else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    av_log(avctx, AV_LOG_VERBOSE,
           "w:%d h:%d dir:%d -> w:%d h:%d rotation:%s vflip:%d\n",
           inlink->w, inlink->h, s->dir, outlink->w, outlink->h,
           s->dir == 1 || s->dir == 3 ? "clockwise" : "counterclockwise",
           s->dir == 0 || s->dir == 3);
    return 0;
}

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    TransposeOpenCLContext *s = inlink->dst->priv;

    return s->passthrough ?
        ff_null_get_video_buffer   (inlink, w, h) :
        ff_default_get_video_buffer(inlink, w, h);
}

static int transpose_opencl_filter_frame(AVFilterLink *inlink, AVFrame *input)
{
    AVFilterContext    *avctx = inlink->dst;
    AVFilterLink     *outlink = avctx->outputs[0];
    TransposeOpenCLContext *ctx = avctx->priv;
    AVFrame *output = NULL;
    size_t global_work[2];
    cl_mem src, dst;
    cl_int cle;
    int err, p;

    av_log(ctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input->format),
           input->width, input->height, input->pts);

    if (!input->hw_frames_ctx)
        return AVERROR(EINVAL);

    if (ctx->passthrough)
        return ff_filter_frame(outlink, input);

    output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0)
        goto fail;

    if (input->sample_aspect_ratio.num == 0) {
        output->sample_aspect_ratio = input->sample_aspect_ratio;
    } else {
        output->sample_aspect_ratio.num = input->sample_aspect_ratio.den;
        output->sample_aspect_ratio.den = input->sample_aspect_ratio.num;
    }

    if (!ctx->initialised) {
        err = transpose_opencl_init(avctx);
        if (err < 0)
            goto fail;
    }

    for (p = 0; p < FF_ARRAY_ELEMS(output->data); p++) {
        src = (cl_mem) input->data[p];
        dst = (cl_mem) output->data[p];

        if (!dst)
            break;
        CL_SET_KERNEL_ARG(ctx->kernel, 0, cl_mem, &dst);
        CL_SET_KERNEL_ARG(ctx->kernel, 1, cl_mem, &src);
        CL_SET_KERNEL_ARG(ctx->kernel, 2, cl_int, &ctx->dir);

        err = ff_opencl_filter_work_size_from_image(avctx, global_work, output,
                                                    p, 16);

        cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel, 2, NULL,
                                     global_work, NULL,
                                     0, NULL, NULL);
        CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue kernel: %d.\n", cle);
    }
    cle = clFinish(ctx->command_queue);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to finish command queue: %d.\n", cle);

    av_frame_free(&input);

    av_log(ctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(output->format),
           output->width, output->height, output->pts);

    return ff_filter_frame(outlink, output);

fail:
    clFinish(ctx->command_queue);
    av_frame_free(&input);
    av_frame_free(&output);
    return err;
}

static av_cold void transpose_opencl_uninit(AVFilterContext *avctx)
{
    TransposeOpenCLContext *ctx = avctx->priv;
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

#define OFFSET(x) offsetof(TransposeOpenCLContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption transpose_opencl_options[] = {
    { "dir", "set transpose direction", OFFSET(dir), AV_OPT_TYPE_INT, { .i64 = TRANSPOSE_CCLOCK_FLIP }, 0, 3, FLAGS, .unit = "dir" },
        { "cclock_flip", "rotate counter-clockwise with vertical flip", 0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK_FLIP }, .flags=FLAGS, .unit = "dir" },
        { "clock",       "rotate clockwise",                            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK       }, .flags=FLAGS, .unit = "dir" },
        { "cclock",      "rotate counter-clockwise",                    0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK      }, .flags=FLAGS, .unit = "dir" },
        { "clock_flip",  "rotate clockwise with vertical flip",         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK_FLIP  }, .flags=FLAGS, .unit = "dir" },

    { "passthrough", "do not apply transposition if the input matches the specified geometry",
      OFFSET(passthrough), AV_OPT_TYPE_INT, {.i64=TRANSPOSE_PT_TYPE_NONE},  0, INT_MAX, FLAGS, .unit = "passthrough" },
        { "none",      "always apply transposition",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_NONE},      INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },
        { "portrait",  "preserve portrait geometry",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_PORTRAIT},  INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },
        { "landscape", "preserve landscape geometry",  0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_LANDSCAPE}, INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },

    { NULL }
};

AVFILTER_DEFINE_CLASS(transpose_opencl);

static const AVFilterPad transpose_opencl_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .get_buffer.video = get_video_buffer,
        .filter_frame = &transpose_opencl_filter_frame,
        .config_props = &ff_opencl_filter_config_input,
    },
};

static const AVFilterPad transpose_opencl_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &transpose_opencl_config_output,
    },
};

const AVFilter ff_vf_transpose_opencl = {
    .name           = "transpose_opencl",
    .description    = NULL_IF_CONFIG_SMALL("Transpose input video"),
    .priv_size      = sizeof(TransposeOpenCLContext),
    .priv_class     = &transpose_opencl_class,
    .init           = &ff_opencl_filter_init,
    .uninit         = &transpose_opencl_uninit,
    FILTER_INPUTS(transpose_opencl_inputs),
    FILTER_OUTPUTS(transpose_opencl_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_OPENCL),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};
