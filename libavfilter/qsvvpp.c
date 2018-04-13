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
 * Intel Quick Sync Video VPP base function
 */

#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/time.h"
#include "libavutil/pixdesc.h"

#include "internal.h"
#include "qsvvpp.h"
#include "video.h"

#define IS_VIDEO_MEMORY(mode)  (mode & (MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET | \
                                        MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET))
#define IS_OPAQUE_MEMORY(mode) (mode & MFX_MEMTYPE_OPAQUE_FRAME)
#define IS_SYSTEM_MEMORY(mode) (mode & MFX_MEMTYPE_SYSTEM_MEMORY)

typedef struct QSVFrame {
    AVFrame          *frame;
    mfxFrameSurface1 *surface;
    mfxFrameSurface1  surface_internal;  /* for system memory */
    struct QSVFrame  *next;
} QSVFrame;

/* abstract struct for all QSV filters */
struct QSVVPPContext {
    mfxSession          session;
    int (*filter_frame) (AVFilterLink *outlink, AVFrame *frame);/* callback */
    enum AVPixelFormat  out_sw_format;   /* Real output format */
    mfxVideoParam       vpp_param;
    mfxFrameInfo       *frame_infos;     /* frame info for each input */

    /* members related to the input/output surface */
    int                 in_mem_mode;
    int                 out_mem_mode;
    QSVFrame           *in_frame_list;
    QSVFrame           *out_frame_list;
    int                 nb_surface_ptrs_in;
    int                 nb_surface_ptrs_out;
    mfxFrameSurface1  **surface_ptrs_in;
    mfxFrameSurface1  **surface_ptrs_out;

    /* MFXVPP extern parameters */
    mfxExtOpaqueSurfaceAlloc opaque_alloc;
    mfxExtBuffer      **ext_buffers;
    int                 nb_ext_buffers;
};

static const mfxHandleType handle_types[] = {
    MFX_HANDLE_VA_DISPLAY,
    MFX_HANDLE_D3D9_DEVICE_MANAGER,
    MFX_HANDLE_D3D11_DEVICE,
};

static const AVRational default_tb = { 1, 90000 };

/* functions for frameAlloc */
static mfxStatus frame_alloc(mfxHDL pthis, mfxFrameAllocRequest *req,
                             mfxFrameAllocResponse *resp)
{
    QSVVPPContext *s = pthis;
    int i;

    if (!(req->Type & MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET) ||
        !(req->Type & (MFX_MEMTYPE_FROM_VPPIN | MFX_MEMTYPE_FROM_VPPOUT)) ||
        !(req->Type & MFX_MEMTYPE_EXTERNAL_FRAME))
        return MFX_ERR_UNSUPPORTED;

    if (req->Type & MFX_MEMTYPE_FROM_VPPIN) {
        resp->mids = av_mallocz(s->nb_surface_ptrs_in * sizeof(*resp->mids));
        if (!resp->mids)
            return AVERROR(ENOMEM);

        for (i = 0; i < s->nb_surface_ptrs_in; i++)
            resp->mids[i] = s->surface_ptrs_in[i]->Data.MemId;

        resp->NumFrameActual = s->nb_surface_ptrs_in;
    } else {
        resp->mids = av_mallocz(s->nb_surface_ptrs_out * sizeof(*resp->mids));
        if (!resp->mids)
            return AVERROR(ENOMEM);

        for (i = 0; i < s->nb_surface_ptrs_out; i++)
            resp->mids[i] = s->surface_ptrs_out[i]->Data.MemId;

        resp->NumFrameActual = s->nb_surface_ptrs_out;
    }

    return MFX_ERR_NONE;
}

