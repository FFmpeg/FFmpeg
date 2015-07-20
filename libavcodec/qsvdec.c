/*
 * Intel MediaSDK QSV codec-independent code
 *
 * copyright (c) 2013 Luca Barbato
 * copyright (c) 2015 Anton Khirnov <anton@khirnov.net>
 *
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

#include <string.h>
#include <sys/types.h>

#include <mfx/mfxvideo.h>

#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/log.h"
#include "libavutil/pixfmt.h"
#include "libavutil/time.h"

#include "avcodec.h"
#include "internal.h"
#include "qsv.h"
#include "qsv_internal.h"
#include "qsvdec.h"

int ff_qsv_map_pixfmt(enum AVPixelFormat format)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return AV_PIX_FMT_NV12;
    default:
        return AVERROR(ENOSYS);
    }
}

int ff_qsv_decode_init(AVCodecContext *avctx, QSVContext *q, AVPacket *avpkt)
{
    mfxVideoParam param = { { 0 } };
    mfxBitstream bs   = { { { 0 } } };
    int ret;

    q->iopattern  = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
    if (!q->session) {
        if (avctx->hwaccel_context) {
            AVQSVContext *qsv = avctx->hwaccel_context;

            q->session        = qsv->session;
            q->iopattern      = qsv->iopattern;
            q->ext_buffers    = qsv->ext_buffers;
            q->nb_ext_buffers = qsv->nb_ext_buffers;
        }
        if (!q->session) {
            ret = ff_qsv_init_internal_session(avctx, &q->internal_qs, NULL);
            if (ret < 0)
                return ret;

            q->session = q->internal_qs.session;
        }
    }

    if (avpkt->size) {
        bs.Data       = avpkt->data;
        bs.DataLength = avpkt->size;
        bs.MaxLength  = bs.DataLength;
        bs.TimeStamp  = avpkt->pts;
    } else
        return AVERROR_INVALIDDATA;

    ret = ff_qsv_codec_id_to_mfx(avctx->codec_id);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported codec_id %08x\n", avctx->codec_id);
        return ret;
    }

    param.mfx.CodecId = ret;

    ret = MFXVideoDECODE_DecodeHeader(q->session, &bs, &param);
    if (MFX_ERR_MORE_DATA==ret) {
        return AVERROR(EAGAIN);
    } else if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Decode header error %d\n", ret);
        return ff_qsv_error(ret);
    }
    param.IOPattern   = q->iopattern;
    param.AsyncDepth  = q->async_depth;
    param.ExtParam    = q->ext_buffers;
    param.NumExtParam = q->nb_ext_buffers;
    param.mfx.FrameInfo.BitDepthLuma   = 8;
    param.mfx.FrameInfo.BitDepthChroma = 8;

    ret = MFXVideoDECODE_Init(q->session, &param);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing the MFX video decoder\n");
        return ff_qsv_error(ret);
    }

    avctx->pix_fmt      = AV_PIX_FMT_NV12;
    avctx->profile      = param.mfx.CodecProfile;
    avctx->level        = param.mfx.CodecLevel;
    avctx->coded_width  = param.mfx.FrameInfo.Width;
    avctx->coded_height = param.mfx.FrameInfo.Height;
    avctx->width        = param.mfx.FrameInfo.CropW - param.mfx.FrameInfo.CropX;
    avctx->height       = param.mfx.FrameInfo.CropH - param.mfx.FrameInfo.CropY;

    q->async_fifo = av_fifo_alloc((1 + q->async_depth) *
                                  (sizeof(mfxSyncPoint) + sizeof(QSVFrame*)));
    if (!q->async_fifo)
        return AVERROR(ENOMEM);


    return 0;
}

static int alloc_frame(AVCodecContext *avctx, QSVFrame *frame)
{
    int ret;

    ret = ff_get_buffer(avctx, frame->frame, AV_GET_BUFFER_FLAG_REF);
    if (ret < 0)
        return ret;

    if (frame->frame->format == AV_PIX_FMT_QSV) {
        frame->surface = (mfxFrameSurface1*)frame->frame->data[3];
    } else {
        frame->surface_internal.Info.BitDepthLuma   = 8;
        frame->surface_internal.Info.BitDepthChroma = 8;
        frame->surface_internal.Info.FourCC         = MFX_FOURCC_NV12;
        frame->surface_internal.Info.Width          = avctx->coded_width;
        frame->surface_internal.Info.Height         = avctx->coded_height;
        frame->surface_internal.Info.ChromaFormat   = MFX_CHROMAFORMAT_YUV420;

        frame->surface_internal.Data.PitchLow = frame->frame->linesize[0];
        frame->surface_internal.Data.Y        = frame->frame->data[0];
        frame->surface_internal.Data.UV       = frame->frame->data[1];

        frame->surface = &frame->surface_internal;
    }

    return 0;
}

static void qsv_clear_unused_frames(QSVContext *q)
{
    QSVFrame *cur = q->work_frames;
    while (cur) {
        if (cur->surface && !cur->surface->Data.Locked && !cur->queued) {
            cur->surface = NULL;
            av_frame_unref(cur->frame);
        }
        cur = cur->next;
    }
}

static int get_surface(AVCodecContext *avctx, QSVContext *q, mfxFrameSurface1 **surf)
{
    QSVFrame *frame, **last;
    int ret;

    qsv_clear_unused_frames(q);

    frame = q->work_frames;
    last  = &q->work_frames;
    while (frame) {
        if (!frame->surface) {
            ret = alloc_frame(avctx, frame);
            if (ret < 0)
                return ret;
            *surf = frame->surface;
            return 0;
        }

        last  = &frame->next;
        frame = frame->next;
    }

    frame = av_mallocz(sizeof(*frame));
    if (!frame)
        return AVERROR(ENOMEM);
    frame->frame = av_frame_alloc();
    if (!frame->frame) {
        av_freep(&frame);
        return AVERROR(ENOMEM);
    }
    *last = frame;

    ret = alloc_frame(avctx, frame);
    if (ret < 0)
        return ret;

    *surf = frame->surface;

    return 0;
}

static QSVFrame *find_frame(QSVContext *q, mfxFrameSurface1 *surf)
{
    QSVFrame *cur = q->work_frames;
    while (cur) {
        if (surf == cur->surface)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

int ff_qsv_decode(AVCodecContext *avctx, QSVContext *q,
                  AVFrame *frame, int *got_frame,
                  AVPacket *avpkt)
{
    QSVFrame *out_frame;
    mfxFrameSurface1 *insurf;
    mfxFrameSurface1 *outsurf;
    mfxSyncPoint sync;
    mfxBitstream bs = { { { 0 } } };
    int ret;

    if (avpkt->size) {
        bs.Data       = avpkt->data;
        bs.DataLength = avpkt->size;
        bs.MaxLength  = bs.DataLength;
        bs.TimeStamp  = avpkt->pts;
    }

    do {
        ret = get_surface(avctx, q, &insurf);
        if (ret < 0)
            return ret;

        ret = MFXVideoDECODE_DecodeFrameAsync(q->session, avpkt->size ? &bs : NULL,
                                              insurf, &outsurf, &sync);
        if (ret == MFX_WRN_DEVICE_BUSY)
            av_usleep(1);

    } while (ret == MFX_WRN_DEVICE_BUSY || ret == MFX_ERR_MORE_SURFACE);

    if (ret != MFX_ERR_NONE &&
        ret != MFX_ERR_MORE_DATA &&
        ret != MFX_WRN_VIDEO_PARAM_CHANGED &&
        ret != MFX_ERR_MORE_SURFACE) {
        av_log(avctx, AV_LOG_ERROR, "Error during QSV decoding.\n");
        return ff_qsv_error(ret);
    }

    if (sync) {
        QSVFrame *out_frame = find_frame(q, outsurf);

        if (!out_frame) {
            av_log(avctx, AV_LOG_ERROR,
                   "The returned surface does not correspond to any frame\n");
            return AVERROR_BUG;
        }

        out_frame->queued = 1;
        av_fifo_generic_write(q->async_fifo, &out_frame, sizeof(out_frame), NULL);
        av_fifo_generic_write(q->async_fifo, &sync,      sizeof(sync),      NULL);
    }

    if (!av_fifo_space(q->async_fifo) ||
        (!avpkt->size && av_fifo_size(q->async_fifo))) {
        AVFrame *src_frame;

        av_fifo_generic_read(q->async_fifo, &out_frame, sizeof(out_frame), NULL);
        av_fifo_generic_read(q->async_fifo, &sync,      sizeof(sync),      NULL);
        out_frame->queued = 0;

        MFXVideoCORE_SyncOperation(q->session, sync, 60000);

        src_frame = out_frame->frame;

        ret = av_frame_ref(frame, src_frame);
        if (ret < 0)
            return ret;

        outsurf = out_frame->surface;

        frame->pkt_pts = frame->pts = outsurf->Data.TimeStamp;

        frame->repeat_pict =
            outsurf->Info.PicStruct & MFX_PICSTRUCT_FRAME_TRIPLING ? 4 :
            outsurf->Info.PicStruct & MFX_PICSTRUCT_FRAME_DOUBLING ? 2 :
            outsurf->Info.PicStruct & MFX_PICSTRUCT_FIELD_REPEATED ? 1 : 0;
        frame->top_field_first =
            outsurf->Info.PicStruct & MFX_PICSTRUCT_FIELD_TFF;
        frame->interlaced_frame =
            !(outsurf->Info.PicStruct & MFX_PICSTRUCT_PROGRESSIVE);

        *got_frame = 1;
    }

    return bs.DataOffset;
}

int ff_qsv_decode_close(QSVContext *q)
{
    QSVFrame *cur = q->work_frames;

    while (cur) {
        q->work_frames = cur->next;
        av_frame_free(&cur->frame);
        av_freep(&cur);
        cur = q->work_frames;
    }

    av_fifo_free(q->async_fifo);
    q->async_fifo = NULL;

    MFXVideoDECODE_Close(q->session);
    q->session = NULL;

    ff_qsv_close_internal_session(&q->internal_qs);

    return 0;
}
