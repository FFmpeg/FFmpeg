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

/**
 * @file
 * CUDA video padding filter
 */

#include <float.h>

#include "filters.h"
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/cuda_check.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/colorspace.h"

#include "cuda/load_helper.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, device_hwctx->internal->cuda_dl, x)
#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define BLOCK_X 32
#define BLOCK_Y 16

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_NV12,
};

typedef struct CUDAPadContext {
    const AVClass *class;

    AVBufferRef *frames_ctx;

    int w, h;       ///< output dimensions, a value of 0 will result in the input size
    int x, y;       ///< offsets of the input area with respect to the padded area
    int in_w, in_h; ///< width and height for the padded input video

    char *w_expr;   ///< width expression
    char *h_expr;   ///< height expression
    char *x_expr;   ///< x offset expression
    char *y_expr;   ///< y offset expression

    uint8_t rgba_color[4];    ///< color for the padding area
    uint8_t parsed_color[4];
    AVRational aspect;

    int eval_mode;

    int last_out_w, last_out_h; ///< used to evaluate the prior output width and height with the incoming frame

    AVCUDADeviceContext *hwctx;
    CUmodule cu_module;
    CUfunction cu_func_uchar;
    CUfunction cu_func_uchar2;
} CUDAPadContext;

static const char *const var_names[] = {
    "in_w",  "iw",
    "in_h",  "ih",
    "out_w", "ow",
    "out_h", "oh",
    "x",
    "y",
    "a",
    "sar",
    "dar",
    "hsub",
    "vsub",
    NULL
};

enum {
    VAR_IN_W,
    VAR_IW,
    VAR_IN_H,
    VAR_IH,
    VAR_OUT_W,
    VAR_OW,
    VAR_OUT_H,
    VAR_OH,
    VAR_X,
    VAR_Y,
    VAR_A,
    VAR_SAR,
    VAR_DAR,
    VAR_HSUB,
    VAR_VSUB,
    VARS_NB
};

enum EvalMode {
    EVAL_MODE_INIT,
    EVAL_MODE_FRAME,
    EVAL_MODE_NB
};