static mfxStatus frame_free(mfxHDL pthis, mfxFrameAllocResponse *resp)
{
    av_freep(&resp->mids);
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

static int pix_fmt_to_mfx_fourcc(int format)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P:
        return MFX_FOURCC_YV12;
    case AV_PIX_FMT_NV12:
        return MFX_FOURCC_NV12;
    case AV_PIX_FMT_YUYV422:
        return MFX_FOURCC_YUY2;
    case AV_PIX_FMT_RGB32:
        return MFX_FOURCC_RGB4;
    }

    return MFX_FOURCC_NV12;
}

static int map_frame_to_surface(AVFrame *frame, mfxFrameSurface1 *surface)
{
    switch (frame->format) {
    case AV_PIX_FMT_NV12:
        surface->Data.Y  = frame->data[0];
        surface->Data.UV = frame->data[1];
        break;
    case AV_PIX_FMT_YUV420P:
        surface->Data.Y = frame->data[0];
        surface->Data.U = frame->data[1];
        surface->Data.V = frame->data[2];
        break;
    case AV_PIX_FMT_YUYV422:
        surface->Data.Y = frame->data[0];
        surface->Data.U = frame->data[0] + 1;
        surface->Data.V = frame->data[0] + 3;
        break;
    case AV_PIX_FMT_RGB32:
        surface->Data.B = frame->data[0];
        surface->Data.G = frame->data[0] + 1;
        surface->Data.R = frame->data[0] + 2;
        surface->Data.A = frame->data[0] + 3;
        break;
    default:
        return MFX_ERR_UNSUPPORTED;
    }
    surface->Data.Pitch = frame->linesize[0];

    return 0;
}

/* fill the surface info */
static int fill_frameinfo_by_link(mfxFrameInfo *frameinfo, AVFilterLink *link)
{
    enum AVPixelFormat        pix_fmt;
    AVHWFramesContext        *frames_ctx;
    AVQSVFramesContext       *frames_hwctx;
    const AVPixFmtDescriptor *desc;

    if (link->format == AV_PIX_FMT_QSV) {
        if (!link->hw_frames_ctx)
            return AVERROR(EINVAL);

        frames_ctx   = (AVHWFramesContext *)link->hw_frames_ctx->data;
        frames_hwctx = frames_ctx->hwctx;
        *frameinfo   = frames_hwctx->surfaces[0].Info;
    } else {
        pix_fmt = link->format;
        desc = av_pix_fmt_desc_get(pix_fmt);
        if (!desc)
            return AVERROR_BUG;

        frameinfo->CropX          = 0;
        frameinfo->CropY          = 0;
        frameinfo->Width          = FFALIGN(link->w, 32);
        frameinfo->Height         = FFALIGN(link->h, 32);
        frameinfo->PicStruct      = MFX_PICSTRUCT_PROGRESSIVE;
        frameinfo->FourCC         = pix_fmt_to_mfx_fourcc(pix_fmt);
        frameinfo->BitDepthLuma   = desc->comp[0].depth;
        frameinfo->BitDepthChroma = desc->comp[0].depth;
        frameinfo->Shift          = desc->comp[0].depth > 8;
        if (desc->log2_chroma_w && desc->log2_chroma_h)
            frameinfo->ChromaFormat = MFX_CHROMAFORMAT_YUV420;
        else if (desc->log2_chroma_w)
            frameinfo->ChromaFormat = MFX_CHROMAFORMAT_YUV422;
        else
            frameinfo->ChromaFormat = MFX_CHROMAFORMAT_YUV444;
    }

    frameinfo->CropW          = link->w;
    frameinfo->CropH          = link->h;
    frameinfo->FrameRateExtN  = link->frame_rate.num;
    frameinfo->FrameRateExtD  = link->frame_rate.den;
    frameinfo->AspectRatioW   = link->sample_aspect_ratio.num ? link->sample_aspect_ratio.num : 1;
    frameinfo->AspectRatioH   = link->sample_aspect_ratio.den ? link->sample_aspect_ratio.den : 1;

    return 0;
}

