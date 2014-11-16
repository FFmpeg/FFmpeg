/*
 * Apple HTTP Live Streaming segmenter
 * Copyright (c) 2012, Luca Barbato
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

#include <float.h>
#include <stdint.h>

#include "libavutil/avassert.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"

#include "avformat.h"
#include "internal.h"

typedef struct HLSSegment {
    char filename[1024];
    double duration; /* in seconds */
    int64_t pos;
    int64_t size;

    struct HLSSegment *next;
} HLSSegment;

typedef enum HLSFlags {
    // Generate a single media file and use byte ranges in the playlist.
    HLS_SINGLE_FILE = (1 << 0),
} HLSFlags;

typedef struct HLSContext {
    const AVClass *class;  // Class for private options.
    unsigned number;
    int64_t sequence;
    int64_t start_sequence;
    AVOutputFormat *oformat;

    AVFormatContext *avf;

    float time;            // Set by a private option.
    int max_nb_segments;   // Set by a private option.
    int  wrap;             // Set by a private option.
    uint32_t flags;        // enum HLSFlags

    int allowcache;
    int64_t recording_time;
    int has_video;
    int64_t start_pts;
    int64_t end_pts;
    double duration;      // last segment duration computed so far, in seconds
    int64_t start_pos;    // last segment starting position
    int64_t size;         // last segment size
    int nb_entries;

    HLSSegment *segments;
    HLSSegment *last_segment;

    char *basename;
    char *baseurl;
    char *format_options_str;
    AVDictionary *format_options;

    AVIOContext *pb;
} HLSContext;

static int hls_mux_init(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc;
    int i, ret;

    ret = avformat_alloc_output_context2(&hls->avf, hls->oformat, NULL, NULL);
    if (ret < 0)
        return ret;
    oc = hls->avf;

    oc->oformat            = hls->oformat;
    oc->interrupt_callback = s->interrupt_callback;
    oc->max_delay          = s->max_delay;
    av_dict_copy(&oc->metadata, s->metadata, 0);

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st;
        if (!(st = avformat_new_stream(oc, NULL)))
            return AVERROR(ENOMEM);
        avcodec_copy_context(st->codec, s->streams[i]->codec);
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        st->time_base = s->streams[i]->time_base;
    }
    hls->start_pos = 0;

    return 0;
}

/* Create a new segment and append it to the segment list */
static int hls_append_segment(HLSContext *hls, double duration, int64_t pos,
                              int64_t size)
{
    HLSSegment *en = av_malloc(sizeof(*en));

    if (!en)
        return AVERROR(ENOMEM);

    av_strlcpy(en->filename, av_basename(hls->avf->filename), sizeof(en->filename));

    en->duration = duration;
    en->pos      = pos;
    en->size     = size;
    en->next     = NULL;

    if (!hls->segments)
        hls->segments = en;
    else
        hls->last_segment->next = en;

    hls->last_segment = en;

    if (hls->max_nb_segments && hls->nb_entries >= hls->max_nb_segments) {
        en = hls->segments;
        hls->segments = en->next;
        av_free(en);
    } else
        hls->nb_entries++;

    hls->sequence++;

    return 0;
}

static void hls_free_segments(HLSContext *hls)
{
    HLSSegment *p = hls->segments, *en;

    while(p) {
        en = p;
        p = p->next;
        av_free(en);
    }
}

static int hls_window(AVFormatContext *s, int last)
{
    HLSContext *hls = s->priv_data;
    HLSSegment *en;
    int target_duration = 0;
    int ret = 0;
    int64_t sequence = FFMAX(hls->start_sequence, hls->sequence - hls->nb_entries);
    int version = hls->flags & HLS_SINGLE_FILE ? 4 : 3;

    if ((ret = avio_open2(&hls->pb, s->filename, AVIO_FLAG_WRITE,
                          &s->interrupt_callback, NULL)) < 0)
        goto fail;

    for (en = hls->segments; en; en = en->next) {
        if (target_duration < en->duration)
            target_duration = ceil(en->duration);
    }

    avio_printf(hls->pb, "#EXTM3U\n");
    avio_printf(hls->pb, "#EXT-X-VERSION:%d\n", version);
    if (hls->allowcache == 0 || hls->allowcache == 1) {
        avio_printf(hls->pb, "#EXT-X-ALLOW-CACHE:%s\n", hls->allowcache == 0 ? "NO" : "YES");
    }
    avio_printf(hls->pb, "#EXT-X-TARGETDURATION:%d\n", target_duration);
    avio_printf(hls->pb, "#EXT-X-MEDIA-SEQUENCE:%"PRId64"\n", sequence);

    av_log(s, AV_LOG_VERBOSE, "EXT-X-MEDIA-SEQUENCE:%"PRId64"\n",
           sequence);

    for (en = hls->segments; en; en = en->next) {
        avio_printf(hls->pb, "#EXTINF:%f,\n", en->duration);
        if (hls->flags & HLS_SINGLE_FILE)
             avio_printf(hls->pb, "#EXT-X-BYTERANGE:%"PRIi64"@%"PRIi64"\n",
                         en->size, en->pos);
        if (hls->baseurl)
            avio_printf(hls->pb, "%s", hls->baseurl);
        avio_printf(hls->pb, "%s\n", en->filename);
    }

    if (last)
        avio_printf(hls->pb, "#EXT-X-ENDLIST\n");

fail:
    avio_closep(&hls->pb);
    return ret;
}

