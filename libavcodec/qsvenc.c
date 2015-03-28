/*
 * Intel MediaSDK QSV encoder utility functions
 *
 * copyright (c) 2013 Yukinori Yamazoe
 * copyright (c) 2015 Anton Khirnov
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
#include "libavutil/time.h"
#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "internal.h"
#include "qsv.h"
#include "qsv_internal.h"
#include "qsvenc.h"

static int init_video_param(AVCodecContext *avctx, QSVEncContext *q)
{
    const char *ratecontrol_desc;

    float quant;
    int ret;

    ret = ff_qsv_codec_id_to_mfx(avctx->codec_id);
    if (ret < 0)
        return AVERROR_BUG;
    q->param.mfx.CodecId = ret;

    if (avctx->level > 0)
        q->param.mfx.CodecLevel = avctx->level;

    q->param.mfx.CodecProfile       = q->profile;
    q->param.mfx.TargetUsage        = q->preset;
    q->param.mfx.GopPicSize         = FFMAX(0, avctx->gop_size);
    q->param.mfx.GopRefDist         = FFMAX(-1, avctx->max_b_frames) + 1;
    q->param.mfx.GopOptFlag         = avctx->flags & CODEC_FLAG_CLOSED_GOP ?
                                      MFX_GOP_CLOSED : 0;
    q->param.mfx.IdrInterval        = q->idr_interval;
    q->param.mfx.NumSlice           = avctx->slices;
    q->param.mfx.NumRefFrame        = FFMAX(0, avctx->refs);
    q->param.mfx.EncodedOrder       = 0;
    q->param.mfx.BufferSizeInKB     = 0;

    q->param.mfx.FrameInfo.FourCC         = MFX_FOURCC_NV12;
    q->param.mfx.FrameInfo.Width          = FFALIGN(avctx->width, 16);
    q->param.mfx.FrameInfo.Height         = FFALIGN(avctx->height, 32);
    q->param.mfx.FrameInfo.CropX          = 0;
    q->param.mfx.FrameInfo.CropY          = 0;
    q->param.mfx.FrameInfo.CropW          = avctx->width;
    q->param.mfx.FrameInfo.CropH          = avctx->height;
    q->param.mfx.FrameInfo.AspectRatioW   = avctx->sample_aspect_ratio.num;
    q->param.mfx.FrameInfo.AspectRatioH   = avctx->sample_aspect_ratio.den;
    q->param.mfx.FrameInfo.PicStruct      = MFX_PICSTRUCT_PROGRESSIVE;
    q->param.mfx.FrameInfo.ChromaFormat   = MFX_CHROMAFORMAT_YUV420;
    q->param.mfx.FrameInfo.BitDepthLuma   = 8;
    q->param.mfx.FrameInfo.BitDepthChroma = 8;

    if (avctx->framerate.den > 0 && avctx->framerate.num > 0) {
        q->param.mfx.FrameInfo.FrameRateExtN = avctx->framerate.num;
        q->param.mfx.FrameInfo.FrameRateExtD = avctx->framerate.den;
    } else {
        q->param.mfx.FrameInfo.FrameRateExtN  = avctx->time_base.den;
        q->param.mfx.FrameInfo.FrameRateExtD  = avctx->time_base.num;
    }

    if (avctx->flags & CODEC_FLAG_QSCALE) {
        q->param.mfx.RateControlMethod = MFX_RATECONTROL_CQP;
        ratecontrol_desc = "constant quantization parameter (CQP)";
    } else if (avctx->rc_max_rate == avctx->bit_rate) {
        q->param.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
        ratecontrol_desc = "constant bitrate (CBR)";
    } else if (!avctx->rc_max_rate) {
        q->param.mfx.RateControlMethod = MFX_RATECONTROL_AVBR;
        ratecontrol_desc = "average variable bitrate (AVBR)";
    } else {
        q->param.mfx.RateControlMethod = MFX_RATECONTROL_VBR;
        ratecontrol_desc = "variable bitrate (VBR)";
    }

    av_log(avctx, AV_LOG_VERBOSE, "Using the %s ratecontrol method\n", ratecontrol_desc);

    switch (q->param.mfx.RateControlMethod) {
    case MFX_RATECONTROL_CBR:
    case MFX_RATECONTROL_VBR:
        q->param.mfx.InitialDelayInKB = avctx->rc_initial_buffer_occupancy / 1000;
        q->param.mfx.TargetKbps       = avctx->bit_rate / 1000;
        q->param.mfx.MaxKbps          = avctx->bit_rate / 1000;
        break;
    case MFX_RATECONTROL_CQP:
        quant = avctx->global_quality / FF_QP2LAMBDA;

        q->param.mfx.QPI = av_clip(quant * fabs(avctx->i_quant_factor) + avctx->i_quant_offset, 0, 51);
        q->param.mfx.QPP = av_clip(quant, 0, 51);
        q->param.mfx.QPB = av_clip(quant * fabs(avctx->b_quant_factor) + avctx->b_quant_offset, 0, 51);

        break;
    case MFX_RATECONTROL_AVBR:
        q->param.mfx.TargetKbps  = avctx->bit_rate / 1000;
        q->param.mfx.Convergence = q->avbr_convergence;
        q->param.mfx.Accuracy    = q->avbr_accuracy;
        break;
    }

    q->extco.Header.BufferId      = MFX_EXTBUFF_CODING_OPTION;
    q->extco.Header.BufferSz      = sizeof(q->extco);
    q->extco.CAVLC                = avctx->coder_type == FF_CODER_TYPE_VLC ?
                                    MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN;

    q->extparam[0] = (mfxExtBuffer *)&q->extco;

    q->param.ExtParam    = q->extparam;
    q->param.NumExtParam = FF_ARRAY_ELEMS(q->extparam);

    return 0;
}

static int qsv_retrieve_enc_params(AVCodecContext *avctx, QSVEncContext *q)
{
    uint8_t sps_buf[128];
    uint8_t pps_buf[128];

    mfxExtCodingOptionSPSPPS extradata = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION_SPSPPS,
        .Header.BufferSz = sizeof(extradata),
        .SPSBuffer = sps_buf, .SPSBufSize = sizeof(sps_buf),
        .PPSBuffer = pps_buf, .PPSBufSize = sizeof(pps_buf)
    };

    mfxExtBuffer *ext_buffers[] = {
        (mfxExtBuffer*)&extradata,
    };

    int ret;

    q->param.ExtParam    = ext_buffers;
    q->param.NumExtParam = FF_ARRAY_ELEMS(ext_buffers);

    ret = MFXVideoENCODE_GetVideoParam(q->session, &q->param);
    if (ret < 0)
        return ff_qsv_error(ret);

    q->packet_size = q->param.mfx.BufferSizeInKB * 1000;

    if (!extradata.SPSBufSize || !extradata.PPSBufSize) {
        av_log(avctx, AV_LOG_ERROR, "No extradata returned from libmfx.\n");
        return AVERROR_UNKNOWN;
    }

    avctx->extradata = av_malloc(extradata.SPSBufSize + extradata.PPSBufSize +
                                 FF_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    memcpy(avctx->extradata,                        sps_buf, extradata.SPSBufSize);
    memcpy(avctx->extradata + extradata.SPSBufSize, pps_buf, extradata.PPSBufSize);
    avctx->extradata_size = extradata.SPSBufSize + extradata.PPSBufSize;
    memset(avctx->extradata + avctx->extradata_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    return 0;
}

int ff_qsv_enc_init(AVCodecContext *avctx, QSVEncContext *q)
{
    int ret;

    q->param.IOPattern  = MFX_IOPATTERN_IN_SYSTEM_MEMORY;
    q->param.AsyncDepth = q->async_depth;

    if (avctx->hwaccel_context) {
        AVQSVContext *qsv = avctx->hwaccel_context;

        q->session         = qsv->session;
        q->param.IOPattern = qsv->iopattern;
    }

    if (!q->session) {
        ret = ff_qsv_init_internal_session(avctx, &q->internal_session);
        if (ret < 0)
            return ret;

        q->session = q->internal_session;
    }

    ret = init_video_param(avctx, q);
    if (ret < 0)
        return ret;

    ret = MFXVideoENCODE_QueryIOSurf(q->session, &q->param, &q->req);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error querying the encoding parameters\n");
        return ff_qsv_error(ret);
    }

    ret = MFXVideoENCODE_Init(q->session, &q->param);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing the encoder\n");
        return ff_qsv_error(ret);
    }

    ret = qsv_retrieve_enc_params(avctx, q);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error retrieving encoding parameters.\n");
        return ret;
    }

    avctx->coded_frame = av_frame_alloc();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    q->avctx = avctx;

    return 0;
}

static void clear_unused_frames(QSVEncContext *q)
{
    QSVFrame *cur = q->work_frames;
    while (cur) {
        if (cur->surface && !cur->surface->Data.Locked) {
            cur->surface = NULL;
            av_frame_unref(cur->frame);
        }
        cur = cur->next;
    }
}

static int get_free_frame(QSVEncContext *q, QSVFrame **f)
{
    QSVFrame *frame, **last;

    clear_unused_frames(q);

    frame = q->work_frames;
    last  = &q->work_frames;
    while (frame) {
        if (!frame->surface) {
            *f = frame;
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

    *f = frame;

    return 0;
}

static int submit_frame(QSVEncContext *q, const AVFrame *frame,
                        mfxFrameSurface1 **surface)
{
    QSVFrame *qf;
    int ret;

    ret = get_free_frame(q, &qf);
    if (ret < 0)
        return ret;

    if (frame->format == AV_PIX_FMT_QSV) {
        ret = av_frame_ref(qf->frame, frame);
        if (ret < 0)
            return ret;

        qf->surface = (mfxFrameSurface1*)qf->frame->data[3];
        *surface = qf->surface;
        return 0;
    }

    /* make a copy if the input is not padded as libmfx requires */
    if (frame->height & 31 || frame->linesize[0] & 15) {
        qf->frame->height = FFALIGN(frame->height, 32);
        qf->frame->width  = FFALIGN(frame->width, 16);

        ret = ff_get_buffer(q->avctx, qf->frame, AV_GET_BUFFER_FLAG_REF);
        if (ret < 0)
            return ret;

        qf->frame->height = frame->height;
        qf->frame->width  = frame->width;
        ret = av_frame_copy(qf->frame, frame);
        if (ret < 0) {
            av_frame_unref(qf->frame);
            return ret;
        }
    } else {
        ret = av_frame_ref(qf->frame, frame);
        if (ret < 0)
            return ret;
    }

    qf->surface_internal.Info = q->param.mfx.FrameInfo;

    qf->surface_internal.Info.PicStruct =
        !frame->interlaced_frame ? MFX_PICSTRUCT_PROGRESSIVE :
        frame->top_field_first   ? MFX_PICSTRUCT_FIELD_TFF :
                                   MFX_PICSTRUCT_FIELD_BFF;
    if (frame->repeat_pict == 1)
        qf->surface_internal.Info.PicStruct |= MFX_PICSTRUCT_FIELD_REPEATED;
    else if (frame->repeat_pict == 2)
        qf->surface_internal.Info.PicStruct |= MFX_PICSTRUCT_FRAME_DOUBLING;
    else if (frame->repeat_pict == 4)
        qf->surface_internal.Info.PicStruct |= MFX_PICSTRUCT_FRAME_TRIPLING;

    qf->surface_internal.Data.PitchLow  = qf->frame->linesize[0];
    qf->surface_internal.Data.Y         = qf->frame->data[0];
    qf->surface_internal.Data.UV        = qf->frame->data[1];
    qf->surface_internal.Data.TimeStamp = av_rescale_q(frame->pts, q->avctx->time_base, (AVRational){1, 90000});

    qf->surface = &qf->surface_internal;

    *surface = qf->surface;

    return 0;
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