static void clear_unused_frames(QSVFrame *list)
{
    while (list) {
        if (list->surface && !list->surface->Data.Locked) {
            list->surface = NULL;
            av_frame_free(&list->frame);
        }
        list = list->next;
    }
}

static void clear_frame_list(QSVFrame **list)
{
    while (*list) {
        QSVFrame *frame;

        frame = *list;
        *list = (*list)->next;
        av_frame_free(&frame->frame);
        av_freep(&frame);
    }
}

static QSVFrame *get_free_frame(QSVFrame **list)
{
    QSVFrame *out = *list;

    for (; out; out = out->next) {
        if (!out->surface)
            break;
    }

    if (!out) {
        out = av_mallocz(sizeof(*out));
        if (!out) {
            av_log(NULL, AV_LOG_ERROR, "Can't alloc new output frame.\n");
            return NULL;
        }
        out->next  = *list;
        *list      = out;
    }

    return out;
}

/* get the input surface */
static QSVFrame *submit_frame(QSVVPPContext *s, AVFilterLink *inlink, AVFrame *picref)
{
    QSVFrame        *qsv_frame;
    AVFilterContext *ctx = inlink->dst;

    clear_unused_frames(s->in_frame_list);

    qsv_frame = get_free_frame(&s->in_frame_list);
    if (!qsv_frame)
        return NULL;

    /* Turn AVFrame into mfxFrameSurface1.
     * For video/opaque memory mode, pix_fmt is AV_PIX_FMT_QSV, and
     * mfxFrameSurface1 is stored in AVFrame->data[3];
     * for system memory mode, raw video data is stored in
     * AVFrame, we should map it into mfxFrameSurface1.
     */
    if (!IS_SYSTEM_MEMORY(s->in_mem_mode)) {
        if (picref->format != AV_PIX_FMT_QSV) {
            av_log(ctx, AV_LOG_ERROR, "QSVVPP gets a wrong frame.\n");
            return NULL;
        }
        qsv_frame->frame   = picref;
        qsv_frame->surface = (mfxFrameSurface1 *)qsv_frame->frame->data[3];
    } else {
        /* make a copy if the input is not padded as libmfx requires */
        if (picref->height & 31 || picref->linesize[0] & 31) {
            qsv_frame->frame = ff_get_video_buffer(inlink,
                                                   FFALIGN(inlink->w, 32),
                                                   FFALIGN(inlink->h, 32));
            if (!qsv_frame->frame)
                return NULL;

            qsv_frame->frame->width   = picref->width;
            qsv_frame->frame->height  = picref->height;

            if (av_frame_copy(qsv_frame->frame, picref) < 0) {
                av_frame_free(&qsv_frame->frame);
                return NULL;
            }

            av_frame_copy_props(qsv_frame->frame, picref);
            av_frame_free(&picref);
        } else
            qsv_frame->frame = picref;

        if (map_frame_to_surface(qsv_frame->frame,
                                &qsv_frame->surface_internal) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Unsupported frame.\n");
            return NULL;
        }
        qsv_frame->surface = &qsv_frame->surface_internal;
    }

    qsv_frame->surface->Info           = s->frame_infos[FF_INLINK_IDX(inlink)];
    qsv_frame->surface->Data.TimeStamp = av_rescale_q(qsv_frame->frame->pts,
                                                      inlink->time_base, default_tb);

    qsv_frame->surface->Info.PicStruct =
            !qsv_frame->frame->interlaced_frame ? MFX_PICSTRUCT_PROGRESSIVE :
            (qsv_frame->frame->top_field_first ? MFX_PICSTRUCT_FIELD_TFF :
                                                 MFX_PICSTRUCT_FIELD_BFF);
    if (qsv_frame->frame->repeat_pict == 1)
        qsv_frame->surface->Info.PicStruct |= MFX_PICSTRUCT_FIELD_REPEATED;
    else if (qsv_frame->frame->repeat_pict == 2)
        qsv_frame->surface->Info.PicStruct |= MFX_PICSTRUCT_FRAME_DOUBLING;
    else if (qsv_frame->frame->repeat_pict == 4)
        qsv_frame->surface->Info.PicStruct |= MFX_PICSTRUCT_FRAME_TRIPLING;

    return qsv_frame;
}

