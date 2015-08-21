/*
 * WebM DASH Manifest XML muxer
 * Copyright (c) 2014 Vignesh Venkatasubramanian
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

/*
 * WebM DASH Specification:
 * https://sites.google.com/a/webmproject.org/wiki/adaptive-streaming/webm-dash-specification
 * ISO DASH Specification:
 * http://standards.iso.org/ittf/PubliclyAvailableStandards/c065274_ISO_IEC_23009-1_2014.zip
 */

#include <float.h>
#include <stdint.h>
#include <string.h>

#include "avformat.h"
#include "avio_internal.h"
#include "matroska.h"

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "libavutil/time_internal.h"

typedef struct AdaptationSet {
    char id[10];
    int *streams;
    int nb_streams;
} AdaptationSet;

typedef struct WebMDashMuxContext {
    const AVClass  *class;
    char *adaptation_sets;
    AdaptationSet *as;
    int nb_as;
    int representation_id;
    int is_live;
    int chunk_start_index;
    int chunk_duration;
    char *utc_timing_url;
    double time_shift_buffer_depth;
    int minimum_update_period;
    int debug_mode;
} WebMDashMuxContext;

static const char *get_codec_name(int codec_id)
{
    switch (codec_id) {
        case AV_CODEC_ID_VP8:
            return "vp8";
        case AV_CODEC_ID_VP9:
            return "vp9";
        case AV_CODEC_ID_VORBIS:
            return "vorbis";
        case AV_CODEC_ID_OPUS:
            return "opus";
    }
    return NULL;
}

static double get_duration(AVFormatContext *s)
{
    int i = 0;
    double max = 0.0;
    for (i = 0; i < s->nb_streams; i++) {
        AVDictionaryEntry *duration = av_dict_get(s->streams[i]->metadata,
                                                  DURATION, NULL, 0);
        if (!duration || atof(duration->value) < 0) continue;
        if (atof(duration->value) > max) max = atof(duration->value);
    }
    return max / 1000;
}

