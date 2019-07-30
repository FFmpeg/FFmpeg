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
 * scale video filter - QSV
 */

#include <mfx/mfxvideo.h>

#include <stdio.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "libavfilter/qsvvpp.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

static const char *const var_names[] = {
    "PI",
    "PHI",
    "E",
    "in_w",   "iw",
    "in_h",   "ih",
    "out_w",  "ow",
    "out_h",  "oh",
    "a", "dar",
    "sar",
    NULL
};

enum var_name {
    VAR_PI,
    VAR_PHI,
    VAR_E,
    VAR_IN_W,   VAR_IW,
    VAR_IN_H,   VAR_IH,
    VAR_OUT_W,  VAR_OW,
    VAR_OUT_H,  VAR_OH,
    VAR_A, VAR_DAR,
    VAR_SAR,
    VARS_NB
};

#define QSV_HAVE_SCALING_CONFIG  QSV_VERSION_ATLEAST(1, 19)

typedef struct QSVScaleContext {
    const AVClass *class;

    /* a clone of the main session, used internally for scaling */
    mfxSession   session;

    mfxMemId *mem_ids_in;
    int nb_mem_ids_in;

    mfxMemId *mem_ids_out;
    int nb_mem_ids_out;

    mfxFrameSurface1 **surface_ptrs_in;
    int             nb_surface_ptrs_in;

    mfxFrameSurface1 **surface_ptrs_out;
    int             nb_surface_ptrs_out;

    mfxExtOpaqueSurfaceAlloc opaque_alloc;

#if QSV_HAVE_SCALING_CONFIG
    mfxExtVPPScaling         scale_conf;
#endif
    int                      mode;

    mfxExtBuffer             *ext_buffers[1 + QSV_HAVE_SCALING_CONFIG];
    int                      num_ext_buf;

    int shift_width, shift_height;

    /**
     * New dimensions. Special values are:
     *   0 = original width/height
     *  -1 = keep original aspect
     */
    int w, h;

    /**
     * Output sw format. AV_PIX_FMT_NONE for no conversion.
     */
    enum AVPixelFormat format;

    char *w_expr;               ///< width  expression string
    char *h_expr;               ///< height expression string
    char *format_str;
} QSVScaleContext;

