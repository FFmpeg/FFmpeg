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
  //q->param.mfx.TimeStampCalc      = 0; // API 1.3
  //q->param.mfx.ExtendedPicStruct  = 0; // API 1.3
  //q->param.mfx.BRCParamMultiplier = 0; // API 1.3
  //q->param.mfx.SliceGroupsPresent = 0; // API 1.6
    q->param.mfx.RateControlMethod =
        (q->qpi >= 0 && q->qpp >= 0 && q->qpb >= 0) ||
        avctx->flags & CODEC_FLAG_QSCALE      ? MFX_RATECONTROL_CQP :
        avctx->rc_max_rate &&
        avctx->rc_max_rate == avctx->bit_rate ? MFX_RATECONTROL_CBR :
                                                MFX_RATECONTROL_VBR;

    if (ret == MFX_CODEC_AVC)
        av_log(avctx, AV_LOG_VERBOSE, "Codec:AVC\n");
    else if (ret == MFX_CODEC_MPEG2)
        av_log(avctx, AV_LOG_VERBOSE, "Codec:MPEG2\n");
    if (q->param.mfx.GopPicSize)
        av_log(avctx, AV_LOG_VERBOSE, "GopPicSize:%d\n", q->param.mfx.GopPicSize);
    if (q->param.mfx.GopRefDist)
        av_log(avctx, AV_LOG_VERBOSE, "GopRefDist:%d\n", q->param.mfx.GopRefDist);
    if (q->param.mfx.NumSlice)
        av_log(avctx, AV_LOG_VERBOSE, "NumSlice:%d\n", q->param.mfx.NumSlice);
    if (q->param.mfx.NumRefFrame)
        av_log(avctx, AV_LOG_VERBOSE, "NumRefFrame:%d\n", q->param.mfx.NumRefFrame);

    switch (q->param.mfx.RateControlMethod) {
    case MFX_RATECONTROL_CBR: // API 1.0
        av_log(avctx, AV_LOG_VERBOSE, "RateControlMethod:CBR\n");
      //q->param.mfx.InitialDelayInKB;
        q->param.mfx.TargetKbps = avctx->bit_rate / 1000;
        q->param.mfx.MaxKbps    = avctx->bit_rate / 1000;
        av_log(avctx, AV_LOG_VERBOSE, "TargetKbps:%d\n", q->param.mfx.TargetKbps);
        break;
    case MFX_RATECONTROL_VBR: // API 1.0
        av_log(avctx, AV_LOG_VERBOSE, "RateControlMethod:VBR\n");
      //q->param.mfx.InitialDelayInKB;
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
    case MFX_RATECONTROL_AVBR: // API 1.3
        av_log(avctx, AV_LOG_ERROR,
               "RateControlMethod:AVBR is unimplemented.\n");
        /*
        q->param.mfx.TargetKbps;
        q->param.mfx.Accuracy;    // API 1.3
        q->param.mfx.Convergence; // API 1.3
        */
        return AVERROR(EINVAL);
#if 0
    case MFX_RATECONTROL_LA: // API 1.7
        av_log(avctx, AV_LOG_ERROR,
               "RateControlMethod:LA is unimplemented.\n");
        /*
        q->param.mfx.InitialDelayInKB;
        q->param.mfx.TargetKbps;
        q->param.mfx.MaxKbps;
        q->extco2.LookAheadDepth; // API 1.7
        q->extco2.Trellis;        // API 1.6
        */
        return AVERROR(EINVAL);
#endif
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
    q->param.mfx.FrameInfo.PicStruct     = MFX_PICSTRUCT_PROGRESSIVE;
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
  //q->extco.NalHrdConformance    = MFX_CODINGOPTION_UNKNOWN; // API 1.3
  //q->extco.SingleSeiNalUnit     = MFX_CODINGOPTION_UNKNOWN; // API 1.3
  //q->extco.VuiVclHrdParameters  = MFX_CODINGOPTION_UNKNOWN; // API 1.3
    q->extco.ResetRefList         = MFX_CODINGOPTION_UNKNOWN;
  //q->extco.RefPicMarkRep        = MFX_CODINGOPTION_UNKNOWN; // API 1.3
  //q->extco.FieldOutput          = MFX_CODINGOPTION_UNKNOWN; // API 1.3
  //q->extco.ViewOutput           = MFX_CODINGOPTION_UNKNOWN; // API 1.4
    q->extco.MaxDecFrameBuffering = MFX_CODINGOPTION_UNKNOWN;
    q->extco.AUDelimiter          = MFX_CODINGOPTION_UNKNOWN; // or OFF
    q->extco.EndOfStream          = MFX_CODINGOPTION_UNKNOWN;
    q->extco.PicTimingSEI         = MFX_CODINGOPTION_UNKNOWN; // or OFF
    q->extco.VuiNalHrdParameters  = MFX_CODINGOPTION_UNKNOWN;
    q->extco.FramePicture         = MFX_CODINGOPTION_ON;
  //q->extco.RecoveryPointSEI     = MFX_CODINGOPTION_UNKNOWN; // API 1.6

    if (q->extco.CAVLC == MFX_CODINGOPTION_ON)
        av_log(avctx, AV_LOG_VERBOSE, "CAVLC:ON\n");

    q->extparam[q->param.NumExtParam] = (mfxExtBuffer *)&q->extco;
    q->param.ExtParam = q->extparam;
    q->param.NumExtParam++;

/*
    q->extcospspps.Header.BufferId = MFX_EXTBUFF_CODING_OPTION_SPSPPS;
    q->extcospspps.Header.BufferSz = sizeof(q->extcospspps);
    q->extcospspps.SPSBuffer       = q->spspps[0];
    q->extcospspps.SPSBufSize      = sizeof(q->spspps[0]);
    q->extcospspps.PPSBuffer       = q->spspps[1];
    q->extcospspps.PPSBufSize      = sizeof(q->spspps[1]);

    q->extparam[q->param.NumExtParam] = (mfxExtBuffer *)&q->extcospspps;
    q->param.ExtParam = q->extparam;
    q->param.NumExtParam++;
*/

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

    if (impl & MFX_IMPL_SOFTWARE)
        av_log(avctx, AV_LOG_VERBOSE,
               "Using Intel QuickSync encoder software implementation.\n");
    else if (impl & MFX_IMPL_HARDWARE)
        av_log(avctx, AV_LOG_VERBOSE,
               "Using Intel QuickSync encoder hardware accelerated implementation.\n");
    else
        av_log(avctx, AV_LOG_VERBOSE,
               "Unknown Intel QuickSync encoder implementation %d.\n", impl);

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

    MFXVideoENCODE_GetVideoParam(q->session, &q->param);

    q->first_pts = AV_NOPTS_VALUE;
    q->pts_delay = AV_NOPTS_VALUE;

    return ret;
}

