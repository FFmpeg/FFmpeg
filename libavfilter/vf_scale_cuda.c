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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/csp.h"
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
#include "colorspace.h"
#include "filters.h"
#include "formats.h"
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

/* Buffers a pass can read from or write to. */
enum {
    CUDA_SCALE_BUF_IN,
    CUDA_SCALE_BUF_TMP,
    CUDA_SCALE_BUF_TMP2,
    CUDA_SCALE_BUF_OUT,
    CUDA_SCALE_BUF_NB,
};

#define CUDA_SCALE_MAX_PASSES 3

/*
 * One kernel pass. Everything the kernels need beyond the geometry of the
 * source and destination buffers is described here, so that a pass does
 * not depend on its position in the chain.
 */
typedef struct CUDAScalePass {
    CUfunction func;            ///< kernel for the primary plane(s)
    CUfunction func_uv;         ///< kernel for the chroma plane(s)
    CUDAScaleFilter filter;     ///< generic filter weights for func
    CUDAScaleFilter filter_uv;  ///< generic filter weights for func_uv
    unsigned int planes;        ///< plane groups written, CUDA_SCALE_PLANE_*
    int src, dst;               ///< CUDA_SCALE_BUF_*
    const AVPixFmtDescriptor *src_desc; ///< sample format read
    const AVPixFmtDescriptor *dst_desc; ///< sample format written
} CUDAScalePass;

