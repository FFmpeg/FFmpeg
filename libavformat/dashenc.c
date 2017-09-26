/*
 * MPEG-DASH ISO BMFF segmenter
 * Copyright (c) 2014 Martin Storsjo
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
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "libavutil/avassert.h"
#include "libavutil/avutil.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/rational.h"
#include "libavutil/time_internal.h"

#include "avc.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "isom.h"
#include "os_support.h"
#include "url.h"
#include "dash.h"

typedef struct Segment {
    char file[1024];
    int64_t start_pos;
    int range_length, index_length;
    int64_t time;
    int duration;
    int n;
} Segment;

typedef struct AdaptationSet {
    char id[10];
    enum AVMediaType media_type;
    AVDictionary *metadata;
} AdaptationSet;

typedef struct OutputStream {
    AVFormatContext *ctx;
    int ctx_inited, as_idx;
    AVIOContext *out;
    char format_name[8];
    int packets_written;
    char initfile[1024];
    int64_t init_start_pos, pos;
    int init_range_length;
    int nb_segments, segments_size, segment_index;
    Segment **segments;
    int64_t first_pts, start_pts, max_pts;
    int64_t last_dts;
    int bit_rate;
    char bandwidth_str[64];

    char codec_str[100];
} OutputStream;

typedef struct DASHContext {
    const AVClass *class;  /* Class for private options. */
    char *adaptation_sets;
    AdaptationSet *as;
    int nb_as;
    int window_size;
    int extra_window_size;
    int min_seg_duration;
    int remove_at_exit;
    int use_template;
    int use_timeline;
    int single_file;
    OutputStream *streams;
    int has_video;
    int64_t last_duration;
    int64_t total_duration;
    char availability_start_time[100];
    char dirname[1024];
    const char *single_file_name;
    const char *init_seg_name;
    const char *media_seg_name;
    AVRational min_frame_rate, max_frame_rate;
    int ambiguous_frame_rate;
    const char *utc_timing_url;
} DASHContext;

static struct codec_string {
    int id;
    const char *str;
} codecs[] = {
    { AV_CODEC_ID_VP8, "vp8" },
    { AV_CODEC_ID_VP9, "vp9" },
    { AV_CODEC_ID_VORBIS, "vorbis" },
    { AV_CODEC_ID_OPUS, "opus" },
    { 0, NULL }
};

static void set_codec_str(AVFormatContext *s, AVCodecParameters *par,
                          char *str, int size)
{
    const AVCodecTag *tags[2] = { NULL, NULL };
    uint32_t tag;
    int i;

    // common Webm codecs are not part of RFC 6381
    for (i = 0; codecs[i].id; i++)
        if (codecs[i].id == par->codec_id) {
            av_strlcpy(str, codecs[i].str, size);
            return;
        }

    // for codecs part of RFC 6381
    if (par->codec_type == AVMEDIA_TYPE_VIDEO)
        tags[0] = ff_codec_movvideo_tags;
    else if (par->codec_type == AVMEDIA_TYPE_AUDIO)
        tags[0] = ff_codec_movaudio_tags;
    else
        return;

    tag = av_codec_get_tag(tags, par->codec_id);
    if (!tag)
        return;
    if (size < 5)
        return;

    AV_WL32(str, tag);
    str[4] = '\0';
    if (!strcmp(str, "mp4a") || !strcmp(str, "mp4v")) {
        uint32_t oti;
        tags[0] = ff_mp4_obj_type;
        oti = av_codec_get_tag(tags, par->codec_id);
        if (oti)
            av_strlcatf(str, size, ".%02"PRIx32, oti);
        else
            return;

        if (tag == MKTAG('m', 'p', '4', 'a')) {
            if (par->extradata_size >= 2) {
                int aot = par->extradata[0] >> 3;
                if (aot == 31)
                    aot = ((AV_RB16(par->extradata) >> 5) & 0x3f) + 32;
                av_strlcatf(str, size, ".%d", aot);
            }
        } else if (tag == MKTAG('m', 'p', '4', 'v')) {
            // Unimplemented, should output ProfileLevelIndication as a decimal number
            av_log(s, AV_LOG_WARNING, "Incomplete RFC 6381 codec string for mp4v\n");
        }
    } else if (!strcmp(str, "avc1")) {
        uint8_t *tmpbuf = NULL;
        uint8_t *extradata = par->extradata;
        int extradata_size = par->extradata_size;
        if (!extradata_size)
            return;
        if (extradata[0] != 1) {
            AVIOContext *pb;
            if (avio_open_dyn_buf(&pb) < 0)
                return;
            if (ff_isom_write_avcc(pb, extradata, extradata_size) < 0) {
                ffio_free_dyn_buf(&pb);
                return;
            }
            extradata_size = avio_close_dyn_buf(pb, &extradata);
            tmpbuf = extradata;
        }

        if (extradata_size >= 4)
            av_strlcatf(str, size, ".%02x%02x%02x",
                        extradata[1], extradata[2], extradata[3]);
        av_free(tmpbuf);
    }
}

static int flush_dynbuf(OutputStream *os, int *range_length)
{
    uint8_t *buffer;

    if (!os->ctx->pb) {
        return AVERROR(EINVAL);
    }

    // flush
    av_write_frame(os->ctx, NULL);
    avio_flush(os->ctx->pb);

    // write out to file
    *range_length = avio_close_dyn_buf(os->ctx->pb, &buffer);
    os->ctx->pb = NULL;
    avio_write(os->out, buffer, *range_length);
    av_free(buffer);

    // re-open buffer
    return avio_open_dyn_buf(&os->ctx->pb);
}