static int write_header(AVFormatContext *s)
{
    WebMDashMuxContext *w = s->priv_data;
    double min_buffer_time = 1.0;
    avio_printf(s->pb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    avio_printf(s->pb, "<MPD\n");
    avio_printf(s->pb, "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
    avio_printf(s->pb, "  xmlns=\"urn:mpeg:DASH:schema:MPD:2011\"\n");
    avio_printf(s->pb, "  xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011\"\n");
    avio_printf(s->pb, "  type=\"%s\"\n", w->is_live ? "dynamic" : "static");
    if (!w->is_live) {
        avio_printf(s->pb, "  mediaPresentationDuration=\"PT%gS\"\n",
                    get_duration(s));
    }
    avio_printf(s->pb, "  minBufferTime=\"PT%gS\"\n", min_buffer_time);
    avio_printf(s->pb, "  profiles=\"%s\"%s",
                w->is_live ? "urn:mpeg:dash:profile:isoff-live:2011" : "urn:webm:dash:profile:webm-on-demand:2012",
                w->is_live ? "\n" : ">\n");
    if (w->is_live) {
        time_t local_time = time(NULL);
        struct tm gmt_buffer;
        struct tm *gmt = gmtime_r(&local_time, &gmt_buffer);
        char gmt_iso[21];
        if (!strftime(gmt_iso, 21, "%Y-%m-%dT%H:%M:%SZ", gmt)) {
            return AVERROR_UNKNOWN;
        }
        if (w->debug_mode) {
            av_strlcpy(gmt_iso, "", 1);
        }
        avio_printf(s->pb, "  availabilityStartTime=\"%s\"\n", gmt_iso);
        avio_printf(s->pb, "  timeShiftBufferDepth=\"PT%gS\"\n", w->time_shift_buffer_depth);
        avio_printf(s->pb, "  minimumUpdatePeriod=\"PT%dS\"", w->minimum_update_period);
        avio_printf(s->pb, ">\n");
        if (w->utc_timing_url) {
            avio_printf(s->pb, "<UTCTiming\n");
            avio_printf(s->pb, "  schemeIdUri=\"urn:mpeg:dash:utc:http-iso:2014\"\n");
            avio_printf(s->pb, "  value=\"%s\"/>\n", w->utc_timing_url);
        }
    }
    return 0;
}

static void write_footer(AVFormatContext *s)
{
    avio_printf(s->pb, "</MPD>\n");
}

static int subsegment_alignment(AVFormatContext *s, AdaptationSet *as) {
    int i;
    AVDictionaryEntry *gold = av_dict_get(s->streams[as->streams[0]]->metadata,
                                          CUE_TIMESTAMPS, NULL, 0);
    if (!gold) return 0;
    for (i = 1; i < as->nb_streams; i++) {
        AVDictionaryEntry *ts = av_dict_get(s->streams[as->streams[i]]->metadata,
                                            CUE_TIMESTAMPS, NULL, 0);
        if (!ts || strncmp(gold->value, ts->value, strlen(gold->value))) return 0;
    }
    return 1;
}

static int bitstream_switching(AVFormatContext *s, AdaptationSet *as) {
    int i;
    AVDictionaryEntry *gold_track_num = av_dict_get(s->streams[as->streams[0]]->metadata,
                                                    TRACK_NUMBER, NULL, 0);
    AVCodecContext *gold_codec = s->streams[as->streams[0]]->codec;
    if (!gold_track_num) return 0;
    for (i = 1; i < as->nb_streams; i++) {
        AVDictionaryEntry *track_num = av_dict_get(s->streams[as->streams[i]]->metadata,
                                                   TRACK_NUMBER, NULL, 0);
        AVCodecContext *codec = s->streams[as->streams[i]]->codec;
        if (!track_num ||
            strncmp(gold_track_num->value, track_num->value, strlen(gold_track_num->value)) ||
            gold_codec->codec_id != codec->codec_id ||
            gold_codec->extradata_size != codec->extradata_size ||
            memcmp(gold_codec->extradata, codec->extradata, codec->extradata_size)) {
            return 0;
        }
    }
    return 1;
}

/*
 * Writes a Representation within an Adaptation Set. Returns 0 on success and
 * < 0 on failure.
 */
static int write_representation(AVFormatContext *s, AVStream *stream, char *id,
                                int output_width, int output_height,
                                int output_sample_rate) {
    WebMDashMuxContext *w = s->priv_data;
    AVDictionaryEntry *irange = av_dict_get(stream->metadata, INITIALIZATION_RANGE, NULL, 0);
    AVDictionaryEntry *cues_start = av_dict_get(stream->metadata, CUES_START, NULL, 0);
    AVDictionaryEntry *cues_end = av_dict_get(stream->metadata, CUES_END, NULL, 0);
    AVDictionaryEntry *filename = av_dict_get(stream->metadata, FILENAME, NULL, 0);
    AVDictionaryEntry *bandwidth = av_dict_get(stream->metadata, BANDWIDTH, NULL, 0);
    if ((w->is_live && (!filename)) ||
        (!w->is_live && (!irange || !cues_start || !cues_end || !filename || !bandwidth))) {
        return AVERROR_INVALIDDATA;
    }
    avio_printf(s->pb, "<Representation id=\"%s\"", id);
    // FIXME: For live, This should be obtained from the input file or as an AVOption.
    avio_printf(s->pb, " bandwidth=\"%s\"",
                w->is_live ? (stream->codec->codec_type == AVMEDIA_TYPE_AUDIO ? "128000" : "1000000") : bandwidth->value);
    if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO && output_width)
        avio_printf(s->pb, " width=\"%d\"", stream->codec->width);
    if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO && output_height)
        avio_printf(s->pb, " height=\"%d\"", stream->codec->height);
    if (stream->codec->codec_type = AVMEDIA_TYPE_AUDIO && output_sample_rate)
        avio_printf(s->pb, " audioSamplingRate=\"%d\"", stream->codec->sample_rate);
    if (w->is_live) {
        // For live streams, Codec and Mime Type always go in the Representation tag.
        avio_printf(s->pb, " codecs=\"%s\"", get_codec_name(stream->codec->codec_id));
        avio_printf(s->pb, " mimeType=\"%s/webm\"",
                    stream->codec->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio");
        // For live streams, subsegments always start with key frames. So this
        // is always 1.
        avio_printf(s->pb, " startsWithSAP=\"1\"");
        avio_printf(s->pb, ">");
    } else {
        avio_printf(s->pb, ">\n");
        avio_printf(s->pb, "<BaseURL>%s</BaseURL>\n", filename->value);
        avio_printf(s->pb, "<SegmentBase\n");
        avio_printf(s->pb, "  indexRange=\"%s-%s\">\n", cues_start->value, cues_end->value);
        avio_printf(s->pb, "<Initialization\n");
        avio_printf(s->pb, "  range=\"0-%s\" />\n", irange->value);
        avio_printf(s->pb, "</SegmentBase>\n");
    }
    avio_printf(s->pb, "</Representation>\n");
    return 0;
}

/*
 * Checks if width of all streams are the same. Returns 1 if true, 0 otherwise.
 */
static int check_matching_width(AVFormatContext *s, AdaptationSet *as) {
    int first_width, i;
    if (as->nb_streams < 2) return 1;
    first_width = s->streams[as->streams[0]]->codec->width;
    for (i = 1; i < as->nb_streams; i++)
        if (first_width != s->streams[as->streams[i]]->codec->width)
          return 0;
    return 1;
}

/*
 * Checks if height of all streams are the same. Returns 1 if true, 0 otherwise.
 */
static int check_matching_height(AVFormatContext *s, AdaptationSet *as) {
    int first_height, i;
    if (as->nb_streams < 2) return 1;
    first_height = s->streams[as->streams[0]]->codec->height;
    for (i = 1; i < as->nb_streams; i++)
        if (first_height != s->streams[as->streams[i]]->codec->height)
          return 0;
    return 1;
}

/*
 * Checks if sample rate of all streams are the same. Returns 1 if true, 0 otherwise.
 */
static int check_matching_sample_rate(AVFormatContext *s, AdaptationSet *as) {
    int first_sample_rate, i;
    if (as->nb_streams < 2) return 1;
    first_sample_rate = s->streams[as->streams[0]]->codec->sample_rate;
    for (i = 1; i < as->nb_streams; i++)
        if (first_sample_rate != s->streams[as->streams[i]]->codec->sample_rate)
          return 0;
    return 1;
}

static void free_adaptation_sets(AVFormatContext *s) {
    WebMDashMuxContext *w = s->priv_data;
    int i;
    for (i = 0; i < w->nb_as; i++) {
        av_freep(&w->as[i].streams);
    }
    av_freep(&w->as);
    w->nb_as = 0;
}

/*
 * Parses a live header filename and computes the representation id,
 * initialization pattern and the media pattern. Pass NULL if you don't want to
 * compute any of those 3. Returns 0 on success and non-zero on failure.
 *
 * Name of the header file should conform to the following pattern:
 * <file_description>_<representation_id>.hdr where <file_description> can be
 * anything. The chunks should be named according to the following pattern:
 * <file_description>_<representation_id>_<chunk_number>.chk
 */
static int parse_filename(char *filename, char **representation_id,
                          char **initialization_pattern, char **media_pattern) {
    char *underscore_pos = NULL;
    char *period_pos = NULL;
    char *temp_pos = NULL;
    char *filename_str = av_strdup(filename);
    if (!filename_str) return AVERROR(ENOMEM);
    temp_pos = av_stristr(filename_str, "_");
    while (temp_pos) {
        underscore_pos = temp_pos + 1;
        temp_pos = av_stristr(temp_pos + 1, "_");
    }
    if (!underscore_pos) return AVERROR_INVALIDDATA;
    period_pos = av_stristr(underscore_pos, ".");
    if (!period_pos) return AVERROR_INVALIDDATA;
    *(underscore_pos - 1) = 0;
    if (representation_id) {
        *representation_id = av_malloc(period_pos - underscore_pos + 1);
        if (!(*representation_id)) return AVERROR(ENOMEM);
        av_strlcpy(*representation_id, underscore_pos, period_pos - underscore_pos + 1);
    }
    if (initialization_pattern) {
        *initialization_pattern = av_asprintf("%s_$RepresentationID$.hdr",
                                              filename_str);
        if (!(*initialization_pattern)) return AVERROR(ENOMEM);
    }
    if (media_pattern) {
        *media_pattern = av_asprintf("%s_$RepresentationID$_$Number$.chk",
                                     filename_str);
        if (!(*media_pattern)) return AVERROR(ENOMEM);
    }
    av_free(filename_str);
    return 0;
}

/*
 * Writes an Adaptation Set. Returns 0 on success and < 0 on failure.
 */
static int write_adaptation_set(AVFormatContext *s, int as_index)
{
    WebMDashMuxContext *w = s->priv_data;
    AdaptationSet *as = &w->as[as_index];
    AVCodecContext *codec = s->streams[as->streams[0]]->codec;
    AVDictionaryEntry *lang;
    int i;
    static const char boolean[2][6] = { "false", "true" };
    int subsegmentStartsWithSAP = 1;

    // Width, Height and Sample Rate will go in the AdaptationSet tag if they
    // are the same for all contained Representations. otherwise, they will go
    // on their respective Representation tag. For live streams, they always go
    // in the Representation tag.
    int width_in_as = 1, height_in_as = 1, sample_rate_in_as = 1;
    if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
      width_in_as = !w->is_live && check_matching_width(s, as);
      height_in_as = !w->is_live && check_matching_height(s, as);
    } else {
      sample_rate_in_as = !w->is_live && check_matching_sample_rate(s, as);
    }

    avio_printf(s->pb, "<AdaptationSet id=\"%s\"", as->id);
    avio_printf(s->pb, " mimeType=\"%s/webm\"",
                codec->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio");
    avio_printf(s->pb, " codecs=\"%s\"", get_codec_name(codec->codec_id));

    lang = av_dict_get(s->streams[as->streams[0]]->metadata, "language", NULL, 0);
    if (lang) avio_printf(s->pb, " lang=\"%s\"", lang->value);

    if (codec->codec_type == AVMEDIA_TYPE_VIDEO && width_in_as)
        avio_printf(s->pb, " width=\"%d\"", codec->width);
    if (codec->codec_type == AVMEDIA_TYPE_VIDEO && height_in_as)
        avio_printf(s->pb, " height=\"%d\"", codec->height);
    if (codec->codec_type == AVMEDIA_TYPE_AUDIO && sample_rate_in_as)
        avio_printf(s->pb, " audioSamplingRate=\"%d\"", codec->sample_rate);

    avio_printf(s->pb, " bitstreamSwitching=\"%s\"",
                boolean[bitstream_switching(s, as)]);
    avio_printf(s->pb, " subsegmentAlignment=\"%s\"",
                boolean[w->is_live || subsegment_alignment(s, as)]);

    for (i = 0; i < as->nb_streams; i++) {
        AVDictionaryEntry *kf = av_dict_get(s->streams[as->streams[i]]->metadata,
                                            CLUSTER_KEYFRAME, NULL, 0);
        if (!w->is_live && (!kf || !strncmp(kf->value, "0", 1))) subsegmentStartsWithSAP = 0;
    }
    avio_printf(s->pb, " subsegmentStartsWithSAP=\"%d\"", subsegmentStartsWithSAP);
    avio_printf(s->pb, ">\n");

    if (w->is_live) {
        AVDictionaryEntry *filename =
            av_dict_get(s->streams[as->streams[0]]->metadata, FILENAME, NULL, 0);
        char *initialization_pattern = NULL;
        char *media_pattern = NULL;
        int ret = parse_filename(filename->value, NULL, &initialization_pattern,
                                 &media_pattern);
        if (ret) return ret;
        avio_printf(s->pb, "<ContentComponent id=\"1\" type=\"%s\"/>\n",
                    codec->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio");
        avio_printf(s->pb, "<SegmentTemplate");
        avio_printf(s->pb, " timescale=\"1000\"");
        avio_printf(s->pb, " duration=\"%d\"", w->chunk_duration);
        avio_printf(s->pb, " media=\"%s\"", media_pattern);
        avio_printf(s->pb, " startNumber=\"%d\"", w->chunk_start_index);
        avio_printf(s->pb, " initialization=\"%s\"", initialization_pattern);
        avio_printf(s->pb, "/>\n");
        av_free(initialization_pattern);
        av_free(media_pattern);
    }

    for (i = 0; i < as->nb_streams; i++) {
        char *representation_id = NULL;
        int ret;
        if (w->is_live) {
            AVDictionaryEntry *filename =
                av_dict_get(s->streams[as->streams[i]]->metadata, FILENAME, NULL, 0);
            if (!filename)
                return AVERROR(EINVAL);
            if (ret = parse_filename(filename->value, &representation_id, NULL, NULL))
                return ret;
        } else {
            representation_id = av_asprintf("%d", w->representation_id++);
            if (!representation_id) return AVERROR(ENOMEM);
        }
        ret = write_representation(s, s->streams[as->streams[i]],
                                   representation_id, !width_in_as,
                                   !height_in_as, !sample_rate_in_as);
        av_free(representation_id);
        if (ret) return ret;
    }
    avio_printf(s->pb, "</AdaptationSet>\n");
    return 0;
}

static int to_integer(char *p, int len)
{
    int ret;
    char *q = av_malloc(sizeof(char) * len);
    if (!q)
        return AVERROR(ENOMEM);
    av_strlcpy(q, p, len);
    ret = atoi(q);
    av_free(q);
    return ret;
}

static int parse_adaptation_sets(AVFormatContext *s)
{
    WebMDashMuxContext *w = s->priv_data;
    char *p = w->adaptation_sets;
    char *q;
    enum { new_set, parsed_id, parsing_streams } state;
    // syntax id=0,streams=0,1,2 id=1,streams=3,4 and so on
    state = new_set;
    while (p < w->adaptation_sets + strlen(w->adaptation_sets)) {
        if (*p == ' ')
            continue;
        else if (state == new_set && !strncmp(p, "id=", 3)) {
            void *mem = av_realloc(w->as, sizeof(*w->as) * (w->nb_as + 1));
            if (mem == NULL)
                return AVERROR(ENOMEM);
            w->as = mem;
            ++w->nb_as;
            w->as[w->nb_as - 1].nb_streams = 0;
            w->as[w->nb_as - 1].streams = NULL;
            p += 3; // consume "id="
            q = w->as[w->nb_as - 1].id;
            while (*p != ',') *q++ = *p++;
            *q = 0;
            p++;
            state = parsed_id;
        } else if (state == parsed_id && !strncmp(p, "streams=", 8)) {
            p += 8; // consume "streams="
            state = parsing_streams;
        } else if (state == parsing_streams) {
            struct AdaptationSet *as = &w->as[w->nb_as - 1];
            q = p;
            while (*q != '\0' && *q != ',' && *q != ' ') q++;
            as->streams = av_realloc(as->streams, sizeof(*as->streams) * ++as->nb_streams);
            if (as->streams == NULL)
                return AVERROR(ENOMEM);
            as->streams[as->nb_streams - 1] = to_integer(p, q - p + 1);
            if (as->streams[as->nb_streams - 1] < 0) return -1;
            if (*q == '\0') break;
            if (*q == ' ') state = new_set;
            p = ++q;
        } else {
            return -1;
        }
    }
    return 0;
}

static int webm_dash_manifest_write_header(AVFormatContext *s)
{
    int i;
    double start = 0.0;
    int ret;
    WebMDashMuxContext *w = s->priv_data;
    ret = parse_adaptation_sets(s);
    if (ret < 0) {
        goto fail;
    }
    ret = write_header(s);
    if (ret < 0) {
        goto fail;
    }
    avio_printf(s->pb, "<Period id=\"0\"");
    avio_printf(s->pb, " start=\"PT%gS\"", start);
    if (!w->is_live) {
        avio_printf(s->pb, " duration=\"PT%gS\"", get_duration(s));
    }
    avio_printf(s->pb, " >\n");

    for (i = 0; i < w->nb_as; i++) {
        ret = write_adaptation_set(s, i);
        if (ret < 0) {
            goto fail;
        }
    }

    avio_printf(s->pb, "</Period>\n");
    write_footer(s);
fail:
    free_adaptation_sets(s);
    return ret < 0 ? ret : 0;
}

static int webm_dash_manifest_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    return AVERROR_EOF;
}

static int webm_dash_manifest_write_trailer(AVFormatContext *s)
{
    free_adaptation_sets(s);
    return 0;
}

#define OFFSET(x) offsetof(WebMDashMuxContext, x)
static const AVOption options[] = {
    { "adaptation_sets", "Adaptation sets. Syntax: id=0,streams=0,1,2 id=1,streams=3,4 and so on", OFFSET(adaptation_sets), AV_OPT_TYPE_STRING, { 0 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "debug_mode", "[private option - users should never set this]. set this to 1 to create deterministic output", OFFSET(debug_mode), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM },
    { "live", "set this to 1 to create a live stream manifest", OFFSET(is_live), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM },
    { "chunk_start_index",  "start index of the chunk", OFFSET(chunk_start_index), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "chunk_duration_ms", "duration of each chunk (in milliseconds)", OFFSET(chunk_duration), AV_OPT_TYPE_INT, {.i64 = 1000}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "utc_timing_url", "URL of the page that will return the UTC timestamp in ISO format", OFFSET(utc_timing_url), AV_OPT_TYPE_STRING, { 0 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "time_shift_buffer_depth", "Smallest time (in seconds) shifting buffer for which any Representation is guaranteed to be available.", OFFSET(time_shift_buffer_depth), AV_OPT_TYPE_DOUBLE, { .dbl = 60.0 }, 1.0, DBL_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "minimum_update_period", "Minimum Update Period (in seconds) of the manifest.", OFFSET(minimum_update_period), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

#if CONFIG_WEBM_DASH_MANIFEST_MUXER
static const AVClass webm_dash_class = {
    .class_name = "WebM DASH Manifest muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_webm_dash_manifest_muxer = {
    .name              = "webm_dash_manifest",
    .long_name         = NULL_IF_CONFIG_SMALL("WebM DASH Manifest"),
    .mime_type         = "application/xml",
    .extensions        = "xml",
    .priv_data_size    = sizeof(WebMDashMuxContext),
    .write_header      = webm_dash_manifest_write_header,
    .write_packet      = webm_dash_manifest_write_packet,
    .write_trailer     = webm_dash_manifest_write_trailer,
    .priv_class        = &webm_dash_class,
};
#endif
