/*
 * Intel MediaSDK QSV encoder utility functions
 *
 * copyright (c) 2013 Yukinori Yamazoe
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <string.h>
#include <sys/types.h>
#include <mfx/mfxvideo.h>

#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/log.h"
#include "libavutil/time.h"
#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "internal.h"
#include "qsv.h"
#include "qsvenc.h"

static int realloc_surface_pool(QSVEncContext *q, int old_nmemb, int new_nmemb)
{
    QSVEncSurfaceList **pp = av_realloc_array(q->surf, new_nmemb, sizeof(QSVEncSurfaceList *));
    if (!pp) {
        av_log(q, AV_LOG_ERROR, "av_realloc_array() failed\n");
        return AVERROR(ENOMEM);
    }

    q->surf = pp;
    q->nb_surf = new_nmemb;

    for (int i = old_nmemb; i < q->nb_surf; i++) {
        if (!(q->surf[i] = av_mallocz(sizeof(QSVEncSurfaceList)))) {
            q->nb_surf = i;
            av_log(q, AV_LOG_ERROR, "av_mallocz() failed in realloc_surface_pool()\n");
            return AVERROR(ENOMEM);
        }
    }

    return 0;
}

static void free_surface_pool(QSVEncContext *q)
{
    for (int i = 0; i < q->nb_surf; i++) {
        if (q->surf[i]->surface.Data.MemId)
            av_frame_free((AVFrame **)(&q->surf[i]->surface.Data.MemId));
        av_freep(&q->surf[i]);
    }
    av_freep(&q->surf);
}

static int realloc_buffer_pool(QSVEncContext *q, int old_nmemb, int new_nmemb)
{
    int size = q->param.mfx.BufferSizeInKB * 1000;
    QSVEncBuffer **pp = av_realloc_array(q->buf, new_nmemb, sizeof(QSVEncBuffer *));
    if (!pp) {
        av_log(q, AV_LOG_ERROR, "av_realloc_array() failed in realloc_buffer_pool()\n");
        return AVERROR(ENOMEM);
    }

    q->buf = pp;
    q->nb_buf = new_nmemb;

    for (int i = old_nmemb; i < q->nb_buf; i++) {
        if (!(q->buf[i] = av_mallocz(sizeof(QSVEncBuffer)))) {
            q->nb_buf = i;
            av_log(q, AV_LOG_ERROR, "av_mallocz() failed in realloc_buffer_pool()\n");
            return AVERROR(ENOMEM);
        }
        if (!(q->buf[i]->data = av_mallocz(size))) {
            q->nb_buf = i;
            av_freep(&q->buf[i]);
            av_log(q, AV_LOG_ERROR, "av_mallocz() failed in realloc_buffer_pool()\n");
            return AVERROR(ENOMEM);
        }
        q->buf[i]->bs.Data      = q->buf[i]->data;
        q->buf[i]->bs.MaxLength = size;
    }

    return 0;
}

static void free_buffer_pool(QSVEncContext *q)
{
    for (int i = 0; i < q->nb_buf; i++) {
        av_freep(&q->buf[i]->data);
        av_freep(&q->buf[i]);
    }
    av_freep(&q->buf);
}

static int init_video_param(AVCodecContext *avctx, QSVEncContext *q)
{
    float quant;
    int ret;

    if ((ret = ff_qsv_codec_id_to_mfx(avctx->codec_id)) < 0)
        return ret;
    q->param.mfx.CodecId            = ret;
    q->param.mfx.CodecProfile       = q->profile;
    q->param.mfx.CodecLevel         = q->level;
    q->param.mfx.TargetUsage        = q->preset;
    q->param.mfx.GopPicSize         = avctx->gop_size < 0 ? 0 : avctx->gop_size;
    q->param.mfx.GopRefDist         = av_clip(avctx->max_b_frames, -1, 16) + 1;
    q->param.mfx.GopOptFlag         = avctx->flags & CODEC_FLAG_CLOSED_GOP ?
                                      MFX_GOP_CLOSED :
                                      0;
    q->param.mfx.IdrInterval        = q->idr_interval;
    q->param.mfx.NumSlice           = avctx->slices;
    q->param.mfx.NumRefFrame        = avctx->refs < 0 ? 0 : avctx->refs;
    q->param.mfx.EncodedOrder       = 0;
    q->param.mfx.BufferSizeInKB     = 0;
    q->param.mfx.RateControlMethod =
        (q->qpi >= 0 && q->qpp >= 0 && q->qpb >= 0) ||
        avctx->flags & CODEC_FLAG_QSCALE      ? MFX_RATECONTROL_CQP :
        avctx->rc_max_rate &&
        avctx->rc_max_rate == avctx->bit_rate ? MFX_RATECONTROL_CBR :
                                                MFX_RATECONTROL_VBR;

    switch (q->param.mfx.RateControlMethod) {
    case MFX_RATECONTROL_CBR: // API 1.0
        av_log(avctx, AV_LOG_VERBOSE, "RateControlMethod:CBR\n");
        q->param.mfx.TargetKbps = avctx->bit_rate / 1000;
        q->param.mfx.MaxKbps    = avctx->bit_rate / 1000;
        av_log(avctx, AV_LOG_VERBOSE, "TargetKbps:%d\n", q->param.mfx.TargetKbps);
        break;
    case MFX_RATECONTROL_VBR: // API 1.0
        av_log(avctx, AV_LOG_VERBOSE, "RateControlMethod:VBR\n");
        q->param.mfx.TargetKbps = avctx->bit_rate / 1000; // >1072
        q->param.mfx.MaxKbps    = avctx->rc_max_rate / 1000;
        av_log(avctx, AV_LOG_VERBOSE, "TargetKbps:%d\n", q->param.mfx.TargetKbps);
        if (q->param.mfx.MaxKbps)
            av_log(avctx, AV_LOG_VERBOSE, "MaxKbps:%d\n", q->param.mfx.MaxKbps);
        break;
    case MFX_RATECONTROL_CQP: // API 1.1
        av_log(avctx, AV_LOG_VERBOSE, "RateControlMethod:CQP\n");
        if (q->qpi >= 0) {
            q->param.mfx.QPI = q->qpi;
        } else {
            quant = avctx->global_quality / FF_QP2LAMBDA;
            if (avctx->i_quant_factor)
                quant *= fabs(avctx->i_quant_factor);
            quant += avctx->i_quant_offset;
            q->param.mfx.QPI = av_clip(quant, 0, 51);
        }

        if (q->qpp >= 0) {
            q->param.mfx.QPP = q->qpp;
        } else {
            quant = avctx->global_quality / FF_QP2LAMBDA;
            q->param.mfx.QPP = av_clip(quant, 0, 51);
        }

        if (q->qpb >= 0) {
            q->param.mfx.QPB = q->qpb;
        } else {
            quant = avctx->global_quality / FF_QP2LAMBDA;
            if (avctx->b_quant_factor)
                quant *= fabs(avctx->b_quant_factor);
            quant += avctx->b_quant_offset;
            q->param.mfx.QPB = av_clip(quant, 0, 51);
        }

        av_log(avctx, AV_LOG_VERBOSE, "QPI:%d, QPP:%d, QPB:%d\n",
               q->param.mfx.QPI, q->param.mfx.QPP, q->param.mfx.QPB);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "RateControlMethod:%d is undefined.\n",
               q->param.mfx.RateControlMethod);
        return AVERROR(EINVAL);
    }

    q->param.mfx.FrameInfo.FourCC        = MFX_FOURCC_NV12;
    q->param.mfx.FrameInfo.Width         = FFALIGN(avctx->width, 16);
    q->param.mfx.FrameInfo.Height        = FFALIGN(avctx->height, 32);
    q->param.mfx.FrameInfo.CropX         = 0;
    q->param.mfx.FrameInfo.CropY         = 0;
    q->param.mfx.FrameInfo.CropW         = avctx->width;
    q->param.mfx.FrameInfo.CropH         = avctx->height;
    q->param.mfx.FrameInfo.FrameRateExtN = avctx->time_base.den;
    q->param.mfx.FrameInfo.FrameRateExtD = avctx->time_base.num;
    q->param.mfx.FrameInfo.AspectRatioW  = avctx->sample_aspect_ratio.num;
    q->param.mfx.FrameInfo.AspectRatioH  = avctx->sample_aspect_ratio.den;
    q->param.mfx.FrameInfo.PicStruct     = MFX_PICSTRUCT_UNKNOWN;
    q->param.mfx.FrameInfo.ChromaFormat  = MFX_CHROMAFORMAT_YUV420;

    av_log(avctx, AV_LOG_VERBOSE, "FrameRate:%d/%d\n",
           q->param.mfx.FrameInfo.FrameRateExtN,
           q->param.mfx.FrameInfo.FrameRateExtD);

    q->extco.Header.BufferId      = MFX_EXTBUFF_CODING_OPTION;
    q->extco.Header.BufferSz      = sizeof(q->extco);
    q->extco.RateDistortionOpt    = MFX_CODINGOPTION_UNKNOWN;
    q->extco.EndOfSequence        = MFX_CODINGOPTION_UNKNOWN;
    q->extco.CAVLC                = avctx->coder_type == FF_CODER_TYPE_VLC ?
                                    MFX_CODINGOPTION_ON :
                                    MFX_CODINGOPTION_UNKNOWN;
    q->extco.ResetRefList         = MFX_CODINGOPTION_UNKNOWN;
    q->extco.MaxDecFrameBuffering = MFX_CODINGOPTION_UNKNOWN;
    q->extco.AUDelimiter          = MFX_CODINGOPTION_UNKNOWN; // or OFF
    q->extco.EndOfStream          = MFX_CODINGOPTION_UNKNOWN;
    q->extco.PicTimingSEI         = MFX_CODINGOPTION_UNKNOWN; // or OFF
    q->extco.VuiNalHrdParameters  = MFX_CODINGOPTION_UNKNOWN;
    q->extco.FramePicture         = MFX_CODINGOPTION_ON;

    if (q->extco.CAVLC == MFX_CODINGOPTION_ON)
        av_log(avctx, AV_LOG_VERBOSE, "CAVLC:ON\n");

    q->extparam[q->param.NumExtParam] = (mfxExtBuffer *)&q->extco;
    q->param.ExtParam = q->extparam;
    q->param.NumExtParam++;

    return 0;
}

static int get_video_param(AVCodecContext *avctx, QSVEncContext *q)
{
    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER) {
        int size = sizeof(q->spspps);
        uint8_t *tmp = av_malloc(size);
        if (!tmp) {
            av_log(avctx, AV_LOG_ERROR, "av_malloc() failed\n");
            return AVERROR(ENOMEM);
        }

        q->extcospspps.Header.BufferId = MFX_EXTBUFF_CODING_OPTION_SPSPPS;
        q->extcospspps.Header.BufferSz = sizeof(q->extcospspps);
        q->extcospspps.SPSBuffer       = q->spspps[0];
        q->extcospspps.SPSBufSize      = sizeof(q->spspps[0]);
        q->extcospspps.PPSBuffer       = q->spspps[1];
        q->extcospspps.PPSBufSize      = sizeof(q->spspps[1]);

        q->extparam[q->param.NumExtParam] = (mfxExtBuffer *)&q->extcospspps;
        q->param.ExtParam = q->extparam;
        q->param.NumExtParam++;

        MFXVideoENCODE_GetVideoParam(q->session, &q->param);

        q->param.NumExtParam--;

        memcpy(tmp, q->spspps, size);
        avctx->extradata = tmp;
        avctx->extradata_size = size;
    } else {
        MFXVideoENCODE_GetVideoParam(q->session, &q->param);
    }

    return 0;
}

int ff_qsv_enc_init(AVCodecContext *avctx, QSVEncContext *q)
{
    mfxIMPL impl   = MFX_IMPL_AUTO_ANY;
    mfxVersion ver = { { QSV_VERSION_MINOR, QSV_VERSION_MAJOR } };
    int ret;

    if ((ret = MFXInit(impl, &ver, &q->session)) < 0) {
        return ff_qsv_error(ret);
    }

    MFXQueryIMPL(q->session, &impl);

    switch (MFX_IMPL_BASETYPE(impl)) {
    case MFX_IMPL_SOFTWARE:
        av_log(avctx, AV_LOG_VERBOSE,
               "Using Intel QuickSync encoder software implementation.\n");
        break;
    case MFX_IMPL_HARDWARE:
    case MFX_IMPL_HARDWARE2:
    case MFX_IMPL_HARDWARE3:
    case MFX_IMPL_HARDWARE4:
        av_log(avctx, AV_LOG_VERBOSE,
               "Using Intel QuickSync encoder hardware accelerated implementation.\n");
        break;
    default:
        av_log(avctx, AV_LOG_VERBOSE,
               "Unknown Intel QuickSync encoder implementation %d.\n", impl);
    }

    q->param.IOPattern  = MFX_IOPATTERN_IN_SYSTEM_MEMORY;
    q->param.AsyncDepth = q->async_depth;

    if ((ret = init_video_param(avctx, q)) < 0)
        return ret;

    ret = MFXVideoENCODE_QueryIOSurf(q->session, &q->param, &q->req);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "MFXVideoENCODE_QueryIOSurf():%d\n", ret);
        return ff_qsv_error(ret);
    }

    if (ret = MFXVideoENCODE_Init(q->session, &q->param)) {
        av_log(avctx, AV_LOG_ERROR, "MFXVideoENCODE_Init():%d\n", ret);
        return ff_qsv_error(ret);
    }

    if ((ret = get_video_param(avctx, q)) < 0)
        return ret;

    if (ret = realloc_surface_pool(q, 0, q->req.NumFrameSuggested))
        return ret;

    if (ret = realloc_buffer_pool(q, 0, q->req.NumFrameSuggested))
        return ret;

    q->first_pts = AV_NOPTS_VALUE;
    q->pts_delay = AV_NOPTS_VALUE;

    return ret;
}

static QSVEncSurfaceList *get_surface_pool(QSVEncContext *q)
{
    int i;

    for (i = 0; i < q->nb_surf; i++)
        if (!q->surf[i]->surface.Data.Locked && !q->surf[i]->pending)
            break;

    if (i == q->nb_surf)
        if (realloc_surface_pool(q, q->nb_surf, q->nb_surf * 2))
            return NULL;

    if (q->surf[i]->surface.Data.MemId)
        av_frame_free((AVFrame **)(&q->surf[i]->surface.Data.MemId));

    return q->surf[i];
}

static AVFrame *clone_aligned_frame(AVCodecContext *avctx,
                                    const AVFrame *frame)
{
    int width    = frame->linesize[0];
    int height   = FFALIGN(frame->height, 32);
    int size     = width * height;
    AVFrame *ret = NULL;

    // check AVFrame buffer alignment for QSV
    if (!(width % 16) && frame->buf[0] && (frame->buf[0]->size >= size)) {
        if (!(ret = av_frame_clone(frame))) {
            av_log(avctx, AV_LOG_ERROR, "av_frame_clone() failed\n");
            goto fail;
        }
    } else {
        if (!(ret = av_frame_alloc())) {
            av_log(avctx, AV_LOG_ERROR, "av_frame_alloc() failed\n");
            goto fail;
        }
        if (ff_get_buffer(avctx, ret, 0) < 0) {
            av_log(avctx, AV_LOG_ERROR, "ff_get_buffer() failed\n");
            goto fail;
        }
        if (av_frame_copy_props(ret, frame) < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_frame_copy_props() failed\n");
            goto fail;
        }
        av_image_copy(ret->data, ret->linesize, frame->data, frame->linesize,
                      frame->format, frame->width, frame->height);
    }

    return ret;

fail:
    if (ret)
        av_frame_free(&ret);

    return NULL;
}

static void set_surface_param(QSVEncContext *q, mfxFrameSurface1 *surf,
                              AVFrame *frame)
{
    surf->Info = q->param.mfx.FrameInfo;

    surf->Info.PicStruct =
        !frame->interlaced_frame ? MFX_PICSTRUCT_PROGRESSIVE :
        frame->top_field_first   ? MFX_PICSTRUCT_FIELD_TFF :
                                   MFX_PICSTRUCT_FIELD_BFF;
    if (frame->repeat_pict == 1)
        surf->Info.PicStruct |= MFX_PICSTRUCT_FIELD_REPEATED;
    else if (frame->repeat_pict == 2)
        surf->Info.PicStruct |= MFX_PICSTRUCT_FRAME_DOUBLING;
    else if (frame->repeat_pict == 4)
        surf->Info.PicStruct |= MFX_PICSTRUCT_FRAME_TRIPLING;

    surf->Data.MemId     = frame;
    surf->Data.Y         = frame->data[0];
    surf->Data.UV        = frame->data[1];
    surf->Data.Pitch     = frame->linesize[0];
    surf->Data.TimeStamp = frame->pts;
}

static int add_surface_list(AVCodecContext *avctx, QSVEncContext *q,
                            const AVFrame *frame)
{
    QSVEncSurfaceList *list;
    AVFrame *clone;

    if (!(list = get_surface_pool(q)))
        return AVERROR(ENOMEM);

    if (!(clone = clone_aligned_frame(avctx, frame)))
        return AVERROR(ENOMEM);

    set_surface_param(q, &list->surface, clone);

    list->pending = 1;
    list->next = NULL;

    if (q->pending_enc_end)
        q->pending_enc_end->next = list;
    else
        q->pending_enc = list;

    q->pending_enc_end = list;

    return 0;
}

static void remove_surface_list(QSVEncContext *q)
{
    if (q->pending_enc) {
        QSVEncSurfaceList *list = q->pending_enc;
        q->pending_enc = q->pending_enc->next;
        list->pending = 0;
        list->next = NULL;
        if (!q->pending_enc)
            q->pending_enc_end = NULL;
    }
}

static QSVEncBuffer *get_buffer(QSVEncContext *q)
{
    int i;

    for (i = 0; i < q->nb_buf; i++)
        if (!q->buf[i]->sync)
            break;

    if (i == q->nb_buf)
        if (realloc_buffer_pool(q, q->nb_buf, q->nb_buf * 2))
            return NULL;

    q->buf[i]->bs.DataOffset = 0;
    q->buf[i]->bs.DataLength = 0;
    q->buf[i]->prev          = NULL;
    q->buf[i]->next          = NULL;

    return q->buf[i];
}

static void release_buffer(QSVEncBuffer *buf)
{
    buf->sync = 0;
}

static void enqueue_buffer(QSVEncBuffer **head, QSVEncBuffer **tail, int *nb,
                           QSVEncBuffer *list)
{
    list->prev = *tail;
    list->next = NULL;

    if (*tail)
        (*tail)->next = list;
    else
        *head = list;

    *tail = list;

    if (nb)
        (*nb)++;
}

static QSVEncBuffer *dequeue_buffer(QSVEncBuffer **head, QSVEncBuffer **tail,
                                    int *nb)
{
    QSVEncBuffer *list = *head;

    *head = (*head)->next;

    if (*head)
        (*head)->prev = NULL;
    else
        *tail = NULL;

    if (nb)
        (*nb)--;

    list->prev = list->next = NULL;

    return list;
}

static void fill_buffer_dts(QSVEncContext *q, QSVEncBuffer *list,
                            int64_t base_dts)
{
    QSVEncBuffer *prev = list;
    int64_t dts        = base_dts - q->pts_delay;

    while (prev && prev->dts == AV_NOPTS_VALUE) {
        prev->dts = dts;
        prev = prev->prev;
        dts -= q->pts_delay;
    }
}

static void print_interlace_msg(AVCodecContext *avctx, QSVEncContext *q)
{
    if (q->param.mfx.CodecId == MFX_CODEC_AVC) {
        if (q->param.mfx.CodecProfile == MFX_PROFILE_AVC_BASELINE ||
            q->param.mfx.CodecLevel < MFX_LEVEL_AVC_21 ||
            q->param.mfx.CodecLevel > MFX_LEVEL_AVC_41)
            av_log(avctx, AV_LOG_WARNING,
                   "Interlaced coding is supported"
                   " at Main/High Profile Level 2.1-4.1\n");
    }
}

int ff_qsv_enc_frame(AVCodecContext *avctx, QSVEncContext *q,
                     AVPacket *pkt, const AVFrame *frame, int *got_packet)
{
    mfxFrameSurface1 *insurf = NULL;
    QSVEncBuffer *outbuf     = NULL;
    int busymsec             = 0;
    int ret;

    *got_packet = 0;

    if (frame) {
        if (q->first_pts == AV_NOPTS_VALUE)
            q->first_pts = frame->pts;
        else if (q->pts_delay == AV_NOPTS_VALUE)
            q->pts_delay = frame->pts - q->first_pts;

        if ((ret = add_surface_list(avctx, q, frame)) < 0)
            return ret;

        ret = MFX_ERR_MORE_DATA;
    } else {
        ret = MFX_ERR_NONE;
    }

    do {
        if (q->pending_enc)
            insurf = &q->pending_enc->surface;
        else if (ret != MFX_ERR_NONE)
            break;

        outbuf = get_buffer(q);
        if (!outbuf)
            return AVERROR(ENOMEM);

        ret = MFXVideoENCODE_EncodeFrameAsync(q->session, NULL, insurf,
                                              &outbuf->bs, &outbuf->sync);

        if (ret == MFX_WRN_DEVICE_BUSY) {
            if (frame) {
                // try to encode this surface next time
                av_log(avctx, AV_LOG_VERBOSE, "MFXVideoENCODE_EncodeFrameAsync():MFX_WRN_DEVICE_BUSY\n");
                return 0;
            } else if (busymsec > q->timeout) {
                av_log(avctx, AV_LOG_WARNING, "Timeout, device is so busy\n");
                return AVERROR(EIO);
            }
            av_usleep(1000);
            busymsec++;
        } else {
            busymsec = 0;
            remove_surface_list(q);
        }
    } while (ret == MFX_ERR_MORE_DATA || ret == MFX_WRN_DEVICE_BUSY);

    if (ret == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM && frame->interlaced_frame)
        print_interlace_msg(avctx, q);

    ret = ret == MFX_ERR_MORE_DATA ? 0 : ff_qsv_error(ret);

    if (outbuf->sync)
        enqueue_buffer(&q->pending_sync, &q->pending_sync_end, &q->nb_sync,
                       outbuf);

    if (q->pending_sync &&
        (q->nb_sync >= q->req.NumFrameMin || !frame)) {
        outbuf = dequeue_buffer(&q->pending_sync, &q->pending_sync_end,
                                &q->nb_sync);

        ret = MFXVideoCORE_SyncOperation(q->session, outbuf->sync,
                                         SYNC_TIME_DEFAULT);
        if ((ret = ff_qsv_error(ret)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "MFXVideoCORE_SyncOperation() failed\n");
            return ret;
        }

        if (outbuf->bs.FrameType & MFX_FRAMETYPE_REF ||
            outbuf->bs.FrameType & MFX_FRAMETYPE_xREF) {
            outbuf->dts = AV_NOPTS_VALUE;
        } else {
            outbuf->dts = outbuf->bs.TimeStamp;
            fill_buffer_dts(q, q->pending_dts_end, outbuf->dts);
        }

        enqueue_buffer(&q->pending_dts, &q->pending_dts_end, NULL, outbuf);
    }

    if (q->pending_dts && q->pending_dts->dts != AV_NOPTS_VALUE) {
        outbuf = dequeue_buffer(&q->pending_dts, &q->pending_dts_end, NULL);

        if ((ret = ff_alloc_packet(pkt, outbuf->bs.DataLength)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "ff_alloc_packet() failed\n");
            release_buffer(outbuf);
            return ret;
        }

//        pkt->dts  = outbuf->dts;
        pkt->pts  = outbuf->bs.TimeStamp;
        pkt->size = outbuf->bs.DataLength;

        if (outbuf->bs.FrameType & MFX_FRAMETYPE_I ||
            outbuf->bs.FrameType & MFX_FRAMETYPE_xI ||
            outbuf->bs.FrameType & MFX_FRAMETYPE_IDR ||
            outbuf->bs.FrameType & MFX_FRAMETYPE_xIDR)
            pkt->flags |= AV_PKT_FLAG_KEY;

        memcpy(pkt->data, outbuf->bs.Data + outbuf->bs.DataOffset,
               outbuf->bs.DataLength);

        release_buffer(outbuf);

        *got_packet = 1;
    }

    return ret;
}

int ff_qsv_enc_close(AVCodecContext *avctx, QSVEncContext *q)
{
    MFXVideoENCODE_Close(q->session);

    MFXClose(q->session);

    free_surface_pool(q);

    free_buffer_pool(q);

    return 0;
}