static int flush_init_segment(AVFormatContext *s, OutputStream *os)
{
    DASHContext *c = s->priv_data;
    int ret, range_length;

    ret = flush_dynbuf(os, &range_length);
    if (ret < 0)
        return ret;

    os->pos = os->init_range_length = range_length;
    if (!c->single_file)
        ff_format_io_close(s, &os->out);
    return 0;
}

static void dash_free(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    int i, j;

    if (c->as) {
        for (i = 0; i < c->nb_as; i++)
            av_dict_free(&c->as[i].metadata);
        av_freep(&c->as);
        c->nb_as = 0;
    }

    if (!c->streams)
        return;
    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        if (os->ctx && os->ctx_inited)
            av_write_trailer(os->ctx);
        if (os->ctx && os->ctx->pb)
            ffio_free_dyn_buf(&os->ctx->pb);
        ff_format_io_close(s, &os->out);
        if (os->ctx)
            avformat_free_context(os->ctx);
        for (j = 0; j < os->nb_segments; j++)
            av_free(os->segments[j]);
        av_free(os->segments);
    }
    av_freep(&c->streams);
}

static void output_segment_list(OutputStream *os, AVIOContext *out, DASHContext *c)
{
    int i, start_index = 0, start_number = 1;
    if (c->window_size) {
        start_index  = FFMAX(os->nb_segments   - c->window_size, 0);
        start_number = FFMAX(os->segment_index - c->window_size, 1);
    }

    if (c->use_template) {
        int timescale = c->use_timeline ? os->ctx->streams[0]->time_base.den : AV_TIME_BASE;
        avio_printf(out, "\t\t\t\t<SegmentTemplate timescale=\"%d\" ", timescale);
        if (!c->use_timeline)
            avio_printf(out, "duration=\"%"PRId64"\" ", c->last_duration);
        avio_printf(out, "initialization=\"%s\" media=\"%s\" startNumber=\"%d\">\n", c->init_seg_name, c->media_seg_name, c->use_timeline ? start_number : 1);
        if (c->use_timeline) {
            int64_t cur_time = 0;
            avio_printf(out, "\t\t\t\t\t<SegmentTimeline>\n");
            for (i = start_index; i < os->nb_segments; ) {
                Segment *seg = os->segments[i];
                int repeat = 0;
                avio_printf(out, "\t\t\t\t\t\t<S ");
                if (i == start_index || seg->time != cur_time) {
                    cur_time = seg->time;
                    avio_printf(out, "t=\"%"PRId64"\" ", seg->time);
                }
                avio_printf(out, "d=\"%d\" ", seg->duration);
                while (i + repeat + 1 < os->nb_segments &&
                       os->segments[i + repeat + 1]->duration == seg->duration &&
                       os->segments[i + repeat + 1]->time == os->segments[i + repeat]->time + os->segments[i + repeat]->duration)
                    repeat++;
                if (repeat > 0)
                    avio_printf(out, "r=\"%d\" ", repeat);
                avio_printf(out, "/>\n");
                i += 1 + repeat;
                cur_time += (1 + repeat) * seg->duration;
            }
            avio_printf(out, "\t\t\t\t\t</SegmentTimeline>\n");
        }
        avio_printf(out, "\t\t\t\t</SegmentTemplate>\n");
    } else if (c->single_file) {
        avio_printf(out, "\t\t\t\t<BaseURL>%s</BaseURL>\n", os->initfile);
        avio_printf(out, "\t\t\t\t<SegmentList timescale=\"%d\" duration=\"%"PRId64"\" startNumber=\"%d\">\n", AV_TIME_BASE, c->last_duration, start_number);
        avio_printf(out, "\t\t\t\t\t<Initialization range=\"%"PRId64"-%"PRId64"\" />\n", os->init_start_pos, os->init_start_pos + os->init_range_length - 1);
        for (i = start_index; i < os->nb_segments; i++) {
            Segment *seg = os->segments[i];
            avio_printf(out, "\t\t\t\t\t<SegmentURL mediaRange=\"%"PRId64"-%"PRId64"\" ", seg->start_pos, seg->start_pos + seg->range_length - 1);
            if (seg->index_length)
                avio_printf(out, "indexRange=\"%"PRId64"-%"PRId64"\" ", seg->start_pos, seg->start_pos + seg->index_length - 1);
            avio_printf(out, "/>\n");
        }
        avio_printf(out, "\t\t\t\t</SegmentList>\n");
    } else {
        avio_printf(out, "\t\t\t\t<SegmentList timescale=\"%d\" duration=\"%"PRId64"\" startNumber=\"%d\">\n", AV_TIME_BASE, c->last_duration, start_number);
        avio_printf(out, "\t\t\t\t\t<Initialization sourceURL=\"%s\" />\n", os->initfile);
        for (i = start_index; i < os->nb_segments; i++) {
            Segment *seg = os->segments[i];
            avio_printf(out, "\t\t\t\t\t<SegmentURL media=\"%s\" />\n", seg->file);
        }
        avio_printf(out, "\t\t\t\t</SegmentList>\n");
    }
}

static char *xmlescape(const char *str) {
    int outlen = strlen(str)*3/2 + 6;
    char *out = av_realloc(NULL, outlen + 1);
    int pos = 0;
    if (!out)
        return NULL;
    for (; *str; str++) {
        if (pos + 6 > outlen) {
            char *tmp;
            outlen = 2 * outlen + 6;
            tmp = av_realloc(out, outlen + 1);
            if (!tmp) {
                av_free(out);
                return NULL;
            }
            out = tmp;
        }
        if (*str == '&') {
            memcpy(&out[pos], "&amp;", 5);
            pos += 5;
        } else if (*str == '<') {
            memcpy(&out[pos], "&lt;", 4);
            pos += 4;
        } else if (*str == '>') {
            memcpy(&out[pos], "&gt;", 4);
            pos += 4;
        } else if (*str == '\'') {
            memcpy(&out[pos], "&apos;", 6);
            pos += 6;
        } else if (*str == '\"') {
            memcpy(&out[pos], "&quot;", 6);
            pos += 6;
        } else {
            out[pos++] = *str;
        }
    }
    out[pos] = '\0';
    return out;
}

