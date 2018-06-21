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
 * deinterlace video filter - QSV
 */

#include <mfx/mfxvideo.h>

#include <stdio.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/common.h"
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

enum {
    QSVDEINT_MORE_OUTPUT = 1,
    QSVDEINT_MORE_INPUT,
};

typedef struct QSVFrame {
    AVFrame *frame;
    mfxFrameSurface1 surface;
    int used;

    struct QSVFrame *next;
} QSVFrame;

typedef struct QSVDeintContext {
    const AVClass *class;

    AVBufferRef *hw_frames_ctx;
    /* a clone of the main session, used internally for deinterlacing */
    mfxSession   session;

    mfxMemId *mem_ids;
    int    nb_mem_ids;

    mfxFrameSurface1 **surface_ptrs;
    int             nb_surface_ptrs;

    mfxExtOpaqueSurfaceAlloc opaque_alloc;
    mfxExtVPPDeinterlacing   deint_conf;
    mfxExtBuffer            *ext_buffers[2];
    int                      num_ext_buffers;

    QSVFrame *work_frames;

    int64_t last_pts;

    int eof;

    /* option for Deinterlacing algorithm to be used */
    int mode;
} QSVDeintContext;

static void qsvdeint_uninit(AVFilterContext *ctx)
{
    QSVDeintContext *s = ctx->priv;
    QSVFrame *cur;

    if (s->session) {
        MFXClose(s->session);
        s->session = NULL;
    }
    av_buffer_unref(&s->hw_frames_ctx);

    cur = s->work_frames;
    while (cur) {
        s->work_frames = cur->next;
        av_frame_free(&cur->frame);
        av_freep(&cur);
        cur = s->work_frames;
    }

    av_freep(&s->mem_ids);
    s->nb_mem_ids = 0;

    av_freep(&s->surface_ptrs);
    s->nb_surface_ptrs = 0;
}

static int qsvdeint_query_formats(AVFilterContext *ctx)
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

static mfxStatus frame_alloc(mfxHDL pthis, mfxFrameAllocRequest *req,
                             mfxFrameAllocResponse *resp)
{
    AVFilterContext *ctx = pthis;
    QSVDeintContext   *s = ctx->priv;

    if (!(req->Type & MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET) ||
        !(req->Type & (MFX_MEMTYPE_FROM_VPPIN | MFX_MEMTYPE_FROM_VPPOUT)) ||
        !(req->Type & MFX_MEMTYPE_EXTERNAL_FRAME))
        return MFX_ERR_UNSUPPORTED;

    resp->mids           = s->mem_ids;
    resp->NumFrameActual = s->nb_mem_ids;

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

    QSVDeintContext                  *s = ctx->priv;
    AVHWFramesContext    *hw_frames_ctx = (AVHWFramesContext*)s->hw_frames_ctx->data;
    AVQSVFramesContext *hw_frames_hwctx = hw_frames_ctx->hwctx;
    AVQSVDeviceContext    *device_hwctx = hw_frames_ctx->device_ctx->hwctx;

    int opaque = !!(hw_frames_hwctx->frame_type & MFX_MEMTYPE_OPAQUE_FRAME);

    mfxHDL handle = NULL;
    mfxHandleType handle_type;
    mfxVersion ver;
    mfxIMPL impl;
    mfxVideoParam par;
    mfxStatus err;
    int i;

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

    /* create a "slave" session with those same properties, to be used for
     * actual deinterlacing */
    err = MFXInit(impl, &ver, &s->session);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error initializing a session for deinterlacing\n");
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

    s->deint_conf.Header.BufferId = MFX_EXTBUFF_VPP_DEINTERLACING;
    s->deint_conf.Header.BufferSz = sizeof(s->deint_conf);
    s->deint_conf.Mode = s->mode;

    s->ext_buffers[s->num_ext_buffers++] = (mfxExtBuffer *)&s->deint_conf;

    if (opaque) {
        s->surface_ptrs = av_mallocz_array(hw_frames_hwctx->nb_surfaces,
                                           sizeof(*s->surface_ptrs));
        if (!s->surface_ptrs)
            return AVERROR(ENOMEM);
        for (i = 0; i < hw_frames_hwctx->nb_surfaces; i++)
            s->surface_ptrs[i] = hw_frames_hwctx->surfaces + i;
        s->nb_surface_ptrs = hw_frames_hwctx->nb_surfaces;

        s->opaque_alloc.In.Surfaces   = s->surface_ptrs;
        s->opaque_alloc.In.NumSurface = s->nb_surface_ptrs;
        s->opaque_alloc.In.Type       = hw_frames_hwctx->frame_type;

        s->opaque_alloc.Out = s->opaque_alloc.In;

        s->opaque_alloc.Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
        s->opaque_alloc.Header.BufferSz = sizeof(s->opaque_alloc);

        s->ext_buffers[s->num_ext_buffers++] = (mfxExtBuffer *)&s->opaque_alloc;

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

        s->mem_ids = av_mallocz_array(hw_frames_hwctx->nb_surfaces,
                                      sizeof(*s->mem_ids));
        if (!s->mem_ids)
            return AVERROR(ENOMEM);
        for (i = 0; i < hw_frames_hwctx->nb_surfaces; i++)
            s->mem_ids[i] = hw_frames_hwctx->surfaces[i].Data.MemId;
        s->nb_mem_ids = hw_frames_hwctx->nb_surfaces;

        err = MFXVideoCORE_SetFrameAllocator(s->session, &frame_allocator);
        if (err != MFX_ERR_NONE)
            return AVERROR_UNKNOWN;

        par.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;
    }

    par.ExtParam    = s->ext_buffers;
    par.NumExtParam = s->num_ext_buffers;

    par.AsyncDepth = 1;    // TODO async

    par.vpp.In = hw_frames_hwctx->surfaces[0].Info;

    par.vpp.In.CropW = ctx->inputs[0]->w;
    par.vpp.In.CropH = ctx->inputs[0]->h;

    if (ctx->inputs[0]->frame_rate.num) {
        par.vpp.In.FrameRateExtN = ctx->inputs[0]->frame_rate.num;
        par.vpp.In.FrameRateExtD = ctx->inputs[0]->frame_rate.den;
    } else {
        par.vpp.In.FrameRateExtN = ctx->inputs[0]->time_base.num;
        par.vpp.In.FrameRateExtD = ctx->inputs[0]->time_base.den;
    }

    par.vpp.Out = par.vpp.In;

    if (ctx->outputs[0]->frame_rate.num) {
        par.vpp.Out.FrameRateExtN = ctx->outputs[0]->frame_rate.num;
        par.vpp.Out.FrameRateExtD = ctx->outputs[0]->frame_rate.den;
    } else {
        par.vpp.Out.FrameRateExtN = ctx->outputs[0]->time_base.num;
        par.vpp.Out.FrameRateExtD = ctx->outputs[0]->time_base.den;
    }

    err = MFXVideoVPP_Init(s->session, &par);
    if (err != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error opening the VPP for deinterlacing: %d\n", err);
        return AVERROR_UNKNOWN;
    }

    return 0;
}

