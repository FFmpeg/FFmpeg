/*
* Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <float.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/refstruct.h"

#include "libswscale/filters.h"

#include "avfilter.h"
#include "filters.h"
#include "scale_eval.h"
#include "video.h"

#include "cuda/load_helper.h"
#include "vf_scale_cuda.h"

struct format_entry {
    enum AVPixelFormat format;
    char name[13];
};

static const struct format_entry supported_formats[] = {
    {AV_PIX_FMT_YUV420P,  "planar8"},
    {AV_PIX_FMT_YUV422P,  "planar8"},
    {AV_PIX_FMT_YUV444P,  "planar8"},
    {AV_PIX_FMT_YUV420P10,"planar10"},
    {AV_PIX_FMT_YUV422P10,"planar10"},
    {AV_PIX_FMT_YUV444P10,"planar10"},
    {AV_PIX_FMT_YUV444P10MSB,"planar16"},
    {AV_PIX_FMT_YUV444P12MSB,"planar16"},
    {AV_PIX_FMT_YUV444P16,"planar16"},
    {AV_PIX_FMT_NV12,     "semiplanar8"},
    {AV_PIX_FMT_NV16,     "semiplanar8"},
    {AV_PIX_FMT_P010,     "semiplanar10"},
    {AV_PIX_FMT_P210,     "semiplanar10"},
    {AV_PIX_FMT_P012,     "semiplanar16"},
    {AV_PIX_FMT_P212,     "semiplanar16"},
    {AV_PIX_FMT_P016,     "semiplanar16"},
    {AV_PIX_FMT_P216,     "semiplanar16"},
    {AV_PIX_FMT_0RGB32,   "bgr0"},
    {AV_PIX_FMT_0BGR32,   "rgb0"},
    {AV_PIX_FMT_RGB32,    "bgra"},
    {AV_PIX_FMT_BGR32,    "rgba"},
};

#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define BLOCKX 32
#define BLOCKY 16

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

enum {
    INTERP_ALGO_DEFAULT,

    INTERP_ALGO_NEAREST,
    INTERP_ALGO_BILINEAR,
    INTERP_ALGO_BICUBIC,
    INTERP_ALGO_LANCZOS,

    INTERP_ALGO_COUNT
};

enum {
    FILTER_OUT,
    FILTER_TMP,
    FILTER_NB,
};

enum {
    CUDA_SCALE_DIR_NONE = -1,
    CUDA_SCALE_DIR_X,
    CUDA_SCALE_DIR_Y,
};

enum {
    CUDA_SCALE_PLANE_PRIMARY = 1 << 0,
    CUDA_SCALE_PLANE_CHROMA  = 1 << 1,
};

typedef struct CUDAScalePassPlan {
    int dir[FILTER_NB];
    /* Plane groups whose sample grids change along each axis. */
    unsigned int x_planes;
    unsigned int y_planes;
} CUDAScalePassPlan;

typedef struct CUDAScaleFilter {
    CUdeviceptr weights; ///< float[dst_size][filter_size]
    CUdeviceptr offsets; ///< int[dst_size]
    int filter_size;
    int dst_size;
} CUDAScaleFilter;

typedef struct CUDATex {
    CUtexObject tex[4];
    CUdeviceptr data[4];
    int         linesize[4];
    int         width, height;
    int         log2_chroma_w, log2_chroma_h;
    int         crop_left, crop_top, crop_width, crop_height;
    int         color_range;
    int         external_data;
} CUDATex;

typedef struct CUDAScaleFilterSet {
    CUDAScaleFilter filters[FILTER_NB];
    CUDAScaleFilter filters_uv[FILTER_NB];
    CUDATex inter_tex;
    CUDAScalePassPlan pass_plan;
    unsigned int tmp_planes;
    int in_width, in_height;
    int normalize_crop;
} CUDAScaleFilterSet;

typedef struct CUDAScaleContext {
    const AVClass *class;

    AVCUDADeviceContext *hwctx;

    enum AVPixelFormat in_fmt, out_fmt;
    const AVPixFmtDescriptor *in_desc, *out_desc;
    int in_planes, out_planes;
    int in_plane_depths[4];
    int in_plane_channels[4];

    AVBufferRef *frames_ctx;
    AVFrame     *frame;

    AVFrame *tmp_frame;
    int passthrough;

    /**
     * Output sw format. AV_PIX_FMT_NONE for no conversion.
     */
    enum AVPixelFormat format;

    char *w_expr;               ///< width  expression string
    char *h_expr;               ///< height expression string

    int force_original_aspect_ratio;
    int force_divisible_by;
    int reset_sar;

    CUcontext   cu_ctx;
    CUmodule    cu_module;
    CUfunction  cu_func_fixed;
    CUfunction  cu_func_fixed_uv;
    CUfunction  cu_func_out[2];
    CUfunction  cu_func_out_uv[2];
    CUfunction  cu_func_tmp;
    CUfunction  cu_func_tmp_uv;
    CUstream    cu_stream;

    int interp_algo;
    int interp_use_linear;
    int interp_as_integer;

    CUDAScaleFilterSet filter_set;
    int use_filters;
    int use_filters_opt; /* -1 for auto */

    float param;
} CUDAScaleContext;

/*
 * Compare the unrounded plane scale. AV_CEIL_RSHIFT() can hide a change
 * in sample geometry for odd dimensions: 3px 4:4:4 -> 5px 4:2:0 has
 * three stored chroma samples on each side, but the virtual plane size
 * changes from 3 to 2.5 and still requires filtering.
 */
static int cudascale_plane_needs_scale(int in_size, int out_size,
                                       int in_sub, int out_sub)
{
    return (int64_t)out_size * (1 << in_sub) !=
           (int64_t)in_size  * (1 << out_sub);
}

static int cudascale_plane_is_downscaled(int in_size, int out_size,
                                         int in_sub, int out_sub)
{
    return (int64_t)out_size * (1 << in_sub) <
           (int64_t)in_size  * (1 << out_sub);
}

static double cudascale_plane_virtual_size(int src_size,
                                           int in_size, int out_size,
                                           int in_sub, int out_sub)
{
    return src_size * ((double) out_size / in_size) *
           (1 << in_sub) / (1 << out_sub);
}