static void write_time(AVIOContext *out, int64_t time)
{
    int seconds = time / AV_TIME_BASE;
    int fractions = time % AV_TIME_BASE;
    int minutes = seconds / 60;
    int hours = minutes / 60;
    seconds %= 60;
    minutes %= 60;
    avio_printf(out, "PT");
    if (hours)
        avio_printf(out, "%dH", hours);
    if (hours || minutes)
        avio_printf(out, "%dM", minutes);
    avio_printf(out, "%d.%dS", seconds, fractions / (AV_TIME_BASE / 10));
}

static void format_date_now(char *buf, int size)
{
    time_t t = time(NULL);
    struct tm *ptm, tmbuf;
    ptm = gmtime_r(&t, &tmbuf);
    if (ptm) {
        if (!strftime(buf, size, "%Y-%m-%dT%H:%M:%SZ", ptm))
            buf[0] = '\0';
    }
}

static int write_adaptation_set(AVFormatContext *s, AVIOContext *out, int as_index)
{
    DASHContext *c = s->priv_data;
    AdaptationSet *as = &c->as[as_index];
    AVDictionaryEntry *lang, *role;
    int i;

    avio_printf(out, "\t\t<AdaptationSet id=\"%s\" contentType=\"%s\" segmentAlignment=\"true\" bitstreamSwitching=\"true\"",
                as->id, as->media_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio");
    if (as->media_type == AVMEDIA_TYPE_VIDEO && c->max_frame_rate.num && !c->ambiguous_frame_rate)
        avio_printf(out, " %s=\"%d/%d\"", (av_cmp_q(c->min_frame_rate, c->max_frame_rate) < 0) ? "maxFrameRate" : "frameRate", c->max_frame_rate.num, c->max_frame_rate.den);
    lang = av_dict_get(as->metadata, "language", NULL, 0);
    if (lang)
        avio_printf(out, " lang=\"%s\"", lang->value);
    avio_printf(out, ">\n");

    role = av_dict_get(as->metadata, "role", NULL, 0);
    if (role)
        avio_printf(out, "\t\t\t<Role schemeIdUri=\"urn:mpeg:dash:role:2011\" value=\"%s\"/>\n", role->value);

    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];

        if (os->as_idx - 1 != as_index)
            continue;

        if (as->media_type == AVMEDIA_TYPE_VIDEO) {
            AVStream *st = s->streams[i];
            avio_printf(out, "\t\t\t<Representation id=\"%d\" mimeType=\"video/%s\" codecs=\"%s\"%s width=\"%d\" height=\"%d\"",
                i, os->format_name, os->codec_str, os->bandwidth_str, s->streams[i]->codecpar->width, s->streams[i]->codecpar->height);
            if (st->avg_frame_rate.num)
                avio_printf(out, " frameRate=\"%d/%d\"", st->avg_frame_rate.num, st->avg_frame_rate.den);
            avio_printf(out, ">\n");
        } else {
            avio_printf(out, "\t\t\t<Representation id=\"%d\" mimeType=\"audio/%s\" codecs=\"%s\"%s audioSamplingRate=\"%d\">\n",
                i, os->format_name, os->codec_str, os->bandwidth_str, s->streams[i]->codecpar->sample_rate);
            avio_printf(out, "\t\t\t\t<AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"%d\" />\n",
                s->streams[i]->codecpar->channels);
        }
        output_segment_list(os, out, c);
        avio_printf(out, "\t\t\t</Representation>\n");
    }
    avio_printf(out, "\t\t</AdaptationSet>\n");

    return 0;
}

static int add_adaptation_set(AVFormatContext *s, AdaptationSet **as, enum AVMediaType type)
{
    DASHContext *c = s->priv_data;

    void *mem = av_realloc(c->as, sizeof(*c->as) * (c->nb_as + 1));
    if (!mem)
        return AVERROR(ENOMEM);
    c->as = mem;
    ++c->nb_as;

    *as = &c->as[c->nb_as - 1];
    memset(*as, 0, sizeof(**as));
    (*as)->media_type = type;

    return 0;
}

static int adaptation_set_add_stream(AVFormatContext *s, int as_idx, int i)
{
    DASHContext *c = s->priv_data;
    AdaptationSet *as = &c->as[as_idx - 1];
    OutputStream *os = &c->streams[i];

    if (as->media_type != s->streams[i]->codecpar->codec_type) {
        av_log(s, AV_LOG_ERROR, "Codec type of stream %d doesn't match AdaptationSet's media type\n", i);
        return AVERROR(EINVAL);
    } else if (os->as_idx) {
        av_log(s, AV_LOG_ERROR, "Stream %d is already assigned to an AdaptationSet\n", i);
        return AVERROR(EINVAL);
    }
    os->as_idx = as_idx;

    return 0;
}