static QSVEncSurfaceList *get_surface_pool(QSVEncContext *q)
{
    QSVEncSurfaceList **pool = &q->surf_pool;
    QSVEncSurfaceList *list;

    while (*pool && ((*pool)->surface.Data.Locked ||
            (*pool)->prev || (*pool)->next))
        pool = &(*pool)->pool;

    if (!(*pool))
        if (!(*pool = av_mallocz(sizeof(QSVEncSurfaceList))))
            return NULL;

    list = *pool;
    if (list->surface.Data.MemId)
        av_frame_free((AVFrame **)(&list->surface.Data.MemId));

    return list;
}

static void free_surface_pool(QSVEncContext *q)
{
    QSVEncSurfaceList **pool = &q->surf_pool;
    QSVEncSurfaceList *list;

    while (*pool) {
        list = *pool;
        *pool = list->pool;
        av_frame_free((AVFrame **)(&list->surface.Data.MemId));
        av_freep(&list);
    }
}

static AVFrame *clone_aligned_frame(AVCodecContext *avctx, AVFrame *frame)
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

static int put_surface_from_frame(AVCodecContext *avctx, QSVEncContext *q, AVFrame *frame)
{
    QSVEncSurfaceList *list;
    AVFrame *clone;

    if (!(list = get_surface_pool(q)))
        return AVERROR(ENOMEM);

    if (!(clone = clone_aligned_frame(avctx, frame)))
        return AVERROR(ENOMEM);

    set_surface_param(q, &list->surface, clone);

    list->prev = q->pending_enc_end;
    list->next = NULL;

    if (q->pending_enc_end)
        q->pending_enc_end->next = list;
    else
        q->pending_enc = list;

    q->pending_enc_end = list;

    return 0;
}

static mfxFrameSurface1 *get_surface(QSVEncContext *q)
{
    QSVEncSurfaceList *list = q->pending_enc;

    q->pending_enc = list->next;

    if (q->pending_enc)
        q->pending_enc->prev = NULL;
    else
        q->pending_enc_end = NULL;

    list->prev = list->next = NULL;

    return &list->surface;
}

