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
 * @todo use direct rendering (no allocation of a new frame)
 * @todo support a PTS correction mechanism
 */

/* #define DEBUG */

#include <float.h>
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/timestamp.h"
#include "libavformat/avformat.h"
#include "audio.h"
#include "avcodec.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct {
    AVStream *st;
    int done;
} MovieStream;

typedef struct {
    /* common A/V fields */
    const AVClass *class;
    int64_t seek_point;   ///< seekpoint in microseconds
    double seek_point_d;
    char *format_name;
    char *file_name;
    char *stream_specs; /**< user-provided list of streams, separated by + */
    int stream_index; /**< for compatibility */
    int loop_count;

    AVFormatContext *format_ctx;
    int eof;
    AVPacket pkt, pkt0;
    AVFrame *frame;   ///< video frame to store the decoded images in

    int max_stream_index; /**< max stream # actually used for output */
    MovieStream *st; /**< array of all streams, one per output */
    int *out_index; /**< stream number -> output number map, or -1 */
} MovieContext;

#define OFFSET(x) offsetof(MovieContext, x)
#define F AV_OPT_FLAG_FILTERING_PARAM

static const AVOption movie_options[]= {
{"format_name",  "set format name",         OFFSET(format_name),  AV_OPT_TYPE_STRING, {.str =  0},  CHAR_MIN, CHAR_MAX, F },
{"f",            "set format name",         OFFSET(format_name),  AV_OPT_TYPE_STRING, {.str =  0},  CHAR_MIN, CHAR_MAX, F },
{"streams",      "set streams",             OFFSET(stream_specs), AV_OPT_TYPE_STRING, {.str =  0},  CHAR_MAX, CHAR_MAX, F },
{"s",            "set streams",             OFFSET(stream_specs), AV_OPT_TYPE_STRING, {.str =  0},  CHAR_MAX, CHAR_MAX, F },
{"si",           "set stream index",        OFFSET(stream_index), AV_OPT_TYPE_INT,    {.i64 = -1},  -1,       INT_MAX, F },
{"stream_index", "set stream index",        OFFSET(stream_index), AV_OPT_TYPE_INT,    {.i64 = -1},  -1,       INT_MAX, F },
{"seek_point",   "set seekpoint (seconds)", OFFSET(seek_point_d), AV_OPT_TYPE_DOUBLE, {.dbl =  0},  0,        (INT64_MAX-1) / 1000000, F },
{"sp",           "set seekpoint (seconds)", OFFSET(seek_point_d), AV_OPT_TYPE_DOUBLE, {.dbl =  0},  0,        (INT64_MAX-1) / 1000000, F },
{"loop",         "set loop count",          OFFSET(loop_count),   AV_OPT_TYPE_INT,    {.i64 =  1},  0,        INT_MAX, F },
{NULL},
};

static int movie_config_output_props(AVFilterLink *outlink);
static int movie_request_frame(AVFilterLink *outlink);

static AVStream *find_stream(void *log, AVFormatContext *avf, const char *spec)
{
    int i, ret, already = 0, stream_id = -1;
    char type_char, dummy;
    AVStream *found = NULL;
    enum AVMediaType type;

    ret = sscanf(spec, "d%[av]%d%c", &type_char, &stream_id, &dummy);
    if (ret >= 1 && ret <= 2) {
        type = type_char == 'v' ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO;
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
    if (found->codec->codec_type != AVMEDIA_TYPE_VIDEO &&
        found->codec->codec_type != AVMEDIA_TYPE_AUDIO) {
        av_log(log, AV_LOG_ERROR, "Stream specifier \"%s\" matched a %s stream,"
               "currently unsupported by libavfilter\n", spec,
               av_get_media_type_string(found->codec->codec_type));
        return NULL;
    }
    return found;
}

static int open_stream(void *log, MovieStream *st)
{
    AVCodec *codec;
    int ret;

    codec = avcodec_find_decoder(st->st->codec->codec_id);
    if (!codec) {
        av_log(log, AV_LOG_ERROR, "Failed to find any codec\n");
        return AVERROR(EINVAL);
    }

    if ((ret = avcodec_open2(st->st->codec, codec, NULL)) < 0) {
        av_log(log, AV_LOG_ERROR, "Failed to open codec\n");
        return ret;
    }

    return 0;
}

static int guess_channel_layout(MovieStream *st, int st_index, void *log_ctx)
{
    AVCodecContext *dec_ctx = st->st->codec;
    char buf[256];
    int64_t chl = av_get_default_channel_layout(dec_ctx->channels);

    if (!chl) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Channel layout is not set in stream %d, and could not "
               "be guessed from the number of channels (%d)\n",
               st_index, dec_ctx->channels);
        return AVERROR(EINVAL);
    }

    av_get_channel_layout_string(buf, sizeof(buf), dec_ctx->channels, chl);
    av_log(log_ctx, AV_LOG_WARNING,
           "Channel layout is not set in output stream %d, "
           "guessed channel layout is '%s'\n",
           st_index, buf);
    dec_ctx->channel_layout = chl;
    return 0;
}

