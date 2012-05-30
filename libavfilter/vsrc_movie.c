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

/* #define DEBUG */

#include <float.h>
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavformat/avformat.h"
#include "avfilter.h"
#include "formats.h"
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
    AVFilterBufferRef *picref;
} MovieContext;

#define OFFSET(x) offsetof(MovieContext, x)

static const AVOption movie_options[]= {
{"format_name",  "set format name",         OFFSET(format_name),  AV_OPT_TYPE_STRING, {.str =  0},  CHAR_MIN, CHAR_MAX },
{"f",            "set format name",         OFFSET(format_name),  AV_OPT_TYPE_STRING, {.str =  0},  CHAR_MIN, CHAR_MAX },
{"stream_index", "set stream index",        OFFSET(stream_index), AV_OPT_TYPE_INT,    {.dbl = -1},  -1,       INT_MAX  },
{"si",           "set stream index",        OFFSET(stream_index), AV_OPT_TYPE_INT,    {.dbl = -1},  -1,       INT_MAX  },
{"seek_point",   "set seekpoint (seconds)", OFFSET(seek_point_d), AV_OPT_TYPE_DOUBLE, {.dbl =  0},  0,        (INT64_MAX-1) / 1000000 },
{"sp",           "set seekpoint (seconds)", OFFSET(seek_point_d), AV_OPT_TYPE_DOUBLE, {.dbl =  0},  0,        (INT64_MAX-1) / 1000000 },
{NULL},
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

static int movie_init(AVFilterContext *ctx)
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

    if ((ret = avcodec_open2(movie->codec_ctx, codec, NULL)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to open codec\n");
        return ret;
    }

    if (!(movie->frame = avcodec_alloc_frame()) ) {
        av_log(ctx, AV_LOG_ERROR, "Failed to alloc frame\n");
        return AVERROR(ENOMEM);
    }

    movie->w = movie->codec_ctx->width;
    movie->h = movie->codec_ctx->height;

    av_log(ctx, AV_LOG_INFO, "seek_point:%"PRIi64" format_name:%s file_name:%s stream_index:%d\n",
           movie->seek_point, movie->format_name, movie->file_name,
           movie->stream_index);

    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    MovieContext *movie = ctx->priv;
    int ret;
    movie->class = &movie_class;
    av_opt_set_defaults(movie);

    if (args)
        movie->file_name = av_get_token(&args, ":");
    if (!movie->file_name || !*movie->file_name) {
        av_log(ctx, AV_LOG_ERROR, "No filename provided!\n");
        return AVERROR(EINVAL);
    }

    if (*args++ == ':' && (ret = av_set_options_string(movie, args, "=", ":")) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
        return ret;
    }

    movie->seek_point = movie->seek_point_d * 1000000 + 0.5;

    return movie_init(ctx);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;

    av_free(movie->file_name);
    av_free(movie->format_name);
    if (movie->codec_ctx)
        avcodec_close(movie->codec_ctx);
    if (movie->format_ctx)
        avformat_close_input(&movie->format_ctx);
    avfilter_unref_buffer(movie->picref);
    av_freep(&movie->frame);
}

static int query_formats(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;
    enum PixelFormat pix_fmts[] = { movie->codec_ctx->pix_fmt, PIX_FMT_NONE };

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
    AVStream *st = movie->format_ctx->streams[movie->stream_index];

    if (movie->is_done == 1)
        return 0;

    while ((ret = av_read_frame(movie->format_ctx, &pkt)) >= 0) {
        // Is this a packet from the video stream?
        if (pkt.stream_index == movie->stream_index) {
            movie->codec_ctx->reordered_opaque = pkt.pos;
            avcodec_decode_video2(movie->codec_ctx, movie->frame, &frame_decoded, &pkt);

            if (frame_decoded) {
                /* FIXME: avoid the memcpy */
                movie->picref = avfilter_get_video_buffer(outlink, AV_PERM_WRITE | AV_PERM_PRESERVE |
                                                          AV_PERM_REUSE2, outlink->w, outlink->h);
                av_image_copy(movie->picref->data, movie->picref->linesize,
                              movie->frame->data,  movie->frame->linesize,
                              movie->picref->format, outlink->w, outlink->h);
                avfilter_copy_frame_props(movie->picref, movie->frame);

                /* FIXME: use a PTS correction mechanism as that in
                 * ffplay.c when some API will be available for that */
                /* use pkt_dts if pkt_pts is not available */
                movie->picref->pts = movie->frame->pkt_pts == AV_NOPTS_VALUE ?
                    movie->frame->pkt_dts : movie->frame->pkt_pts;

                movie->picref->pos                    = movie->frame->reordered_opaque;
                if (!movie->frame->sample_aspect_ratio.num)
                    movie->picref->video->pixel_aspect = st->sample_aspect_ratio;
                av_dlog(outlink->src,
                        "movie_get_frame(): file:'%s' pts:%"PRId64" time:%lf pos:%"PRId64" aspect:%d/%d\n",
                        movie->file_name, movie->picref->pts,
                        (double)movie->picref->pts * av_q2d(st->time_base),
                        movie->picref->pos,
                        movie->picref->video->pixel_aspect.num, movie->picref->video->pixel_aspect.den);
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
    AVFilterBufferRef *outpicref;
    MovieContext *movie = outlink->src->priv;
    int ret;

    if (movie->is_done)
        return AVERROR_EOF;
    if ((ret = movie_get_frame(outlink)) < 0)
        return ret;

    outpicref = avfilter_ref_buffer(movie->picref, ~0);
    ff_start_frame(outlink, outpicref);
    ff_draw_slice(outlink, 0, outlink->h, 1);
    ff_end_frame(outlink);
    avfilter_unref_buffer(movie->picref);
    movie->picref = NULL;

    return 0;
}

AVFilter avfilter_vsrc_movie = {
    .name          = "movie",
    .description   = NULL_IF_CONFIG_SMALL("Read from a movie source."),
    .priv_size     = sizeof(MovieContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = request_frame,
                                    .config_props    = config_output_props, },
                                  { .name = NULL}},
};
