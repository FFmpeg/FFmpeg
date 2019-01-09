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
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/mem.h"
#include "libavutil/log.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/time.h"

#include "avcodec.h"
#include "internal.h"
#include "qsv.h"
#include "qsv_internal.h"
#include "qsvdec.h"

const AVCodecHWConfigInternal *ff_qsv_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public = {
            .pix_fmt     = AV_PIX_FMT_QSV,
            .methods     = AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX |
                           AV_CODEC_HW_CONFIG_METHOD_AD_HOC,
            .device_type = AV_HWDEVICE_TYPE_QSV,
        },
        .hwaccel = NULL,
    },
    NULL
};

static int qsv_init_session(AVCodecContext *avctx, QSVContext *q, mfxSession session,
                            AVBufferRef *hw_frames_ref, AVBufferRef *hw_device_ref)
{
    int ret;

    if (session) {
        q->session = session;
    } else if (hw_frames_ref) {
        if (q->internal_session) {
            MFXClose(q->internal_session);
            q->internal_session = NULL;
        }
        av_buffer_unref(&q->frames_ctx.hw_frames_ctx);

        q->frames_ctx.hw_frames_ctx = av_buffer_ref(hw_frames_ref);
        if (!q->frames_ctx.hw_frames_ctx)
            return AVERROR(ENOMEM);

        ret = ff_qsv_init_session_frames(avctx, &q->internal_session,
                                         &q->frames_ctx, q->load_plugins,
                                         q->iopattern == MFX_IOPATTERN_OUT_OPAQUE_MEMORY);
        if (ret < 0) {
            av_buffer_unref(&q->frames_ctx.hw_frames_ctx);
            return ret;
        }

        q->session = q->internal_session;
    } else if (hw_device_ref) {
        if (q->internal_session) {
            MFXClose(q->internal_session);
            q->internal_session = NULL;
        }

        ret = ff_qsv_init_session_device(avctx, &q->internal_session,
                                         hw_device_ref, q->load_plugins);
        if (ret < 0)
            return ret;

        q->session = q->internal_session;
    } else {
        if (!q->internal_session) {
            ret = ff_qsv_init_internal_session(avctx, &q->internal_session,
                                               q->load_plugins);
            if (ret < 0)
                return ret;
        }

        q->session = q->internal_session;
    }

    /* make sure the decoder is uninitialized */
    MFXVideoDECODE_Close(q->session);

    return 0;
}

static inline unsigned int qsv_fifo_item_size(void)
{
    return sizeof(mfxSyncPoint*) + sizeof(QSVFrame*);
}

static inline unsigned int qsv_fifo_size(const AVFifoBuffer* fifo)
{
    return av_fifo_size(fifo) / qsv_fifo_item_size();
}

