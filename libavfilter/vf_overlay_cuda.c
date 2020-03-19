/*
 * Copyright (c) 2020 Yaroslav Pogrebnyak <yyyaroslav@gmail.com>
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

/**
 * @file
 * Overlay one video on top of another using cuda hardware acceleration
 */

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"

#include "avfilter.h"
#include "framesync.h"
#include "internal.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, ctx->hwctx->internal->cuda_dl, x)
#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )

#define BLOCK_X 32
#define BLOCK_Y 16

static const enum AVPixelFormat supported_main_formats[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE,
};

static const enum AVPixelFormat supported_overlay_formats[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_NONE,
};

/**
 * OverlayCUDAContext
 */
typedef struct OverlayCUDAContext {
    const AVClass      *class;

    enum AVPixelFormat in_format_overlay;
    enum AVPixelFormat in_format_main;

    AVCUDADeviceContext *hwctx;

    CUcontext cu_ctx;
    CUmodule cu_module;
    CUfunction cu_func;
    CUstream cu_stream;

    FFFrameSync fs;

    int x_position;
    int y_position;

} OverlayCUDAContext;

/**
 * Helper to find out if provided format is supported by filter
 */
static int format_is_supported(const enum AVPixelFormat formats[], enum AVPixelFormat fmt)
{
    for (int i = 0; formats[i] != AV_PIX_FMT_NONE; i++)
        if (formats[i] == fmt)
            return 1;
    return 0;
}

/**
 * Helper checks if we can process main and overlay pixel formats
 */
static int formats_match(const enum AVPixelFormat format_main, const enum AVPixelFormat format_overlay) {
    switch(format_main) {
    case AV_PIX_FMT_NV12:
        return format_overlay == AV_PIX_FMT_NV12;
    case AV_PIX_FMT_YUV420P:
        return format_overlay == AV_PIX_FMT_YUV420P ||
               format_overlay == AV_PIX_FMT_YUVA420P;
    default:
        return 0;
    }
}

/**
 * Call overlay kernell for a plane
 */
static int overlay_cuda_call_kernel(
    OverlayCUDAContext *ctx,
    int x_position, int y_position,
    uint8_t* main_data, int main_linesize,
    int main_width, int main_height,
    uint8_t* overlay_data, int overlay_linesize,
    int overlay_width, int overlay_height,
    uint8_t* alpha_data, int alpha_linesize,
    int alpha_adj_x, int alpha_adj_y) {

    CudaFunctions *cu = ctx->hwctx->internal->cuda_dl;

    void* kernel_args[] = {
        &x_position, &y_position,
        &main_data, &main_linesize,
        &overlay_data, &overlay_linesize,
        &overlay_width, &overlay_height,
        &alpha_data, &alpha_linesize,
        &alpha_adj_x, &alpha_adj_y,
    };

    return CHECK_CU(cu->cuLaunchKernel(
        ctx->cu_func,
        DIV_UP(main_width, BLOCK_X), DIV_UP(main_height, BLOCK_Y), 1,
        BLOCK_X, BLOCK_Y, 1,
        0, ctx->cu_stream, kernel_args, NULL));
}

/**
 * Perform blend overlay picture over main picture
 */