static int eval_expr(AVFilterContext *ctx)
{
    CUDAPadContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    double var_values[VARS_NB], res;
    char *expr;
    int ret;

    var_values[VAR_IN_W]   = var_values[VAR_IW]   = s->in_w;
    var_values[VAR_IN_H]   = var_values[VAR_IH]   = s->in_h;
    var_values[VAR_OUT_W]  = var_values[VAR_OW]  = NAN;
    var_values[VAR_OUT_H]  = var_values[VAR_OH]  = NAN;
    var_values[VAR_A]      = (double)s->in_w / s->in_h;
    var_values[VAR_SAR]    = inlink->sample_aspect_ratio.num ?
                             (double)inlink->sample_aspect_ratio.num /
                             inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]    = var_values[VAR_A] * var_values[VAR_SAR];
    var_values[VAR_HSUB]   = 1 << desc->log2_chroma_w;
    var_values[VAR_VSUB]   = 1 << desc->log2_chroma_h;

    expr = s->w_expr;
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values, NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto fail;

    s->w = res;
    if (s->w < 0) {
        av_log(ctx, AV_LOG_ERROR, "Width expression is negative.\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    var_values[VAR_OUT_W] = var_values[VAR_OW] = s->w;

    expr = s->h_expr;
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values, NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto fail;

    s->h = res;
    if (s->h < 0) {
        av_log(ctx, AV_LOG_ERROR, "Height expression is negative.\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }
    var_values[VAR_OUT_H] = var_values[VAR_OH] = s->h;

    if (!s->h)
        s->h = s->in_h;

    var_values[VAR_OUT_H] = var_values[VAR_OH] = s->h;


    expr = s->w_expr;
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values, NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto fail;

    s->w = res;
    if (s->w < 0) {
        av_log(ctx, AV_LOG_ERROR, "Width expression is negative.\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }
    if (!s->w)
        s->w = s->in_w;

    var_values[VAR_OUT_W] = var_values[VAR_OW] = s->w;


    expr = s->x_expr;
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values, NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto fail;

    s->x = res;


    expr = s->y_expr;
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values, NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto fail;

    s->y = res;

    if (s->x < 0 || s->x + s->in_w > s->w) {
        s->x = (s->w - s->in_w) / 2;
        av_log(ctx, AV_LOG_VERBOSE, "centering X offset.\n");
    }

    if (s->y < 0 || s->y + s->in_h > s->h) {
        s->y = (s->h - s->in_h) / 2;
        av_log(ctx, AV_LOG_VERBOSE, "centering Y offset.\n");
    }

    s->w = av_clip(s->w, 1, INT_MAX);
    s->h = av_clip(s->h, 1, INT_MAX);

    if (s->w < s->in_w || s->h < s->in_h) {
        av_log(ctx, AV_LOG_ERROR, "Padded size < input size.\n");
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_DEBUG,
           "w:%d h:%d -> w:%d h:%d x:%d y:%d color:0x%02X%02X%02X%02X\n",
           inlink->w, inlink->h, s->w, s->h, s->x, s->y, s->rgba_color[0],
           s->rgba_color[1], s->rgba_color[2], s->rgba_color[3]);

    return 0;

fail:
    av_log(ctx, AV_LOG_ERROR, "Error evaluating '%s'\n", expr);
    return ret;
}

static int cuda_pad_alloc_out_frames_ctx(AVFilterContext *ctx, AVBufferRef **out_frames_ctx, const int width, const int height)
{
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink *inl = ff_filter_link(inlink);
    AVHWFramesContext *in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
    int ret;

    *out_frames_ctx = av_hwframe_ctx_alloc(in_frames_ctx->device_ref);
    if (!*out_frames_ctx) {
        return AVERROR(ENOMEM);
    }

    AVHWFramesContext *out_fc = (AVHWFramesContext *)(*out_frames_ctx)->data;
    out_fc->format    = AV_PIX_FMT_CUDA;
    out_fc->sw_format = in_frames_ctx->sw_format;

    out_fc->width     = FFALIGN(width, 32);
    out_fc->height    = FFALIGN(height, 32);

    ret = av_hwframe_ctx_init(*out_frames_ctx);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to init output ctx\n");
        av_buffer_unref(out_frames_ctx);
        return ret;
    }

    return 0;
}

static av_cold int cuda_pad_init(AVFilterContext *ctx)
{
    CUDAPadContext *s = ctx->priv;

    s->last_out_w = -1;
    s->last_out_h = -1;

    return 0;
}

static av_cold void cuda_pad_uninit(AVFilterContext *ctx)
{
    CUDAPadContext *s = ctx->priv;
    CUcontext dummy;

    av_buffer_unref(&s->frames_ctx);

    if (s->hwctx && s->cu_module) {
        CudaFunctions *cu = s->hwctx->internal->cuda_dl;
        AVCUDADeviceContext *device_hwctx = s->hwctx;
        CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
        CHECK_CU(cu->cuModuleUnload(s->cu_module));
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    }

    s->cu_module = NULL;
    s->hwctx = NULL;
}

static av_cold int cuda_pad_load_functions(AVFilterContext *ctx)
{
    CUDAPadContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    CUcontext dummy_cu_ctx;
    int ret;

    AVCUDADeviceContext *device_hwctx = s->hwctx;

    extern const unsigned char ff_vf_pad_cuda_ptx_data[];
    extern const unsigned int ff_vf_pad_cuda_ptx_len;

    ret = CHECK_CU(cu->cuCtxPushCurrent(device_hwctx->cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, device_hwctx, &s->cu_module,
                              ff_vf_pad_cuda_ptx_data, ff_vf_pad_cuda_ptx_len);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load CUDA module\n");
        goto end;
    }

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_uchar, s->cu_module, "pad_uchar"));
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load pad_planar_cuda\n");
        goto end;
    }

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_uchar2, s->cu_module, "pad_uchar2"));
    if (ret < 0)
        av_log(ctx, AV_LOG_ERROR, "Failed to load pad_uv_cuda\n");

end:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy_cu_ctx));

    return ret;
}

static int cuda_pad_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    CUDAPadContext *s = ctx->priv;

    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink *inl = ff_filter_link(inlink);

    FilterLink *ol = ff_filter_link(outlink);

    AVHWFramesContext *in_frames_ctx;
    int format_supported = 0;
    int ret;

    s->in_w = inlink->w;
    s->in_h = inlink->h;
    ret = eval_expr(ctx);
    if (ret < 0)
        return ret;

    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }

    in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
    s->hwctx = in_frames_ctx->device_ctx->hwctx;

    for (int i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++) {
        if (in_frames_ctx->sw_format == supported_formats[i]) {
            format_supported = 1;
            break;
        }
    }
    if (!format_supported) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format.\n");
        return AVERROR(EINVAL);
    }

    s->parsed_color[0] = RGB_TO_Y_BT709(s->rgba_color[0], s->rgba_color[1], s->rgba_color[2]);
    s->parsed_color[1] = RGB_TO_U_BT709(s->rgba_color[0], s->rgba_color[1], s->rgba_color[2], 0);
    s->parsed_color[2] = RGB_TO_V_BT709(s->rgba_color[0], s->rgba_color[1], s->rgba_color[2], 0);
    s->parsed_color[3] = s->rgba_color[3];

    ret = cuda_pad_alloc_out_frames_ctx(ctx, &s->frames_ctx, s->w, s->h);
    if (ret < 0)
        return ret;

    ol->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
    if (!ol->hw_frames_ctx)
        return AVERROR(ENOMEM);

    outlink->w         = s->w;
    outlink->h         = s->h;
    outlink->time_base = inlink->time_base;
    outlink->format    = AV_PIX_FMT_CUDA;

    s->last_out_w = s->w;
    s->last_out_h = s->h;

    ret = cuda_pad_load_functions(ctx);
    if (ret < 0)
        return ret;

    return 0;
}

