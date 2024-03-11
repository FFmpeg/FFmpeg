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

#include "config_components.h"

#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <mfxvideo.h>

#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/mem.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "libavutil/time.h"
#include "libavutil/imgutils.h"
#include "libavutil/film_grain_params.h"
#include "libavutil/mastering_display_metadata.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "internal.h"
#include "decode.h"
#include "hwconfig.h"
#include "qsv.h"
#include "qsv_internal.h"

#if QSV_ONEVPL
#include <mfxdispatcher.h>
#else
#define MFXUnload(a) do { } while(0)
#endif

static const AVRational mfx_tb = { 1, 90000 };

#define PTS_TO_MFX_PTS(pts, pts_tb) ((pts) == AV_NOPTS_VALUE ? \
    MFX_TIMESTAMP_UNKNOWN : pts_tb.num ? \
    av_rescale_q(pts, pts_tb, mfx_tb) : pts)

#define MFX_PTS_TO_PTS(mfx_pts, pts_tb) ((mfx_pts) == MFX_TIMESTAMP_UNKNOWN ? \
    AV_NOPTS_VALUE : pts_tb.num ? \
    av_rescale_q(mfx_pts, mfx_tb, pts_tb) : mfx_pts)

typedef struct QSVAsyncFrame {
    mfxSyncPoint *sync;
    QSVFrame     *frame;
} QSVAsyncFrame;

typedef struct QSVContext {
    // the session used for decoding
    mfxSession session;
    mfxVersion ver;

    // the session we allocated internally, in case the caller did not provide
    // one
    QSVSession internal_qs;

    QSVFramesContext frames_ctx;

    /**
     * a linked list of frames currently being used by QSV
     */
    QSVFrame *work_frames;

    AVFifo *async_fifo;
    int zero_consume_run;
    int reinit_flag;

    enum AVPixelFormat orig_pix_fmt;
    uint32_t fourcc;
    mfxFrameInfo frame_info;
    AVBufferPool *pool;
    int suggest_pool_size;
    int initialized;

    // options set by the caller
    int async_depth;
    int iopattern;
    int gpu_copy;

    char *load_plugins;

    mfxExtBuffer **ext_buffers;
    int         nb_ext_buffers;
} QSVContext;

static const AVCodecHWConfigInternal *const qsv_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public = {
            .pix_fmt     = AV_PIX_FMT_QSV,
            .methods     = AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX |
                           AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX,
            .device_type = AV_HWDEVICE_TYPE_QSV,
        },
        .hwaccel = NULL,
    },
    NULL
};

static int qsv_get_continuous_buffer(AVCodecContext *avctx, AVFrame *frame,
                                     AVBufferPool *pool)
{
    int ret = 0;

    ret = ff_decode_frame_props(avctx, frame);
    if (ret < 0)
        return ret;

    frame->width       = avctx->width;
    frame->height      = avctx->height;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_NV12:
        frame->linesize[0] = FFALIGN(avctx->width, 128);
        break;
    case AV_PIX_FMT_P010:
    case AV_PIX_FMT_P012:
    case AV_PIX_FMT_YUYV422:
        frame->linesize[0] = 2 * FFALIGN(avctx->width, 128);
        break;
    case AV_PIX_FMT_Y210:
    case AV_PIX_FMT_VUYX:
    case AV_PIX_FMT_XV30:
    case AV_PIX_FMT_Y212:
        frame->linesize[0] = 4 * FFALIGN(avctx->width, 128);
        break;
    case AV_PIX_FMT_XV36:
        frame->linesize[0] = 8 * FFALIGN(avctx->width, 128);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format.\n");
        return AVERROR(EINVAL);
    }

    frame->buf[0]      = av_buffer_pool_get(pool);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    frame->data[0] = frame->buf[0]->data;
    if (avctx->pix_fmt == AV_PIX_FMT_NV12 ||
        avctx->pix_fmt == AV_PIX_FMT_P010 ||
        avctx->pix_fmt == AV_PIX_FMT_P012) {
        frame->linesize[1] = frame->linesize[0];
        frame->data[1] = frame->data[0] +
            frame->linesize[0] * FFALIGN(avctx->height, 64);
    }

    ret = ff_attach_decode_data(frame);
    if (ret < 0)
        return ret;

    return 0;
}