static int qsv_decode_init(AVCodecContext *avctx, QSVContext *q)
{
    const AVPixFmtDescriptor *desc;
    mfxSession session = NULL;
    int iopattern = 0;
    mfxVideoParam param = { 0 };
    int frame_width  = avctx->coded_width;
    int frame_height = avctx->coded_height;
    int ret;

    desc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    if (!desc)
        return AVERROR_BUG;

    if (!q->async_fifo) {
        q->async_fifo = av_fifo_alloc(q->async_depth * qsv_fifo_item_size());
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

    if (avctx->hw_frames_ctx) {
        AVHWFramesContext    *frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        AVQSVFramesContext *frames_hwctx = frames_ctx->hwctx;

        if (!iopattern) {
            if (frames_hwctx->frame_type & MFX_MEMTYPE_OPAQUE_FRAME)
                iopattern = MFX_IOPATTERN_OUT_OPAQUE_MEMORY;
            else if (frames_hwctx->frame_type & MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET)
                iopattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
        }
    }

    if (!iopattern)
        iopattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
    q->iopattern = iopattern;

    ret = qsv_init_session(avctx, q, session, avctx->hw_frames_ctx, avctx->hw_device_ctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing an MFX session\n");
        return ret;
    }

    ret = ff_qsv_codec_id_to_mfx(avctx->codec_id);
    if (ret < 0)
        return ret;

    param.mfx.CodecId      = ret;
    param.mfx.CodecProfile = ff_qsv_profile_to_mfx(avctx->codec_id, avctx->profile);
    param.mfx.CodecLevel   = avctx->level == FF_LEVEL_UNKNOWN ? MFX_LEVEL_UNKNOWN : avctx->level;

    param.mfx.FrameInfo.BitDepthLuma   = desc->comp[0].depth;
    param.mfx.FrameInfo.BitDepthChroma = desc->comp[0].depth;
    param.mfx.FrameInfo.Shift          = desc->comp[0].depth > 8;
    param.mfx.FrameInfo.FourCC         = q->fourcc;
    param.mfx.FrameInfo.Width          = frame_width;
    param.mfx.FrameInfo.Height         = frame_height;
    param.mfx.FrameInfo.ChromaFormat   = MFX_CHROMAFORMAT_YUV420;

    switch (avctx->field_order) {
    case AV_FIELD_PROGRESSIVE:
        param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
        break;
    case AV_FIELD_TT:
        param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_FIELD_TFF;
        break;
    case AV_FIELD_BB:
        param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_FIELD_BFF;
        break;
    default:
        param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_UNKNOWN;
        break;
    }

    param.IOPattern   = q->iopattern;
    param.AsyncDepth  = q->async_depth;
    param.ExtParam    = q->ext_buffers;
    param.NumExtParam = q->nb_ext_buffers;

    ret = MFXVideoDECODE_Init(q->session, &param);
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret,
                                  "Error initializing the MFX video decoder");

    q->frame_info = param.mfx.FrameInfo;

    return 0;
}

