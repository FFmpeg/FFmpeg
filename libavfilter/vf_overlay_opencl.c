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
#include "framesync.h"
#include "internal.h"
#include "opencl.h"
#include "opencl_source.h"
#include "video.h"

typedef struct OverlayOpenCLContext {
    OpenCLFilterContext ocf;

    int              initialised;
    cl_kernel        kernel;
    cl_command_queue command_queue;

    FFFrameSync      fs;

    int              nb_planes;
    int              x_subsample;
    int              y_subsample;
    int              alpha_separate;

    int              x_position;
    int              y_position;
} OverlayOpenCLContext;

static int overlay_opencl_load(AVFilterContext *avctx,
                               enum AVPixelFormat main_format,
                               enum AVPixelFormat overlay_format)
{
    OverlayOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    const char *source = ff_source_overlay_cl;
    const char *kernel;
    const AVPixFmtDescriptor *main_desc, *overlay_desc;
    int err, i, main_planes, overlay_planes;

    main_desc    = av_pix_fmt_desc_get(main_format);
    overlay_desc = av_pix_fmt_desc_get(overlay_format);

    main_planes = overlay_planes = 0;
    for (i = 0; i < main_desc->nb_components; i++)
        main_planes = FFMAX(main_planes,
                            main_desc->comp[i].plane + 1);
    for (i = 0; i < overlay_desc->nb_components; i++)
        overlay_planes = FFMAX(overlay_planes,
                               overlay_desc->comp[i].plane + 1);

    ctx->nb_planes = main_planes;
    ctx->x_subsample = 1 << main_desc->log2_chroma_w;
    ctx->y_subsample = 1 << main_desc->log2_chroma_h;

    if (ctx->x_position % ctx->x_subsample ||
        ctx->y_position % ctx->y_subsample) {
        av_log(avctx, AV_LOG_WARNING, "Warning: overlay position (%d, %d) "
               "does not match subsampling (%d, %d).\n",
               ctx->x_position, ctx->y_position,
               ctx->x_subsample, ctx->y_subsample);
    }

    if (main_planes == overlay_planes) {
        if (main_desc->nb_components == overlay_desc->nb_components)
            kernel = "overlay_no_alpha";
        else
            kernel = "overlay_internal_alpha";
        ctx->alpha_separate = 0;
    } else {
        kernel = "overlay_external_alpha";
        ctx->alpha_separate = 1;
    }

    av_log(avctx, AV_LOG_DEBUG, "Using kernel %s.\n", kernel);

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

static int overlay_opencl_blend(FFFrameSync *fs)
{
    AVFilterContext    *avctx = fs->parent;
    AVFilterLink     *outlink = avctx->outputs[0];
    OverlayOpenCLContext *ctx = avctx->priv;
    AVFrame *input_main, *input_overlay;
    AVFrame *output;
    cl_mem mem;
    cl_int cle, x, y;
    size_t global_work[2];
    int kernel_arg = 0;
    int err, plane;

    err = ff_framesync_get_frame(fs, 0, &input_main, 0);
    if (err < 0)
        return err;
    err = ff_framesync_get_frame(fs, 1, &input_overlay, 0);
    if (err < 0)
        return err;

    if (!ctx->initialised) {
        AVHWFramesContext *main_fc =
            (AVHWFramesContext*)input_main->hw_frames_ctx->data;
        AVHWFramesContext *overlay_fc =
            (AVHWFramesContext*)input_overlay->hw_frames_ctx->data;

        err = overlay_opencl_load(avctx, main_fc->sw_format,
                                  overlay_fc->sw_format);
        if (err < 0)
            return err;
    }

    output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    for (plane = 0; plane < ctx->nb_planes; plane++) {
        kernel_arg = 0;

        mem = (cl_mem)output->data[plane];
        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_mem, &mem);
        kernel_arg++;

        mem = (cl_mem)input_main->data[plane];
        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_mem, &mem);
        kernel_arg++;

        mem = (cl_mem)input_overlay->data[plane];
        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_mem, &mem);
        kernel_arg++;

        if (ctx->alpha_separate) {
            mem = (cl_mem)input_overlay->data[ctx->nb_planes];
            CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_mem, &mem);
            kernel_arg++;
        }

        x = ctx->x_position / (plane == 0 ? 1 : ctx->x_subsample);
        y = ctx->y_position / (plane == 0 ? 1 : ctx->y_subsample);

        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_int, &x);
        kernel_arg++;
        CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_int, &y);
        kernel_arg++;

        if (ctx->alpha_separate) {
            cl_int alpha_adj_x = plane == 0 ? 1 : ctx->x_subsample;
            cl_int alpha_adj_y = plane == 0 ? 1 : ctx->y_subsample;

            CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_int, &alpha_adj_x);
            kernel_arg++;
            CL_SET_KERNEL_ARG(ctx->kernel, kernel_arg, cl_int, &alpha_adj_y);
            kernel_arg++;
        }

        err = ff_opencl_filter_work_size_from_image(avctx, global_work,
                                                    output, plane, 0);
        if (err < 0)
            goto fail;

        cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel, 2, NULL,
                                     global_work, NULL, 0, NULL, NULL);
        CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue overlay kernel "
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

static int overlay_opencl_config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    OverlayOpenCLContext *ctx = avctx->priv;
    int err;

    err = ff_opencl_filter_config_output(outlink);
    if (err < 0)
        return err;

    err = ff_framesync_init_dualinput(&ctx->fs, avctx);
    if (err < 0)
        return err;

    return ff_framesync_configure(&ctx->fs);
}

static av_cold int overlay_opencl_init(AVFilterContext *avctx)
{
    OverlayOpenCLContext *ctx = avctx->priv;

    ctx->fs.on_event = &overlay_opencl_blend;

    return ff_opencl_filter_init(avctx);
}

static int overlay_opencl_activate(AVFilterContext *avctx)
{
    OverlayOpenCLContext *ctx = avctx->priv;

    return ff_framesync_activate(&ctx->fs);
}

static av_cold void overlay_opencl_uninit(AVFilterContext *avctx)
{
    OverlayOpenCLContext *ctx = avctx->priv;
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

#define OFFSET(x) offsetof(OverlayOpenCLContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption overlay_opencl_options[] = {
    { "x", "Overlay x position",
      OFFSET(x_position), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, .flags = FLAGS },
    { "y", "Overlay y position",
      OFFSET(y_position), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(overlay_opencl);

static const AVFilterPad overlay_opencl_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_opencl_filter_config_input,
    },
    {
        .name         = "overlay",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_opencl_filter_config_input,
    },
};

static const AVFilterPad overlay_opencl_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = &overlay_opencl_config_output,
    },
};

const AVFilter ff_vf_overlay_opencl = {
    .name            = "overlay_opencl",
    .description     = NULL_IF_CONFIG_SMALL("Overlay one video on top of another"),
    .priv_size       = sizeof(OverlayOpenCLContext),
    .priv_class      = &overlay_opencl_class,
    .init            = &overlay_opencl_init,
    .uninit          = &overlay_opencl_uninit,
    .activate        = &overlay_opencl_activate,
    FILTER_INPUTS(overlay_opencl_inputs),
    FILTER_OUTPUTS(overlay_opencl_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_OPENCL),
    .flags_internal  = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};
