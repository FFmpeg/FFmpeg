/*
 * Copyright (c) 2011, Luca Barbato
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
 * @file generic segmenter
 * M3U8 specification can be find here:
 * @url{http://tools.ietf.org/id/draft-pantos-http-live-streaming-08.txt}
 */

#include <float.h>

#include "avformat.h"
#include "internal.h"

#include "libavutil/avassert.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/parseutils.h"
#include "libavutil/mathematics.h"

typedef enum {
    LIST_TYPE_UNDEFINED = -1,
    LIST_TYPE_FLAT = 0,
    LIST_TYPE_CSV,
    LIST_TYPE_M3U8,
    LIST_TYPE_EXT, ///< deprecated
    LIST_TYPE_NB,
} ListType;


#define SEGMENT_LIST_FLAG_CACHE 1
#define SEGMENT_LIST_FLAG_LIVE  2

typedef struct {
    const AVClass *class;  /**< Class for private options. */
    int segment_idx;       ///< index of the segment file to write, starting from 0
    int segment_idx_wrap;  ///< number after which the index wraps
    int segment_count;     ///< number of segment files already written
    AVFormatContext *avf;
    char *format;          ///< format to use for output segment files
    char *list;            ///< filename for the segment list file
    int   list_count;      ///< list counter
    int   list_flags;      ///< flags affecting list generation
    int   list_size;       ///< number of entries for the segment list file
    double list_max_segment_time; ///< max segment time in the current list
    ListType list_type;    ///< set the list type
    AVIOContext *list_pb;  ///< list file put-byte context
    char *time_str;        ///< segment duration specification string
    int64_t time;          ///< segment duration
    char *times_str;       ///< segment times specification string
    int64_t *times;        ///< list of segment interval specification
    int nb_times;          ///< number of elments in the times array
    char *time_delta_str;  ///< approximation value duration used for the segment times
    int64_t time_delta;
    int has_video;
    double start_time, end_time;
} SegmentContext;

static void print_csv_escaped_str(AVIOContext *ctx, const char *str)
{
    int needs_quoting = !!str[strcspn(str, "\",\n\r")];

    if (needs_quoting)
        avio_w8(ctx, '"');

    for (; *str; str++) {
        if (*str == '"')
            avio_w8(ctx, '"');
        avio_w8(ctx, *str);
    }
    if (needs_quoting)
        avio_w8(ctx, '"');
}

static int segment_start(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    int err = 0;

    if (seg->segment_idx_wrap)
        seg->segment_idx %= seg->segment_idx_wrap;

    if (av_get_frame_filename(oc->filename, sizeof(oc->filename),
                              s->filename, seg->segment_idx++) < 0) {
        av_log(oc, AV_LOG_ERROR, "Invalid segment filename template '%s'\n", s->filename);
        return AVERROR(EINVAL);
    }
    seg->segment_count++;

    if ((err = avio_open2(&oc->pb, oc->filename, AVIO_FLAG_WRITE,
                          &s->interrupt_callback, NULL)) < 0)
        return err;

    if (!oc->priv_data && oc->oformat->priv_data_size > 0) {
        oc->priv_data = av_mallocz(oc->oformat->priv_data_size);
        if (!oc->priv_data) {
            avio_close(oc->pb);
            return AVERROR(ENOMEM);
        }
        if (oc->oformat->priv_class) {
            *(const AVClass**)oc->priv_data = oc->oformat->priv_class;
            av_opt_set_defaults(oc->priv_data);
        }
    }

    if ((err = oc->oformat->write_header(oc)) < 0) {
        goto fail;
    }

    return 0;

fail:
    av_log(oc, AV_LOG_ERROR, "Failure occurred when starting segment '%s'\n",
           oc->filename);
    avio_close(oc->pb);
    av_freep(&oc->priv_data);

    return err;
}

static int segment_list_open(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    int ret;

    ret = avio_open2(&seg->list_pb, seg->list, AVIO_FLAG_WRITE,
                     &s->interrupt_callback, NULL);
    if (ret < 0)
        return ret;
    seg->list_max_segment_time = 0;

    if (seg->list_type == LIST_TYPE_M3U8) {
        avio_printf(seg->list_pb, "#EXTM3U\n");
        avio_printf(seg->list_pb, "#EXT-X-VERSION:3\n");
        avio_printf(seg->list_pb, "#EXT-X-MEDIA-SEQUENCE:%d\n", seg->list_count);
        avio_printf(seg->list_pb, "#EXT-X-ALLOWCACHE:%d\n",
                    !!(seg->list_flags & SEGMENT_LIST_FLAG_CACHE));
        if (seg->list_flags & SEGMENT_LIST_FLAG_LIVE)
            avio_printf(seg->list_pb,
                        "#EXT-X-TARGETDURATION:%"PRId64"\n", seg->time / 1000000);
    }

    return ret;
}