static av_cold int qsvscale_init(AVFilterContext *ctx)
{
    QSVScaleContext *s = ctx->priv;

    if (!strcmp(s->format_str, "same")) {
        s->format = AV_PIX_FMT_NONE;
    } else {
        s->format = av_get_pix_fmt(s->format_str);
        if (s->format == AV_PIX_FMT_NONE) {
            av_log(ctx, AV_LOG_ERROR, "Unrecognized pixel format: %s\n", s->format_str);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static av_cold void qsvscale_uninit(AVFilterContext *ctx)
{
    QSVScaleContext *s = ctx->priv;

    if (s->session) {
        MFXClose(s->session);
        s->session = NULL;
    }

    av_freep(&s->mem_ids_in);
    av_freep(&s->mem_ids_out);
    s->nb_mem_ids_in  = 0;
    s->nb_mem_ids_out = 0;

    av_freep(&s->surface_ptrs_in);
    av_freep(&s->surface_ptrs_out);
    s->nb_surface_ptrs_in  = 0;
    s->nb_surface_ptrs_out = 0;
}

static int qsvscale_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_formats[] = {
        AV_PIX_FMT_QSV, AV_PIX_FMT_NONE,
    };
    AVFilterFormats *pix_fmts  = ff_make_format_list(pixel_formats);
    int ret;

    if ((ret = ff_set_common_formats(ctx, pix_fmts)) < 0)
        return ret;

    return 0;
}

static int init_out_pool(AVFilterContext *ctx,
                         int out_width, int out_height)
{
    QSVScaleContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    AVHWFramesContext *in_frames_ctx;
    AVHWFramesContext *out_frames_ctx;
    AVQSVFramesContext *in_frames_hwctx;
    AVQSVFramesContext *out_frames_hwctx;
    enum AVPixelFormat in_format;
    enum AVPixelFormat out_format;
    int i, ret;

    /* check that we have a hw context */
    if (!ctx->inputs[0]->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }
    in_frames_ctx   = (AVHWFramesContext*)ctx->inputs[0]->hw_frames_ctx->data;
    in_frames_hwctx = in_frames_ctx->hwctx;

    in_format     = in_frames_ctx->sw_format;
    out_format    = (s->format == AV_PIX_FMT_NONE) ? in_format : s->format;

    outlink->hw_frames_ctx = av_hwframe_ctx_alloc(in_frames_ctx->device_ref);
    if (!outlink->hw_frames_ctx)
        return AVERROR(ENOMEM);
    out_frames_ctx   = (AVHWFramesContext*)outlink->hw_frames_ctx->data;
    out_frames_hwctx = out_frames_ctx->hwctx;

    out_frames_ctx->format            = AV_PIX_FMT_QSV;
    out_frames_ctx->width             = FFALIGN(out_width,  16);
    out_frames_ctx->height            = FFALIGN(out_height, 16);
    out_frames_ctx->sw_format         = out_format;
    out_frames_ctx->initial_pool_size = 4;

    out_frames_hwctx->frame_type = in_frames_hwctx->frame_type;

    ret = ff_filter_init_hw_frames(ctx, outlink, 32);
    if (ret < 0)
        return ret;

    ret = av_hwframe_ctx_init(outlink->hw_frames_ctx);
    if (ret < 0)
        return ret;

    for (i = 0; i < out_frames_hwctx->nb_surfaces; i++) {
        mfxFrameInfo *info = &out_frames_hwctx->surfaces[i].Info;
        info->CropW = out_width;
        info->CropH = out_height;
    }

    return 0;
}

static mfxStatus frame_alloc(mfxHDL pthis, mfxFrameAllocRequest *req,
                             mfxFrameAllocResponse *resp)
{
    AVFilterContext *ctx = pthis;
    QSVScaleContext   *s = ctx->priv;

    if (!(req->Type & MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET) ||
        !(req->Type & (MFX_MEMTYPE_FROM_VPPIN | MFX_MEMTYPE_FROM_VPPOUT)) ||
        !(req->Type & MFX_MEMTYPE_EXTERNAL_FRAME))
        return MFX_ERR_UNSUPPORTED;

    if (req->Type & MFX_MEMTYPE_FROM_VPPIN) {
        resp->mids           = s->mem_ids_in;
        resp->NumFrameActual = s->nb_mem_ids_in;
    } else {
        resp->mids           = s->mem_ids_out;
        resp->NumFrameActual = s->nb_mem_ids_out;
    }

    return MFX_ERR_NONE;
}

static mfxStatus frame_free(mfxHDL pthis, mfxFrameAllocResponse *resp)
{
    return MFX_ERR_NONE;
}

static mfxStatus frame_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
    return MFX_ERR_UNSUPPORTED;
}

static mfxStatus frame_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
    return MFX_ERR_UNSUPPORTED;
}

static mfxStatus frame_get_hdl(mfxHDL pthis, mfxMemId mid, mfxHDL *hdl)
{
    *hdl = mid;
    return MFX_ERR_NONE;
}

static const mfxHandleType handle_types[] = {
    MFX_HANDLE_VA_DISPLAY,
    MFX_HANDLE_D3D9_DEVICE_MANAGER,
    MFX_HANDLE_D3D11_DEVICE,
};