static int qsv_init_session(AVCodecContext *avctx, QSVContext *q, mfxSession session,
                            AVBufferRef *hw_frames_ref, AVBufferRef *hw_device_ref)
{
    int ret;

    if (q->gpu_copy == MFX_GPUCOPY_ON &&
        !(q->iopattern & MFX_IOPATTERN_OUT_SYSTEM_MEMORY)) {
        av_log(avctx, AV_LOG_WARNING, "GPU-accelerated memory copy "
                        "only works in system memory mode.\n");
        q->gpu_copy = MFX_GPUCOPY_OFF;
    }
    if (session) {
        q->session = session;
    } else if (hw_frames_ref) {
        if (q->internal_qs.session) {
            MFXClose(q->internal_qs.session);
            q->internal_qs.session = NULL;
        }
        av_buffer_unref(&q->frames_ctx.hw_frames_ctx);

        q->frames_ctx.hw_frames_ctx = av_buffer_ref(hw_frames_ref);
        if (!q->frames_ctx.hw_frames_ctx)
            return AVERROR(ENOMEM);

        ret = ff_qsv_init_session_frames(avctx, &q->internal_qs.session,
                                         &q->frames_ctx, q->load_plugins,
#if QSV_HAVE_OPAQUE
                                         q->iopattern == MFX_IOPATTERN_OUT_OPAQUE_MEMORY,
#else
                                         0,
#endif
                                         q->gpu_copy);
        if (ret < 0) {
            av_buffer_unref(&q->frames_ctx.hw_frames_ctx);
            return ret;
        }

        q->session = q->internal_qs.session;
    } else if (hw_device_ref) {
        if (q->internal_qs.session) {
            MFXClose(q->internal_qs.session);
            q->internal_qs.session = NULL;
        }

        ret = ff_qsv_init_session_device(avctx, &q->internal_qs.session,
                                         hw_device_ref, q->load_plugins, q->gpu_copy);
        if (ret < 0)
            return ret;

        q->session = q->internal_qs.session;
    } else {
        if (!q->internal_qs.session) {
            ret = ff_qsv_init_internal_session(avctx, &q->internal_qs,
                                               q->load_plugins, q->gpu_copy);
            if (ret < 0)
                return ret;
        }

        q->session = q->internal_qs.session;
    }

    if (MFXQueryVersion(q->session, &q->ver) != MFX_ERR_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Error querying the session version. \n");
        q->session = NULL;

        if (q->internal_qs.session) {
            MFXClose(q->internal_qs.session);
            q->internal_qs.session = NULL;
        }

        if (q->internal_qs.loader) {
            MFXUnload(q->internal_qs.loader);
            q->internal_qs.loader = NULL;
        }

        return AVERROR_EXTERNAL;
    }

    /* make sure the decoder is uninitialized */
    MFXVideoDECODE_Close(q->session);

    return 0;
}

static int qsv_decode_preinit(AVCodecContext *avctx, QSVContext *q, enum AVPixelFormat pix_fmt, mfxVideoParam *param)
{
    mfxSession session = NULL;
    int iopattern = 0;
    int ret;
    enum AVPixelFormat pix_fmts[3] = {
        AV_PIX_FMT_QSV, /* opaque format in case of video memory output */
        pix_fmt,        /* system memory format obtained from bitstream parser */
        AV_PIX_FMT_NONE };

    ret = ff_get_format(avctx, pix_fmts);
    if (ret < 0) {
        q->orig_pix_fmt = avctx->pix_fmt = AV_PIX_FMT_NONE;
        return ret;
    }

    if (!q->async_fifo) {
        q->async_fifo = av_fifo_alloc2(q->async_depth, sizeof(QSVAsyncFrame), 0);
        if (!q->async_fifo)
            return AVERROR(ENOMEM);
    }

    if (avctx->pix_fmt == AV_PIX_FMT_QSV && avctx->hwaccel_context) {
        AVQSVContext *user_ctx = avctx->hwaccel_context;
        session           = user_ctx->session;
        iopattern         = user_ctx->iopattern;
        q->ext_buffers    = user_ctx->ext_buffers;
        q->nb_ext_buffers = user_ctx->nb_ext_buffers;
    }

    if (avctx->hw_device_ctx && !avctx->hw_frames_ctx && ret == AV_PIX_FMT_QSV) {
        AVHWFramesContext *hwframes_ctx;
        AVQSVFramesContext *frames_hwctx;

        avctx->hw_frames_ctx = av_hwframe_ctx_alloc(avctx->hw_device_ctx);

        if (!avctx->hw_frames_ctx) {
            av_log(avctx, AV_LOG_ERROR, "av_hwframe_ctx_alloc failed\n");
            return AVERROR(ENOMEM);
        }

        hwframes_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        frames_hwctx = hwframes_ctx->hwctx;
        hwframes_ctx->width             = FFALIGN(avctx->coded_width,  32);
        hwframes_ctx->height            = FFALIGN(avctx->coded_height, 32);
        hwframes_ctx->format            = AV_PIX_FMT_QSV;
        hwframes_ctx->sw_format         = avctx->sw_pix_fmt;
        hwframes_ctx->initial_pool_size = q->suggest_pool_size + 16 + avctx->extra_hw_frames;
        frames_hwctx->frame_type        = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;

        ret = av_hwframe_ctx_init(avctx->hw_frames_ctx);

        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error initializing a QSV frame pool\n");
            av_buffer_unref(&avctx->hw_frames_ctx);
            return ret;
        }
    }

    if (avctx->hw_frames_ctx) {
        AVHWFramesContext    *frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        AVQSVFramesContext *frames_hwctx = frames_ctx->hwctx;

        if (!iopattern) {
#if QSV_HAVE_OPAQUE
            if (frames_hwctx->frame_type & MFX_MEMTYPE_OPAQUE_FRAME)
                iopattern = MFX_IOPATTERN_OUT_OPAQUE_MEMORY;
            else if (frames_hwctx->frame_type & MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET)
                iopattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
#else
            if (frames_hwctx->frame_type & MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET)
                iopattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
#endif
        }
    }

    if (!iopattern)
        iopattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
    q->iopattern = iopattern;

    ff_qsv_print_iopattern(avctx, q->iopattern, "Decoder");

    ret = qsv_init_session(avctx, q, session, avctx->hw_frames_ctx, avctx->hw_device_ctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing an MFX session\n");
        return ret;
    }

    param->IOPattern   = q->iopattern;
    param->AsyncDepth  = q->async_depth;
    param->ExtParam    = q->ext_buffers;
    param->NumExtParam = q->nb_ext_buffers;

    return 0;
 }