static int alloc_frame(AVCodecContext *avctx, QSVContext *q, QSVFrame *frame)
{
    int ret;

    ret = ff_get_buffer(avctx, frame->frame, AV_GET_BUFFER_FLAG_REF);
    if (ret < 0)
        return ret;

    if (frame->frame->format == AV_PIX_FMT_QSV) {
        frame->surface = *(mfxFrameSurface1*)frame->frame->data[3];
    } else {
        frame->surface.Info = q->frame_info;

        frame->surface.Data.PitchLow = frame->frame->linesize[0];
        frame->surface.Data.Y        = frame->frame->data[0];
        frame->surface.Data.UV       = frame->frame->data[1];
    }

    if (q->frames_ctx.mids) {
        ret = ff_qsv_find_surface_idx(&q->frames_ctx, frame);
        if (ret < 0)
            return ret;

        frame->surface.Data.MemId = &q->frames_ctx.mids[ret];
    }
    frame->surface.Data.ExtParam    = &frame->ext_param;
    frame->surface.Data.NumExtParam = 1;
    frame->ext_param                = (mfxExtBuffer*)&frame->dec_info;
    frame->dec_info.Header.BufferId = MFX_EXTBUFF_DECODED_FRAME_INFO;
    frame->dec_info.Header.BufferSz = sizeof(frame->dec_info);

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

static int qsv_decode(AVCodecContext *avctx, QSVContext *q,
                      AVFrame *frame, int *got_frame,
                      AVPacket *avpkt)
{
    QSVFrame *out_frame;
    mfxFrameSurface1 *insurf;
    mfxFrameSurface1 *outsurf;
    mfxSyncPoint *sync;
    mfxBitstream bs = { { { 0 } } };
    int ret;

    if (avpkt->size) {
        bs.Data       = avpkt->data;
        bs.DataLength = avpkt->size;
        bs.MaxLength  = bs.DataLength;
        bs.TimeStamp  = avpkt->pts;
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
        if (q->zero_consume_run > 1)
            ff_qsv_print_warning(avctx, ret, "A decode call did not consume any data");
    } else if (!*sync && bs.DataOffset) {
        ++q->buffered_count;
    } else {
        q->zero_consume_run = 0;
    }

    if (*sync) {
        QSVFrame *out_frame = find_frame(q, outsurf);

        if (!out_frame) {
            av_log(avctx, AV_LOG_ERROR,
                   "The returned surface does not correspond to any frame\n");
            av_freep(&sync);
            return AVERROR_BUG;
        }

        out_frame->queued = 1;
        av_fifo_generic_write(q->async_fifo, &out_frame, sizeof(out_frame), NULL);
        av_fifo_generic_write(q->async_fifo, &sync,      sizeof(sync),      NULL);
    } else {
        av_freep(&sync);
    }

    if ((qsv_fifo_size(q->async_fifo) >= q->async_depth) ||
        (!avpkt->size && av_fifo_size(q->async_fifo))) {
        AVFrame *src_frame;

        av_fifo_generic_read(q->async_fifo, &out_frame, sizeof(out_frame), NULL);
        av_fifo_generic_read(q->async_fifo, &sync,      sizeof(sync),      NULL);
        out_frame->queued = 0;

        if (avctx->pix_fmt != AV_PIX_FMT_QSV) {
            do {
                ret = MFXVideoCORE_SyncOperation(q->session, *sync, 1000);
            } while (ret == MFX_WRN_IN_EXECUTION);
        }

        av_freep(&sync);

        src_frame = out_frame->frame;

        ret = av_frame_ref(frame, src_frame);
        if (ret < 0)
            return ret;

        outsurf = &out_frame->surface;

#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
        frame->pkt_pts = outsurf->Data.TimeStamp;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        frame->pts = outsurf->Data.TimeStamp;

        frame->repeat_pict =
            outsurf->Info.PicStruct & MFX_PICSTRUCT_FRAME_TRIPLING ? 4 :
            outsurf->Info.PicStruct & MFX_PICSTRUCT_FRAME_DOUBLING ? 2 :
            outsurf->Info.PicStruct & MFX_PICSTRUCT_FIELD_REPEATED ? 1 : 0;
        frame->top_field_first =
            outsurf->Info.PicStruct & MFX_PICSTRUCT_FIELD_TFF;
        frame->interlaced_frame =
            !(outsurf->Info.PicStruct & MFX_PICSTRUCT_PROGRESSIVE);
        frame->pict_type = ff_qsv_map_pictype(out_frame->dec_info.FrameType);
        //Key frame is IDR frame is only suitable for H264. For HEVC, IRAPs are key frames.
        if (avctx->codec_id == AV_CODEC_ID_H264)
            frame->key_frame = !!(out_frame->dec_info.FrameType & MFX_FRAMETYPE_IDR);

        /* update the surface properties */
        if (avctx->pix_fmt == AV_PIX_FMT_QSV)
            ((mfxFrameSurface1*)frame->data[3])->Info = outsurf->Info;

        *got_frame = 1;
    }

    return bs.DataOffset;
}

int ff_qsv_decode_close(QSVContext *q)
{
    QSVFrame *cur = q->work_frames;

    if (q->session)
        MFXVideoDECODE_Close(q->session);

    while (q->async_fifo && av_fifo_size(q->async_fifo)) {
        QSVFrame *out_frame;
        mfxSyncPoint *sync;

        av_fifo_generic_read(q->async_fifo, &out_frame, sizeof(out_frame), NULL);
        av_fifo_generic_read(q->async_fifo, &sync,      sizeof(sync),      NULL);

        av_freep(&sync);
    }

    while (cur) {
        q->work_frames = cur->next;
        av_frame_free(&cur->frame);
        av_freep(&cur);
        cur = q->work_frames;
    }

    av_fifo_free(q->async_fifo);
    q->async_fifo = NULL;

    av_parser_close(q->parser);
    avcodec_free_context(&q->avctx_internal);

    if (q->internal_session)
        MFXClose(q->internal_session);

    av_buffer_unref(&q->frames_ctx.hw_frames_ctx);
    av_buffer_unref(&q->frames_ctx.mids_buf);

    return 0;
}

int ff_qsv_process_data(AVCodecContext *avctx, QSVContext *q,
                        AVFrame *frame, int *got_frame, AVPacket *pkt)
{
    uint8_t *dummy_data;
    int dummy_size;
    int ret;
    const AVPixFmtDescriptor *desc;

    if (!q->avctx_internal) {
        q->avctx_internal = avcodec_alloc_context3(NULL);
        if (!q->avctx_internal)
            return AVERROR(ENOMEM);

        q->avctx_internal->codec_id = avctx->codec_id;

        q->parser = av_parser_init(avctx->codec_id);
        if (!q->parser)
            return AVERROR(ENOMEM);

        q->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
        q->orig_pix_fmt   = AV_PIX_FMT_NONE;
    }

    if (!pkt->size)
        return qsv_decode(avctx, q, frame, got_frame, pkt);

    /* we assume the packets are already split properly and want
     * just the codec parameters here */
    av_parser_parse2(q->parser, q->avctx_internal,
                     &dummy_data, &dummy_size,
                     pkt->data, pkt->size, pkt->pts, pkt->dts,
                     pkt->pos);

    avctx->field_order  = q->parser->field_order;
    /* TODO: flush delayed frames on reinit */
    if (q->parser->format       != q->orig_pix_fmt    ||
        FFALIGN(q->parser->coded_width, 16)  != FFALIGN(avctx->coded_width, 16) ||
        FFALIGN(q->parser->coded_height, 16) != FFALIGN(avctx->coded_height, 16)) {
        enum AVPixelFormat pix_fmts[3] = { AV_PIX_FMT_QSV,
                                           AV_PIX_FMT_NONE,
                                           AV_PIX_FMT_NONE };
        enum AVPixelFormat qsv_format;
        AVPacket zero_pkt = {0};

        if (q->buffered_count) {
            q->reinit_flag = 1;
            /* decode zero-size pkt to flush the buffered pkt before reinit */
            q->buffered_count--;
            return qsv_decode(avctx, q, frame, got_frame, &zero_pkt);
        }

        q->reinit_flag = 0;

        qsv_format = ff_qsv_map_pixfmt(q->parser->format, &q->fourcc);
        if (qsv_format < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Decoding pixel format '%s' is not supported\n",
                   av_get_pix_fmt_name(q->parser->format));
            ret = AVERROR(ENOSYS);
            goto reinit_fail;
        }

        q->orig_pix_fmt     = q->parser->format;
        avctx->pix_fmt      = pix_fmts[1] = qsv_format;
        avctx->width        = q->parser->width;
        avctx->height       = q->parser->height;
        avctx->coded_width  = FFALIGN(q->parser->coded_width, 16);
        avctx->coded_height = FFALIGN(q->parser->coded_height, 16);
        avctx->level        = q->avctx_internal->level;
        avctx->profile      = q->avctx_internal->profile;

        ret = ff_get_format(avctx, pix_fmts);
        if (ret < 0)
            goto reinit_fail;

        avctx->pix_fmt = ret;

        desc = av_pix_fmt_desc_get(avctx->pix_fmt);
        if (!desc)
            goto reinit_fail;

         if (desc->comp[0].depth > 8) {
            avctx->coded_width =  FFALIGN(q->parser->coded_width, 32);
            avctx->coded_height = FFALIGN(q->parser->coded_height, 32);
        }

        ret = qsv_decode_init(avctx, q);
        if (ret < 0)
            goto reinit_fail;
    }

    return qsv_decode(avctx, q, frame, got_frame, pkt);

reinit_fail:
    q->orig_pix_fmt = q->parser->format = avctx->pix_fmt = AV_PIX_FMT_NONE;
    return ret;
}

void ff_qsv_decode_flush(AVCodecContext *avctx, QSVContext *q)
{
    q->orig_pix_fmt = AV_PIX_FMT_NONE;
}