static int overlay_cuda_blend(FFFrameSync *fs)
{
    int ret;

    AVFilterContext *avctx = fs->parent;
    OverlayCUDAContext *ctx = avctx->priv;
    AVFilterLink *outlink = avctx->outputs[0];

    CudaFunctions *cu = ctx->hwctx->internal->cuda_dl;
    CUcontext dummy, cuda_ctx = ctx->hwctx->cuda_ctx;

    AVFrame *input_main, *input_overlay;

    ctx->cu_ctx = cuda_ctx;

    // read main and overlay frames from inputs
    ret = ff_framesync_dualinput_get(fs, &input_main, &input_overlay);
    if (ret < 0)
        return ret;

    if (!input_main || !input_overlay)
        return AVERROR_BUG;

    ret = av_frame_make_writable(input_main);
    if (ret < 0) {
        av_frame_free(&input_main);
        return ret;
    }

    // push cuda context

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0) {
        av_frame_free(&input_main);
        return ret;
    }

    // overlay first plane

    overlay_cuda_call_kernel(ctx,
        ctx->x_position, ctx->y_position,
        input_main->data[0], input_main->linesize[0],
        input_main->width, input_main->height,
        input_overlay->data[0], input_overlay->linesize[0],
        input_overlay->width, input_overlay->height,
        input_overlay->data[3], input_overlay->linesize[3], 1, 1);

    // overlay rest planes depending on pixel format

    switch(ctx->in_format_overlay) {
    case AV_PIX_FMT_NV12:
        overlay_cuda_call_kernel(ctx,
            ctx->x_position, ctx->y_position / 2,
            input_main->data[1], input_main->linesize[1],
            input_main->width, input_main->height / 2,
            input_overlay->data[1], input_overlay->linesize[1],
            input_overlay->width, input_overlay->height / 2,
            0, 0, 0, 0);
        break;
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVA420P:
        overlay_cuda_call_kernel(ctx,
            ctx->x_position / 2 , ctx->y_position / 2,
            input_main->data[1], input_main->linesize[1],
            input_main->width / 2, input_main->height / 2,
            input_overlay->data[1], input_overlay->linesize[1],
            input_overlay->width / 2, input_overlay->height / 2,
            input_overlay->data[3], input_overlay->linesize[3], 2, 2);

        overlay_cuda_call_kernel(ctx,
            ctx->x_position / 2 , ctx->y_position / 2,
            input_main->data[2], input_main->linesize[2],
            input_main->width / 2, input_main->height / 2,
            input_overlay->data[2], input_overlay->linesize[2],
            input_overlay->width / 2, input_overlay->height / 2,
            input_overlay->data[3], input_overlay->linesize[3], 2, 2);
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Passed unsupported overlay pixel format\n");
        av_frame_free(&input_main);
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
        return AVERROR_BUG;
    }

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    return ff_filter_frame(outlink, input_main);
}

/**
 * Initialize overlay_cuda
 */
static av_cold int overlay_cuda_init(AVFilterContext *avctx)
{
    OverlayCUDAContext* ctx = avctx->priv;
    ctx->fs.on_event = &overlay_cuda_blend;

    return 0;
}

/**
 * Uninitialize overlay_cuda
 */
static av_cold void overlay_cuda_uninit(AVFilterContext *avctx)
{
    OverlayCUDAContext* ctx = avctx->priv;

    ff_framesync_uninit(&ctx->fs);

    if (ctx->hwctx && ctx->cu_module) {
        CUcontext dummy;
        CudaFunctions *cu = ctx->hwctx->internal->cuda_dl;
        CHECK_CU(cu->cuCtxPushCurrent(ctx->cu_ctx));
        CHECK_CU(cu->cuModuleUnload(ctx->cu_module));
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    }
}

/**
 * Activate overlay_cuda
 */
static int overlay_cuda_activate(AVFilterContext *avctx)
{
    OverlayCUDAContext *ctx = avctx->priv;

    return ff_framesync_activate(&ctx->fs);
}

/**
 * Query formats
 */
static int overlay_cuda_query_formats(AVFilterContext *avctx)
{
    static const enum AVPixelFormat pixel_formats[] = {
        AV_PIX_FMT_CUDA, AV_PIX_FMT_NONE,
    };

    AVFilterFormats *pix_fmts = ff_make_format_list(pixel_formats);

    return ff_set_common_formats(avctx, pix_fmts);
}

/**
 * Configure output
 */
