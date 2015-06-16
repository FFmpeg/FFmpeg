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

#include "config.h"
#include <float.h>
#include <stdint.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "libavutil/avassert.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"

#include "avformat.h"
#include "internal.h"
#include "os_support.h"

#define KEYSIZE 16
#define LINE_BUFFER_SIZE 1024

typedef struct HLSSegment {
    char filename[1024];
    double duration; /* in seconds */
    int64_t pos;
    int64_t size;

    char key_uri[LINE_BUFFER_SIZE + 1];
    char iv_string[KEYSIZE*2 + 1];

    struct HLSSegment *next;
} HLSSegment;

typedef enum HLSFlags {
    // Generate a single media file and use byte ranges in the playlist.
    HLS_SINGLE_FILE = (1 << 0),
    HLS_DELETE_SEGMENTS = (1 << 1),
    HLS_ROUND_DURATIONS = (1 << 2),
    HLS_DISCONT_START = (1 << 3),
    HLS_OMIT_ENDLIST = (1 << 4),
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
    char *segment_filename;

    int allowcache;
    int64_t recording_time;
    int has_video;
    int64_t start_pts;
    int64_t end_pts;
    double duration;      // last segment duration computed so far, in seconds
    int64_t start_pos;    // last segment starting position
    int64_t size;         // last segment size
    int nb_entries;
    int discontinuity_set;

    HLSSegment *segments;
    HLSSegment *last_segment;
    HLSSegment *old_segments;

    char *basename;
    char *baseurl;
    char *format_options_str;
    AVDictionary *format_options;

    char *key_info_file;
    char key_file[LINE_BUFFER_SIZE + 1];
    char key_uri[LINE_BUFFER_SIZE + 1];
    char key_string[KEYSIZE*2 + 1];
    char iv_string[KEYSIZE*2 + 1];
} HLSContext;

static int hls_delete_old_segments(HLSContext *hls) {

    HLSSegment *segment, *previous_segment = NULL;
    float playlist_duration = 0.0f;
    int ret = 0, path_size;
    char *dirname = NULL, *p, *path;

    segment = hls->segments;
    while (segment) {
        playlist_duration += segment->duration;
        segment = segment->next;
    }

    segment = hls->old_segments;
    while (segment) {
        playlist_duration -= segment->duration;
        previous_segment = segment;
        segment = previous_segment->next;
        if (playlist_duration <= -previous_segment->duration) {
            previous_segment->next = NULL;
            break;
        }
    }

    if (segment) {
        if (hls->segment_filename) {
            dirname = av_strdup(hls->segment_filename);
        } else {
            dirname = av_strdup(hls->avf->filename);
        }
        if (!dirname) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        p = (char *)av_basename(dirname);
        *p = '\0';
    }

    while (segment) {
        av_log(hls, AV_LOG_DEBUG, "deleting old segment %s\n",
                                  segment->filename);
        path_size = strlen(dirname) + strlen(segment->filename) + 1;
        path = av_malloc(path_size);
        if (!path) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        av_strlcpy(path, dirname, path_size);
        av_strlcat(path, segment->filename, path_size);
        if (unlink(path) < 0) {
            av_log(hls, AV_LOG_ERROR, "failed to delete old segment %s: %s\n",
                                     path, strerror(errno));
        }
        av_free(path);
        previous_segment = segment;
        segment = previous_segment->next;
        av_free(previous_segment);
    }

fail:
    av_free(dirname);

    return ret;
}