/* get the output surface */
static QSVFrame *query_frame(QSVVPPContext *s, AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    QSVFrame        *out_frame;
    int              ret;

    clear_unused_frames(s->out_frame_list);

    out_frame = get_free_frame(&s->out_frame_list);
    if (!out_frame)
        return NULL;

    /* For video memory, get a hw frame;
     * For system memory, get a sw frame and map it into a mfx_surface. */
    if (!IS_SYSTEM_MEMORY(s->out_mem_mode)) {
        out_frame->frame = av_frame_alloc();
        if (!out_frame->frame)
            return NULL;

        ret = av_hwframe_get_buffer(outlink->hw_frames_ctx, out_frame->frame, 0);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Can't allocate a surface.\n");
            return NULL;
        }

        out_frame->surface = (mfxFrameSurface1 *)out_frame->frame->data[3];
    } else {
        /* Get a frame with aligned dimensions.
         * Libmfx need system memory being 128x64 aligned */
        out_frame->frame = ff_get_video_buffer(outlink,
                                               FFALIGN(outlink->w, 128),
                                               FFALIGN(outlink->h, 64));
        if (!out_frame->frame)
            return NULL;

        out_frame->frame->width  = outlink->w;
        out_frame->frame->height = outlink->h;

        ret = map_frame_to_surface(out_frame->frame,
                                  &out_frame->surface_internal);
        if (ret < 0)
            return NULL;

        out_frame->surface = &out_frame->surface_internal;
    }

    out_frame->surface->Info = s->vpp_param.vpp.Out;

    return out_frame;
}