typedef struct CUDAScaleFilterSet {
    CUDAScalePass passes[CUDA_SCALE_MAX_PASSES];
    int nb_passes;
    CUDATex tmp[2];             ///< CUDA_SCALE_BUF_TMP, CUDA_SCALE_BUF_TMP2
    int in_use_linear;          ///< texture filtering of the input
    int in_as_integer;          ///< integer texture reads of the input
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
    CUfunction  cu_func_tmp_v;      ///< vertical float pass before RGB->YUV
    CUfunction  cu_func_conv;       ///< conversion with a float RGB side
    CUfunction  cu_func_conv_uv;
    CUstream    cu_stream;

    int interp_algo;
    int interp_use_linear;
    int interp_as_integer;

    CUDAScaleFilterSet filter_set;
    int use_filters;
    int use_filters_opt; /* -1 for auto */

    float param;

    bool yuv2rgb;
    bool rgb2yuv;
    int out_range;                   ///< requested output color range (AVColorRange)
    int out_color_matrix;            ///< requested output YCbCr matrix (AVColorSpace)
    enum AVColorRange  color_range;  ///< resolved color range for conversion
    CUDAScaleColorMatrix color_matrix;
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
    for (int i = 0; i < FF_ARRAY_ELEMS(set->tmp); i++)
        cuda_tex_uninit(cu, &set->tmp[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(set->passes); i++) {
        filter_uninit(cu, &set->passes[i].filter);
        filter_uninit(cu, &set->passes[i].filter_uv);
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
                          int width, int height,
                          int log2_chroma_w, int log2_chroma_h,
                          int nb_planes, const int depths[4],
                          const int channels[4],
                          int use_float, unsigned int planes)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    int ret = 0;

    *tex = (CUDATex) {
        .width          = width,
        .height         = height,
        .crop_width     = width,
        .crop_height    = height,
        .log2_chroma_w  = log2_chroma_w,
        .log2_chroma_h  = log2_chroma_h,
    };

    for (int i = 0; i < nb_planes; i++) {
        const int is_chroma = i == 1 || i == 2;
        const unsigned int plane = is_chroma ? CUDA_SCALE_PLANE_CHROMA :
                                               CUDA_SCALE_PLANE_PRIMARY;
        const int sub_x   = is_chroma ? tex->log2_chroma_w : 0;
        const int sub_y   = is_chroma ? tex->log2_chroma_h : 0;
        const int plane_w = AV_CEIL_RSHIFT(width,  sub_x);
        const int plane_h = AV_CEIL_RSHIFT(height, sub_y);
        const int sizeof_pixel = (use_float ? sizeof(float) :
                                  depths[i] <= 8 ? 1 : 2) * channels[i];

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
                                  depths[i] <= 8 ?
                                  CU_AD_FORMAT_UNSIGNED_INT8 :
                                  CU_AD_FORMAT_UNSIGNED_INT16,
            .res.pitch2D.numChannels  = channels[i],
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

/* H.273 YCgCo uses the luma excursion for chroma as well. */
static int chroma_uses_luma_span(enum AVColorSpace colorspace)
{
    return colorspace == AVCOL_SPC_YCGCO
        || colorspace == AVCOL_SPC_YCGCO_RE
        || colorspace == AVCOL_SPC_YCGCO_RO;
}

static int compute_yuv2rgb_matrix(void *log_ctx, CUDAScaleColorMatrix *mat,
                                  enum AVColorSpace colorspace,
                                  bool limited_range)
{
    const AVLumaCoefficients *coeffs = av_csp_luma_coeffs_from_avcsp(colorspace);
    float y_scale = 1.0f;
    float uv_scale = 1.0f;
    double rgb2yuv[3][3], yuv2rgb[3][3];

    /* Constant luminance is not the matrix returned for these coefficients. */
    if (!coeffs || colorspace == AVCOL_SPC_BT2020_CL) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Unsupported colorspace %d for YUV->RGB conversion\n", colorspace);
        return AVERROR(EINVAL);
    }

    ff_fill_rgb2yuv_table(coeffs, rgb2yuv);
    ff_matrix_invert_3x3(rgb2yuv, yuv2rgb);

    if (limited_range) {
        y_scale  = 255.0f / (235.0f - 16.0f);
        uv_scale = chroma_uses_luma_span(colorspace) ? y_scale
                                                     : 255.0f / (240.0f - 16.0f);
    }

    for (int i = 0; i < 3; i++) {
        mat->m[i][0] = (float)yuv2rgb[i][0] * y_scale;
        mat->m[i][1] = (float)yuv2rgb[i][1] * uv_scale;
        mat->m[i][2] = (float)yuv2rgb[i][2] * uv_scale;
    }

    return 0;
}

static int compute_rgb2yuv_matrix(void *log_ctx, CUDAScaleColorMatrix *mat,
                                  enum AVColorSpace colorspace,
                                  bool limited_range)
{
    const AVLumaCoefficients *coeffs = av_csp_luma_coeffs_from_avcsp(colorspace);
    double rgb2yuv[3][3];
    float y_scale = 1.0f;
    float uv_scale = 1.0f;

    if (!coeffs || colorspace == AVCOL_SPC_BT2020_CL) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Unsupported colorspace %d for RGB->YUV conversion\n", colorspace);
        return AVERROR(EINVAL);
    }

    ff_fill_rgb2yuv_table(coeffs, rgb2yuv);

    if (limited_range) {
        y_scale  = (235.0f - 16.0f) / 255.0f;
        uv_scale = chroma_uses_luma_span(colorspace) ? y_scale
                                                     : (240.0f - 16.0f) / 255.0f;
    }

    for (int i = 0; i < 3; i++) {
        float scale = (i == 0) ? y_scale : uv_scale;
        mat->m[i][0] = (float)rgb2yuv[i][0] * scale;
        mat->m[i][1] = (float)rgb2yuv[i][1] * scale;
        mat->m[i][2] = (float)rgb2yuv[i][2] * scale;
    }

    return 0;
}

/* Whether two specified properties differ; unspecified is compatible. */
static int cudascale_conflicts(int in, int out, int unspecified)
{
    return in != out && in != unspecified && out != unspecified;
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
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    FilterLink     *inl = ff_filter_link(inlink);
    FilterLink    *outl = ff_filter_link(outlink);

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

    s->yuv2rgb = !(s->in_desc->flags & AV_PIX_FMT_FLAG_RGB) &&
                  (s->out_desc->flags & AV_PIX_FMT_FLAG_RGB);
    if (s->yuv2rgb) {

        enum AVColorSpace cs = inlink->colorspace;
        bool limited_range;

        /* When colorspace is unspecified, default to BT.601 (SMPTE 170M)
         * to match swscale behavior (SWS_CS_DEFAULT = ITU601).
         */
        if (cs == AVCOL_SPC_UNSPECIFIED)
            cs = AVCOL_SPC_SMPTE170M;

        if (cs == AVCOL_SPC_RGB) {
            av_log(ctx, AV_LOG_ERROR,
                   "YUV input is tagged with the RGB colorspace\n");
            return AVERROR(EINVAL);
        }

        limited_range = (inlink->color_range != AVCOL_RANGE_JPEG);

        ret = compute_yuv2rgb_matrix(ctx, &s->color_matrix, cs, limited_range);
        if (ret < 0)
            return ret;

        s->color_range = limited_range ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;

        av_log(ctx, AV_LOG_VERBOSE,
               "YUV->RGB conversion enabled (%s, %s range)\n",
               av_color_space_name(cs),
               limited_range ? "limited" : "full");
    }

    s->rgb2yuv = (s->in_desc->flags & AV_PIX_FMT_FLAG_RGB) &&
                 !(s->out_desc->flags & AV_PIX_FMT_FLAG_RGB);
    if (s->rgb2yuv) {
        /* The matrix and range are those negotiated for the output, which
         * the output frames are tagged with. When unspecified, use BT.601
         * (SMPTE 170M) limited range to match swscale behavior. */
        enum AVColorSpace cs = outlink->colorspace;
        bool limited_range;

        if (cs == AVCOL_SPC_UNSPECIFIED)
            cs = AVCOL_SPC_SMPTE170M;

        limited_range = (outlink->color_range != AVCOL_RANGE_JPEG);

        ret = compute_rgb2yuv_matrix(ctx, &s->color_matrix, cs, limited_range);
        if (ret < 0)
            return ret;

        s->color_range = limited_range ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;

        av_log(ctx, AV_LOG_VERBOSE,
               "RGB->YUV conversion enabled (%s, %s range)\n",
               av_color_space_name(cs),
               limited_range ? "limited" : "full");
    }

    if (s->out_desc->flags & AV_PIX_FMT_FLAG_RGB) {
        /* Without a requested format, these are negotiated before the output
         * is known to be RGB, and would describe the RGB output link. */
        if (s->out_range != AVCOL_RANGE_UNSPECIFIED ||
            s->out_color_matrix != AVCOL_SPC_UNSPECIFIED) {
            av_log(ctx, AV_LOG_ERROR,
                   "out_range and out_color_matrix cannot be used with RGB output\n");
            return AVERROR(EINVAL);
        }
    } else if (s->rgb2yuv) {
        if ((s->out_desc->log2_chroma_w || s->out_desc->log2_chroma_h) &&
            outlink->chroma_location != AVCHROMA_LOC_UNSPECIFIED &&
            outlink->chroma_location != AVCHROMA_LOC_CENTER) {
            av_log(ctx, AV_LOG_ERROR,
                   "RGB->YUV conversion produces centered chroma, cannot "
                   "output %s chroma location\n",
                   av_chroma_location_name(outlink->chroma_location));
            return AVERROR(ENOSYS);
        }
    } else {
        /* Without RGB->YUV conversion, the samples keep their encoding.
         * Only two different specified descriptions conflict: an
         * unspecified input may take on the negotiated description, like
         * buffersrc promotes an unspecified range to limited, and an
         * unspecified output leaves it to the frames. */
        if (cudascale_conflicts(inlink->colorspace, outlink->colorspace,
                                AVCOL_SPC_UNSPECIFIED) ||
            cudascale_conflicts(inlink->color_range, outlink->color_range,
                                AVCOL_RANGE_UNSPECIFIED)) {
            av_log(ctx, AV_LOG_ERROR,
                   "Cannot convert from %s %s range to %s %s range, scale_cuda "
                   "only changes YCbCr matrix and range when converting from RGB\n",
                   av_color_space_name(inlink->colorspace),
                   av_color_range_name(inlink->color_range),
                   av_color_space_name(outlink->colorspace),
                   av_color_range_name(outlink->color_range));
            return AVERROR(ENOSYS);
        }

        /* Scaling with the same subsampling keeps the chroma sites. */
        if ((s->in_desc->log2_chroma_w || s->in_desc->log2_chroma_h) &&
            s->in_desc->log2_chroma_w == s->out_desc->log2_chroma_w &&
            s->in_desc->log2_chroma_h == s->out_desc->log2_chroma_h &&
            cudascale_conflicts(inlink->chroma_location, outlink->chroma_location,
                                AVCHROMA_LOC_UNSPECIFIED)) {
            av_log(ctx, AV_LOG_ERROR,
                   "Cannot convert chroma location from %s to %s\n",
                   av_chroma_location_name(inlink->chroma_location),
                   av_chroma_location_name(outlink->chroma_location));
            return AVERROR(ENOSYS);
        }
    }

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

        if (s->yuv2rgb || s->rgb2yuv) {
            /* Conversions plan their own passes, see
             * cudascale_conv_set_init(). */
            if (s->use_filters_opt == 0 &&
                (in_width != out_width || in_height != out_height))
                av_log(ctx, AV_LOG_WARNING, "Scaling during YUV<->RGB "
                       "conversion always uses the generic filters\n");
            s->use_filters = 0;
        } else if (s->interp_algo == INTERP_ALGO_NEAREST) {
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

    /* The direct conversion kernels sample 1:1; scaling during conversion
     * uses the generic filters with interp_algo instead. */
    switch (s->yuv2rgb || s->rgb2yuv ? INTERP_ALGO_NEAREST : s->interp_algo) {
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

        /* YUV->RGB produces all output components in a single kernel,
         * there is no separate chroma pass. */
        if (!s->yuv2rgb) {
            snprintf(buf, sizeof(buf), "Subsample_%s_%s_%s_uv", fixed_infix,
                     in_fmt_name, out_fmt_name);
            ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_fixed_uv,
                                                   s->cu_module, buf));
            if (ret < 0)
                goto unsupported;
        }
    }

    if (s->yuv2rgb || s->rgb2yuv) {
        const char *rgb_name = s->yuv2rgb ? out_fmt_name : in_fmt_name;

        snprintf(buf, sizeof(buf), "Conv_float_%s_%s", in_fmt_name, out_fmt_name);
        ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_conv, s->cu_module, buf));
        if (ret < 0)
            goto unsupported;

        snprintf(buf, sizeof(buf), "Subsample_Generic_float_h_%s_%s", rgb_name, rgb_name);
        ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_tmp, s->cu_module, buf));
        if (ret < 0)
            goto unsupported;

        if (s->yuv2rgb) {
            static const char *const infix[] = { "Generic_h", "Generic_v" };

            for (int dir = CUDA_SCALE_DIR_X; dir <= CUDA_SCALE_DIR_Y; dir++) {
                snprintf(buf, sizeof(buf), "Subsample_%s_%s_%s",
                         infix[dir], rgb_name, rgb_name);
                ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_out[dir],
                                                       s->cu_module, buf));
                if (ret < 0)
                    goto unsupported;
            }
        } else {
            snprintf(buf, sizeof(buf), "Conv_float_%s_%s_uv", in_fmt_name, out_fmt_name);
            ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_conv_uv, s->cu_module, buf));
            if (ret < 0)
                goto unsupported;

            snprintf(buf, sizeof(buf), "Subsample_Generic_float_v_%s_%s", rgb_name, rgb_name);
            ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_tmp_v, s->cu_module, buf));
            if (ret < 0)
                goto unsupported;
        }
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
        case INTERP_ALGO_NEAREST:  params.scaler = SWS_SCALE_POINT; break;
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