static void cudascale_plan_passes(CUDAScalePassPlan *plan,
                                  int in_width, int in_height,
                                  int out_width, int out_height,
                                  int in_sub_x, int in_sub_y,
                                  int out_sub_x, int out_sub_y,
                                  int has_chroma)
{
    unsigned int x_planes = 0;
    unsigned int y_planes = 0;

    *plan = (CUDAScalePassPlan) {
        .dir = { CUDA_SCALE_DIR_NONE, CUDA_SCALE_DIR_NONE },
    };

    if (cudascale_plane_needs_scale(in_width, out_width, 0, 0))
        x_planes |= CUDA_SCALE_PLANE_PRIMARY;
    if (cudascale_plane_needs_scale(in_height, out_height, 0, 0))
        y_planes |= CUDA_SCALE_PLANE_PRIMARY;

    if (has_chroma) {
        if (cudascale_plane_needs_scale(in_width, out_width,
                                        in_sub_x, out_sub_x))
            x_planes |= CUDA_SCALE_PLANE_CHROMA;
        if (cudascale_plane_needs_scale(in_height, out_height,
                                        in_sub_y, out_sub_y))
            y_planes |= CUDA_SCALE_PLANE_CHROMA;
    }

    plan->x_planes = x_planes;
    plan->y_planes = y_planes;

    if (x_planes && y_planes) {
        plan->dir[FILTER_TMP] = CUDA_SCALE_DIR_X;
        plan->dir[FILTER_OUT] = CUDA_SCALE_DIR_Y;
    } else if (x_planes) {
        plan->dir[FILTER_OUT] = CUDA_SCALE_DIR_X;
    } else if (y_planes) {
        plan->dir[FILTER_OUT] = CUDA_SCALE_DIR_Y;
    }
}

static av_cold int cudascale_init(AVFilterContext *ctx)
{
    CUDAScaleContext *s = ctx->priv;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    s->tmp_frame = av_frame_alloc();
    if (!s->tmp_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static void filter_uninit(CudaFunctions *cu, CUDAScaleFilter *filter)
{
    if (filter->weights)
        cu->cuMemFree(filter->weights);
    if (filter->offsets)
        cu->cuMemFree(filter->offsets);
    memset(filter, 0, sizeof(*filter));
}

static void cuda_tex_uninit(CudaFunctions *cu, CUDATex *t)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(t->tex); i++) {
        if (t->tex[i])
            cu->cuTexObjectDestroy(t->tex[i]);
        if (t->data[i] && !t->external_data)
            cu->cuMemFree(t->data[i]);
    }

    memset(t, 0, sizeof(*t));
}

static void filter_set_uninit(CudaFunctions *cu, CUDAScaleFilterSet *set)
{
    cuda_tex_uninit(cu, &set->inter_tex);
    for (int i = 0; i < FF_ARRAY_ELEMS(set->filters); i++) {
        filter_uninit(cu, &set->filters[i]);
        filter_uninit(cu, &set->filters_uv[i]);
    }
    memset(set, 0, sizeof(*set));
}

static av_cold void cudascale_uninit(AVFilterContext *ctx)
{
    CUDAScaleContext *s = ctx->priv;

    if (s->hwctx) {
        CudaFunctions *cu = s->hwctx->internal->cuda_dl;
        CUcontext dummy;

        CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));

        filter_set_uninit(cu, &s->filter_set);

        if (s->cu_module) {
            CHECK_CU(cu->cuModuleUnload(s->cu_module));
            s->cu_module = NULL;
        }

        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    }

    av_frame_free(&s->frame);
    av_buffer_unref(&s->frames_ctx);
    av_frame_free(&s->tmp_frame);
}

static av_cold int init_hwframe_ctx(CUDAScaleContext *s, AVBufferRef *device_ctx, int width, int height)
{
    AVBufferRef *out_ref = NULL;
    AVHWFramesContext *out_ctx;
    int ret;

    out_ref = av_hwframe_ctx_alloc(device_ctx);
    if (!out_ref)
        return AVERROR(ENOMEM);
    out_ctx = (AVHWFramesContext*)out_ref->data;

    out_ctx->format    = AV_PIX_FMT_CUDA;
    out_ctx->sw_format = s->out_fmt;
    out_ctx->width     = FFALIGN(width,  32);
    out_ctx->height    = FFALIGN(height, 32);

    ret = av_hwframe_ctx_init(out_ref);
    if (ret < 0)
        goto fail;

    av_frame_unref(s->frame);
    ret = av_hwframe_get_buffer(out_ref, s->frame, 0);
    if (ret < 0)
        goto fail;

    s->frame->width  = width;
    s->frame->height = height;

    av_buffer_unref(&s->frames_ctx);
    s->frames_ctx = out_ref;

    return 0;
fail:
    av_buffer_unref(&out_ref);
    return ret;
}

static int inter_buf_init(AVFilterContext *ctx, CUDATex *tex,
                          int out_width, int in_height,
                          int use_float, unsigned int planes)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    int ret = 0;

    *tex = (CUDATex) {
        .width          = out_width,
        .height         = in_height,
        .crop_width     = out_width,
        .crop_height    = in_height,
        .log2_chroma_w  = s->out_desc->log2_chroma_w,
        .log2_chroma_h  = s->in_desc->log2_chroma_h,
    };

    for (int i = 0; i < s->in_planes; i++) {
        const int is_chroma = i == 1 || i == 2;
        const unsigned int plane = is_chroma ? CUDA_SCALE_PLANE_CHROMA :
                                               CUDA_SCALE_PLANE_PRIMARY;
        const int sub_x   = is_chroma ? tex->log2_chroma_w : 0;
        const int sub_y   = is_chroma ? tex->log2_chroma_h : 0;
        const int plane_w = AV_CEIL_RSHIFT(out_width, sub_x);
        const int plane_h = AV_CEIL_RSHIFT(in_height, sub_y);
        const int sizeof_pixel = (use_float ? sizeof(float) :
                                  s->in_plane_depths[i] <= 8 ? 1 : 2) *
                                  s->in_plane_channels[i];

        if (!(planes & plane))
            continue;

        size_t pitch;
        ret = CHECK_CU(cu->cuMemAllocPitch(&tex->data[i], &pitch,
                                           (size_t) plane_w * sizeof_pixel,
                                           plane_h, 16));
        if (ret < 0)
            goto fail;
        tex->linesize[i] = pitch;

        CUDA_TEXTURE_DESC tex_desc = {
            /* inter tex is always read as float */
            .filterMode = CU_TR_FILTER_MODE_POINT,
        };

        CUDA_RESOURCE_DESC res_desc = {
            .resType = CU_RESOURCE_TYPE_PITCH2D,
            .res.pitch2D.format = use_float ? CU_AD_FORMAT_FLOAT :
                                  s->in_plane_depths[i] <= 8 ?
                                  CU_AD_FORMAT_UNSIGNED_INT8 :
                                  CU_AD_FORMAT_UNSIGNED_INT16,
            .res.pitch2D.numChannels  = s->in_plane_channels[i],
            .res.pitch2D.devPtr       = tex->data[i],
            .res.pitch2D.pitchInBytes = pitch,
            .res.pitch2D.width        = plane_w,
            .res.pitch2D.height       = plane_h,
        };

        ret = CHECK_CU(cu->cuTexObjectCreate(&tex->tex[i], &res_desc,
                                             &tex_desc, NULL));
        if (ret < 0)
            goto fail;
    }

    return 0;

