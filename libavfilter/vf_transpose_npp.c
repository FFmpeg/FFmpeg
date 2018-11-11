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

#include <nppi.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, device_hwctx->internal->cuda_dl, x)

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P
};

enum TransposeStage {
    STAGE_ROTATE,
    STAGE_TRANSPOSE,
    STAGE_NB
};

enum Transpose {
    NPP_TRANSPOSE_CCLOCK_FLIP = 0,
    NPP_TRANSPOSE_CLOCK = 1,
    NPP_TRANSPOSE_CCLOCK = 2,
    NPP_TRANSPOSE_CLOCK_FLIP = 3
};

enum Passthrough {
    NPP_TRANSPOSE_PT_TYPE_NONE = 0,
    NPP_TRANSPOSE_PT_TYPE_LANDSCAPE,
    NPP_TRANSPOSE_PT_TYPE_PORTRAIT
};

typedef struct NPPTransposeStageContext {
    int stage_needed;
    enum AVPixelFormat in_fmt;
    enum AVPixelFormat out_fmt;
    struct {
        int width;
        int height;
    } planes_in[3], planes_out[3];
    AVBufferRef *frames_ctx;
    AVFrame     *frame;
} NPPTransposeStageContext;

typedef struct NPPTransposeContext {
    const AVClass *class;
    NPPTransposeStageContext stages[STAGE_NB];
    AVFrame *tmp_frame;

    int passthrough;    ///< PassthroughType, landscape passthrough mode enabled
    int dir;            ///< TransposeDir
} NPPTransposeContext;

static int npptranspose_init(AVFilterContext *ctx)
{
    NPPTransposeContext *s = ctx->priv;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(s->stages); i++) {
        s->stages[i].frame = av_frame_alloc();
        if (!s->stages[i].frame)
            return AVERROR(ENOMEM);
    }

    s->tmp_frame = av_frame_alloc();
    if (!s->tmp_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static void npptranspose_uninit(AVFilterContext *ctx)
{
    NPPTransposeContext *s = ctx->priv;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(s->stages); i++) {
        av_frame_free(&s->stages[i].frame);
        av_buffer_unref(&s->stages[i].frames_ctx);
    }

    av_frame_free(&s->tmp_frame);
}

static int npptranspose_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_formats[] = {
        AV_PIX_FMT_CUDA, AV_PIX_FMT_NONE,
    };

    AVFilterFormats *pix_fmts = ff_make_format_list(pixel_formats);
    return ff_set_common_formats(ctx, pix_fmts);
}

static int init_stage(NPPTransposeStageContext *stage, AVBufferRef *device_ctx)
{
    AVBufferRef *out_ref = NULL;
    AVHWFramesContext *out_ctx;
    int in_sw, in_sh, out_sw, out_sh;
    int ret, i;

    av_pix_fmt_get_chroma_sub_sample(stage->in_fmt,  &in_sw,  &in_sh);
    av_pix_fmt_get_chroma_sub_sample(stage->out_fmt, &out_sw, &out_sh);

    if (!stage->planes_out[0].width) {
        stage->planes_out[0].width  = stage->planes_in[0].width;
        stage->planes_out[0].height = stage->planes_in[0].height;
    }

    for (i = 1; i < FF_ARRAY_ELEMS(stage->planes_in); i++) {
        stage->planes_in[i].width   = stage->planes_in[0].width   >> in_sw;
        stage->planes_in[i].height  = stage->planes_in[0].height  >> in_sh;
        stage->planes_out[i].width  = stage->planes_out[0].width  >> out_sw;
        stage->planes_out[i].height = stage->planes_out[0].height >> out_sh;
    }

    out_ref = av_hwframe_ctx_alloc(device_ctx);
    if (!out_ref)
        return AVERROR(ENOMEM);
    out_ctx = (AVHWFramesContext*)out_ref->data;

    out_ctx->format    = AV_PIX_FMT_CUDA;
    out_ctx->sw_format = stage->out_fmt;
    out_ctx->width     = FFALIGN(stage->planes_out[0].width,  32);
    out_ctx->height    = FFALIGN(stage->planes_out[0].height, 32);

    ret = av_hwframe_ctx_init(out_ref);
    if (ret < 0)
        goto fail;

    av_frame_unref(stage->frame);
    ret = av_hwframe_get_buffer(out_ref, stage->frame, 0);
    if (ret < 0)
        goto fail;

    stage->frame->width  = stage->planes_out[0].width;
    stage->frame->height = stage->planes_out[0].height;
    av_buffer_unref(&stage->frames_ctx);
    stage->frames_ctx = out_ref;

    return 0;

fail:
    av_buffer_unref(&out_ref);
    return ret;
}