/*
 * Converting together with scaling scales on the RGB side, so that every
 * scaling pass works on a single packed plane and reuses the generic RGB
 * kernels: YUV->RGB converts at the visible input size before scaling,
 * RGB->YUV after scaling at the output size. The RGB intermediates are
 * normalized floats, so the scaler rounds and clamps only once.
 */
static int cudascale_conv_set_init(AVFilterContext *ctx,
                                   CUDAScaleFilterSet *set,
                                   int in_width, int in_height)
{
    CUDAScaleContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const AVPixFmtDescriptor *rgb_desc = s->yuv2rgb ? s->out_desc : s->in_desc;
    static const int rgb_depths[4]   = { 8 };
    static const int rgb_channels[4] = { 4 };
    const int scale_x = in_width  != outlink->w;
    const int scale_y = in_height != outlink->h;
    int src = CUDA_SCALE_BUF_IN, tmp = CUDA_SCALE_BUF_TMP;
    CUDAScalePass *pass;
    int ret;

    /* Scaling reads RGB input as normalized floats. */
    set->in_use_linear = 0;
    set->in_as_integer = s->yuv2rgb;

    if (s->yuv2rgb) {
        ret = inter_buf_init(ctx, &set->tmp[tmp - CUDA_SCALE_BUF_TMP],
                             in_width, in_height, 0, 0, 1,
                             rgb_depths, rgb_channels, 1,
                             CUDA_SCALE_PLANE_PRIMARY);
        if (ret < 0)
            return ret;

        set->passes[set->nb_passes++] = (CUDAScalePass) {
            .func     = s->cu_func_conv,
            .planes   = CUDA_SCALE_PLANE_PRIMARY,
            .src      = src,
            .dst      = tmp,
            .src_desc = s->in_desc,
            .dst_desc = rgb_desc,
        };
        src = tmp++;
    }

    for (int dir = CUDA_SCALE_DIR_X; dir <= CUDA_SCALE_DIR_Y; dir++) {
        const int last = dir == CUDA_SCALE_DIR_Y || !scale_y;
        const int to_out = last && s->yuv2rgb;

        if (!(dir == CUDA_SCALE_DIR_X ? scale_x : scale_y))
            continue;

        if (!to_out) {
            ret = inter_buf_init(ctx, &set->tmp[tmp - CUDA_SCALE_BUF_TMP],
                                 outlink->w,
                                 dir == CUDA_SCALE_DIR_Y ? outlink->h : in_height,
                                 0, 0, 1, rgb_depths, rgb_channels, 1,
                                 CUDA_SCALE_PLANE_PRIMARY);
            if (ret < 0)
                return ret;
        }

        pass = &set->passes[set->nb_passes++];
        *pass = (CUDAScalePass) {
            .func     = to_out ? s->cu_func_out[dir] :
                        dir == CUDA_SCALE_DIR_X ? s->cu_func_tmp :
                                                  s->cu_func_tmp_v,
            .planes   = CUDA_SCALE_PLANE_PRIMARY,
            .src      = src,
            .dst      = to_out ? CUDA_SCALE_BUF_OUT : tmp,
            .src_desc = rgb_desc,
            .dst_desc = rgb_desc,
        };
        ret = dir == CUDA_SCALE_DIR_X ?
              cudascale_filter_init(ctx, &pass->filter, in_width, outlink->w, 0.0, 1) :
              cudascale_filter_init(ctx, &pass->filter, in_height, outlink->h, 0.0, 1);
        if (ret < 0)
            return ret;
        if (!to_out)
            src = tmp++;
    }

    if (s->rgb2yuv) {
        set->passes[set->nb_passes++] = (CUDAScalePass) {
            .func     = s->cu_func_conv,
            .func_uv  = s->cu_func_conv_uv,
            .planes   = CUDA_SCALE_PLANE_PRIMARY | CUDA_SCALE_PLANE_CHROMA,
            .src      = src,
            .dst      = CUDA_SCALE_BUF_OUT,
            .src_desc = rgb_desc,
            .dst_desc = s->out_desc,
        };
    }

    return 0;
}