fail:
    cuda_tex_uninit(cu, tex);
    return ret;
}

static int format_is_supported(enum AVPixelFormat fmt)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i].format == fmt)
            return 1;
    return 0;
}

static const char* get_format_name(enum AVPixelFormat fmt)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i].format == fmt)
            return supported_formats[i].name;
    return NULL;
}

static av_cold void set_format_info(AVFilterContext *ctx, enum AVPixelFormat in_format, enum AVPixelFormat out_format)
{
    CUDAScaleContext *s = ctx->priv;
    int i, p, d;

    s->in_fmt = in_format;
    s->out_fmt = out_format;

    s->in_desc  = av_pix_fmt_desc_get(s->in_fmt);
    s->out_desc = av_pix_fmt_desc_get(s->out_fmt);
    s->in_planes  = av_pix_fmt_count_planes(s->in_fmt);
    s->out_planes = av_pix_fmt_count_planes(s->out_fmt);

    // find maximum step of each component of each plane
    // For our subset of formats, this should accurately tell us how many channels CUDA needs
    // i.e. 1 for Y plane, 2 for UV plane of NV12, 4 for single plane of RGB0 formats

    for (i = 0; i < s->in_desc->nb_components; i++) {
        d = (s->in_desc->comp[i].depth + 7) / 8;
        p = s->in_desc->comp[i].plane;
        s->in_plane_channels[p] = FFMAX(s->in_plane_channels[p], s->in_desc->comp[i].step / d);

        s->in_plane_depths[p] = s->in_desc->comp[i].depth;
    }
}

static av_cold int init_processing_chain(AVFilterContext *ctx, int in_width, int in_height,
                                         int out_width, int out_height)
{
    CUDAScaleContext *s = ctx->priv;
    FilterLink     *inl = ff_filter_link(ctx->inputs[0]);
    FilterLink    *outl = ff_filter_link(ctx->outputs[0]);

    AVHWFramesContext *in_frames_ctx;

    enum AVPixelFormat in_format;
    enum AVPixelFormat out_format;
    int ret;

    /* check that we have a hw context */
    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }
    in_frames_ctx = (AVHWFramesContext*)inl->hw_frames_ctx->data;
    in_format     = in_frames_ctx->sw_format;
    out_format    = (s->format == AV_PIX_FMT_NONE) ? in_format : s->format;

    if (!format_is_supported(in_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n",
               av_get_pix_fmt_name(in_format));
        return AVERROR(ENOSYS);
    }
    if (!format_is_supported(out_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported output format: %s\n",
               av_get_pix_fmt_name(out_format));
        return AVERROR(ENOSYS);
    }

    set_format_info(ctx, in_format, out_format);
    if (s->passthrough && in_width == out_width && in_height == out_height && in_format == out_format) {
        s->frames_ctx = av_buffer_ref(inl->hw_frames_ctx);
        if (!s->frames_ctx)
            return AVERROR(ENOMEM);

        s->use_filters = 0;
    } else {
        s->passthrough = 0;

        ret = init_hwframe_ctx(s, in_frames_ctx->device_ref, out_width, out_height);
        if (ret < 0)
            return ret;

        if (in_width == out_width && in_height == out_height &&
            in_format == out_format && s->interp_algo == INTERP_ALGO_DEFAULT &&
            s->use_filters_opt != 1)
            s->interp_algo = INTERP_ALGO_NEAREST;

        if (s->interp_algo == INTERP_ALGO_NEAREST) {
            s->use_filters = 0;
        } else if (s->use_filters_opt >= 0) {
            s->use_filters = s->use_filters_opt;
        } else {
            /* Lanczos needs the generic path for its full windowed-sinc
             * kernel. Other algorithms need it when downscaling for correct
             * anti-aliasing. */
            s->use_filters = s->interp_algo == INTERP_ALGO_LANCZOS ||
                             cudascale_plane_is_downscaled(in_width, out_width,
                                                           0, 0) ||
                             cudascale_plane_is_downscaled(in_height, out_height,
                                                           0, 0) ||
                             (s->in_planes > 1 && s->out_planes > 1 &&
                              (cudascale_plane_is_downscaled(
                                   in_width, out_width,
                                   s->in_desc->log2_chroma_w,
                                   s->out_desc->log2_chroma_w) ||
                               cudascale_plane_is_downscaled(
                                   in_height, out_height,
                                   s->in_desc->log2_chroma_h,
                                   s->out_desc->log2_chroma_h)));
        }
    }

    outl->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int cudascale_load_functions(AVFilterContext *ctx)
{
    CUDAScaleContext *s = ctx->priv;
    CUcontext dummy, cuda_ctx = s->hwctx->cuda_ctx;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    char buf[128];
    int ret;

    const char *in_fmt_name = get_format_name(s->in_fmt);
    const char *out_fmt_name = get_format_name(s->out_fmt);

    const char *fixed_infix;
    int fixed_use_linear;
    int fixed_as_integer;

    extern const unsigned char ff_vf_scale_cuda_ptx_data[];
    extern const unsigned int ff_vf_scale_cuda_ptx_len;

    switch (s->interp_algo) {
    case INTERP_ALGO_NEAREST:
        fixed_infix = "Nearest";
        fixed_use_linear = 0;
        fixed_as_integer = 1;
        break;
    case INTERP_ALGO_BILINEAR:
        fixed_infix = "Bilinear";
        fixed_use_linear = 1;
        fixed_as_integer = 1;
        break;
    case INTERP_ALGO_DEFAULT:
    case INTERP_ALGO_BICUBIC:
        fixed_infix = "Bicubic";
        fixed_use_linear = 0;
        fixed_as_integer = 0;
        break;
    case INTERP_ALGO_LANCZOS:
        fixed_infix = "Lanczos";
        fixed_use_linear = 0;
        fixed_as_integer = 0;
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Unknown interpolation algorithm\n");
        return AVERROR_BUG;
    }

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, s->hwctx, &s->cu_module,
                              ff_vf_scale_cuda_ptx_data, ff_vf_scale_cuda_ptx_len);
    if (ret < 0)
        goto fail;

    if (s->use_filters) {
        static const char *const infix[] = { "Generic_h", "Generic_v" };
        const char *tmp_infix = s->interp_algo == INTERP_ALGO_LANCZOS ?
                                "Generic_float_h" : "Generic_h";

        s->interp_use_linear = 0;
        s->interp_as_integer = 0;

        for (int dir = CUDA_SCALE_DIR_X; dir <= CUDA_SCALE_DIR_Y; dir++) {
            snprintf(buf, sizeof(buf), "Subsample_%s_%s_%s",
                     infix[dir], in_fmt_name, out_fmt_name);
            ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_out[dir],
                                                   s->cu_module, buf));
            if (ret < 0)
                goto unsupported;

            snprintf(buf, sizeof(buf), "Subsample_%s_%s_%s_uv",
                     infix[dir], in_fmt_name, out_fmt_name);
            ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_out_uv[dir],
                                                   s->cu_module, buf));
            if (ret < 0)
                goto unsupported;
        }

        snprintf(buf, sizeof(buf), "Subsample_%s_%s_%s", tmp_infix,
                 in_fmt_name, in_fmt_name);
        ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_tmp,
                                               s->cu_module, buf));
        if (ret < 0)
            goto unsupported;

        if (s->in_planes > 1) {
            snprintf(buf, sizeof(buf), "Subsample_%s_%s_%s_uv", tmp_infix,
                     in_fmt_name, in_fmt_name);
            ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_tmp_uv,
                                                   s->cu_module, buf));
            if (ret < 0)
                goto unsupported;
        }
    } else {
        s->interp_use_linear = fixed_use_linear;
        s->interp_as_integer = fixed_as_integer;

        snprintf(buf, sizeof(buf), "Subsample_%s_%s_%s", fixed_infix,
                 in_fmt_name, out_fmt_name);
        ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_fixed,
                                               s->cu_module, buf));
        if (ret < 0)
            goto unsupported;

        snprintf(buf, sizeof(buf), "Subsample_%s_%s_%s_uv", fixed_infix,
                 in_fmt_name, out_fmt_name);
        ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_fixed_uv,
                                               s->cu_module, buf));
        if (ret < 0)
            goto unsupported;
    }

    goto fail;

