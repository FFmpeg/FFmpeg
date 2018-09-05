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

#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "framesync.h"
#include "internal.h"
#include "opencl.h"
#include "video.h"

typedef struct ProgramOpenCLContext {
    OpenCLFilterContext ocf;

    int                 loaded;
    cl_uint             index;
    cl_kernel           kernel;
    cl_command_queue    command_queue;

    FFFrameSync         fs;
    AVFrame           **frames;

    const char         *source_file;
    const char         *kernel_name;
    int                 nb_inputs;
    int                 width, height;
    enum AVPixelFormat  source_format;
    AVRational          source_rate;
} ProgramOpenCLContext;

static int program_opencl_load(AVFilterContext *avctx)
{
    ProgramOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    int err;

    err = ff_opencl_filter_load_program_from_file(avctx, ctx->source_file);
    if (err < 0)
        return err;

    ctx->command_queue = clCreateCommandQueue(ctx->ocf.hwctx->context,
                                              ctx->ocf.hwctx->device_id,
                                              0, &cle);
    if (!ctx->command_queue) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create OpenCL "
               "command queue: %d.\n", cle);
        return AVERROR(EIO);
    }

    ctx->kernel = clCreateKernel(ctx->ocf.program, ctx->kernel_name, &cle);
    if (!ctx->kernel) {
        if (cle == CL_INVALID_KERNEL_NAME) {
            av_log(avctx, AV_LOG_ERROR, "Kernel function '%s' not found in "
                   "program.\n", ctx->kernel_name);
        } else {
            av_log(avctx, AV_LOG_ERROR, "Failed to create kernel: %d.\n", cle);
        }
        return AVERROR(EIO);
    }

    ctx->loaded = 1;
    return 0;
}

static int program_opencl_run(AVFilterContext *avctx)
{
    AVFilterLink     *outlink = avctx->outputs[0];
    ProgramOpenCLContext *ctx = avctx->priv;
    AVFrame *output = NULL;
    cl_int cle;
    size_t global_work[2];
    cl_mem src, dst;
    int err, input, plane;

    if (!ctx->loaded) {
        err = program_opencl_load(avctx);
        if (err < 0)
            return err;
    }

    output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    for (plane = 0; plane < FF_ARRAY_ELEMS(output->data); plane++) {
        dst = (cl_mem)output->data[plane];
        if (!dst)
            break;

        cle = clSetKernelArg(ctx->kernel, 0, sizeof(cl_mem), &dst);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "destination image argument: %d.\n", cle);
            err = AVERROR_UNKNOWN;
            goto fail;
        }
        cle = clSetKernelArg(ctx->kernel, 1, sizeof(cl_uint), &ctx->index);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "index argument: %d.\n", cle);
            err = AVERROR_UNKNOWN;
            goto fail;
        }

        for (input = 0; input < ctx->nb_inputs; input++) {
            av_assert0(ctx->frames[input]);

            src = (cl_mem)ctx->frames[input]->data[plane];
            av_assert0(src);

            cle = clSetKernelArg(ctx->kernel, 2 + input, sizeof(cl_mem), &src);
            if (cle != CL_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                       "source image argument %d: %d.\n", input, cle);
                err = AVERROR_UNKNOWN;
                goto fail;
            }
        }

        err = ff_opencl_filter_work_size_from_image(avctx, global_work,
                                                    output, plane, 0);
        if (err < 0)
            goto fail;

        av_log(avctx, AV_LOG_DEBUG, "Run kernel on plane %d "
               "(%zux%zu).\n", plane, global_work[0], global_work[1]);

        cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel, 2, NULL,
                                     global_work, NULL, 0, NULL, NULL);
        CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue kernel: %d.\n", cle);
    }

    cle = clFinish(ctx->command_queue);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to finish command queue: %d.\n", cle);

    if (ctx->nb_inputs > 0) {
        err = av_frame_copy_props(output, ctx->frames[0]);
        if (err < 0)
            goto fail;
    } else {
        output->pts = ctx->index;
    }
    ++ctx->index;

    av_log(ctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(output->format),
           output->width, output->height, output->pts);

    return ff_filter_frame(outlink, output);