static int format_is_supported(enum AVPixelFormat fmt)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;

    return 0;
}

static int init_processing_chain(AVFilterContext *ctx, int in_width, int in_height,
                                 int out_width, int out_height)
{
    NPPTransposeContext *s = ctx->priv;
    AVHWFramesContext *in_frames_ctx;
    enum AVPixelFormat format;
    int i, ret, last_stage = -1;
    int rot_width = out_width, rot_height = out_height;

    /* check that we have a hw context */
    if (!ctx->inputs[0]->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }

    in_frames_ctx = (AVHWFramesContext*)ctx->inputs[0]->hw_frames_ctx->data;
    format        = in_frames_ctx->sw_format;

    if (!format_is_supported(format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n",
               av_get_pix_fmt_name(format));
        return AVERROR(ENOSYS);
    }

    if (s->dir != NPP_TRANSPOSE_CCLOCK_FLIP) {
        s->stages[STAGE_ROTATE].stage_needed = 1;
    }

    if (s->dir == NPP_TRANSPOSE_CCLOCK_FLIP || s->dir == NPP_TRANSPOSE_CLOCK_FLIP) {
        s->stages[STAGE_TRANSPOSE].stage_needed = 1;

        /* Rotating by 180° in case of clock_flip, or not at all for cclock_flip, so width/height unchanged by rotation */
        rot_width = in_width;
        rot_height = in_height;
    }

    s->stages[STAGE_ROTATE].in_fmt               = format;
    s->stages[STAGE_ROTATE].out_fmt              = format;
    s->stages[STAGE_ROTATE].planes_in[0].width   = in_width;
    s->stages[STAGE_ROTATE].planes_in[0].height  = in_height;
    s->stages[STAGE_ROTATE].planes_out[0].width  = rot_width;
    s->stages[STAGE_ROTATE].planes_out[0].height = rot_height;
    s->stages[STAGE_TRANSPOSE].in_fmt               = format;
    s->stages[STAGE_TRANSPOSE].out_fmt              = format;
    s->stages[STAGE_TRANSPOSE].planes_in[0].width   = rot_width;
    s->stages[STAGE_TRANSPOSE].planes_in[0].height  = rot_height;
    s->stages[STAGE_TRANSPOSE].planes_out[0].width  = out_width;
    s->stages[STAGE_TRANSPOSE].planes_out[0].height = out_height;

    /* init the hardware contexts */
    for (i = 0; i < FF_ARRAY_ELEMS(s->stages); i++) {
        if (!s->stages[i].stage_needed)
            continue;
        ret = init_stage(&s->stages[i], in_frames_ctx->device_ref);
        if (ret < 0)
            return ret;
        last_stage = i;
    }

    if (last_stage >= 0) {
        ctx->outputs[0]->hw_frames_ctx = av_buffer_ref(s->stages[last_stage].frames_ctx);
    } else {
        ctx->outputs[0]->hw_frames_ctx = av_buffer_ref(ctx->inputs[0]->hw_frames_ctx);
        s->passthrough = 1;
    }

    if (!ctx->outputs[0]->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static int npptranspose_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx   = outlink->src;
    AVFilterLink *inlink   = ctx->inputs[0];
    NPPTransposeContext *s = ctx->priv;
    int ret;

    if ((inlink->w >= inlink->h && s->passthrough == NPP_TRANSPOSE_PT_TYPE_LANDSCAPE) ||
        (inlink->w <= inlink->h && s->passthrough == NPP_TRANSPOSE_PT_TYPE_PORTRAIT))
    {
        if (inlink->hw_frames_ctx) {
            outlink->hw_frames_ctx = av_buffer_ref(inlink->hw_frames_ctx);
            if (!outlink->hw_frames_ctx)
                return AVERROR(ENOMEM);
        }

        av_log(ctx, AV_LOG_VERBOSE,
               "w:%d h:%d -> w:%d h:%d (passthrough mode)\n",
               inlink->w, inlink->h, inlink->w, inlink->h);
        return 0;
    } else {
        s->passthrough = NPP_TRANSPOSE_PT_TYPE_NONE;
    }

    outlink->w = inlink->h;
    outlink->h = inlink->w;
    outlink->sample_aspect_ratio = (AVRational){inlink->sample_aspect_ratio.den, inlink->sample_aspect_ratio.num};

    ret = init_processing_chain(ctx, inlink->w, inlink->h, outlink->w, outlink->h);
    if (ret < 0)
        return ret;

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d -transpose-> w:%d h:%d\n",
           inlink->w, inlink->h, outlink->w, outlink->h);

    return 0;
}