unsupported:
    av_log(ctx, AV_LOG_FATAL, "Unsupported conversion: %s -> %s\n",
           in_fmt_name, out_fmt_name);
    ret = AVERROR(ENOSYS);

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    return ret;
}

static int cudascale_filter_init(AVFilterContext *ctx,
                                 CUDAScaleFilter *f,
                                 int src_size, int dst_size,
                                 double virtual_size, int needs_scale)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;

    SwsFilterParams params = {
        .scaler_params = { SWS_PARAM_DEFAULT, SWS_PARAM_DEFAULT },
        .src_size      = src_size,
        .dst_size      = dst_size,
        .virtual_size  = virtual_size,
    };

    if (!needs_scale) {
        params.scaler = SWS_SCALE_POINT;
        params.virtual_size = 0.0;
    } else {
        switch (s->interp_algo) {
        case INTERP_ALGO_NEAREST:  return 0; /* no weights needed */
        case INTERP_ALGO_BILINEAR: params.scaler = SWS_SCALE_BILINEAR; break;
        case INTERP_ALGO_LANCZOS:
            params.scaler = SWS_SCALE_LANCZOS;
            if (s->param != SCALE_CUDA_PARAM_DEFAULT)
                params.scaler_params[0] = s->param;
            break;
        case INTERP_ALGO_DEFAULT:
        case INTERP_ALGO_BICUBIC:
            params.scaler = SWS_SCALE_BICUBIC;
            params.scaler_params[0] = params.scaler_params[1] = 0.0;
            if (s->param != SCALE_CUDA_PARAM_DEFAULT)
                params.scaler_params[1] = s->param;
            break;
        }
    }

    SwsFilterWeights *weights = NULL;
    int ret = ff_sws_filter_generate(ctx, &params, &weights);
    if (ret < 0) {
        if (ret == AVERROR(ENOTSUP)) {
            av_log(ctx, AV_LOG_ERROR, "Filter size exceeds the maximum "
                   "currently supported by the CUDA scaler (%d).\n",
                   SWS_FILTER_SIZE_MAX);
        }
        return ret;
    }

    float *tmp = av_malloc_array(weights->num_weights, sizeof(*tmp));
    if (!tmp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    for (size_t i = 0; i < weights->num_weights; i++)
        tmp[i] = weights->weights[i] / (float) SWS_FILTER_SCALE;

    f->filter_size = weights->filter_size;
    f->dst_size    = dst_size;

    const size_t weights_size = weights->num_weights * sizeof(*tmp);
    ret = CHECK_CU(cu->cuMemAlloc(&f->weights, weights_size));
    if (ret < 0)
        goto fail;
    ret = CHECK_CU(cu->cuMemcpyHtoD(f->weights, tmp, weights_size));
    if (ret < 0)
        goto fail;

    const size_t offsets_size = dst_size * sizeof(*weights->offsets);
    ret = CHECK_CU(cu->cuMemAlloc(&f->offsets, offsets_size));
    if (ret < 0)
        goto fail;
    ret = CHECK_CU(cu->cuMemcpyHtoD(f->offsets, weights->offsets, offsets_size));
    if (ret < 0)
        goto fail;

    av_log(ctx, AV_LOG_VERBOSE, "  using %d tap '%s' filter: %d -> %d\n",
           f->filter_size, weights->name, src_size, dst_size);

    ret = 0;

fail:
    av_free(tmp);
    av_refstruct_unref(&weights);
    return ret;
}

