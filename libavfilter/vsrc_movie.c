/*
 * Copyright (c) 2010 Stefano Sabatini
 * Copyright (c) 2008 Victor Paesa
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

/**
 * @file
 * movie video source
 *
 * @todo use direct rendering (no allocation of a new frame)
 * @todo support a PTS correction mechanism
 * @todo support more than one output stream
 */

#include <float.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavformat/avformat.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    int64_t seek_point;   ///< seekpoint in microseconds
    double seek_point_d;
    char *format_name;
    char *file_name;
    int stream_index;

    AVFormatContext *format_ctx;
    AVCodecContext *codec_ctx;
    int is_done;
    AVFrame *frame;   ///< video frame to store the decoded images in

    int w, h;
} MovieContext;

#define OFFSET(x) offsetof(MovieContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM

static const AVOption movie_options[]= {
    { "filename",     NULL,                      OFFSET(file_name),    AV_OPT_TYPE_STRING,                                    .flags = FLAGS },
    { "format_name",  "set format name",         OFFSET(format_name),  AV_OPT_TYPE_STRING,                                    .flags = FLAGS },
    { "f",            "set format name",         OFFSET(format_name),  AV_OPT_TYPE_STRING,                                    .flags = FLAGS },
    { "stream_index", "set stream index",        OFFSET(stream_index), AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX,                 FLAGS  },
    { "si",           "set stream index",        OFFSET(stream_index), AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX,                 FLAGS  },
    { "seek_point",   "set seekpoint (seconds)", OFFSET(seek_point_d), AV_OPT_TYPE_DOUBLE, { .dbl =  0 },  0, (INT64_MAX-1) / 1000000, FLAGS },
    { "sp",           "set seekpoint (seconds)", OFFSET(seek_point_d), AV_OPT_TYPE_DOUBLE, { .dbl =  0 },  0, (INT64_MAX-1) / 1000000, FLAGS },
    { NULL },
};

static const char *movie_get_name(void *ctx)
{
    return "movie";
}

static const AVClass movie_class = {
    "MovieContext",
    movie_get_name,
    movie_options
};