static QSVEncBuffer *alloc_buffer(QSVEncContext *q)
{
    QSVEncBuffer *buf = NULL;
    uint8_t *data     = NULL;
    int size          = q->param.mfx.BufferSizeInKB * 1000;

    if (!(buf = av_mallocz(sizeof(*buf)))) {
        av_log(q, AV_LOG_ERROR, "av_mallocz() failed\n");
        goto fail;
    }
    if (!(data = av_mallocz(size))) {
        av_log(q, AV_LOG_ERROR, "av_mallocz() failed\n");
        goto fail;
    }

    buf->bs.Data      = buf->data = data;
    buf->bs.MaxLength = size;

    return buf;

fail:
    av_freep(&buf);
    av_freep(&data);

    return NULL;
}

static QSVEncBuffer *get_buffer(QSVEncContext *q)
{
    QSVEncBuffer **pool = &q->buf_pool;
    QSVEncBuffer *buf;

    while (*pool && (*pool)->sync)
        pool = &(*pool)->pool;

    if (!(*pool))
        if (!(*pool = alloc_buffer(q)))
            return NULL;

    buf = *pool;
    buf->bs.DataOffset = 0;
    buf->bs.DataLength = 0;
    buf->prev = NULL;
    buf->next = NULL;

    return buf;
}

static void release_buffer(QSVEncBuffer *buf)
{
    buf->sync = 0;
}

static void free_buffer_pool(QSVEncContext *q)
{
    QSVEncBuffer **pool = &q->buf_pool;
    QSVEncBuffer *buf;

    while (*pool) {
        buf = *pool;
        *pool = buf->pool;
        av_freep(&buf->data);
        av_freep(&buf);
    }
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

static void print_frametype(AVCodecContext *avctx, QSVEncContext *q,
                            mfxBitstream *bs, int indent)
{
    char buf[1024];

    buf[0] = '\0';

    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
             "TimeStamp:%"PRId64", ", bs->TimeStamp);
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "FrameType:");

    if (bs->FrameType & MFX_FRAMETYPE_I)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " I");
    if (bs->FrameType & MFX_FRAMETYPE_P)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " P");
    if (bs->FrameType & MFX_FRAMETYPE_B)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " B");
    if (bs->FrameType & MFX_FRAMETYPE_S)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " S");
    if (bs->FrameType & MFX_FRAMETYPE_REF)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " REF");
    if (bs->FrameType & MFX_FRAMETYPE_IDR)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " IDR");
    if (bs->FrameType & MFX_FRAMETYPE_xI)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " xI");
    if (bs->FrameType & MFX_FRAMETYPE_xP)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " xP");
    if (bs->FrameType & MFX_FRAMETYPE_xB)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " xB");
    if (bs->FrameType & MFX_FRAMETYPE_xS)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " xS");
    if (bs->FrameType & MFX_FRAMETYPE_xREF)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " xREF");
    if (bs->FrameType & MFX_FRAMETYPE_xIDR)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " xIDR");

    av_log(q, AV_LOG_DEBUG, "%*s%s\n", 4 * indent, "", buf);
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
    mfxFrameSurface1 *surf = NULL;
    QSVEncBuffer *outbuf   = NULL;
    int ret;

    *got_packet = 0;

    if (frame) {
        if (q->first_pts == AV_NOPTS_VALUE)
            q->first_pts = frame->pts;
        else if (q->pts_delay == AV_NOPTS_VALUE)
            q->pts_delay = frame->pts - q->first_pts;

        if ((ret = put_surface_from_frame(avctx, q, frame)) < 0)
            return ret;

        ret = MFX_ERR_MORE_DATA;
    } else {
        ret = MFX_ERR_NONE;
    }

    do {
        if (ret == MFX_ERR_MORE_DATA) {
            if (q->pending_enc)
                surf = get_surface(q);
            else
                break;
        }

        outbuf = get_buffer(q);

        ret = MFXVideoENCODE_EncodeFrameAsync(q->session, NULL, surf,
                                              &outbuf->bs, &outbuf->sync);

        if (ret == MFX_WRN_DEVICE_BUSY) {
            av_usleep(1000);
            return 0;
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
        if ((ret = ff_qsv_error(ret)) < 0)
            return ret;

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

//        print_frametype(avctx, q, &outbuf->bs, 12);

        if ((ret = ff_alloc_packet(pkt, outbuf->bs.DataLength)) < 0) {
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