static int cudascale_filter_set_init(AVFilterContext *ctx,
                                     CUDAScaleFilterSet *set,
                                     int in_width, int in_height,
                                     int normalize_crop)
{
    CUDAScaleContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    const int in_sub_x  = s->in_desc->log2_chroma_w;
    const int in_sub_y  = s->in_desc->log2_chroma_h;
    const int out_sub_x = s->out_desc->log2_chroma_w;
    const int out_sub_y = s->out_desc->log2_chroma_h;

    const int has_chroma = s->in_planes > 1 && s->out_planes > 1;
    const unsigned int all_planes = CUDA_SCALE_PLANE_PRIMARY |
                                    (has_chroma ? CUDA_SCALE_PLANE_CHROMA : 0);
    int pass_x, pass_y;

    set->in_width       = in_width;
    set->in_height      = in_height;
    set->normalize_crop = normalize_crop;

    cudascale_plan_passes(&set->pass_plan,
                          in_width, in_height, outlink->w, outlink->h,
                          in_sub_x, in_sub_y, out_sub_x, out_sub_y,
                          has_chroma);

    /* The generic path also handles non-scaling copies and format
     * conversions with a one-tap horizontal filter. */
    if (set->pass_plan.dir[FILTER_OUT] == CUDA_SCALE_DIR_NONE)
        set->pass_plan.dir[FILTER_OUT] = CUDA_SCALE_DIR_X;

    pass_x = set->pass_plan.dir[FILTER_TMP] == CUDA_SCALE_DIR_X ?
             FILTER_TMP :
             set->pass_plan.dir[FILTER_OUT] == CUDA_SCALE_DIR_X ?
             FILTER_OUT : -1;
    pass_y = set->pass_plan.dir[FILTER_OUT] == CUDA_SCALE_DIR_Y ?
             FILTER_OUT : -1;

    if (pass_x >= 0) {
        const unsigned int planes = pass_x == FILTER_TMP ?
                                    (normalize_crop ? all_planes :
                                                      set->pass_plan.x_planes) :
                                    all_planes;

        if (pass_x == FILTER_TMP)
            set->tmp_planes = planes;

        if (planes & CUDA_SCALE_PLANE_PRIMARY) {
            ret = cudascale_filter_init(ctx, &set->filters[pass_x],
                                        in_width, outlink->w, 0.0,
                                        !!(set->pass_plan.x_planes &
                                           CUDA_SCALE_PLANE_PRIMARY));
            if (ret < 0)
                goto fail;
        }
        if (planes & CUDA_SCALE_PLANE_CHROMA) {
            const int src_size = AV_CEIL_RSHIFT(in_width,  in_sub_x);
            const int dst_size = AV_CEIL_RSHIFT(outlink->w, out_sub_x);
            const double virtual_size = cudascale_plane_virtual_size(
                src_size, in_width, outlink->w, in_sub_x, out_sub_x);
            ret = cudascale_filter_init(ctx, &set->filters_uv[pass_x],
                                        src_size, dst_size, virtual_size,
                                        !!(set->pass_plan.x_planes &
                                           CUDA_SCALE_PLANE_CHROMA));
            if (ret < 0)
                goto fail;
        }
    }

    if (pass_y >= 0) {
        ret = cudascale_filter_init(ctx, &set->filters[pass_y],
                                    in_height, outlink->h, 0.0,
                                    !!(set->pass_plan.y_planes &
                                       CUDA_SCALE_PLANE_PRIMARY));
        if (ret < 0)
            goto fail;
        if (all_planes & CUDA_SCALE_PLANE_CHROMA) {
            const int src_size = AV_CEIL_RSHIFT(in_height,  in_sub_y);
            const int dst_size = AV_CEIL_RSHIFT(outlink->h, out_sub_y);
            const double virtual_size = cudascale_plane_virtual_size(
                src_size, in_height, outlink->h, in_sub_y, out_sub_y);
            ret = cudascale_filter_init(ctx, &set->filters_uv[pass_y],
                                        src_size, dst_size, virtual_size,
                                        !!(set->pass_plan.y_planes &
                                           CUDA_SCALE_PLANE_CHROMA));
            if (ret < 0)
                goto fail;
        }
    }

    if (pass_x == FILTER_TMP) {
        ret = inter_buf_init(ctx, &set->inter_tex,
                             outlink->w, in_height,
                             s->interp_algo == INTERP_ALGO_LANCZOS,
                             set->tmp_planes);
        if (ret < 0)
            goto fail;
    }

    ret = 0;

fail:
    return ret;
}

static int cudascale_prepare_filter_set(AVFilterContext *ctx,
                                        int in_width, int in_height,
                                        int normalize_crop)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    CUDAScaleFilterSet next = { 0 };
    CUDAScaleFilterSet old;
    int ret;

    if (s->filter_set.in_width == in_width &&
        s->filter_set.in_height == in_height &&
        s->filter_set.normalize_crop == normalize_crop)
        return 0;

    ret = cudascale_filter_set_init(ctx, &next, in_width, in_height,
                                    normalize_crop);
    if (ret < 0)
        goto fail;

    if (s->filter_set.in_width) {
        /* Queued kernels may still reference the active LUTs and texture. */
        ret = CHECK_CU(cu->cuStreamSynchronize(s->cu_stream));
        if (ret < 0)
            goto fail;
    }

    old = s->filter_set;
    s->filter_set = next;
    memset(&next, 0, sizeof(next));
    filter_set_uninit(cu, &old);

    av_log(ctx, AV_LOG_VERBOSE,
           "Prepared generic filters for visible input %dx%d%s\n",
           in_width, in_height, normalize_crop ? " (cropped)" : "");
    return 0;

fail:
    filter_set_uninit(cu, &next);
    return ret;
}

static av_cold int cudascale_setup_filters(AVFilterContext *ctx,
                                           int in_width, int in_height)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    CUcontext dummy;
    int ret;

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
    if (ret < 0)
        return ret;

    ret = cudascale_prepare_filter_set(ctx, in_width, in_height, 0);

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ret;
}

