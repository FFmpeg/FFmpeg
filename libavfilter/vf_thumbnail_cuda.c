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

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"

#include "cuda/load_helper.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

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
    AVCUDADeviceContext *hwctx;

    CUmodule    cu_module;

    CUfunction  cu_func_uchar;
    CUfunction  cu_func_uchar2;
    CUfunction  cu_func_ushort;
    CUfunction  cu_func_ushort2;
    CUstream    cu_stream;

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

static int thumbnail_kernel(AVFilterContext *ctx, CUfunction func, int channels,
    int *histogram, uint8_t *src_dptr, int src_width, int src_height, int src_pitch, int pixel_size)
{
    int ret;
    ThumbnailCudaContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    CUtexObject tex = 0;
    void *args[] = { &tex, &histogram, &src_width, &src_height };

    CUDA_TEXTURE_DESC tex_desc = {
        .filterMode = CU_TR_FILTER_MODE_LINEAR,
        .flags = CU_TRSF_READ_AS_INTEGER,
    };

    CUDA_RESOURCE_DESC res_desc = {
        .resType = CU_RESOURCE_TYPE_PITCH2D,
        .res.pitch2D.format = pixel_size == 1 ?
                              CU_AD_FORMAT_UNSIGNED_INT8 :
                              CU_AD_FORMAT_UNSIGNED_INT16,
        .res.pitch2D.numChannels = channels,
        .res.pitch2D.width = src_width,
        .res.pitch2D.height = src_height,
        .res.pitch2D.pitchInBytes = src_pitch,
        .res.pitch2D.devPtr = (CUdeviceptr)src_dptr,
    };

    ret = CHECK_CU(cu->cuTexObjectCreate(&tex, &res_desc, &tex_desc, NULL));
    if (ret < 0)
        goto exit;

    ret = CHECK_CU(cu->cuLaunchKernel(func,
                                      DIV_UP(src_width, BLOCKX), DIV_UP(src_height, BLOCKY), 1,
                                      BLOCKX, BLOCKY, 1, 0, s->cu_stream, args, NULL));
exit:
    if (tex)
        CHECK_CU(cu->cuTexObjectDestroy(tex));

    return ret;
}