static int qsv_decode_init_context(AVCodecContext *avctx, QSVContext *q, mfxVideoParam *param)
{
    int ret;

    avctx->width        = param->mfx.FrameInfo.CropW;
    avctx->height       = param->mfx.FrameInfo.CropH;
    avctx->coded_width  = param->mfx.FrameInfo.Width;
    avctx->coded_height = param->mfx.FrameInfo.Height;
    avctx->level        = param->mfx.CodecLevel;
    avctx->profile      = param->mfx.CodecProfile;
    avctx->field_order  = ff_qsv_map_picstruct(param->mfx.FrameInfo.PicStruct);
    avctx->pix_fmt      = ff_qsv_map_fourcc(param->mfx.FrameInfo.FourCC);

    ret = MFXVideoDECODE_Init(q->session, param);
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret,
                                  "Error initializing the MFX video decoder");

    q->frame_info = param->mfx.FrameInfo;

    if (!avctx->hw_frames_ctx)
        q->pool = av_buffer_pool_init(av_image_get_buffer_size(avctx->pix_fmt,
                    FFALIGN(avctx->width, 128), FFALIGN(avctx->height, 64), 1), av_buffer_allocz);
    return 0;
}

static int qsv_decode_header(AVCodecContext *avctx, QSVContext *q,
                             const AVPacket *avpkt, enum AVPixelFormat pix_fmt,
                             mfxVideoParam *param)
{
    int ret;
    mfxExtVideoSignalInfo video_signal_info = { 0 };
    mfxExtBuffer *header_ext_params[1] = { (mfxExtBuffer *)&video_signal_info };
    mfxBitstream bs = { 0 };

    if (avpkt->size) {
        bs.Data       = avpkt->data;
        bs.DataLength = avpkt->size;
        bs.MaxLength  = bs.DataLength;
        bs.TimeStamp  = PTS_TO_MFX_PTS(avpkt->pts, avctx->pkt_timebase);
        if (avctx->field_order == AV_FIELD_PROGRESSIVE)
            bs.DataFlag   |= MFX_BITSTREAM_COMPLETE_FRAME;
    } else
        return AVERROR_INVALIDDATA;


    if(!q->session) {
        ret = qsv_decode_preinit(avctx, q, pix_fmt, param);
        if (ret < 0)
            return ret;
    }

    ret = ff_qsv_codec_id_to_mfx(avctx->codec_id);
    if (ret < 0)
        return ret;

    param->mfx.CodecId = ret;
    video_signal_info.Header.BufferId = MFX_EXTBUFF_VIDEO_SIGNAL_INFO;
    video_signal_info.Header.BufferSz = sizeof(video_signal_info);
    // The SDK doesn't support other ext buffers when calling MFXVideoDECODE_DecodeHeader,
    // so do not append this buffer to the existent buffer array
    param->ExtParam    = header_ext_params;
    param->NumExtParam = 1;
    ret = MFXVideoDECODE_DecodeHeader(q->session, &bs, param);
    if (MFX_ERR_MORE_DATA == ret) {
       return AVERROR(EAGAIN);
    }
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret,
                "Error decoding stream header");

    avctx->color_range = video_signal_info.VideoFullRange ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    if (video_signal_info.ColourDescriptionPresent) {
        avctx->color_primaries = video_signal_info.ColourPrimaries;
        avctx->color_trc = video_signal_info.TransferCharacteristics;
        avctx->colorspace = video_signal_info.MatrixCoefficients;
    }

    param->ExtParam    = q->ext_buffers;
    param->NumExtParam = q->nb_ext_buffers;

#if QSV_VERSION_ATLEAST(1, 34)
    if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 34) && avctx->codec_id == AV_CODEC_ID_AV1)
        param->mfx.FilmGrain = (avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN) ? 0 : param->mfx.FilmGrain;
#endif

    return 0;
}

static int alloc_frame(AVCodecContext *avctx, QSVContext *q, QSVFrame *frame)
{
    int ret;

    if (q->pool)
        ret = qsv_get_continuous_buffer(avctx, frame->frame, q->pool);
    else
        ret = ff_get_buffer(avctx, frame->frame, AV_GET_BUFFER_FLAG_REF);

    if (ret < 0)
        return ret;

    if (frame->frame->format == AV_PIX_FMT_QSV) {
        frame->surface = *(mfxFrameSurface1*)frame->frame->data[3];
    } else {
        ret = ff_qsv_map_frame_to_surface(frame->frame, &frame->surface);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "map frame to surface failed.\n");
            return ret;
        }
    }

    frame->surface.Info = q->frame_info;

    if (q->frames_ctx.mids) {
        ret = ff_qsv_find_surface_idx(&q->frames_ctx, frame);
        if (ret < 0)
            return ret;

        frame->surface.Data.MemId = &q->frames_ctx.mids[ret];
    }

    frame->surface.Data.ExtParam    = frame->ext_param;
    frame->surface.Data.NumExtParam = 0;
    frame->num_ext_params           = 0;
    frame->dec_info.Header.BufferId = MFX_EXTBUFF_DECODED_FRAME_INFO;
    frame->dec_info.Header.BufferSz = sizeof(frame->dec_info);
    ff_qsv_frame_add_ext_param(avctx, frame, (mfxExtBuffer *)&frame->dec_info);
#if QSV_VERSION_ATLEAST(1, 34)
    if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 34) && avctx->codec_id == AV_CODEC_ID_AV1) {
        frame->av1_film_grain_param.Header.BufferId = MFX_EXTBUFF_AV1_FILM_GRAIN_PARAM;
        frame->av1_film_grain_param.Header.BufferSz = sizeof(frame->av1_film_grain_param);
        frame->av1_film_grain_param.FilmGrainFlags = 0;
        ff_qsv_frame_add_ext_param(avctx, frame, (mfxExtBuffer *)&frame->av1_film_grain_param);
    }