static int hls_encryption_start(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    int ret;
    AVIOContext *pb;
    uint8_t key[KEYSIZE];

    if ((ret = avio_open2(&pb, hls->key_info_file, AVIO_FLAG_READ,
                           &s->interrupt_callback, NULL)) < 0) {
        av_log(hls, AV_LOG_ERROR,
                "error opening key info file %s\n", hls->key_info_file);
        return ret;
    }

    ff_get_line(pb, hls->key_uri, sizeof(hls->key_uri));
    hls->key_uri[strcspn(hls->key_uri, "\r\n")] = '\0';

    ff_get_line(pb, hls->key_file, sizeof(hls->key_file));
    hls->key_file[strcspn(hls->key_file, "\r\n")] = '\0';

    ff_get_line(pb, hls->iv_string, sizeof(hls->iv_string));
    hls->iv_string[strcspn(hls->iv_string, "\r\n")] = '\0';

    avio_close(pb);

    if (!*hls->key_uri) {
        av_log(hls, AV_LOG_ERROR, "no key URI specified in key info file\n");
        return AVERROR(EINVAL);
    }

    if (!*hls->key_file) {
        av_log(hls, AV_LOG_ERROR, "no key file specified in key info file\n");
        return AVERROR(EINVAL);
    }

    if ((ret = avio_open2(&pb, hls->key_file, AVIO_FLAG_READ,
                           &s->interrupt_callback, NULL)) < 0) {
        av_log(hls, AV_LOG_ERROR, "error opening key file %s\n", hls->key_file);
        return ret;
    }

    ret = avio_read(pb, key, sizeof(key));
    avio_close(pb);
    if (ret != sizeof(key)) {
        av_log(hls, AV_LOG_ERROR, "error reading key file %s\n", hls->key_file);
        if (ret >= 0 || ret == AVERROR_EOF)
            ret = AVERROR(EINVAL);
        return ret;
    }
    ff_data_to_hex(hls->key_string, key, sizeof(key), 0);

    return 0;
}

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
    int ret;

    if (!en)
        return AVERROR(ENOMEM);

    av_strlcpy(en->filename, av_basename(hls->avf->filename), sizeof(en->filename));

    en->duration = duration;
    en->pos      = pos;
    en->size     = size;
    en->next     = NULL;

    if (hls->key_info_file) {
        av_strlcpy(en->key_uri, hls->key_uri, sizeof(en->key_uri));
        av_strlcpy(en->iv_string, hls->iv_string, sizeof(en->iv_string));
    }

    if (!hls->segments)
        hls->segments = en;
    else
        hls->last_segment->next = en;

    hls->last_segment = en;

    if (hls->max_nb_segments && hls->nb_entries >= hls->max_nb_segments) {
        en = hls->segments;
        hls->segments = en->next;
        if (en && hls->flags & HLS_DELETE_SEGMENTS &&
                !(hls->flags & HLS_SINGLE_FILE || hls->wrap)) {
            en->next = hls->old_segments;
            hls->old_segments = en;
            if ((ret = hls_delete_old_segments(hls)) < 0)
                return ret;
        } else
            av_free(en);
    } else
        hls->nb_entries++;

    hls->sequence++;

    return 0;
}

static void hls_free_segments(HLSSegment *p)
{
    HLSSegment *en;

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
    AVIOContext *out = NULL;
    char temp_filename[1024];
    int64_t sequence = FFMAX(hls->start_sequence, hls->sequence - hls->nb_entries);
    int version = hls->flags & HLS_SINGLE_FILE ? 4 : 3;
    const char *proto = avio_find_protocol_name(s->filename);
    int use_rename = proto && !strcmp(proto, "file");
    static unsigned warned_non_file;
    char *key_uri = NULL;
    char *iv_string = NULL;

    if (!use_rename && !warned_non_file++)
        av_log(s, AV_LOG_ERROR, "Cannot use rename on non file protocol, this may lead to races and temporarly partial files\n");

    snprintf(temp_filename, sizeof(temp_filename), use_rename ? "%s.tmp" : "%s", s->filename);
    if ((ret = avio_open2(&out, temp_filename, AVIO_FLAG_WRITE,
                          &s->interrupt_callback, NULL)) < 0)
        goto fail;

    for (en = hls->segments; en; en = en->next) {
        if (target_duration < en->duration)
            target_duration = ceil(en->duration);
    }

    hls->discontinuity_set = 0;
    avio_printf(out, "#EXTM3U\n");
    avio_printf(out, "#EXT-X-VERSION:%d\n", version);
    if (hls->allowcache == 0 || hls->allowcache == 1) {
        avio_printf(out, "#EXT-X-ALLOW-CACHE:%s\n", hls->allowcache == 0 ? "NO" : "YES");
    }
    avio_printf(out, "#EXT-X-TARGETDURATION:%d\n", target_duration);
    avio_printf(out, "#EXT-X-MEDIA-SEQUENCE:%"PRId64"\n", sequence);

    av_log(s, AV_LOG_VERBOSE, "EXT-X-MEDIA-SEQUENCE:%"PRId64"\n",
           sequence);
    if((hls->flags & HLS_DISCONT_START) && sequence==hls->start_sequence && hls->discontinuity_set==0 ){
        avio_printf(out, "#EXT-X-DISCONTINUITY\n");
        hls->discontinuity_set = 1;
    }
    for (en = hls->segments; en; en = en->next) {
        if (hls->key_info_file && (!key_uri || strcmp(en->key_uri, key_uri) ||
                                    av_strcasecmp(en->iv_string, iv_string))) {
            avio_printf(out, "#EXT-X-KEY:METHOD=AES-128,URI=\"%s\"", en->key_uri);
            if (*en->iv_string)
                avio_printf(out, ",IV=0x%s", en->iv_string);
            avio_printf(out, "\n");
            key_uri = en->key_uri;
            iv_string = en->iv_string;
        }

        if (hls->flags & HLS_ROUND_DURATIONS)
            avio_printf(out, "#EXTINF:%d,\n",  (int)round(en->duration));
        else
            avio_printf(out, "#EXTINF:%f,\n", en->duration);
        if (hls->flags & HLS_SINGLE_FILE)
             avio_printf(out, "#EXT-X-BYTERANGE:%"PRIi64"@%"PRIi64"\n",
                         en->size, en->pos);
        if (hls->baseurl)
            avio_printf(out, "%s", hls->baseurl);
        avio_printf(out, "%s\n", en->filename);
    }

    if (last && (hls->flags & HLS_OMIT_ENDLIST)==0)
        avio_printf(out, "#EXT-X-ENDLIST\n");

fail:
    avio_closep(&out);
    if (ret >= 0 && use_rename)
        ff_rename(temp_filename, s->filename, s);
    return ret;
}

