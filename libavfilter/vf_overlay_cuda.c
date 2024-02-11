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
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/eval.h"

#include "avfilter.h"
#include "filters.h"
#include "framesync.h"
#include "internal.h"

#include "cuda/load_helper.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, ctx->hwctx->internal->cuda_dl, x)
#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )

#define BLOCK_X 32
#define BLOCK_Y 16

#define MAIN    0
#define OVERLAY 1

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

enum var_name {
    VAR_MAIN_W,    VAR_MW,
    VAR_MAIN_H,    VAR_MH,
    VAR_OVERLAY_W, VAR_OW,
    VAR_OVERLAY_H, VAR_OH,
    VAR_X,
    VAR_Y,
    VAR_N,
#if FF_API_FRAME_PKT
    VAR_POS,
#endif
    VAR_T,
    VAR_VARS_NB
};

enum EvalMode {
    EVAL_MODE_INIT,
    EVAL_MODE_FRAME,
    EVAL_MODE_NB
};

static const char *const var_names[] = {
    "main_w",    "W", ///< width  of the main    video
    "main_h",    "H", ///< height of the main    video
    "overlay_w", "w", ///< width  of the overlay video
    "overlay_h", "h", ///< height of the overlay video
    "x",
    "y",
    "n",            ///< number of frame
#if FF_API_FRAME_PKT
    "pos",          ///< position in the file
#endif
    "t",            ///< timestamp expressed in seconds
    NULL
};

/**
 * OverlayCUDAContext
 */