static av_cold int movie_common_init(AVFilterContext *ctx, const char *args, const AVClass *class)
{
    MovieContext *movie = ctx->priv;
    AVInputFormat *iformat = NULL;
    int64_t timestamp;
    int nb_streams, ret, i;
    char default_streams[16], *stream_specs, *spec, *cursor;
    char name[16];
    AVStream *st;

    movie->class = class;
    av_opt_set_defaults(movie);

    if (args)
        movie->file_name = av_get_token(&args, ":");
    if (!movie->file_name || !*movie->file_name) {
        av_log(ctx, AV_LOG_ERROR, "No filename provided!\n");
        return AVERROR(EINVAL);
    }

    if (*args++ == ':' && (ret = av_set_options_string(movie, args, "=", ":")) < 0)
        return ret;

    movie->seek_point = movie->seek_point_d * 1000000 + 0.5;

    stream_specs = movie->stream_specs;
    if (!stream_specs) {
        snprintf(default_streams, sizeof(default_streams), "d%c%d",
                 !strcmp(ctx->filter->name, "amovie") ? 'a' : 'v',
                 movie->stream_index);
        stream_specs = default_streams;
    }
    for (cursor = stream_specs, nb_streams = 1; *cursor; cursor++)
        if (*cursor == '+')
            nb_streams++;

    if (movie->loop_count != 1 && nb_streams != 1) {
        av_log(ctx, AV_LOG_ERROR,
               "Loop with several streams is currently unsupported\n");
        return AVERROR_PATCHWELCOME;
    }

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

    for (i = 0; i < movie->format_ctx->nb_streams; i++)
        movie->format_ctx->streams[i]->discard = AVDISCARD_ALL;

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
    }
    if (av_strtok(NULL, "+", &cursor))
        return AVERROR_BUG;

    movie->out_index = av_calloc(movie->max_stream_index + 1,
                                 sizeof(*movie->out_index));
    if (!movie->out_index)
        return AVERROR(ENOMEM);
    for (i = 0; i <= movie->max_stream_index; i++)
        movie->out_index[i] = -1;
    for (i = 0; i < nb_streams; i++)
        movie->out_index[movie->st[i].st->index] = i;

    for (i = 0; i < nb_streams; i++) {
        AVFilterPad pad = { 0 };
        snprintf(name, sizeof(name), "out%d", i);
        pad.type          = movie->st[i].st->codec->codec_type;
        pad.name          = av_strdup(name);
        pad.config_props  = movie_config_output_props;
        pad.request_frame = movie_request_frame;
        ff_insert_outpad(ctx, i, &pad);
        ret = open_stream(ctx, &movie->st[i]);
        if (ret < 0)
            return ret;
        if ( movie->st[i].st->codec->codec->type == AVMEDIA_TYPE_AUDIO &&
            !movie->st[i].st->codec->channel_layout) {
            ret = guess_channel_layout(&movie->st[i], i, ctx);
            if (ret < 0)
                return ret;
        }
    }

    if (!(movie->frame = avcodec_alloc_frame()) ) {
        av_log(log, AV_LOG_ERROR, "Failed to alloc frame\n");
        return AVERROR(ENOMEM);
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
        av_freep(&ctx->output_pads[i].name);
        if (movie->st[i].st)
            avcodec_close(movie->st[i].st->codec);
    }
    av_opt_free(movie);
    av_freep(&movie->file_name);
    av_freep(&movie->st);
    av_freep(&movie->out_index);
    avcodec_free_frame(&movie->frame);
    if (movie->format_ctx)
        avformat_close_input(&movie->format_ctx);
}