static int overlay_cuda_config_output(AVFilterLink *outlink)
{

    extern char vf_overlay_cuda_ptx[];

    int err;
    AVFilterContext* avctx = outlink->src;
    OverlayCUDAContext* ctx = avctx->priv;

    AVFilterLink *inlink = avctx->inputs[0];
    AVHWFramesContext  *frames_ctx = (AVHWFramesContext*)inlink->hw_frames_ctx->data;

    AVFilterLink *inlink_overlay = avctx->inputs[1];
    AVHWFramesContext  *frames_ctx_overlay = (AVHWFramesContext*)inlink_overlay->hw_frames_ctx->data;

    CUcontext dummy, cuda_ctx;
    CudaFunctions *cu;

    // check main input formats

    if (!frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on main input\n");
        return AVERROR(EINVAL);
    }

    ctx->in_format_main = frames_ctx->sw_format;
    if (!format_is_supported(supported_main_formats, ctx->in_format_main)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported main input format: %s\n",
               av_get_pix_fmt_name(ctx->in_format_main));
        return AVERROR(ENOSYS);
    }

    // check overlay input formats

    if (!frames_ctx_overlay) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on overlay input\n");
        return AVERROR(EINVAL);
    }

    ctx->in_format_overlay = frames_ctx_overlay->sw_format;
    if (!format_is_supported(supported_overlay_formats, ctx->in_format_overlay)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported overlay input format: %s\n",
            av_get_pix_fmt_name(ctx->in_format_overlay));
        return AVERROR(ENOSYS);
    }

    // check we can overlay pictures with those pixel formats

    if (!formats_match(ctx->in_format_main, ctx->in_format_overlay)) {
        av_log(ctx, AV_LOG_ERROR, "Can't overlay %s on %s \n",
            av_get_pix_fmt_name(ctx->in_format_overlay), av_get_pix_fmt_name(ctx->in_format_main));
        return AVERROR(EINVAL);
    }

    // initialize

    ctx->hwctx = frames_ctx->device_ctx->hwctx;
    cuda_ctx = ctx->hwctx->cuda_ctx;
    ctx->fs.time_base = inlink->time_base;

    ctx->cu_stream = ctx->hwctx->stream;

    outlink->hw_frames_ctx = av_buffer_ref(inlink->hw_frames_ctx);

    // load functions

    cu = ctx->hwctx->internal->cuda_dl;

    err = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (err < 0) {
        return err;
    }

    err = CHECK_CU(cu->cuModuleLoadData(&ctx->cu_module, vf_overlay_cuda_ptx));
    if (err < 0) {
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
        return err;
    }

    err = CHECK_CU(cu->cuModuleGetFunction(&ctx->cu_func, ctx->cu_module, "Overlay_Cuda"));
    if (err < 0) {
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
        return err;
    }

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    // init dual input

    err = ff_framesync_init_dualinput(&ctx->fs, avctx);
    if (err < 0) {
        return err;
    }

    return ff_framesync_configure(&ctx->fs);
}


#define OFFSET(x) offsetof(OverlayCUDAContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption overlay_cuda_options[] = {
    { "x", "Overlay x position",
      OFFSET(x_position), AV_OPT_TYPE_INT, { .i64 = 0 }, INT_MIN, INT_MAX, .flags = FLAGS },
    { "y", "Overlay y position",
      OFFSET(y_position), AV_OPT_TYPE_INT, { .i64 = 0 }, INT_MIN, INT_MAX, .flags = FLAGS },
    { "eof_action", "Action to take when encountering EOF from secondary input ",
        OFFSET(fs.opt_eof_action), AV_OPT_TYPE_INT, { .i64 = EOF_ACTION_REPEAT },
        EOF_ACTION_REPEAT, EOF_ACTION_PASS, .flags = FLAGS, "eof_action" },
        { "repeat", "Repeat the previous frame.",   0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_REPEAT }, .flags = FLAGS, "eof_action" },
        { "endall", "End both streams.",            0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_ENDALL }, .flags = FLAGS, "eof_action" },
        { "pass",   "Pass through the main input.", 0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_PASS },   .flags = FLAGS, "eof_action" },
    { "shortest", "force termination when the shortest input terminates", OFFSET(fs.opt_shortest), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "repeatlast", "repeat overlay of the last overlay frame", OFFSET(fs.opt_repeatlast), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { NULL },
};

FRAMESYNC_DEFINE_CLASS(overlay_cuda, OverlayCUDAContext, fs);

static const AVFilterPad overlay_cuda_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name         = "overlay",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

static const AVFilterPad overlay_cuda_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = &overlay_cuda_config_output,
    },
    { NULL }
};

AVFilter ff_vf_overlay_cuda = {
    .name            = "overlay_cuda",
    .description     = NULL_IF_CONFIG_SMALL("Overlay one video on top of another using CUDA"),
    .priv_size       = sizeof(OverlayCUDAContext),
    .priv_class      = &overlay_cuda_class,
    .init            = &overlay_cuda_init,
    .uninit          = &overlay_cuda_uninit,
    .activate        = &overlay_cuda_activate,
    .query_formats   = &overlay_cuda_query_formats,
    .inputs          = overlay_cuda_inputs,
    .outputs         = overlay_cuda_outputs,
    .preinit         = overlay_cuda_framesync_preinit,
    .flags_internal  = FF_FILTER_FLAG_HWFRAME_AWARE,
};