static int qsvdeint_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    QSVDeintContext  *s = ctx->priv;
    int ret;

    qsvdeint_uninit(ctx);

    s->last_pts = AV_NOPTS_VALUE;
    outlink->frame_rate = av_mul_q(inlink->frame_rate,
                                   (AVRational){ 2, 1 });
    outlink->time_base  = av_mul_q(inlink->time_base,
                                   (AVRational){ 1, 2 });

    /* check that we have a hw context */
    if (!inlink->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }

    s->hw_frames_ctx = av_buffer_ref(inlink->hw_frames_ctx);
    if (!s->hw_frames_ctx)
        return AVERROR(ENOMEM);

    av_buffer_unref(&outlink->hw_frames_ctx);
    outlink->hw_frames_ctx = av_buffer_ref(inlink->hw_frames_ctx);
    if (!outlink->hw_frames_ctx) {
        qsvdeint_uninit(ctx);
        return AVERROR(ENOMEM);
    }

    ret = init_out_session(ctx);
    if (ret < 0)
        return ret;


    return 0;
}

static void clear_unused_frames(QSVDeintContext *s)
{
    QSVFrame *cur = s->work_frames;
    while (cur) {
        if (!cur->surface.Data.Locked) {
            av_frame_free(&cur->frame);
            cur->used = 0;
        }
        cur = cur->next;
    }
}

static int get_free_frame(QSVDeintContext *s, QSVFrame **f)
{
    QSVFrame *frame, **last;

    clear_unused_frames(s);

    frame = s->work_frames;
    last  = &s->work_frames;
    while (frame) {
        if (!frame->used) {
            *f = frame;
            return 0;
        }

        last  = &frame->next;
        frame = frame->next;
    }

    frame = av_mallocz(sizeof(*frame));
    if (!frame)
        return AVERROR(ENOMEM);
    *last = frame;
    *f    = frame;

    return 0;
}

static int submit_frame(AVFilterContext *ctx, AVFrame *frame,
                        mfxFrameSurface1 **surface)
{
    QSVDeintContext *s = ctx->priv;
    QSVFrame *qf;
    int ret;

    ret = get_free_frame(s, &qf);
    if (ret < 0)
        return ret;

    qf->frame = frame;

    qf->surface = *(mfxFrameSurface1*)qf->frame->data[3];

    qf->surface.Data.Locked = 0;
    qf->surface.Info.CropW  = qf->frame->width;
    qf->surface.Info.CropH  = qf->frame->height;

    qf->surface.Info.PicStruct = !qf->frame->interlaced_frame ? MFX_PICSTRUCT_PROGRESSIVE :
                                 (qf->frame->top_field_first ? MFX_PICSTRUCT_FIELD_TFF :
                                                           MFX_PICSTRUCT_FIELD_BFF);
    if (qf->frame->repeat_pict == 1)
        qf->surface.Info.PicStruct |= MFX_PICSTRUCT_FIELD_REPEATED;
    else if (qf->frame->repeat_pict == 2)
        qf->surface.Info.PicStruct |= MFX_PICSTRUCT_FRAME_DOUBLING;
    else if (qf->frame->repeat_pict == 4)
        qf->surface.Info.PicStruct |= MFX_PICSTRUCT_FRAME_TRIPLING;

    if (ctx->inputs[0]->frame_rate.num) {
        qf->surface.Info.FrameRateExtN = ctx->inputs[0]->frame_rate.num;
        qf->surface.Info.FrameRateExtD = ctx->inputs[0]->frame_rate.den;
    } else {
        qf->surface.Info.FrameRateExtN = ctx->inputs[0]->time_base.num;
        qf->surface.Info.FrameRateExtD = ctx->inputs[0]->time_base.den;
    }

    qf->surface.Data.TimeStamp = av_rescale_q(qf->frame->pts,
                                              ctx->inputs[0]->time_base,
                                              (AVRational){1, 90000});

    *surface = &qf->surface;
    qf->used = 1;

    return 0;
}