typedef struct OverlayCUDAContext {
    const AVClass      *class;

    enum AVPixelFormat in_format_overlay;
    enum AVPixelFormat in_format_main;

    AVBufferRef *hw_device_ctx;
    AVCUDADeviceContext *hwctx;

    CUcontext cu_ctx;
    CUmodule cu_module;
    CUfunction cu_func;
    CUstream cu_stream;

    FFFrameSync fs;

    int eval_mode;
    int x_position;
    int y_position;

    double var_values[VAR_VARS_NB];
    char *x_expr, *y_expr;

    AVExpr *x_pexpr, *y_pexpr;
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

static inline int normalize_xy(double d, int chroma_sub)
{
    if (isnan(d))
        return INT_MAX;
    return (int)d & ~((1 << chroma_sub) - 1);
}

static void eval_expr(AVFilterContext *ctx)
{
    OverlayCUDAContext *s = ctx->priv;

    s->var_values[VAR_X] = av_expr_eval(s->x_pexpr, s->var_values, NULL);
    s->var_values[VAR_Y] = av_expr_eval(s->y_pexpr, s->var_values, NULL);
    /* necessary if x is expressed from y  */
    s->var_values[VAR_X] = av_expr_eval(s->x_pexpr, s->var_values, NULL);

    s->x_position = normalize_xy(s->var_values[VAR_X], 1);

    /* the cuda pixel format is using hwaccel, normalizing y is unnecessary */
    s->y_position = s->var_values[VAR_Y];
}

static int set_expr(AVExpr **pexpr, const char *expr, const char *option, void *log_ctx)
{
    int ret;
    AVExpr *old = NULL;

    if (*pexpr)
        old = *pexpr;
    ret = av_expr_parse(pexpr, expr, var_names,
                        NULL, NULL, NULL, NULL, 0, log_ctx);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Error when evaluating the expression '%s' for %s\n",
               expr, option);
        *pexpr = old;
        return ret;
    }

    av_expr_free(old);
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
    AVFilterLink *inlink = avctx->inputs[0];

    CudaFunctions *cu = ctx->hwctx->internal->cuda_dl;
    CUcontext dummy, cuda_ctx = ctx->hwctx->cuda_ctx;

    AVFrame *input_main, *input_overlay;

    ctx->cu_ctx = cuda_ctx;

    // read main and overlay frames from inputs
    ret = ff_framesync_dualinput_get(fs, &input_main, &input_overlay);
    if (ret < 0)
        return ret;

    if (!input_main)
        return AVERROR_BUG;

    if (!input_overlay)
        return ff_filter_frame(outlink, input_main);

    ret = ff_inlink_make_frame_writable(inlink, &input_main);
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

    if (ctx->eval_mode == EVAL_MODE_FRAME) {
        ctx->var_values[VAR_N] = inlink->frame_count_out;
        ctx->var_values[VAR_T] = input_main->pts == AV_NOPTS_VALUE ?
            NAN : input_main->pts * av_q2d(inlink->time_base);

#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
        {
            int64_t pos = input_main->pkt_pos;
            ctx->var_values[VAR_POS] = pos == -1 ? NAN : pos;
        }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        ctx->var_values[VAR_OVERLAY_W] = ctx->var_values[VAR_OW] = input_overlay->width;
        ctx->var_values[VAR_OVERLAY_H] = ctx->var_values[VAR_OH] = input_overlay->height;
        ctx->var_values[VAR_MAIN_W   ] = ctx->var_values[VAR_MW] = input_main->width;
        ctx->var_values[VAR_MAIN_H   ] = ctx->var_values[VAR_MH] = input_main->height;

        eval_expr(avctx);

        av_log(avctx, AV_LOG_DEBUG, "n:%f t:%f x:%f xi:%d y:%f yi:%d\n",
               ctx->var_values[VAR_N], ctx->var_values[VAR_T],
               ctx->var_values[VAR_X], ctx->x_position,
               ctx->var_values[VAR_Y], ctx->y_position);
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

static int config_input_overlay(AVFilterLink *inlink)
{
    AVFilterContext *ctx  = inlink->dst;
    OverlayCUDAContext  *s = inlink->dst->priv;
    int ret;


    /* Finish the configuration by evaluating the expressions
       now when both inputs are configured. */
    s->var_values[VAR_MAIN_W   ] = s->var_values[VAR_MW] = ctx->inputs[MAIN   ]->w;
    s->var_values[VAR_MAIN_H   ] = s->var_values[VAR_MH] = ctx->inputs[MAIN   ]->h;
    s->var_values[VAR_OVERLAY_W] = s->var_values[VAR_OW] = ctx->inputs[OVERLAY]->w;
    s->var_values[VAR_OVERLAY_H] = s->var_values[VAR_OH] = ctx->inputs[OVERLAY]->h;
    s->var_values[VAR_X]   = NAN;
    s->var_values[VAR_Y]   = NAN;
    s->var_values[VAR_N]   = 0;
    s->var_values[VAR_T]   = NAN;
#if FF_API_FRAME_PKT
    s->var_values[VAR_POS] = NAN;
#endif

    if ((ret = set_expr(&s->x_pexpr, s->x_expr, "x", ctx)) < 0 ||
        (ret = set_expr(&s->y_pexpr, s->y_expr, "y", ctx)) < 0)
        return ret;

    if (s->eval_mode == EVAL_MODE_INIT) {
        eval_expr(ctx);
        av_log(ctx, AV_LOG_VERBOSE, "x:%f xi:%d y:%f yi:%d\n",
               s->var_values[VAR_X], s->x_position,
               s->var_values[VAR_Y], s->y_position);
    }

    return 0;
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

    av_expr_free(ctx->x_pexpr); ctx->x_pexpr = NULL;
    av_expr_free(ctx->y_pexpr); ctx->y_pexpr = NULL;
    av_buffer_unref(&ctx->hw_device_ctx);
    ctx->hwctx = NULL;
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
 * Configure output
 */
static int overlay_cuda_config_output(AVFilterLink *outlink)
{
    extern const unsigned char ff_vf_overlay_cuda_ptx_data[];
    extern const unsigned int ff_vf_overlay_cuda_ptx_len;

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

    ctx->hw_device_ctx = av_buffer_ref(frames_ctx->device_ref);
    if (!ctx->hw_device_ctx)
        return AVERROR(ENOMEM);
    ctx->hwctx = ((AVHWDeviceContext*)ctx->hw_device_ctx->data)->hwctx;

    cuda_ctx = ctx->hwctx->cuda_ctx;
    ctx->fs.time_base = inlink->time_base;

    ctx->cu_stream = ctx->hwctx->stream;

    outlink->hw_frames_ctx = av_buffer_ref(inlink->hw_frames_ctx);
    if (!outlink->hw_frames_ctx)
        return AVERROR(ENOMEM);

    // load functions

    cu = ctx->hwctx->internal->cuda_dl;

    err = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (err < 0) {
        return err;
    }

    err = ff_cuda_load_module(ctx, ctx->hwctx, &ctx->cu_module, ff_vf_overlay_cuda_ptx_data, ff_vf_overlay_cuda_ptx_len);
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
    { "x", "set the x expression of overlay", OFFSET(x_expr), AV_OPT_TYPE_STRING, { .str = "0" }, 0, 0, FLAGS },
    { "y", "set the y expression of overlay", OFFSET(y_expr), AV_OPT_TYPE_STRING, { .str = "0" }, 0, 0, FLAGS },
    { "eof_action", "Action to take when encountering EOF from secondary input ",
        OFFSET(fs.opt_eof_action), AV_OPT_TYPE_INT, { .i64 = EOF_ACTION_REPEAT },
        EOF_ACTION_REPEAT, EOF_ACTION_PASS, .flags = FLAGS, .unit = "eof_action" },
        { "repeat", "Repeat the previous frame.",   0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_REPEAT }, .flags = FLAGS, .unit = "eof_action" },
        { "endall", "End both streams.",            0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_ENDALL }, .flags = FLAGS, .unit = "eof_action" },
        { "pass",   "Pass through the main input.", 0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_PASS },   .flags = FLAGS, .unit = "eof_action" },
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, { .i64 = EVAL_MODE_FRAME }, 0, EVAL_MODE_NB - 1, FLAGS, .unit = "eval" },
         { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, { .i64=EVAL_MODE_INIT },  .flags = FLAGS, .unit = "eval" },
         { "frame", "eval expressions per-frame",                  0, AV_OPT_TYPE_CONST, { .i64=EVAL_MODE_FRAME }, .flags = FLAGS, .unit = "eval" },
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
        .config_props = config_input_overlay,
    },
};

static const AVFilterPad overlay_cuda_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = &overlay_cuda_config_output,
    },
};

const AVFilter ff_vf_overlay_cuda = {
    .name            = "overlay_cuda",
    .description     = NULL_IF_CONFIG_SMALL("Overlay one video on top of another using CUDA"),
    .priv_size       = sizeof(OverlayCUDAContext),
    .priv_class      = &overlay_cuda_class,
    .init            = &overlay_cuda_init,
    .uninit          = &overlay_cuda_uninit,
    .activate        = &overlay_cuda_activate,
    FILTER_INPUTS(overlay_cuda_inputs),
    FILTER_OUTPUTS(overlay_cuda_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),
    .preinit         = overlay_cuda_framesync_preinit,
    .flags_internal  = FF_FILTER_FLAG_HWFRAME_AWARE,
};
