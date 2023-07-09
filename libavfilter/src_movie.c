/*
 * Copyright (c) 2010 Stefano Sabatini
 * Copyright (c) 2008 Victor Paesa
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

/**
 * @file
 * movie video source
 *
 * @todo support a PTS correction mechanism
 */

#include "config_components.h"

#include <float.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/timestamp.h"

#include "libavcodec/avcodec.h"

#include "libavformat/avformat.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct MovieStream {
    AVFilterLink *link;
    AVStream *st;
    AVCodecContext *codec_ctx;
    int64_t discontinuity_threshold;
    int64_t last_pts;
    AVFrame *frame;
    int eof;
} MovieStream;

typedef struct MovieContext {
    /* common A/V fields */
    const AVClass *class;
    int64_t seek_point;   ///< seekpoint in microseconds
    double seek_point_d;
    char *format_name;
    char *file_name;
    char *stream_specs; /**< user-provided list of streams, separated by + */
    int stream_index; /**< for compatibility */
    int loop_count;
    int64_t discontinuity_threshold;
    int64_t ts_offset;
    int dec_threads;

    AVPacket *pkt;
    AVFormatContext *format_ctx;

    int eof;
    int max_stream_index; /**< max stream # actually used for output */
    MovieStream *st; /**< array of all streams, one per output */
    int *out_index; /**< stream number -> output number map, or -1 */
    AVDictionary *format_opts;
} MovieContext;

#define OFFSET(x) offsetof(MovieContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_VIDEO_PARAM

static const AVOption movie_options[]= {
    { "filename",     NULL,                      OFFSET(file_name),    AV_OPT_TYPE_STRING,                                    .flags = FLAGS },
    { "format_name",  "set format name",         OFFSET(format_name),  AV_OPT_TYPE_STRING,                                    .flags = FLAGS },
    { "f",            "set format name",         OFFSET(format_name),  AV_OPT_TYPE_STRING,                                    .flags = FLAGS },
    { "stream_index", "set stream index",        OFFSET(stream_index), AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX,                 FLAGS  },
    { "si",           "set stream index",        OFFSET(stream_index), AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX,                 FLAGS  },
    { "seek_point",   "set seekpoint (seconds)", OFFSET(seek_point_d), AV_OPT_TYPE_DOUBLE, { .dbl =  0 },  0, (INT64_MAX-1) / 1000000, FLAGS },
    { "sp",           "set seekpoint (seconds)", OFFSET(seek_point_d), AV_OPT_TYPE_DOUBLE, { .dbl =  0 },  0, (INT64_MAX-1) / 1000000, FLAGS },
    { "streams",      "set streams",             OFFSET(stream_specs), AV_OPT_TYPE_STRING, {.str =  0},  0, 0, FLAGS },
    { "s",            "set streams",             OFFSET(stream_specs), AV_OPT_TYPE_STRING, {.str =  0},  0, 0, FLAGS },
    { "loop",         "set loop count",          OFFSET(loop_count),   AV_OPT_TYPE_INT,    {.i64 =  1},  0,        INT_MAX, FLAGS },
    { "discontinuity", "set discontinuity threshold", OFFSET(discontinuity_threshold), AV_OPT_TYPE_DURATION, {.i64 = 0}, 0, INT64_MAX, FLAGS },
    { "dec_threads",  "set the number of threads for decoding", OFFSET(dec_threads), AV_OPT_TYPE_INT, {.i64 =  0}, 0, INT_MAX, FLAGS },
    { "format_opts",  "set format options for the opened file", OFFSET(format_opts), AV_OPT_TYPE_DICT, {.str = NULL}, 0, 0, FLAGS},
    { NULL },
};

static int movie_config_output_props(AVFilterLink *outlink);

