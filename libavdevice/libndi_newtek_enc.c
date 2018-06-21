/*
 * NewTek NDI output
 * Copyright (c) 2017 Maksym Veremeyenko
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

#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"

#include "libndi_newtek_common.h"

struct NDIContext {
    const AVClass *cclass;

    /* Options */
    int reference_level;
    int clock_video, clock_audio;

    NDIlib_video_frame_t *video;
    NDIlib_audio_frame_interleaved_16s_t *audio;
    NDIlib_send_instance_t ndi_send;
    AVFrame *last_avframe;
};

static int ndi_write_trailer(AVFormatContext *avctx)
{
    struct NDIContext *ctx = avctx->priv_data;

    if (ctx->ndi_send) {
        NDIlib_send_destroy(ctx->ndi_send);
        av_frame_free(&ctx->last_avframe);
    }

    av_freep(&ctx->video);
    av_freep(&ctx->audio);

    return 0;
}

static int ndi_write_video_packet(AVFormatContext *avctx, AVStream *st, AVPacket *pkt)
{
    struct NDIContext *ctx = avctx->priv_data;
    AVFrame *avframe, *tmp = (AVFrame *)pkt->data;

    if (tmp->format != AV_PIX_FMT_UYVY422 && tmp->format != AV_PIX_FMT_BGRA &&
        tmp->format != AV_PIX_FMT_BGR0 && tmp->format != AV_PIX_FMT_RGBA &&
        tmp->format != AV_PIX_FMT_RGB0) {
        av_log(avctx, AV_LOG_ERROR, "Got a frame with invalid pixel format.\n");
        return AVERROR(EINVAL);
    }

    if (tmp->linesize[0] < 0) {
        av_log(avctx, AV_LOG_ERROR, "Got a frame with negative linesize.\n");
        return AVERROR(EINVAL);
    }

    if (tmp->width  != ctx->video->xres ||
        tmp->height != ctx->video->yres) {
        av_log(avctx, AV_LOG_ERROR, "Got a frame with invalid dimension.\n");
        av_log(avctx, AV_LOG_ERROR, "tmp->width=%d, tmp->height=%d, ctx->video->xres=%d, ctx->video->yres=%d\n",
            tmp->width, tmp->height, ctx->video->xres, ctx->video->yres);
        return AVERROR(EINVAL);
    }

    avframe = av_frame_clone(tmp);
    if (!avframe)
        return AVERROR(ENOMEM);

    ctx->video->timecode = av_rescale_q(pkt->pts, st->time_base, NDI_TIME_BASE_Q);

    ctx->video->line_stride_in_bytes = avframe->linesize[0];
    ctx->video->p_data = (void *)(avframe->data[0]);

    av_log(avctx, AV_LOG_DEBUG, "%s: pkt->pts=%"PRId64", timecode=%"PRId64", st->time_base=%d/%d\n",
        __func__, pkt->pts, ctx->video->timecode, st->time_base.num, st->time_base.den);

    /* asynchronous for one frame, but will block if a second frame
        is given before the first one has been sent */
    NDIlib_send_send_video_async(ctx->ndi_send, ctx->video);

    av_frame_free(&ctx->last_avframe);
    ctx->last_avframe = avframe;

    return 0;
}

static int ndi_write_audio_packet(AVFormatContext *avctx, AVStream *st, AVPacket *pkt)
{
    struct NDIContext *ctx = avctx->priv_data;

    ctx->audio->p_data = (short *)pkt->data;
    ctx->audio->timecode = av_rescale_q(pkt->pts, st->time_base, NDI_TIME_BASE_Q);
    ctx->audio->no_samples = pkt->size / (ctx->audio->no_channels << 1);

    av_log(avctx, AV_LOG_DEBUG, "%s: pkt->pts=%"PRId64", timecode=%"PRId64", st->time_base=%d/%d\n",
        __func__, pkt->pts, ctx->audio->timecode, st->time_base.num, st->time_base.den);

    NDIlib_util_send_send_audio_interleaved_16s(ctx->ndi_send, ctx->audio);

    return 0;
}

static int ndi_write_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    AVStream *st = avctx->streams[pkt->stream_index];

    if      (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        return ndi_write_video_packet(avctx, st, pkt);
    else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        return ndi_write_audio_packet(avctx, st, pkt);

    return AVERROR_BUG;
}

static int ndi_setup_audio(AVFormatContext *avctx, AVStream *st)
{
    struct NDIContext *ctx = avctx->priv_data;
    AVCodecParameters *c = st->codecpar;

    if (ctx->audio) {
        av_log(avctx, AV_LOG_ERROR, "Only one audio stream is supported!\n");
        return AVERROR(EINVAL);
    }

    ctx->audio = av_mallocz(sizeof(NDIlib_audio_frame_interleaved_16s_t));
    if (!ctx->audio)
        return AVERROR(ENOMEM);

    ctx->audio->sample_rate = c->sample_rate;
    ctx->audio->no_channels = c->channels;
    ctx->audio->reference_level = ctx->reference_level;

    avpriv_set_pts_info(st, 64, 1, NDI_TIME_BASE);

    return 0;
}

