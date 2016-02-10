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

// See ISO/IEC 23009-1:2014 5.3.9.4.4
typedef enum {
    DASH_TMPL_ID_UNDEFINED = -1,
    DASH_TMPL_ID_ESCAPE,
    DASH_TMPL_ID_REP_ID,
    DASH_TMPL_ID_NUMBER,
    DASH_TMPL_ID_BANDWIDTH,
    DASH_TMPL_ID_TIME,
} DASHTmplId;

typedef struct Segment {
    char file[1024];
    int64_t start_pos;
    int range_length, index_length;
    int64_t time;
    int duration;
    int n;
} Segment;

typedef struct OutputStream {
    AVFormatContext *ctx;
    int ctx_inited;
    uint8_t iobuf[32768];
    URLContext *out;
    int packets_written;
    char initfile[1024];
    int64_t init_start_pos;
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
    int window_size;
    int extra_window_size;
    int min_seg_duration;
    int remove_at_exit;
    int use_template;
    int use_timeline;
    int single_file;
    OutputStream *streams;
    int has_video, has_audio;
    int64_t last_duration;
    int64_t total_duration;
    char availability_start_time[100];
    char dirname[1024];
    const char *single_file_name;
    const char *init_seg_name;
    const char *media_seg_name;
    AVRational min_frame_rate, max_frame_rate;
    int ambiguous_frame_rate;
} DASHContext;

static int dash_write(void *opaque, uint8_t *buf, int buf_size)
{
    OutputStream *os = opaque;
    if (os->out)
        ffurl_write(os->out, buf, buf_size);
    return buf_size;
}