/* create the QSV session */
static int init_vpp_session(AVFilterContext *avctx, QSVVPPContext *s)
{
    AVFilterLink                 *inlink = avctx->inputs[0];
    AVFilterLink                *outlink = avctx->outputs[0];
    AVQSVFramesContext  *in_frames_hwctx = NULL;
    AVQSVFramesContext *out_frames_hwctx = NULL;

    AVBufferRef *device_ref;
    AVHWDeviceContext *device_ctx;
    AVQSVDeviceContext *device_hwctx;
    mfxHDL handle;
    mfxHandleType handle_type;
    mfxVersion ver;
    mfxIMPL impl;
    int ret, i;

    if (inlink->hw_frames_ctx) {
        AVHWFramesContext *frames_ctx = (AVHWFramesContext *)inlink->hw_frames_ctx->data;

        device_ref      = frames_ctx->device_ref;
        in_frames_hwctx = frames_ctx->hwctx;

        s->in_mem_mode = in_frames_hwctx->frame_type;

        s->surface_ptrs_in = av_mallocz_array(in_frames_hwctx->nb_surfaces,
                                              sizeof(*s->surface_ptrs_in));
        if (!s->surface_ptrs_in)
            return AVERROR(ENOMEM);

        for (i = 0; i < in_frames_hwctx->nb_surfaces; i++)
            s->surface_ptrs_in[i] = in_frames_hwctx->surfaces + i;

        s->nb_surface_ptrs_in = in_frames_hwctx->nb_surfaces;
    } else if (avctx->hw_device_ctx) {
        device_ref     = avctx->hw_device_ctx;
        s->in_mem_mode = MFX_MEMTYPE_SYSTEM_MEMORY;
    } else {
        av_log(avctx, AV_LOG_ERROR, "No hw context provided.\n");
        return AVERROR(EINVAL);
    }

    device_ctx   = (AVHWDeviceContext *)device_ref->data;
    device_hwctx = device_ctx->hwctx;

    if (outlink->format == AV_PIX_FMT_QSV) {
        AVHWFramesContext *out_frames_ctx;
        AVBufferRef *out_frames_ref = av_hwframe_ctx_alloc(device_ref);
        if (!out_frames_ref)
            return AVERROR(ENOMEM);

        s->out_mem_mode = IS_OPAQUE_MEMORY(s->in_mem_mode) ?
                          MFX_MEMTYPE_OPAQUE_FRAME :
                          MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;

        out_frames_ctx   = (AVHWFramesContext *)out_frames_ref->data;
        out_frames_hwctx = out_frames_ctx->hwctx;

        out_frames_ctx->format            = AV_PIX_FMT_QSV;
        out_frames_ctx->width             = FFALIGN(outlink->w, 32);
        out_frames_ctx->height            = FFALIGN(outlink->h, 32);
        out_frames_ctx->sw_format         = s->out_sw_format;
        out_frames_ctx->initial_pool_size = 64;
        out_frames_hwctx->frame_type      = s->out_mem_mode;

        ret = av_hwframe_ctx_init(out_frames_ref);
        if (ret < 0) {
            av_buffer_unref(&out_frames_ref);
            av_log(avctx, AV_LOG_ERROR, "Error creating frames_ctx for output pad.\n");
            return ret;
        }

        s->surface_ptrs_out = av_mallocz_array(out_frames_hwctx->nb_surfaces,
                                               sizeof(*s->surface_ptrs_out));
        if (!s->surface_ptrs_out) {
            av_buffer_unref(&out_frames_ref);
            return AVERROR(ENOMEM);
        }

        for (i = 0; i < out_frames_hwctx->nb_surfaces; i++)
            s->surface_ptrs_out[i] = out_frames_hwctx->surfaces + i;
        s->nb_surface_ptrs_out = out_frames_hwctx->nb_surfaces;

        av_buffer_unref(&outlink->hw_frames_ctx);
        outlink->hw_frames_ctx = out_frames_ref;
    } else
        s->out_mem_mode = MFX_MEMTYPE_SYSTEM_MEMORY;

    /* extract the properties of the "master" session given to us */
    ret = MFXQueryIMPL(device_hwctx->session, &impl);
    if (ret == MFX_ERR_NONE)
        ret = MFXQueryVersion(device_hwctx->session, &ver);
    if (ret != MFX_ERR_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Error querying the session attributes\n");
        return AVERROR_UNKNOWN;
    }

    for (i = 0; i < FF_ARRAY_ELEMS(handle_types); i++) {
        ret = MFXVideoCORE_GetHandle(device_hwctx->session, handle_types[i], &handle);
        if (ret == MFX_ERR_NONE) {
            handle_type = handle_types[i];
            break;
        }
    }

    /* create a "slave" session with those same properties, to be used for vpp */
    ret = MFXInit(impl, &ver, &s->session);
    if (ret != MFX_ERR_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing a session for scaling\n");
        return AVERROR_UNKNOWN;
    }

    if (handle) {
        ret = MFXVideoCORE_SetHandle(s->session, handle_type, handle);
        if (ret != MFX_ERR_NONE)
            return AVERROR_UNKNOWN;
    }

    if (QSV_RUNTIME_VERSION_ATLEAST(ver, 1, 25)) {
        ret = MFXJoinSession(device_hwctx->session, s->session);
        if (ret != MFX_ERR_NONE)
            return AVERROR_UNKNOWN;
    }

    if (IS_OPAQUE_MEMORY(s->in_mem_mode) || IS_OPAQUE_MEMORY(s->out_mem_mode)) {
        s->opaque_alloc.In.Surfaces   = s->surface_ptrs_in;
        s->opaque_alloc.In.NumSurface = s->nb_surface_ptrs_in;
        s->opaque_alloc.In.Type       = s->in_mem_mode;

        s->opaque_alloc.Out.Surfaces   = s->surface_ptrs_out;
        s->opaque_alloc.Out.NumSurface = s->nb_surface_ptrs_out;
        s->opaque_alloc.Out.Type       = s->out_mem_mode;

        s->opaque_alloc.Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
        s->opaque_alloc.Header.BufferSz = sizeof(s->opaque_alloc);
    } else if (IS_VIDEO_MEMORY(s->in_mem_mode) || IS_VIDEO_MEMORY(s->out_mem_mode)) {
        mfxFrameAllocator frame_allocator = {
            .pthis  = s,
            .Alloc  = frame_alloc,
            .Lock   = frame_lock,
            .Unlock = frame_unlock,
            .GetHDL = frame_get_hdl,
            .Free   = frame_free,
        };

        ret = MFXVideoCORE_SetFrameAllocator(s->session, &frame_allocator);
        if (ret != MFX_ERR_NONE)
            return AVERROR_UNKNOWN;
    }

    return 0;
}