#endif

#if QSV_VERSION_ATLEAST(1, 35)
    if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 35) && avctx->codec_id == AV_CODEC_ID_HEVC) {
        frame->mdcv.Header.BufferId = MFX_EXTBUFF_MASTERING_DISPLAY_COLOUR_VOLUME;
        frame->mdcv.Header.BufferSz = sizeof(frame->mdcv);
        // The data in mdcv is valid when this flag is 1
        frame->mdcv.InsertPayloadToggle = 0;
        ff_qsv_frame_add_ext_param(avctx, frame, (mfxExtBuffer *)&frame->mdcv);

        frame->clli.Header.BufferId = MFX_EXTBUFF_CONTENT_LIGHT_LEVEL_INFO;
        frame->clli.Header.BufferSz = sizeof(frame->clli);
        // The data in clli is valid when this flag is 1
        frame->clli.InsertPayloadToggle = 0;
        ff_qsv_frame_add_ext_param(avctx, frame, (mfxExtBuffer *)&frame->clli);
    }
#endif

    frame->used = 1;

    return 0;
}

static void qsv_clear_unused_frames(QSVContext *q)
{
    QSVFrame *cur = q->work_frames;
    while (cur) {
        if (cur->used && !cur->surface.Data.Locked && !cur->queued) {
            cur->used = 0;
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
        if (!frame->used) {
            ret = alloc_frame(avctx, q, frame);
            if (ret < 0)
                return ret;
            *surf = &frame->surface;
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

    ret = alloc_frame(avctx, q, frame);
    if (ret < 0)
        return ret;

    *surf = &frame->surface;

    return 0;
}

static QSVFrame *find_frame(QSVContext *q, mfxFrameSurface1 *surf)
{
    QSVFrame *cur = q->work_frames;
    while (cur) {
        if (surf == &cur->surface)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

#if QSV_VERSION_ATLEAST(1, 34)
static int qsv_export_film_grain(AVCodecContext *avctx, mfxExtAV1FilmGrainParam *ext_param, AVFrame *frame)
{
    AVFilmGrainParams *fgp;
    AVFilmGrainAOMParams *aom;
    int i;

    if (!(ext_param->FilmGrainFlags & MFX_FILM_GRAIN_APPLY))
        return 0;

    fgp = av_film_grain_params_create_side_data(frame);

    if (!fgp)
        return AVERROR(ENOMEM);

    fgp->type = AV_FILM_GRAIN_PARAMS_AV1;
    fgp->seed = ext_param->GrainSeed;
    aom = &fgp->codec.aom;

    aom->chroma_scaling_from_luma = !!(ext_param->FilmGrainFlags & MFX_FILM_GRAIN_CHROMA_SCALING_FROM_LUMA);
    aom->scaling_shift = ext_param->GrainScalingMinus8 + 8;
    aom->ar_coeff_lag = ext_param->ArCoeffLag;
    aom->ar_coeff_shift = ext_param->ArCoeffShiftMinus6 + 6;
    aom->grain_scale_shift = ext_param->GrainScaleShift;
    aom->overlap_flag = !!(ext_param->FilmGrainFlags & MFX_FILM_GRAIN_OVERLAP);
    aom->limit_output_range = !!(ext_param->FilmGrainFlags & MFX_FILM_GRAIN_CLIP_TO_RESTRICTED_RANGE);

    aom->num_y_points = ext_param->NumYPoints;

    for (i = 0; i < aom->num_y_points; i++) {
        aom->y_points[i][0] = ext_param->PointY[i].Value;
        aom->y_points[i][1] = ext_param->PointY[i].Scaling;
    }

    aom->num_uv_points[0] = ext_param->NumCbPoints;

    for (i = 0; i < aom->num_uv_points[0]; i++) {
        aom->uv_points[0][i][0] = ext_param->PointCb[i].Value;
        aom->uv_points[0][i][1] = ext_param->PointCb[i].Scaling;
    }

    aom->num_uv_points[1] = ext_param->NumCrPoints;

    for (i = 0; i < aom->num_uv_points[1]; i++) {
        aom->uv_points[1][i][0] = ext_param->PointCr[i].Value;
        aom->uv_points[1][i][1] = ext_param->PointCr[i].Scaling;
    }

    for (i = 0; i < 24; i++)
        aom->ar_coeffs_y[i] = ext_param->ArCoeffsYPlus128[i] - 128;

    for (i = 0; i < 25; i++) {
        aom->ar_coeffs_uv[0][i] = ext_param->ArCoeffsCbPlus128[i] - 128;
        aom->ar_coeffs_uv[1][i] = ext_param->ArCoeffsCrPlus128[i] - 128;
    }

    aom->uv_mult[0] = ext_param->CbMult;
    aom->uv_mult[1] = ext_param->CrMult;
    aom->uv_mult_luma[0] = ext_param->CbLumaMult;
    aom->uv_mult_luma[1] = ext_param->CrLumaMult;
    aom->uv_offset[0] = ext_param->CbOffset;
    aom->uv_offset[1] = ext_param->CrOffset;

    return 0;
}
#endif

#if QSV_VERSION_ATLEAST(1, 35)
static int qsv_export_hdr_side_data(AVCodecContext *avctx, mfxExtMasteringDisplayColourVolume *mdcv,
                                    mfxExtContentLightLevelInfo *clli, AVFrame *frame)
{
    int ret;

    // The SDK re-uses this flag for HDR SEI parsing
    if (mdcv->InsertPayloadToggle) {
        AVMasteringDisplayMetadata *mastering;
        const int mapping[3] = {2, 0, 1};
        const int chroma_den = 50000;
        const int luma_den = 10000;
        int i;

        ret = ff_decode_mastering_display_new(avctx, frame, &mastering);
        if (ret < 0)
            return ret;

        if (mastering) {
            for (i = 0; i < 3; i++) {
                const int j = mapping[i];
                mastering->display_primaries[i][0] = av_make_q(mdcv->DisplayPrimariesX[j], chroma_den);
                mastering->display_primaries[i][1] = av_make_q(mdcv->DisplayPrimariesY[j], chroma_den);
            }

            mastering->white_point[0] = av_make_q(mdcv->WhitePointX, chroma_den);
            mastering->white_point[1] = av_make_q(mdcv->WhitePointY, chroma_den);

            mastering->max_luminance = av_make_q(mdcv->MaxDisplayMasteringLuminance, luma_den);
            mastering->min_luminance = av_make_q(mdcv->MinDisplayMasteringLuminance, luma_den);

            mastering->has_luminance = 1;
            mastering->has_primaries = 1;
        }
    }

    // The SDK re-uses this flag for HDR SEI parsing
    if (clli->InsertPayloadToggle) {
        AVContentLightMetadata *light;

        ret = ff_decode_content_light_new(avctx, frame, &light);
        if (ret < 0)
            return ret;

        if (light) {
            light->MaxCLL  = clli->MaxContentLightLevel;
            light->MaxFALL = clli->MaxPicAverageLightLevel;
        }
    }

    return 0;
}

#endif

static int qsv_decode(AVCodecContext *avctx, QSVContext *q,
                      AVFrame *frame, int *got_frame,
                      const AVPacket *avpkt)
{
    mfxFrameSurface1 *insurf;
    mfxFrameSurface1 *outsurf;
    mfxSyncPoint *sync;
    mfxBitstream bs = { { { 0 } } };
    int ret;

    if (avpkt->size) {
        bs.Data       = avpkt->data;
        bs.DataLength = avpkt->size;
        bs.MaxLength  = bs.DataLength;
        bs.TimeStamp  = PTS_TO_MFX_PTS(avpkt->pts, avctx->pkt_timebase);
        if (avctx->field_order == AV_FIELD_PROGRESSIVE)
            bs.DataFlag   |= MFX_BITSTREAM_COMPLETE_FRAME;
    }

    sync = av_mallocz(sizeof(*sync));
    if (!sync) {
        av_freep(&sync);
        return AVERROR(ENOMEM);
    }

    do {
        ret = get_surface(avctx, q, &insurf);
        if (ret < 0) {
            av_freep(&sync);
            return ret;
        }

        ret = MFXVideoDECODE_DecodeFrameAsync(q->session, avpkt->size ? &bs : NULL,
                                              insurf, &outsurf, sync);
        if (ret == MFX_WRN_DEVICE_BUSY)
            av_usleep(500);

    } while (ret == MFX_WRN_DEVICE_BUSY || ret == MFX_ERR_MORE_SURFACE);

    if (ret == MFX_ERR_INCOMPATIBLE_VIDEO_PARAM) {
        q->reinit_flag = 1;
        av_log(avctx, AV_LOG_DEBUG, "Video parameter change\n");
        av_freep(&sync);
        return 0;
    }

    if (ret != MFX_ERR_NONE &&
        ret != MFX_ERR_MORE_DATA &&
        ret != MFX_WRN_VIDEO_PARAM_CHANGED &&
        ret != MFX_ERR_MORE_SURFACE) {
        av_freep(&sync);
        return ff_qsv_print_error(avctx, ret,
                                  "Error during QSV decoding.");
    }

    /* make sure we do not enter an infinite loop if the SDK
     * did not consume any data and did not return anything */
    if (!*sync && !bs.DataOffset) {
        bs.DataOffset = avpkt->size;
        ++q->zero_consume_run;
        if (q->zero_consume_run > 1 &&
            (avpkt->size ||
            ret != MFX_ERR_MORE_DATA))
            ff_qsv_print_warning(avctx, ret, "A decode call did not consume any data");
    } else {
        q->zero_consume_run = 0;
    }

    if (*sync) {
        QSVAsyncFrame aframe;
        QSVFrame *out_frame = find_frame(q, outsurf);

        if (!out_frame) {
            av_log(avctx, AV_LOG_ERROR,
                   "The returned surface does not correspond to any frame\n");
            av_freep(&sync);
            return AVERROR_BUG;
        }

        out_frame->queued += 1;

        aframe = (QSVAsyncFrame){ sync, out_frame };
        av_fifo_write(q->async_fifo, &aframe, 1);
    } else {
        av_freep(&sync);
    }

    if ((av_fifo_can_read(q->async_fifo) >= q->async_depth) ||
        (!avpkt->size && av_fifo_can_read(q->async_fifo))) {
        QSVAsyncFrame aframe;
        AVFrame *src_frame;

        av_fifo_read(q->async_fifo, &aframe, 1);
        aframe.frame->queued -= 1;

        if (avctx->pix_fmt != AV_PIX_FMT_QSV) {
            do {
                ret = MFXVideoCORE_SyncOperation(q->session, *aframe.sync, 1000);
            } while (ret == MFX_WRN_IN_EXECUTION);
        }

        av_freep(&aframe.sync);

        src_frame = aframe.frame->frame;

        ret = av_frame_ref(frame, src_frame);
        if (ret < 0)
            return ret;

        outsurf = &aframe.frame->surface;

        frame->pts = MFX_PTS_TO_PTS(outsurf->Data.TimeStamp, avctx->pkt_timebase);
#if QSV_VERSION_ATLEAST(1, 34)
        if ((avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN) &&
            QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 34) &&
            avctx->codec_id == AV_CODEC_ID_AV1) {
            ret = qsv_export_film_grain(avctx, &aframe.frame->av1_film_grain_param, frame);

            if (ret < 0)
                return ret;
        }
#endif

#if QSV_VERSION_ATLEAST(1, 35)
        if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 35) && avctx->codec_id == AV_CODEC_ID_HEVC) {
            ret = qsv_export_hdr_side_data(avctx, &aframe.frame->mdcv, &aframe.frame->clli, frame);

            if (ret < 0)
                return ret;
        }
#endif

        frame->repeat_pict =
            outsurf->Info.PicStruct & MFX_PICSTRUCT_FRAME_TRIPLING ? 4 :
            outsurf->Info.PicStruct & MFX_PICSTRUCT_FRAME_DOUBLING ? 2 :
            outsurf->Info.PicStruct & MFX_PICSTRUCT_FIELD_REPEATED ? 1 : 0;
        frame->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST *
            !!(outsurf->Info.PicStruct & MFX_PICSTRUCT_FIELD_TFF);
        frame->flags |= AV_FRAME_FLAG_INTERLACED *
            !(outsurf->Info.PicStruct & MFX_PICSTRUCT_PROGRESSIVE);
        frame->pict_type = ff_qsv_map_pictype(aframe.frame->dec_info.FrameType);
        //Key frame is IDR frame is only suitable for H264. For HEVC, IRAPs are key frames.
        if (avctx->codec_id == AV_CODEC_ID_H264) {
            if (aframe.frame->dec_info.FrameType & MFX_FRAMETYPE_IDR)
                frame->flags |= AV_FRAME_FLAG_KEY;
            else
                frame->flags &= ~AV_FRAME_FLAG_KEY;
        }

        /* update the surface properties */
        if (avctx->pix_fmt == AV_PIX_FMT_QSV)
            ((mfxFrameSurface1*)frame->data[3])->Info = outsurf->Info;

        *got_frame = 1;
    }

    return bs.DataOffset;
}

static void qsv_decode_close_qsvcontext(QSVContext *q)
{
    QSVFrame *cur = q->work_frames;

    if (q->session)
        MFXVideoDECODE_Close(q->session);

    if (q->async_fifo) {
        QSVAsyncFrame aframe;
        while (av_fifo_read(q->async_fifo, &aframe, 1) >= 0)
            av_freep(&aframe.sync);
        av_fifo_freep2(&q->async_fifo);
    }

    while (cur) {
        q->work_frames = cur->next;
        av_frame_free(&cur->frame);
        av_freep(&cur);
        cur = q->work_frames;
    }

    ff_qsv_close_internal_session(&q->internal_qs);

    av_buffer_unref(&q->frames_ctx.hw_frames_ctx);
    av_buffer_unref(&q->frames_ctx.mids_buf);
    av_buffer_pool_uninit(&q->pool);
}

static int qsv_process_data(AVCodecContext *avctx, QSVContext *q,
                            AVFrame *frame, int *got_frame, const AVPacket *pkt)
{
    int ret;
    mfxVideoParam param = { 0 };
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_NV12;

    if (!pkt->size)
        return qsv_decode(avctx, q, frame, got_frame, pkt);

    /* TODO: flush delayed frames on reinit */

    // sw_pix_fmt, coded_width/height should be set for ff_get_format(),
    // assume sw_pix_fmt is NV12 and coded_width/height to be 1280x720,
    // the assumption may be not corret but will be updated after header decoded if not true.
    if (q->orig_pix_fmt != AV_PIX_FMT_NONE)
        pix_fmt = q->orig_pix_fmt;
    if (!avctx->coded_width)
        avctx->coded_width = 1280;
    if (!avctx->coded_height)
        avctx->coded_height = 720;

    /* decode zero-size pkt to flush the buffered pkt before reinit */
    if (q->reinit_flag) {
        AVPacket zero_pkt = {0};
        ret = qsv_decode(avctx, q, frame, got_frame, &zero_pkt);
        if (ret < 0 || *got_frame)
            return ret;
    }

    if (q->reinit_flag || !q->session || !q->initialized) {
        mfxFrameAllocRequest request;
        memset(&request, 0, sizeof(request));

        q->reinit_flag = 0;
        ret = qsv_decode_header(avctx, q, pkt, pix_fmt, &param);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN))
                av_log(avctx, AV_LOG_VERBOSE, "More data is required to decode header\n");
            else
                av_log(avctx, AV_LOG_ERROR, "Error decoding header\n");
            goto reinit_fail;
        }
        param.IOPattern = q->iopattern;

        q->orig_pix_fmt = avctx->pix_fmt = pix_fmt = ff_qsv_map_fourcc(param.mfx.FrameInfo.FourCC);

        avctx->coded_width  = param.mfx.FrameInfo.Width;
        avctx->coded_height = param.mfx.FrameInfo.Height;

        ret = MFXVideoDECODE_QueryIOSurf(q->session, &param, &request);
        if (ret < 0)
            return ff_qsv_print_error(avctx, ret, "Error querying IO surface");

        q->suggest_pool_size = request.NumFrameSuggested;

        ret = qsv_decode_preinit(avctx, q, pix_fmt, &param);
        if (ret < 0)
            goto reinit_fail;
        q->initialized = 0;
    }

    if (!q->initialized) {
        ret = qsv_decode_init_context(avctx, q, &param);
        if (ret < 0)
            goto reinit_fail;
        q->initialized = 1;
    }

    return qsv_decode(avctx, q, frame, got_frame, pkt);