static int ndi_setup_video(AVFormatContext *avctx, AVStream *st)
{
    struct NDIContext *ctx = avctx->priv_data;
    AVCodecParameters *c = st->codecpar;

    if (ctx->video) {
        av_log(avctx, AV_LOG_ERROR, "Only one video stream is supported!\n");
        return AVERROR(EINVAL);
    }

    if (c->codec_id != AV_CODEC_ID_WRAPPED_AVFRAME) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported codec format!"
               " Only AV_CODEC_ID_WRAPPED_AVFRAME is supported (-vcodec wrapped_avframe).\n");
        return AVERROR(EINVAL);
    }

    if (c->format != AV_PIX_FMT_UYVY422 && c->format != AV_PIX_FMT_BGRA &&
        c->format != AV_PIX_FMT_BGR0 && c->format != AV_PIX_FMT_RGBA &&
        c->format != AV_PIX_FMT_RGB0) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format!"
               " Only AV_PIX_FMT_UYVY422, AV_PIX_FMT_BGRA, AV_PIX_FMT_BGR0,"
               " AV_PIX_FMT_RGBA, AV_PIX_FMT_RGB0 is supported.\n");
        return AVERROR(EINVAL);
    }

    if (c->field_order == AV_FIELD_BB || c->field_order == AV_FIELD_BT) {
        av_log(avctx, AV_LOG_ERROR, "Lower field-first disallowed");
        return AVERROR(EINVAL);
    }

    ctx->video = av_mallocz(sizeof(NDIlib_video_frame_t));
    if (!ctx->video)
        return AVERROR(ENOMEM);

    switch(c->format) {
        case AV_PIX_FMT_UYVY422:
            ctx->video->FourCC = NDIlib_FourCC_type_UYVY;
            break;
        case AV_PIX_FMT_BGRA:
            ctx->video->FourCC = NDIlib_FourCC_type_BGRA;
            break;
        case AV_PIX_FMT_BGR0:
            ctx->video->FourCC = NDIlib_FourCC_type_BGRX;
            break;
        case AV_PIX_FMT_RGBA:
            ctx->video->FourCC = NDIlib_FourCC_type_RGBA;
            break;
        case AV_PIX_FMT_RGB0:
            ctx->video->FourCC = NDIlib_FourCC_type_RGBX;
            break;
    }

    ctx->video->xres = c->width;
    ctx->video->yres = c->height;
    ctx->video->frame_rate_N = st->avg_frame_rate.num;
    ctx->video->frame_rate_D = st->avg_frame_rate.den;
    ctx->video->frame_format_type = c->field_order == AV_FIELD_PROGRESSIVE
        ? NDIlib_frame_format_type_progressive
        : NDIlib_frame_format_type_interleaved;

    if (st->sample_aspect_ratio.num) {
        AVRational display_aspect_ratio;
        av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                  st->codecpar->width  * (int64_t)st->sample_aspect_ratio.num,
                  st->codecpar->height * (int64_t)st->sample_aspect_ratio.den,
                  1024 * 1024);
        ctx->video->picture_aspect_ratio = av_q2d(display_aspect_ratio);
    }
    else
        ctx->video->picture_aspect_ratio = (double)st->codecpar->width/st->codecpar->height;

    avpriv_set_pts_info(st, 64, 1, NDI_TIME_BASE);

    return 0;
}

static int ndi_write_header(AVFormatContext *avctx)
{
    int ret = 0;
    unsigned int n;
    struct NDIContext *ctx = avctx->priv_data;
    const NDIlib_send_create_t ndi_send_desc = { .p_ndi_name = avctx->url,
        .p_groups = NULL, .clock_video = ctx->clock_video, .clock_audio = ctx->clock_audio };

    if (!NDIlib_initialize()) {
        av_log(avctx, AV_LOG_ERROR, "NDIlib_initialize failed.\n");
        return AVERROR_EXTERNAL;
    }

    /* check if streams compatible */
    for (n = 0; n < avctx->nb_streams; n++) {
        AVStream *st = avctx->streams[n];
        AVCodecParameters *c = st->codecpar;
        if        (c->codec_type == AVMEDIA_TYPE_AUDIO) {
            if ((ret = ndi_setup_audio(avctx, st)))
                goto error;
        } else if (c->codec_type == AVMEDIA_TYPE_VIDEO) {
            if ((ret = ndi_setup_video(avctx, st)))
                goto error;
        } else {
            av_log(avctx, AV_LOG_ERROR, "Unsupported stream type.\n");
            ret = AVERROR(EINVAL);
            goto error;
        }
    }

    ctx->ndi_send = NDIlib_send_create(&ndi_send_desc);
    if (!ctx->ndi_send) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create NDI output %s\n", avctx->url);
        ret = AVERROR_EXTERNAL;
    }

error:
    return ret;
}

#define OFFSET(x) offsetof(struct NDIContext, x)
static const AVOption options[] = {
    { "reference_level", "The audio reference level in dB"  , OFFSET(reference_level), AV_OPT_TYPE_INT, { .i64 = 0 }, -20, 20, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM},
    { "clock_video", "These specify whether video 'clock' themselves"  , OFFSET(clock_video), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM },
    { "clock_audio", "These specify whether audio 'clock' themselves"  , OFFSET(clock_audio), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM },
    { NULL },
};

static const AVClass libndi_newtek_muxer_class = {
    .class_name = "NDI muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

AVOutputFormat ff_libndi_newtek_muxer = {
    .name           = "libndi_newtek",
    .long_name      = NULL_IF_CONFIG_SMALL("Network Device Interface (NDI) output using NewTek library"),
    .audio_codec    = AV_CODEC_ID_PCM_S16LE,
    .video_codec    = AV_CODEC_ID_WRAPPED_AVFRAME,
    .subtitle_codec = AV_CODEC_ID_NONE,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &libndi_newtek_muxer_class,
    .priv_data_size = sizeof(struct NDIContext),
    .write_header   = ndi_write_header,
    .write_packet   = ndi_write_packet,
    .write_trailer  = ndi_write_trailer,
};