static int npptranspose_rotate(AVFilterContext *ctx, NPPTransposeStageContext *stage,
                               AVFrame *out, AVFrame *in)
{
    NPPTransposeContext *s = ctx->priv;
    NppStatus err;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(stage->planes_in) && i < FF_ARRAY_ELEMS(in->data) && in->data[i]; i++) {
        int iw = stage->planes_in[i].width;
        int ih = stage->planes_in[i].height;
        int ow = stage->planes_out[i].width;
        int oh = stage->planes_out[i].height;

        // nppRotate uses 0,0 as the rotation point
        // need to shift the image accordingly after rotation
        // need to substract 1 to get the correct coordinates
        double angle = s->dir == NPP_TRANSPOSE_CLOCK ? -90.0 : s->dir == NPP_TRANSPOSE_CCLOCK ? 90.0 : 180.0;
        int shiftw = (s->dir == NPP_TRANSPOSE_CLOCK  || s->dir == NPP_TRANSPOSE_CLOCK_FLIP) ? ow - 1 : 0;
        int shifth = (s->dir == NPP_TRANSPOSE_CCLOCK || s->dir == NPP_TRANSPOSE_CLOCK_FLIP) ? oh - 1 : 0;

        err = nppiRotate_8u_C1R(in->data[i], (NppiSize){ iw, ih },
                                in->linesize[i], (NppiRect){ 0, 0, iw, ih },
                                out->data[i], out->linesize[i],
                                (NppiRect){ 0, 0, ow, oh },
                                angle, shiftw, shifth, NPPI_INTER_NN);
        if (err != NPP_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "NPP rotate error: %d\n", err);
            return AVERROR_UNKNOWN;
        }
    }

    return 0;
}

static int npptranspose_transpose(AVFilterContext *ctx, NPPTransposeStageContext *stage,
                                  AVFrame *out, AVFrame *in)
{
    NppStatus err;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(stage->planes_in) && i < FF_ARRAY_ELEMS(in->data) && in->data[i]; i++) {
        int iw = stage->planes_in[i].width;
        int ih = stage->planes_in[i].height;

        err = nppiTranspose_8u_C1R(in->data[i], in->linesize[i],
                                   out->data[i], out->linesize[i],
                                   (NppiSize){ iw, ih });
        if (err != NPP_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "NPP transpose error: %d\n", err);
            return AVERROR_UNKNOWN;
        }
    }

    return 0;
}

static int (*const npptranspose_process[])(AVFilterContext *ctx, NPPTransposeStageContext *stage,
                                           AVFrame *out, AVFrame *in) = {
    [STAGE_ROTATE]       = npptranspose_rotate,
    [STAGE_TRANSPOSE]    = npptranspose_transpose
};

static int npptranspose_filter(AVFilterContext *ctx, AVFrame *out, AVFrame *in)
{
    NPPTransposeContext *s = ctx->priv;
    AVFrame *src = in;
    int i, ret, last_stage = -1;

    for (i = 0; i < FF_ARRAY_ELEMS(s->stages); i++) {
        if (!s->stages[i].stage_needed)
            continue;

        ret = npptranspose_process[i](ctx, &s->stages[i], s->stages[i].frame, src);
        if (ret < 0)
            return ret;

        src        = s->stages[i].frame;
        last_stage = i;
    }

    if (last_stage < 0)
        return AVERROR_BUG;

    ret = av_hwframe_get_buffer(src->hw_frames_ctx, s->tmp_frame, 0);
    if (ret < 0)
        return ret;

    av_frame_move_ref(out, src);
    av_frame_move_ref(src, s->tmp_frame);

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        return ret;

    return 0;
}