static int hls_start(AVFormatContext *s)
{
    HLSContext *c = s->priv_data;
    AVFormatContext *oc = c->avf;
    int err = 0;

    if (c->flags & HLS_SINGLE_FILE)
        av_strlcpy(oc->filename, c->basename,
                   sizeof(oc->filename));
    else
        if (av_get_frame_filename(oc->filename, sizeof(oc->filename),
                                  c->basename, c->wrap ? c->sequence % c->wrap : c->sequence) < 0) {
            av_log(oc, AV_LOG_ERROR, "Invalid segment filename template '%s'\n", c->basename);
            return AVERROR(EINVAL);
        }
    c->number++;

    if ((err = avio_open2(&oc->pb, oc->filename, AVIO_FLAG_WRITE,
                          &s->interrupt_callback, NULL)) < 0)
        return err;

    if (oc->oformat->priv_class && oc->priv_data)
        av_opt_set(oc->priv_data, "mpegts_flags", "resend_headers", 0);

    return 0;
}

static int hls_write_header(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    int ret, i;
    char *p;
    const char *pattern = "%d.ts";
    AVDictionary *options = NULL;
    int basename_size = strlen(s->filename) + strlen(pattern) + 1;

    hls->sequence       = hls->start_sequence;
    hls->recording_time = hls->time * AV_TIME_BASE;
    hls->start_pts      = AV_NOPTS_VALUE;

    if (hls->flags & HLS_SINGLE_FILE)
        pattern = ".ts";

    if (hls->format_options_str) {
        ret = av_dict_parse_string(&hls->format_options, hls->format_options_str, "=", ":", 0);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Could not parse format options list '%s'\n", hls->format_options_str);
            goto fail;
        }
    }

    for (i = 0; i < s->nb_streams; i++)
        hls->has_video +=
            s->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO;

    if (hls->has_video > 1)
        av_log(s, AV_LOG_WARNING,
               "More than a single video stream present, "
               "expect issues decoding it.\n");

    hls->oformat = av_guess_format("mpegts", NULL, NULL);

    if (!hls->oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }

    hls->basename = av_malloc(basename_size);

    if (!hls->basename) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    strcpy(hls->basename, s->filename);

    p = strrchr(hls->basename, '.');

    if (p)
        *p = '\0';

    av_strlcat(hls->basename, pattern, basename_size);

    if ((ret = hls_mux_init(s)) < 0)
        goto fail;

    if ((ret = hls_start(s)) < 0)
        goto fail;

    av_dict_copy(&options, hls->format_options, 0);
    ret = avformat_write_header(hls->avf, &options);
    if (av_dict_count(options)) {
        av_log(s, AV_LOG_ERROR, "Some of provided format options in '%s' are not recognized\n", hls->format_options_str);
        ret = AVERROR(EINVAL);
        goto fail;
    }
    av_assert0(s->nb_streams == hls->avf->nb_streams);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *inner_st  = hls->avf->streams[i];
        AVStream *outer_st = s->streams[i];
        avpriv_set_pts_info(outer_st, inner_st->pts_wrap_bits, inner_st->time_base.num, inner_st->time_base.den);
    }
fail:

    av_dict_free(&options);
    if (ret) {
        av_free(hls->basename);
        if (hls->avf)
            avformat_free_context(hls->avf);
    }
    return ret;
}