static void segment_list_close(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;

    if (seg->list_type == LIST_TYPE_M3U8) {
        if (!(seg->list_flags & SEGMENT_LIST_FLAG_LIVE))
            avio_printf(seg->list_pb, "#EXT-X-TARGETDURATION:%d\n",
                        (int)ceil(seg->list_max_segment_time));
        avio_printf(seg->list_pb, "#EXT-X-ENDLIST\n");
    }
    seg->list_count++;

    avio_close(seg->list_pb);
}

static int segment_end(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    int ret = 0;

    if (oc->oformat->write_trailer)
        ret = oc->oformat->write_trailer(oc);

    if (ret < 0)
        av_log(s, AV_LOG_ERROR, "Failure occurred when ending segment '%s'\n",
               oc->filename);

    if (seg->list) {
        if (seg->list_size && !(seg->segment_count % seg->list_size)) {
            segment_list_close(s);
            if ((ret = segment_list_open(s)) < 0)
                goto end;
        }

        if (seg->list_type == LIST_TYPE_FLAT) {
            avio_printf(seg->list_pb, "%s\n", oc->filename);
        } else if (seg->list_type == LIST_TYPE_CSV || seg->list_type == LIST_TYPE_EXT) {
            print_csv_escaped_str(seg->list_pb, oc->filename);
            avio_printf(seg->list_pb, ",%f,%f\n", seg->start_time, seg->end_time);
        } else if (seg->list_type == LIST_TYPE_M3U8) {
            avio_printf(seg->list_pb, "#EXTINF:%f,\n%s\n",
                        seg->end_time - seg->start_time, oc->filename);
        }
        seg->list_max_segment_time = FFMAX(seg->end_time - seg->start_time, seg->list_max_segment_time);
        avio_flush(seg->list_pb);
    }

end:
    avio_close(oc->pb);
    if (oc->oformat->priv_class)
        av_opt_free(oc->priv_data);
    av_freep(&oc->priv_data);

    return ret;
}

static int parse_times(void *log_ctx, int64_t **times, int *nb_times,
                       const char *times_str)
{
    char *p;
    int i, ret = 0;
    char *times_str1 = av_strdup(times_str);
    char *saveptr = NULL;

    if (!times_str1)
        return AVERROR(ENOMEM);

#define FAIL(err) ret = err; goto end

    *nb_times = 1;
    for (p = times_str1; *p; p++)
        if (*p == ',')
            (*nb_times)++;

    *times = av_malloc(sizeof(**times) * *nb_times);
    if (!*times) {
        av_log(log_ctx, AV_LOG_ERROR, "Could not allocate forced times array\n");
        FAIL(AVERROR(ENOMEM));
    }

    p = times_str1;
    for (i = 0; i < *nb_times; i++) {
        int64_t t;
        char *tstr = av_strtok(p, ",", &saveptr);
        av_assert0(tstr);
        p = NULL;

        ret = av_parse_time(&t, tstr, 1);
        if (ret < 0) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Invalid time duration specification in %s\n", p);
            FAIL(AVERROR(EINVAL));
        }
        (*times)[i] = t;

        /* check on monotonicity */
        if (i && (*times)[i-1] > (*times)[i]) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Specified time %f is greater than the following time %f\n",
                   (float)((*times)[i])/1000000, (float)((*times)[i-1])/1000000);
            FAIL(AVERROR(EINVAL));
        }
    }

end:
    av_free(times_str1);
    return ret;
}