static AVStream *find_stream(void *log, AVFormatContext *avf, const char *spec)
{
    int i, ret, already = 0, stream_id = -1;
    char type_char[2], dummy;
    AVStream *found = NULL;
    enum AVMediaType type;

    ret = sscanf(spec, "d%1[av]%d%c", type_char, &stream_id, &dummy);
    if (ret >= 1 && ret <= 2) {
        type = type_char[0] == 'v' ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO;
        ret = av_find_best_stream(avf, type, stream_id, -1, NULL, 0);
        if (ret < 0) {
            av_log(log, AV_LOG_ERROR, "No %s stream with index '%d' found\n",
                   av_get_media_type_string(type), stream_id);
            return NULL;
        }
        return avf->streams[ret];
    }
    for (i = 0; i < avf->nb_streams; i++) {
        ret = avformat_match_stream_specifier(avf, avf->streams[i], spec);
        if (ret < 0) {
            av_log(log, AV_LOG_ERROR,
                   "Invalid stream specifier \"%s\"\n", spec);
            return NULL;
        }
        if (!ret)
            continue;
        if (avf->streams[i]->discard != AVDISCARD_ALL) {
            already++;
            continue;
        }
        if (found) {
            av_log(log, AV_LOG_WARNING,
                   "Ambiguous stream specifier \"%s\", using #%d\n", spec, i);
            break;
        }
        found = avf->streams[i];
    }
    if (!found) {
        av_log(log, AV_LOG_WARNING, "Stream specifier \"%s\" %s\n", spec,
               already ? "matched only already used streams" :
                         "did not match any stream");
        return NULL;
    }
    if (found->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
        found->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        av_log(log, AV_LOG_ERROR, "Stream specifier \"%s\" matched a %s stream,"
               "currently unsupported by libavfilter\n", spec,
               av_get_media_type_string(found->codecpar->codec_type));
        return NULL;
    }
    return found;
}

static int get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    int linesize_align[AV_NUM_DATA_POINTERS];
    MovieStream *st = avctx->opaque;
    AVFilterLink *outlink = st->link;
    int w, h, ow, oh, copy = 0;
    AVFrame *new;

    h = oh = frame->height;
    w = ow = frame->width;

    copy = frame->format != outlink->format;
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (w != outlink->w || h != outlink->h)
            copy |= 1;
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (outlink->sample_rate != frame->sample_rate ||
            av_channel_layout_compare(&outlink->ch_layout, &frame->ch_layout))
            copy |= 1;
        break;
    }

    if (copy || !(avctx->codec->capabilities & AV_CODEC_CAP_DR1))
        return avcodec_default_get_buffer2(avctx, frame, flags);

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        avcodec_align_dimensions2(avctx, &w, &h, linesize_align);
        new = ff_default_get_video_buffer(outlink, w, h);
        break;
    case AVMEDIA_TYPE_AUDIO:
        new = ff_default_get_audio_buffer(outlink, frame->nb_samples);
        break;
    default:
        return -1;
    }

    av_frame_copy_props(new, frame);
    av_frame_unref(frame);
    av_frame_move_ref(frame, new);
    av_frame_free(&new);

    frame->width  = ow;
    frame->height = oh;

    return 0;
}

