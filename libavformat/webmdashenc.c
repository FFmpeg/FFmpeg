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
} WebMDashMuxContext;

static const char *get_codec_name(int codec_id)
{
    return avcodec_descriptor_get(codec_id)->name;
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
    AVIOContext *pb = s->pb;
    double min_buffer_time = 1.0;
    avio_printf(pb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    avio_printf(pb, "<MPD\n");
    avio_printf(pb, "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
    avio_printf(pb, "  xmlns=\"urn:mpeg:DASH:schema:MPD:2011\"\n");
    avio_printf(pb, "  xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011\"\n");
    avio_printf(pb, "  type=\"%s\"\n", w->is_live ? "dynamic" : "static");
    if (!w->is_live) {
        avio_printf(pb, "  mediaPresentationDuration=\"PT%gS\"\n",
                    get_duration(s));
    }
    avio_printf(pb, "  minBufferTime=\"PT%gS\"\n", min_buffer_time);
    avio_printf(pb, "  profiles=\"%s\"%s",
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
        if (s->flags & AVFMT_FLAG_BITEXACT) {
            av_strlcpy(gmt_iso, "", 1);
        }
        avio_printf(pb, "  availabilityStartTime=\"%s\"\n", gmt_iso);
        avio_printf(pb, "  timeShiftBufferDepth=\"PT%gS\"\n", w->time_shift_buffer_depth);
        avio_printf(pb, "  minimumUpdatePeriod=\"PT%dS\"", w->minimum_update_period);
        avio_printf(pb, ">\n");
        if (w->utc_timing_url) {
            avio_printf(pb, "<UTCTiming\n");
            avio_printf(pb, "  schemeIdUri=\"urn:mpeg:dash:utc:http-iso:2014\"\n");
            avio_printf(pb, "  value=\"%s\"/>\n", w->utc_timing_url);
        }
    }
    return 0;
}

static void write_footer(AVFormatContext *s)
{
    avio_printf(s->pb, "</MPD>\n");
}

static int subsegment_alignment(AVFormatContext *s, const AdaptationSet *as)
{
    int i;
    AVDictionaryEntry *gold = av_dict_get(s->streams[as->streams[0]]->metadata,
                                          CUE_TIMESTAMPS, NULL, 0);
    if (!gold) return 0;
    for (i = 1; i < as->nb_streams; i++) {
        AVDictionaryEntry *ts = av_dict_get(s->streams[as->streams[i]]->metadata,
                                            CUE_TIMESTAMPS, NULL, 0);
        if (!ts || !av_strstart(ts->value, gold->value, NULL)) return 0;
    }
    return 1;
}

static int bitstream_switching(AVFormatContext *s, const AdaptationSet *as)
{
    int i;
    const AVStream *gold_st = s->streams[as->streams[0]];
    AVDictionaryEntry *gold_track_num = av_dict_get(gold_st->metadata,
                                                    TRACK_NUMBER, NULL, 0);
    AVCodecParameters *gold_par = gold_st->codecpar;
    if (!gold_track_num) return 0;
    for (i = 1; i < as->nb_streams; i++) {
        const AVStream *st = s->streams[as->streams[i]];
        AVDictionaryEntry *track_num = av_dict_get(st->metadata,
                                                   TRACK_NUMBER, NULL, 0);
        AVCodecParameters *par = st->codecpar;
        if (!track_num ||
            !av_strstart(track_num->value, gold_track_num->value, NULL) ||
            gold_par->codec_id != par->codec_id ||
            gold_par->extradata_size != par->extradata_size ||
            (par->extradata_size > 0 &&
             memcmp(gold_par->extradata, par->extradata, par->extradata_size))) {
            return 0;
        }
    }
    return 1;
}

/*
 * Writes a Representation within an Adaptation Set. Returns 0 on success and
 * < 0 on failure.
 */
static int write_representation(AVFormatContext *s, AVStream *st, char *id,
                                int output_width, int output_height,
                                int output_sample_rate)
{
    WebMDashMuxContext *w = s->priv_data;
    AVIOContext *pb = s->pb;
    const AVCodecParameters *par = st->codecpar;
    AVDictionaryEntry *bandwidth = av_dict_get(st->metadata, BANDWIDTH, NULL, 0);
    const char *bandwidth_str;
    avio_printf(pb, "<Representation id=\"%s\"", id);
    if (bandwidth) {
        bandwidth_str = bandwidth->value;
    } else if (w->is_live) {
        // if bandwidth for live was not provided, use a default
        bandwidth_str = (par->codec_type == AVMEDIA_TYPE_AUDIO) ? "128000" : "1000000";
    } else {
        return AVERROR(EINVAL);
    }
    avio_printf(pb, " bandwidth=\"%s\"", bandwidth_str);
    if (par->codec_type == AVMEDIA_TYPE_VIDEO && output_width)
        avio_printf(pb, " width=\"%d\"", par->width);
    if (par->codec_type == AVMEDIA_TYPE_VIDEO && output_height)
        avio_printf(pb, " height=\"%d\"", par->height);
    if (par->codec_type == AVMEDIA_TYPE_AUDIO && output_sample_rate)
        avio_printf(pb, " audioSamplingRate=\"%d\"", par->sample_rate);
    if (w->is_live) {
        // For live streams, Codec and Mime Type always go in the Representation tag.
        avio_printf(pb, " codecs=\"%s\"", get_codec_name(par->codec_id));
        avio_printf(pb, " mimeType=\"%s/webm\"",
                    par->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio");
        // For live streams, subsegments always start with key frames. So this
        // is always 1.
        avio_printf(pb, " startsWithSAP=\"1\"");
        avio_printf(pb, ">");
    } else {
        AVDictionaryEntry *irange = av_dict_get(st->metadata, INITIALIZATION_RANGE, NULL, 0);
        AVDictionaryEntry *cues_start = av_dict_get(st->metadata, CUES_START, NULL, 0);
        AVDictionaryEntry *cues_end = av_dict_get(st->metadata, CUES_END, NULL, 0);
        AVDictionaryEntry *filename = av_dict_get(st->metadata, FILENAME, NULL, 0);
        if (!irange || !cues_start || !cues_end || !filename)
            return AVERROR(EINVAL);

        avio_printf(pb, ">\n");
        avio_printf(pb, "<BaseURL>%s</BaseURL>\n", filename->value);
        avio_printf(pb, "<SegmentBase\n");
        avio_printf(pb, "  indexRange=\"%s-%s\">\n", cues_start->value, cues_end->value);
        avio_printf(pb, "<Initialization\n");
        avio_printf(pb, "  range=\"0-%s\" />\n", irange->value);
        avio_printf(pb, "</SegmentBase>\n");
    }
    avio_printf(pb, "</Representation>\n");
    return 0;
}

/*
 * Checks if width of all streams are the same. Returns 1 if true, 0 otherwise.
 */
static int check_matching_width(AVFormatContext *s, const AdaptationSet *as)
{
    int first_width, i;
    if (as->nb_streams < 2) return 1;
    first_width = s->streams[as->streams[0]]->codecpar->width;
    for (i = 1; i < as->nb_streams; i++)
        if (first_width != s->streams[as->streams[i]]->codecpar->width)
          return 0;
    return 1;
}

/*
 * Checks if height of all streams are the same. Returns 1 if true, 0 otherwise.
 */
static int check_matching_height(AVFormatContext *s, const AdaptationSet *as)
{
    int first_height, i;
    if (as->nb_streams < 2) return 1;
    first_height = s->streams[as->streams[0]]->codecpar->height;
    for (i = 1; i < as->nb_streams; i++)
        if (first_height != s->streams[as->streams[i]]->codecpar->height)
          return 0;
    return 1;
}

/*
 * Checks if sample rate of all streams are the same. Returns 1 if true, 0 otherwise.
 */
static int check_matching_sample_rate(AVFormatContext *s, const AdaptationSet *as)
{
    int first_sample_rate, i;
    if (as->nb_streams < 2) return 1;
    first_sample_rate = s->streams[as->streams[0]]->codecpar->sample_rate;
    for (i = 1; i < as->nb_streams; i++)
        if (first_sample_rate != s->streams[as->streams[i]]->codecpar->sample_rate)
          return 0;
    return 1;
}

static void free_adaptation_sets(AVFormatContext *s)
{
    WebMDashMuxContext *w = s->priv_data;
    int i;
    for (i = 0; i < w->nb_as; i++) {
        av_freep(&w->as[i].streams);
    }
    av_freep(&w->as);
    w->nb_as = 0;
}

/*
 * Parses a live header filename and returns the position of the '_' and '.'
 * delimiting <file_description> and <representation_id>.
 *
 * Name of the header file should conform to the following pattern:
 * <file_description>_<representation_id>.hdr where <file_description> can be
 * anything. The chunks should be named according to the following pattern:
 * <file_description>_<representation_id>_<chunk_number>.chk
 */
static int split_filename(char *filename, char **underscore_pos,
                          char **period_pos)
{
    *underscore_pos = strrchr(filename, '_');
    if (!*underscore_pos)
        return AVERROR(EINVAL);
    *period_pos = strchr(*underscore_pos, '.');
    if (!*period_pos)
        return AVERROR(EINVAL);
    return 0;
}

/*
 * Writes an Adaptation Set. Returns 0 on success and < 0 on failure.
 */
static int write_adaptation_set(AVFormatContext *s, int as_index)
{
    WebMDashMuxContext *w = s->priv_data;
    AdaptationSet *as = &w->as[as_index];
    const AVStream *st = s->streams[as->streams[0]];
    AVCodecParameters *par = st->codecpar;
    AVDictionaryEntry *lang;
    AVIOContext *pb = s->pb;
    int i;
    static const char boolean[2][6] = { "false", "true" };
    int subsegmentStartsWithSAP = 1;

    // Width, Height and Sample Rate will go in the AdaptationSet tag if they
    // are the same for all contained Representations. otherwise, they will go
    // on their respective Representation tag. For live streams, they always go
    // in the Representation tag.
    int width_in_as = 1, height_in_as = 1, sample_rate_in_as = 1;
    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
      width_in_as  = !w->is_live && check_matching_width (s, as);
      height_in_as = !w->is_live && check_matching_height(s, as);
    } else {
      sample_rate_in_as = !w->is_live && check_matching_sample_rate(s, as);
    }

    avio_printf(pb, "<AdaptationSet id=\"%s\"", as->id);
    avio_printf(pb, " mimeType=\"%s/webm\"",
                par->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio");
    avio_printf(pb, " codecs=\"%s\"", get_codec_name(par->codec_id));

    lang = av_dict_get(st->metadata, "language", NULL, 0);
    if (lang)
        avio_printf(pb, " lang=\"%s\"", lang->value);

    if (par->codec_type == AVMEDIA_TYPE_VIDEO && width_in_as)
        avio_printf(pb, " width=\"%d\"", par->width);
    if (par->codec_type == AVMEDIA_TYPE_VIDEO && height_in_as)
        avio_printf(pb, " height=\"%d\"", par->height);
    if (par->codec_type == AVMEDIA_TYPE_AUDIO && sample_rate_in_as)
        avio_printf(pb, " audioSamplingRate=\"%d\"", par->sample_rate);

    avio_printf(pb, " bitstreamSwitching=\"%s\"",
                boolean[bitstream_switching(s, as)]);
    avio_printf(pb, " subsegmentAlignment=\"%s\"",
                boolean[w->is_live || subsegment_alignment(s, as)]);

    for (i = 0; i < as->nb_streams; i++) {
        AVDictionaryEntry *kf = av_dict_get(s->streams[as->streams[i]]->metadata,
                                            CLUSTER_KEYFRAME, NULL, 0);
        if (!w->is_live && (!kf || !strncmp(kf->value, "0", 1))) subsegmentStartsWithSAP = 0;
    }
    avio_printf(pb, " subsegmentStartsWithSAP=\"%d\"", subsegmentStartsWithSAP);
    avio_printf(pb, ">\n");

    if (w->is_live) {
        AVDictionaryEntry *filename =
            av_dict_get(st->metadata, FILENAME, NULL, 0);
        char *underscore_pos, *period_pos;
        int ret;
        if (!filename)
            return AVERROR(EINVAL);
        ret = split_filename(filename->value, &underscore_pos, &period_pos);
        if (ret) return ret;
        *underscore_pos = '\0';
        avio_printf(pb, "<ContentComponent id=\"1\" type=\"%s\"/>\n",
                    par->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio");
        avio_printf(pb, "<SegmentTemplate");
        avio_printf(pb, " timescale=\"1000\"");
        avio_printf(pb, " duration=\"%d\"", w->chunk_duration);
        avio_printf(pb, " media=\"%s_$RepresentationID$_$Number$.chk\"",
                    filename->value);
        avio_printf(pb, " startNumber=\"%d\"", w->chunk_start_index);
        avio_printf(pb, " initialization=\"%s_$RepresentationID$.hdr\"",
                    filename->value);
        avio_printf(pb, "/>\n");
        *underscore_pos = '_';
    }

    for (i = 0; i < as->nb_streams; i++) {
        char buf[25], *representation_id = buf, *underscore_pos, *period_pos;
        AVStream *st = s->streams[as->streams[i]];
        int ret;
        if (w->is_live) {
            AVDictionaryEntry *filename =
                av_dict_get(st->metadata, FILENAME, NULL, 0);
            if (!filename)
                return AVERROR(EINVAL);
            ret = split_filename(filename->value, &underscore_pos, &period_pos);
            if (ret < 0)
                return ret;
            representation_id = underscore_pos + 1;
            *period_pos       = '\0';
        } else {
            snprintf(buf, sizeof(buf), "%d", w->representation_id++);
        }
        ret = write_representation(s, st, representation_id, !width_in_as,
                                   !height_in_as, !sample_rate_in_as);
        if (ret) return ret;
        if (w->is_live)
            *period_pos = '.';
    }
    avio_printf(s->pb, "</AdaptationSet>\n");
    return 0;
}

static int parse_adaptation_sets(AVFormatContext *s)
{
    WebMDashMuxContext *w = s->priv_data;
    char *p = w->adaptation_sets;
    char *q;
    enum { new_set, parsed_id, parsing_streams } state;
    if (!w->adaptation_sets) {
        av_log(s, AV_LOG_ERROR, "The 'adaptation_sets' option must be set.\n");
        return AVERROR(EINVAL);
    }
    // syntax id=0,streams=0,1,2 id=1,streams=3,4 and so on
    state = new_set;
    while (1) {
        if (*p == '\0') {
            if (state == new_set)
                break;
            else
                return AVERROR(EINVAL);
        } else if (state == new_set && *p == ' ') {
            p++;
            continue;
        } else if (state == new_set && !strncmp(p, "id=", 3)) {
            void *mem = av_realloc(w->as, sizeof(*w->as) * (w->nb_as + 1));
            const char *comma;
            if (mem == NULL)
                return AVERROR(ENOMEM);
            w->as = mem;
            ++w->nb_as;
            w->as[w->nb_as - 1].nb_streams = 0;
            w->as[w->nb_as - 1].streams = NULL;
            p += 3; // consume "id="
            q = w->as[w->nb_as - 1].id;
            comma = strchr(p, ',');
            if (!comma || comma - p >= sizeof(w->as[w->nb_as - 1].id)) {
                av_log(s, AV_LOG_ERROR, "'id' in 'adaptation_sets' is malformed.\n");
                return AVERROR(EINVAL);
            }
            while (*p != ',') *q++ = *p++;
            *q = 0;
            p++;
            state = parsed_id;
        } else if (state == parsed_id && !strncmp(p, "streams=", 8)) {
            p += 8; // consume "streams="
            state = parsing_streams;
        } else if (state == parsing_streams) {
            struct AdaptationSet *as = &w->as[w->nb_as - 1];
            int64_t num;
            int ret = av_reallocp_array(&as->streams, ++as->nb_streams,
                                        sizeof(*as->streams));
            if (ret < 0)
                return ret;
            num = strtoll(p, &q, 10);
            if (!av_isdigit(*p) || (*q != ' ' && *q != '\0' && *q != ',') ||
                num < 0 || num >= s->nb_streams) {
                av_log(s, AV_LOG_ERROR, "Invalid value for 'streams' in adapation_sets.\n");
                return AVERROR(EINVAL);
            }
            as->streams[as->nb_streams - 1] = num;
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

    for (unsigned i = 0; i < s->nb_streams; i++) {
        enum AVCodecID codec_id = s->streams[i]->codecpar->codec_id;
        if (codec_id != AV_CODEC_ID_VP8    && codec_id != AV_CODEC_ID_VP9 &&
            codec_id != AV_CODEC_ID_VORBIS && codec_id != AV_CODEC_ID_OPUS)
            return AVERROR(EINVAL);
    }

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

#define OFFSET(x) offsetof(WebMDashMuxContext, x)
static const AVOption options[] = {
    { "adaptation_sets", "Adaptation sets. Syntax: id=0,streams=0,1,2 id=1,streams=3,4 and so on", OFFSET(adaptation_sets), AV_OPT_TYPE_STRING, { 0 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "live", "create a live stream manifest", OFFSET(is_live), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM },
    { "chunk_start_index",  "start index of the chunk", OFFSET(chunk_start_index), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "chunk_duration_ms", "duration of each chunk (in milliseconds)", OFFSET(chunk_duration), AV_OPT_TYPE_INT, {.i64 = 1000}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "utc_timing_url", "URL of the page that will return the UTC timestamp in ISO format", OFFSET(utc_timing_url), AV_OPT_TYPE_STRING, { 0 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "time_shift_buffer_depth", "Smallest time (in seconds) shifting buffer for which any Representation is guaranteed to be available.", OFFSET(time_shift_buffer_depth), AV_OPT_TYPE_DOUBLE, { .dbl = 60.0 }, 1.0, DBL_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "minimum_update_period", "Minimum Update Period (in seconds) of the manifest.", OFFSET(minimum_update_period), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

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
    .priv_class        = &webm_dash_class,
};