static int parse_adaptation_sets(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    const char *p = c->adaptation_sets;
    enum { new_set, parse_id, parsing_streams } state;
    AdaptationSet *as;
    int i, n, ret;

    // default: one AdaptationSet for each stream
    if (!p) {
        for (i = 0; i < s->nb_streams; i++) {
            if ((ret = add_adaptation_set(s, &as, s->streams[i]->codecpar->codec_type)) < 0)
                return ret;
            snprintf(as->id, sizeof(as->id), "%d", i);

            c->streams[i].as_idx = c->nb_as;
        }
        goto end;
    }

    // syntax id=0,streams=0,1,2 id=1,streams=3,4 and so on
    state = new_set;
    while (*p) {
        if (*p == ' ') {
            p++;
            continue;
        } else if (state == new_set && av_strstart(p, "id=", &p)) {

            if ((ret = add_adaptation_set(s, &as, AVMEDIA_TYPE_UNKNOWN)) < 0)
                return ret;

            n = strcspn(p, ",");
            snprintf(as->id, sizeof(as->id), "%.*s", n, p);

            p += n;
            if (*p)
                p++;
            state = parse_id;
        } else if (state == parse_id && av_strstart(p, "streams=", &p)) {
            state = parsing_streams;
        } else if (state == parsing_streams) {
            AdaptationSet *as = &c->as[c->nb_as - 1];
            char idx_str[8], *end_str;

            n = strcspn(p, " ,");
            snprintf(idx_str, sizeof(idx_str), "%.*s", n, p);
            p += n;

            // if value is "a" or "v", map all streams of that type
            if (as->media_type == AVMEDIA_TYPE_UNKNOWN && (idx_str[0] == 'v' || idx_str[0] == 'a')) {
                enum AVMediaType type = (idx_str[0] == 'v') ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO;
                av_log(s, AV_LOG_DEBUG, "Map all streams of type %s\n", idx_str);

                for (i = 0; i < s->nb_streams; i++) {
                    if (s->streams[i]->codecpar->codec_type != type)
                        continue;

                    as->media_type = s->streams[i]->codecpar->codec_type;

                    if ((ret = adaptation_set_add_stream(s, c->nb_as, i)) < 0)
                        return ret;
                }
            } else { // select single stream
                i = strtol(idx_str, &end_str, 10);
                if (idx_str == end_str || i < 0 || i >= s->nb_streams) {
                    av_log(s, AV_LOG_ERROR, "Selected stream \"%s\" not found!\n", idx_str);
                    return AVERROR(EINVAL);
                }
                av_log(s, AV_LOG_DEBUG, "Map stream %d\n", i);

                if (as->media_type == AVMEDIA_TYPE_UNKNOWN) {
                    as->media_type = s->streams[i]->codecpar->codec_type;
                }

                if ((ret = adaptation_set_add_stream(s, c->nb_as, i)) < 0)
                    return ret;
            }

            if (*p == ' ')
                state = new_set;
            if (*p)
                p++;
        } else {
            return AVERROR(EINVAL);
        }
    }

end:
    // check for unassigned streams
    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        if (!os->as_idx) {
            av_log(s, AV_LOG_ERROR, "Stream %d is not mapped to an AdaptationSet\n", i);
            return AVERROR(EINVAL);
        }
    }
    return 0;
}

static int write_manifest(AVFormatContext *s, int final)
{
    DASHContext *c = s->priv_data;
    AVIOContext *out;
    char temp_filename[1024];
    int ret, i;
    const char *proto = avio_find_protocol_name(s->filename);
    int use_rename = proto && !strcmp(proto, "file");
    static unsigned int warned_non_file = 0;
    AVDictionaryEntry *title = av_dict_get(s->metadata, "title", NULL, 0);

    if (!use_rename && !warned_non_file++)
        av_log(s, AV_LOG_ERROR, "Cannot use rename on non file protocol, this may lead to races and temporary partial files\n");

    snprintf(temp_filename, sizeof(temp_filename), use_rename ? "%s.tmp" : "%s", s->filename);
    ret = s->io_open(s, &out, temp_filename, AVIO_FLAG_WRITE, NULL);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Unable to open %s for writing\n", temp_filename);
        return ret;
    }
    avio_printf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    avio_printf(out, "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
                "\txmlns=\"urn:mpeg:dash:schema:mpd:2011\"\n"
                "\txmlns:xlink=\"http://www.w3.org/1999/xlink\"\n"
                "\txsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd\"\n"
                "\tprofiles=\"urn:mpeg:dash:profile:isoff-live:2011\"\n"
                "\ttype=\"%s\"\n", final ? "static" : "dynamic");
    if (final) {
        avio_printf(out, "\tmediaPresentationDuration=\"");
        write_time(out, c->total_duration);
        avio_printf(out, "\"\n");
    } else {
        int64_t update_period = c->last_duration / AV_TIME_BASE;
        char now_str[100];
        if (c->use_template && !c->use_timeline)
            update_period = 500;
        avio_printf(out, "\tminimumUpdatePeriod=\"PT%"PRId64"S\"\n", update_period);
        avio_printf(out, "\tsuggestedPresentationDelay=\"PT%"PRId64"S\"\n", c->last_duration / AV_TIME_BASE);
        if (!c->availability_start_time[0] && s->nb_streams > 0 && c->streams[0].nb_segments > 0) {
            format_date_now(c->availability_start_time, sizeof(c->availability_start_time));
        }
        if (c->availability_start_time[0])
            avio_printf(out, "\tavailabilityStartTime=\"%s\"\n", c->availability_start_time);
        format_date_now(now_str, sizeof(now_str));
        if (now_str[0])
            avio_printf(out, "\tpublishTime=\"%s\"\n", now_str);
        if (c->window_size && c->use_template) {
            avio_printf(out, "\ttimeShiftBufferDepth=\"");
            write_time(out, c->last_duration * c->window_size);
            avio_printf(out, "\"\n");
        }
    }
    avio_printf(out, "\tminBufferTime=\"");
    write_time(out, c->last_duration * 2);
    avio_printf(out, "\">\n");
    avio_printf(out, "\t<ProgramInformation>\n");
    if (title) {
        char *escaped = xmlescape(title->value);
        avio_printf(out, "\t\t<Title>%s</Title>\n", escaped);
        av_free(escaped);
    }
    avio_printf(out, "\t</ProgramInformation>\n");
    if (c->utc_timing_url)
        avio_printf(out, "\t<UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"%s\"/>\n", c->utc_timing_url);

    if (c->window_size && s->nb_streams > 0 && c->streams[0].nb_segments > 0 && !c->use_template) {
        OutputStream *os = &c->streams[0];
        int start_index = FFMAX(os->nb_segments - c->window_size, 0);
        int64_t start_time = av_rescale_q(os->segments[start_index]->time, s->streams[0]->time_base, AV_TIME_BASE_Q);
        avio_printf(out, "\t<Period id=\"0\" start=\"");
        write_time(out, start_time);
        avio_printf(out, "\">\n");
    } else {
        avio_printf(out, "\t<Period id=\"0\" start=\"PT0.0S\">\n");
    }

    for (i = 0; i < c->nb_as; i++) {
        if ((ret = write_adaptation_set(s, out, i)) < 0)
            return ret;
    }
    avio_printf(out, "\t</Period>\n");
    avio_printf(out, "</MPD>\n");
    avio_flush(out);
    ff_format_io_close(s, &out);

    if (use_rename)
        return avpriv_io_move(temp_filename, s->filename);

    return 0;
}