static int thumbnail(AVFilterContext *ctx, int *histogram, AVFrame *in)
{
    AVHWFramesContext *in_frames_ctx = (AVHWFramesContext*)in->hw_frames_ctx->data;
    ThumbnailCudaContext *s = ctx->priv;

    switch (in_frames_ctx->sw_format) {
    case AV_PIX_FMT_NV12:
        thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 1);
        thumbnail_kernel(ctx, s->cu_func_uchar2, 2,
            histogram + 256, in->data[1], in->width / 2, in->height / 2, in->linesize[1], 1);
        break;
    case AV_PIX_FMT_YUV420P:
        thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 1);
        thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram + 256, in->data[1], in->width / 2, in->height / 2, in->linesize[1], 1);
        thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram + 512, in->data[2], in->width / 2, in->height / 2, in->linesize[2], 1);
        break;
    case AV_PIX_FMT_YUV444P:
        thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 1);
        thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram + 256, in->data[1], in->width, in->height, in->linesize[1], 1);
        thumbnail_kernel(ctx, s->cu_func_uchar, 1,
            histogram + 512, in->data[2], in->width, in->height, in->linesize[2], 1);
        break;
    case AV_PIX_FMT_P010LE:
    case AV_PIX_FMT_P016LE:
        thumbnail_kernel(ctx, s->cu_func_ushort, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 2);
        thumbnail_kernel(ctx, s->cu_func_ushort2, 2,
            histogram + 256, in->data[1], in->width / 2, in->height / 2, in->linesize[1], 2);
        break;
    case AV_PIX_FMT_YUV444P16:
        thumbnail_kernel(ctx, s->cu_func_ushort2, 1,
            histogram, in->data[0], in->width, in->height, in->linesize[0], 2);
        thumbnail_kernel(ctx, s->cu_func_ushort2, 1,
            histogram + 256, in->data[1], in->width, in->height, in->linesize[1], 2);
        thumbnail_kernel(ctx, s->cu_func_ushort2, 1,
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
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    AVFilterLink *outlink = ctx->outputs[0];
    int *hist = s->frames[s->n].histogram;
    AVHWFramesContext *hw_frames_ctx = (AVHWFramesContext*)s->hw_frames_ctx->data;
    CUcontext dummy;
    CUDA_MEMCPY2D cpy = { 0 };
    int ret = 0;

    // keep a reference of each frame
    s->frames[s->n].buf = frame;

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
    if (ret < 0)
        return ret;

    CHECK_CU(cu->cuMemsetD8Async(s->data, 0, HIST_SIZE * sizeof(int), s->cu_stream));

    thumbnail(ctx, (int*)s->data, frame);

    cpy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    cpy.dstMemoryType = CU_MEMORYTYPE_HOST;
    cpy.srcDevice = s->data;
    cpy.dstHost = hist;
    cpy.srcPitch = HIST_SIZE * sizeof(int);
    cpy.dstPitch = HIST_SIZE * sizeof(int);
    cpy.WidthInBytes = HIST_SIZE * sizeof(int);
    cpy.Height = 1;

    ret = CHECK_CU(cu->cuMemcpy2DAsync(&cpy, s->cu_stream));
    if (ret < 0)
        return ret;

    if (hw_frames_ctx->sw_format == AV_PIX_FMT_NV12 || hw_frames_ctx->sw_format == AV_PIX_FMT_YUV420P ||
        hw_frames_ctx->sw_format == AV_PIX_FMT_P010LE || hw_frames_ctx->sw_format == AV_PIX_FMT_P016LE)
    {
        int i;
        for (i = 256; i < HIST_SIZE; i++)
            hist[i] = 4 * hist[i];
    }

    ret = CHECK_CU(cu->cuCtxPopCurrent(&dummy));
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
    ThumbnailCudaContext *s = ctx->priv;

    if (s->hwctx) {
        CudaFunctions *cu = s->hwctx->internal->cuda_dl;

        if (s->data) {
            CHECK_CU(cu->cuMemFree(s->data));
            s->data = 0;
        }

        if (s->cu_module) {
            CHECK_CU(cu->cuModuleUnload(s->cu_module));
            s->cu_module = NULL;
        }
    }

    if (s->frames) {
        for (int i = 0; i < s->n_frames && s->frames[i].buf; i++)
            av_frame_free(&s->frames[i].buf);
        av_freep(&s->frames);
    }
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
    FilterLink      *inl = ff_filter_link(inlink);
    FilterLink     *outl = ff_filter_link(ctx->outputs[0]);
    ThumbnailCudaContext *s = ctx->priv;
    AVHWFramesContext     *hw_frames_ctx = (AVHWFramesContext*)inl->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = hw_frames_ctx->device_ctx->hwctx;
    CUcontext dummy, cuda_ctx = device_hwctx->cuda_ctx;
    CudaFunctions *cu = device_hwctx->internal->cuda_dl;
    int ret;

    extern const unsigned char ff_vf_thumbnail_cuda_ptx_data[];
    extern const unsigned int ff_vf_thumbnail_cuda_ptx_len;

    s->hwctx = device_hwctx;
    s->cu_stream = s->hwctx->stream;

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, device_hwctx, &s->cu_module, ff_vf_thumbnail_cuda_ptx_data, ff_vf_thumbnail_cuda_ptx_len);
    if (ret < 0)
        return ret;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_uchar, s->cu_module, "Thumbnail_uchar"));
    if (ret < 0)
        return ret;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_uchar2, s->cu_module, "Thumbnail_uchar2"));
    if (ret < 0)
        return ret;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_ushort, s->cu_module, "Thumbnail_ushort"));
    if (ret < 0)
        return ret;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_ushort2, s->cu_module, "Thumbnail_ushort2"));
    if (ret < 0)
        return ret;

    ret = CHECK_CU(cu->cuMemAlloc(&s->data, HIST_SIZE * sizeof(int)));
    if (ret < 0)
        return ret;

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    s->hw_frames_ctx = inl->hw_frames_ctx;

    outl->hw_frames_ctx = av_buffer_ref(s->hw_frames_ctx);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    s->tb = inlink->time_base;

    if (!format_is_supported(hw_frames_ctx->sw_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n", av_get_pix_fmt_name(hw_frames_ctx->sw_format));
        return AVERROR(ENOSYS);
    }

    return 0;
}

static const AVFilterPad thumbnail_cuda_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad thumbnail_cuda_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_thumbnail_cuda = {
    .name          = "thumbnail_cuda",
    .description   = NULL_IF_CONFIG_SMALL("Select the most representative frame in a given sequence of consecutive frames."),
    .priv_size     = sizeof(ThumbnailCudaContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(thumbnail_cuda_inputs),
    FILTER_OUTPUTS(thumbnail_cuda_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),
    .priv_class    = &thumbnail_cuda_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
