/*
 * Copyright (c) 2018 Dylan Fernando
 * Copyright (c) 2018 Danil Iashchenko
 *
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

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "internal.h"
#include "opencl.h"
#include "opencl_source.h"
#include "video.h"
#include "boxblur.h"

typedef struct AverageBlurOpenCLContext {
    OpenCLFilterContext ocf;

    int              initialised;
    cl_kernel        kernel_horiz;
    cl_kernel        kernel_vert;
    cl_command_queue command_queue;

    int radiusH;
    int radiusV;
    int planes;

    FilterParam luma_param;
    FilterParam chroma_param;
    FilterParam alpha_param;
    int radius[4];
    int power[4];

} AverageBlurOpenCLContext;


static int avgblur_opencl_init(AVFilterContext *avctx)
{
    AverageBlurOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    int err;

    err = ff_opencl_filter_load_program(avctx, &ff_source_avgblur_cl, 1);
    if (err < 0)
        goto fail;

    ctx->command_queue = clCreateCommandQueue(ctx->ocf.hwctx->context,
                                              ctx->ocf.hwctx->device_id,
                                              0, &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create OpenCL "
                     "command queue %d.\n", cle);

    ctx->kernel_horiz = clCreateKernel(ctx->ocf.program,"avgblur_horiz", &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create horizontal "
                     "kernel %d.\n", cle);

    ctx->kernel_vert = clCreateKernel(ctx->ocf.program,"avgblur_vert", &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create vertical "
                     "kernel %d.\n", cle);

    ctx->initialised = 1;
    return 0;

fail:
    if (ctx->command_queue)
        clReleaseCommandQueue(ctx->command_queue);
    if (ctx->kernel_horiz)
        clReleaseKernel(ctx->kernel_horiz);
    if (ctx->kernel_vert)
        clReleaseKernel(ctx->kernel_vert);
    return err;
}


static int avgblur_opencl_make_filter_params(AVFilterLink *inlink)
{
    AVFilterContext    *ctx = inlink->dst;
    AverageBlurOpenCLContext *s = ctx->priv;
    int i;

    if (s->radiusV <= 0) {
        s->radiusV = s->radiusH;
    }

    for (i = 0; i < 4; i++) {
        s->power[i] = 1;
    }
    return 0;
}


static int boxblur_opencl_make_filter_params(AVFilterLink *inlink)
{
    AVFilterContext    *ctx = inlink->dst;
    AverageBlurOpenCLContext *s = ctx->priv;
    int err, i;

    err = ff_boxblur_eval_filter_params(inlink,
                                        &s->luma_param,
                                        &s->chroma_param,
                                        &s->alpha_param);

    if (err != 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to evaluate "
               "filter params: %d.\n", err);
        return err;
    }

    s->radius[Y] = s->luma_param.radius;
    s->radius[U] = s->radius[V] = s->chroma_param.radius;
    s->radius[A] = s->alpha_param.radius;

    s->power[Y] = s->luma_param.power;
    s->power[U] = s->power[V] = s->chroma_param.power;
    s->power[A] = s->alpha_param.power;

    for (i = 0; i < 4; i++) {
        if (s->power[i] == 0) {
            s->power[i] = 1;
            s->radius[i] = 0;
        }
    }

    return 0;
}


static int avgblur_opencl_filter_frame(AVFilterLink *inlink, AVFrame *input)
{
    AVFilterContext    *avctx = inlink->dst;
    AVFilterLink     *outlink = avctx->outputs[0];
    AverageBlurOpenCLContext *ctx = avctx->priv;
    AVFrame *output = NULL;
    AVFrame *intermediate = NULL;
    cl_int cle;
    size_t global_work[2];
    cl_mem src, dst, inter;
    int err, p, radius_x, radius_y, i;

    av_log(ctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input->format),
           input->width, input->height, input->pts);

    if (!input->hw_frames_ctx)
        return AVERROR(EINVAL);

    if (!ctx->initialised) {
        err = avgblur_opencl_init(avctx);
        if (err < 0)
            goto fail;

        if (!strcmp(avctx->filter->name, "avgblur_opencl")) {
            err = avgblur_opencl_make_filter_params(inlink);
            if (err < 0)
                goto fail;
        } else if (!strcmp(avctx->filter->name, "boxblur_opencl")) {
            err = boxblur_opencl_make_filter_params(inlink);
            if (err < 0)
                goto fail;
        }

    }

    output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    intermediate = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!intermediate) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    for (p = 0; p < FF_ARRAY_ELEMS(output->data); p++) {
        src = (cl_mem) input->data[p];
        dst = (cl_mem) output->data[p];
        inter = (cl_mem)intermediate->data[p];

        if (!dst)
            break;

        radius_x = ctx->radiusH;
        radius_y = ctx->radiusV;

        if (!(ctx->planes & (1 << p))) {
            radius_x = 0;
            radius_y = 0;
        }

        for (i = 0; i < ctx->power[p]; i++) {
            CL_SET_KERNEL_ARG(ctx->kernel_horiz, 0, cl_mem, &inter);
            CL_SET_KERNEL_ARG(ctx->kernel_horiz, 1, cl_mem, i == 0 ? &src : &dst);
            if (!strcmp(avctx->filter->name, "avgblur_opencl")) {
                CL_SET_KERNEL_ARG(ctx->kernel_horiz, 2, cl_int, &radius_x);
            } else if (!strcmp(avctx->filter->name, "boxblur_opencl")) {
                CL_SET_KERNEL_ARG(ctx->kernel_horiz, 2, cl_int, &ctx->radius[p]);
            }

            err = ff_opencl_filter_work_size_from_image(avctx, global_work,
                                                        i == 0 ? intermediate : output, p, 0);
            if (err < 0)
                goto fail;

            av_log(avctx, AV_LOG_DEBUG, "Run kernel on plane %d "
                   "(%"SIZE_SPECIFIER"x%"SIZE_SPECIFIER").\n",
                   p, global_work[0], global_work[1]);

            cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel_horiz, 2, NULL,
                                         global_work, NULL,
                                         0, NULL, NULL);
            CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue horizontal "
                             "kernel: %d.\n", cle);

            err = ff_opencl_filter_work_size_from_image(avctx, global_work,
                                                        i == 0 ? output : intermediate, p, 0);

            CL_SET_KERNEL_ARG(ctx->kernel_vert, 0, cl_mem, &dst);
            CL_SET_KERNEL_ARG(ctx->kernel_vert, 1, cl_mem, &inter);

            if (!strcmp(avctx->filter->name, "avgblur_opencl")) {
                CL_SET_KERNEL_ARG(ctx->kernel_vert, 2, cl_int, &radius_y);
            } else if (!strcmp(avctx->filter->name, "boxblur_opencl")) {
                CL_SET_KERNEL_ARG(ctx->kernel_vert, 2, cl_int, &ctx->radius[p]);
            }

            cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel_vert, 2, NULL,
                                         global_work, NULL,
                                         0, NULL, NULL);
            CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue vertical "
                             "kernel: %d.\n", cle);
        }
    }

    cle = clFinish(ctx->command_queue);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to finish command queue: %d.\n", cle);

    err = av_frame_copy_props(output, input);
    if (err < 0)
        goto fail;

    av_frame_free(&input);
    av_frame_free(&intermediate);

    av_log(ctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(output->format),
           output->width, output->height, output->pts);

    return ff_filter_frame(outlink, output);

fail:
    clFinish(ctx->command_queue);
    av_frame_free(&input);
    av_frame_free(&output);
    av_frame_free(&intermediate);
    return err;
}


static av_cold void avgblur_opencl_uninit(AVFilterContext *avctx)
{
    AverageBlurOpenCLContext *ctx = avctx->priv;
    cl_int cle;

    if (ctx->kernel_horiz) {
        cle = clReleaseKernel(ctx->kernel_horiz);
        if (cle != CL_SUCCESS)
            av_log(avctx, AV_LOG_ERROR, "Failed to release "
                   "kernel: %d.\n", cle);
    }

    if (ctx->kernel_vert) {
        cle = clReleaseKernel(ctx->kernel_vert);
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


static const AVFilterPad avgblur_opencl_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &avgblur_opencl_filter_frame,
        .config_props = &ff_opencl_filter_config_input,
    },
};


static const AVFilterPad avgblur_opencl_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_opencl_filter_config_output,
    },
};


#define OFFSET(x) offsetof(AverageBlurOpenCLContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

#if CONFIG_AVGBLUR_OPENCL_FILTER

static const AVOption avgblur_opencl_options[] = {
    { "sizeX",  "set horizontal size",  OFFSET(radiusH), AV_OPT_TYPE_INT, {.i64=1},   1, 1024, FLAGS },
    { "planes", "set planes to filter", OFFSET(planes),  AV_OPT_TYPE_INT, {.i64=0xF}, 0,  0xF, FLAGS },
    { "sizeY",  "set vertical size",    OFFSET(radiusV), AV_OPT_TYPE_INT, {.i64=0},   0, 1024, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(avgblur_opencl);


const AVFilter ff_vf_avgblur_opencl = {
    .name           = "avgblur_opencl",
    .description    = NULL_IF_CONFIG_SMALL("Apply average blur filter"),
    .priv_size      = sizeof(AverageBlurOpenCLContext),
    .priv_class     = &avgblur_opencl_class,
    .init           = &ff_opencl_filter_init,
    .uninit         = &avgblur_opencl_uninit,
    FILTER_INPUTS(avgblur_opencl_inputs),
    FILTER_OUTPUTS(avgblur_opencl_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_OPENCL),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};

#endif /* CONFIG_AVGBLUR_OPENCL_FILTER */