// RFC 6381
static void set_codec_str(AVFormatContext *s, AVCodecContext *codec,
                          char *str, int size)
{
    const AVCodecTag *tags[2] = { NULL, NULL };
    uint32_t tag;
    if (codec->codec_type == AVMEDIA_TYPE_VIDEO)
        tags[0] = ff_codec_movvideo_tags;
    else if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
        tags[0] = ff_codec_movaudio_tags;
    else
        return;

    tag = av_codec_get_tag(tags, codec->codec_id);
    if (!tag)
        return;
    if (size < 5)
        return;

    AV_WL32(str, tag);
    str[4] = '\0';
    if (!strcmp(str, "mp4a") || !strcmp(str, "mp4v")) {
        uint32_t oti;
        tags[0] = ff_mp4_obj_type;
        oti = av_codec_get_tag(tags, codec->codec_id);
        if (oti)
            av_strlcatf(str, size, ".%02x", oti);
        else
            return;

        if (tag == MKTAG('m', 'p', '4', 'a')) {
            if (codec->extradata_size >= 2) {
                int aot = codec->extradata[0] >> 3;
                if (aot == 31)
                    aot = ((AV_RB16(codec->extradata) >> 5) & 0x3f) + 32;
                av_strlcatf(str, size, ".%d", aot);
            }
        } else if (tag == MKTAG('m', 'p', '4', 'v')) {
            // Unimplemented, should output ProfileLevelIndication as a decimal number
            av_log(s, AV_LOG_WARNING, "Incomplete RFC 6381 codec string for mp4v\n");
        }
    } else if (!strcmp(str, "avc1")) {
        uint8_t *tmpbuf = NULL;
        uint8_t *extradata = codec->extradata;
        int extradata_size = codec->extradata_size;
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

static void dash_free(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    int i, j;
    if (!c->streams)
        return;
    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        if (os->ctx && os->ctx_inited)
            av_write_trailer(os->ctx);
        if (os->ctx && os->ctx->pb)
            av_free(os->ctx->pb);
        ffurl_close(os->out);
        os->out =  NULL;
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

static DASHTmplId dash_read_tmpl_id(const char *identifier, char *format_tag,
                                    size_t format_tag_size, const char **ptr) {
    const char *next_ptr;
    DASHTmplId id_type = DASH_TMPL_ID_UNDEFINED;

    if (av_strstart(identifier, "$$", &next_ptr)) {
        id_type = DASH_TMPL_ID_ESCAPE;
        *ptr = next_ptr;
    } else if (av_strstart(identifier, "$RepresentationID$", &next_ptr)) {
        id_type = DASH_TMPL_ID_REP_ID;
        // default to basic format, as $RepresentationID$ identifiers
        // are not allowed to have custom format-tags.
        av_strlcpy(format_tag, "%d", format_tag_size);
        *ptr = next_ptr;
    } else { // the following identifiers may have an explicit format_tag
        if (av_strstart(identifier, "$Number", &next_ptr))
            id_type = DASH_TMPL_ID_NUMBER;
        else if (av_strstart(identifier, "$Bandwidth", &next_ptr))
            id_type = DASH_TMPL_ID_BANDWIDTH;
        else if (av_strstart(identifier, "$Time", &next_ptr))
            id_type = DASH_TMPL_ID_TIME;
        else
            id_type = DASH_TMPL_ID_UNDEFINED;

        // next parse the dash format-tag and generate a c-string format tag
        // (next_ptr now points at the first '%' at the beginning of the format-tag)
        if (id_type != DASH_TMPL_ID_UNDEFINED) {
            const char *number_format = (id_type == DASH_TMPL_ID_TIME) ? PRId64 : "d";
            if (next_ptr[0] == '$') { // no dash format-tag
                snprintf(format_tag, format_tag_size, "%%%s", number_format);
                *ptr = &next_ptr[1];
            } else {
                const char *width_ptr;
                // only tolerate single-digit width-field (i.e. up to 9-digit width)
                if (av_strstart(next_ptr, "%0", &width_ptr) &&
                    av_isdigit(width_ptr[0]) &&
                    av_strstart(&width_ptr[1], "d$", &next_ptr)) {
                    // yes, we're using a format tag to build format_tag.
                    snprintf(format_tag, format_tag_size, "%s%c%s", "%0", width_ptr[0], number_format);
                    *ptr = next_ptr;
                } else {
                    av_log(NULL, AV_LOG_WARNING, "Failed to parse format-tag beginning with %s. Expected either a "
                                                 "closing '$' character or a format-string like '%%0[width]d', "
                                                 "where width must be a single digit\n", next_ptr);
                    id_type = DASH_TMPL_ID_UNDEFINED;
                }
            }
        }
    }
    return id_type;
}

static void dash_fill_tmpl_params(char *dst, size_t buffer_size,
                                  const char *template, int rep_id,
                                  int number, int bit_rate,
                                  int64_t time) {
    int dst_pos = 0;
    const char *t_cur = template;
    while (dst_pos < buffer_size - 1 && *t_cur) {
        char format_tag[7]; // May be "%d", "%0Xd", or "%0Xlld" (for $Time$), where X is in [0-9]
        int n = 0;
        DASHTmplId id_type;
        const char *t_next = strchr(t_cur, '$'); // copy over everything up to the first '$' character
        if (t_next) {
            int num_copy_bytes = FFMIN(t_next - t_cur, buffer_size - dst_pos - 1);
            av_strlcpy(&dst[dst_pos], t_cur, num_copy_bytes + 1);
            // advance
            dst_pos += num_copy_bytes;
            t_cur = t_next;
        } else { // no more DASH identifiers to substitute - just copy the rest over and break
            av_strlcpy(&dst[dst_pos], t_cur, buffer_size - dst_pos);
            break;
        }

        if (dst_pos >= buffer_size - 1 || !*t_cur)
            break;

        // t_cur is now pointing to a '$' character
        id_type = dash_read_tmpl_id(t_cur, format_tag, sizeof(format_tag), &t_next);
        switch (id_type) {
        case DASH_TMPL_ID_ESCAPE:
            av_strlcpy(&dst[dst_pos], "$", 2);
            n = 1;
            break;
        case DASH_TMPL_ID_REP_ID:
            n = snprintf(&dst[dst_pos], buffer_size - dst_pos, format_tag, rep_id);
            break;
        case DASH_TMPL_ID_NUMBER:
            n = snprintf(&dst[dst_pos], buffer_size - dst_pos, format_tag, number);
            break;
        case DASH_TMPL_ID_BANDWIDTH:
            n = snprintf(&dst[dst_pos], buffer_size - dst_pos, format_tag, bit_rate);
            break;
        case DASH_TMPL_ID_TIME:
            n = snprintf(&dst[dst_pos], buffer_size - dst_pos, format_tag, time);
            break;
        case DASH_TMPL_ID_UNDEFINED:
            // copy over one byte and advance
            av_strlcpy(&dst[dst_pos], t_cur, 2);
            n = 1;
            t_next = &t_cur[1];
            break;
        }
        // t_next points just past the processed identifier
        // n is the number of bytes that were attempted to be written to dst
        // (may have failed to write all because buffer_size).

        // advance
        dst_pos += FFMIN(n, buffer_size - dst_pos - 1);
        t_cur = t_next;
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
        if (!strftime(buf, size, "%Y-%m-%dT%H:%M:%S", ptm))
            buf[0] = '\0';
    }
}

static int write_manifest(AVFormatContext *s, int final)
{
    DASHContext *c = s->priv_data;
    AVIOContext *out;
    char temp_filename[1024];
    int ret, i;
    AVDictionaryEntry *title = av_dict_get(s->metadata, "title", NULL, 0);

    snprintf(temp_filename, sizeof(temp_filename), "%s.tmp", s->filename);
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
    write_time(out, c->last_duration);
    avio_printf(out, "\">\n");
    avio_printf(out, "\t<ProgramInformation>\n");
    if (title) {
        char *escaped = xmlescape(title->value);
        avio_printf(out, "\t\t<Title>%s</Title>\n", escaped);
        av_free(escaped);
    }
    avio_printf(out, "\t</ProgramInformation>\n");
    if (c->window_size && s->nb_streams > 0 && c->streams[0].nb_segments > 0 && !c->use_template) {
        OutputStream *os = &c->streams[0];
        int start_index = FFMAX(os->nb_segments - c->window_size, 0);
        int64_t start_time = av_rescale_q(os->segments[start_index]->time, s->streams[0]->time_base, AV_TIME_BASE_Q);
        avio_printf(out, "\t<Period start=\"");
        write_time(out, start_time);
        avio_printf(out, "\">\n");
    } else {
        avio_printf(out, "\t<Period start=\"PT0.0S\">\n");
    }

    if (c->has_video) {
        avio_printf(out, "\t\t<AdaptationSet contentType=\"video\" segmentAlignment=\"true\" bitstreamSwitching=\"true\"");
        if (c->max_frame_rate.num && !c->ambiguous_frame_rate)
            avio_printf(out, " %s=\"%d/%d\"", (av_cmp_q(c->min_frame_rate, c->max_frame_rate) < 0) ? "maxFrameRate" : "frameRate", c->max_frame_rate.num, c->max_frame_rate.den);
        avio_printf(out, ">\n");

        for (i = 0; i < s->nb_streams; i++) {
            AVStream *st = s->streams[i];
            OutputStream *os = &c->streams[i];

            if (st->codec->codec_type != AVMEDIA_TYPE_VIDEO)
                continue;

            avio_printf(out, "\t\t\t<Representation id=\"%d\" mimeType=\"video/mp4\" codecs=\"%s\"%s width=\"%d\" height=\"%d\"", i, os->codec_str, os->bandwidth_str, st->codec->width, st->codec->height);
            if (st->avg_frame_rate.num)
                avio_printf(out, " frameRate=\"%d/%d\"", st->avg_frame_rate.num, st->avg_frame_rate.den);
            avio_printf(out, ">\n");

            output_segment_list(&c->streams[i], out, c);
            avio_printf(out, "\t\t\t</Representation>\n");
        }
        avio_printf(out, "\t\t</AdaptationSet>\n");
    }
    if (c->has_audio) {
        avio_printf(out, "\t\t<AdaptationSet contentType=\"audio\" segmentAlignment=\"true\" bitstreamSwitching=\"true\">\n");
        for (i = 0; i < s->nb_streams; i++) {
            AVStream *st = s->streams[i];
            OutputStream *os = &c->streams[i];

            if (st->codec->codec_type != AVMEDIA_TYPE_AUDIO)
                continue;

            avio_printf(out, "\t\t\t<Representation id=\"%d\" mimeType=\"audio/mp4\" codecs=\"%s\"%s audioSamplingRate=\"%d\">\n", i, os->codec_str, os->bandwidth_str, st->codec->sample_rate);
            avio_printf(out, "\t\t\t\t<AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"%d\" />\n", st->codec->channels);
            output_segment_list(&c->streams[i], out, c);
            avio_printf(out, "\t\t\t</Representation>\n");
        }
        avio_printf(out, "\t\t</AdaptationSet>\n");
    }
    avio_printf(out, "\t</Period>\n");
    avio_printf(out, "</MPD>\n");
    avio_flush(out);
    ff_format_io_close(s, &out);
    return ff_rename(temp_filename, s->filename, s);
}

static int dash_write_header(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    int ret = 0, i;
    AVOutputFormat *oformat;
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

    oformat = av_guess_format("mp4", NULL, NULL);
    if (!oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }

    c->streams = av_mallocz(sizeof(*c->streams) * s->nb_streams);
    if (!c->streams) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        AVFormatContext *ctx;
        AVStream *st;
        AVDictionary *opts = NULL;
        char filename[1024];

        os->bit_rate = s->streams[i]->codec->bit_rate ?
                       s->streams[i]->codec->bit_rate :
                       s->streams[i]->codec->rc_max_rate;
        if (os->bit_rate) {
            snprintf(os->bandwidth_str, sizeof(os->bandwidth_str),
                     " bandwidth=\"%d\"", os->bit_rate);
        } else {
            int level = s->strict_std_compliance >= FF_COMPLIANCE_STRICT ?
                        AV_LOG_ERROR : AV_LOG_WARNING;
            av_log(s, level, "No bit rate set for stream %d\n", i);
            if (s->strict_std_compliance >= FF_COMPLIANCE_STRICT) {
                ret = AVERROR(EINVAL);
                goto fail;
            }
        }

        ctx = avformat_alloc_context();
        if (!ctx) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        os->ctx = ctx;
        ctx->oformat = oformat;
        ctx->interrupt_callback = s->interrupt_callback;
        ctx->opaque             = s->opaque;
        ctx->io_close           = s->io_close;
        ctx->io_open            = s->io_open;

        if (!(st = avformat_new_stream(ctx, NULL))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        avcodec_copy_context(st->codec, s->streams[i]->codec);
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        st->time_base = s->streams[i]->time_base;
        ctx->avoid_negative_ts = s->avoid_negative_ts;

        ctx->pb = avio_alloc_context(os->iobuf, sizeof(os->iobuf), AVIO_FLAG_WRITE, os, NULL, dash_write, NULL);
        if (!ctx->pb) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        if (c->single_file) {
            if (c->single_file_name)
                dash_fill_tmpl_params(os->initfile, sizeof(os->initfile), c->single_file_name, i, 0, os->bit_rate, 0);
            else
                snprintf(os->initfile, sizeof(os->initfile), "%s-stream%d.m4s", basename, i);
        } else {
            dash_fill_tmpl_params(os->initfile, sizeof(os->initfile), c->init_seg_name, i, 0, os->bit_rate, 0);
        }
        snprintf(filename, sizeof(filename), "%s%s", c->dirname, os->initfile);
        ret = ffurl_open_whitelist(&os->out, filename, AVIO_FLAG_WRITE, &s->interrupt_callback, NULL, s->protocol_whitelist);
        if (ret < 0)
            goto fail;
        os->init_start_pos = 0;

        av_dict_set(&opts, "movflags", "frag_custom+dash+delay_moov", 0);
        if ((ret = avformat_write_header(ctx, &opts)) < 0) {
             goto fail;
        }
        os->ctx_inited = 1;
        avio_flush(ctx->pb);
        av_dict_free(&opts);

        av_log(s, AV_LOG_VERBOSE, "Representation %d init segment will be written to: %s\n", i, filename);

        s->streams[i]->time_base = st->time_base;
        // If the muxer wants to shift timestamps, request to have them shifted
        // already before being handed to this muxer, so we don't have mismatches
        // between the MPD and the actual segments.
        s->avoid_negative_ts = ctx->avoid_negative_ts;
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
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
        } else if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            c->has_audio = 1;
        }

        set_codec_str(s, st->codec, os->codec_str, sizeof(os->codec_str));
        os->first_pts = AV_NOPTS_VALUE;
        os->max_pts = AV_NOPTS_VALUE;
        os->last_dts = AV_NOPTS_VALUE;
        os->segment_index = 1;
    }

    if (!c->has_video && c->min_seg_duration <= 0) {
        av_log(s, AV_LOG_WARNING, "no video stream and no min seg duration set\n");
        ret = AVERROR(EINVAL);
    }
    ret = write_manifest(s, 0);
    if (!ret)
        av_log(s, AV_LOG_VERBOSE, "Manifest written to: %s\n", s->filename);

fail:
    if (ret)
        dash_free(s);
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
    URLContext *fd;
    int ret;

    ret = ffurl_open_whitelist(&fd, full_path, AVIO_FLAG_READ, &s->interrupt_callback, NULL, s->protocol_whitelist);
    if (ret < 0)
        return;
    if (ffurl_seek(fd, pos, SEEK_SET) != pos) {
        ffurl_close(fd);
        return;
    }
    ret = ffurl_read(fd, buf, 8);
    ffurl_close(fd);
    if (ret < 8)
        return;
    if (AV_RL32(&buf[4]) != MKTAG('s', 'i', 'd', 'x'))
        return;
    *index_length = AV_RB32(&buf[0]);
}

static int update_stream_extradata(AVFormatContext *s, OutputStream *os,
                                   AVCodecContext *codec)
{
    uint8_t *extradata;

    if (os->ctx->streams[0]->codec->extradata_size || !codec->extradata_size)
        return 0;

    extradata = av_malloc(codec->extradata_size);

    if (!extradata)
        return AVERROR(ENOMEM);

    memcpy(extradata, codec->extradata, codec->extradata_size);

    os->ctx->streams[0]->codec->extradata = extradata;
    os->ctx->streams[0]->codec->extradata_size = codec->extradata_size;

    set_codec_str(s, codec, os->codec_str, sizeof(os->codec_str));

    return 0;
}

static int dash_flush(AVFormatContext *s, int final, int stream)
{
    DASHContext *c = s->priv_data;
    int i, ret = 0;
    int cur_flush_segment_index = 0;
    if (stream >= 0)
        cur_flush_segment_index = c->streams[stream].segment_index;

    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        char filename[1024] = "", full_path[1024], temp_path[1024];
        int64_t start_pos;
        int range_length, index_length = 0;

        if (!os->packets_written)
            continue;

        // Flush the single stream that got a keyframe right now.
        // Flush all audio streams as well, in sync with video keyframes,
        // but not the other video streams.
        if (stream >= 0 && i != stream) {
            if (s->streams[i]->codec->codec_type != AVMEDIA_TYPE_AUDIO)
                continue;
            // Make sure we don't flush audio streams multiple times, when
            // all video streams are flushed one at a time.
            if (c->has_video && os->segment_index > cur_flush_segment_index)
                continue;
        }

        if (!os->init_range_length) {
            av_write_frame(os->ctx, NULL);
            os->init_range_length = avio_tell(os->ctx->pb);
            if (!c->single_file) {
                ffurl_close(os->out);
                os->out = NULL;
            }
        }

        start_pos = avio_tell(os->ctx->pb);

        if (!c->single_file) {
            dash_fill_tmpl_params(filename, sizeof(filename), c->media_seg_name, i, os->segment_index, os->bit_rate, os->start_pts);
            snprintf(full_path, sizeof(full_path), "%s%s", c->dirname, filename);
            snprintf(temp_path, sizeof(temp_path), "%s.tmp", full_path);
            ret = ffurl_open_whitelist(&os->out, temp_path, AVIO_FLAG_WRITE, &s->interrupt_callback, NULL, s->protocol_whitelist);
            if (ret < 0)
                break;
            write_styp(os->ctx->pb);
        } else {
            snprintf(full_path, sizeof(full_path), "%s%s", c->dirname, os->initfile);
        }

        av_write_frame(os->ctx, NULL);
        avio_flush(os->ctx->pb);
        os->packets_written = 0;

        range_length = avio_tell(os->ctx->pb) - start_pos;
        if (c->single_file) {
            find_index_range(s, full_path, start_pos, &index_length);
        } else {
            ffurl_close(os->out);
            os->out = NULL;
            ret = ff_rename(temp_path, full_path, s);
            if (ret < 0)
                break;
        }
        add_segment(os, filename, os->start_pts, os->max_pts - os->start_pts, start_pos, range_length, index_length);
        av_log(s, AV_LOG_VERBOSE, "Representation %d media segment %d written to: %s\n", i, os->segment_index, full_path);
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
    int64_t seg_end_duration = (os->segment_index) * (int64_t) c->min_seg_duration;
    int ret;

    ret = update_stream_extradata(s, os, st->codec);
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

    if ((!c->has_video || st->codec->codec_type == AVMEDIA_TYPE_VIDEO) &&
        pkt->flags & AV_PKT_FLAG_KEY && os->packets_written &&
        av_compare_ts(pkt->pts - os->first_pts, st->time_base,
                      seg_end_duration, AV_TIME_BASE_Q) >= 0) {
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

    dash_free(s);
    return 0;
}

#define OFFSET(x) offsetof(DASHContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
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
    .write_header   = dash_write_header,
    .write_packet   = dash_write_packet,
    .write_trailer  = dash_write_trailer,
    .codec_tag      = (const AVCodecTag* const []){ ff_mp4_obj_type, 0 },
    .priv_class     = &dash_class,
};
