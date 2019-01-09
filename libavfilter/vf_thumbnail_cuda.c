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

#include <cuda.h>

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda.h"
#include "libavutil/cuda_check.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "internal.h"

#define CHECK_CU(x) FF_CUDA_CHECK(ctx, x)

#define HIST_SIZE (3*256)
#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define BLOCKX 32
#define BLOCKY 16

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_P016,
    AV_PIX_FMT_YUV444P16,
};

struct thumb_frame {
    AVFrame *buf;               ///< cached frame
    int histogram[HIST_SIZE];   ///< RGB color distribution histogram of the frame
};

typedef struct ThumbnailCudaContext {
    const AVClass *class;
    int n;                      ///< current frame
    int n_frames;               ///< number of frames for analysis
    struct thumb_frame *frames; ///< the n_frames frames
    AVRational tb;              ///< copy of the input timebase to ease access

    AVBufferRef *hw_frames_ctx;

    CUmodule    cu_module;

    CUfunction  cu_func_uchar;
    CUfunction  cu_func_uchar2;
    CUfunction  cu_func_ushort;
    CUfunction  cu_func_ushort2;
    CUtexref    cu_tex_uchar;
    CUtexref    cu_tex_uchar2;
    CUtexref    cu_tex_ushort;
    CUtexref    cu_tex_ushort2;

    CUdeviceptr data;
} ThumbnailCudaContext;

