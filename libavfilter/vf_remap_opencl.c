/*
 * Copyright (c) 2022 Paul B Mahol
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

#include "libavutil/colorspace.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "framesync.h"
#include "opencl.h"
#include "opencl_source.h"
#include "video.h"

typedef struct RemapOpenCLContext {
    OpenCLFilterContext ocf;

    int nb_planes;
    int interp;
    uint8_t fill_rgba[4];
    cl_float4 cl_fill_color;

    int              initialised;
    cl_kernel        kernel;
    cl_command_queue command_queue;

    FFFrameSync fs;
} RemapOpenCLContext;

#define OFFSET(x) offsetof(RemapOpenCLContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption remap_opencl_options[] = {
    { "interp", "set interpolation method", OFFSET(interp), AV_OPT_TYPE_INT,   {.i64=1}, 0, 1, FLAGS, .unit = "interp" },
    {  "near",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, .unit = "interp" },
    {  "linear", NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, .unit = "interp" },
    { "fill", "set the color of the unmapped pixels", OFFSET(fill_rgba), AV_OPT_TYPE_COLOR, {.str="black"}, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(remap_opencl);

static av_cold int remap_opencl_init(AVFilterContext *avctx)
{
    return ff_opencl_filter_init(avctx);
}

static const char *kernels[] = { "remap_near", "remap_linear" };

static int remap_opencl_load(AVFilterContext *avctx,
                             enum AVPixelFormat main_format,
                             enum AVPixelFormat xmap_format,
                             enum AVPixelFormat ymap_format)
{
    RemapOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    const char *source = ff_source_remap_cl;
    const char *kernel = kernels[ctx->interp];
    const AVPixFmtDescriptor *main_desc;
    int err, main_planes;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(main_format);
    int is_rgb = !!(desc->flags & AV_PIX_FMT_FLAG_RGB);
    const float scale = 1.f / 255.f;
    uint8_t rgba_map[4];

    ff_fill_rgba_map(rgba_map, main_format);

    if (is_rgb) {
        ctx->cl_fill_color.s[rgba_map[0]] = ctx->fill_rgba[0] * scale;
        ctx->cl_fill_color.s[rgba_map[1]] = ctx->fill_rgba[1] * scale;
        ctx->cl_fill_color.s[rgba_map[2]] = ctx->fill_rgba[2] * scale;
        ctx->cl_fill_color.s[rgba_map[3]] = ctx->fill_rgba[3] * scale;
    } else {
        ctx->cl_fill_color.s[0] = RGB_TO_Y_BT709(ctx->fill_rgba[0], ctx->fill_rgba[1], ctx->fill_rgba[2]) * scale;
        ctx->cl_fill_color.s[1] = RGB_TO_U_BT709(ctx->fill_rgba[0], ctx->fill_rgba[1], ctx->fill_rgba[2], 0) * scale;
        ctx->cl_fill_color.s[2] = RGB_TO_V_BT709(ctx->fill_rgba[0], ctx->fill_rgba[1], ctx->fill_rgba[2], 0) * scale;
        ctx->cl_fill_color.s[3] = ctx->fill_rgba[3] * scale;
    }

    main_desc = av_pix_fmt_desc_get(main_format);

    main_planes = 0;
    for (int i = 0; i < main_desc->nb_components; i++)
        main_planes = FFMAX(main_planes,
                            main_desc->comp[i].plane + 1);

    ctx->nb_planes = main_planes;

    err = ff_opencl_filter_load_program(avctx, &source, 1);
    if (err < 0)
        goto fail;

    ctx->command_queue = clCreateCommandQueue(ctx->ocf.hwctx->context,
                                              ctx->ocf.hwctx->device_id,
                                              0, &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create OpenCL "
                     "command queue %d.\n", cle);

    ctx->kernel = clCreateKernel(ctx->ocf.program, kernel, &cle);
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

static int remap_opencl_process_frame(FFFrameSync *fs)
{
    AVFilterContext *avctx = fs->parent;
    AVFilterLink *outlink = avctx->outputs[0];
    RemapOpenCLContext *ctx = avctx->priv;
    AVFrame *input_main, *input_xmap, *input_ymap;
    AVFrame *output;
    cl_mem mem;
    cl_int cle;
    size_t global_work[2];
    int kernel_arg = 0;
    int err, plane;

    err = ff_framesync_get_frame(fs, 0, &input_main, 0);
    if (err < 0)
        return err;
    err = ff_framesync_get_frame(fs, 1, &input_xmap, 0);
    if (err < 0)
        return err;
    err = ff_framesync_get_frame(fs, 2, &input_ymap, 0);
    if (err < 0)
        return err;

    if (!ctx->initialised) {
        AVHWFramesContext *main_fc =
           (AVHWFramesContext*)input_main->hw_frames_ctx->data;
        AVHWFramesContext *xmap_fc =
            (AVHWFramesContext*)input_xmap->hw_frames_ctx->data;
        AVHWFramesContext *ymap_fc =
            (AVHWFramesContext*)input_ymap->hw_frames_ctx->data;

        err = remap_opencl_load(avctx, main_fc->sw_format,
                                xmap_fc->sw_format,
                                ymap_fc->sw_format);
        if (err < 0)
            return err;
    }

    output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    for (plane = 0; plane < ctx->nb_planes; plane++) {
        cl_float4 cl_fill_color;
        kernel_arg = 0;

        if (ctx->nb_planes == 1)
            cl_fill_color = ctx->cl_fill_color;
        else
            cl_fill_color.s[0] = ctx->cl_fill_color.s[plane];

        mem = (cl_mem)output->data[plane];
        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_mem, &mem);
        kernel_arg++;

        mem = (cl_mem)input_main->data[plane];
        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_mem, &mem);
        kernel_arg++;

        mem = (cl_mem)input_xmap->data[0];
        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_mem, &mem);
        kernel_arg++;

        mem = (cl_mem)input_ymap->data[0];
        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_mem, &mem);
        kernel_arg++;

        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_float4, &cl_fill_color);
        kernel_arg++;

        err = ff_opencl_filter_work_size_from_image(avctx, global_work,
                                                    output, plane, 0);
        if (err < 0)
            goto fail;

        cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel, 2, NULL,
                                     global_work, NULL, 0, NULL, NULL);
        CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue remap kernel "
                         "for plane %d: %d.\n", plane, cle);
    }

    cle = clFinish(ctx->command_queue);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to finish command queue: %d.\n", cle);

    err = av_frame_copy_props(output, input_main);

    av_log(avctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(output->format),
           output->width, output->height, output->pts);

    return ff_filter_frame(outlink, output);

fail:
    av_frame_free(&output);
    return err;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    RemapOpenCLContext *s = ctx->priv;
    AVFilterLink *srclink = ctx->inputs[0];
    AVFilterLink *xlink = ctx->inputs[1];
    AVFilterLink *ylink = ctx->inputs[2];
    FilterLink *il = ff_filter_link(srclink);
    FilterLink *ol = ff_filter_link(outlink);
    FFFrameSyncIn *in;
    int ret;

    if (xlink->w != ylink->w || xlink->h != ylink->h) {
        av_log(ctx, AV_LOG_ERROR, "Second input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "third input link %s parameters (%dx%d)\n",
               ctx->input_pads[1].name, xlink->w, xlink->h,
               ctx->input_pads[2].name, ylink->w, ylink->h);
        return AVERROR(EINVAL);
    }

    outlink->w = xlink->w;
    outlink->h = xlink->h;
    outlink->sample_aspect_ratio = srclink->sample_aspect_ratio;
    ol->frame_rate = il->frame_rate;

    ret = ff_framesync_init(&s->fs, ctx, 3);
    if (ret < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = srclink->time_base;
    in[1].time_base = xlink->time_base;
    in[2].time_base = ylink->time_base;
    in[0].sync   = 2;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_STOP;
    in[1].sync   = 1;
    in[1].before = EXT_NULL;
    in[1].after  = EXT_INFINITY;
    in[2].sync   = 1;
    in[2].before = EXT_NULL;
    in[2].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = remap_opencl_process_frame;

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;
    if (ret < 0)
        return ret;

    s->ocf.output_width  = outlink->w;
    s->ocf.output_height = outlink->h;

    return ff_opencl_filter_config_output(outlink);
}

static int activate(AVFilterContext *ctx)
{
    RemapOpenCLContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void remap_opencl_uninit(AVFilterContext *avctx)
{
    RemapOpenCLContext *ctx = avctx->priv;
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

    ff_framesync_uninit(&ctx->fs);
}

static const AVFilterPad remap_opencl_inputs[] = {
    {
        .name         = "source",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_opencl_filter_config_input,
    },
    {
        .name         = "xmap",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_opencl_filter_config_input,
    },
    {
        .name         = "ymap",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_opencl_filter_config_input,
    },
};

static const AVFilterPad remap_opencl_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_remap_opencl = {
    .name          = "remap_opencl",
    .description   = NULL_IF_CONFIG_SMALL("Remap pixels using OpenCL."),
    .priv_size     = sizeof(RemapOpenCLContext),
    .init          = remap_opencl_init,
    .uninit        = remap_opencl_uninit,
    .activate      = activate,
    FILTER_INPUTS(remap_opencl_inputs),
    FILTER_OUTPUTS(remap_opencl_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_OPENCL),
    .priv_class    = &remap_opencl_class,
    .flags_internal  = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};