reinit_fail:
    q->orig_pix_fmt = avctx->pix_fmt = AV_PIX_FMT_NONE;
    return ret;
}

enum LoadPlugin {
    LOAD_PLUGIN_NONE,
    LOAD_PLUGIN_HEVC_SW,
    LOAD_PLUGIN_HEVC_HW,
};

typedef struct QSVDecContext {
    AVClass *class;
    QSVContext qsv;

    int load_plugin;

    AVFifo *packet_fifo;

    AVPacket buffer_pkt;
} QSVDecContext;

static void qsv_clear_buffers(QSVDecContext *s)
{
    AVPacket pkt;
    while (av_fifo_read(s->packet_fifo, &pkt, 1) >= 0)
        av_packet_unref(&pkt);

    av_packet_unref(&s->buffer_pkt);
}

static av_cold int qsv_decode_close(AVCodecContext *avctx)
{
    QSVDecContext *s = avctx->priv_data;

    qsv_decode_close_qsvcontext(&s->qsv);

    qsv_clear_buffers(s);

    av_fifo_freep2(&s->packet_fifo);

    return 0;
}

static av_cold int qsv_decode_init(AVCodecContext *avctx)
{
    QSVDecContext *s = avctx->priv_data;
    int ret;
    const char *uid = NULL;

    if (avctx->codec_id == AV_CODEC_ID_VP8) {
        uid = "f622394d8d87452f878c51f2fc9b4131";
    } else if (avctx->codec_id == AV_CODEC_ID_VP9) {
        uid = "a922394d8d87452f878c51f2fc9b4131";
    }
    else if (avctx->codec_id == AV_CODEC_ID_HEVC && s->load_plugin != LOAD_PLUGIN_NONE) {
        static const char * const uid_hevcdec_sw = "15dd936825ad475ea34e35f3f54217a6";
        static const char * const uid_hevcdec_hw = "33a61c0b4c27454ca8d85dde757c6f8e";

        if (s->qsv.load_plugins[0]) {
            av_log(avctx, AV_LOG_WARNING,
                   "load_plugins is not empty, but load_plugin is not set to 'none'."
                   "The load_plugin value will be ignored.\n");
        } else {
            if (s->load_plugin == LOAD_PLUGIN_HEVC_SW)
                uid = uid_hevcdec_sw;
            else
                uid = uid_hevcdec_hw;
        }
    }
    if (uid) {
        av_freep(&s->qsv.load_plugins);
        s->qsv.load_plugins = av_strdup(uid);
        if (!s->qsv.load_plugins)
            return AVERROR(ENOMEM);
    }

    s->qsv.orig_pix_fmt = AV_PIX_FMT_NV12;
    s->packet_fifo = av_fifo_alloc2(1, sizeof(AVPacket),
                                    AV_FIFO_FLAG_AUTO_GROW);
    if (!s->packet_fifo) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (!avctx->pkt_timebase.num)
        av_log(avctx, AV_LOG_WARNING, "Invalid pkt_timebase, passing timestamps as-is.\n");

    return 0;
fail:
    qsv_decode_close(avctx);
    return ret;
}