#define OFFSET(x) offsetof(ThumbnailCudaContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption thumbnail_cuda_options[] = {
    { "n", "set the frames batch size", OFFSET(n_frames), AV_OPT_TYPE_INT, {.i64=100}, 2, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(thumbnail_cuda);

static av_cold int init(AVFilterContext *ctx)
{
    ThumbnailCudaContext *s = ctx->priv;

    s->frames = av_calloc(s->n_frames, sizeof(*s->frames));
    if (!s->frames) {
        av_log(ctx, AV_LOG_ERROR,
               "Allocation failure, try to lower the number of frames\n");
        return AVERROR(ENOMEM);
    }
    av_log(ctx, AV_LOG_VERBOSE, "batch size: %d frames\n", s->n_frames);
    return 0;
}

/**
 * @brief        Compute Sum-square deviation to estimate "closeness".
 * @param hist   color distribution histogram
 * @param median average color distribution histogram
 * @return       sum of squared errors
 */
static double frame_sum_square_err(const int *hist, const double *median)
{
    int i;
    double err, sum_sq_err = 0;

    for (i = 0; i < HIST_SIZE; i++) {
        err = median[i] - (double)hist[i];
        sum_sq_err += err*err;
    }
    return sum_sq_err;
}

static AVFrame *get_best_frame(AVFilterContext *ctx)
{
    AVFrame *picref;
    ThumbnailCudaContext *s = ctx->priv;
    int i, j, best_frame_idx = 0;
    int nb_frames = s->n;
    double avg_hist[HIST_SIZE] = {0}, sq_err, min_sq_err = -1;

    // average histogram of the N frames
    for (j = 0; j < FF_ARRAY_ELEMS(avg_hist); j++) {
        for (i = 0; i < nb_frames; i++)
            avg_hist[j] += (double)s->frames[i].histogram[j];
        avg_hist[j] /= nb_frames;
    }

    // find the frame closer to the average using the sum of squared errors
    for (i = 0; i < nb_frames; i++) {
        sq_err = frame_sum_square_err(s->frames[i].histogram, avg_hist);
        if (i == 0 || sq_err < min_sq_err)
            best_frame_idx = i, min_sq_err = sq_err;
    }

    // free and reset everything (except the best frame buffer)
    for (i = 0; i < nb_frames; i++) {
        memset(s->frames[i].histogram, 0, sizeof(s->frames[i].histogram));
        if (i != best_frame_idx)
            av_frame_free(&s->frames[i].buf);
    }
    s->n = 0;

    // raise the chosen one
    picref = s->frames[best_frame_idx].buf;
    av_log(ctx, AV_LOG_INFO, "frame id #%d (pts_time=%f) selected "
           "from a set of %d images\n", best_frame_idx,
           picref->pts * av_q2d(s->tb), nb_frames);
    s->frames[best_frame_idx].buf = NULL;

    return picref;
}

static int thumbnail_kernel(ThumbnailCudaContext *ctx, CUfunction func, CUtexref tex, int channels,
    int *histogram, uint8_t *src_dptr, int src_width, int src_height, int src_pitch, int pixel_size)
{
    CUdeviceptr src_devptr = (CUdeviceptr)src_dptr;
    void *args[] = { &histogram, &src_width, &src_height };
    CUDA_ARRAY_DESCRIPTOR desc;

    desc.Width = src_width;
    desc.Height = src_height;
    desc.NumChannels = channels;
    if (pixel_size == 1) {
        desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
    }
    else {
        desc.Format = CU_AD_FORMAT_UNSIGNED_INT16;
    }

    CHECK_CU(cuTexRefSetAddress2D_v3(tex, &desc, src_devptr, src_pitch));
    CHECK_CU(cuLaunchKernel(func,
                            DIV_UP(src_width, BLOCKX), DIV_UP(src_height, BLOCKY), 1,
                            BLOCKX, BLOCKY, 1, 0, 0, args, NULL));

    return 0;
}

static int thumbnail(AVFilterContext *ctx, int *histogram, AVFrame *in)
{
    AVHWFramesContext *in_frames_ctx = (AVHWFramesContext*)in->hw_frames_ctx->data;
    ThumbnailCudaContext *s = ctx->priv;

    switch (in_frames_ctx->sw_format) {
    case AV_PIX_FMT_NV12:
        thumbnail_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 1);
        thumbnail_kernel(s, s->cu_func_uchar2, s->cu_tex_uchar2, 2,
            histogram + 256, in->data[1], in->width / 2, in->height / 2, in->linesize[1], 1);
        break;
    case AV_PIX_FMT_YUV420P:
        thumbnail_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 1);
        thumbnail_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
            histogram + 256, in->data[1], in->width / 2, in->height / 2, in->linesize[1], 1);
        thumbnail_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
            histogram + 512, in->data[2], in->width / 2, in->height / 2, in->linesize[2], 1);
        break;
    case AV_PIX_FMT_YUV444P:
        thumbnail_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 1);
        thumbnail_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
            histogram + 256, in->data[1], in->width, in->height, in->linesize[1], 1);
        thumbnail_kernel(s, s->cu_func_uchar, s->cu_tex_uchar, 1,
            histogram + 512, in->data[2], in->width, in->height, in->linesize[2], 1);
        break;
    case AV_PIX_FMT_P010LE:
    case AV_PIX_FMT_P016LE:
        thumbnail_kernel(s, s->cu_func_ushort, s->cu_tex_ushort, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 2);
        thumbnail_kernel(s, s->cu_func_ushort2, s->cu_tex_ushort2, 2,
            histogram + 256, in->data[1], in->width / 2, in->height / 2, in->linesize[1], 2);
        break;
    case AV_PIX_FMT_YUV444P16:
        thumbnail_kernel(s, s->cu_func_ushort2, s->cu_tex_uchar, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 2);
        thumbnail_kernel(s, s->cu_func_ushort2, s->cu_tex_uchar, 1,
            histogram + 256, in->data[1], in->width, in->height, in->linesize[1], 2);
        thumbnail_kernel(s, s->cu_func_ushort2, s->cu_tex_uchar, 1,
            histogram + 512, in->data[2], in->width, in->height, in->linesize[2], 2);
        break;
    default:
        return AVERROR_BUG;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx  = inlink->dst;
    ThumbnailCudaContext *s   = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int *hist = s->frames[s->n].histogram;
    AVHWFramesContext *hw_frames_ctx = (AVHWFramesContext*)s->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = hw_frames_ctx->device_ctx->hwctx;
    CUcontext dummy;
    CUDA_MEMCPY2D cpy = { 0 };
    int ret = 0;

    // keep a reference of each frame
    s->frames[s->n].buf = frame;

    ret = CHECK_CU(cuCtxPushCurrent(device_hwctx->cuda_ctx));
    if (ret < 0)
        return ret;

    CHECK_CU(cuMemsetD8(s->data, 0, HIST_SIZE * sizeof(int)));

    thumbnail(ctx, (int*)s->data, frame);

    cpy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    cpy.dstMemoryType = CU_MEMORYTYPE_HOST;
    cpy.srcDevice = s->data;
    cpy.dstHost = hist;
    cpy.srcPitch = HIST_SIZE * sizeof(int);
    cpy.dstPitch = HIST_SIZE * sizeof(int);
    cpy.WidthInBytes = HIST_SIZE * sizeof(int);
    cpy.Height = 1;

    ret = CHECK_CU(cuMemcpy2D(&cpy));
    if (ret < 0)
        return ret;

    if (hw_frames_ctx->sw_format == AV_PIX_FMT_NV12 || hw_frames_ctx->sw_format == AV_PIX_FMT_YUV420P ||
        hw_frames_ctx->sw_format == AV_PIX_FMT_P010LE || hw_frames_ctx->sw_format == AV_PIX_FMT_P016LE)
    {
        int i;
        for (i = 256; i < HIST_SIZE; i++)
            hist[i] = 4 * hist[i];
    }

    CHECK_CU(cuCtxPopCurrent(&dummy));
    if (ret < 0)
        return ret;

    // no selection until the buffer of N frames is filled up
    s->n++;
    if (s->n < s->n_frames)
        return 0;

    return ff_filter_frame(outlink, get_best_frame(ctx));
}