static int dict_copy_entry(AVDictionary **dst, const AVDictionary *src, const char *key)
{
    AVDictionaryEntry *entry = av_dict_get(src, key, NULL, 0);
    if (entry)
        av_dict_set(dst, key, entry->value, AV_DICT_DONT_OVERWRITE);
    return 0;
}

static int dict_set_int(AVDictionary **pm, const char *key, int64_t value, int flags)
{
    char valuestr[22];
    snprintf(valuestr, sizeof(valuestr), "%"PRId64, value);
    flags &= ~AV_DICT_DONT_STRDUP_VAL;
    return av_dict_set(pm, key, valuestr, flags);
}

static int dash_init(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    int ret = 0, i;
    char *ptr;
    char basename[1024];

    if (c->single_file_name)
        c->single_file = 1;
    if (c->single_file)
        c->use_template = 0;
    c->ambiguous_frame_rate = 0;

    av_strlcpy(c->dirname, s->filename, sizeof(c->dirname));
    ptr = strrchr(c->dirname, '/');
    if (ptr) {
        av_strlcpy(basename, &ptr[1], sizeof(basename));
        ptr[1] = '\0';
    } else {
        c->dirname[0] = '\0';
        av_strlcpy(basename, s->filename, sizeof(basename));
    }

    ptr = strrchr(basename, '.');
    if (ptr)
        *ptr = '\0';

    c->streams = av_mallocz(sizeof(*c->streams) * s->nb_streams);
    if (!c->streams)
        return AVERROR(ENOMEM);

    if ((ret = parse_adaptation_sets(s)) < 0)
        return ret;

    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        AdaptationSet *as = &c->as[os->as_idx - 1];
        AVFormatContext *ctx;
        AVStream *st;
        AVDictionary *opts = NULL;
        char filename[1024];

        os->bit_rate = s->streams[i]->codecpar->bit_rate;
        if (os->bit_rate) {
            snprintf(os->bandwidth_str, sizeof(os->bandwidth_str),
                     " bandwidth=\"%d\"", os->bit_rate);
        } else {
            int level = s->strict_std_compliance >= FF_COMPLIANCE_STRICT ?
                        AV_LOG_ERROR : AV_LOG_WARNING;
            av_log(s, level, "No bit rate set for stream %d\n", i);
            if (s->strict_std_compliance >= FF_COMPLIANCE_STRICT)
                return AVERROR(EINVAL);
        }

        // copy AdaptationSet language and role from stream metadata
        dict_copy_entry(&as->metadata, s->streams[i]->metadata, "language");
        dict_copy_entry(&as->metadata, s->streams[i]->metadata, "role");

        ctx = avformat_alloc_context();
        if (!ctx)
            return AVERROR(ENOMEM);

        // choose muxer based on codec: webm for VP8/9 and opus, mp4 otherwise
        // note: os->format_name is also used as part of the mimetype of the
        //       representation, e.g. video/<format_name>
        if (s->streams[i]->codecpar->codec_id == AV_CODEC_ID_VP8 ||
            s->streams[i]->codecpar->codec_id == AV_CODEC_ID_VP9 ||
            s->streams[i]->codecpar->codec_id == AV_CODEC_ID_OPUS ||
            s->streams[i]->codecpar->codec_id == AV_CODEC_ID_VORBIS) {
            snprintf(os->format_name, sizeof(os->format_name), "webm");
        } else {
            snprintf(os->format_name, sizeof(os->format_name), "mp4");
        }
        ctx->oformat = av_guess_format(os->format_name, NULL, NULL);
        if (!ctx->oformat)
            return AVERROR_MUXER_NOT_FOUND;
        os->ctx = ctx;
        ctx->interrupt_callback = s->interrupt_callback;
        ctx->opaque             = s->opaque;
        ctx->io_close           = s->io_close;
        ctx->io_open            = s->io_open;

        if (!(st = avformat_new_stream(ctx, NULL)))
            return AVERROR(ENOMEM);
        avcodec_parameters_copy(st->codecpar, s->streams[i]->codecpar);
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        st->time_base = s->streams[i]->time_base;
        ctx->avoid_negative_ts = s->avoid_negative_ts;
        ctx->flags = s->flags;

        if ((ret = avio_open_dyn_buf(&ctx->pb)) < 0)
            return ret;

        if (c->single_file) {
            if (c->single_file_name)
                ff_dash_fill_tmpl_params(os->initfile, sizeof(os->initfile), c->single_file_name, i, 0, os->bit_rate, 0);
            else
                snprintf(os->initfile, sizeof(os->initfile), "%s-stream%d.m4s", basename, i);
        } else {
            ff_dash_fill_tmpl_params(os->initfile, sizeof(os->initfile), c->init_seg_name, i, 0, os->bit_rate, 0);
        }
        snprintf(filename, sizeof(filename), "%s%s", c->dirname, os->initfile);
        ret = s->io_open(s, &os->out, filename, AVIO_FLAG_WRITE, NULL);
        if (ret < 0)
            return ret;
        os->init_start_pos = 0;

        if (!strcmp(os->format_name, "mp4")) {
            av_dict_set(&opts, "movflags", "frag_custom+dash+delay_moov", 0);
        } else {
            dict_set_int(&opts, "cluster_time_limit", c->min_seg_duration / 1000, 0);
            dict_set_int(&opts, "cluster_size_limit", 5 * 1024 * 1024, 0); // set a large cluster size limit
        }
        if ((ret = avformat_write_header(ctx, &opts)) < 0)
            return ret;
        os->ctx_inited = 1;
        avio_flush(ctx->pb);
        av_dict_free(&opts);

        av_log(s, AV_LOG_VERBOSE, "Representation %d init segment will be written to: %s\n", i, filename);

        // Flush init segment
        // except for mp4, since delay_moov is set and the init segment
        // is then flushed after the first packets
        if (strcmp(os->format_name, "mp4")) {
            flush_init_segment(s, os);
        }

        s->streams[i]->time_base = st->time_base;
        // If the muxer wants to shift timestamps, request to have them shifted
        // already before being handed to this muxer, so we don't have mismatches
        // between the MPD and the actual segments.
        s->avoid_negative_ts = ctx->avoid_negative_ts;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            AVRational avg_frame_rate = s->streams[i]->avg_frame_rate;
            if (avg_frame_rate.num > 0) {
                if (av_cmp_q(avg_frame_rate, c->min_frame_rate) < 0)
                    c->min_frame_rate = avg_frame_rate;
                if (av_cmp_q(c->max_frame_rate, avg_frame_rate) < 0)
                    c->max_frame_rate = avg_frame_rate;
            } else {
                c->ambiguous_frame_rate = 1;
            }
            c->has_video = 1;
        }

        set_codec_str(s, st->codecpar, os->codec_str, sizeof(os->codec_str));
        os->first_pts = AV_NOPTS_VALUE;
        os->max_pts = AV_NOPTS_VALUE;
        os->last_dts = AV_NOPTS_VALUE;
        os->segment_index = 1;
    }

    if (!c->has_video && c->min_seg_duration <= 0) {
        av_log(s, AV_LOG_WARNING, "no video stream and no min seg duration set\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

static int dash_write_header(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    int i, ret;
    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        if ((ret = avformat_write_header(os->ctx, NULL)) < 0) {
            dash_free(s);
            return ret;
        }
    }
    ret = write_manifest(s, 0);
    if (!ret)
        av_log(s, AV_LOG_VERBOSE, "Manifest written to: %s\n", s->filename);
    return ret;
}

static int add_segment(OutputStream *os, const char *file,
                       int64_t time, int duration,
                       int64_t start_pos, int64_t range_length,
                       int64_t index_length)
{
    int err;
    Segment *seg;
    if (os->nb_segments >= os->segments_size) {
        os->segments_size = (os->segments_size + 1) * 2;
        if ((err = av_reallocp(&os->segments, sizeof(*os->segments) *
                               os->segments_size)) < 0) {
            os->segments_size = 0;
            os->nb_segments = 0;
            return err;
        }
    }
    seg = av_mallocz(sizeof(*seg));
    if (!seg)
        return AVERROR(ENOMEM);
    av_strlcpy(seg->file, file, sizeof(seg->file));
    seg->time = time;
    seg->duration = duration;
    if (seg->time < 0) { // If pts<0, it is expected to be cut away with an edit list
        seg->duration += seg->time;
        seg->time = 0;
    }
    seg->start_pos = start_pos;
    seg->range_length = range_length;
    seg->index_length = index_length;
    os->segments[os->nb_segments++] = seg;
    os->segment_index++;
    return 0;
}

static void write_styp(AVIOContext *pb)
{
    avio_wb32(pb, 24);
    ffio_wfourcc(pb, "styp");
    ffio_wfourcc(pb, "msdh");
    avio_wb32(pb, 0); /* minor */
    ffio_wfourcc(pb, "msdh");
    ffio_wfourcc(pb, "msix");
}

static void find_index_range(AVFormatContext *s, const char *full_path,
                             int64_t pos, int *index_length)
{
    uint8_t buf[8];
    AVIOContext *pb;
    int ret;

    ret = s->io_open(s, &pb, full_path, AVIO_FLAG_READ, NULL);
    if (ret < 0)
        return;
    if (avio_seek(pb, pos, SEEK_SET) != pos) {
        ff_format_io_close(s, &pb);
        return;
    }
    ret = avio_read(pb, buf, 8);
    ff_format_io_close(s, &pb);
    if (ret < 8)
        return;
    if (AV_RL32(&buf[4]) != MKTAG('s', 'i', 'd', 'x'))
        return;
    *index_length = AV_RB32(&buf[0]);
}

static int update_stream_extradata(AVFormatContext *s, OutputStream *os,
                                   AVCodecParameters *par)
{
    uint8_t *extradata;

    if (os->ctx->streams[0]->codecpar->extradata_size || !par->extradata_size)
        return 0;

    extradata = av_malloc(par->extradata_size);

    if (!extradata)
        return AVERROR(ENOMEM);

    memcpy(extradata, par->extradata, par->extradata_size);

    os->ctx->streams[0]->codecpar->extradata = extradata;
    os->ctx->streams[0]->codecpar->extradata_size = par->extradata_size;

    set_codec_str(s, par, os->codec_str, sizeof(os->codec_str));

    return 0;
}

static int dash_flush(AVFormatContext *s, int final, int stream)
{
    DASHContext *c = s->priv_data;
    int i, ret = 0;

    const char *proto = avio_find_protocol_name(s->filename);
    int use_rename = proto && !strcmp(proto, "file");

    int cur_flush_segment_index = 0;
    if (stream >= 0)
        cur_flush_segment_index = c->streams[stream].segment_index;

    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        char filename[1024] = "", full_path[1024], temp_path[1024];
        int range_length, index_length = 0;

        if (!os->packets_written)
            continue;

        // Flush the single stream that got a keyframe right now.
        // Flush all audio streams as well, in sync with video keyframes,
        // but not the other video streams.
        if (stream >= 0 && i != stream) {
            if (s->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                continue;
            // Make sure we don't flush audio streams multiple times, when
            // all video streams are flushed one at a time.
            if (c->has_video && os->segment_index > cur_flush_segment_index)
                continue;
        }

        if (!os->init_range_length) {
            flush_init_segment(s, os);
        }

        if (!c->single_file) {
            ff_dash_fill_tmpl_params(filename, sizeof(filename), c->media_seg_name, i, os->segment_index, os->bit_rate, os->start_pts);
            snprintf(full_path, sizeof(full_path), "%s%s", c->dirname, filename);
            snprintf(temp_path, sizeof(temp_path), use_rename ? "%s.tmp" : "%s", full_path);
            ret = s->io_open(s, &os->out, temp_path, AVIO_FLAG_WRITE, NULL);
            if (ret < 0)
                break;
            if (!strcmp(os->format_name, "mp4"))
                write_styp(os->ctx->pb);
        } else {
            snprintf(full_path, sizeof(full_path), "%s%s", c->dirname, os->initfile);
        }

        ret = flush_dynbuf(os, &range_length);
        if (ret < 0)
            break;
        os->packets_written = 0;

        if (c->single_file) {
            find_index_range(s, full_path, os->pos, &index_length);
        } else {
            ff_format_io_close(s, &os->out);

            if (use_rename) {
                ret = avpriv_io_move(temp_path, full_path);
                if (ret < 0)
                    break;
            }
        }

        if (!os->bit_rate) {
            // calculate average bitrate of first segment
            int64_t bitrate = (int64_t) range_length * 8 * AV_TIME_BASE / (os->max_pts - os->start_pts);
            if (bitrate >= 0) {
                os->bit_rate = bitrate;
                snprintf(os->bandwidth_str, sizeof(os->bandwidth_str),
                     " bandwidth=\"%d\"", os->bit_rate);
            }
        }
        add_segment(os, filename, os->start_pts, os->max_pts - os->start_pts, os->pos, range_length, index_length);
        av_log(s, AV_LOG_VERBOSE, "Representation %d media segment %d written to: %s\n", i, os->segment_index, full_path);

        os->pos += range_length;
    }

    if (c->window_size || (final && c->remove_at_exit)) {
        for (i = 0; i < s->nb_streams; i++) {
            OutputStream *os = &c->streams[i];
            int j;
            int remove = os->nb_segments - c->window_size - c->extra_window_size;
            if (final && c->remove_at_exit)
                remove = os->nb_segments;
            if (remove > 0) {
                for (j = 0; j < remove; j++) {
                    char filename[1024];
                    snprintf(filename, sizeof(filename), "%s%s", c->dirname, os->segments[j]->file);
                    unlink(filename);
                    av_free(os->segments[j]);
                }
                os->nb_segments -= remove;
                memmove(os->segments, os->segments + remove, os->nb_segments * sizeof(*os->segments));
            }
        }
    }

    if (ret >= 0)
        ret = write_manifest(s, final);
    return ret;
}

static int dash_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    DASHContext *c = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    OutputStream *os = &c->streams[pkt->stream_index];
    int ret;

    ret = update_stream_extradata(s, os, st->codecpar);
    if (ret < 0)
        return ret;

    // Fill in a heuristic guess of the packet duration, if none is available.
    // The mp4 muxer will do something similar (for the last packet in a fragment)
    // if nothing is set (setting it for the other packets doesn't hurt).
    // By setting a nonzero duration here, we can be sure that the mp4 muxer won't
    // invoke its heuristic (this doesn't have to be identical to that algorithm),
    // so that we know the exact timestamps of fragments.
    if (!pkt->duration && os->last_dts != AV_NOPTS_VALUE)
        pkt->duration = pkt->dts - os->last_dts;
    os->last_dts = pkt->dts;

    // If forcing the stream to start at 0, the mp4 muxer will set the start
    // timestamps to 0. Do the same here, to avoid mismatches in duration/timestamps.
    if (os->first_pts == AV_NOPTS_VALUE &&
        s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_MAKE_ZERO) {
        pkt->pts -= pkt->dts;
        pkt->dts  = 0;
    }

    if (os->first_pts == AV_NOPTS_VALUE)
        os->first_pts = pkt->pts;

    if ((!c->has_video || st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) &&
        pkt->flags & AV_PKT_FLAG_KEY && os->packets_written &&
        av_compare_ts(pkt->pts - os->start_pts, st->time_base,
                      c->min_seg_duration, AV_TIME_BASE_Q) >= 0) {
        int64_t prev_duration = c->last_duration;

        c->last_duration = av_rescale_q(pkt->pts - os->start_pts,
                                        st->time_base,
                                        AV_TIME_BASE_Q);
        c->total_duration = av_rescale_q(pkt->pts - os->first_pts,
                                         st->time_base,
                                         AV_TIME_BASE_Q);

        if ((!c->use_timeline || !c->use_template) && prev_duration) {
            if (c->last_duration < prev_duration*9/10 ||
                c->last_duration > prev_duration*11/10) {
                av_log(s, AV_LOG_WARNING,
                       "Segment durations differ too much, enable use_timeline "
                       "and use_template, or keep a stricter keyframe interval\n");
            }
        }

        if ((ret = dash_flush(s, 0, pkt->stream_index)) < 0)
            return ret;
    }

    if (!os->packets_written) {
        // If we wrote a previous segment, adjust the start time of the segment
        // to the end of the previous one (which is the same as the mp4 muxer
        // does). This avoids gaps in the timeline.
        if (os->max_pts != AV_NOPTS_VALUE)
            os->start_pts = os->max_pts;
        else
            os->start_pts = pkt->pts;
    }
    if (os->max_pts == AV_NOPTS_VALUE)
        os->max_pts = pkt->pts + pkt->duration;
    else
        os->max_pts = FFMAX(os->max_pts, pkt->pts + pkt->duration);
    os->packets_written++;
    return ff_write_chained(os->ctx, 0, pkt, s, 0);
}