static av_cold int movie_init(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;
    AVInputFormat *iformat = NULL;
    AVCodec *codec;
    int ret;
    int64_t timestamp;

    av_register_all();

    // Try to find the movie format (container)
    iformat = movie->format_name ? av_find_input_format(movie->format_name) : NULL;

    movie->format_ctx = NULL;
    if ((ret = avformat_open_input(&movie->format_ctx, movie->file_name, iformat, NULL)) < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Failed to avformat_open_input '%s'\n", movie->file_name);
        return ret;
    }
    if ((ret = avformat_find_stream_info(movie->format_ctx, NULL)) < 0)
        av_log(ctx, AV_LOG_WARNING, "Failed to find stream info\n");

    // if seeking requested, we execute it
    if (movie->seek_point > 0) {
        timestamp = movie->seek_point;
        // add the stream start time, should it exist
        if (movie->format_ctx->start_time != AV_NOPTS_VALUE) {
            if (timestamp > INT64_MAX - movie->format_ctx->start_time) {
                av_log(ctx, AV_LOG_ERROR,
                       "%s: seek value overflow with start_time:%"PRId64" seek_point:%"PRId64"\n",
                       movie->file_name, movie->format_ctx->start_time, movie->seek_point);
                return AVERROR(EINVAL);
            }
            timestamp += movie->format_ctx->start_time;
        }
        if ((ret = av_seek_frame(movie->format_ctx, -1, timestamp, AVSEEK_FLAG_BACKWARD)) < 0) {
            av_log(ctx, AV_LOG_ERROR, "%s: could not seek to position %"PRId64"\n",
                   movie->file_name, timestamp);
            return ret;
        }
    }

    /* select the video stream */
    if ((ret = av_find_best_stream(movie->format_ctx, AVMEDIA_TYPE_VIDEO,
                                   movie->stream_index, -1, NULL, 0)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "No video stream with index '%d' found\n",
               movie->stream_index);
        return ret;
    }
    movie->stream_index = ret;
    movie->codec_ctx = movie->format_ctx->streams[movie->stream_index]->codec;

    /*
     * So now we've got a pointer to the so-called codec context for our video
     * stream, but we still have to find the actual codec and open it.
     */
    codec = avcodec_find_decoder(movie->codec_ctx->codec_id);
    if (!codec) {
        av_log(ctx, AV_LOG_ERROR, "Failed to find any codec\n");
        return AVERROR(EINVAL);
    }

    movie->codec_ctx->refcounted_frames = 1;

    if ((ret = avcodec_open2(movie->codec_ctx, codec, NULL)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to open codec\n");
        return ret;
    }

    movie->w = movie->codec_ctx->width;
    movie->h = movie->codec_ctx->height;

    av_log(ctx, AV_LOG_VERBOSE, "seek_point:%"PRIi64" format_name:%s file_name:%s stream_index:%d\n",
           movie->seek_point, movie->format_name, movie->file_name,
           movie->stream_index);

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;

    movie->seek_point = movie->seek_point_d * 1000000 + 0.5;

    return movie_init(ctx);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;

    if (movie->codec_ctx)
        avcodec_close(movie->codec_ctx);
    if (movie->format_ctx)
        avformat_close_input(&movie->format_ctx);
    av_frame_free(&movie->frame);
}

static int query_formats(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;
    enum AVPixelFormat pix_fmts[] = { movie->codec_ctx->pix_fmt, AV_PIX_FMT_NONE };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_output_props(AVFilterLink *outlink)
{
    MovieContext *movie = outlink->src->priv;

    outlink->w = movie->w;
    outlink->h = movie->h;
    outlink->time_base = movie->format_ctx->streams[movie->stream_index]->time_base;

    return 0;
}

static int movie_get_frame(AVFilterLink *outlink)
{
    MovieContext *movie = outlink->src->priv;
    AVPacket pkt;
    int ret, frame_decoded;
    AVStream av_unused *st = movie->format_ctx->streams[movie->stream_index];

    if (movie->is_done == 1)
        return 0;

    movie->frame = av_frame_alloc();
    if (!movie->frame)
        return AVERROR(ENOMEM);

    while ((ret = av_read_frame(movie->format_ctx, &pkt)) >= 0) {
        // Is this a packet from the video stream?
        if (pkt.stream_index == movie->stream_index) {
            avcodec_decode_video2(movie->codec_ctx, movie->frame, &frame_decoded, &pkt);

            if (frame_decoded) {
                if (movie->frame->pkt_pts != AV_NOPTS_VALUE)
                    movie->frame->pts = movie->frame->pkt_pts;
                av_dlog(outlink->src,
                        "movie_get_frame(): file:'%s' pts:%"PRId64" time:%f aspect:%d/%d\n",
                        movie->file_name, movie->frame->pts,
                        (double)movie->frame->pts * av_q2d(st->time_base),
                        movie->frame->sample_aspect_ratio.num,
                        movie->frame->sample_aspect_ratio.den);
                // We got it. Free the packet since we are returning
                av_free_packet(&pkt);

                return 0;
            }
        }
        // Free the packet that was allocated by av_read_frame
        av_free_packet(&pkt);
    }

    // On multi-frame source we should stop the mixing process when
    // the movie source does not have more frames
    if (ret == AVERROR_EOF)
        movie->is_done = 1;
    return ret;
}

static int request_frame(AVFilterLink *outlink)
{
    MovieContext *movie = outlink->src->priv;
    int ret;

    if (movie->is_done)
        return AVERROR_EOF;
    if ((ret = movie_get_frame(outlink)) < 0)
        return ret;

    ret = ff_filter_frame(outlink, movie->frame);
    movie->frame = NULL;

    return ret;
}

static const AVFilterPad avfilter_vsrc_movie_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_output_props,
    },
    { NULL }
};

AVFilter ff_vsrc_movie = {
    .name          = "movie",
    .description   = NULL_IF_CONFIG_SMALL("Read from a movie source."),
    .priv_size     = sizeof(MovieContext),
    .priv_class    = &movie_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = NULL,
    .outputs   = avfilter_vsrc_movie_outputs,
};