static int movie_query_formats(AVFilterContext *ctx)
{
    MovieContext *movie = ctx->priv;
    int list[] = { 0, -1 };
    int64_t list64[] = { 0, -1 };
    int i;

    for (i = 0; i < ctx->nb_outputs; i++) {
        MovieStream *st = &movie->st[i];
        AVCodecContext *c = st->st->codec;
        AVFilterLink *outlink = ctx->outputs[i];

        switch (c->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            list[0] = c->pix_fmt;
            ff_formats_ref(ff_make_format_list(list), &outlink->in_formats);
            break;
        case AVMEDIA_TYPE_AUDIO:
            list[0] = c->sample_fmt;
            ff_formats_ref(ff_make_format_list(list), &outlink->in_formats);
            list[0] = c->sample_rate;
            ff_formats_ref(ff_make_format_list(list), &outlink->in_samplerates);
            list64[0] = c->channel_layout;
            ff_channel_layouts_ref(avfilter_make_format64_list(list64),
                                   &outlink->in_channel_layouts);
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
    AVCodecContext *c = st->st->codec;

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

    return 0;
}

static AVFilterBufferRef *frame_to_buf(enum AVMediaType type, AVFrame *frame,
                                       AVFilterLink *outlink)
{
    AVFilterBufferRef *buf, *copy;

    buf = avfilter_get_buffer_ref_from_frame(type, frame,
                                             AV_PERM_WRITE |
                                             AV_PERM_PRESERVE |
                                             AV_PERM_REUSE2);
    if (!buf)
        return NULL;
    buf->pts = av_frame_get_best_effort_timestamp(frame);
    copy = ff_copy_buffer_ref(outlink, buf);
    if (!copy)
        return NULL;
    buf->buf->data[0] = NULL; /* it belongs to the frame */
    avfilter_unref_buffer(buf);
    return copy;
}

static char *describe_bufref_to_str(char *dst, size_t dst_size,
                                    AVFilterBufferRef *buf,
                                    AVFilterLink *link)
{
    switch (buf->type) {
    case AVMEDIA_TYPE_VIDEO:
        snprintf(dst, dst_size,
                 "video pts:%s time:%s pos:%"PRId64" size:%dx%d aspect:%d/%d",
                 av_ts2str(buf->pts), av_ts2timestr(buf->pts, &link->time_base),
                 buf->pos, buf->video->w, buf->video->h,
                 buf->video->sample_aspect_ratio.num,
                 buf->video->sample_aspect_ratio.den);
                 break;
    case AVMEDIA_TYPE_AUDIO:
        snprintf(dst, dst_size,
                 "audio pts:%s time:%s pos:%"PRId64" samples:%d",
                 av_ts2str(buf->pts), av_ts2timestr(buf->pts, &link->time_base),
                 buf->pos, buf->audio->nb_samples);
                 break;
    default:
        snprintf(dst, dst_size, "%s BUG", av_get_media_type_string(buf->type));
        break;
    }
    return dst;
}

#define describe_bufref(buf, link) \
    describe_bufref_to_str((char[1024]){0}, 1024, buf, link)

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
        avcodec_flush_buffers(movie->st[i].st->codec);
        movie->st[i].done = 0;
    }
    movie->eof = 0;
    return 0;
}

/**
 * Try to push a frame to the requested output.
 *
 * @param ctx     filter context
 * @param out_id  number of output where a frame is wanted;
 *                if the frame is read from file, used to set the return value;
 *                if the codec is being flushed, flush the corresponding stream
 * @return  1 if a frame was pushed on the requested output,
 *          0 if another attempt is possible,
 *          <0 AVERROR code
 */