static int cudascale_filter_set_init(AVFilterContext *ctx,
                                     CUDAScaleFilterSet *set,
                                     int in_width, int in_height,
                                     int normalize_crop)
{
    CUDAScaleContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    CUDAScalePassPlan plan;
    CUDAScalePass *pass_x = NULL, *pass_y = NULL, *pass;
    int ret;

    const int in_sub_x  = s->in_desc->log2_chroma_w;
    const int in_sub_y  = s->in_desc->log2_chroma_h;
    const int out_sub_x = s->out_desc->log2_chroma_w;
    const int out_sub_y = s->out_desc->log2_chroma_h;

    const int has_chroma = s->in_planes > 1 && s->out_planes > 1;
    const unsigned int all_planes = CUDA_SCALE_PLANE_PRIMARY |
                                    (has_chroma ? CUDA_SCALE_PLANE_CHROMA : 0);
    const unsigned int out_planes = CUDA_SCALE_PLANE_PRIMARY |
                                    (s->out_planes > 1 ? CUDA_SCALE_PLANE_CHROMA : 0);

    set->in_width       = in_width;
    set->in_height      = in_height;
    set->normalize_crop = normalize_crop;
    set->in_use_linear  = s->interp_use_linear;
    set->in_as_integer  = s->interp_as_integer;

    if ((s->yuv2rgb || s->rgb2yuv) &&
        (in_width != outlink->w || in_height != outlink->h))
        return cudascale_conv_set_init(ctx, set, in_width, in_height);

    if (!s->use_filters) {
        /* The fixed function kernels scale and convert in a single pass,
         * as do the conversion kernels without scaling. */
        set->passes[set->nb_passes++] = (CUDAScalePass) {
            .func     = s->cu_func_fixed,
            .func_uv  = s->cu_func_fixed_uv,
            .planes   = out_planes,
            .src      = CUDA_SCALE_BUF_IN,
            .dst      = CUDA_SCALE_BUF_OUT,
            .src_desc = s->in_desc,
            .dst_desc = s->out_desc,
        };
        return 0;
    }

    cudascale_plan_passes(&plan,
                          in_width, in_height, outlink->w, outlink->h,
                          in_sub_x, in_sub_y, out_sub_x, out_sub_y,
                          has_chroma);

    /* The generic path also handles non-scaling copies and format
     * conversions with a one-tap horizontal filter. */
    if (plan.dir[FILTER_OUT] == CUDA_SCALE_DIR_NONE)
        plan.dir[FILTER_OUT] = CUDA_SCALE_DIR_X;

    if (plan.dir[FILTER_TMP] == CUDA_SCALE_DIR_X) {
        /* The intermediate keeps the input format, except for taking the
         * horizontal chroma subsampling of the output. A cropped input
         * needs all planes to be normalized. */
        pass_x = &set->passes[set->nb_passes++];
        *pass_x = (CUDAScalePass) {
            .func     = s->cu_func_tmp,
            .func_uv  = s->cu_func_tmp_uv,
            .planes   = normalize_crop ? all_planes : plan.x_planes,
            .src      = CUDA_SCALE_BUF_IN,
            .dst      = CUDA_SCALE_BUF_TMP,
            .src_desc = s->in_desc,
            .dst_desc = s->in_desc,
        };
    }

    pass = &set->passes[set->nb_passes++];
    *pass = (CUDAScalePass) {
        .func     = s->cu_func_out[plan.dir[FILTER_OUT]],
        .func_uv  = s->cu_func_out_uv[plan.dir[FILTER_OUT]],
        .planes   = out_planes,
        .src      = pass_x ? CUDA_SCALE_BUF_TMP : CUDA_SCALE_BUF_IN,
        .dst      = CUDA_SCALE_BUF_OUT,
        .src_desc = s->in_desc,
        .dst_desc = s->out_desc,
    };
    if (plan.dir[FILTER_OUT] == CUDA_SCALE_DIR_X)
        pass_x = pass;
    else
        pass_y = pass;

    if (pass_x) {
        const unsigned int planes = pass_x->dst == CUDA_SCALE_BUF_TMP ?
                                    pass_x->planes : all_planes;

        if (planes & CUDA_SCALE_PLANE_PRIMARY) {
            ret = cudascale_filter_init(ctx, &pass_x->filter,
                                        in_width, outlink->w, 0.0,
                                        !!(plan.x_planes &
                                           CUDA_SCALE_PLANE_PRIMARY));
            if (ret < 0)
                return ret;
        }
        if (planes & CUDA_SCALE_PLANE_CHROMA) {
            const int src_size = AV_CEIL_RSHIFT(in_width,  in_sub_x);
            const int dst_size = AV_CEIL_RSHIFT(outlink->w, out_sub_x);
            const double virtual_size = cudascale_plane_virtual_size(
                src_size, in_width, outlink->w, in_sub_x, out_sub_x);
            ret = cudascale_filter_init(ctx, &pass_x->filter_uv,
                                        src_size, dst_size, virtual_size,
                                        !!(plan.x_planes &
                                           CUDA_SCALE_PLANE_CHROMA));
            if (ret < 0)
                return ret;
        }
    }

    if (pass_y) {
        ret = cudascale_filter_init(ctx, &pass_y->filter,
                                    in_height, outlink->h, 0.0,
                                    !!(plan.y_planes &
                                       CUDA_SCALE_PLANE_PRIMARY));
        if (ret < 0)
            return ret;
        if (all_planes & CUDA_SCALE_PLANE_CHROMA) {
            const int src_size = AV_CEIL_RSHIFT(in_height,  in_sub_y);
            const int dst_size = AV_CEIL_RSHIFT(outlink->h, out_sub_y);
            const double virtual_size = cudascale_plane_virtual_size(
                src_size, in_height, outlink->h, in_sub_y, out_sub_y);
            ret = cudascale_filter_init(ctx, &pass_y->filter_uv,
                                        src_size, dst_size, virtual_size,
                                        !!(plan.y_planes &
                                           CUDA_SCALE_PLANE_CHROMA));
            if (ret < 0)
                return ret;
        }
    }

    if (pass_x && pass_x->dst == CUDA_SCALE_BUF_TMP) {
        ret = inter_buf_init(ctx, &set->tmp[0],
                             outlink->w, in_height,
                             out_sub_x, in_sub_y, s->in_planes,
                             s->in_plane_depths, s->in_plane_channels,
                             s->interp_algo == INTERP_ALGO_LANCZOS,
                             pass_x->planes);
        if (ret < 0)
            return ret;
    }

    return 0;
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

    /* Only the generic filters and conversions depend on the visible
     * input size. */
    if (s->filter_set.nb_passes &&
        ((!s->use_filters && !s->yuv2rgb && !s->rgb2yuv) ||
         (s->filter_set.in_width == in_width &&
          s->filter_set.in_height == in_height &&
          s->filter_set.normalize_crop == normalize_crop)))
        return 0;

    ret = cudascale_filter_set_init(ctx, &next, in_width, in_height,
                                    normalize_crop);
    if (ret < 0)
        goto fail;

    if (s->filter_set.nb_passes) {
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
           "Prepared %d pass%s for visible input %dx%d%s\n",
           s->filter_set.nb_passes, s->filter_set.nb_passes > 1 ? "es" : "",
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

/* All YCbCr matrices, preferring BT.601 (SMPTE 170M), the default of
 * RGB->YUV conversion. */
static AVFilterFormats *cudascale_yuv_color_spaces(void)
{
    AVFilterFormats *ret = NULL;

    if (ff_add_format(&ret, AVCOL_SPC_SMPTE170M) < 0 ||
        ff_add_format(&ret, AVCOL_SPC_UNSPECIFIED) < 0)
        return NULL;
    for (int csp = 0; csp < AVCOL_SPC_NB; csp++) {
        if (csp == AVCOL_SPC_RGB || csp == AVCOL_SPC_RESERVED ||
            csp == AVCOL_SPC_UNSPECIFIED || csp == AVCOL_SPC_SMPTE170M)
            continue;
        if (ff_add_format(&ret, csp) < 0)
            return NULL;
    }

    return ret;
}


static int cudascale_query_formats(const AVFilterContext *ctx,
                                   AVFilterFormatsConfig **cfg_in,
                                   AVFilterFormatsConfig **cfg_out)
{
    const CUDAScaleContext *s = ctx->priv;
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_CUDA, AV_PIX_FMT_NONE };
    static const int rgb_ranges[] = { AVCOL_RANGE_JPEG, AVCOL_RANGE_UNSPECIFIED, -1 };
    static const int yuv_ranges[] = { AVCOL_RANGE_MPEG, AVCOL_RANGE_JPEG, -1 };
    const AVPixFmtDescriptor *out_desc = s->format != AV_PIX_FMT_NONE ?
                                         av_pix_fmt_desc_get(s->format) : NULL;
    const int out_rgb = out_desc && (out_desc->flags & AV_PIX_FMT_FLAG_RGB);
    AVFilterFormats *formats;
    int ret;

    if ((ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, pix_fmts)) < 0)
        return ret;

    /* The software format of CUDA frames is unknown during negotiation, so
     * whether a YUV<->RGB conversion happens is only known at configuration
     * time. Negotiate the input and output properties independently.
     * Configuration rejects mismatches that the processing cannot produce.
     *
     * Unless set explicitly, the output properties are preferences rather
     * than constraints: a single value negotiated for the input, which
     * buffersrc does for ordinary YUV, is still followed by the output. YUV
     * output of unconstrained input, which all RGB input is, defaults to
     * BT.601 limited range; an unspecified range is resolved to limited.
     * RGB output prefers full range without forcing it, so that neither
     * a limited input range is followed, nor does it carry over to YUV
     * output converted from it again. */
    if ((ret = ff_formats_ref(ff_all_color_spaces(), &cfg_in[0]->color_spaces)) < 0 ||
        (ret = ff_formats_ref(ff_all_color_ranges(), &cfg_in[0]->color_ranges)) < 0)
        return ret;

    if (out_rgb)
        formats = ff_make_formats_list_singleton(AVCOL_SPC_RGB);
    else if (s->out_color_matrix != AVCOL_SPC_UNSPECIFIED)
        formats = ff_make_formats_list_singleton(s->out_color_matrix);
    else if (out_desc)
        /* YUV output, possibly converted from RGB, never carries RGB. */
        formats = cudascale_yuv_color_spaces();
    else
        formats = ff_all_color_spaces();
    if ((ret = ff_formats_ref(formats, &cfg_out[0]->color_spaces)) < 0)
        return ret;

    if (out_rgb)
        formats = ff_make_format_list(rgb_ranges);
    else if (s->out_range != AVCOL_RANGE_UNSPECIFIED)
        formats = ff_make_formats_list_singleton(s->out_range);
    else if (out_desc)
        formats = ff_make_format_list(yuv_ranges);
    else
        formats = ff_all_color_ranges();
    if ((ret = ff_formats_ref(formats, &cfg_out[0]->color_ranges)) < 0)
        return ret;

    /* Without a requested format, the chroma sampling cannot change, so the
     * chroma location is shared. Otherwise, RGB output does not take on the
     * input location, and YUV output follows a single input location, but
     * prefers leaving it unspecified: RGB->YUV conversion describes its
     * centered chroma on the frames, while unconstrained YUV is not relabeled. */
    if (!out_desc)
        return ff_set_common_chroma_locations2(ctx, cfg_in, cfg_out,
                                               ff_all_chroma_locations());

    if ((ret = ff_formats_ref(ff_all_chroma_locations(),
                              &cfg_in[0]->chroma_locations)) < 0)
        return ret;
    formats = out_rgb ? ff_make_formats_list_singleton(AVCHROMA_LOC_UNSPECIFIED) :
                        ff_all_chroma_locations();
    if ((ret = ff_formats_ref(formats, &cfg_out[0]->chroma_locations)) < 0)
        return ret;

    return 0;
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

    ret = cudascale_load_functions(ctx);
    if (ret < 0)
        return ret;

    if (!s->passthrough) {
        ret = cudascale_setup_filters(ctx, inlink->w, inlink->h);
        if (ret < 0)
            return ret;
    }

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

static int call_resize_kernel(AVFilterContext *ctx, const CUDAScalePass *pass,
                              CUfunction func, const CUDAScaleFilter *filter,
                              const CUtexObject src_tex[4],
                              int src_left, int src_top, int src_width, int src_height,
                              const CUdeviceptr out_data[4],
                              int dst_width, int dst_height, int dst_pitch, int mpeg_range)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    const AVPixFmtDescriptor *src_desc = pass->src_desc;
    const AVPixFmtDescriptor *dst_desc = pass->dst_desc;

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
        .weights = filter->weights,
        .offsets = filter->offsets,
        .filter_size = filter->filter_size,
        /* Supported formats use the same depth and shift for every
         * component. */
        .src_depth = src_desc->comp[0].depth,
        .src_storage_max = ((1U << src_desc->comp[0].depth) - 1) <<
                           src_desc->comp[0].shift,
        .color_matrix = s->color_matrix,
        .dst_depth = dst_desc->comp[0].depth,
        .dst_shift = dst_desc->comp[0].shift,
    };

    /* Color conversion kernels need the subsampling of the YUV side. */
    if (!(src_desc->flags & AV_PIX_FMT_FLAG_RGB) &&
         (dst_desc->flags & AV_PIX_FMT_FLAG_RGB)) {
        params.log2_chroma_w = src_desc->log2_chroma_w;
        params.log2_chroma_h = src_desc->log2_chroma_h;
    } else if ((src_desc->flags & AV_PIX_FMT_FLAG_RGB) &&
               !(dst_desc->flags & AV_PIX_FMT_FLAG_RGB)) {
        params.log2_chroma_w = dst_desc->log2_chroma_w;
        params.log2_chroma_h = dst_desc->log2_chroma_h;
    }

    void *args[] = { &params };

    return CHECK_CU(cu->cuLaunchKernel(func,
                                       DIV_UP(dst_width, BLOCKX), DIV_UP(dst_height, BLOCKY), 1,
                                       BLOCKX, BLOCKY, 1, 0, s->cu_stream, args, NULL));
}