static int qsv_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    QSVDecContext *s = avctx->priv_data;
    int ret;

    /* buffer the input packet */
    if (avpkt->size) {
        AVPacket input_ref;

        ret = av_packet_ref(&input_ref, avpkt);
        if (ret < 0)
            return ret;
        av_fifo_write(s->packet_fifo, &input_ref, 1);
    }

    /* process buffered data */
    while (!*got_frame) {
        /* prepare the input data */
        if (s->buffer_pkt.size <= 0) {
            /* no more data */
            if (!av_fifo_can_read(s->packet_fifo))
                return avpkt->size ? avpkt->size : qsv_process_data(avctx, &s->qsv, frame, got_frame, avpkt);
            /* in progress of reinit, no read from fifo and keep the buffer_pkt */
            if (!s->qsv.reinit_flag) {
                av_packet_unref(&s->buffer_pkt);
                av_fifo_read(s->packet_fifo, &s->buffer_pkt, 1);
            }
        }

        ret = qsv_process_data(avctx, &s->qsv, frame, got_frame, &s->buffer_pkt);
        if (ret < 0){
            if (ret == AVERROR(EAGAIN))
                ret = 0;

            /* Drop buffer_pkt when failed to decode the packet. Otherwise,
               the decoder will keep decoding the failure packet. */
            av_packet_unref(&s->buffer_pkt);
            return ret;
        }
        if (s->qsv.reinit_flag)
            continue;

        s->buffer_pkt.size -= ret;
        s->buffer_pkt.data += ret;
    }

    return avpkt->size;
}