static int hls_start(AVFormatContext *s)
{
    HLSContext *c = s->priv_data;
    AVFormatContext *oc = c->avf;
    AVDictionary *options = NULL;
    char *filename, iv_string[KEYSIZE*2 + 1];
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

    if (c->key_info_file) {
        if ((err = hls_encryption_start(s)) < 0)
            return err;
        if ((err = av_dict_set(&options, "encryption_key", c->key_string, 0))
                < 0)
            return err;
        err = av_strlcpy(iv_string, c->iv_string, sizeof(iv_string));
        if (!err)
            snprintf(iv_string, sizeof(iv_string), "%032"PRIx64, c->sequence);
        if ((err = av_dict_set(&options, "encryption_iv", iv_string, 0)) < 0)
            return err;

        filename = av_asprintf("crypto:%s", oc->filename);
        if (!filename) {
            av_dict_free(&options);
            return AVERROR(ENOMEM);
        }
        err = avio_open2(&oc->pb, filename, AVIO_FLAG_WRITE,
                         &s->interrupt_callback, &options);
        av_free(filename);
        av_dict_free(&options);
        if (err < 0)
            return err;
    } else
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
    int basename_size;

    hls->sequence       = hls->start_sequence;
    hls->recording_time = hls->time * AV_TIME_BASE;
    hls->start_pts      = AV_NOPTS_VALUE;

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

    if (hls->segment_filename) {
        hls->basename = av_strdup(hls->segment_filename);
        if (!hls->basename) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    } else {
        if (hls->flags & HLS_SINGLE_FILE)
            pattern = ".ts";

        basename_size = strlen(s->filename) + strlen(pattern) + 1;
        hls->basename = av_malloc(basename_size);
        if (!hls->basename) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        av_strlcpy(hls->basename, s->filename, basename_size);

        p = strrchr(hls->basename, '.');
        if (p)
            *p = '\0';
        av_strlcat(hls->basename, pattern, basename_size);
    }

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
    if (ret < 0) {
        av_freep(&hls->basename);
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
        if (ret < 0)
            return ret;

        hls->end_pts = pkt->pts;
        hls->duration = 0;

        if (hls->flags & HLS_SINGLE_FILE) {
            if (hls->avf->oformat->priv_class && hls->avf->priv_data)
                av_opt_set(hls->avf->priv_data, "mpegts_flags", "resend_headers", 0);
            hls->number++;
        } else {
            avio_closep(&oc->pb);

            ret = hls_start(s);
        }

        if (ret < 0)
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
    if (oc->pb) {
        hls->size = avio_tell(hls->avf->pb) - hls->start_pos;
        avio_closep(&oc->pb);
        hls_append_segment(hls, hls->duration, hls->start_pos, hls->size);
    }
    av_freep(&hls->basename);
    avformat_free_context(oc);
    hls->avf = NULL;
    hls_window(s, 1);

    hls_free_segments(hls->segments);
    hls_free_segments(hls->old_segments);
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
    {"hls_segment_filename", "filename template for segment files", OFFSET(segment_filename),   AV_OPT_TYPE_STRING, {.str = NULL},            0,       0,         E},
    {"hls_key_info_file",    "file with key URI and key file path", OFFSET(key_info_file),      AV_OPT_TYPE_STRING, {.str = NULL},            0,       0,         E},
    {"hls_flags",     "set flags affecting HLS playlist and media file generation", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = 0 }, 0, UINT_MAX, E, "flags"},
    {"single_file",   "generate a single media file indexed with byte ranges", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_SINGLE_FILE }, 0, UINT_MAX,   E, "flags"},
    {"delete_segments", "delete segment files that are no longer part of the playlist", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_DELETE_SEGMENTS }, 0, UINT_MAX,   E, "flags"},
    {"round_durations", "round durations in m3u8 to whole numbers", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_ROUND_DURATIONS }, 0, UINT_MAX,   E, "flags"},
    {"discont_start", "start the playlist with a discontinuity tag", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_DISCONT_START }, 0, UINT_MAX,   E, "flags"},
    {"omit_endlist", "Do not append an endlist when ending stream", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_OMIT_ENDLIST }, 0, UINT_MAX,   E, "flags"},

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