static int cudascale_run_pass(AVFilterContext *ctx, const CUDAScalePass *pass,
                              const CUDATex *out, const CUDATex *in)
{
    CUDAScaleContext *s = ctx->priv;
    /* The color matrix was computed at config time, so color conversions use
     * the resolved color range to keep mpeg_range consistent with it. Other
     * passes follow the frame the source was produced from. */
    const int conversion = (pass->src_desc->flags ^ pass->dst_desc->flags) &
                           AV_PIX_FMT_FLAG_RGB;
    const int mpeg_range = conversion ?
                           (s->color_range != AVCOL_RANGE_JPEG) :
                           (in->color_range != AVCOL_RANGE_JPEG);
    int ret;

    if (pass->planes & CUDA_SCALE_PLANE_PRIMARY) {
        // scale primary plane(s). Usually Y (and A), or single plane of RGB frames.
        ret = call_resize_kernel(ctx, pass, pass->func, &pass->filter,
                                 in->tex, in->crop_left, in->crop_top,
                                 in->crop_width, in->crop_height,
                                 out->data, out->width, out->height,
                                 out->linesize[0], mpeg_range);
        if (ret < 0)
            return ret;
    }

    if (pass->planes & CUDA_SCALE_PLANE_CHROMA) {
        // scale UV plane. Scale function sets both U and V plane, or singular interleaved plane.
        /* Match av_frame_apply_cropping(): subsampled plane origins round
         * down, while visible plane extents round up. */
        ret = call_resize_kernel(ctx, pass, pass->func_uv, &pass->filter_uv,
                                 in->tex,
                                 in->crop_left >> in->log2_chroma_w,
                                 in->crop_top  >> in->log2_chroma_h,
                                 AV_CEIL_RSHIFT(in->crop_width, in->log2_chroma_w),
                                 AV_CEIL_RSHIFT(in->crop_height, in->log2_chroma_h),
                                 out->data,
                                 AV_CEIL_RSHIFT(out->width, out->log2_chroma_w),
                                 AV_CEIL_RSHIFT(out->height, out->log2_chroma_h),
                                 out->linesize[1], mpeg_range);
        if (ret < 0)
            return ret;
    }

    return 0;
}