fail:
    clFinish(ctx->command_queue);
    av_frame_free(&output);
    return err;
}

static int program_opencl_request_frame(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;

    return program_opencl_run(avctx);
}

static int program_opencl_filter(FFFrameSync *fs)
{
    AVFilterContext    *avctx = fs->parent;
    ProgramOpenCLContext *ctx = avctx->priv;
    int err, i;

    for (i = 0; i < ctx->nb_inputs; i++) {
        err = ff_framesync_get_frame(&ctx->fs, i, &ctx->frames[i], 0);
        if (err < 0)
            return err;
    }

    return program_opencl_run(avctx);
}

static int program_opencl_activate(AVFilterContext *avctx)
{
    ProgramOpenCLContext *ctx = avctx->priv;

    av_assert0(ctx->nb_inputs > 0);

    return ff_framesync_activate(&ctx->fs);
}

static int program_opencl_config_output(AVFilterLink *outlink)
{
    AVFilterContext    *avctx = outlink->src;
    ProgramOpenCLContext *ctx = avctx->priv;
    int err;

    err = ff_opencl_filter_config_output(outlink);
    if (err < 0)
        return err;

    if (ctx->nb_inputs > 0) {
        FFFrameSyncIn *in;
        int i;

        err = ff_framesync_init(&ctx->fs, avctx, ctx->nb_inputs);
        if (err < 0)
            return err;

        ctx->fs.opaque = ctx;
        ctx->fs.on_event = &program_opencl_filter;

        in = ctx->fs.in;
        for (i = 0; i < ctx->nb_inputs; i++) {
            const AVFilterLink *inlink = avctx->inputs[i];

            in[i].time_base = inlink->time_base;
            in[i].sync      = 1;
            in[i].before    = EXT_STOP;
            in[i].after     = EXT_INFINITY;
        }

        err = ff_framesync_configure(&ctx->fs);
        if (err < 0)
            return err;

    } else {
        outlink->time_base = av_inv_q(ctx->source_rate);
    }

    return 0;
}

static av_cold int program_opencl_init(AVFilterContext *avctx)
{
    ProgramOpenCLContext *ctx = avctx->priv;
    int err;

    ff_opencl_filter_init(avctx);

    ctx->ocf.output_width  = ctx->width;
    ctx->ocf.output_height = ctx->height;

    if (!strcmp(avctx->filter->name, "openclsrc")) {
        if (!ctx->ocf.output_width || !ctx->ocf.output_height) {
            av_log(avctx, AV_LOG_ERROR, "OpenCL source requires output "
                   "dimensions to be specified.\n");
            return AVERROR(EINVAL);
        }

        ctx->nb_inputs = 0;
        ctx->ocf.output_format = ctx->source_format;
    } else {
        int i;

        ctx->frames = av_mallocz_array(ctx->nb_inputs,
                                       sizeof(*ctx->frames));
        if (!ctx->frames)
            return AVERROR(ENOMEM);

        for (i = 0; i < ctx->nb_inputs; i++) {
            AVFilterPad input;
            memset(&input, 0, sizeof(input));

            input.type = AVMEDIA_TYPE_VIDEO;
            input.name = av_asprintf("input%d", i);
            if (!input.name)
                return AVERROR(ENOMEM);

            input.config_props = &ff_opencl_filter_config_input;

            err = ff_insert_inpad(avctx, i, &input);
            if (err < 0) {
                av_freep(&input.name);
                return err;
            }
        }
    }

    return 0;
}