int ff_qsvvpp_create(AVFilterContext *avctx, QSVVPPContext **vpp, QSVVPPParam *param)
{
    int i;
    int ret;
    QSVVPPContext *s;

    s = av_mallocz(sizeof(*s));
    if (!s)
        return AVERROR(ENOMEM);

    s->filter_frame  = param->filter_frame;
    if (!s->filter_frame)
        s->filter_frame = ff_filter_frame;
    s->out_sw_format = param->out_sw_format;

    /* create the vpp session */
    ret = init_vpp_session(avctx, s);
    if (ret < 0)
        goto failed;

    s->frame_infos = av_mallocz_array(avctx->nb_inputs, sizeof(*s->frame_infos));
    if (!s->frame_infos) {
        ret = AVERROR(ENOMEM);
        goto failed;
    }

    /* Init each input's information */
    for (i = 0; i < avctx->nb_inputs; i++) {
        ret = fill_frameinfo_by_link(&s->frame_infos[i], avctx->inputs[i]);
        if (ret < 0)
            goto failed;
    }

    /* Update input's frame info according to crop */
    for (i = 0; i < param->num_crop; i++) {
        QSVVPPCrop *crop = param->crop + i;
        if (crop->in_idx > avctx->nb_inputs) {
            ret = AVERROR(EINVAL);
            goto failed;
        }
        s->frame_infos[crop->in_idx].CropX = crop->x;
        s->frame_infos[crop->in_idx].CropY = crop->y;
        s->frame_infos[crop->in_idx].CropW = crop->w;
        s->frame_infos[crop->in_idx].CropH = crop->h;
    }

    s->vpp_param.vpp.In = s->frame_infos[0];

    ret = fill_frameinfo_by_link(&s->vpp_param.vpp.Out, avctx->outputs[0]);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Fail to get frame info from link.\n");
        goto failed;
    }

    if (IS_OPAQUE_MEMORY(s->in_mem_mode) || IS_OPAQUE_MEMORY(s->out_mem_mode)) {
        s->nb_ext_buffers = param->num_ext_buf + 1;
        s->ext_buffers = av_mallocz_array(s->nb_ext_buffers, sizeof(*s->ext_buffers));
        if (!s->ext_buffers) {
            ret = AVERROR(ENOMEM);
            goto failed;
        }

        s->ext_buffers[0] = (mfxExtBuffer *)&s->opaque_alloc;
        for (i = 1; i < param->num_ext_buf; i++)
            s->ext_buffers[i]    = param->ext_buf[i - 1];
        s->vpp_param.ExtParam    = s->ext_buffers;
        s->vpp_param.NumExtParam = s->nb_ext_buffers;
    } else {
        s->vpp_param.NumExtParam = param->num_ext_buf;
        s->vpp_param.ExtParam    = param->ext_buf;
    }

    s->vpp_param.AsyncDepth = 1;

    if (IS_SYSTEM_MEMORY(s->in_mem_mode))
        s->vpp_param.IOPattern |= MFX_IOPATTERN_IN_SYSTEM_MEMORY;
    else if (IS_VIDEO_MEMORY(s->in_mem_mode))
        s->vpp_param.IOPattern |= MFX_IOPATTERN_IN_VIDEO_MEMORY;
    else if (IS_OPAQUE_MEMORY(s->in_mem_mode))
        s->vpp_param.IOPattern |= MFX_IOPATTERN_IN_OPAQUE_MEMORY;

    if (IS_SYSTEM_MEMORY(s->out_mem_mode))
        s->vpp_param.IOPattern |= MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
    else if (IS_VIDEO_MEMORY(s->out_mem_mode))
        s->vpp_param.IOPattern |= MFX_IOPATTERN_OUT_VIDEO_MEMORY;
    else if (IS_OPAQUE_MEMORY(s->out_mem_mode))
        s->vpp_param.IOPattern |= MFX_IOPATTERN_OUT_OPAQUE_MEMORY;

    ret = MFXVideoVPP_Init(s->session, &s->vpp_param);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create a qsvvpp, ret = %d.\n", ret);
        goto failed;
    }

    *vpp = s;
    return 0;