static int open_stream(AVFilterContext *ctx, MovieStream *st, int dec_threads)
{
    const AVCodec *codec;
    int ret;

    codec = avcodec_find_decoder(st->st->codecpar->codec_id);
    if (!codec) {
        av_log(ctx, AV_LOG_ERROR, "Failed to find any codec\n");
        return AVERROR(EINVAL);
    }

    st->codec_ctx = avcodec_alloc_context3(codec);
    if (!st->codec_ctx)
        return AVERROR(ENOMEM);

    st->codec_ctx->opaque = st;
    st->codec_ctx->get_buffer2 = get_buffer;
    ret = avcodec_parameters_to_context(st->codec_ctx, st->st->codecpar);
    if (ret < 0)
        return ret;

    if (!dec_threads)
        dec_threads = ff_filter_get_nb_threads(ctx);
    st->codec_ctx->thread_count = dec_threads;

    if ((ret = avcodec_open2(st->codec_ctx, codec, NULL)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to open codec\n");
        return ret;
    }

    return 0;
}

static int guess_channel_layout(MovieStream *st, int st_index, void *log_ctx)
{
    AVCodecParameters *dec_par = st->st->codecpar;
    char buf[256];
    AVChannelLayout chl = { 0 };

    av_channel_layout_default(&chl, dec_par->ch_layout.nb_channels);

    if (!KNOWN(&chl)) {
        av_log(log_ctx, AV_LOG_WARNING,
               "Channel layout is not set in stream %d, and could not "
               "be guessed from the number of channels (%d)\n",
               st_index, dec_par->ch_layout.nb_channels);
        return av_channel_layout_copy(&dec_par->ch_layout, &chl);
    }

    av_channel_layout_describe(&chl, buf, sizeof(buf));
    av_log(log_ctx, AV_LOG_WARNING,
           "Channel layout is not set in output stream %d, "
           "guessed channel layout is '%s'\n",
           st_index, buf);
    return av_channel_layout_copy(&dec_par->ch_layout, &chl);
}

static av_cold int movie_common_init(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;
    const AVInputFormat *iformat = NULL;
    int64_t timestamp;
    int nb_streams = 1, ret, i;
    char default_streams[16], *stream_specs, *spec, *cursor;
    AVStream *st;

    if (!movie->file_name) {
        av_log(ctx, AV_LOG_ERROR, "No filename provided!\n");
        return AVERROR(EINVAL);
    }

    movie->seek_point = movie->seek_point_d * 1000000 + 0.5;

    stream_specs = movie->stream_specs;
    if (!stream_specs) {
        snprintf(default_streams, sizeof(default_streams), "d%c%d",
                 !strcmp(ctx->filter->name, "amovie") ? 'a' : 'v',
                 movie->stream_index);
        stream_specs = default_streams;
    }
    for (cursor = stream_specs; *cursor; cursor++)
        if (*cursor == '+')
            nb_streams++;

    if (movie->loop_count != 1 && nb_streams != 1) {
        av_log(ctx, AV_LOG_ERROR,
               "Loop with several streams is currently unsupported\n");
        return AVERROR_PATCHWELCOME;
    }

    // Try to find the movie format (container)
    iformat = movie->format_name ? av_find_input_format(movie->format_name) : NULL;

    movie->format_ctx = NULL;
    if ((ret = avformat_open_input(&movie->format_ctx, movie->file_name, iformat, &movie->format_opts)) < 0) {
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
            if (timestamp > 0 && movie->format_ctx->start_time > INT64_MAX - timestamp) {
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

    for (i = 0; i < movie->format_ctx->nb_streams; i++)
        movie->format_ctx->streams[i]->discard = AVDISCARD_ALL;

    movie->pkt = av_packet_alloc();
    if (!movie->pkt)
        return AVERROR(ENOMEM);
    movie->st = av_calloc(nb_streams, sizeof(*movie->st));
    if (!movie->st)
        return AVERROR(ENOMEM);

    for (i = 0; i < nb_streams; i++) {
        spec = av_strtok(stream_specs, "+", &cursor);
        if (!spec)
            return AVERROR_BUG;
        stream_specs = NULL; /* for next strtok */
        st = find_stream(ctx, movie->format_ctx, spec);
        if (!st)
            return AVERROR(EINVAL);
        st->discard = AVDISCARD_DEFAULT;
        movie->st[i].st = st;
        movie->max_stream_index = FFMAX(movie->max_stream_index, st->index);
        movie->st[i].discontinuity_threshold =
            av_rescale_q(movie->discontinuity_threshold, AV_TIME_BASE_Q, st->time_base);

        movie->st[i].frame = av_frame_alloc();
        if (!movie->st[i].frame)
            return AVERROR(ENOMEM);
    }
    if (av_strtok(NULL, "+", &cursor))
        return AVERROR_BUG;

    movie->out_index = av_calloc(movie->max_stream_index + 1,
                                 sizeof(*movie->out_index));
    if (!movie->out_index)
        return AVERROR(ENOMEM);
    for (i = 0; i <= movie->max_stream_index; i++)
        movie->out_index[i] = -1;
    for (i = 0; i < nb_streams; i++) {
        AVFilterPad pad = { 0 };
        movie->out_index[movie->st[i].st->index] = i;
        pad.type          = movie->st[i].st->codecpar->codec_type;
        pad.name          = av_asprintf("out%d", i);
        if (!pad.name)
            return AVERROR(ENOMEM);
        pad.config_props  = movie_config_output_props;
        if ((ret = ff_append_outpad_free_name(ctx, &pad)) < 0)
            return ret;
        if ( movie->st[i].st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
            !KNOWN(&movie->st[i].st->codecpar->ch_layout)) {
            ret = guess_channel_layout(&movie->st[i], i, ctx);
            if (ret < 0)
                return ret;
        }
        ret = open_stream(ctx, &movie->st[i], movie->dec_threads);
        if (ret < 0)
            return ret;
    }

    av_log(ctx, AV_LOG_VERBOSE, "seek_point:%"PRIi64" format_name:%s file_name:%s stream_index:%d\n",
           movie->seek_point, movie->format_name, movie->file_name,
           movie->stream_index);

    return 0;
}

static av_cold void movie_uninit(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;
    int i;

    for (i = 0; i < ctx->nb_outputs; i++) {
        if (movie->st[i].st)
            avcodec_free_context(&movie->st[i].codec_ctx);
        av_frame_free(&movie->st[i].frame);
    }
    av_packet_free(&movie->pkt);
    av_freep(&movie->st);
    av_freep(&movie->out_index);
    if (movie->format_ctx)
        avformat_close_input(&movie->format_ctx);
}

static int movie_query_formats(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;
    int list[] = { 0, -1 };
    AVChannelLayout list64[] = { { 0 }, { 0 } };
    int i, ret;

    for (i = 0; i < ctx->nb_outputs; i++) {
        MovieStream *st = &movie->st[i];
        AVCodecParameters *c = st->st->codecpar;
        AVFilterLink *outlink = ctx->outputs[i];

        switch (c->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            list[0] = c->format;
            if ((ret = ff_formats_ref(ff_make_format_list(list), &outlink->incfg.formats)) < 0)
                return ret;
            break;
        case AVMEDIA_TYPE_AUDIO:
            list[0] = c->format;
            if ((ret = ff_formats_ref(ff_make_format_list(list), &outlink->incfg.formats)) < 0)
                return ret;
            list[0] = c->sample_rate;
            if ((ret = ff_formats_ref(ff_make_format_list(list), &outlink->incfg.samplerates)) < 0)
                return ret;
            list64[0] = c->ch_layout;
            if ((ret = ff_channel_layouts_ref(ff_make_channel_layout_list(list64),
                                   &outlink->incfg.channel_layouts)) < 0)
                return ret;
            break;
        }
    }

    return 0;
}

static int movie_config_output_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MovieContext *movie  = ctx->priv;
    unsigned out_id = FF_OUTLINK_IDX(outlink);
    MovieStream *st = &movie->st[out_id];
    AVCodecParameters *c = st->st->codecpar;

    outlink->time_base = st->st->time_base;

    switch (c->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        outlink->w          = c->width;
        outlink->h          = c->height;
        outlink->frame_rate = st->st->r_frame_rate;
        break;
    case AVMEDIA_TYPE_AUDIO:
        break;
    }

    st->link = outlink;

    return 0;
}

static int rewind_file(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;
    int64_t timestamp = movie->seek_point;
    int ret, i;

    if (movie->format_ctx->start_time != AV_NOPTS_VALUE)
        timestamp += movie->format_ctx->start_time;
    ret = av_seek_frame(movie->format_ctx, -1, timestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Unable to loop: %s\n", av_err2str(ret));
        movie->loop_count = 1; /* do not try again */
        return ret;
    }

    for (i = 0; i < ctx->nb_outputs; i++) {
        avcodec_flush_buffers(movie->st[i].codec_ctx);
    }
    return 0;
}

static int flush_decoder(AVFilterContext *ctx, int i)
{
    MovieContext *movie = ctx->priv;
    AVCodecContext *dec = movie->st[i].codec_ctx;

    return avcodec_send_packet(dec, NULL);
}

static int decode_packet(AVFilterContext *ctx, int i)
{
    AVFilterLink *outlink = ctx->outputs[i];
    MovieContext *movie = ctx->priv;
    MovieStream *st = &movie->st[i];
    AVCodecContext *dec = movie->st[i].codec_ctx;
    AVFrame *frame = movie->st[i].frame;
    AVPacket *pkt = movie->pkt;
    int ret = 0;

    // submit the packet to the decoder
    if (!movie->eof) {
        ret = avcodec_send_packet(dec, pkt);
        if (ret < 0)
            return ret;
    }

    // get all the available frames from the decoder
    if (ret >= 0) {
        ret = avcodec_receive_frame(dec, frame);
        if (ret < 0) {
            // those two return values are special and mean there is no output
            // frame available, but there were no errors during decoding
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return 0;
            return ret;
        }

        frame->pts = frame->best_effort_timestamp;
        if (frame->pts != AV_NOPTS_VALUE) {
            if (movie->ts_offset)
                frame->pts += av_rescale_q_rnd(movie->ts_offset, AV_TIME_BASE_Q, outlink->time_base, AV_ROUND_UP);
            if (st->discontinuity_threshold) {
                if (st->last_pts != AV_NOPTS_VALUE) {
                    int64_t diff = frame->pts - st->last_pts;
                    if (diff < 0 || diff > st->discontinuity_threshold) {
                        av_log(ctx, AV_LOG_VERBOSE, "Discontinuity in stream:%d diff:%"PRId64"\n", i, diff);
                        movie->ts_offset += av_rescale_q_rnd(-diff, outlink->time_base, AV_TIME_BASE_Q, AV_ROUND_UP);
                        frame->pts -= diff;
                    }
                }
            }
            st->last_pts = frame->pts;
        }
        ret = ff_filter_frame(outlink, av_frame_clone(frame));
        if (ret < 0)
            return ret;
        if (ret == 0)
            return 1;
    }

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;
    int wanted = 0, ret;

    for (int i = 0; i < ctx->nb_outputs; i++) {
        if (ff_outlink_frame_wanted(ctx->outputs[i]))
            wanted++;
    }

    if (wanted == 0)
        return FFERROR_NOT_READY;

    if (!movie->eof) {
        ret = av_read_frame(movie->format_ctx, movie->pkt);
        if (ret < 0) {
            movie->eof = 1;
            for (int i = 0; i < ctx->nb_outputs; i++)
                flush_decoder(ctx, i);
            ff_filter_set_ready(ctx, 100);
            return 0;
        } else {
            int pkt_out_id = movie->pkt->stream_index > movie->max_stream_index ? -1 :
                             movie->out_index[movie->pkt->stream_index];

            if (pkt_out_id >= 0) {
                ret = decode_packet(ctx, pkt_out_id);
            }
            av_packet_unref(movie->pkt);
            ff_filter_set_ready(ctx, 100);
            return (ret <= 0) ? ret : 0;
        }
    } else {
        int nb_eofs = 0;

        for (int i = 0; i < ctx->nb_outputs; i++) {
            if (!movie->st[i].eof) {
                ret = decode_packet(ctx, i);
                if (ret <= 0)
                    movie->st[i].eof = 1;
            }
            nb_eofs += movie->st[i].eof == 1;
        }
        if (nb_eofs == ctx->nb_outputs && movie->loop_count != 1) {
            ret = rewind_file(ctx);
            if (ret < 0)
                return ret;
            movie->loop_count -= movie->loop_count > 1;
            av_log(ctx, AV_LOG_VERBOSE, "Stream finished, looping.\n");
            ff_filter_set_ready(ctx, 100);
            for (int i = 0; i < ctx->nb_outputs; i++)
                movie->st[i].eof = 0;
            movie->eof = 0;
            return 0;
        } else {
            for (int i = 0; i < ctx->nb_outputs; i++) {
                if (movie->st[i].eof) {
                    ff_outlink_set_status(ctx->outputs[i], AVERROR_EOF, movie->st[i].last_pts);
                    nb_eofs++;
                }
            }
        }

        if (nb_eofs < ctx->nb_outputs)
            ff_filter_set_ready(ctx, 100);
        return 0;
    }

    return FFERROR_NOT_READY;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    MovieContext *movie = ctx->priv;
    int ret = AVERROR(ENOSYS);

    if (!strcmp(cmd, "seek")) {
        int idx, flags, i;
        int64_t ts;
        char tail[2];

        if (sscanf(args, "%i|%"SCNi64"|%i %1s", &idx, &ts, &flags, tail) != 3)
            return AVERROR(EINVAL);

        ret = av_seek_frame(movie->format_ctx, idx, ts, flags);
        if (ret < 0)
            return ret;

        for (i = 0; i < ctx->nb_outputs; i++) {
            avcodec_flush_buffers(movie->st[i].codec_ctx);
        }
        return ret;
    } else if (!strcmp(cmd, "get_duration")) {
        int print_len;
        char tail[2];

        if (!res || res_len <= 0)
            return AVERROR(EINVAL);

        if (args && sscanf(args, "%1s", tail) == 1)
            return AVERROR(EINVAL);

        print_len = snprintf(res, res_len, "%"PRId64, movie->format_ctx->duration);
        if (print_len < 0 || print_len >= res_len)
            return AVERROR(EINVAL);

        return 0;
    }

    return ret;
}

AVFILTER_DEFINE_CLASS_EXT(movie, "(a)movie", movie_options);

#if CONFIG_MOVIE_FILTER

const AVFilter ff_avsrc_movie = {
    .name          = "movie",
    .description   = NULL_IF_CONFIG_SMALL("Read from a movie source."),
    .priv_size     = sizeof(MovieContext),
    .priv_class    = &movie_class,
    .init          = movie_common_init,
    .activate      = activate,
    .uninit        = movie_uninit,
    FILTER_QUERY_FUNC(movie_query_formats),

    .inputs    = NULL,
    .outputs   = NULL,
    .flags     = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
    .process_command = process_command
};

#endif  /* CONFIG_MOVIE_FILTER */

#if CONFIG_AMOVIE_FILTER

const AVFilter ff_avsrc_amovie = {
    .name          = "amovie",
    .description   = NULL_IF_CONFIG_SMALL("Read audio from a movie source."),
    .priv_class    = &movie_class,
    .priv_size     = sizeof(MovieContext),
    .init          = movie_common_init,
    .activate      = activate,
    .uninit        = movie_uninit,
    FILTER_QUERY_FUNC(movie_query_formats),

    .inputs     = NULL,
    .outputs    = NULL,
    .flags      = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
    .process_command = process_command,
};

#endif /* CONFIG_AMOVIE_FILTER */