static av_cold void program_opencl_uninit(AVFilterContext *avctx)
{
    ProgramOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    int i;

    if (ctx->nb_inputs > 0) {
        ff_framesync_uninit(&ctx->fs);

        av_freep(&ctx->frames);
        for (i = 0; i < avctx->nb_inputs; i++)
            av_freep(&avctx->input_pads[i].name);
    }

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

#define OFFSET(x) offsetof(ProgramOpenCLContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)

#if CONFIG_PROGRAM_OPENCL_FILTER

static const AVOption program_opencl_options[] = {
    { "source", "OpenCL program source file", OFFSET(source_file),
      AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },
    { "kernel", "Kernel name in program",     OFFSET(kernel_name),
      AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },

    { "inputs", "Number of inputs", OFFSET(nb_inputs),
      AV_OPT_TYPE_INT,              { .i64 = 1 }, 1, INT_MAX, FLAGS },

    { "size",   "Video size",       OFFSET(width),
      AV_OPT_TYPE_IMAGE_SIZE,       { .str = NULL }, 0, 0, FLAGS },
    { "s",      "Video size",       OFFSET(width),
      AV_OPT_TYPE_IMAGE_SIZE,       { .str = NULL }, 0, 0, FLAGS },

    { NULL },
};

FRAMESYNC_DEFINE_CLASS(program_opencl, ProgramOpenCLContext, fs);

static const AVFilterPad program_opencl_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &program_opencl_config_output,
    },
    { NULL }
};

AVFilter ff_vf_program_opencl = {
    .name           = "program_opencl",
    .description    = NULL_IF_CONFIG_SMALL("Filter video using an OpenCL program"),
    .priv_size      = sizeof(ProgramOpenCLContext),
    .priv_class     = &program_opencl_class,
    .preinit        = &program_opencl_framesync_preinit,
    .init           = &program_opencl_init,
    .uninit         = &program_opencl_uninit,
    .query_formats  = &ff_opencl_filter_query_formats,
    .activate       = &program_opencl_activate,
    .inputs         = NULL,
    .outputs        = program_opencl_outputs,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};

#endif

#if CONFIG_OPENCLSRC_FILTER

static const AVOption openclsrc_options[] = {
    { "source", "OpenCL program source file", OFFSET(source_file),
      AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },
    { "kernel", "Kernel name in program",     OFFSET(kernel_name),
      AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },

    { "size",   "Video size",       OFFSET(width),
      AV_OPT_TYPE_IMAGE_SIZE,       { .str = NULL }, 0, 0, FLAGS },
    { "s",      "Video size",       OFFSET(width),
      AV_OPT_TYPE_IMAGE_SIZE,       { .str = NULL }, 0, 0, FLAGS },

    { "format", "Video format",     OFFSET(source_format),
      AV_OPT_TYPE_PIXEL_FMT,        { .i64 = AV_PIX_FMT_NONE }, -1, INT_MAX, FLAGS },

    { "rate",   "Video frame rate", OFFSET(source_rate),
      AV_OPT_TYPE_VIDEO_RATE,       { .str = "25" }, 0, INT_MAX, FLAGS },
    { "r",      "Video frame rate", OFFSET(source_rate),
      AV_OPT_TYPE_VIDEO_RATE,       { .str = "25" }, 0, INT_MAX, FLAGS },

    { NULL },
};

AVFILTER_DEFINE_CLASS(openclsrc);

static const AVFilterPad openclsrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = &program_opencl_config_output,
        .request_frame = &program_opencl_request_frame,
    },
    { NULL }
};

AVFilter ff_vsrc_openclsrc = {
    .name           = "openclsrc",
    .description    = NULL_IF_CONFIG_SMALL("Generate video using an OpenCL program"),
    .priv_size      = sizeof(ProgramOpenCLContext),
    .priv_class     = &openclsrc_class,
    .init           = &program_opencl_init,
    .uninit         = &program_opencl_uninit,
    .query_formats  = &ff_opencl_filter_query_formats,
    .inputs         = NULL,
    .outputs        = openclsrc_outputs,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};

#endif