int ff_qsv_encode(AVCodecContext *avctx, QSVEncContext *q,
                  AVPacket *pkt, const AVFrame *frame, int *got_packet)
{
    mfxBitstream bs = { { { 0 } } };

    mfxFrameSurface1 *surf = NULL;
    mfxSyncPoint sync      = NULL;
    int ret;

    if (frame) {
        ret = submit_frame(q, frame, &surf);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error submitting the frame for encoding.\n");
            return ret;
        }
    }

    ret = ff_alloc_packet(pkt, q->packet_size);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating the output packet\n");
        return ret;
    }
    bs.Data      = pkt->data;
    bs.MaxLength = pkt->size;

    do {
        ret = MFXVideoENCODE_EncodeFrameAsync(q->session, NULL, surf, &bs, &sync);
        if (ret == MFX_WRN_DEVICE_BUSY)
            av_usleep(1);
    } while (ret > 0);

    if (ret < 0)
        return (ret == MFX_ERR_MORE_DATA) ? 0 : ff_qsv_error(ret);

    if (ret == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM && frame->interlaced_frame)
        print_interlace_msg(avctx, q);

    if (sync) {
        MFXVideoCORE_SyncOperation(q->session, sync, 60000);

        if (bs.FrameType & MFX_FRAMETYPE_I || bs.FrameType & MFX_FRAMETYPE_xI)
            avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
        else if (bs.FrameType & MFX_FRAMETYPE_P || bs.FrameType & MFX_FRAMETYPE_xP)
            avctx->coded_frame->pict_type = AV_PICTURE_TYPE_P;
        else if (bs.FrameType & MFX_FRAMETYPE_B || bs.FrameType & MFX_FRAMETYPE_xB)
            avctx->coded_frame->pict_type = AV_PICTURE_TYPE_B;

        pkt->dts  = av_rescale_q(bs.DecodeTimeStamp, (AVRational){1, 90000}, avctx->time_base);
        pkt->pts  = av_rescale_q(bs.TimeStamp,       (AVRational){1, 90000}, avctx->time_base);
        pkt->size = bs.DataLength;

        if (bs.FrameType & MFX_FRAMETYPE_IDR ||
            bs.FrameType & MFX_FRAMETYPE_xIDR)
            pkt->flags |= AV_PKT_FLAG_KEY;

        *got_packet = 1;
    }

    return 0;
}

int ff_qsv_enc_close(AVCodecContext *avctx, QSVEncContext *q)
{
    QSVFrame *cur;

    MFXVideoENCODE_Close(q->session);
    if (q->internal_session)
        MFXClose(q->internal_session);
    q->session          = NULL;
    q->internal_session = NULL;

    cur = q->work_frames;
    while (cur) {
        q->work_frames = cur->next;
        av_frame_free(&cur->frame);
        av_freep(&cur);
        cur = q->work_frames;
    }

    av_frame_free(&avctx->coded_frame);

    return 0;
}