static int init_out_session(AVFilterContext *ctx)
{

    QSVScaleContext                   *s = ctx->priv;
    AVHWFramesContext     *in_frames_ctx = (AVHWFramesContext*)ctx->inputs[0]->hw_frames_ctx->data;
    AVHWFramesContext    *out_frames_ctx = (AVHWFramesContext*)ctx->outputs[0]->hw_frames_ctx->data;
    AVQSVFramesContext  *in_frames_hwctx = in_frames_ctx->hwctx;
    AVQSVFramesContext *out_frames_hwctx = out_frames_ctx->hwctx;
    AVQSVDeviceContext     *device_hwctx = in_frames_ctx->device_ctx->hwctx;

    int opaque = !!(in_frames_hwctx->frame_type & MFX_MEMTYPE_OPAQUE_FRAME);

    mfxHDL handle = NULL;
    mfxHandleType handle_type;
    mfxVersion ver;
    mfxIMPL impl;
    mfxVideoParam par;
    mfxStatus err;
    int i;

    s->num_ext_buf = 0;

    /* extract the properties of the "master" session given to us */
    err = MFXQueryIMPL(device_hwctx->session, &impl);
    if (err == MFX_ERR_NONE)
        err = MFXQueryVersion(device_hwctx->session, &ver);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error querying the session attributes\n");
        return AVERROR_UNKNOWN;
    }

    for (i = 0; i < FF_ARRAY_ELEMS(handle_types); i++) {
        err = MFXVideoCORE_GetHandle(device_hwctx->session, handle_types[i], &handle);
        if (err == MFX_ERR_NONE) {
            handle_type = handle_types[i];
            break;
        }
    }

    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error getting the session handle\n");
        return AVERROR_UNKNOWN;
    }

    /* create a "slave" session with those same properties, to be used for
     * actual scaling */
    err = MFXInit(impl, &ver, &s->session);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error initializing a session for scaling\n");
        return AVERROR_UNKNOWN;
    }

    if (handle) {
        err = MFXVideoCORE_SetHandle(s->session, handle_type, handle);
        if (err != MFX_ERR_NONE)
            return AVERROR_UNKNOWN;
    }

    if (QSV_RUNTIME_VERSION_ATLEAST(ver, 1, 25)) {
        err = MFXJoinSession(device_hwctx->session, s->session);
            if (err != MFX_ERR_NONE)
                return AVERROR_UNKNOWN;
    }

    memset(&par, 0, sizeof(par));

    if (opaque) {
        s->surface_ptrs_in = av_mallocz_array(in_frames_hwctx->nb_surfaces,
                                              sizeof(*s->surface_ptrs_in));
        if (!s->surface_ptrs_in)
            return AVERROR(ENOMEM);
        for (i = 0; i < in_frames_hwctx->nb_surfaces; i++)
            s->surface_ptrs_in[i] = in_frames_hwctx->surfaces + i;
        s->nb_surface_ptrs_in = in_frames_hwctx->nb_surfaces;

        s->surface_ptrs_out = av_mallocz_array(out_frames_hwctx->nb_surfaces,
                                               sizeof(*s->surface_ptrs_out));
        if (!s->surface_ptrs_out)
            return AVERROR(ENOMEM);
        for (i = 0; i < out_frames_hwctx->nb_surfaces; i++)
            s->surface_ptrs_out[i] = out_frames_hwctx->surfaces + i;
        s->nb_surface_ptrs_out = out_frames_hwctx->nb_surfaces;

        s->opaque_alloc.In.Surfaces   = s->surface_ptrs_in;
        s->opaque_alloc.In.NumSurface = s->nb_surface_ptrs_in;
        s->opaque_alloc.In.Type       = in_frames_hwctx->frame_type;

        s->opaque_alloc.Out.Surfaces   = s->surface_ptrs_out;
        s->opaque_alloc.Out.NumSurface = s->nb_surface_ptrs_out;
        s->opaque_alloc.Out.Type       = out_frames_hwctx->frame_type;

        s->opaque_alloc.Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
        s->opaque_alloc.Header.BufferSz = sizeof(s->opaque_alloc);

        s->ext_buffers[s->num_ext_buf++] = (mfxExtBuffer*)&s->opaque_alloc;

        par.IOPattern = MFX_IOPATTERN_IN_OPAQUE_MEMORY | MFX_IOPATTERN_OUT_OPAQUE_MEMORY;
    } else {
        mfxFrameAllocator frame_allocator = {
            .pthis  = ctx,
            .Alloc  = frame_alloc,
            .Lock   = frame_lock,
            .Unlock = frame_unlock,
            .GetHDL = frame_get_hdl,
            .Free   = frame_free,
        };

        s->mem_ids_in = av_mallocz_array(in_frames_hwctx->nb_surfaces,
                                         sizeof(*s->mem_ids_in));
        if (!s->mem_ids_in)
            return AVERROR(ENOMEM);
        for (i = 0; i < in_frames_hwctx->nb_surfaces; i++)
            s->mem_ids_in[i] = in_frames_hwctx->surfaces[i].Data.MemId;
        s->nb_mem_ids_in = in_frames_hwctx->nb_surfaces;

        s->mem_ids_out = av_mallocz_array(out_frames_hwctx->nb_surfaces,
                                          sizeof(*s->mem_ids_out));
        if (!s->mem_ids_out)
            return AVERROR(ENOMEM);
        for (i = 0; i < out_frames_hwctx->nb_surfaces; i++)
            s->mem_ids_out[i] = out_frames_hwctx->surfaces[i].Data.MemId;
        s->nb_mem_ids_out = out_frames_hwctx->nb_surfaces;

        err = MFXVideoCORE_SetFrameAllocator(s->session, &frame_allocator);
        if (err != MFX_ERR_NONE)
            return AVERROR_UNKNOWN;

        par.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;
    }