/* Describe an output frame consistently with the negotiated output link. */
static void cudascale_describe_frame(AVFilterContext *ctx, AVFrame *frame)
{
    CUDAScaleContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const int out_sub = s->out_desc->log2_chroma_w || s->out_desc->log2_chroma_h;

    if (s->yuv2rgb) {
        /* RGB is full range, also where the link leaves it unspecified. */
        frame->colorspace      = AVCOL_SPC_RGB;
        frame->color_range     = AVCOL_RANGE_JPEG;
        frame->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
    } else if (s->rgb2yuv) {
        /* The conversion matrix was built from the negotiated link
         * properties, and the chroma averaging centers the samples. */
        frame->colorspace      = outlink->colorspace;
        frame->color_range     = outlink->color_range;
        frame->chroma_location = out_sub ? AVCHROMA_LOC_CENTER :
                                           AVCHROMA_LOC_UNSPECIFIED;
    } else if (!(s->out_desc->flags & AV_PIX_FMT_FLAG_RGB)) {
        /* Plain scaling keeps the input description, but takes on the
         * negotiated one where the input is unspecified. */
        if (frame->colorspace == AVCOL_SPC_UNSPECIFIED)
            frame->colorspace = outlink->colorspace;
        if (frame->color_range == AVCOL_RANGE_UNSPECIFIED)
            frame->color_range = outlink->color_range;
        if (out_sub && frame->chroma_location == AVCHROMA_LOC_UNSPECIFIED)
            frame->chroma_location = outlink->chroma_location;
    }
}