static int process_frame(AVFilterContext *ctx, const AVFrame *in,
                         mfxFrameSurface1 *surf_in)
{
    QSVDeintContext    *s = ctx->priv;
    AVFilterLink  *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];

    AVFrame *out;
    mfxFrameSurface1 *surf_out;
    mfxSyncPoint sync = NULL;
    mfxStatus err;
    int ret, again = 0;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    surf_out = (mfxFrameSurface1*)out->data[3];
    surf_out->Info.CropW     = outlink->w;
    surf_out->Info.CropH     = outlink->h;
    surf_out->Info.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;

    do {
        err = MFXVideoVPP_RunFrameVPPAsync(s->session, surf_in, surf_out,
                                           NULL, &sync);
        if (err == MFX_WRN_DEVICE_BUSY)
            av_usleep(1);
    } while (err == MFX_WRN_DEVICE_BUSY);

    if (err == MFX_ERR_MORE_DATA) {
        av_frame_free(&out);
        return QSVDEINT_MORE_INPUT;
    }

    if ((err < 0 && err != MFX_ERR_MORE_SURFACE) || !sync) {
        av_log(ctx, AV_LOG_ERROR, "Error during deinterlacing: %d\n", err);
        ret = AVERROR_UNKNOWN;
        goto fail;
    }
    if (err == MFX_ERR_MORE_SURFACE)
        again = 1;

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

    out->width            = outlink->w;
    out->height           = outlink->h;
    out->interlaced_frame = 0;

    out->pts = av_rescale_q(out->pts, inlink->time_base, outlink->time_base);
    if (out->pts == s->last_pts)
        out->pts++;
    s->last_pts = out->pts;

    ret = ff_filter_frame(outlink, out);
    if (ret < 0)
        return ret;

    return again ? QSVDEINT_MORE_OUTPUT : 0;
fail:
    av_frame_free(&out);
    return ret;
}

static int qsvdeint_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext *ctx = link->dst;

    mfxFrameSurface1 *surf_in;
    int ret;

    ret = submit_frame(ctx, in, &surf_in);
    if (ret < 0) {
        av_frame_free(&in);
        return ret;
    }

    do {
        ret = process_frame(ctx, in, surf_in);
        if (ret < 0)
            return ret;
    } while (ret == QSVDEINT_MORE_OUTPUT);

    return 0;
}

static int qsvdeint_request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;

    return ff_request_frame(ctx->inputs[0]);
}

#define OFFSET(x) offsetof(QSVDeintContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption options[] = {
    { "mode", "set deinterlace mode", OFFSET(mode),   AV_OPT_TYPE_INT, {.i64 = MFX_DEINTERLACING_ADVANCED}, MFX_DEINTERLACING_BOB, MFX_DEINTERLACING_ADVANCED, FLAGS, "mode"},
    { "bob",   "bob algorithm",                  0, AV_OPT_TYPE_CONST,      {.i64 = MFX_DEINTERLACING_BOB}, MFX_DEINTERLACING_BOB, MFX_DEINTERLACING_ADVANCED, FLAGS, "mode"},
    { "advanced", "Motion adaptive algorithm",   0, AV_OPT_TYPE_CONST, {.i64 = MFX_DEINTERLACING_ADVANCED}, MFX_DEINTERLACING_BOB, MFX_DEINTERLACING_ADVANCED, FLAGS, "mode"},
    { NULL },
};

static const AVClass qsvdeint_class = {
    .class_name = "deinterlace_qsv",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad qsvdeint_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = qsvdeint_filter_frame,
    },
    { NULL }
};

static const AVFilterPad qsvdeint_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = qsvdeint_config_props,
        .request_frame = qsvdeint_request_frame,
    },
    { NULL }
};

AVFilter ff_vf_deinterlace_qsv = {
    .name      = "deinterlace_qsv",
    .description = NULL_IF_CONFIG_SMALL("QuickSync video deinterlacing"),

    .uninit        = qsvdeint_uninit,
    .query_formats = qsvdeint_query_formats,

    .priv_size = sizeof(QSVDeintContext),
    .priv_class = &qsvdeint_class,

    .inputs    = qsvdeint_inputs,
    .outputs   = qsvdeint_outputs,

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