static int dash_write_trailer(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;

    if (s->nb_streams > 0) {
        OutputStream *os = &c->streams[0];
        // If no segments have been written so far, try to do a crude
        // guess of the segment duration
        if (!c->last_duration)
            c->last_duration = av_rescale_q(os->max_pts - os->start_pts,
                                            s->streams[0]->time_base,
                                            AV_TIME_BASE_Q);
        c->total_duration = av_rescale_q(os->max_pts - os->first_pts,
                                         s->streams[0]->time_base,
                                         AV_TIME_BASE_Q);
    }
    dash_flush(s, 1, -1);

    if (c->remove_at_exit) {
        char filename[1024];
        int i;
        for (i = 0; i < s->nb_streams; i++) {
            OutputStream *os = &c->streams[i];
            snprintf(filename, sizeof(filename), "%s%s", c->dirname, os->initfile);
            unlink(filename);
        }
        unlink(s->filename);
    }

    return 0;
}

static int dash_check_bitstream(struct AVFormatContext *s, const AVPacket *avpkt)
{
    DASHContext *c = s->priv_data;
    OutputStream *os = &c->streams[avpkt->stream_index];
    AVFormatContext *oc = os->ctx;
    if (oc->oformat->check_bitstream) {
        int ret;
        AVPacket pkt = *avpkt;
        pkt.stream_index = 0;
        ret = oc->oformat->check_bitstream(oc, &pkt);
        if (ret == 1) {
            AVStream *st = s->streams[avpkt->stream_index];
            AVStream *ost = oc->streams[0];
            st->internal->bsfcs = ost->internal->bsfcs;
            st->internal->nb_bsfcs = ost->internal->nb_bsfcs;
            ost->internal->bsfcs = NULL;
            ost->internal->nb_bsfcs = 0;
        }
        return ret;
    }
    return 1;
}