static int cuda_pad_pad(AVFilterContext *ctx, AVFrame *out, const AVFrame *in)
{
    CUDAPadContext *s = ctx->priv;
    FilterLink *inl = ff_filter_link(ctx->inputs[0]);

    AVHWFramesContext *in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(in_frames_ctx->sw_format);

    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    AVCUDADeviceContext *device_hwctx = s->hwctx;
    int ret;


    const int nb_planes = av_pix_fmt_count_planes(in_frames_ctx->sw_format);
    for (int plane = 0; plane < nb_planes; plane++) {
        const AVComponentDescriptor *cur_comp = &pixdesc->comp[0];
        for (int comp = 1; comp < pixdesc->nb_components && cur_comp->plane != plane; comp++)
            cur_comp = &pixdesc->comp[comp];

        int hsub = (plane == 1 || plane == 2) ? pixdesc->log2_chroma_w : 0;
        int vsub = (plane == 1 || plane == 2) ? pixdesc->log2_chroma_h : 0;

        int src_w = AV_CEIL_RSHIFT(s->in_w, hsub);
        int src_h = AV_CEIL_RSHIFT(s->in_h, vsub);

        int dst_w = AV_CEIL_RSHIFT(s->w, hsub);
        int dst_h = AV_CEIL_RSHIFT(s->h, vsub);

        int y_plane_offset = AV_CEIL_RSHIFT(s->y, vsub);
        int x_plane_offset = AV_CEIL_RSHIFT(s->x, hsub);

        if (x_plane_offset + src_w > dst_w || y_plane_offset + src_h > dst_h) {
            av_log(ctx, AV_LOG_ERROR,
                   "ROI out of bounds in plane %d: offset=(%d,%d) in=(%dx%d) "
                   "out=(%dx%d)\n",
                   plane, x_plane_offset, y_plane_offset, src_w, src_h, dst_w, dst_h);
            return AVERROR(EINVAL);
        }

        int dst_linesize = out->linesize[plane] / cur_comp->step;
        int src_linesize = in->linesize[plane] / cur_comp->step;

        CUdeviceptr d_dst = (CUdeviceptr)out->data[plane];
        CUdeviceptr d_src = (CUdeviceptr)in->data[plane];

        CUfunction cuda_func;

        if (cur_comp->step == 1 && cur_comp->depth == 8)
            cuda_func = s->cu_func_uchar;
        else if(cur_comp->step == 2 && cur_comp->depth == 8)
            cuda_func = s->cu_func_uchar2;
        else
            return AVERROR_BUG;

        void *kernel_args[] = {
            &d_dst, &dst_linesize, &dst_w, &dst_h,
            &d_src, &src_linesize, &src_w, &src_h,
            &x_plane_offset, &y_plane_offset, &s->parsed_color[plane]
        };

        unsigned int grid_x = DIV_UP(dst_w, BLOCK_X);
        unsigned int grid_y = DIV_UP(dst_h, BLOCK_Y);

        ret = CHECK_CU(cu->cuLaunchKernel(cuda_func, grid_x, grid_y, 1,
                                          BLOCK_X, BLOCK_Y, 1,
                                          0, s->hwctx->stream, kernel_args, NULL));

        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed to launch kernel for plane %d\n", plane);
            return ret;
        }
    }

    return 0;
}