failed:
    ff_qsvvpp_free(&s);

    return ret;
}

int ff_qsvvpp_free(QSVVPPContext **vpp)
{
    QSVVPPContext *s = *vpp;

    if (!s)
        return 0;

    if (s->session) {
        MFXVideoVPP_Close(s->session);
        MFXClose(s->session);
    }

    /* release all the resources */
    clear_frame_list(&s->in_frame_list);
    clear_frame_list(&s->out_frame_list);
    av_freep(&s->surface_ptrs_in);
    av_freep(&s->surface_ptrs_out);
    av_freep(&s->ext_buffers);
    av_freep(&s->frame_infos);
    av_freep(vpp);

    return 0;
}

int ff_qsvvpp_filter_frame(QSVVPPContext *s, AVFilterLink *inlink, AVFrame *picref)
{
    AVFilterContext  *ctx     = inlink->dst;
    AVFilterLink     *outlink = ctx->outputs[0];
    mfxSyncPoint      sync;
    QSVFrame         *in_frame, *out_frame;
    int               ret, filter_ret;

    in_frame = submit_frame(s, inlink, picref);
    if (!in_frame) {
        av_log(ctx, AV_LOG_ERROR, "Failed to submit frame on input[%d]\n",
               FF_INLINK_IDX(inlink));
        return AVERROR(ENOMEM);
    }

    do {
        out_frame = query_frame(s, outlink);
        if (!out_frame) {
            av_log(ctx, AV_LOG_ERROR, "Failed to query an output frame.\n");
            return AVERROR(ENOMEM);
        }

        do {
            ret = MFXVideoVPP_RunFrameVPPAsync(s->session, in_frame->surface,
                                               out_frame->surface, NULL, &sync);
            if (ret == MFX_WRN_DEVICE_BUSY)
                av_usleep(500);
        } while (ret == MFX_WRN_DEVICE_BUSY);

        if (ret < 0 && ret != MFX_ERR_MORE_SURFACE) {
            /* Ignore more_data error */
            if (ret == MFX_ERR_MORE_DATA)
                ret = AVERROR(EAGAIN);
            break;
        }

        if (MFXVideoCORE_SyncOperation(s->session, sync, 1000) < 0)
            av_log(ctx, AV_LOG_WARNING, "Sync failed.\n");

        out_frame->frame->pts = av_rescale_q(out_frame->surface->Data.TimeStamp,
                                             default_tb, outlink->time_base);

        filter_ret = s->filter_frame(outlink, out_frame->frame);
        if (filter_ret < 0) {
            av_frame_free(&out_frame->frame);
            ret = filter_ret;
            break;
        }
        out_frame->frame = NULL;
    } while(ret == MFX_ERR_MORE_SURFACE);

    return ret;
}