static void qsv_decode_flush(AVCodecContext *avctx)
{
    QSVDecContext *s = avctx->priv_data;

    qsv_clear_buffers(s);

    s->qsv.orig_pix_fmt = AV_PIX_FMT_NONE;
    s->qsv.initialized = 0;
}

#define OFFSET(x) offsetof(QSVDecContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

#define DEFINE_QSV_DECODER_WITH_OPTION(x, X, bsf_name, opt) \
static const AVClass x##_qsv_class = { \
    .class_name = #x "_qsv", \
    .item_name  = av_default_item_name, \
    .option     = opt, \
    .version    = LIBAVUTIL_VERSION_INT, \
}; \
const FFCodec ff_##x##_qsv_decoder = { \
    .p.name         = #x "_qsv", \
    CODEC_LONG_NAME(#X " video (Intel Quick Sync Video acceleration)"), \
    .priv_data_size = sizeof(QSVDecContext), \
    .p.type         = AVMEDIA_TYPE_VIDEO, \
    .p.id           = AV_CODEC_ID_##X, \
    .init           = qsv_decode_init, \
    FF_CODEC_DECODE_CB(qsv_decode_frame), \
    .flush          = qsv_decode_flush, \
    .close          = qsv_decode_close, \
    .bsfs           = bsf_name, \
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_DR1 | AV_CODEC_CAP_AVOID_PROBING | AV_CODEC_CAP_HYBRID, \
    .p.priv_class   = &x##_qsv_class, \
    .hw_configs     = qsv_hw_configs, \
    .p.wrapper_name = "qsv", \
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE, \
}; \