static int hls_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc = hls->avf;
    AVStream *st = s->streams[pkt->stream_index];
    int64_t end_pts = hls->recording_time * hls->number;
    int is_ref_pkt = 1;
    int ret, can_split = 1;

    if (hls->start_pts == AV_NOPTS_VALUE) {
        hls->start_pts = pkt->pts;
        hls->end_pts   = pkt->pts;
    }

    if (hls->has_video) {
        can_split = st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                    pkt->flags & AV_PKT_FLAG_KEY;
        is_ref_pkt = st->codec->codec_type == AVMEDIA_TYPE_VIDEO;
    }
    if (pkt->pts == AV_NOPTS_VALUE)
        is_ref_pkt = can_split = 0;

    if (is_ref_pkt)
        hls->duration = (double)(pkt->pts - hls->end_pts)
                                   * st->time_base.num / st->time_base.den;

    if (can_split && av_compare_ts(pkt->pts - hls->start_pts, st->time_base,
                                   end_pts, AV_TIME_BASE_Q) >= 0) {
        int64_t new_start_pos;
        av_write_frame(oc, NULL); /* Flush any buffered data */

        new_start_pos = avio_tell(hls->avf->pb);
        hls->size = new_start_pos - hls->start_pos;
        ret = hls_append_segment(hls, hls->duration, hls->start_pos, hls->size);
        hls->start_pos = new_start_pos;
        if (ret)
            return ret;

        hls->end_pts = pkt->pts;
        hls->duration = 0;

        if (hls->flags & HLS_SINGLE_FILE) {
            if (hls->avf->oformat->priv_class && hls->avf->priv_data)
                av_opt_set(hls->avf->priv_data, "mpegts_flags", "resend_headers", 0);
            hls->number++;
        } else {
            avio_close(oc->pb);

            ret = hls_start(s);
        }

        if (ret)
            return ret;

        oc = hls->avf;

        if ((ret = hls_window(s, 0)) < 0)
            return ret;
    }

    ret = ff_write_chained(oc, pkt->stream_index, pkt, s, 0);

    return ret;
}

static int hls_write_trailer(struct AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc = hls->avf;

    av_write_trailer(oc);
    hls->size = avio_tell(hls->avf->pb) - hls->start_pos;
    avio_closep(&oc->pb);
    av_free(hls->basename);
    hls_append_segment(hls, hls->duration, hls->start_pos, hls->size);
    avformat_free_context(oc);
    hls->avf = NULL;
    hls_window(s, 1);

    hls_free_segments(hls);
    avio_close(hls->pb);
    return 0;
}

#define OFFSET(x) offsetof(HLSContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"start_number",  "set first number in the sequence",        OFFSET(start_sequence),AV_OPT_TYPE_INT64,  {.i64 = 0},     0, INT64_MAX, E},
    {"hls_time",      "set segment length in seconds",           OFFSET(time),    AV_OPT_TYPE_FLOAT,  {.dbl = 2},     0, FLT_MAX, E},
    {"hls_list_size", "set maximum number of playlist entries",  OFFSET(max_nb_segments),    AV_OPT_TYPE_INT,    {.i64 = 5},     0, INT_MAX, E},
    {"hls_ts_options","set hls mpegts list of options for the container format used for hls", OFFSET(format_options_str), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
    {"hls_wrap",      "set number after which the index wraps",  OFFSET(wrap),    AV_OPT_TYPE_INT,    {.i64 = 0},     0, INT_MAX, E},
    {"hls_allow_cache", "explicitly set whether the client MAY (1) or MUST NOT (0) cache media segments", OFFSET(allowcache), AV_OPT_TYPE_INT, {.i64 = -1}, INT_MIN, INT_MAX, E},
    {"hls_base_url",  "url to prepend to each playlist entry",   OFFSET(baseurl), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E},
    {"hls_flags",     "set flags affecting HLS playlist and media file generation", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = 0 }, 0, UINT_MAX, E, "flags"},
    {"single_file",   "generate a single media file indexed with byte ranges", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_SINGLE_FILE }, 0, UINT_MAX,   E, "flags"},

    { NULL },
};

static const AVClass hls_class = {
    .class_name = "hls muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};


AVOutputFormat ff_hls_muxer = {
    .name           = "hls",
    .long_name      = NULL_IF_CONFIG_SMALL("Apple HTTP Live Streaming"),
    .extensions     = "m3u8",
    .priv_data_size = sizeof(HLSContext),
    .audio_codec    = AV_CODEC_ID_AAC,
    .video_codec    = AV_CODEC_ID_H264,
    .flags          = AVFMT_NOFILE | AVFMT_ALLOW_FLUSH,
    .write_header   = hls_write_header,
    .write_packet   = hls_write_packet,
    .write_trailer  = hls_write_trailer,
    .priv_class     = &hls_class,
};