#if CONFIG_BOXBLUR_OPENCL_FILTER

static const AVOption boxblur_opencl_options[] = {
    { "luma_radius", "Radius of the luma blurring box", OFFSET(luma_param.radius_expr), AV_OPT_TYPE_STRING, {.str="2"}, .flags = FLAGS },
    { "lr",          "Radius of the luma blurring box", OFFSET(luma_param.radius_expr), AV_OPT_TYPE_STRING, {.str="2"}, .flags = FLAGS },
    { "luma_power",  "How many times should the boxblur be applied to luma",  OFFSET(luma_param.power), AV_OPT_TYPE_INT, {.i64=2}, 0, INT_MAX, .flags = FLAGS },
    { "lp",          "How many times should the boxblur be applied to luma",  OFFSET(luma_param.power), AV_OPT_TYPE_INT, {.i64=2}, 0, INT_MAX, .flags = FLAGS },

    { "chroma_radius", "Radius of the chroma blurring box", OFFSET(chroma_param.radius_expr), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "cr",            "Radius of the chroma blurring box", OFFSET(chroma_param.radius_expr), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "chroma_power",  "How many times should the boxblur be applied to chroma",  OFFSET(chroma_param.power), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },
    { "cp",            "How many times should the boxblur be applied to chroma",  OFFSET(chroma_param.power), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },

    { "alpha_radius", "Radius of the alpha blurring box", OFFSET(alpha_param.radius_expr), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "ar",           "Radius of the alpha blurring box", OFFSET(alpha_param.radius_expr), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "alpha_power",  "How many times should the boxblur be applied to alpha",  OFFSET(alpha_param.power), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },
    { "ap",           "How many times should the boxblur be applied to alpha",  OFFSET(alpha_param.power), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(boxblur_opencl);

const AVFilter ff_vf_boxblur_opencl = {
    .name           = "boxblur_opencl",
    .description    = NULL_IF_CONFIG_SMALL("Apply boxblur filter to input video"),
    .priv_size      = sizeof(AverageBlurOpenCLContext),
    .priv_class     = &boxblur_opencl_class,
    .init           = &ff_opencl_filter_init,
    .uninit         = &avgblur_opencl_uninit,
    FILTER_INPUTS(avgblur_opencl_inputs),
    FILTER_OUTPUTS(avgblur_opencl_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_OPENCL),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};

#endif /* CONFIG_BOXBLUR_OPENCL_FILTER */