static int seg_write_header(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc;
    int ret, i;

    seg->segment_count = 0;

    if (seg->time_str && seg->times_str) {
        av_log(s, AV_LOG_ERROR,
               "segment_time and segment_times options are mutually exclusive, select just one of them\n");
        return AVERROR(EINVAL);
    }

    if ((seg->list_flags & SEGMENT_LIST_FLAG_LIVE) && seg->times_str) {
        av_log(s, AV_LOG_ERROR,
               "segment_flags +live and segment_times options are mutually exclusive:"
               "specify -segment_time if you want a live-friendly list\n");
        return AVERROR(EINVAL);
    }

    if (seg->times_str) {
        if ((ret = parse_times(s, &seg->times, &seg->nb_times, seg->times_str)) < 0)
            return ret;
    } else {
        /* set default value if not specified */
        if (!seg->time_str)
            seg->time_str = av_strdup("2");
        if ((ret = av_parse_time(&seg->time, seg->time_str, 1)) < 0) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid time duration specification '%s' for segment_time option\n",
                   seg->time_str);
            return ret;
        }
    }

    if (seg->time_delta_str) {
        if ((ret = av_parse_time(&seg->time_delta, seg->time_delta_str, 1)) < 0) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid time duration specification '%s' for delta option\n",
                   seg->time_delta_str);
            return ret;
        }
    }

    oc = avformat_alloc_context();

    if (!oc)
        return AVERROR(ENOMEM);

    if (seg->list) {
        if (seg->list_type == LIST_TYPE_UNDEFINED) {
            if      (av_match_ext(seg->list, "csv" )) seg->list_type = LIST_TYPE_CSV;
            else if (av_match_ext(seg->list, "ext" )) seg->list_type = LIST_TYPE_EXT;
            else if (av_match_ext(seg->list, "m3u8")) seg->list_type = LIST_TYPE_M3U8;
            else                                      seg->list_type = LIST_TYPE_FLAT;
        }
        if ((ret = segment_list_open(s)) < 0)
            goto fail;
    }
    if (seg->list_type == LIST_TYPE_EXT)
        av_log(s, AV_LOG_WARNING, "'ext' list type option is deprecated in favor of 'csv'\n");

    for (i = 0; i< s->nb_streams; i++)
        seg->has_video +=
            (s->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO);

    if (seg->has_video > 1)
        av_log(s, AV_LOG_WARNING,
               "More than a single video stream present, "
               "expect issues decoding it.\n");

    oc->oformat = av_guess_format(seg->format, s->filename, NULL);

    if (!oc->oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }
    if (oc->oformat->flags & AVFMT_NOFILE) {
        av_log(s, AV_LOG_ERROR, "format %s not supported.\n",
               oc->oformat->name);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    seg->avf = oc;

    oc->streams = s->streams;
    oc->nb_streams = s->nb_streams;

    if (av_get_frame_filename(oc->filename, sizeof(oc->filename),
                              s->filename, seg->segment_idx++) < 0) {
        ret = AVERROR(EINVAL);
        goto fail;
    }
    seg->segment_count++;

    if ((ret = avio_open2(&oc->pb, oc->filename, AVIO_FLAG_WRITE,
                          &s->interrupt_callback, NULL)) < 0)
        goto fail;

    if ((ret = avformat_write_header(oc, NULL)) < 0) {
        avio_close(oc->pb);
        goto fail;
    }

fail:
    if (ret) {
        if (oc) {
            oc->streams = NULL;
            oc->nb_streams = 0;
            avformat_free_context(oc);
        }
        if (seg->list)
            segment_list_close(s);
    }
    return ret;
}

static int seg_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    AVStream *st = oc->streams[pkt->stream_index];
    int64_t end_pts;
    int ret;

    if (seg->times) {
        end_pts = seg->segment_count <= seg->nb_times ?
            seg->times[seg->segment_count-1] : INT64_MAX;
    } else {
        end_pts = seg->time * seg->segment_count;
    }

    /* if the segment has video, start a new segment *only* with a key video frame */
    if ((st->codec->codec_type == AVMEDIA_TYPE_VIDEO || !seg->has_video) &&
        av_compare_ts(pkt->pts, st->time_base,
                      end_pts-seg->time_delta, AV_TIME_BASE_Q) >= 0 &&
        pkt->flags & AV_PKT_FLAG_KEY) {

        av_log(s, AV_LOG_DEBUG, "Next segment starts with packet stream:%d pts:%"PRId64" pts_time:%f\n",
               pkt->stream_index, pkt->pts, pkt->pts * av_q2d(st->time_base));

        if ((ret = segment_end(s)) < 0 || (ret = segment_start(s)) < 0)
            goto fail;
        seg->start_time = (double)pkt->pts * av_q2d(st->time_base);
    } else if (pkt->pts != AV_NOPTS_VALUE) {
        seg->end_time = FFMAX(seg->end_time,
                              (double)(pkt->pts + pkt->duration) * av_q2d(st->time_base));
    }

    ret = oc->oformat->write_packet(oc, pkt);