static int cudascale_scale(AVFilterContext *ctx, AVFrame *out, AVFrame *in,
                           int in_width, int in_height, int normalize_crop)
{
    CUDAScaleContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret = 0;

    ret = cudascale_prepare_filter_set(ctx, in_width, in_height,
                                       normalize_crop);
    if (ret < 0)
        return ret;

    CUDATex in_tex = {0}, out_tex = {0};
    ret = cuda_tex_map_frame(ctx, in, s->in_plane_depths,
                             s->in_plane_channels, &in_tex,
                             s->filter_set.in_use_linear,
                             s->filter_set.in_as_integer);
    if (ret < 0)
        goto fail;

    ret = cuda_tex_map_frame(ctx, s->frame, NULL, NULL, &out_tex, 0, 0);
    if (ret < 0)
        goto fail;

    /* Local views of the buffers; intermediates borrow textures below. */
    CUDATex bufs[CUDA_SCALE_BUF_NB] = {
        [CUDA_SCALE_BUF_IN]  = in_tex,
        [CUDA_SCALE_BUF_TMP]  = s->filter_set.tmp[0],
        [CUDA_SCALE_BUF_TMP2] = s->filter_set.tmp[1],
        [CUDA_SCALE_BUF_OUT] = out_tex,
    };

    for (int i = 0; i < s->filter_set.nb_passes; i++) {
        const CUDAScalePass *pass = &s->filter_set.passes[i];
        const CUDATex *src = &bufs[pass->src];
        CUDATex *dst = &bufs[pass->dst];

        ret = cudascale_run_pass(ctx, pass, dst, src);
        if (ret < 0)
            goto fail;

        if (pass->dst != CUDA_SCALE_BUF_OUT) {
            /* Later passes read the plane groups this pass did not write
             * from its source, and follow the range of the frame. */
            for (int j = 0; j < FF_ARRAY_ELEMS(dst->tex); j++) {
                if (!dst->tex[j])
                    dst->tex[j] = src->tex[j];
            }
            dst->color_range = src->color_range;
        }
    }

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

    cudascale_describe_frame(ctx, out);

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

    if (s->passthrough) {
        cudascale_describe_frame(ctx, in);
        return ff_filter_frame(outlink, in);
    }

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
    { "out_range", "Output color range", OFFSET(out_range), AV_OPT_TYPE_INT, { .i64 = AVCOL_RANGE_UNSPECIFIED }, AVCOL_RANGE_UNSPECIFIED, AVCOL_RANGE_JPEG, FLAGS, .unit = "range" },
        { "auto",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_UNSPECIFIED }, 0, 0, FLAGS, .unit = "range" },
        { "full",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG },        0, 0, FLAGS, .unit = "range" },
        { "limited", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG },        0, 0, FLAGS, .unit = "range" },
        { "jpeg",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG },        0, 0, FLAGS, .unit = "range" },
        { "mpeg",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG },        0, 0, FLAGS, .unit = "range" },
        { "tv",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG },        0, 0, FLAGS, .unit = "range" },
        { "pc",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG },        0, 0, FLAGS, .unit = "range" },
    { "out_color_matrix", "Output YCbCr matrix", OFFSET(out_color_matrix), AV_OPT_TYPE_INT, { .i64 = AVCOL_SPC_UNSPECIFIED }, AVCOL_SPC_BT709, AVCOL_SPC_NB - 1, FLAGS, .unit = "color" },
        { "auto",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_UNSPECIFIED }, 0, 0, FLAGS, .unit = "color" },
        { "bt601",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_SMPTE170M },   0, 0, FLAGS, .unit = "color" },
        { "smpte170m", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_SMPTE170M },   0, 0, FLAGS, .unit = "color" },
        { "bt470",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT470BG },     0, 0, FLAGS, .unit = "color" },
        { "bt470bg",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT470BG },     0, 0, FLAGS, .unit = "color" },
        { "bt709",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT709 },       0, 0, FLAGS, .unit = "color" },
        { "fcc",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_FCC },         0, 0, FLAGS, .unit = "color" },
        { "smpte240m", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_SMPTE240M },   0, 0, FLAGS, .unit = "color" },
        { "ycgco",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_YCGCO },       0, 0, FLAGS, .unit = "color" },
        { "bt2020",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT2020_NCL },  0, 0, FLAGS, .unit = "color" },
        { "bt2020nc",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_SPC_BT2020_NCL },  0, 0, FLAGS, .unit = "color" },
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

    FILTER_QUERY_FUNC2(cudascale_query_formats),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