#define DEFINE_QSV_DECODER(x, X, bsf_name) DEFINE_QSV_DECODER_WITH_OPTION(x, X, bsf_name, options)

#if CONFIG_HEVC_QSV_DECODER
static const AVOption hevc_options[] = {
    { "async_depth", "Internal parallelization depth, the higher the value the higher the latency.", OFFSET(qsv.async_depth), AV_OPT_TYPE_INT, { .i64 = ASYNC_DEPTH_DEFAULT }, 1, INT_MAX, VD },

    { "load_plugin", "A user plugin to load in an internal session", OFFSET(load_plugin), AV_OPT_TYPE_INT, { .i64 = LOAD_PLUGIN_HEVC_HW }, LOAD_PLUGIN_NONE, LOAD_PLUGIN_HEVC_HW, VD, .unit = "load_plugin" },
    { "none",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = LOAD_PLUGIN_NONE },    0, 0, VD, .unit = "load_plugin" },
    { "hevc_sw",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = LOAD_PLUGIN_HEVC_SW }, 0, 0, VD, .unit = "load_plugin" },
    { "hevc_hw",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = LOAD_PLUGIN_HEVC_HW }, 0, 0, VD, .unit = "load_plugin" },

    { "load_plugins", "A :-separate list of hexadecimal plugin UIDs to load in an internal session",
        OFFSET(qsv.load_plugins), AV_OPT_TYPE_STRING, { .str = "" }, 0, 0, VD },

    { "gpu_copy", "A GPU-accelerated copy between video and system memory", OFFSET(qsv.gpu_copy), AV_OPT_TYPE_INT, { .i64 = MFX_GPUCOPY_DEFAULT }, MFX_GPUCOPY_DEFAULT, MFX_GPUCOPY_OFF, VD, .unit = "gpu_copy"},
        { "default", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_GPUCOPY_DEFAULT }, 0, 0, VD, .unit = "gpu_copy"},
        { "on",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_GPUCOPY_ON },      0, 0, VD, .unit = "gpu_copy"},
        { "off",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_GPUCOPY_OFF },     0, 0, VD, .unit = "gpu_copy"},
    { NULL },
};
DEFINE_QSV_DECODER_WITH_OPTION(hevc, HEVC, "hevc_mp4toannexb", hevc_options)
#endif

static const AVOption options[] = {
    { "async_depth", "Internal parallelization depth, the higher the value the higher the latency.", OFFSET(qsv.async_depth), AV_OPT_TYPE_INT, { .i64 = ASYNC_DEPTH_DEFAULT }, 1, INT_MAX, VD },

    { "gpu_copy", "A GPU-accelerated copy between video and system memory", OFFSET(qsv.gpu_copy), AV_OPT_TYPE_INT, { .i64 = MFX_GPUCOPY_DEFAULT }, MFX_GPUCOPY_DEFAULT, MFX_GPUCOPY_OFF, VD, .unit = "gpu_copy"},
        { "default", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_GPUCOPY_DEFAULT }, 0, 0, VD, .unit = "gpu_copy"},
        { "on",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_GPUCOPY_ON },      0, 0, VD, .unit = "gpu_copy"},
        { "off",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_GPUCOPY_OFF },     0, 0, VD, .unit = "gpu_copy"},
    { NULL },
};

#if CONFIG_H264_QSV_DECODER
DEFINE_QSV_DECODER(h264, H264, "h264_mp4toannexb")
#endif

#if CONFIG_MPEG2_QSV_DECODER
DEFINE_QSV_DECODER(mpeg2, MPEG2VIDEO, NULL)
#endif

#if CONFIG_VC1_QSV_DECODER
DEFINE_QSV_DECODER(vc1, VC1, NULL)
#endif

#if CONFIG_MJPEG_QSV_DECODER
DEFINE_QSV_DECODER(mjpeg, MJPEG, NULL)
#endif

#if CONFIG_VP8_QSV_DECODER
DEFINE_QSV_DECODER(vp8, VP8, NULL)
#endif

#if CONFIG_VP9_QSV_DECODER
DEFINE_QSV_DECODER(vp9, VP9, NULL)
#endif

#if CONFIG_AV1_QSV_DECODER
DEFINE_QSV_DECODER(av1, AV1, NULL)
#endif