fail:
    if (ret < 0) {
        oc->streams = NULL;
        oc->nb_streams = 0;
        if (seg->list)
            avio_close(seg->list_pb);
        avformat_free_context(oc);
    }

    return ret;
}

static int seg_write_trailer(struct AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    int ret = segment_end(s);
    if (seg->list)
        segment_list_close(s);

    av_opt_free(seg);
    av_freep(&seg->times);

    oc->streams = NULL;
    oc->nb_streams = 0;
    avformat_free_context(oc);
    return ret;
}

#define OFFSET(x) offsetof(SegmentContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "segment_format",    "set container format used for the segments", OFFSET(format),  AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E },
    { "segment_list",      "set the segment list filename",              OFFSET(list),    AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E },

    { "segment_list_flags","set flags affecting segment list generation", OFFSET(list_flags), AV_OPT_TYPE_FLAGS, {.i64 = SEGMENT_LIST_FLAG_CACHE }, 0, UINT_MAX, E, "list_flags"},
    { "cache",             "allow list caching",                                    0, AV_OPT_TYPE_CONST, {.i64 = SEGMENT_LIST_FLAG_CACHE }, INT_MIN, INT_MAX,   E, "list_flags"},
    { "live",              "enable live-friendly list generation (useful for HLS)", 0, AV_OPT_TYPE_CONST, {.i64 = SEGMENT_LIST_FLAG_LIVE }, INT_MIN, INT_MAX,    E, "list_flags"},

    { "segment_list_size", "set the maximum number of playlist entries", OFFSET(list_size), AV_OPT_TYPE_INT,  {.i64 = 0},     0, INT_MAX, E },
    { "segment_list_type", "set the segment list type",                  OFFSET(list_type), AV_OPT_TYPE_INT,  {.i64 = LIST_TYPE_UNDEFINED}, -1, LIST_TYPE_NB-1, E, "list_type" },
    { "flat", "flat format",     0, AV_OPT_TYPE_CONST, {.i64=LIST_TYPE_FLAT }, INT_MIN, INT_MAX, 0, "list_type" },
    { "csv",  "csv format",      0, AV_OPT_TYPE_CONST, {.i64=LIST_TYPE_CSV  }, INT_MIN, INT_MAX, 0, "list_type" },
    { "ext",  "extended format", 0, AV_OPT_TYPE_CONST, {.i64=LIST_TYPE_EXT  }, INT_MIN, INT_MAX, 0, "list_type" },
    { "m3u8", "M3U8 format",     0, AV_OPT_TYPE_CONST, {.i64=LIST_TYPE_M3U8 }, INT_MIN, INT_MAX, 0, "list_type" },
    { "segment_time",      "set segment duration",                       OFFSET(time_str),AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E },
    { "segment_time_delta","set approximation value used for the segment times", OFFSET(time_delta_str), AV_OPT_TYPE_STRING, {.str = "0"}, 0, 0, E },
    { "segment_times",     "set segment split time points",              OFFSET(times_str),AV_OPT_TYPE_STRING,{.str = NULL},  0, 0,       E },
    { "segment_wrap",      "set number after which the index wraps",     OFFSET(segment_idx_wrap), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, E },
    { NULL },
};

static const AVClass seg_class = {
    .class_name = "segment muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_segment_muxer = {
    .name           = "segment",
    .long_name      = NULL_IF_CONFIG_SMALL("segment"),
    .priv_data_size = sizeof(SegmentContext),
    .flags          = AVFMT_GLOBALHEADER | AVFMT_NOFILE,
    .write_header   = seg_write_header,
    .write_packet   = seg_write_packet,
    .write_trailer  = seg_write_trailer,
    .priv_class     = &seg_class,
};

static const AVClass sseg_class = {
    .class_name = "stream_segment muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_stream_segment_muxer = {
    .name           = "stream_segment,ssegment",
    .long_name      = NULL_IF_CONFIG_SMALL("streaming segment muxer"),
    .priv_data_size = sizeof(SegmentContext),
    .flags          = AVFMT_NOFILE,
    .write_header   = seg_write_header,
    .write_packet   = seg_write_packet,
    .write_trailer  = seg_write_trailer,
    .priv_class     = &sseg_class,
};
