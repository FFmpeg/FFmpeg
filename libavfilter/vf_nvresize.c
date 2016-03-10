/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
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

/* External headers */
#include <cudautils.h>

/* FFmpeg headers */
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"

#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define MAX_OUTPUT 16
#define BLOCKX 32
#define BLOCKY 16

typedef struct cu_tex {
    int w;
    int h;
    size_t pitch;
    CUdeviceptr dptr;
} cu_tex;

typedef struct NVResizeContext {
    const AVClass *class;

    /**
    * New dimensions. Special values are:
    *   0 = original width/height
    *  -1 = keep original aspect
    *  -N = try to keep aspect but make sure it is divisible by N
    */
    int nb_outputs;

    char *size_str;
    int force_original_aspect_ratio;
    int readback_FB;
    int gpu;

    int cuda_inited;

    CUcontext   cu_ctx;
    CudaDynLoadFunctions* cu_dl_func;
    CUmodule    cu_module;
    CUfunction  cu_func_uchar;
    CUfunction  cu_func_uchar2;
    CUfunction  cu_func_uchar4;
    CUtexref    cu_tex_uchar;
    CUtexref    cu_tex_uchar2;
    CUtexref    cu_tex_uchar4;
    cu_tex      intex;
    cu_tex      outtex[MAX_OUTPUT];

} NVResizeContext;

#define OFFSET(x) offsetof(NVResizeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM

static const AVOption nvresize_options[] = {
    { "outputs",  "set number of outputs",  OFFSET(nb_outputs),  AV_OPT_TYPE_INT, { .i64 = 1 }, 1, MAX_OUTPUT, FLAGS },
    { "readback", "read result back to FB", OFFSET(readback_FB), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS },
    { "size",     "set video size",         OFFSET(size_str),    AV_OPT_TYPE_STRING, {.str = NULL}, 0, FLAGS },
    { "s",        "set video size",         OFFSET(size_str),    AV_OPT_TYPE_STRING, {.str = NULL}, 0, FLAGS },
    { "gpu", "Selects which NVENC capable GPU to use. First GPU is 0, second is 1, and so on.", OFFSET(gpu), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "force_original_aspect_ratio", "decrease or increase w/h if necessary to keep the original AR", OFFSET(force_original_aspect_ratio), AV_OPT_TYPE_INT, { .i64 = 0}, 0, 2, FLAGS, "force_oar" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(nvresize);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_ARGB,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_ABGR,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE,
    };

    AVFilterFormats *fmts_list = ff_make_format_list((const int*)pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    NVResizeContext *s = ctx->priv;

    int outIdx = atoi(outlink->srcpad->name + 3);
    int64_t w, h;
    int factor_w, factor_h;

    w = s->outtex[outIdx].w;
    h = s->outtex[outIdx].h;

    // Check if it is requested that the result has to be divisible by a some
    // factor (w or h = -n with n being the factor).
    factor_w = 1;
    factor_h = 1;
    if (w < -1) {
        factor_w = -w;
    }
    if (h < -1) {
        factor_h = -h;
    }

    if (w < 0 && h < 0)
        s->outtex[outIdx].w = s->outtex[outIdx].h = 0;

    if (!(w = s->outtex[outIdx].w))
        w = inlink->w;
    if (!(h = s->outtex[outIdx].h))
        h = inlink->h;

    // Make sure that the result is divisible by the factor we determined
    // earlier. If no factor was set, it is nothing will happen as the default
    // factor is 1
    if (w < 0)
        w = av_rescale(h, inlink->w, inlink->h * factor_w) * factor_w;
    if (h < 0)
        h = av_rescale(w, inlink->h, inlink->w * factor_h) * factor_h;

    // Note that force_original_aspect_ratio may overwrite the previous set
    // dimensions so that it is not divisible by the set factors anymore.
    if (s->force_original_aspect_ratio) {
        int tmp_w = av_rescale(h, inlink->w, inlink->h);
        int tmp_h = av_rescale(w, inlink->h, inlink->w);

        if (s->force_original_aspect_ratio == 1) {
             w = FFMIN(tmp_w, w);
             h = FFMIN(tmp_h, h);
        } else {
             w = FFMAX(tmp_w, w);
             h = FFMAX(tmp_h, h);
        }
    }

    if (w > INT_MAX || h > INT_MAX ||
        (h * inlink->w) > INT_MAX  ||
        (w * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Resd value for width or height is too big.\n");

    s->outtex[outIdx].w = outlink->w = w;
    s->outtex[outIdx].h = outlink->h = h;

    if (inlink->sample_aspect_ratio.num){
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h * inlink->w, outlink->w * inlink->h}, inlink->sample_aspect_ratio);
    } else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    // create output device memory
    switch(outlink->format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_NV12:
        __cu(s->cu_dl_func->cu_mem_alloc_pitch(&s->outtex[outIdx].dptr,
                &s->outtex[outIdx].pitch, s->outtex[outIdx].w, s->outtex[outIdx].h*3/2, 16));
        break;

    case AV_PIX_FMT_YUV444P:
        __cu(s->cu_dl_func->cu_mem_alloc_pitch(&s->outtex[outIdx].dptr,
                &s->outtex[outIdx].pitch, s->outtex[outIdx].w, s->outtex[outIdx].h*3, 16));
        break;

    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_ABGR:
    case AV_PIX_FMT_BGRA:
        __cu(s->cu_dl_func->cu_mem_alloc_pitch(&s->outtex[outIdx].dptr,
                &s->outtex[outIdx].pitch, s->outtex[outIdx].w*4, s->outtex[outIdx].h, 16));
        break;
    }

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    extern char resize_ptx[];
    NVResizeContext *s = ctx->priv;
    int ret;
    int i, j;
    int count = 0;
    for (i = 0; i < s->nb_outputs; i++) {
        char name[32];
        AVFilterPad pad = { 0 };

        snprintf(name, sizeof(name), "out%d", i);
        pad.type = ctx->filter->inputs[0].type;
        pad.name = av_strdup(name);
        pad.config_props = config_output;
        if (!pad.name)
            return AVERROR(ENOMEM);

        ff_insert_outpad(ctx, i, &pad);
    }

    // parse size parameters here
    if (s->size_str) {
        char split = '|';
        char* found = NULL;
        char* head = s->size_str;
        while ((found = strchr(head, split)) != NULL) {
            *found = 0;
            if ((ret = av_parse_video_size(&s->outtex[count].w, &s->outtex[count].h, head)) < 0) {
                av_log(ctx, AV_LOG_ERROR, "Invalid size '%s'\n", head);
                return ret;
            }
            head = found+1;
            count++;
        }

        if ((ret = av_parse_video_size(&s->outtex[count].w, &s->outtex[count].h, head)) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid size '%s'\n", head);
            return ret;
        }
        count++;
    }

    // sort the output
    for (i = 0; i < count; i++) {
        for (j = i+1; j < count; j++) {
            int tempH, tempW;
            if (s->outtex[i].w < s->outtex[j].w) {
                tempW = s->outtex[i].w;          tempH = s->outtex[i].h;
                s->outtex[i].w = s->outtex[j].w; s->outtex[i].h = s->outtex[j].h;
                s->outtex[j].w = tempW;          s->outtex[j].h = tempH;
            }
        }
    }

    if (count < s->nb_outputs) {
        int offset = s->nb_outputs - count;
        for (i = s->nb_outputs-1; i >= offset; i--) {
            s->outtex[i].w = s->outtex[i-offset].w;
            s->outtex[i].h = s->outtex[i-offset].h;
        }
        for (i = 0; i < offset; i++) {
            s->outtex[i].w = s->outtex[i].h = 0;
        }
    }

    // init cuda_context
    if (!s->cu_ctx) {
        init_cuda();
        get_cuda_context(&s->cu_ctx, s->gpu);
    }
    s->cu_dl_func = get_cuda_dl_func();

    __cu(s->cu_dl_func->cu_module_load_data(&s->cu_module, resize_ptx));

    // load functions
    __cu(s->cu_dl_func->cu_module_get_function(&s->cu_func_uchar,   s->cu_module, "Subsample_Bilinear_uchar"));
    __cu(s->cu_dl_func->cu_module_get_function(&s->cu_func_uchar2,  s->cu_module, "Subsample_Bilinear_uchar2"));
    __cu(s->cu_dl_func->cu_module_get_function(&s->cu_func_uchar4,  s->cu_module, "Subsample_Bilinear_uchar4"));
    __cu(s->cu_dl_func->cu_module_get_texref(&s->cu_tex_uchar,  s->cu_module, "uchar_tex"));
    __cu(s->cu_dl_func->cu_module_get_texref(&s->cu_tex_uchar2, s->cu_module, "uchar2_tex"));
    __cu(s->cu_dl_func->cu_module_get_texref(&s->cu_tex_uchar4, s->cu_module, "uchar4_tex"));

    __cu(s->cu_dl_func->cu_texref_set_flags(s->cu_tex_uchar,  CU_TRSF_READ_AS_INTEGER));
    __cu(s->cu_dl_func->cu_texref_set_flags(s->cu_tex_uchar2, CU_TRSF_READ_AS_INTEGER));
    __cu(s->cu_dl_func->cu_texref_set_flags(s->cu_tex_uchar4, CU_TRSF_READ_AS_INTEGER));
    __cu(s->cu_dl_func->cu_texref_set_filtermode(s->cu_tex_uchar,  CU_TR_FILTER_MODE_LINEAR));
    __cu(s->cu_dl_func->cu_texref_set_filtermode(s->cu_tex_uchar2, CU_TR_FILTER_MODE_LINEAR));
    __cu(s->cu_dl_func->cu_texref_set_filtermode(s->cu_tex_uchar4, CU_TR_FILTER_MODE_LINEAR));

    return 0;
}

static int copy_from_avframe(NVResizeContext *s, AVFrame* src, cu_tex* dst)
{
    av_assert0(src->width == dst->w && src->height == dst->h);

    switch (src->format) {
    case AV_PIX_FMT_YUV420P:
        // copy Y channel
        __cu(cuMemCpy2d(src->data[0], (CUdeviceptr)NULL, src->linesize[0], NULL, dst->dptr, dst->pitch, src->width, src->height, CU_MEMORYTYPE_HOST, CU_MEMORYTYPE_DEVICE));
        // copy U channel
        __cu(cuMemCpy2d(src->data[1], (CUdeviceptr)NULL, src->linesize[1], NULL, dst->dptr + dst->pitch*dst->h, dst->pitch / 2, src->width / 2, src->height / 2, CU_MEMORYTYPE_HOST, CU_MEMORYTYPE_DEVICE));
        // copy V channel
        __cu(cuMemCpy2d(src->data[2], (CUdeviceptr)NULL, src->linesize[2], NULL, dst->dptr + dst->pitch*dst->h * 5 / 4, dst->pitch / 2, src->width / 2, src->height / 2, CU_MEMORYTYPE_HOST, CU_MEMORYTYPE_DEVICE));

        break;

    case AV_PIX_FMT_YUV444P:
        // copy Y channel
        __cu(cuMemCpy2d(src->data[0], (CUdeviceptr)NULL, src->linesize[0], NULL, dst->dptr, dst->pitch, src->width, src->height, CU_MEMORYTYPE_HOST, CU_MEMORYTYPE_DEVICE));
        // copy U channel
        __cu(cuMemCpy2d(src->data[1], (CUdeviceptr)NULL, src->linesize[1], NULL, dst->dptr + dst->pitch*dst->h, dst->pitch, src->width, src->height, CU_MEMORYTYPE_HOST, CU_MEMORYTYPE_DEVICE));
        // copy V channel
        __cu(cuMemCpy2d(src->data[2], (CUdeviceptr)NULL, src->linesize[2], NULL, dst->dptr + dst->pitch*dst->h * 2, dst->pitch, src->width, src->height, CU_MEMORYTYPE_HOST, CU_MEMORYTYPE_DEVICE));
        break;

    case AV_PIX_FMT_NV12:
        // copy Y channel
        __cu(cuMemCpy2d(src->data[0], (CUdeviceptr)NULL, src->linesize[0], NULL, dst->dptr, dst->pitch, src->width, src->height, CU_MEMORYTYPE_HOST, CU_MEMORYTYPE_DEVICE));
        // copy UV channel
        __cu(cuMemCpy2d(src->data[1], (CUdeviceptr)NULL, src->linesize[1], NULL, dst->dptr + dst->pitch*dst->h, dst->pitch, src->width, src->height / 2, CU_MEMORYTYPE_HOST, CU_MEMORYTYPE_DEVICE));
        break;

    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_ABGR:
    case AV_PIX_FMT_BGRA:
        // copy the packed 32-bit plane
        __cu(cuMemCpy2d(src->data[0], (CUdeviceptr)NULL, src->linesize[0], NULL, dst->dptr, dst->pitch, src->width * 4, src->height, CU_MEMORYTYPE_HOST, CU_MEMORYTYPE_DEVICE));

        break;

    default:
        av_log(NULL, AV_LOG_FATAL, "Unsupported input format: %s!\n", av_get_pix_fmt_name(src->format));
        return -1;
    }
    return 0;
}

static int copy_to_avframe(NVResizeContext* s, cu_tex* src, AVFrame* dst)
{
    //av_assert0(src->w == dst->width && src->h == dst->height);

    switch (dst->format) {
    case AV_PIX_FMT_YUV420P:
        // copy Y channel
        __cu(cuMemCpy2d(NULL, src->dptr, src->pitch, dst->data[0], (CUdeviceptr)NULL, dst->linesize[0], dst->width, dst->height, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_HOST));
        // copy U channel
        __cu(cuMemCpy2d(NULL, src->dptr + src->pitch*src->h, src->pitch / 2, dst->data[1], (CUdeviceptr)NULL, dst->linesize[1], dst->width / 2, dst->height / 2, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_HOST));
        // copy V channel
        __cu(cuMemCpy2d(NULL, src->dptr + src->pitch*src->h * 5 / 4, src->pitch / 2, dst->data[2], (CUdeviceptr)NULL, dst->linesize[2], dst->width / 2, dst->height / 2, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_HOST));
        break;

    case AV_PIX_FMT_YUV444P:
        // copy Y channel
        __cu(cuMemCpy2d(NULL, src->dptr, src->pitch, dst->data[0], (CUdeviceptr)NULL, dst->linesize[0], dst->width, dst->height, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_HOST));
        // copy U channel
        __cu(cuMemCpy2d(NULL, src->dptr + src->pitch*src->h, src->pitch, dst->data[1], (CUdeviceptr)NULL, dst->linesize[1], dst->width, dst->height, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_HOST));
        // copy V channel
        __cu(cuMemCpy2d(NULL, src->dptr + src->pitch*src->h * 2, src->pitch, dst->data[2], (CUdeviceptr)NULL, dst->linesize[2], dst->width, dst->height, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_HOST));

        break;

    case AV_PIX_FMT_NV12:
        // copy Y channel
        __cu(cuMemCpy2d(NULL, src->dptr, src->pitch, dst->data[0], (CUdeviceptr)NULL, dst->linesize[0], dst->width, dst->height, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_HOST));
        // copy UV channel
        __cu(cuMemCpy2d(NULL, src->dptr + src->pitch*src->h, src->pitch, dst->data[1], (CUdeviceptr)NULL, dst->linesize[1], dst->width, dst->height / 2, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_HOST));
        break;

    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_ABGR:
    case AV_PIX_FMT_BGRA:
        // copy the packed 32-bit plane
        __cu(cuMemCpy2d(NULL, src->dptr, src->pitch, dst->data[0], (CUdeviceptr)NULL, dst->linesize[0], dst->width * 4, dst->height, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_HOST));

        break;

    default:
        av_log(NULL, AV_LOG_FATAL, "Unsupported output format: %s!\n", av_get_pix_fmt_name(dst->format));
        return -1;
    }
    return 0;
}

static int call_resize_kernel(CudaDynLoadFunctions* dl_func, CUfunction func, CUtexref tex, int channels,
                             CUdeviceptr src_dptr, int src_width, int src_height, int src_pitch,
                             CUdeviceptr dst_dptr, int dst_width, int dst_height, int dst_pitch)
{
    void *args_uchar[] = { &dst_dptr, &dst_width, &dst_height, &dst_pitch, &src_width, &src_height };
    CUDA_ARRAY_DESCRIPTOR desc;
    desc.Width  = src_width;
    desc.Height = src_height;
    desc.NumChannels = channels;
    desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
    __cu(dl_func->cu_texref_set_address_2D(tex, &desc, src_dptr, src_pitch));

    __cu(dl_func->cu_launch_kernel(func, DIV_UP(dst_width, BLOCKX), DIV_UP(dst_height, BLOCKY), 1,
        BLOCKX, BLOCKY, 1, 0, NULL, args_uchar, NULL));

    return 0;
}

static int do_cuda_resize(NVResizeContext *s, cu_tex* src, cu_tex* dst, int format)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P:
        if (src->w == dst->w && src->h == dst->h && src->pitch == dst->pitch) {
            __cu(cuMemCpy2d(NULL, src->dptr, src->pitch, NULL, dst->dptr, dst->pitch, src->pitch, src->h*3/2, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_DEVICE));

        }
        else {
            call_resize_kernel(s->cu_dl_func, s->cu_func_uchar, s->cu_tex_uchar, 1,
                    src->dptr, src->w, src->h, src->pitch,
                    dst->dptr, dst->w, dst->h, dst->pitch);

            call_resize_kernel(s->cu_dl_func, s->cu_func_uchar, s->cu_tex_uchar, 1,
                    src->dptr+src->pitch*src->h, src->w/2, src->h/2, src->pitch/2,
                    dst->dptr+dst->pitch*dst->h, dst->w/2, dst->h/2, dst->pitch/2);

            call_resize_kernel(s->cu_dl_func, s->cu_func_uchar, s->cu_tex_uchar, 1,
                    src->dptr+src->pitch*src->h*5/4, src->w/2, src->h/2, src->pitch/2,
                    dst->dptr+dst->pitch*dst->h*5/4, dst->w/2, dst->h/2, dst->pitch/2);
        }

        break;

    case AV_PIX_FMT_YUV444P:
        if (src->w == dst->w && src->h == dst->h) {
            __cu(cuMemCpy2d(NULL, src->dptr, src->pitch, NULL, dst->dptr, dst->pitch, src->w, src->h*3, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_DEVICE));
        }
        else {
            call_resize_kernel(s->cu_dl_func, s->cu_func_uchar, s->cu_tex_uchar, 1,
                    src->dptr, src->w, src->h, src->pitch,
                    dst->dptr, dst->w, dst->h, dst->pitch);

            call_resize_kernel(s->cu_dl_func, s->cu_func_uchar, s->cu_tex_uchar, 1,
                    src->dptr+src->pitch*src->h, src->w, src->h, src->pitch,
                    dst->dptr+dst->pitch*dst->h, dst->w, dst->h, dst->pitch);

            call_resize_kernel(s->cu_dl_func, s->cu_func_uchar, s->cu_tex_uchar, 1,
                    src->dptr+src->pitch*src->h*2, src->w, src->h, src->pitch,
                    dst->dptr+dst->pitch*dst->h*2, dst->w, dst->h, dst->pitch);
        }

        break;

    case AV_PIX_FMT_NV12:
        if (src->w == dst->w && src->h == dst->h) {
            __cu(cuMemCpy2d(NULL, src->dptr, src->pitch, NULL, dst->dptr, dst->pitch, src->w, src->h*3/2, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_DEVICE));
        }
        else {
            call_resize_kernel(s->cu_dl_func, s->cu_func_uchar, s->cu_tex_uchar, 1,
                    src->dptr, src->w, src->h, src->pitch,
                    dst->dptr, dst->w, dst->h, dst->pitch);

            call_resize_kernel(s->cu_dl_func, s->cu_func_uchar2, s->cu_tex_uchar2, 2,
                    src->dptr+src->pitch*src->h, src->w/2, src->h/2, src->pitch,
                    dst->dptr+dst->pitch*dst->h, dst->w/2, dst->h/2, dst->pitch/2);
        }

        break;

    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_ABGR:
    case AV_PIX_FMT_BGRA:
        if (src->w == dst->w && src->h == dst->h) {
            __cu(cuMemCpy2d(NULL, src->dptr, src->pitch, NULL, dst->dptr, dst->pitch, src->w*4, src->h, CU_MEMORYTYPE_DEVICE, CU_MEMORYTYPE_DEVICE));

        }
        else {
            call_resize_kernel(s->cu_dl_func, s->cu_func_uchar4, s->cu_tex_uchar4, 4,
                    src->dptr, src->w, src->h, src->pitch,
                    dst->dptr, dst->w, dst->h, dst->pitch/4);
        }

        break;

    default:
        av_log(NULL, AV_LOG_FATAL, "Unsupported input format: %s!\n", av_get_pix_fmt_name(format));
        return -1;
    }

    return 0;
}