static av_cold void uninit(AVFilterContext *ctx)
{
    int i;
    ThumbnailCudaContext *s = ctx->priv;

    if (s->data) {
        CHECK_CU(cuMemFree(s->data));
        s->data = 0;
    }

    if (s->cu_module) {
        CHECK_CU(cuModuleUnload(s->cu_module));
        s->cu_module = NULL;
    }

    for (i = 0; i < s->n_frames && s->frames[i].buf; i++)
        av_frame_free(&s->frames[i].buf);
    av_freep(&s->frames);
}

static int request_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    ThumbnailCudaContext *s = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && s->n) {
        ret = ff_filter_frame(link, get_best_frame(ctx));
        if (ret < 0)
            return ret;
        ret = AVERROR_EOF;
    }
    if (ret < 0)
        return ret;
    return 0;
}

static int format_is_supported(enum AVPixelFormat fmt)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ThumbnailCudaContext *s = ctx->priv;
    AVHWFramesContext     *hw_frames_ctx = (AVHWFramesContext*)inlink->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = hw_frames_ctx->device_ctx->hwctx;
    CUcontext dummy, cuda_ctx = device_hwctx->cuda_ctx;
    int ret;

    extern char vf_thumbnail_cuda_ptx[];

    ret = CHECK_CU(cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = CHECK_CU(cuModuleLoadData(&s->cu_module, vf_thumbnail_cuda_ptx));
    if (ret < 0)
        return ret;

    CHECK_CU(cuModuleGetFunction(&s->cu_func_uchar, s->cu_module, "Thumbnail_uchar"));
    CHECK_CU(cuModuleGetFunction(&s->cu_func_uchar2, s->cu_module, "Thumbnail_uchar2"));
    CHECK_CU(cuModuleGetFunction(&s->cu_func_ushort, s->cu_module, "Thumbnail_ushort"));
    CHECK_CU(cuModuleGetFunction(&s->cu_func_ushort2, s->cu_module, "Thumbnail_ushort2"));

    CHECK_CU(cuModuleGetTexRef(&s->cu_tex_uchar, s->cu_module, "uchar_tex"));
    CHECK_CU(cuModuleGetTexRef(&s->cu_tex_uchar2, s->cu_module, "uchar2_tex"));
    CHECK_CU(cuModuleGetTexRef(&s->cu_tex_ushort, s->cu_module, "ushort_tex"));
    CHECK_CU(cuModuleGetTexRef(&s->cu_tex_ushort2, s->cu_module, "ushort2_tex"));

    CHECK_CU(cuTexRefSetFlags(s->cu_tex_uchar, CU_TRSF_READ_AS_INTEGER));
    CHECK_CU(cuTexRefSetFlags(s->cu_tex_uchar2, CU_TRSF_READ_AS_INTEGER));
    CHECK_CU(cuTexRefSetFlags(s->cu_tex_ushort, CU_TRSF_READ_AS_INTEGER));
    CHECK_CU(cuTexRefSetFlags(s->cu_tex_ushort2, CU_TRSF_READ_AS_INTEGER));

    CHECK_CU(cuTexRefSetFilterMode(s->cu_tex_uchar, CU_TR_FILTER_MODE_LINEAR));
    CHECK_CU(cuTexRefSetFilterMode(s->cu_tex_uchar2, CU_TR_FILTER_MODE_LINEAR));
    CHECK_CU(cuTexRefSetFilterMode(s->cu_tex_ushort, CU_TR_FILTER_MODE_LINEAR));
    CHECK_CU(cuTexRefSetFilterMode(s->cu_tex_ushort2, CU_TR_FILTER_MODE_LINEAR));

    ret = CHECK_CU(cuMemAlloc(&s->data, HIST_SIZE * sizeof(int)));
    if (ret < 0)
        return ret;

    CHECK_CU(cuCtxPopCurrent(&dummy));

    s->hw_frames_ctx = ctx->inputs[0]->hw_frames_ctx;

    ctx->outputs[0]->hw_frames_ctx = av_buffer_ref(s->hw_frames_ctx);
    if (!ctx->outputs[0]->hw_frames_ctx)
        return AVERROR(ENOMEM);

    s->tb = inlink->time_base;

    if (!format_is_supported(hw_frames_ctx->sw_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n", av_get_pix_fmt_name(hw_frames_ctx->sw_format));
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_CUDA,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static const AVFilterPad thumbnail_cuda_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad thumbnail_cuda_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_thumbnail_cuda = {
    .name          = "thumbnail_cuda",
    .description   = NULL_IF_CONFIG_SMALL("Select the most representative frame in a given sequence of consecutive frames."),
    .priv_size     = sizeof(ThumbnailCudaContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = thumbnail_cuda_inputs,
    .outputs       = thumbnail_cuda_outputs,
    .priv_class    = &thumbnail_cuda_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