static av_cold int cudascale_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    CUDAScaleContext *s  = ctx->priv;
    AVHWFramesContext     *frames_ctx;
    AVCUDADeviceContext *device_hwctx;
    int w, h;
    double w_adj = 1.0;
    int ret;

    if ((ret = ff_scale_eval_dimensions(s,
                                        s->w_expr, s->h_expr,
                                        inlink, outlink,
                                        &w, &h)) < 0)
        goto fail;

    if (s->reset_sar)
        w_adj = inlink->sample_aspect_ratio.num ?
        (double)inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;

    ret = ff_scale_adjust_dimensions(inlink, &w, &h,
                                     s->force_original_aspect_ratio,
                                     s->force_divisible_by, w_adj);
    if (ret < 0)
        goto fail;

    if (((int64_t)h * inlink->w) > INT_MAX  ||
        ((int64_t)w * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Rescaled value for width or height is too big.\n");

    outlink->w = w;
    outlink->h = h;

    ret = init_processing_chain(ctx, inlink->w, inlink->h, w, h);
    if (ret < 0)
        return ret;

    frames_ctx   = (AVHWFramesContext*)inl->hw_frames_ctx->data;
    device_hwctx = frames_ctx->device_ctx->hwctx;

    s->hwctx = device_hwctx;
    s->cu_stream = s->hwctx->stream;

    if (s->reset_sar)
        outlink->sample_aspect_ratio = (AVRational){1, 1};
    else if (inlink->sample_aspect_ratio.num) {
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h*inlink->w,
                                                             outlink->w*inlink->h},
                                                inlink->sample_aspect_ratio);
    } else {
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    }

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d fmt:%s -> w:%d h:%d fmt:%s%s\n",
           inlink->w, inlink->h, av_get_pix_fmt_name(s->in_fmt),
           outlink->w, outlink->h, av_get_pix_fmt_name(s->out_fmt),
           s->passthrough ? " (passthrough)" : "");

    if (s->use_filters) {
        ret = cudascale_setup_filters(ctx, inlink->w, inlink->h);
        if (ret < 0)
            return ret;
    }

    ret = cudascale_load_functions(ctx);
    if (ret < 0)
        return ret;

    return 0;

fail:
    return ret;
}

static int cudascale_frame_geometry(AVFilterContext *ctx, const AVFrame *frame,
                                    int *width, int *height,
                                    int *normalize_crop)
{
    AVFilterLink *inlink = ctx->inputs[0];
    size_t frame_width, frame_height;

    if (frame->width <= 0 || frame->height <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid crop rectangle\n");
        return AVERROR(EINVAL);
    }

    frame_width  = frame->width;
    frame_height = frame->height;
    if (frame->crop_left >= frame_width ||
        frame->crop_right >= frame_width - frame->crop_left ||
        frame->crop_top >= frame_height ||
        frame->crop_bottom >= frame_height - frame->crop_top) {
        av_log(ctx, AV_LOG_ERROR, "Invalid crop rectangle\n");
        return AVERROR(EINVAL);
    }

    *width  = (int)(frame_width  - frame->crop_left - frame->crop_right);
    *height = (int)(frame_height - frame->crop_top  - frame->crop_bottom);
    *normalize_crop = frame->crop_left || frame->crop_top ||
                      *width != inlink->w || *height != inlink->h;

    return 0;
}

/* if depths/channels are NULL, only maps pointers without creating textures */
static int cuda_tex_map_frame(AVFilterContext *ctx, const AVFrame *frame,
                              const int depths[4], const int channels[4],
                              CUDATex *tex, int use_linear, int as_integer)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;

    const AVHWFramesContext *fctx = (const AVHWFramesContext*)frame->hw_frames_ctx->data;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fctx->sw_format);
    const int planes = av_pix_fmt_count_planes(fctx->sw_format);

    *tex = (CUDATex) {
        .width       = frame->width,
        .height      = frame->height,
        .crop_left   = frame->crop_left,
        .crop_top    = frame->crop_top,
        .crop_width  = (frame->width  - frame->crop_right)  - frame->crop_left,
        .crop_height = (frame->height - frame->crop_bottom) - frame->crop_top,
        .color_range = frame->color_range,
        .log2_chroma_w = desc->log2_chroma_w,
        .log2_chroma_h = desc->log2_chroma_h,
        .external_data = 1,
    };

    for (int i = 0; i < planes; i++) {
        tex->data[i]     = (CUdeviceptr)frame->data[i];
        tex->linesize[i] = frame->linesize[i];
        if (!depths || !channels)
            continue;

        CUDA_TEXTURE_DESC tex_desc = {
            .filterMode = use_linear ?
                          CU_TR_FILTER_MODE_LINEAR :
                          CU_TR_FILTER_MODE_POINT,
            .flags = as_integer ? CU_TRSF_READ_AS_INTEGER : 0,
        };

        const int is_chroma = i == 1 || i == 2;
        const int sub_x = is_chroma ? desc->log2_chroma_w : 0;
        const int sub_y = is_chroma ? desc->log2_chroma_h : 0;
        CUDA_RESOURCE_DESC res_desc = {
            .resType = CU_RESOURCE_TYPE_PITCH2D,
            .res.pitch2D.format = depths[i] <= 8 ?
                                  CU_AD_FORMAT_UNSIGNED_INT8 :
                                  CU_AD_FORMAT_UNSIGNED_INT16,
            .res.pitch2D.numChannels = channels[i],
            .res.pitch2D.pitchInBytes = tex->linesize[i],
            .res.pitch2D.devPtr = tex->data[i],
            .res.pitch2D.width  = AV_CEIL_RSHIFT(frame->width,  sub_x),
            .res.pitch2D.height = AV_CEIL_RSHIFT(frame->height, sub_y),
        };

        int ret = CHECK_CU(cu->cuTexObjectCreate(&tex->tex[i], &res_desc, &tex_desc, NULL));
        if (ret < 0) {
            cuda_tex_uninit(cu, tex);
            return ret;
        }
    }

    return 0;
}