static int cuda_pad_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    CUDAPadContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    FilterLink *outl = ff_filter_link(outlink);

    AVHWFramesContext *out_frames_ctx = (AVHWFramesContext *)outl->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = out_frames_ctx->device_ctx->hwctx;

    int ret;

    if (s->eval_mode == EVAL_MODE_FRAME) {
        s->in_w   = in->width;
        s->in_h   = in->height;
        s->aspect = in->sample_aspect_ratio;

        ret = eval_expr(ctx);
        if (ret < 0) {
            av_frame_free(&in);
            return ret;
        }
    }


    if (s->x == 0 && s->y == 0 &&
        s->w == in->width && s->h == in->height) {
        av_log(ctx, AV_LOG_DEBUG, "No border. Passing the frame unmodified.\n");
        s->last_out_w = s->w;
        s->last_out_h = s->h;
        return ff_filter_frame(outlink, in);
    }


    if (s->w != s->last_out_w || s->h != s->last_out_h) {

        av_buffer_unref(&s->frames_ctx);

        ret = cuda_pad_alloc_out_frames_ctx(ctx, &s->frames_ctx, s->w, s->h);
        if (ret < 0)
            return ret;

        av_buffer_unref(&outl->hw_frames_ctx);
        outl->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
        if (!outl->hw_frames_ctx) {
            av_frame_free(&in);
            av_log(ctx, AV_LOG_ERROR, "Failed to allocate output frame context.\n");
            return AVERROR(ENOMEM);
        }
        outlink->w = s->w;
        outlink->h = s->h;

        s->last_out_w = s->w;
        s->last_out_h = s->h;
    }

    AVFrame *out = av_frame_alloc();
    if (!out) {
        av_frame_free(&in);
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate output AVFrame.\n");
        return AVERROR(ENOMEM);
    }
    ret = av_hwframe_get_buffer(outl->hw_frames_ctx, out, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Unable to get output buffer: %s\n",
               av_err2str(ret));
        av_frame_free(&out);
        av_frame_free(&in);
        return ret;
    }

    CUcontext dummy;
    ret = CHECK_CU(device_hwctx->internal->cuda_dl->cuCtxPushCurrent(
        device_hwctx->cuda_ctx));
    if (ret < 0) {
        av_frame_free(&out);
        av_frame_free(&in);
        return ret;
    }

    ret = cuda_pad_pad(ctx, out, in);

    CHECK_CU(device_hwctx->internal->cuda_dl->cuCtxPopCurrent(&dummy));

    if (ret < 0) {
        av_frame_free(&out);
        av_frame_free(&in);
        return ret;
    }

    av_frame_copy_props(out, in);
    out->width  = s->w;
    out->height = s->h;


    av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
              (int64_t)in->sample_aspect_ratio.num * out->height * in->width,
              (int64_t)in->sample_aspect_ratio.den * out->width * in->height,
              INT_MAX);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(CUDAPadContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption cuda_pad_options[] = {
    { "width",  "set the pad area width expression",                             OFFSET(w_expr),     AV_OPT_TYPE_STRING,   {.str = "iw"},       0, 0,        FLAGS },
    { "w",      "set the pad area width expression",                             OFFSET(w_expr),     AV_OPT_TYPE_STRING,   {.str = "iw"},       0, 0,        FLAGS },
    { "height", "set the pad area height expression",                            OFFSET(h_expr),     AV_OPT_TYPE_STRING,   {.str = "ih"},       0, 0,        FLAGS },
    { "h",      "set the pad area height expression",                            OFFSET(h_expr),     AV_OPT_TYPE_STRING,   {.str = "ih"},       0, 0,        FLAGS },
    { "x",      "set the x offset expression for the input image position",      OFFSET(x_expr),     AV_OPT_TYPE_STRING,   {.str = "0"},        0, 0,        FLAGS },
    { "y",      "set the y offset expression for the input image position",      OFFSET(y_expr),     AV_OPT_TYPE_STRING,   {.str = "0"},        0, 0,        FLAGS },
    { "color",  "set the color of the padded area border",                       OFFSET(rgba_color), AV_OPT_TYPE_COLOR,    {.str = "black"},    .flags =      FLAGS },
    { "eval",   "specify when to evaluate expressions",                          OFFSET(eval_mode),  AV_OPT_TYPE_INT,      {.i64 = EVAL_MODE_INIT}, 0, EVAL_MODE_NB-1, FLAGS, .unit = "eval" },
         { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_INIT},  .flags = FLAGS, .unit = "eval" },
         { "frame", "eval expressions during initialization and per-frame", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = FLAGS, .unit = "eval" },
    { "aspect", "pad to fit an aspect instead of a resolution",                  OFFSET(aspect),     AV_OPT_TYPE_RATIONAL, {.dbl = 0},        0, DBL_MAX,    FLAGS },
    { NULL }
};

static const AVClass cuda_pad_class = {
    .class_name = "pad_cuda",
    .item_name  = av_default_item_name,
    .option     = cuda_pad_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad cuda_pad_inputs[] = {{
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = cuda_pad_filter_frame
}};

static const AVFilterPad cuda_pad_outputs[] = {{
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = cuda_pad_config_props,
}};

const FFFilter ff_vf_pad_cuda = {
    .p.name         = "pad_cuda",
    .p.description  = NULL_IF_CONFIG_SMALL("CUDA-based GPU padding filter"),
    .init           = cuda_pad_init,
    .uninit         = cuda_pad_uninit,

    .p.priv_class   = &cuda_pad_class,

    FILTER_INPUTS(cuda_pad_inputs),
    FILTER_OUTPUTS(cuda_pad_outputs),

    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),

    .priv_size      = sizeof(CUDAPadContext),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
