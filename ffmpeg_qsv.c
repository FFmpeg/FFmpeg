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

#include <mfx/mfxvideo.h>
#include <stdlib.h>

#include "libavutil/dict.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavcodec/qsv.h"

#include "ffmpeg.h"

char *qsv_device = NULL;

static int qsv_get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream *ist = s->opaque;

    return av_hwframe_get_buffer(ist->hw_frames_ctx, frame, 0);
}

static void qsv_uninit(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    av_buffer_unref(&ist->hw_frames_ctx);
}

static int qsv_device_init(InputStream *ist)
{
    int err;
    AVDictionary *dict = NULL;

    if (qsv_device) {
        err = av_dict_set(&dict, "child_device", qsv_device, 0);
        if (err < 0)
            return err;
    }

    err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_QSV,
                                 ist->hwaccel_device, dict, 0);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error creating a QSV device\n");
        goto err_out;
    }

err_out:
    if (dict)
        av_dict_free(&dict);

    return err;
}

int qsv_init(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    AVHWFramesContext *frames_ctx;
    AVQSVFramesContext *frames_hwctx;
    int ret;

    if (!hw_device_ctx) {
        ret = qsv_device_init(ist);
        if (ret < 0)
            return ret;
    }

    av_buffer_unref(&ist->hw_frames_ctx);
    ist->hw_frames_ctx = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!ist->hw_frames_ctx)
        return AVERROR(ENOMEM);

    frames_ctx   = (AVHWFramesContext*)ist->hw_frames_ctx->data;
    frames_hwctx = frames_ctx->hwctx;

    frames_ctx->width             = FFALIGN(s->coded_width,  32);
    frames_ctx->height            = FFALIGN(s->coded_height, 32);
    frames_ctx->format            = AV_PIX_FMT_QSV;
    frames_ctx->sw_format         = s->sw_pix_fmt;
    frames_ctx->initial_pool_size = 64;
    frames_hwctx->frame_type      = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;

    ret = av_hwframe_ctx_init(ist->hw_frames_ctx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error initializing a QSV frame pool\n");
        return ret;
    }

    ist->hwaccel_get_buffer = qsv_get_buffer;
    ist->hwaccel_uninit     = qsv_uninit;

    return 0;
}

int qsv_transcode_init(OutputStream *ost)
{
    InputStream *ist;
    const enum AVPixelFormat *pix_fmt;

    int err, i;
    AVBufferRef *encode_frames_ref = NULL;
    AVHWFramesContext *encode_frames;
    AVQSVFramesContext *qsv_frames;

    /* check if the encoder supports QSV */
    if (!ost->enc->pix_fmts)
        return 0;
    for (pix_fmt = ost->enc->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; pix_fmt++)
        if (*pix_fmt == AV_PIX_FMT_QSV)
            break;
    if (*pix_fmt == AV_PIX_FMT_NONE)
        return 0;

    if (strcmp(ost->avfilter, "null") || ost->source_index < 0)
        return 0;

    /* check if the decoder supports QSV and the output only goes to this stream */
    ist = input_streams[ost->source_index];
    if (ist->hwaccel_id != HWACCEL_QSV || !ist->dec || !ist->dec->pix_fmts)
        return 0;
    for (pix_fmt = ist->dec->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; pix_fmt++)
        if (*pix_fmt == AV_PIX_FMT_QSV)
            break;
    if (*pix_fmt == AV_PIX_FMT_NONE)
        return 0;

    for (i = 0; i < nb_output_streams; i++)
        if (output_streams[i] != ost &&
            output_streams[i]->source_index == ost->source_index)
            return 0;

    av_log(NULL, AV_LOG_VERBOSE, "Setting up QSV transcoding\n");

    if (!hw_device_ctx) {
        err = qsv_device_init(ist);
        if (err < 0)
            goto fail;
    }

    // This creates a dummy hw_frames_ctx for the encoder to be
    // suitably initialised.  It only contains one real frame, so
    // hopefully doesn't waste too much memory.

    encode_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!encode_frames_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    encode_frames = (AVHWFramesContext*)encode_frames_ref->data;
    qsv_frames = encode_frames->hwctx;

    encode_frames->width     = FFALIGN(ist->resample_width,  32);
    encode_frames->height    = FFALIGN(ist->resample_height, 32);
    encode_frames->format    = AV_PIX_FMT_QSV;
    encode_frames->sw_format = AV_PIX_FMT_NV12;
    encode_frames->initial_pool_size = 1;

    qsv_frames->frame_type = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;

    err = av_hwframe_ctx_init(encode_frames_ref);
    if (err < 0)
        goto fail;

    ist->dec_ctx->pix_fmt       = AV_PIX_FMT_QSV;
    ist->resample_pix_fmt       = AV_PIX_FMT_QSV;

    ost->enc_ctx->pix_fmt       = AV_PIX_FMT_QSV;
    ost->enc_ctx->hw_frames_ctx = encode_frames_ref;

    return 0;

fail:
    av_buffer_unref(&encode_frames_ref);
    return err;
}