static int call_resize_kernel(AVFilterContext *ctx, CUfunction func,
                              const CUtexObject src_tex[4],
                              int src_left, int src_top, int src_width, int src_height,
                              const CUdeviceptr out_data[4],
                              int dst_width, int dst_height, int dst_pitch, int mpeg_range,
                              const CUDAScaleFilter *filter)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;

    CUDAScaleKernelParams params = {
        .src_tex = {src_tex[0], src_tex[1], src_tex[2], src_tex[3]},
        .dst = {
            out_data[0],
            out_data[1],
            out_data[2],
            out_data[3]
        },
        .dst_width = dst_width,
        .dst_height = dst_height,
        .dst_pitch = dst_pitch,
        .src_left = src_left,
        .src_top = src_top,
        .src_width = src_width,
        .src_height = src_height,
        .param = s->param,
        .mpeg_range = mpeg_range,
        /* Supported input formats use the same depth and shift for
         * every component. */
        .src_depth = s->in_desc->comp[0].depth,
        .src_storage_max = ((1U << s->in_desc->comp[0].depth) - 1) <<
                           s->in_desc->comp[0].shift,
    };

    if (filter) {
        params.weights = filter->weights;
        params.offsets = filter->offsets;
        params.filter_size = filter->filter_size;
    }

    void *args[] = { &params };

    return CHECK_CU(cu->cuLaunchKernel(func,
                                       DIV_UP(dst_width, BLOCKX), DIV_UP(dst_height, BLOCKY), 1,
                                       BLOCKX, BLOCKY, 1, 0, s->cu_stream, args, NULL));
}

static int scalecuda_resize(AVFilterContext *ctx,
                            const CUDAScaleFilterSet *set, int pass,
                            const CUDATex *out, const CUDATex *in)
{
    CUDAScaleContext *s = ctx->priv;
    const CUDAScaleFilter *filter = NULL;
    const CUDAScaleFilter *filter_uv = NULL;
    CUfunction func, func_uv;
    int mpeg_range = in->color_range != AVCOL_RANGE_JPEG;
    int ret;

    int out_planes = s->out_planes;
    if (pass == FILTER_TMP) {
        out_planes = s->in_planes;
        func    = s->cu_func_tmp;
        func_uv = s->cu_func_tmp_uv;
    } else if (s->use_filters) {
        const int dir = set->pass_plan.dir[FILTER_OUT];

        av_assert0(dir == CUDA_SCALE_DIR_X || dir == CUDA_SCALE_DIR_Y);
        func    = s->cu_func_out[dir];
        func_uv = s->cu_func_out_uv[dir];
    } else {
        func    = s->cu_func_fixed;
        func_uv = s->cu_func_fixed_uv;
    }

    if (s->use_filters) {
        filter    = &set->filters[pass];
        filter_uv = &set->filters_uv[pass];
    }

    if (pass != FILTER_TMP ||
        (set->tmp_planes & CUDA_SCALE_PLANE_PRIMARY)) {
        // scale primary plane(s). Usually Y (and A), or single plane of RGB frames.
        ret = call_resize_kernel(ctx, func,
                                 in->tex, in->crop_left, in->crop_top,
                                 in->crop_width, in->crop_height,
                                 out->data, out->width, out->height,
                                 out->linesize[0], mpeg_range, filter);
        if (ret < 0)
            return ret;
    }

    if (out_planes > 1 &&
        (pass != FILTER_TMP ||
         (set->tmp_planes & CUDA_SCALE_PLANE_CHROMA))) {
        // scale UV plane. Scale function sets both U and V plane, or singular interleaved plane.
        /* Match av_frame_apply_cropping(): subsampled plane origins round
         * down, while visible plane extents round up. */
        ret = call_resize_kernel(ctx, func_uv, in->tex,
                                 in->crop_left >> in->log2_chroma_w,
                                 in->crop_top  >> in->log2_chroma_h,
                                 AV_CEIL_RSHIFT(in->crop_width, in->log2_chroma_w),
                                 AV_CEIL_RSHIFT(in->crop_height, in->log2_chroma_h),
                                 out->data,
                                 AV_CEIL_RSHIFT(out->width, out->log2_chroma_w),
                                 AV_CEIL_RSHIFT(out->height, out->log2_chroma_h),
                                 out->linesize[1], mpeg_range, filter_uv);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int cudascale_scale(AVFilterContext *ctx, AVFrame *out, AVFrame *in,
                           int in_width, int in_height, int normalize_crop)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret = 0;

    if (s->use_filters) {
        ret = cudascale_prepare_filter_set(ctx, in_width, in_height,
                                           normalize_crop);
        if (ret < 0)
            return ret;
    }

    CUDATex in_tex = {0}, out_tex = {0};
    ret = cuda_tex_map_frame(ctx, in, s->in_plane_depths,
                             s->in_plane_channels, &in_tex,
                             s->interp_use_linear, s->interp_as_integer);
    if (ret < 0)
        goto fail;

    ret = cuda_tex_map_frame(ctx, s->frame, NULL, NULL, &out_tex, 0, 0);
    if (ret < 0)
        goto fail;

    const CUDATex *src = &in_tex;
    CUDATex inter_tex;
    if (s->use_filters &&
        s->filter_set.pass_plan.dir[FILTER_TMP] != CUDA_SCALE_DIR_NONE) {
        /* Handle first pass separately */
        s->filter_set.inter_tex.color_range = in->color_range;
        inter_tex = s->filter_set.inter_tex;
        /* Reuse input textures for plane groups not touched by this pass. */
        for (int i = 0; i < s->in_planes; i++) {
            if (!inter_tex.tex[i])
                inter_tex.tex[i] = in_tex.tex[i];
        }
        ret = scalecuda_resize(ctx, &s->filter_set, FILTER_TMP,
                               &s->filter_set.inter_tex, src);
        if (ret < 0)
            goto fail;
        src = &inter_tex;
    }

    ret = scalecuda_resize(ctx, &s->filter_set, FILTER_OUT, &out_tex, src);
    if (ret < 0)
        goto fail;

    ret = av_hwframe_get_buffer(s->frame->hw_frames_ctx, s->tmp_frame, 0);
    if (ret < 0)
        goto fail;

    av_frame_move_ref(out, s->frame);
    av_frame_move_ref(s->frame, s->tmp_frame);

    s->frame->width  = outlink->w;
    s->frame->height = outlink->h;

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        goto fail;

    out->crop_top    = 0;
    out->crop_bottom = 0;
    out->crop_left   = 0;
    out->crop_right  = 0;

    if (in->crop_left || in->crop_right ||
        in->crop_top  || in->crop_bottom ||
        out->width != in_width || out->height != in_height) {
        av_frame_side_data_remove_by_props(&out->side_data, &out->nb_side_data,
                                           AV_SIDE_DATA_PROP_SIZE_DEPENDENT);
    }

fail:
    cuda_tex_uninit(cu, &in_tex);
    cuda_tex_uninit(cu, &out_tex);
    return ret;
}

static int cudascale_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext       *ctx = link->dst;
    CUDAScaleContext        *s = ctx->priv;
    AVFilterLink      *outlink = ctx->outputs[0];
    CudaFunctions          *cu = s->hwctx->internal->cuda_dl;

    AVFrame *out = NULL;
    CUcontext dummy;
    int in_width, in_height, normalize_crop;
    int ret = 0;

    if (s->passthrough)
        return ff_filter_frame(outlink, in);

    ret = cudascale_frame_geometry(ctx, in, &in_width, &in_height,
                                   &normalize_crop);
    if (ret < 0)
        goto fail;

    out = av_frame_alloc();
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
    if (ret < 0)
        goto fail;

    ret = cudascale_scale(ctx, out, in, in_width, in_height,
                          normalize_crop);

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    if (ret < 0)
        goto fail;

    if (s->reset_sar) {
        out->sample_aspect_ratio = (AVRational){1, 1};
    } else {
        av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
                  (int64_t)in->sample_aspect_ratio.num * outlink->h * in_width,
                  (int64_t)in->sample_aspect_ratio.den * outlink->w * in_height,
                  INT_MAX);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

static AVFrame *cudascale_get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    CUDAScaleContext *s = inlink->dst->priv;

    return s->passthrough ?
        ff_null_get_video_buffer   (inlink, w, h) :
        ff_default_get_video_buffer(inlink, w, h);
}