#define OFFSET(x) offsetof(DASHContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "adaptation_sets", "Adaptation sets. Syntax: id=0,streams=0,1,2 id=1,streams=3,4 and so on", OFFSET(adaptation_sets), AV_OPT_TYPE_STRING, { 0 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_size", "number of segments kept in the manifest", OFFSET(window_size), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, E },
    { "extra_window_size", "number of segments kept outside of the manifest before removing from disk", OFFSET(extra_window_size), AV_OPT_TYPE_INT, { .i64 = 5 }, 0, INT_MAX, E },
    { "min_seg_duration", "minimum segment duration (in microseconds)", OFFSET(min_seg_duration), AV_OPT_TYPE_INT64, { .i64 = 5000000 }, 0, INT_MAX, E },
    { "remove_at_exit", "remove all segments when finished", OFFSET(remove_at_exit), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "use_template", "Use SegmentTemplate instead of SegmentList", OFFSET(use_template), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, E },
    { "use_timeline", "Use SegmentTimeline in SegmentTemplate", OFFSET(use_timeline), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, E },
    { "single_file", "Store all segments in one file, accessed using byte ranges", OFFSET(single_file), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "single_file_name", "DASH-templated name to be used for baseURL. Implies storing all segments in one file, accessed using byte ranges", OFFSET(single_file_name), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, E },
    { "init_seg_name", "DASH-templated name to used for the initialization segment", OFFSET(init_seg_name), AV_OPT_TYPE_STRING, {.str = "init-stream$RepresentationID$.m4s"}, 0, 0, E },
    { "media_seg_name", "DASH-templated name to used for the media segments", OFFSET(media_seg_name), AV_OPT_TYPE_STRING, {.str = "chunk-stream$RepresentationID$-$Number%05d$.m4s"}, 0, 0, E },
    { "utc_timing_url", "URL of the page that will return the UTC timestamp in ISO format", OFFSET(utc_timing_url), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { NULL },
};

static const AVClass dash_class = {
    .class_name = "dash muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_dash_muxer = {
    .name           = "dash",
    .long_name      = NULL_IF_CONFIG_SMALL("DASH Muxer"),
    .priv_data_size = sizeof(DASHContext),
    .audio_codec    = AV_CODEC_ID_AAC,
    .video_codec    = AV_CODEC_ID_H264,
    .flags          = AVFMT_GLOBALHEADER | AVFMT_NOFILE | AVFMT_TS_NEGATIVE,
    .init           = dash_init,
    .write_header   = dash_write_header,
    .write_packet   = dash_write_packet,
    .write_trailer  = dash_write_trailer,
    .deinit         = dash_free,
    .check_bitstream = dash_check_bitstream,
    .priv_class     = &dash_class,
};