#if QSV_HAVE_SCALING_CONFIG
    memset(&s->scale_conf, 0, sizeof(mfxExtVPPScaling));
    s->scale_conf.Header.BufferId     = MFX_EXTBUFF_VPP_SCALING;
    s->scale_conf.Header.BufferSz     = sizeof(mfxExtVPPScaling);
    s->scale_conf.ScalingMode         = s->mode;
    s->ext_buffers[s->num_ext_buf++]  = (mfxExtBuffer*)&s->scale_conf;
    av_log(ctx, AV_LOG_VERBOSE, "Scaling mode: %"PRIu16"\n", s->mode);
#endif

    par.ExtParam    = s->ext_buffers;
    par.NumExtParam = s->num_ext_buf;

    par.AsyncDepth = 1;    // TODO async

    par.vpp.In  = in_frames_hwctx->surfaces[0].Info;
    par.vpp.Out = out_frames_hwctx->surfaces[0].Info;

    /* Apparently VPP requires the frame rate to be set to some value, otherwise
     * init will fail (probably for the framerate conversion filter). Since we
     * are only doing scaling here, we just invent an arbitrary
     * value */
    par.vpp.In.FrameRateExtN  = 25;
    par.vpp.In.FrameRateExtD  = 1;
    par.vpp.Out.FrameRateExtN = 25;
    par.vpp.Out.FrameRateExtD = 1;

    err = MFXVideoVPP_Init(s->session, &par);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error opening the VPP for scaling\n");
        return AVERROR_UNKNOWN;
    }

    return 0;
}

static int init_scale_session(AVFilterContext *ctx, int in_width, int in_height,
                              int out_width, int out_height)
{
    int ret;

    qsvscale_uninit(ctx);

    ret = init_out_pool(ctx, out_width, out_height);
    if (ret < 0)
        return ret;

    ret = init_out_session(ctx);
    if (ret < 0)
        return ret;

    return 0;
}