#define OFFSET(x) offsetof(CUDAScaleContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption options[] = {
    { "w", "Output video width",  OFFSET(w_expr), AV_OPT_TYPE_STRING, { .str = "iw" }, .flags = FLAGS },
    { "h", "Output video height", OFFSET(h_expr), AV_OPT_TYPE_STRING, { .str = "ih" }, .flags = FLAGS },
    { "interp_algo", "Interpolation algorithm used for resizing", OFFSET(interp_algo), AV_OPT_TYPE_INT, { .i64 = INTERP_ALGO_DEFAULT }, 0, INTERP_ALGO_COUNT - 1, FLAGS, .unit = "interp_algo" },
        { "nearest",  "nearest neighbour", 0, AV_OPT_TYPE_CONST, { .i64 = INTERP_ALGO_NEAREST }, 0, 0, FLAGS, .unit = "interp_algo" },
        { "bilinear", "bilinear", 0, AV_OPT_TYPE_CONST, { .i64 = INTERP_ALGO_BILINEAR }, 0, 0, FLAGS, .unit = "interp_algo" },
        { "bicubic",  "bicubic",  0, AV_OPT_TYPE_CONST, { .i64 = INTERP_ALGO_BICUBIC  }, 0, 0, FLAGS, .unit = "interp_algo" },
        { "lanczos",  "lanczos",  0, AV_OPT_TYPE_CONST, { .i64 = INTERP_ALGO_LANCZOS  }, 0, 0, FLAGS, .unit = "interp_algo" },
    { "format", "Output video pixel format", OFFSET(format), AV_OPT_TYPE_PIXEL_FMT, { .i64 = AV_PIX_FMT_NONE }, INT_MIN, INT_MAX, .flags=FLAGS },
    { "passthrough", "Do not process frames at all if parameters match", OFFSET(passthrough), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
    { "use_filters", "Use generic filters instead of fixed function kernels", OFFSET(use_filters_opt), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1, FLAGS, .unit = "use_filters" },
        { "auto",    NULL,  0, AV_OPT_TYPE_CONST, {.i64 = -1}, 0, 0, FLAGS, .unit = "use_filters" },
    { "param", "Algorithm-Specific parameter", OFFSET(param), AV_OPT_TYPE_FLOAT, { .dbl = SCALE_CUDA_PARAM_DEFAULT }, -FLT_MAX, FLT_MAX, FLAGS },
    { "force_original_aspect_ratio", "decrease or increase w/h if necessary to keep the original AR", OFFSET(force_original_aspect_ratio), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, SCALE_FORCE_OAR_NB-1, FLAGS, .unit = "force_oar" },
        { "disable",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = SCALE_FORCE_OAR_DISABLE  }, 0, 0, FLAGS, .unit = "force_oar" },
        { "decrease", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = SCALE_FORCE_OAR_DECREASE }, 0, 0, FLAGS, .unit = "force_oar" },
        { "increase", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = SCALE_FORCE_OAR_INCREASE }, 0, 0, FLAGS, .unit = "force_oar" },
    { "force_divisible_by", "enforce that the output resolution is divisible by a defined integer when force_original_aspect_ratio is used", OFFSET(force_divisible_by), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, 256, FLAGS },
    { "reset_sar", "reset SAR to 1 and scale to square pixels if scaling proportionally", OFFSET(reset_sar), AV_OPT_TYPE_BOOL, { .i64 = 0}, 0, 1, FLAGS },
    { NULL },
};

static const AVClass cudascale_class = {
    .class_name = "cudascale",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad cudascale_inputs[] = {
    {
        .name        = "default",
        .type        = AVMEDIA_TYPE_VIDEO,
        .filter_frame = cudascale_filter_frame,
        .get_buffer.video = cudascale_get_video_buffer,
    },
};

static const AVFilterPad cudascale_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = cudascale_config_props,
    },
};

const FFFilter ff_vf_scale_cuda = {
    .p.name        = "scale_cuda",
    .p.description = NULL_IF_CONFIG_SMALL("GPU accelerated video resizer"),

    .p.priv_class  = &cudascale_class,

    .init          = cudascale_init,
    .uninit        = cudascale_uninit,

    .priv_size = sizeof(CUDAScaleContext),

    FILTER_INPUTS(cudascale_inputs),
    FILTER_OUTPUTS(cudascale_outputs),

    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
