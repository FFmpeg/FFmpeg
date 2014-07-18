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
 */

#include <stdint.h>
#include <string.h>

#include "avformat.h"
#include "avio_internal.h"
#include "matroska.h"

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"

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
        if (duration == NULL || atof(duration->value) < 0) continue;
        if (atof(duration->value) > max) max = atof(duration->value);
    }
    return max / 1000;
}

static void write_header(AVFormatContext *s)
{
    double min_buffer_time = 1.0;
    avio_printf(s->pb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    avio_printf(s->pb, "<MPD\n");
    avio_printf(s->pb, "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
    avio_printf(s->pb, "  xmlns=\"urn:mpeg:DASH:schema:MPD:2011\"\n");
    avio_printf(s->pb, "  xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011\"\n");
    avio_printf(s->pb, "  type=\"static\"\n");
    avio_printf(s->pb, "  mediaPresentationDuration=\"PT%gS\"\n",
                get_duration(s));
    avio_printf(s->pb, "  minBufferTime=\"PT%gS\"\n",
                min_buffer_time);
    avio_printf(s->pb, "  profiles=\"urn:webm:dash:profile:webm-on-demand:2012\"");
    avio_printf(s->pb, ">\n");
}

static void write_footer(AVFormatContext *s)
{
    avio_printf(s->pb, "</MPD>");
}

static int subsegment_alignment(AVFormatContext *s, AdaptationSet *as) {
    int i;
    AVDictionaryEntry *gold = av_dict_get(s->streams[as->streams[0]]->metadata,
                                          CUE_TIMESTAMPS, NULL, 0);
    if (gold == NULL) return 0;
    for (i = 1; i < as->nb_streams; i++) {
        AVDictionaryEntry *ts = av_dict_get(s->streams[as->streams[i]]->metadata,
                                            CUE_TIMESTAMPS, NULL, 0);
        if (ts == NULL || strncmp(gold->value, ts->value, strlen(gold->value))) return 0;
    }
    return 1;
}

static int bitstream_switching(AVFormatContext *s, AdaptationSet *as) {
    int i;
    AVDictionaryEntry *gold_track_num = av_dict_get(s->streams[as->streams[0]]->metadata,
                                                    TRACK_NUMBER, NULL, 0);
    AVCodecContext *gold_codec = s->streams[as->streams[0]]->codec;
    if (gold_track_num == NULL) return 0;
    for (i = 1; i < as->nb_streams; i++) {
        AVDictionaryEntry *track_num = av_dict_get(s->streams[as->streams[i]]->metadata,
                                                   TRACK_NUMBER, NULL, 0);
        AVCodecContext *codec = s->streams[as->streams[i]]->codec;
        if (track_num == NULL ||
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
 * Writes an Adaptation Set. Returns 0 on success and < 0 on failure.
 */
static int write_adaptation_set(AVFormatContext *s, int as_index)
{
    WebMDashMuxContext *w = s->priv_data;
    AdaptationSet *as = &w->as[as_index];
    AVCodecContext *codec = s->streams[as->streams[0]]->codec;
    int i;
    static const char boolean[2][6] = { "false", "true" };
    int subsegmentStartsWithSAP = 1;
    AVDictionaryEntry *lang;
    avio_printf(s->pb, "<AdaptationSet id=\"%s\"", as->id);
    avio_printf(s->pb, " mimeType=\"%s/webm\"",
                codec->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio");
    avio_printf(s->pb, " codecs=\"%s\"", get_codec_name(codec->codec_id));

    lang = av_dict_get(s->streams[as->streams[0]]->metadata, "language", NULL, 0);
    if (lang != NULL) avio_printf(s->pb, " lang=\"%s\"", lang->value);

    if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        avio_printf(s->pb, " width=\"%d\"", codec->width);
        avio_printf(s->pb, " height=\"%d\"", codec->height);
    } else {
        avio_printf(s->pb, " audioSamplingRate=\"%d\"", codec->sample_rate);
    }

    avio_printf(s->pb, " bitstreamSwitching=\"%s\"",
                boolean[bitstream_switching(s, as)]);
    avio_printf(s->pb, " subsegmentAlignment=\"%s\"",
                boolean[subsegment_alignment(s, as)]);

    for (i = 0; i < as->nb_streams; i++) {
        AVDictionaryEntry *kf = av_dict_get(s->streams[as->streams[i]]->metadata,
                                            CLUSTER_KEYFRAME, NULL, 0);
        if (kf == NULL || !strncmp(kf->value, "0", 1)) subsegmentStartsWithSAP = 0;
    }
    avio_printf(s->pb, " subsegmentStartsWithSAP=\"%d\"", subsegmentStartsWithSAP);
    avio_printf(s->pb, ">\n");

    for (i = 0; i < as->nb_streams; i++) {
        AVStream *stream = s->streams[as->streams[i]];
        AVDictionaryEntry *irange = av_dict_get(stream->metadata, INITIALIZATION_RANGE, NULL, 0);
        AVDictionaryEntry *cues_start = av_dict_get(stream->metadata, CUES_START, NULL, 0);
        AVDictionaryEntry *cues_end = av_dict_get(stream->metadata, CUES_END, NULL, 0);
        AVDictionaryEntry *filename = av_dict_get(stream->metadata, FILENAME, NULL, 0);
        AVDictionaryEntry *bandwidth = av_dict_get(stream->metadata, BANDWIDTH, NULL, 0);
        if (irange == NULL || cues_start == NULL || cues_end == NULL || filename == NULL ||
            bandwidth == NULL) {
            return -1;
        }
        avio_printf(s->pb, "<Representation id=\"%d\"", i);
        avio_printf(s->pb, " bandwidth=\"%s\"", bandwidth->value);
        avio_printf(s->pb, ">\n");
        avio_printf(s->pb, "<BaseURL>%s</BaseURL>\n", filename->value);
        avio_printf(s->pb, "<SegmentBase\n");
        avio_printf(s->pb, "  indexRange=\"%s-%s\">\n", cues_start->value, cues_end->value);
        avio_printf(s->pb, "<Initialization\n");
        avio_printf(s->pb, "  range=\"0-%s\" />\n", irange->value);
        avio_printf(s->pb, "</SegmentBase>\n");
        avio_printf(s->pb, "</Representation>\n");
    }
    avio_printf(s->pb, "</AdaptationSet>\n");
    return 0;
}

static int to_integer(char *p, int len)
{
    int ret;
    char *q = av_malloc(sizeof(char) * len);
    if (q == NULL) return -1;
    strncpy(q, p, len);
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
            w->as = av_realloc(w->as, sizeof(*w->as) * ++w->nb_as);
            if (w->as == NULL) return -1;
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
            if (as->streams == NULL) return -1;
            as->streams[as->nb_streams - 1] = to_integer(p, q - p);
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
    WebMDashMuxContext *w = s->priv_data;
    parse_adaptation_sets(s);
    write_header(s);
    avio_printf(s->pb, "<Period id=\"0\"");
    avio_printf(s->pb, " start=\"PT%gS\"", start);
    avio_printf(s->pb, " duration=\"PT%gS\"", get_duration(s));
    avio_printf(s->pb, " >\n");

    for (i = 0; i < w->nb_as; i++) {
        if (write_adaptation_set(s, i) < 0) return -1;
    }

    avio_printf(s->pb, "</Period>\n");
    write_footer(s);
    return 0;
}

static int webm_dash_manifest_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    return AVERROR_EOF;
}

static int webm_dash_manifest_write_trailer(AVFormatContext *s)
{
    WebMDashMuxContext *w = s->priv_data;
    int i;
    for (i = 0; i < w->nb_as; i++) {
        av_freep(&w->as[i].streams);
    }
    av_freep(&w->as);
    return 0;
}

#define OFFSET(x) offsetof(WebMDashMuxContext, x)
static const AVOption options[] = {
    { "adaptation_sets", "Adaptation sets. Syntax: id=0,streams=0,1,2 id=1,streams=3,4 and so on", OFFSET(adaptation_sets), AV_OPT_TYPE_STRING, { 0 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
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