static int qsvscale_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    QSVScaleContext  *s = ctx->priv;
    int64_t w, h;
    double var_values[VARS_NB], res;
    char *expr;
    int ret;

    var_values[VAR_PI]    = M_PI;
    var_values[VAR_PHI]   = M_PHI;
    var_values[VAR_E]     = M_E;
    var_values[VAR_IN_W]  = var_values[VAR_IW] = inlink->w;
    var_values[VAR_IN_H]  = var_values[VAR_IH] = inlink->h;
    var_values[VAR_OUT_W] = var_values[VAR_OW] = NAN;
    var_values[VAR_OUT_H] = var_values[VAR_OH] = NAN;
    var_values[VAR_A]     = (double) inlink->w / inlink->h;
    var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]   = var_values[VAR_A] * var_values[VAR_SAR];

    /* evaluate width and height */
    av_expr_parse_and_eval(&res, (expr = s->w_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    s->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->h_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail;
    s->h = var_values[VAR_OUT_H] = var_values[VAR_OH] = res;
    /* evaluate again the width, as it may depend on the output height */
    if ((ret = av_expr_parse_and_eval(&res, (expr = s->w_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail;
    s->w = res;

    w = s->w;
    h = s->h;

    /* sanity check params */
    if (w <  -1 || h <  -1) {
        av_log(ctx, AV_LOG_ERROR, "Size values less than -1 are not acceptable.\n");
        return AVERROR(EINVAL);
    }
    if (w == -1 && h == -1)
        s->w = s->h = 0;

    if (!(w = s->w))
        w = inlink->w;
    if (!(h = s->h))
        h = inlink->h;
    if (w == -1)
        w = av_rescale(h, inlink->w, inlink->h);
    if (h == -1)
        h = av_rescale(w, inlink->h, inlink->w);

    if (w > INT_MAX || h > INT_MAX ||
        (h * inlink->w) > INT_MAX  ||
        (w * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Rescaled value for width or height is too big.\n");

    outlink->w = w;
    outlink->h = h;

    ret = init_scale_session(ctx, inlink->w, inlink->h, w, h);
    if (ret < 0)
        return ret;

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d -> w:%d h:%d\n",
           inlink->w, inlink->h, outlink->w, outlink->h);

    if (inlink->sample_aspect_ratio.num)
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h*inlink->w,
                                                             outlink->w*inlink->h},
                                                inlink->sample_aspect_ratio);
    else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    return 0;

fail:
    av_log(NULL, AV_LOG_ERROR,
           "Error when evaluating the expression '%s'\n", expr);
    return ret;
}

static int qsvscale_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext             *ctx = link->dst;
    QSVScaleContext               *s = ctx->priv;
    AVFilterLink            *outlink = ctx->outputs[0];

    mfxSyncPoint sync = NULL;
    mfxStatus err;

    AVFrame *out = NULL;
    int ret = 0;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    do {
        err = MFXVideoVPP_RunFrameVPPAsync(s->session,
                                           (mfxFrameSurface1*)in->data[3],
                                           (mfxFrameSurface1*)out->data[3],
                                           NULL, &sync);
        if (err == MFX_WRN_DEVICE_BUSY)
            av_usleep(1);
    } while (err == MFX_WRN_DEVICE_BUSY);

    if (err < 0 || !sync) {
        av_log(ctx, AV_LOG_ERROR, "Error during scaling\n");
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    do {
        err = MFXVideoCORE_SyncOperation(s->session, sync, 1000);
    } while (err == MFX_WRN_IN_EXECUTION);
    if (err < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error synchronizing the operation: %d\n", err);
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        goto fail;

    out->width  = outlink->w;
    out->height = outlink->h;

    av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
              (int64_t)in->sample_aspect_ratio.num * outlink->h * link->w,
              (int64_t)in->sample_aspect_ratio.den * outlink->w * link->h,
              INT_MAX);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

#define OFFSET(x) offsetof(QSVScaleContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption options[] = {
    { "w",      "Output video width",  OFFSET(w_expr),     AV_OPT_TYPE_STRING, { .str = "iw"   }, .flags = FLAGS },
    { "h",      "Output video height", OFFSET(h_expr),     AV_OPT_TYPE_STRING, { .str = "ih"   }, .flags = FLAGS },
    { "format", "Output pixel format", OFFSET(format_str), AV_OPT_TYPE_STRING, { .str = "same" }, .flags = FLAGS },

#if QSV_HAVE_SCALING_CONFIG
    { "mode",      "set scaling mode",    OFFSET(mode),    AV_OPT_TYPE_INT,    { .i64 = MFX_SCALING_MODE_DEFAULT}, MFX_SCALING_MODE_DEFAULT, MFX_SCALING_MODE_QUALITY, FLAGS, "mode"},
    { "low_power", "low power mode",        0,             AV_OPT_TYPE_CONST,  { .i64 = MFX_SCALING_MODE_LOWPOWER}, INT_MIN, INT_MAX, FLAGS, "mode"},
    { "hq",        "high quality mode",     0,             AV_OPT_TYPE_CONST,  { .i64 = MFX_SCALING_MODE_QUALITY},  INT_MIN, INT_MAX, FLAGS, "mode"},
#else
    { "mode",      "(not supported)",     OFFSET(mode),    AV_OPT_TYPE_INT,    { .i64 = 0}, 0, INT_MAX, FLAGS, "mode"},
    { "low_power", "",                      0,             AV_OPT_TYPE_CONST,  { .i64 = 1}, 0,   0,     FLAGS, "mode"},
    { "hq",        "",                      0,             AV_OPT_TYPE_CONST,  { .i64 = 2}, 0,   0,     FLAGS, "mode"},
#endif

    { NULL },
};

static const AVClass qsvscale_class = {
    .class_name = "qsvscale",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad qsvscale_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = qsvscale_filter_frame,
    },
    { NULL }
};

static const AVFilterPad qsvscale_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = qsvscale_config_props,
    },
    { NULL }
};

AVFilter ff_vf_scale_qsv = {
    .name      = "scale_qsv",
    .description = NULL_IF_CONFIG_SMALL("QuickSync video scaling and format conversion"),

    .init          = qsvscale_init,
    .uninit        = qsvscale_uninit,
    .query_formats = qsvscale_query_formats,

    .priv_size = sizeof(QSVScaleContext),
    .priv_class = &qsvscale_class,

    .inputs    = qsvscale_inputs,
    .outputs   = qsvscale_outputs,

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