static int npptranspose_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext              *ctx = link->dst;
    NPPTransposeContext            *s = ctx->priv;
    AVFilterLink             *outlink = ctx->outputs[0];
    AVHWFramesContext     *frames_ctx = (AVHWFramesContext*)outlink->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = frames_ctx->device_ctx->hwctx;
    AVFrame *out = NULL;
    CUcontext dummy;
    int ret = 0;

    if (s->passthrough)
        return ff_filter_frame(outlink, in);

    out = av_frame_alloc();
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = CHECK_CU(device_hwctx->internal->cuda_dl->cuCtxPushCurrent(device_hwctx->cuda_ctx));
    if (ret < 0)
        goto fail;

    ret = npptranspose_filter(ctx, out, in);

    CHECK_CU(device_hwctx->internal->cuda_dl->cuCtxPopCurrent(&dummy));
    if (ret < 0)
        goto fail;

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

#define OFFSET(x) offsetof(NPPTransposeContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption options[] = {
    { "dir", "set transpose direction", OFFSET(dir), AV_OPT_TYPE_INT, { .i64 = NPP_TRANSPOSE_CCLOCK_FLIP }, 0, 3, FLAGS, "dir" },
        { "cclock_flip", "rotate counter-clockwise with vertical flip", 0, AV_OPT_TYPE_CONST, { .i64 = NPP_TRANSPOSE_CCLOCK_FLIP }, 0, 0, FLAGS, "dir" },
        { "clock",       "rotate clockwise",                            0, AV_OPT_TYPE_CONST, { .i64 = NPP_TRANSPOSE_CLOCK       }, 0, 0, FLAGS, "dir" },
        { "cclock",      "rotate counter-clockwise",                    0, AV_OPT_TYPE_CONST, { .i64 = NPP_TRANSPOSE_CCLOCK      }, 0, 0, FLAGS, "dir" },
        { "clock_flip",  "rotate clockwise with vertical flip",         0, AV_OPT_TYPE_CONST, { .i64 = NPP_TRANSPOSE_CLOCK_FLIP  }, 0, 0, FLAGS, "dir" },
    { "passthrough", "do not apply transposition if the input matches the specified geometry", OFFSET(passthrough), AV_OPT_TYPE_INT, { .i64 = NPP_TRANSPOSE_PT_TYPE_NONE },  0, 2, FLAGS, "passthrough" },
        { "none",      "always apply transposition",  0, AV_OPT_TYPE_CONST, { .i64 = NPP_TRANSPOSE_PT_TYPE_NONE },      0, 0, FLAGS, "passthrough" },
        { "landscape", "preserve landscape geometry", 0, AV_OPT_TYPE_CONST, { .i64 = NPP_TRANSPOSE_PT_TYPE_LANDSCAPE }, 0, 0, FLAGS, "passthrough" },
        { "portrait",  "preserve portrait geometry",  0, AV_OPT_TYPE_CONST, { .i64 = NPP_TRANSPOSE_PT_TYPE_PORTRAIT },  0, 0, FLAGS, "passthrough" },
    { NULL },
};

static const AVClass npptranspose_class = {
    .class_name = "npptranspose",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad npptranspose_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = npptranspose_filter_frame,
    },
    { NULL }
};

static const AVFilterPad npptranspose_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = npptranspose_config_props,
    },
    { NULL }
};

AVFilter ff_vf_transpose_npp = {
    .name           = "transpose_npp",
    .description    = NULL_IF_CONFIG_SMALL("NVIDIA Performance Primitives video transpose"),
    .init           = npptranspose_init,
    .uninit         = npptranspose_uninit,
    .query_formats  = npptranspose_query_formats,
    .priv_size      = sizeof(NPPTransposeContext),
    .priv_class     = &npptranspose_class,
    .inputs         = npptranspose_inputs,
    .outputs        = npptranspose_outputs,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