static int movie_push_frame(AVFilterContext *ctx, unsigned out_id)
{
    MovieContext *movie = ctx->priv;
    AVPacket *pkt = &movie->pkt;
    MovieStream *st;
    int ret, got_frame = 0, pkt_out_id;
    AVFilterLink *outlink;
    AVFilterBufferRef *buf;

    if (!pkt->size) {
        if (movie->eof) {
            if (movie->st[out_id].done) {
                if (movie->loop_count != 1) {
                    ret = rewind_file(ctx);
                    if (ret < 0)
                        return ret;
                    movie->loop_count -= movie->loop_count > 1;
                    av_log(ctx, AV_LOG_VERBOSE, "Stream finished, looping.\n");
                    return 0; /* retry */
                }
                return AVERROR_EOF;
            }
            pkt->stream_index = movie->st[out_id].st->index;
            /* packet is already ready for flushing */
        } else {
            ret = av_read_frame(movie->format_ctx, &movie->pkt0);
            if (ret < 0) {
                av_init_packet(&movie->pkt0); /* ready for flushing */
                *pkt = movie->pkt0;
                if (ret == AVERROR_EOF) {
                    movie->eof = 1;
                    return 0; /* start flushing */
                }
                return ret;
            }
            *pkt = movie->pkt0;
        }
    }

    pkt_out_id = pkt->stream_index > movie->max_stream_index ? -1 :
                 movie->out_index[pkt->stream_index];
    if (pkt_out_id < 0) {
        av_free_packet(&movie->pkt0);
        pkt->size = 0; /* ready for next run */
        pkt->data = NULL;
        return 0;
    }
    st = &movie->st[pkt_out_id];
    outlink = ctx->outputs[pkt_out_id];

    switch (st->st->codec->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        ret = avcodec_decode_video2(st->st->codec, movie->frame, &got_frame, pkt);
        break;
    case AVMEDIA_TYPE_AUDIO:
        ret = avcodec_decode_audio4(st->st->codec, movie->frame, &got_frame, pkt);
        break;
    default:
        ret = AVERROR(ENOSYS);
        break;
    }
    if (ret < 0) {
        av_log(ctx, AV_LOG_WARNING, "Decode error: %s\n", av_err2str(ret));
        return 0;
    }
    if (!ret)
        ret = pkt->size;

    pkt->data += ret;
    pkt->size -= ret;
    if (pkt->size <= 0) {
        av_free_packet(&movie->pkt0);
        pkt->size = 0; /* ready for next run */
        pkt->data = NULL;
    }
    if (!got_frame) {
        if (!ret)
            st->done = 1;
        return 0;
    }

    buf = frame_to_buf(st->st->codec->codec_type, movie->frame, outlink);
    if (!buf)
        return AVERROR(ENOMEM);
    av_dlog(ctx, "movie_push_frame(): file:'%s' %s\n", movie->file_name,
            describe_bufref(buf, outlink));
    switch (st->st->codec->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (!movie->frame->sample_aspect_ratio.num)
            buf->video->sample_aspect_ratio = st->st->sample_aspect_ratio;
        ff_start_frame(outlink, buf);
        ff_draw_slice(outlink, 0, outlink->h, 1);
        ff_end_frame(outlink);
        break;
    case AVMEDIA_TYPE_AUDIO:
        ff_filter_samples(outlink, buf);
        break;
    }

    return pkt_out_id == out_id;
}

static int movie_request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    unsigned out_id = FF_OUTLINK_IDX(outlink);
    int ret;

    while (1) {
        ret = movie_push_frame(ctx, out_id);
        if (ret)
            return FFMIN(ret, 0);
    }
}

#if CONFIG_MOVIE_FILTER

AVFILTER_DEFINE_CLASS(movie);

static av_cold int movie_init(AVFilterContext *ctx, const char *args)
{
    return movie_common_init(ctx, args, &movie_class);
}

AVFilter avfilter_avsrc_movie = {
    .name          = "movie",
    .description   = NULL_IF_CONFIG_SMALL("Read from a movie source."),
    .priv_size     = sizeof(MovieContext),
    .init          = movie_init,
    .uninit        = movie_uninit,
    .query_formats = movie_query_formats,

    .inputs    = NULL,
    .outputs   = (const AVFilterPad[]) {{ .name = NULL }},
    .priv_class = &movie_class,
};

#endif  /* CONFIG_MOVIE_FILTER */

#if CONFIG_AMOVIE_FILTER

#define amovie_options movie_options
AVFILTER_DEFINE_CLASS(amovie);

static av_cold int amovie_init(AVFilterContext *ctx, const char *args)
{
    return movie_common_init(ctx, args, &amovie_class);
}

AVFilter avfilter_avsrc_amovie = {
    .name          = "amovie",
    .description   = NULL_IF_CONFIG_SMALL("Read audio from a movie source."),
    .priv_size     = sizeof(MovieContext),
    .init          = amovie_init,
    .uninit        = movie_uninit,
    .query_formats = movie_query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name = NULL }},
    .outputs   = (const AVFilterPad[]) {{ .name = NULL }},
    .priv_class = &amovie_class,
};

#endif /* CONFIG_AMOVIE_FILTER */