static cu_tex* find_resize_src(NVResizeContext* s, cu_tex* source, cu_tex* target)
{
    int offset;
    cu_tex* src;
    if (source == NULL) {
        return &s->intex;
    }

    if (target->w * 4 > source->w) {
        return source;
    }

    offset = target - s->outtex;
    for (int i = offset - 1; i >= 0; i--) {
        if (target->w * 4 > s->outtex[i].w) {
            return &s->outtex[i];
        }
    }

    src = (offset == 0 ? source : &s->outtex[offset-1]);
    av_log(NULL, AV_LOG_WARNING, "Output resolution %dx%d differs too much from the previous level %dx%d, "
            "might cause artificial\n", target->w, target->h, src->w, src->h);

    return src;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    NVResizeContext *s = ctx->priv;
    int i;
    cu_tex* resize_src = NULL;
    ffnvinfo* info;

    // copy input to gpu
    if (in->opaque && check_nvinfo(in->opaque) && ((ffnvinfo*)(in->opaque))->dptr[0]) {
        ffnvinfo* info = (ffnvinfo*)in->opaque;
        s->intex.dptr = info->dptr[0];
        s->intex.pitch = info->linesize[0];
        s->intex.w = in->width;
        s->intex.h = in->height;
    }
    else {
        if ( (in->width != s->intex.h || in->height != s->intex.h) &&
             !s->intex.dptr) {
            __cu(s->cu_dl_func->cu_mem_free(s->intex.dptr));
            s->intex.w = in->width;
            s->intex.h = in->height;
            s->intex.dptr = (CUdeviceptr)NULL;
        }
        if (!s->intex.dptr) {
            switch (in->format) {
            case AV_PIX_FMT_YUV420P:
            case AV_PIX_FMT_NV12:
                __cu(s->cu_dl_func->cu_mem_alloc_pitch(&s->intex.dptr, &s->intex.pitch, s->intex.w, s->intex.h*3/2, 16));
                break;
            case AV_PIX_FMT_YUV444P:
                __cu(s->cu_dl_func->cu_mem_alloc_pitch(&s->intex.dptr, &s->intex.pitch, s->intex.w, s->intex.h*3, 16));
                break;
            case AV_PIX_FMT_ARGB:
            case AV_PIX_FMT_RGBA:
            case AV_PIX_FMT_ABGR:
            case AV_PIX_FMT_BGRA:
                __cu(s->cu_dl_func->cu_mem_alloc_pitch(&s->intex.dptr, &s->intex.pitch, s->intex.w*4, s->intex.h, 16));
                break;
            default:
                av_log(NULL, AV_LOG_FATAL, "Unsupported input format: %s!\n", av_get_pix_fmt_name(in->format));
                return -1;
            }
        }
        copy_from_avframe(s, in, &s->intex);
    }

    for (i = 0; i < ctx->nb_outputs; i++) {
        AVFrame *out;
        if (ctx->outputs[i]->closed)
            continue;

        out = ff_get_video_buffer(ctx->outputs[i], ctx->outputs[i]->w, ctx->outputs[i]->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);

        // do works here
        resize_src = find_resize_src(s, resize_src, &s->outtex[i]);
        do_cuda_resize(s, resize_src, &s->outtex[i], in->format);
        info = init_nvinfo();
        switch (out->format) {
        case AV_PIX_FMT_YUV444P:
            info->dptr[0] = s->outtex[i].dptr;
            info->dptr[1] = s->outtex[i].dptr + s->outtex[i].pitch*s->outtex[i].h;
            info->dptr[2] = s->outtex[i].dptr + s->outtex[i].pitch*s->outtex[i].h*2;
            info->linesize[0] = info->linesize[1] = info->linesize[2] = s->outtex[i].pitch;
            break;

        case AV_PIX_FMT_YUV420P:
            info->dptr[0] = s->outtex[i].dptr;
            info->dptr[1] = s->outtex[i].dptr + s->outtex[i].pitch*s->outtex[i].h;
            info->dptr[2] = s->outtex[i].dptr + s->outtex[i].pitch*s->outtex[i].h*5/4;
            info->linesize[0] = s->outtex[i].pitch;
            info->linesize[1] = info->linesize[2] = s->outtex[i].pitch/2;
            break;

        case AV_PIX_FMT_NV12:
            info->dptr[0] = s->outtex[i].dptr;
            info->dptr[1] = s->outtex[i].dptr + s->outtex[i].pitch*s->outtex[i].h;
            info->linesize[0] = info->linesize[1] = s->outtex[i].pitch;
            break;

        case AV_PIX_FMT_ARGB:
        case AV_PIX_FMT_RGBA:
        case AV_PIX_FMT_ABGR:
        case AV_PIX_FMT_BGRA:
            info->dptr[0] = s->outtex[i].dptr;
            info->linesize[0] = s->outtex[i].pitch;
            break;

        default:
            break;
        }

        out->opaque = (void*)info;
        if (s->readback_FB)
            copy_to_avframe(s, &s->outtex[i], out);

        if (ff_filter_frame(ctx->outputs[i], out) < 0)
            break;
    }

    av_frame_free(&in);
    return 0;
}


static av_cold void uninit(AVFilterContext *ctx)
{
    NVResizeContext *s = ctx->priv;

    for (int i = 0; i < s->nb_outputs; i++) {
        av_freep(&ctx->output_pads[i].name);
        if(s->outtex[i].dptr) s->cu_dl_func->cu_mem_free(s->outtex[i].dptr);
    }
    if(s->cu_ctx) release_cuda_context(&s->cu_ctx, s->gpu);

    av_log(ctx, AV_LOG_INFO, "nvresize::uninit\n");

}

static const AVFilterPad nvresize_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = filter_frame,
    },
    { NULL }
};

AVFilter ff_vf_nvresize = {
    .name = "nvresize",
    .description = NULL_IF_CONFIG_SMALL("GPU accelerated video resizer."),
    .inputs  = nvresize_inputs,
    .outputs = NULL,
    .flags   = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
    .priv_class = &nvresize_class,
    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,
    .priv_size = sizeof(NVResizeContext),
};
