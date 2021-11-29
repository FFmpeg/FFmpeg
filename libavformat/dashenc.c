/*
 * MPEG-DASH ISO BMFF segmenter
 * Copyright (c) 2014 Martin Storsjo
 * Copyright (c) 2018 Akamai Technologies, Inc.
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
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/rational.h"
#include "libavutil/time.h"
#include "libavutil/time_internal.h"

#include "av1.h"
#include "avc.h"
#include "avformat.h"
#include "avio_internal.h"
#include "hlsplaylist.h"
#if CONFIG_HTTP_PROTOCOL
#include "http.h"
#endif
#include "internal.h"
#include "isom.h"
#include "os_support.h"
#include "url.h"
#include "vpcc.h"
#include "dash.h"

typedef enum {
    SEGMENT_TYPE_AUTO = 0,
    SEGMENT_TYPE_MP4,
    SEGMENT_TYPE_WEBM,
    SEGMENT_TYPE_NB
} SegmentType;

enum {
    FRAG_TYPE_NONE = 0,
    FRAG_TYPE_EVERY_FRAME,
    FRAG_TYPE_DURATION,
    FRAG_TYPE_PFRAMES,
    FRAG_TYPE_NB
};

#define MPD_PROFILE_DASH 1
#define MPD_PROFILE_DVB  2

typedef struct Segment {
    char file[1024];
    int64_t start_pos;
    int range_length, index_length;
    int64_t time;
    double prog_date_time;
    int64_t duration;
    int n;
} Segment;

typedef struct AdaptationSet {
    int id;
    char *descriptor;
    int64_t seg_duration;
    int64_t frag_duration;
    int frag_type;
    enum AVMediaType media_type;
    AVDictionary *metadata;
    AVRational min_frame_rate, max_frame_rate;
    int ambiguous_frame_rate;
    int64_t max_frag_duration;
    int max_width, max_height;
    int nb_streams;
    AVRational par;
    int trick_idx;
} AdaptationSet;

typedef struct OutputStream {
    AVFormatContext *ctx;
    int ctx_inited, as_idx;
    AVIOContext *out;
    AVCodecParserContext *parser;
    AVCodecContext *parser_avctx;
    int packets_written;
    char initfile[1024];
    int64_t init_start_pos, pos;
    int init_range_length;
    int nb_segments, segments_size, segment_index;
    int64_t seg_duration;
    int64_t frag_duration;
    int64_t last_duration;
    Segment **segments;
    int64_t first_pts, start_pts, max_pts;
    int64_t last_dts, last_pts;
    int last_flags;
    int bit_rate;
    int first_segment_bit_rate;
    SegmentType segment_type;  /* segment type selected for this particular stream */
    const char *format_name;
    const char *extension_name;
    const char *single_file_name;  /* file names selected for this particular stream */
    const char *init_seg_name;
    const char *media_seg_name;

    char codec_str[100];
    int written_len;
    char filename[1024];
    char full_path[1024];
    char temp_path[1024];
    double availability_time_offset;
    AVProducerReferenceTime producer_reference_time;
    char producer_reference_time_str[100];
    int total_pkt_size;
    int64_t total_pkt_duration;
    int muxer_overhead;
    int frag_type;
    int64_t gop_size;
    AVRational sar;
    int coding_dependency;
} OutputStream;

typedef struct DASHContext {
    const AVClass *class;  /* Class for private options. */
    char *adaptation_sets;
    AdaptationSet *as;
    int nb_as;
    int window_size;
    int extra_window_size;
    int64_t seg_duration;
    int64_t frag_duration;
    int remove_at_exit;
    int use_template;
    int use_timeline;
    int single_file;
    OutputStream *streams;
    int has_video;
    int64_t last_duration;
    int64_t total_duration;
    char availability_start_time[100];
    time_t start_time_s;
    int64_t presentation_time_offset;
    char dirname[1024];
    const char *single_file_name;  /* file names as specified in options */
    const char *init_seg_name;
    const char *media_seg_name;
    const char *utc_timing_url;
    const char *method;
    const char *user_agent;
    AVDictionary *http_opts;
    int hls_playlist;
    const char *hls_master_name;
    int http_persistent;
    int master_playlist_created;
    AVIOContext *mpd_out;
    AVIOContext *m3u8_out;
    int streaming;
    int64_t timeout;
    int index_correction;
    AVDictionary *format_options;
    int global_sidx;
    SegmentType segment_type_option;  /* segment type as specified in options */
    int ignore_io_errors;
    int lhls;
    int ldash;
    int master_publish_rate;
    int nr_of_streams_to_flush;
    int nr_of_streams_flushed;
    int frag_type;
    int write_prft;
    int64_t max_gop_size;
    int64_t max_segment_duration;
    int profile;
    int64_t target_latency;
    int target_latency_refid;
    AVRational min_playback_rate;
    AVRational max_playback_rate;
    int64_t update_period;
} DASHContext;

static struct codec_string {
    int id;
    const char *str;
} codecs[] = {
    { AV_CODEC_ID_VP8, "vp8" },
    { AV_CODEC_ID_VP9, "vp9" },
    { AV_CODEC_ID_VORBIS, "vorbis" },
    { AV_CODEC_ID_OPUS, "opus" },
    { AV_CODEC_ID_FLAC, "flac" },
    { 0, NULL }
};

static struct format_string {
    SegmentType segment_type;
    const char *str;
} formats[] = {
    { SEGMENT_TYPE_AUTO, "auto" },
    { SEGMENT_TYPE_MP4, "mp4" },
    { SEGMENT_TYPE_WEBM, "webm" },
    { 0, NULL }
};

static int dashenc_io_open(AVFormatContext *s, AVIOContext **pb, char *filename,
                           AVDictionary **options) {
    DASHContext *c = s->priv_data;
    int http_base_proto = filename ? ff_is_http_proto(filename) : 0;
    int err = AVERROR_MUXER_NOT_FOUND;
    if (!*pb || !http_base_proto || !c->http_persistent) {
        err = s->io_open(s, pb, filename, AVIO_FLAG_WRITE, options);
#if CONFIG_HTTP_PROTOCOL
    } else {
        URLContext *http_url_context = ffio_geturlcontext(*pb);
        av_assert0(http_url_context);
        err = ff_http_do_new_request(http_url_context, filename);
        if (err < 0)
            ff_format_io_close(s, pb);
#endif
    }
    return err;
}

static void dashenc_io_close(AVFormatContext *s, AVIOContext **pb, char *filename) {
    DASHContext *c = s->priv_data;
    int http_base_proto = filename ? ff_is_http_proto(filename) : 0;

    if (!*pb)
        return;

    if (!http_base_proto || !c->http_persistent) {
        ff_format_io_close(s, pb);
#if CONFIG_HTTP_PROTOCOL
    } else {
        URLContext *http_url_context = ffio_geturlcontext(*pb);
        av_assert0(http_url_context);
        avio_flush(*pb);
        ffurl_shutdown(http_url_context, AVIO_FLAG_WRITE);
#endif
    }
}

static const char *get_format_str(SegmentType segment_type) {
    int i;
    for (i = 0; i < SEGMENT_TYPE_NB; i++)
        if (formats[i].segment_type == segment_type)
            return formats[i].str;
    return NULL;
}

static const char *get_extension_str(SegmentType type, int single_file)
{
    switch (type) {

    case SEGMENT_TYPE_MP4:  return single_file ? "mp4" : "m4s";
    case SEGMENT_TYPE_WEBM: return "webm";
    default: return NULL;
    }
}

static int handle_io_open_error(AVFormatContext *s, int err, char *url) {
    DASHContext *c = s->priv_data;
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, errbuf, sizeof(errbuf));
    av_log(s, c->ignore_io_errors ? AV_LOG_WARNING : AV_LOG_ERROR,
           "Unable to open %s for writing: %s\n", url, errbuf);
    return c->ignore_io_errors ? 0 : err;
}

static inline SegmentType select_segment_type(SegmentType segment_type, enum AVCodecID codec_id)
{
    if (segment_type == SEGMENT_TYPE_AUTO) {
        if (codec_id == AV_CODEC_ID_OPUS || codec_id == AV_CODEC_ID_VORBIS ||
            codec_id == AV_CODEC_ID_VP8 || codec_id == AV_CODEC_ID_VP9) {
            segment_type = SEGMENT_TYPE_WEBM;
        } else {
            segment_type = SEGMENT_TYPE_MP4;
        }
    }

    return segment_type;
}

static int init_segment_types(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    int has_mp4_streams = 0;
    for (int i = 0; i < s->nb_streams; ++i) {
        OutputStream *os = &c->streams[i];
        SegmentType segment_type = select_segment_type(
            c->segment_type_option, s->streams[i]->codecpar->codec_id);
        os->segment_type = segment_type;
        os->format_name = get_format_str(segment_type);
        if (!os->format_name) {
            av_log(s, AV_LOG_ERROR, "Could not select DASH segment type for stream %d\n", i);
            return AVERROR_MUXER_NOT_FOUND;
        }
        os->extension_name = get_extension_str(segment_type, c->single_file);
        if (!os->extension_name) {
            av_log(s, AV_LOG_ERROR, "Could not get extension type for stream %d\n", i);
            return AVERROR_MUXER_NOT_FOUND;
        }

        has_mp4_streams |= segment_type == SEGMENT_TYPE_MP4;
    }

    if (c->hls_playlist && !has_mp4_streams) {
         av_log(s, AV_LOG_WARNING, "No mp4 streams, disabling HLS manifest generation\n");
         c->hls_playlist = 0;
    }

    return 0;
}

static void set_vp9_codec_str(AVFormatContext *s, AVCodecParameters *par,
                              AVRational *frame_rate, char *str, int size) {
    VPCC vpcc;
    int ret = ff_isom_get_vpcc_features(s, par, frame_rate, &vpcc);
    if (ret == 0) {
        av_strlcatf(str, size, "vp09.%02d.%02d.%02d",
                    vpcc.profile, vpcc.level, vpcc.bitdepth);
    } else {
        // Default to just vp9 in case of error while finding out profile or level
        av_log(s, AV_LOG_WARNING, "Could not find VP9 profile and/or level\n");
        av_strlcpy(str, "vp9", size);
    }
    return;
}

static void set_codec_str(AVFormatContext *s, AVCodecParameters *par,
                          AVRational *frame_rate, char *str, int size)
{
    const AVCodecTag *tags[2] = { NULL, NULL };
    uint32_t tag;
    int i;

    // common Webm codecs are not part of RFC 6381
    for (i = 0; codecs[i].id; i++)
        if (codecs[i].id == par->codec_id) {
            if (codecs[i].id == AV_CODEC_ID_VP9) {
                set_vp9_codec_str(s, par, frame_rate, str, size);
            } else {
                av_strlcpy(str, codecs[i].str, size);
            }
            return;
        }

    // for codecs part of RFC 6381
    if (par->codec_type == AVMEDIA_TYPE_VIDEO)
        tags[0] = ff_codec_movvideo_tags;
    else if (par->codec_type == AVMEDIA_TYPE_AUDIO)
        tags[0] = ff_codec_movaudio_tags;
    else
        return;

    tag = par->codec_tag;
    if (!tag)
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
    } else if (!strcmp(str, "av01")) {
        AV1SequenceParameters seq;
        if (!par->extradata_size)
            return;
        if (ff_av1_parse_seq_header(&seq, par->extradata, par->extradata_size) < 0)
            return;

        av_strlcatf(str, size, ".%01u.%02u%s.%02u",
                    seq.profile, seq.level, seq.tier ? "H" : "M", seq.bitdepth);
        if (seq.color_description_present_flag)
            av_strlcatf(str, size, ".%01u.%01u%01u%01u.%02u.%02u.%02u.%01u",
                        seq.monochrome,
                        seq.chroma_subsampling_x, seq.chroma_subsampling_y, seq.chroma_sample_position,
                        seq.color_primaries, seq.transfer_characteristics, seq.matrix_coefficients,
                        seq.color_range);
    }
}

static int flush_dynbuf(DASHContext *c, OutputStream *os, int *range_length)
{
    uint8_t *buffer;

    if (!os->ctx->pb) {
        return AVERROR(EINVAL);
    }

    // flush
    av_write_frame(os->ctx, NULL);
    avio_flush(os->ctx->pb);

    if (!c->single_file) {
        // write out to file
        *range_length = avio_close_dyn_buf(os->ctx->pb, &buffer);
        os->ctx->pb = NULL;
        if (os->out)
            avio_write(os->out, buffer + os->written_len, *range_length - os->written_len);
        os->written_len = 0;
        av_free(buffer);

        // re-open buffer
        return avio_open_dyn_buf(&os->ctx->pb);
    } else {
        *range_length = avio_tell(os->ctx->pb) - os->pos;
        return 0;
    }
}

static void set_http_options(AVDictionary **options, DASHContext *c)
{
    if (c->method)
        av_dict_set(options, "method", c->method, 0);
    av_dict_copy(options, c->http_opts, 0);
    if (c->user_agent)
        av_dict_set(options, "user_agent", c->user_agent, 0);
    if (c->http_persistent)
        av_dict_set_int(options, "multiple_requests", 1, 0);
    if (c->timeout >= 0)
        av_dict_set_int(options, "timeout", c->timeout, 0);
}

static void get_hls_playlist_name(char *playlist_name, int string_size,
                                  const char *base_url, int id) {
    if (base_url)
        snprintf(playlist_name, string_size, "%smedia_%d.m3u8", base_url, id);
    else
        snprintf(playlist_name, string_size, "media_%d.m3u8", id);
}

static void get_start_index_number(OutputStream *os, DASHContext *c,
                                   int *start_index, int *start_number) {
    *start_index = 0;
    *start_number = 1;
    if (c->window_size) {
        *start_index  = FFMAX(os->nb_segments   - c->window_size, 0);
        *start_number = FFMAX(os->segment_index - c->window_size, 1);
    }
}

static void write_hls_media_playlist(OutputStream *os, AVFormatContext *s,
                                     int representation_id, int final,
                                     char *prefetch_url) {
    DASHContext *c = s->priv_data;
    int timescale = os->ctx->streams[0]->time_base.den;
    char temp_filename_hls[1024];
    char filename_hls[1024];
    AVDictionary *http_opts = NULL;
    int target_duration = 0;
    int ret = 0;
    const char *proto = avio_find_protocol_name(c->dirname);
    int use_rename = proto && !strcmp(proto, "file");
    int i, start_index, start_number;
    double prog_date_time = 0;

    get_start_index_number(os, c, &start_index, &start_number);

    if (!c->hls_playlist || start_index >= os->nb_segments ||
        os->segment_type != SEGMENT_TYPE_MP4)
        return;

    get_hls_playlist_name(filename_hls, sizeof(filename_hls),
                          c->dirname, representation_id);

    snprintf(temp_filename_hls, sizeof(temp_filename_hls), use_rename ? "%s.tmp" : "%s", filename_hls);

    set_http_options(&http_opts, c);
    ret = dashenc_io_open(s, &c->m3u8_out, temp_filename_hls, &http_opts);
    av_dict_free(&http_opts);
    if (ret < 0) {
        handle_io_open_error(s, ret, temp_filename_hls);
        return;
    }
    for (i = start_index; i < os->nb_segments; i++) {
        Segment *seg = os->segments[i];
        double duration = (double) seg->duration / timescale;
        if (target_duration <= duration)
            target_duration = lrint(duration);
    }

    ff_hls_write_playlist_header(c->m3u8_out, 6, -1, target_duration,
                                 start_number, PLAYLIST_TYPE_NONE, 0);

    ff_hls_write_init_file(c->m3u8_out, os->initfile, c->single_file,
                           os->init_range_length, os->init_start_pos);

    for (i = start_index; i < os->nb_segments; i++) {
        Segment *seg = os->segments[i];

        if (fabs(prog_date_time) < 1e-7) {
            if (os->nb_segments == 1)
                prog_date_time = c->start_time_s;
            else
                prog_date_time = seg->prog_date_time;
        }
        seg->prog_date_time = prog_date_time;

        ret = ff_hls_write_file_entry(c->m3u8_out, 0, c->single_file,
                                (double) seg->duration / timescale, 0,
                                seg->range_length, seg->start_pos, NULL,
                                c->single_file ? os->initfile : seg->file,
                                &prog_date_time, 0, 0, 0);
        if (ret < 0) {
            av_log(os->ctx, AV_LOG_WARNING, "ff_hls_write_file_entry get error\n");
        }
    }

    if (prefetch_url)
        avio_printf(c->m3u8_out, "#EXT-X-PREFETCH:%s\n", prefetch_url);

    if (final)
        ff_hls_write_end_list(c->m3u8_out);

    dashenc_io_close(s, &c->m3u8_out, temp_filename_hls);

    if (use_rename)
        ff_rename(temp_filename_hls, filename_hls, os->ctx);
}

static int flush_init_segment(AVFormatContext *s, OutputStream *os)
{
    DASHContext *c = s->priv_data;
    int ret, range_length;

    ret = flush_dynbuf(c, os, &range_length);
    if (ret < 0)
        return ret;

    os->pos = os->init_range_length = range_length;
    if (!c->single_file) {
        char filename[1024];
        snprintf(filename, sizeof(filename), "%s%s", c->dirname, os->initfile);
        dashenc_io_close(s, &os->out, filename);
    }
    return 0;
}

static void dash_free(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    int i, j;

    if (c->as) {
        for (i = 0; i < c->nb_as; i++) {
            av_dict_free(&c->as[i].metadata);
            av_freep(&c->as[i].descriptor);
        }
        av_freep(&c->as);
        c->nb_as = 0;
    }

    if (!c->streams)
        return;
    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        if (os->ctx && os->ctx->pb) {
            if (!c->single_file)
                ffio_free_dyn_buf(&os->ctx->pb);
            else
                avio_close(os->ctx->pb);
        }
        ff_format_io_close(s, &os->out);
        avformat_free_context(os->ctx);
        avcodec_free_context(&os->parser_avctx);
        av_parser_close(os->parser);
        for (j = 0; j < os->nb_segments; j++)
            av_free(os->segments[j]);
        av_free(os->segments);
        av_freep(&os->single_file_name);
        av_freep(&os->init_seg_name);
        av_freep(&os->media_seg_name);
    }
    av_freep(&c->streams);

    ff_format_io_close(s, &c->mpd_out);
    ff_format_io_close(s, &c->m3u8_out);
}

static void output_segment_list(OutputStream *os, AVIOContext *out, AVFormatContext *s,
                                int representation_id, int final)
{
    DASHContext *c = s->priv_data;
    int i, start_index, start_number;
    get_start_index_number(os, c, &start_index, &start_number);

    if (c->use_template) {
        int timescale = c->use_timeline ? os->ctx->streams[0]->time_base.den : AV_TIME_BASE;
        avio_printf(out, "\t\t\t\t<SegmentTemplate timescale=\"%d\" ", timescale);
        if (!c->use_timeline) {
            avio_printf(out, "duration=\"%"PRId64"\" ", os->seg_duration);
            if (c->streaming && os->availability_time_offset)
                avio_printf(out, "availabilityTimeOffset=\"%.3f\" ",
                            os->availability_time_offset);
        }
        if (c->streaming && os->availability_time_offset && !final)
            avio_printf(out, "availabilityTimeComplete=\"false\" ");

        avio_printf(out, "initialization=\"%s\" media=\"%s\" startNumber=\"%d\"", os->init_seg_name, os->media_seg_name, c->use_timeline ? start_number : 1);
        if (c->presentation_time_offset)
            avio_printf(out, " presentationTimeOffset=\"%"PRId64"\"", c->presentation_time_offset);
        avio_printf(out, ">\n");
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
                avio_printf(out, "d=\"%"PRId64"\" ", seg->duration);
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
        avio_printf(out, "\t\t\t\t<SegmentList timescale=\"%d\" duration=\"%"PRId64"\" startNumber=\"%d\">\n", AV_TIME_BASE, FFMIN(os->seg_duration, os->last_duration), start_number);
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
        avio_printf(out, "\t\t\t\t<SegmentList timescale=\"%d\" duration=\"%"PRId64"\" startNumber=\"%d\">\n", AV_TIME_BASE, FFMIN(os->seg_duration, os->last_duration), start_number);
        avio_printf(out, "\t\t\t\t\t<Initialization sourceURL=\"%s\" />\n", os->initfile);
        for (i = start_index; i < os->nb_segments; i++) {
            Segment *seg = os->segments[i];
            avio_printf(out, "\t\t\t\t\t<SegmentURL media=\"%s\" />\n", seg->file);
        }
        avio_printf(out, "\t\t\t\t</SegmentList>\n");
    }
    if (!c->lhls || final) {
        write_hls_media_playlist(os, s, representation_id, final, NULL);
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

static void format_date(char *buf, int size, int64_t time_us)
{
    struct tm *ptm, tmbuf;
    int64_t time_ms = time_us / 1000;
    const time_t time_s = time_ms / 1000;
    int millisec = time_ms - (time_s * 1000);
    ptm = gmtime_r(&time_s, &tmbuf);
    if (ptm) {
        int len;
        if (!strftime(buf, size, "%Y-%m-%dT%H:%M:%S", ptm)) {
            buf[0] = '\0';
            return;
        }
        len = strlen(buf);
        snprintf(buf + len, size - len, ".%03dZ", millisec);
    }
}

static int write_adaptation_set(AVFormatContext *s, AVIOContext *out, int as_index,
                                int final)
{
    DASHContext *c = s->priv_data;
    AdaptationSet *as = &c->as[as_index];
    AVDictionaryEntry *lang, *role;
    int i;

    avio_printf(out, "\t\t<AdaptationSet id=\"%d\" contentType=\"%s\" startWithSAP=\"1\" segmentAlignment=\"true\" bitstreamSwitching=\"true\"",
                as->id, as->media_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio");
    if (as->media_type == AVMEDIA_TYPE_VIDEO && as->max_frame_rate.num && !as->ambiguous_frame_rate && av_cmp_q(as->min_frame_rate, as->max_frame_rate) < 0)
        avio_printf(out, " maxFrameRate=\"%d/%d\"", as->max_frame_rate.num, as->max_frame_rate.den);
    else if (as->media_type == AVMEDIA_TYPE_VIDEO && as->max_frame_rate.num && !as->ambiguous_frame_rate && !av_cmp_q(as->min_frame_rate, as->max_frame_rate))
        avio_printf(out, " frameRate=\"%d/%d\"", as->max_frame_rate.num, as->max_frame_rate.den);
    if (as->media_type == AVMEDIA_TYPE_VIDEO) {
        avio_printf(out, " maxWidth=\"%d\" maxHeight=\"%d\"", as->max_width, as->max_height);
        avio_printf(out, " par=\"%d:%d\"", as->par.num, as->par.den);
    }
    lang = av_dict_get(as->metadata, "language", NULL, 0);
    if (lang)
        avio_printf(out, " lang=\"%s\"", lang->value);
    avio_printf(out, ">\n");

    if (!final && c->ldash && as->max_frag_duration && !(c->profile & MPD_PROFILE_DVB))
        avio_printf(out, "\t\t\t<Resync dT=\"%"PRId64"\" type=\"0\"/>\n", as->max_frag_duration);
    if (as->trick_idx >= 0)
        avio_printf(out, "\t\t\t<EssentialProperty id=\"%d\" schemeIdUri=\"http://dashif.org/guidelines/trickmode\" value=\"%d\"/>\n", as->id, as->trick_idx);
    role = av_dict_get(as->metadata, "role", NULL, 0);
    if (role)
        avio_printf(out, "\t\t\t<Role schemeIdUri=\"urn:mpeg:dash:role:2011\" value=\"%s\"/>\n", role->value);
    if (as->descriptor)
        avio_printf(out, "\t\t\t%s\n", as->descriptor);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        OutputStream *os = &c->streams[i];
        char bandwidth_str[64] = {'\0'};

        if (os->as_idx - 1 != as_index)
            continue;

        if (os->bit_rate > 0)
            snprintf(bandwidth_str, sizeof(bandwidth_str), " bandwidth=\"%d\"", os->bit_rate);
        else if (final) {
            int average_bit_rate = os->pos * 8 * AV_TIME_BASE / c->total_duration;
            snprintf(bandwidth_str, sizeof(bandwidth_str), " bandwidth=\"%d\"", average_bit_rate);
        } else if (os->first_segment_bit_rate > 0)
            snprintf(bandwidth_str, sizeof(bandwidth_str), " bandwidth=\"%d\"", os->first_segment_bit_rate);

        if (as->media_type == AVMEDIA_TYPE_VIDEO) {
            avio_printf(out, "\t\t\t<Representation id=\"%d\" mimeType=\"video/%s\" codecs=\"%s\"%s width=\"%d\" height=\"%d\"",
                i, os->format_name, os->codec_str, bandwidth_str, s->streams[i]->codecpar->width, s->streams[i]->codecpar->height);
            if (st->codecpar->field_order == AV_FIELD_UNKNOWN)
                avio_printf(out, " scanType=\"unknown\"");
            else if (st->codecpar->field_order != AV_FIELD_PROGRESSIVE)
                avio_printf(out, " scanType=\"interlaced\"");
            avio_printf(out, " sar=\"%d:%d\"", os->sar.num, os->sar.den);
            if (st->avg_frame_rate.num && av_cmp_q(as->min_frame_rate, as->max_frame_rate) < 0)
                avio_printf(out, " frameRate=\"%d/%d\"", st->avg_frame_rate.num, st->avg_frame_rate.den);
            if (as->trick_idx >= 0) {
                AdaptationSet *tas = &c->as[as->trick_idx];
                if (!as->ambiguous_frame_rate && !tas->ambiguous_frame_rate)
                    avio_printf(out, " maxPlayoutRate=\"%d\"", FFMAX((int)av_q2d(av_div_q(tas->min_frame_rate, as->min_frame_rate)), 1));
            }
            if (!os->coding_dependency)
                avio_printf(out, " codingDependency=\"false\"");
            avio_printf(out, ">\n");
        } else {
            avio_printf(out, "\t\t\t<Representation id=\"%d\" mimeType=\"audio/%s\" codecs=\"%s\"%s audioSamplingRate=\"%d\">\n",
                i, os->format_name, os->codec_str, bandwidth_str, s->streams[i]->codecpar->sample_rate);
            avio_printf(out, "\t\t\t\t<AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"%d\" />\n",
                s->streams[i]->codecpar->channels);
        }
        if (!final && c->write_prft && os->producer_reference_time_str[0]) {
            avio_printf(out, "\t\t\t\t<ProducerReferenceTime id=\"%d\" inband=\"true\" type=\"%s\" wallClockTime=\"%s\" presentationTime=\"%"PRId64"\">\n",
                        i, os->producer_reference_time.flags ? "captured" : "encoder", os->producer_reference_time_str, c->presentation_time_offset);
            avio_printf(out, "\t\t\t\t\t<UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"%s\"/>\n", c->utc_timing_url);
            avio_printf(out, "\t\t\t\t</ProducerReferenceTime>\n");
        }
        if (!final && c->ldash && os->gop_size && os->frag_type != FRAG_TYPE_NONE && !(c->profile & MPD_PROFILE_DVB) &&
            (os->frag_type != FRAG_TYPE_DURATION || os->frag_duration != os->seg_duration))
            avio_printf(out, "\t\t\t\t<Resync dT=\"%"PRId64"\" type=\"1\"/>\n", os->gop_size);
        output_segment_list(os, out, s, i, final);
        avio_printf(out, "\t\t\t</Representation>\n");
    }
    avio_printf(out, "\t\t</AdaptationSet>\n");

    return 0;
}

static int add_adaptation_set(AVFormatContext *s, AdaptationSet **as, enum AVMediaType type)
{
    DASHContext *c = s->priv_data;
    void *mem;

    if (c->profile & MPD_PROFILE_DVB && (c->nb_as + 1) > 16) {
        av_log(s, AV_LOG_ERROR, "DVB-DASH profile allows a max of 16 Adaptation Sets\n");
        return AVERROR(EINVAL);
    }
    mem = av_realloc(c->as, sizeof(*c->as) * (c->nb_as + 1));
    if (!mem)
        return AVERROR(ENOMEM);
    c->as = mem;
    ++c->nb_as;

    *as = &c->as[c->nb_as - 1];
    memset(*as, 0, sizeof(**as));
    (*as)->media_type = type;
    (*as)->frag_type = -1;
    (*as)->trick_idx = -1;

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
    if (c->profile & MPD_PROFILE_DVB && (as->nb_streams + 1) > 16) {
        av_log(s, AV_LOG_ERROR, "DVB-DASH profile allows a max of 16 Representations per Adaptation Set\n");
        return AVERROR(EINVAL);
    }
    os->as_idx = as_idx;
    ++as->nb_streams;

    return 0;
}

static int parse_adaptation_sets(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    const char *p = c->adaptation_sets;
    enum { new_set, parse_default, parsing_streams, parse_seg_duration, parse_frag_duration } state;
    AdaptationSet *as;
    int i, n, ret;

    // default: one AdaptationSet for each stream
    if (!p) {
        for (i = 0; i < s->nb_streams; i++) {
            if ((ret = add_adaptation_set(s, &as, s->streams[i]->codecpar->codec_type)) < 0)
                return ret;
            as->id = i;

            c->streams[i].as_idx = c->nb_as;
            ++as->nb_streams;
        }
        goto end;
    }

    // syntax id=0,streams=0,1,2 id=1,streams=3,4 and so on
    // option id=0,descriptor=descriptor_str,streams=0,1,2 and so on
    // option id=0,seg_duration=2.5,frag_duration=0.5,streams=0,1,2
    //        id=1,trick_id=0,seg_duration=10,frag_type=none,streams=3 and so on
    // descriptor is useful to the scheme defined by ISO/IEC 23009-1:2014/Amd.2:2015
    // descriptor_str should be a self-closing xml tag.
    // seg_duration and frag_duration have the same syntax as the global options of
    // the same name, and the former have precedence over them if set.
    state = new_set;
    while (*p) {
        if (*p == ' ') {
            p++;
            continue;
        } else if (state == new_set && av_strstart(p, "id=", &p)) {
            char id_str[10], *end_str;

            n = strcspn(p, ",");
            snprintf(id_str, sizeof(id_str), "%.*s", n, p);

            i = strtol(id_str, &end_str, 10);
            if (id_str == end_str || i < 0 || i > c->nb_as) {
                av_log(s, AV_LOG_ERROR, "\"%s\" is not a valid value for an AdaptationSet id\n", id_str);
                return AVERROR(EINVAL);
            }

            if ((ret = add_adaptation_set(s, &as, AVMEDIA_TYPE_UNKNOWN)) < 0)
                return ret;
            as->id = i;

            p += n;
            if (*p)
                p++;
            state = parse_default;
        } else if (state != new_set && av_strstart(p, "seg_duration=", &p)) {
            state = parse_seg_duration;
        } else if (state != new_set && av_strstart(p, "frag_duration=", &p)) {
            state = parse_frag_duration;
        } else if (state == parse_seg_duration || state == parse_frag_duration) {
            char str[32];
            int64_t usecs = 0;

            n = strcspn(p, ",");
            snprintf(str, sizeof(str), "%.*s", n, p);
            p += n;
            if (*p)
                p++;

            ret = av_parse_time(&usecs, str, 1);
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "Unable to parse option value \"%s\" as duration\n", str);
                return ret;
            }

            if (state == parse_seg_duration)
                as->seg_duration = usecs;
            else
                as->frag_duration = usecs;
            state = parse_default;
        } else if (state != new_set && av_strstart(p, "frag_type=", &p)) {
            char type_str[16];

            n = strcspn(p, ",");
            snprintf(type_str, sizeof(type_str), "%.*s", n, p);
            p += n;
            if (*p)
                p++;

            if (!strcmp(type_str, "duration"))
                as->frag_type = FRAG_TYPE_DURATION;
            else if (!strcmp(type_str, "pframes"))
                as->frag_type = FRAG_TYPE_PFRAMES;
            else if (!strcmp(type_str, "every_frame"))
                as->frag_type = FRAG_TYPE_EVERY_FRAME;
            else if (!strcmp(type_str, "none"))
                as->frag_type = FRAG_TYPE_NONE;
            else {
                av_log(s, AV_LOG_ERROR, "Unable to parse option value \"%s\" as fragment type\n", type_str);
                return ret;
            }
            state = parse_default;
        } else if (state != new_set && av_strstart(p, "descriptor=", &p)) {
            n = strcspn(p, ">") + 1; //followed by one comma, so plus 1
            if (n < strlen(p)) {
                as->descriptor = av_strndup(p, n);
            } else {
                av_log(s, AV_LOG_ERROR, "Parse error, descriptor string should be a self-closing xml tag\n");
                return AVERROR(EINVAL);
            }
            p += n;
            if (*p)
                p++;
            state = parse_default;
        } else if ((state != new_set) && av_strstart(p, "trick_id=", &p)) {
            char trick_id_str[10], *end_str;

            n = strcspn(p, ",");
            snprintf(trick_id_str, sizeof(trick_id_str), "%.*s", n, p);
            p += n;

            as->trick_idx = strtol(trick_id_str, &end_str, 10);
            if (trick_id_str == end_str || as->trick_idx < 0)
                return AVERROR(EINVAL);

            if (*p)
                p++;
            state = parse_default;
        } else if ((state != new_set) && av_strstart(p, "streams=", &p)) { //descriptor and durations are optional
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

    // check references for trick mode AdaptationSet
    for (i = 0; i < c->nb_as; i++) {
        as = &c->as[i];
        if (as->trick_idx < 0)
            continue;
        for (n = 0; n < c->nb_as; n++) {
            if (c->as[n].id == as->trick_idx)
                break;
        }
        if (n >= c->nb_as) {
            av_log(s, AV_LOG_ERROR, "reference AdaptationSet id \"%d\" not found for trick mode AdaptationSet id \"%d\"\n", as->trick_idx, as->id);
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
    const char *proto = avio_find_protocol_name(s->url);
    int use_rename = proto && !strcmp(proto, "file");
    static unsigned int warned_non_file = 0;
    AVDictionaryEntry *title = av_dict_get(s->metadata, "title", NULL, 0);
    AVDictionary *opts = NULL;

    if (!use_rename && !warned_non_file++)
        av_log(s, AV_LOG_ERROR, "Cannot use rename on non file protocol, this may lead to races and temporary partial files\n");

    snprintf(temp_filename, sizeof(temp_filename), use_rename ? "%s.tmp" : "%s", s->url);
    set_http_options(&opts, c);
    ret = dashenc_io_open(s, &c->mpd_out, temp_filename, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        return handle_io_open_error(s, ret, temp_filename);
    }
    out = c->mpd_out;
    avio_printf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    avio_printf(out, "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
                "\txmlns=\"urn:mpeg:dash:schema:mpd:2011\"\n"
                "\txmlns:xlink=\"http://www.w3.org/1999/xlink\"\n"
                "\txsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd\"\n"
                "\tprofiles=\"");
    if (c->profile & MPD_PROFILE_DASH)
         avio_printf(out, "%s%s", "urn:mpeg:dash:profile:isoff-live:2011", c->profile & MPD_PROFILE_DVB ? "," : "\"\n");
    if (c->profile & MPD_PROFILE_DVB)
         avio_printf(out, "%s", "urn:dvb:dash:profile:dvb-dash:2014\"\n");
    avio_printf(out, "\ttype=\"%s\"\n",
                final ? "static" : "dynamic");
    if (final) {
        avio_printf(out, "\tmediaPresentationDuration=\"");
        write_time(out, c->total_duration);
        avio_printf(out, "\"\n");
    } else {
        int64_t update_period = c->last_duration / AV_TIME_BASE;
        char now_str[100];
        if (c->use_template && !c->use_timeline)
            update_period = 500;
        if (c->update_period)
            update_period = c->update_period;
        avio_printf(out, "\tminimumUpdatePeriod=\"PT%"PRId64"S\"\n", update_period);
        if (!c->ldash)
            avio_printf(out, "\tsuggestedPresentationDelay=\"PT%"PRId64"S\"\n", c->last_duration / AV_TIME_BASE);
        if (c->availability_start_time[0])
            avio_printf(out, "\tavailabilityStartTime=\"%s\"\n", c->availability_start_time);
        format_date(now_str, sizeof(now_str), av_gettime());
        if (now_str[0])
            avio_printf(out, "\tpublishTime=\"%s\"\n", now_str);
        if (c->window_size && c->use_template) {
            avio_printf(out, "\ttimeShiftBufferDepth=\"");
            write_time(out, c->last_duration * c->window_size);
            avio_printf(out, "\"\n");
        }
    }
    avio_printf(out, "\tmaxSegmentDuration=\"");
    write_time(out, c->max_segment_duration);
    avio_printf(out, "\"\n");
    avio_printf(out, "\tminBufferTime=\"");
    write_time(out, c->ldash && c->max_gop_size ? c->max_gop_size : c->last_duration * 2);
    avio_printf(out, "\">\n");
    avio_printf(out, "\t<ProgramInformation>\n");
    if (title) {
        char *escaped = xmlescape(title->value);
        avio_printf(out, "\t\t<Title>%s</Title>\n", escaped);
        av_free(escaped);
    }
    avio_printf(out, "\t</ProgramInformation>\n");

    avio_printf(out, "\t<ServiceDescription id=\"0\">\n");
    if (!final && c->target_latency && c->target_latency_refid >= 0) {
        avio_printf(out, "\t\t<Latency target=\"%"PRId64"\"", c->target_latency / 1000);
        if (s->nb_streams > 1)
            avio_printf(out, " referenceId=\"%d\"", c->target_latency_refid);
        avio_printf(out, "/>\n");
    }
    if (av_cmp_q(c->min_playback_rate, (AVRational) {1, 1}) ||
        av_cmp_q(c->max_playback_rate, (AVRational) {1, 1}))
        avio_printf(out, "\t\t<PlaybackRate min=\"%.2f\" max=\"%.2f\"/>\n",
                    av_q2d(c->min_playback_rate), av_q2d(c->max_playback_rate));
    avio_printf(out, "\t</ServiceDescription>\n");

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
        if ((ret = write_adaptation_set(s, out, i, final)) < 0)
            return ret;
    }
    avio_printf(out, "\t</Period>\n");

    if (c->utc_timing_url)
        avio_printf(out, "\t<UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"%s\"/>\n", c->utc_timing_url);

    avio_printf(out, "</MPD>\n");
    avio_flush(out);
    dashenc_io_close(s, &c->mpd_out, temp_filename);

    if (use_rename) {
        if ((ret = ff_rename(temp_filename, s->url, s)) < 0)
            return ret;
    }

    if (c->hls_playlist) {
        char filename_hls[1024];

        // Publish master playlist only the configured rate
        if (c->master_playlist_created && (!c->master_publish_rate ||
            c->streams[0].segment_index % c->master_publish_rate))
            return 0;

        if (*c->dirname)
            snprintf(filename_hls, sizeof(filename_hls), "%s%s", c->dirname, c->hls_master_name);
        else
            snprintf(filename_hls, sizeof(filename_hls), "%s", c->hls_master_name);

        snprintf(temp_filename, sizeof(temp_filename), use_rename ? "%s.tmp" : "%s", filename_hls);

        set_http_options(&opts, c);
        ret = dashenc_io_open(s, &c->m3u8_out, temp_filename, &opts);
        av_dict_free(&opts);
        if (ret < 0) {
            return handle_io_open_error(s, ret, temp_filename);
        }

        ff_hls_write_playlist_version(c->m3u8_out, 7);

        if (c->has_video) {
            // treat audio streams as alternative renditions for video streams
            const char *audio_group = "A1";
            char audio_codec_str[128] = "\0";
            int is_default = 1;
            int max_audio_bitrate = 0;

            for (i = 0; i < s->nb_streams; i++) {
                char playlist_file[64];
                AVStream *st = s->streams[i];
                OutputStream *os = &c->streams[i];
                if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                    continue;
                if (os->segment_type != SEGMENT_TYPE_MP4)
                    continue;
                get_hls_playlist_name(playlist_file, sizeof(playlist_file), NULL, i);
                ff_hls_write_audio_rendition(c->m3u8_out, (char *)audio_group,
                                             playlist_file, NULL, i, is_default);
                max_audio_bitrate = FFMAX(st->codecpar->bit_rate +
                                          os->muxer_overhead, max_audio_bitrate);
                if (!av_strnstr(audio_codec_str, os->codec_str, sizeof(audio_codec_str))) {
                    if (strlen(audio_codec_str))
                        av_strlcat(audio_codec_str, ",", sizeof(audio_codec_str));
                    av_strlcat(audio_codec_str, os->codec_str, sizeof(audio_codec_str));
                }
                is_default = 0;
            }

            for (i = 0; i < s->nb_streams; i++) {
                char playlist_file[64];
                char codec_str[128];
                AVStream *st = s->streams[i];
                OutputStream *os = &c->streams[i];
                char *agroup = NULL;
                int stream_bitrate = os->muxer_overhead;
                if (os->bit_rate > 0)
                    stream_bitrate += os->bit_rate;
                else if (final)
                    stream_bitrate += os->pos * 8 * AV_TIME_BASE / c->total_duration;
                else if (os->first_segment_bit_rate > 0)
                    stream_bitrate += os->first_segment_bit_rate;
                if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
                    continue;
                if (os->segment_type != SEGMENT_TYPE_MP4)
                    continue;
                av_strlcpy(codec_str, os->codec_str, sizeof(codec_str));
                if (max_audio_bitrate) {
                    agroup = (char *)audio_group;
                    stream_bitrate += max_audio_bitrate;
                    av_strlcat(codec_str, ",", sizeof(codec_str));
                    av_strlcat(codec_str, audio_codec_str, sizeof(codec_str));
                }
                get_hls_playlist_name(playlist_file, sizeof(playlist_file), NULL, i);
                ff_hls_write_stream_info(st, c->m3u8_out, stream_bitrate,
                                         playlist_file, agroup,
                                         codec_str, NULL, NULL);
            }

        } else {
            // treat audio streams as separate renditions

            for (i = 0; i < s->nb_streams; i++) {
                char playlist_file[64];
                char codec_str[128];
                AVStream *st = s->streams[i];
                OutputStream *os = &c->streams[i];
                int stream_bitrate = os->muxer_overhead;
                if (os->bit_rate > 0)
                    stream_bitrate += os->bit_rate;
                else if (final)
                    stream_bitrate += os->pos * 8 * AV_TIME_BASE / c->total_duration;
                else if (os->first_segment_bit_rate > 0)
                    stream_bitrate += os->first_segment_bit_rate;
                if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                    continue;
                if (os->segment_type != SEGMENT_TYPE_MP4)
                    continue;
                av_strlcpy(codec_str, os->codec_str, sizeof(codec_str));
                get_hls_playlist_name(playlist_file, sizeof(playlist_file), NULL, i);
                ff_hls_write_stream_info(st, c->m3u8_out, stream_bitrate,
                                         playlist_file, NULL,
                                         codec_str, NULL, NULL);
            }
        }

        dashenc_io_close(s, &c->m3u8_out, temp_filename);
        if (use_rename)
            if ((ret = ff_rename(temp_filename, filename_hls, s)) < 0)
                return ret;
        c->master_playlist_created = 1;
    }

    return 0;
}

static int dict_copy_entry(AVDictionary **dst, const AVDictionary *src, const char *key)
{
    AVDictionaryEntry *entry = av_dict_get(src, key, NULL, 0);
    if (entry)
        av_dict_set(dst, key, entry->value, AV_DICT_DONT_OVERWRITE);
    return 0;
}

static int dash_init(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    int ret = 0, i;
    char *ptr;
    char basename[1024];

    c->nr_of_streams_to_flush = 0;
    if (c->single_file_name)
        c->single_file = 1;
    if (c->single_file)
        c->use_template = 0;

    if (!c->profile) {
        av_log(s, AV_LOG_ERROR, "At least one profile must be enabled.\n");
        return AVERROR(EINVAL);
    }
    if (c->lhls && s->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(s, AV_LOG_ERROR,
               "LHLS is experimental, Please set -strict experimental in order to enable it.\n");
        return AVERROR_EXPERIMENTAL;
    }

    if (c->lhls && !c->streaming) {
        av_log(s, AV_LOG_WARNING, "Enabling streaming as LHLS is enabled\n");
        c->streaming = 1;
    }

    if (c->lhls && !c->hls_playlist) {
        av_log(s, AV_LOG_INFO, "Enabling hls_playlist as LHLS is enabled\n");
        c->hls_playlist = 1;
    }

    if (c->ldash && !c->streaming) {
        av_log(s, AV_LOG_WARNING, "Enabling streaming as LDash is enabled\n");
        c->streaming = 1;
    }

    if (c->target_latency && !c->streaming) {
        av_log(s, AV_LOG_WARNING, "Target latency option will be ignored as streaming is not enabled\n");
        c->target_latency = 0;
    }

    if (c->global_sidx && !c->single_file) {
        av_log(s, AV_LOG_WARNING, "Global SIDX option will be ignored as single_file is not enabled\n");
        c->global_sidx = 0;
    }

    if (c->global_sidx && c->streaming) {
        av_log(s, AV_LOG_WARNING, "Global SIDX option will be ignored as streaming is enabled\n");
        c->global_sidx = 0;
    }
    if (c->frag_type == FRAG_TYPE_NONE && c->streaming) {
        av_log(s, AV_LOG_VERBOSE, "Changing frag_type from none to every_frame as streaming is enabled\n");
        c->frag_type = FRAG_TYPE_EVERY_FRAME;
    }

    if (c->write_prft < 0) {
        c->write_prft = c->ldash;
        if (c->ldash)
            av_log(s, AV_LOG_VERBOSE, "Enabling Producer Reference Time element for Low Latency mode\n");
    }

    if (c->write_prft && !c->utc_timing_url) {
        av_log(s, AV_LOG_WARNING, "Producer Reference Time element option will be ignored as utc_timing_url is not set\n");
        c->write_prft = 0;
    }

    if (c->write_prft && !c->streaming) {
        av_log(s, AV_LOG_WARNING, "Producer Reference Time element option will be ignored as streaming is not enabled\n");
        c->write_prft = 0;
    }

    if (c->ldash && !c->write_prft) {
        av_log(s, AV_LOG_WARNING, "Low Latency mode enabled without Producer Reference Time element option! Resulting manifest may not be complaint\n");
    }

    if (c->target_latency && !c->write_prft) {
        av_log(s, AV_LOG_WARNING, "Target latency option will be ignored as Producer Reference Time element will not be written\n");
        c->target_latency = 0;
    }

    if (av_cmp_q(c->max_playback_rate, c->min_playback_rate) < 0) {
        av_log(s, AV_LOG_WARNING, "Minimum playback rate value is higer than the Maximum. Both will be ignored\n");
        c->min_playback_rate = c->max_playback_rate = (AVRational) {1, 1};
    }

    av_strlcpy(c->dirname, s->url, sizeof(c->dirname));
    ptr = strrchr(c->dirname, '/');
    if (ptr) {
        av_strlcpy(basename, &ptr[1], sizeof(basename));
        ptr[1] = '\0';
    } else {
        c->dirname[0] = '\0';
        av_strlcpy(basename, s->url, sizeof(basename));
    }

    ptr = strrchr(basename, '.');
    if (ptr)
        *ptr = '\0';

    c->streams = av_mallocz(sizeof(*c->streams) * s->nb_streams);
    if (!c->streams)
        return AVERROR(ENOMEM);

    if ((ret = parse_adaptation_sets(s)) < 0)
        return ret;

    if ((ret = init_segment_types(s)) < 0)
        return ret;

    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        AdaptationSet *as = &c->as[os->as_idx - 1];
        AVFormatContext *ctx;
        AVStream *st;
        AVDictionary *opts = NULL;
        char filename[1024];

        os->bit_rate = s->streams[i]->codecpar->bit_rate;
        if (!os->bit_rate) {
            int level = s->strict_std_compliance >= FF_COMPLIANCE_STRICT ?
                        AV_LOG_ERROR : AV_LOG_WARNING;
            av_log(s, level, "No bit rate set for stream %d\n", i);
            if (s->strict_std_compliance >= FF_COMPLIANCE_STRICT)
                return AVERROR(EINVAL);
        }

        // copy AdaptationSet language and role from stream metadata
        dict_copy_entry(&as->metadata, s->streams[i]->metadata, "language");
        dict_copy_entry(&as->metadata, s->streams[i]->metadata, "role");

        if (c->init_seg_name) {
            os->init_seg_name = av_strireplace(c->init_seg_name, "$ext$", os->extension_name);
            if (!os->init_seg_name)
                return AVERROR(ENOMEM);
        }
        if (c->media_seg_name) {
            os->media_seg_name = av_strireplace(c->media_seg_name, "$ext$", os->extension_name);
            if (!os->media_seg_name)
                return AVERROR(ENOMEM);
        }
        if (c->single_file_name) {
            os->single_file_name = av_strireplace(c->single_file_name, "$ext$", os->extension_name);
            if (!os->single_file_name)
                return AVERROR(ENOMEM);
        }

        if (os->segment_type == SEGMENT_TYPE_WEBM) {
            if ((!c->single_file && !av_match_ext(os->init_seg_name, os->format_name))  ||
                (!c->single_file && !av_match_ext(os->media_seg_name, os->format_name)) ||
                ( c->single_file && !av_match_ext(os->single_file_name, os->format_name))) {
                av_log(s, AV_LOG_WARNING,
                       "One or many segment file names doesn't end with .webm. "
                       "Override -init_seg_name and/or -media_seg_name and/or "
                       "-single_file_name to end with the extension .webm\n");
            }
            if (c->streaming) {
                // Streaming not supported as matroskaenc buffers internally before writing the output
                av_log(s, AV_LOG_WARNING, "One or more streams in WebM output format. Streaming option will be ignored\n");
                c->streaming = 0;
            }
        }

        os->ctx = ctx = avformat_alloc_context();
        if (!ctx)
            return AVERROR(ENOMEM);

        ctx->oformat = av_guess_format(os->format_name, NULL, NULL);
        if (!ctx->oformat)
            return AVERROR_MUXER_NOT_FOUND;
        ctx->interrupt_callback    = s->interrupt_callback;
        ctx->opaque                = s->opaque;
        ctx->io_close              = s->io_close;
        ctx->io_close2             = s->io_close2;
        ctx->io_open               = s->io_open;
        ctx->strict_std_compliance = s->strict_std_compliance;

        if (!(st = avformat_new_stream(ctx, NULL)))
            return AVERROR(ENOMEM);
        avcodec_parameters_copy(st->codecpar, s->streams[i]->codecpar);
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        st->time_base = s->streams[i]->time_base;
        st->avg_frame_rate = s->streams[i]->avg_frame_rate;
        ctx->avoid_negative_ts = s->avoid_negative_ts;
        ctx->flags = s->flags;

        os->parser = av_parser_init(st->codecpar->codec_id);
        if (os->parser) {
            os->parser_avctx = avcodec_alloc_context3(NULL);
            if (!os->parser_avctx)
                return AVERROR(ENOMEM);
            ret = avcodec_parameters_to_context(os->parser_avctx, st->codecpar);
            if (ret < 0)
                return ret;
            // We only want to parse frame headers
            os->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
        }

        if (c->single_file) {
            if (os->single_file_name)
                ff_dash_fill_tmpl_params(os->initfile, sizeof(os->initfile), os->single_file_name, i, 0, os->bit_rate, 0);
            else
                snprintf(os->initfile, sizeof(os->initfile), "%s-stream%d.%s", basename, i, os->format_name);
        } else {
            ff_dash_fill_tmpl_params(os->initfile, sizeof(os->initfile), os->init_seg_name, i, 0, os->bit_rate, 0);
        }
        snprintf(filename, sizeof(filename), "%s%s", c->dirname, os->initfile);
        set_http_options(&opts, c);
        if (!c->single_file) {
            if ((ret = avio_open_dyn_buf(&ctx->pb)) < 0)
                return ret;
            ret = s->io_open(s, &os->out, filename, AVIO_FLAG_WRITE, &opts);
        } else {
            ctx->url = av_strdup(filename);
            ret = avio_open2(&ctx->pb, filename, AVIO_FLAG_WRITE, NULL, &opts);
        }
        av_dict_free(&opts);
        if (ret < 0)
            return ret;
        os->init_start_pos = 0;

        av_dict_copy(&opts, c->format_options, 0);
        if (!as->seg_duration)
            as->seg_duration = c->seg_duration;
        if (!as->frag_duration)
            as->frag_duration = c->frag_duration;
        if (as->frag_type < 0)
            as->frag_type = c->frag_type;
        os->seg_duration = as->seg_duration;
        os->frag_duration = as->frag_duration;
        os->frag_type = as->frag_type;

        c->max_segment_duration = FFMAX(c->max_segment_duration, as->seg_duration);

        if (c->profile & MPD_PROFILE_DVB && (os->seg_duration > 15000000 || os->seg_duration < 960000)) {
            av_log(s, AV_LOG_ERROR, "Segment duration %"PRId64" is outside the allowed range for DVB-DASH profile\n", os->seg_duration);
            return AVERROR(EINVAL);
        }

        if (os->frag_type == FRAG_TYPE_DURATION && !os->frag_duration) {
            av_log(s, AV_LOG_WARNING, "frag_type set to duration for stream %d but no frag_duration set\n", i);
            os->frag_type = c->streaming ? FRAG_TYPE_EVERY_FRAME : FRAG_TYPE_NONE;
        }
        if (os->frag_type == FRAG_TYPE_DURATION && os->frag_duration > os->seg_duration) {
            av_log(s, AV_LOG_ERROR, "Fragment duration %"PRId64" is longer than Segment duration %"PRId64"\n", os->frag_duration, os->seg_duration);
            return AVERROR(EINVAL);
        }
        if (os->frag_type == FRAG_TYPE_PFRAMES && (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO || !os->parser)) {
            if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !os->parser)
                av_log(s, AV_LOG_WARNING, "frag_type set to P-Frame reordering, but no parser found for stream %d\n", i);
            os->frag_type = c->streaming ? FRAG_TYPE_EVERY_FRAME : FRAG_TYPE_NONE;
        }
        if (os->frag_type != FRAG_TYPE_PFRAMES && as->trick_idx < 0)
            // Set this now if a parser isn't used
            os->coding_dependency = 1;

        if (os->segment_type == SEGMENT_TYPE_MP4) {
            if (c->streaming)
                // skip_sidx : Reduce bitrate overhead
                // skip_trailer : Avoids growing memory usage with time
                av_dict_set(&opts, "movflags", "+dash+delay_moov+skip_sidx+skip_trailer", AV_DICT_APPEND);
            else {
                if (c->global_sidx)
                    av_dict_set(&opts, "movflags", "+dash+delay_moov+global_sidx+skip_trailer", AV_DICT_APPEND);
                else
                    av_dict_set(&opts, "movflags", "+dash+delay_moov+skip_trailer", AV_DICT_APPEND);
            }
            if (os->frag_type == FRAG_TYPE_EVERY_FRAME)
                av_dict_set(&opts, "movflags", "+frag_every_frame", AV_DICT_APPEND);
            else
                av_dict_set(&opts, "movflags", "+frag_custom", AV_DICT_APPEND);
            if (os->frag_type == FRAG_TYPE_DURATION)
                av_dict_set_int(&opts, "frag_duration", os->frag_duration, 0);
            if (c->write_prft)
                av_dict_set(&opts, "write_prft", "wallclock", 0);
        } else {
            av_dict_set_int(&opts, "cluster_time_limit", c->seg_duration / 1000, 0);
            av_dict_set_int(&opts, "cluster_size_limit", 5 * 1024 * 1024, 0); // set a large cluster size limit
            av_dict_set_int(&opts, "dash", 1, 0);
            av_dict_set_int(&opts, "dash_track_number", i + 1, 0);
            av_dict_set_int(&opts, "live", 1, 0);
        }
        ret = avformat_init_output(ctx, &opts);
        av_dict_free(&opts);
        if (ret < 0)
            return ret;
        os->ctx_inited = 1;
        avio_flush(ctx->pb);

        av_log(s, AV_LOG_VERBOSE, "Representation %d init segment will be written to: %s\n", i, filename);

        s->streams[i]->time_base = st->time_base;
        // If the muxer wants to shift timestamps, request to have them shifted
        // already before being handed to this muxer, so we don't have mismatches
        // between the MPD and the actual segments.
        s->avoid_negative_ts = ctx->avoid_negative_ts;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            AVRational avg_frame_rate = s->streams[i]->avg_frame_rate;
            AVRational par;
            if (avg_frame_rate.num > 0) {
                if (av_cmp_q(avg_frame_rate, as->min_frame_rate) < 0)
                    as->min_frame_rate = avg_frame_rate;
                if (av_cmp_q(as->max_frame_rate, avg_frame_rate) < 0)
                    as->max_frame_rate = avg_frame_rate;
            } else {
                as->ambiguous_frame_rate = 1;
            }

            if (st->codecpar->width > as->max_width)
                as->max_width = st->codecpar->width;
            if (st->codecpar->height > as->max_height)
                as->max_height = st->codecpar->height;

            if (st->sample_aspect_ratio.num)
                os->sar = st->sample_aspect_ratio;
            else
                os->sar = (AVRational){1,1};
            av_reduce(&par.num, &par.den,
                      st->codecpar->width * (int64_t)os->sar.num,
                      st->codecpar->height * (int64_t)os->sar.den,
                      1024 * 1024);

            if (as->par.num && av_cmp_q(par, as->par)) {
                av_log(s, AV_LOG_ERROR, "Conflicting stream aspect ratios values in Adaptation Set %d. Please ensure all adaptation sets have the same aspect ratio\n", os->as_idx);
                return AVERROR(EINVAL);
            }
            as->par = par;

            c->has_video = 1;
        }

        set_codec_str(s, st->codecpar, &st->avg_frame_rate, os->codec_str,
                      sizeof(os->codec_str));
        os->first_pts = AV_NOPTS_VALUE;
        os->max_pts = AV_NOPTS_VALUE;
        os->last_dts = AV_NOPTS_VALUE;
        os->segment_index = 1;

        if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            c->nr_of_streams_to_flush++;
    }

    if (!c->has_video && c->seg_duration <= 0) {
        av_log(s, AV_LOG_WARNING, "no video stream and no seg duration set\n");
        return AVERROR(EINVAL);
    }
    if (!c->has_video && c->frag_type == FRAG_TYPE_PFRAMES)
        av_log(s, AV_LOG_WARNING, "no video stream and P-frame fragmentation set\n");

    c->nr_of_streams_flushed = 0;
    c->target_latency_refid = -1;

    return 0;
}

static int dash_write_header(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    int i, ret;
    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        if ((ret = avformat_write_header(os->ctx, NULL)) < 0)
            return ret;

        // Flush init segment
        // Only for WebM segment, since for mp4 delay_moov is set and
        // the init segment is thus flushed after the first packets.
        if (os->segment_type == SEGMENT_TYPE_WEBM &&
            (ret = flush_init_segment(s, os)) < 0)
            return ret;
    }
    return ret;
}

static int add_segment(OutputStream *os, const char *file,
                       int64_t time, int64_t duration,
                       int64_t start_pos, int64_t range_length,
                       int64_t index_length, int next_exp_index)
{
    int err;
    Segment *seg;
    if (os->nb_segments >= os->segments_size) {
        os->segments_size = (os->segments_size + 1) * 2;
        if ((err = av_reallocp_array(&os->segments, sizeof(*os->segments),
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
    //correcting the segment index if it has fallen behind the expected value
    if (os->segment_index < next_exp_index) {
        av_log(NULL, AV_LOG_WARNING, "Correcting the segment index after file %s: current=%d corrected=%d\n",
               file, os->segment_index, next_exp_index);
        os->segment_index = next_exp_index;
    }
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
                                   AVPacket *pkt, AVRational *frame_rate)
{
    AVCodecParameters *par = os->ctx->streams[0]->codecpar;
    uint8_t *extradata;
    size_t extradata_size;
    int ret;

    if (par->extradata_size)
        return 0;

    extradata = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, &extradata_size);
    if (!extradata_size)
        return 0;

    ret = ff_alloc_extradata(par, extradata_size);
    if (ret < 0)
        return ret;

    memcpy(par->extradata, extradata, extradata_size);

    set_codec_str(s, par, frame_rate, os->codec_str, sizeof(os->codec_str));

    return 0;
}

static void dashenc_delete_file(AVFormatContext *s, char *filename) {
    DASHContext *c = s->priv_data;
    int http_base_proto = ff_is_http_proto(filename);

    if (http_base_proto) {
        AVIOContext *out = NULL;
        AVDictionary *http_opts = NULL;

        set_http_options(&http_opts, c);
        av_dict_set(&http_opts, "method", "DELETE", 0);

        if (dashenc_io_open(s, &out, filename, &http_opts) < 0) {
            av_log(s, AV_LOG_ERROR, "failed to delete %s\n", filename);
        }

        av_dict_free(&http_opts);
        ff_format_io_close(s, &out);
    } else {
        int res = ffurl_delete(filename);
        if (res < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(res, errbuf, sizeof(errbuf));
            av_log(s, (res == AVERROR(ENOENT) ? AV_LOG_WARNING : AV_LOG_ERROR), "failed to delete %s: %s\n", filename, errbuf);
        }
    }
}

static int dashenc_delete_segment_file(AVFormatContext *s, const char* file)
{
    DASHContext *c = s->priv_data;
    AVBPrint buf;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    av_bprintf(&buf, "%s%s", c->dirname, file);
    if (!av_bprint_is_complete(&buf)) {
        av_bprint_finalize(&buf, NULL);
        av_log(s, AV_LOG_WARNING, "Out of memory for filename\n");
        return AVERROR(ENOMEM);
    }

    dashenc_delete_file(s, buf.str);

    av_bprint_finalize(&buf, NULL);
    return 0;
}

static inline void dashenc_delete_media_segments(AVFormatContext *s, OutputStream *os, int remove_count)
{
    for (int i = 0; i < remove_count; ++i) {
        dashenc_delete_segment_file(s, os->segments[i]->file);

        // Delete the segment regardless of whether the file was successfully deleted
        av_free(os->segments[i]);
    }

    os->nb_segments -= remove_count;
    memmove(os->segments, os->segments + remove_count, os->nb_segments * sizeof(*os->segments));
}

static int dash_flush(AVFormatContext *s, int final, int stream)
{
    DASHContext *c = s->priv_data;
    int i, ret = 0;

    const char *proto = avio_find_protocol_name(s->url);
    int use_rename = proto && !strcmp(proto, "file");

    int cur_flush_segment_index = 0, next_exp_index = -1;
    if (stream >= 0) {
        cur_flush_segment_index = c->streams[stream].segment_index;

        //finding the next segment's expected index, based on the current pts value
        if (c->use_template && !c->use_timeline && c->index_correction &&
            c->streams[stream].last_pts != AV_NOPTS_VALUE &&
            c->streams[stream].first_pts != AV_NOPTS_VALUE) {
            int64_t pts_diff = av_rescale_q(c->streams[stream].last_pts -
                                            c->streams[stream].first_pts,
                                            s->streams[stream]->time_base,
                                            AV_TIME_BASE_Q);
            next_exp_index = (pts_diff / c->streams[stream].seg_duration) + 1;
        }
    }

    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        AVStream *st = s->streams[i];
        int range_length, index_length = 0;
        int64_t duration;

        if (!os->packets_written)
            continue;

        // Flush the single stream that got a keyframe right now.
        // Flush all audio streams as well, in sync with video keyframes,
        // but not the other video streams.
        if (stream >= 0 && i != stream) {
            if (s->streams[stream]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
                s->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
                continue;
            if (s->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                continue;
            // Make sure we don't flush audio streams multiple times, when
            // all video streams are flushed one at a time.
            if (c->has_video && os->segment_index > cur_flush_segment_index)
                continue;
        }

        if (c->single_file)
            snprintf(os->full_path, sizeof(os->full_path), "%s%s", c->dirname, os->initfile);

        ret = flush_dynbuf(c, os, &range_length);
        if (ret < 0)
            break;
        os->packets_written = 0;

        if (c->single_file) {
            find_index_range(s, os->full_path, os->pos, &index_length);
        } else {
            dashenc_io_close(s, &os->out, os->temp_path);

            if (use_rename) {
                ret = ff_rename(os->temp_path, os->full_path, os->ctx);
                if (ret < 0)
                    break;
            }
        }

        duration = av_rescale_q(os->max_pts - os->start_pts, st->time_base, AV_TIME_BASE_Q);
        os->last_duration = FFMAX(os->last_duration, duration);

        if (!os->muxer_overhead && os->max_pts > os->start_pts)
            os->muxer_overhead = ((int64_t) (range_length - os->total_pkt_size) *
                                  8 * AV_TIME_BASE) / duration;
        os->total_pkt_size = 0;
        os->total_pkt_duration = 0;

        if (!os->bit_rate && !os->first_segment_bit_rate) {
            os->first_segment_bit_rate = (int64_t) range_length * 8 * AV_TIME_BASE / duration;
        }
        add_segment(os, os->filename, os->start_pts, os->max_pts - os->start_pts, os->pos, range_length, index_length, next_exp_index);
        av_log(s, AV_LOG_VERBOSE, "Representation %d media segment %d written to: %s\n", i, os->segment_index, os->full_path);

        os->pos += range_length;
    }

    if (c->window_size) {
        for (i = 0; i < s->nb_streams; i++) {
            OutputStream *os = &c->streams[i];
            int remove_count = os->nb_segments - c->window_size - c->extra_window_size;
            if (remove_count > 0)
                dashenc_delete_media_segments(s, os, remove_count);
        }
    }

    if (final) {
        for (i = 0; i < s->nb_streams; i++) {
            OutputStream *os = &c->streams[i];
            if (os->ctx && os->ctx_inited) {
                int64_t file_size = avio_tell(os->ctx->pb);
                av_write_trailer(os->ctx);
                if (c->global_sidx) {
                    int j, start_index, start_number;
                    int64_t sidx_size = avio_tell(os->ctx->pb) - file_size;
                    get_start_index_number(os, c, &start_index, &start_number);
                    if (start_index >= os->nb_segments ||
                        os->segment_type != SEGMENT_TYPE_MP4)
                        continue;
                    os->init_range_length += sidx_size;
                    for (j = start_index; j < os->nb_segments; j++) {
                        Segment *seg = os->segments[j];
                        seg->start_pos += sidx_size;
                    }
                }

            }
        }
    }
    if (ret >= 0) {
        if (c->has_video && !final) {
            c->nr_of_streams_flushed++;
            if (c->nr_of_streams_flushed != c->nr_of_streams_to_flush)
                return ret;

            c->nr_of_streams_flushed = 0;
        }
        // In streaming mode the manifest is written at the beginning
        // of the segment instead
        if (!c->streaming || final)
            ret = write_manifest(s, final);
    }
    return ret;
}

static int dash_parse_prft(DASHContext *c, AVPacket *pkt)
{
    OutputStream *os = &c->streams[pkt->stream_index];
    AVProducerReferenceTime *prft;
    size_t side_data_size;

    prft = (AVProducerReferenceTime *)av_packet_get_side_data(pkt, AV_PKT_DATA_PRFT, &side_data_size);
    if (!prft || side_data_size != sizeof(AVProducerReferenceTime) || (prft->flags && prft->flags != 24)) {
        // No encoder generated or user provided capture time AVProducerReferenceTime side data. Instead
        // of letting the mov muxer generate one, do it here so we can also use it for the manifest.
        prft = (AVProducerReferenceTime *)av_packet_new_side_data(pkt, AV_PKT_DATA_PRFT,
                                                                  sizeof(AVProducerReferenceTime));
        if (!prft)
            return AVERROR(ENOMEM);
        prft->wallclock = av_gettime();
        prft->flags = 24;
    }
    if (os->first_pts == AV_NOPTS_VALUE) {
        os->producer_reference_time = *prft;
        if (c->target_latency_refid < 0)
            c->target_latency_refid = pkt->stream_index;
    }

    return 0;
}

static int dash_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    DASHContext *c = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    OutputStream *os = &c->streams[pkt->stream_index];
    AdaptationSet *as = &c->as[os->as_idx - 1];
    int64_t seg_end_duration, elapsed_duration;
    int ret;

    ret = update_stream_extradata(s, os, pkt, &st->avg_frame_rate);
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

    if (c->write_prft) {
        ret = dash_parse_prft(c, pkt);
        if (ret < 0)
            return ret;
    }

    if (os->first_pts == AV_NOPTS_VALUE) {
        os->first_pts = pkt->pts;
    }
    os->last_pts = pkt->pts;

    if (!c->availability_start_time[0]) {
        int64_t start_time_us = av_gettime();
        c->start_time_s = start_time_us / 1000000;
        format_date(c->availability_start_time,
                    sizeof(c->availability_start_time), start_time_us);
    }

    if (!os->packets_written)
        os->availability_time_offset = 0;

    if (!os->availability_time_offset &&
        ((os->frag_type == FRAG_TYPE_DURATION && os->seg_duration != os->frag_duration) ||
         (os->frag_type == FRAG_TYPE_EVERY_FRAME && pkt->duration))) {
        AdaptationSet *as = &c->as[os->as_idx - 1];
        int64_t frame_duration = 0;

        switch (os->frag_type) {
        case FRAG_TYPE_DURATION:
            frame_duration = os->frag_duration;
            break;
        case FRAG_TYPE_EVERY_FRAME:
            frame_duration = av_rescale_q(pkt->duration, st->time_base, AV_TIME_BASE_Q);
            break;
        }

         os->availability_time_offset = ((double) os->seg_duration -
                                         frame_duration) / AV_TIME_BASE;
        as->max_frag_duration = FFMAX(frame_duration, as->max_frag_duration);
    }

    if (c->use_template && !c->use_timeline) {
        elapsed_duration = pkt->pts - os->first_pts;
        seg_end_duration = (int64_t) os->segment_index * os->seg_duration;
    } else {
        elapsed_duration = pkt->pts - os->start_pts;
        seg_end_duration = os->seg_duration;
    }

    if (os->parser &&
        (os->frag_type == FRAG_TYPE_PFRAMES ||
         as->trick_idx >= 0)) {
        // Parse the packets only in scenarios where it's needed
        uint8_t *data;
        int size;
        av_parser_parse2(os->parser, os->parser_avctx,
                         &data, &size, pkt->data, pkt->size,
                         pkt->pts, pkt->dts, pkt->pos);

        os->coding_dependency |= os->parser->pict_type != AV_PICTURE_TYPE_I;
    }

    if (pkt->flags & AV_PKT_FLAG_KEY && os->packets_written &&
        av_compare_ts(elapsed_duration, st->time_base,
                      seg_end_duration, AV_TIME_BASE_Q) >= 0) {
        if (!c->has_video || st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            c->last_duration = av_rescale_q(pkt->pts - os->start_pts,
                    st->time_base,
                    AV_TIME_BASE_Q);
            c->total_duration = av_rescale_q(pkt->pts - os->first_pts,
                    st->time_base,
                    AV_TIME_BASE_Q);

            if ((!c->use_timeline || !c->use_template) && os->last_duration) {
                if (c->last_duration < os->last_duration*9/10 ||
                        c->last_duration > os->last_duration*11/10) {
                    av_log(s, AV_LOG_WARNING,
                            "Segment durations differ too much, enable use_timeline "
                            "and use_template, or keep a stricter keyframe interval\n");
                }
            }
        }

        if (c->write_prft && os->producer_reference_time.wallclock && !os->producer_reference_time_str[0])
            format_date(os->producer_reference_time_str,
                        sizeof(os->producer_reference_time_str),
                        os->producer_reference_time.wallclock);

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

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
        os->frag_type == FRAG_TYPE_PFRAMES &&
        os->packets_written) {
        av_assert0(os->parser);
        if ((os->parser->pict_type == AV_PICTURE_TYPE_P &&
             st->codecpar->video_delay &&
             !(os->last_flags & AV_PKT_FLAG_KEY)) ||
            pkt->flags & AV_PKT_FLAG_KEY) {
            ret = av_write_frame(os->ctx, NULL);
            if (ret < 0)
                return ret;

            if (!os->availability_time_offset) {
                int64_t frag_duration = av_rescale_q(os->total_pkt_duration, st->time_base,
                                                     AV_TIME_BASE_Q);
                os->availability_time_offset = ((double) os->seg_duration -
                                                 frag_duration) / AV_TIME_BASE;
               as->max_frag_duration = FFMAX(frag_duration, as->max_frag_duration);
            }
        }
    }

    if (pkt->flags & AV_PKT_FLAG_KEY && (os->packets_written || os->nb_segments) && !os->gop_size && as->trick_idx < 0) {
        os->gop_size = os->last_duration + av_rescale_q(os->total_pkt_duration, st->time_base, AV_TIME_BASE_Q);
        c->max_gop_size = FFMAX(c->max_gop_size, os->gop_size);
    }

    if ((ret = ff_write_chained(os->ctx, 0, pkt, s, 0)) < 0)
        return ret;

    os->packets_written++;
    os->total_pkt_size += pkt->size;
    os->total_pkt_duration += pkt->duration;
    os->last_flags = pkt->flags;

    if (!os->init_range_length)
        flush_init_segment(s, os);

    //open the output context when the first frame of a segment is ready
    if (!c->single_file && os->packets_written == 1) {
        AVDictionary *opts = NULL;
        const char *proto = avio_find_protocol_name(s->url);
        int use_rename = proto && !strcmp(proto, "file");
        if (os->segment_type == SEGMENT_TYPE_MP4)
            write_styp(os->ctx->pb);
        os->filename[0] = os->full_path[0] = os->temp_path[0] = '\0';
        ff_dash_fill_tmpl_params(os->filename, sizeof(os->filename),
                                 os->media_seg_name, pkt->stream_index,
                                 os->segment_index, os->bit_rate, os->start_pts);
        snprintf(os->full_path, sizeof(os->full_path), "%s%s", c->dirname,
                 os->filename);
        snprintf(os->temp_path, sizeof(os->temp_path),
                 use_rename ? "%s.tmp" : "%s", os->full_path);
        set_http_options(&opts, c);
        ret = dashenc_io_open(s, &os->out, os->temp_path, &opts);
        av_dict_free(&opts);
        if (ret < 0) {
            return handle_io_open_error(s, ret, os->temp_path);
        }

        // in streaming mode, the segments are available for playing
        // before fully written but the manifest is needed so that
        // clients and discover the segment filenames.
        if (c->streaming) {
            write_manifest(s, 0);
        }

        if (c->lhls) {
            char *prefetch_url = use_rename ? NULL : os->filename;
            write_hls_media_playlist(os, s, pkt->stream_index, 0, prefetch_url);
        }
    }

    //write out the data immediately in streaming mode
    if (c->streaming && os->segment_type == SEGMENT_TYPE_MP4) {
        int len = 0;
        uint8_t *buf = NULL;
        avio_flush(os->ctx->pb);
        len = avio_get_dyn_buf (os->ctx->pb, &buf);
        if (os->out) {
            avio_write(os->out, buf + os->written_len, len - os->written_len);
            avio_flush(os->out);
        }
        os->written_len = len;
    }

    return ret;
}

static int dash_write_trailer(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    int i;

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
        for (i = 0; i < s->nb_streams; ++i) {
            OutputStream *os = &c->streams[i];
            dashenc_delete_media_segments(s, os, os->nb_segments);
            dashenc_delete_segment_file(s, os->initfile);
            if (c->hls_playlist && os->segment_type == SEGMENT_TYPE_MP4) {
                char filename[1024];
                get_hls_playlist_name(filename, sizeof(filename), c->dirname, i);
                dashenc_delete_file(s, filename);
            }
        }
        dashenc_delete_file(s, s->url);

        if (c->hls_playlist && c->master_playlist_created) {
            char filename[1024];
            snprintf(filename, sizeof(filename), "%s%s", c->dirname, c->hls_master_name);
            dashenc_delete_file(s, filename);
        }
    }

    return 0;
}

static int dash_check_bitstream(AVFormatContext *s, AVStream *st,
                                const AVPacket *avpkt)
{
    DASHContext *c = s->priv_data;
    OutputStream *os = &c->streams[st->index];
    AVFormatContext *oc = os->ctx;
    if (oc->oformat->check_bitstream) {
        AVStream *const ost = oc->streams[0];
        int ret;
        ret = oc->oformat->check_bitstream(oc, ost, avpkt);
        if (ret == 1) {
            FFStream *const  sti = ffstream(st);
            FFStream *const osti = ffstream(ost);
             sti->bsfc = osti->bsfc;
            osti->bsfc = NULL;
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
    { "seg_duration", "segment duration (in seconds, fractional value can be set)", OFFSET(seg_duration), AV_OPT_TYPE_DURATION, { .i64 = 5000000 }, 0, INT_MAX, E },
    { "frag_duration", "fragment duration (in seconds, fractional value can be set)", OFFSET(frag_duration), AV_OPT_TYPE_DURATION, { .i64 = 0 }, 0, INT_MAX, E },
    { "frag_type", "set type of interval for fragments", OFFSET(frag_type), AV_OPT_TYPE_INT, {.i64 = FRAG_TYPE_NONE }, 0, FRAG_TYPE_NB - 1, E, "frag_type"},
    { "none", "one fragment per segment", 0, AV_OPT_TYPE_CONST, {.i64 = FRAG_TYPE_NONE }, 0, UINT_MAX, E, "frag_type"},
    { "every_frame", "fragment at every frame", 0, AV_OPT_TYPE_CONST, {.i64 = FRAG_TYPE_EVERY_FRAME }, 0, UINT_MAX, E, "frag_type"},
    { "duration", "fragment at specific time intervals", 0, AV_OPT_TYPE_CONST, {.i64 = FRAG_TYPE_DURATION }, 0, UINT_MAX, E, "frag_type"},
    { "pframes", "fragment at keyframes and following P-Frame reordering (Video only, experimental)", 0, AV_OPT_TYPE_CONST, {.i64 = FRAG_TYPE_PFRAMES }, 0, UINT_MAX, E, "frag_type"},
    { "remove_at_exit", "remove all segments when finished", OFFSET(remove_at_exit), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "use_template", "Use SegmentTemplate instead of SegmentList", OFFSET(use_template), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, E },
    { "use_timeline", "Use SegmentTimeline in SegmentTemplate", OFFSET(use_timeline), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, E },
    { "single_file", "Store all segments in one file, accessed using byte ranges", OFFSET(single_file), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "single_file_name", "DASH-templated name to be used for baseURL. Implies storing all segments in one file, accessed using byte ranges", OFFSET(single_file_name), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, E },
    { "init_seg_name", "DASH-templated name to used for the initialization segment", OFFSET(init_seg_name), AV_OPT_TYPE_STRING, {.str = "init-stream$RepresentationID$.$ext$"}, 0, 0, E },
    { "media_seg_name", "DASH-templated name to used for the media segments", OFFSET(media_seg_name), AV_OPT_TYPE_STRING, {.str = "chunk-stream$RepresentationID$-$Number%05d$.$ext$"}, 0, 0, E },
    { "utc_timing_url", "URL of the page that will return the UTC timestamp in ISO format", OFFSET(utc_timing_url), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "method", "set the HTTP method", OFFSET(method), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, E },
    { "http_user_agent", "override User-Agent field in HTTP header", OFFSET(user_agent), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, E},
    { "http_persistent", "Use persistent HTTP connections", OFFSET(http_persistent), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, E },
    { "hls_playlist", "Generate HLS playlist files(master.m3u8, media_%d.m3u8)", OFFSET(hls_playlist), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "hls_master_name", "HLS master playlist name", OFFSET(hls_master_name), AV_OPT_TYPE_STRING, {.str = "master.m3u8"}, 0, 0, E },
    { "streaming", "Enable/Disable streaming mode of output. Each frame will be moof fragment", OFFSET(streaming), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "timeout", "set timeout for socket I/O operations", OFFSET(timeout), AV_OPT_TYPE_DURATION, { .i64 = -1 }, -1, INT_MAX, .flags = E },
    { "index_correction", "Enable/Disable segment index correction logic", OFFSET(index_correction), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "format_options","set list of options for the container format (mp4/webm) used for dash", OFFSET(format_options), AV_OPT_TYPE_DICT, {.str = NULL},  0, 0, E},
    { "global_sidx", "Write global SIDX atom. Applicable only for single file, mp4 output, non-streaming mode", OFFSET(global_sidx), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "dash_segment_type", "set dash segment files type", OFFSET(segment_type_option), AV_OPT_TYPE_INT, {.i64 = SEGMENT_TYPE_AUTO }, 0, SEGMENT_TYPE_NB - 1, E, "segment_type"},
    { "auto", "select segment file format based on codec", 0, AV_OPT_TYPE_CONST, {.i64 = SEGMENT_TYPE_AUTO }, 0, UINT_MAX,   E, "segment_type"},
    { "mp4", "make segment file in ISOBMFF format", 0, AV_OPT_TYPE_CONST, {.i64 = SEGMENT_TYPE_MP4 }, 0, UINT_MAX,   E, "segment_type"},
    { "webm", "make segment file in WebM format", 0, AV_OPT_TYPE_CONST, {.i64 = SEGMENT_TYPE_WEBM }, 0, UINT_MAX,   E, "segment_type"},
    { "ignore_io_errors", "Ignore IO errors during open and write. Useful for long-duration runs with network output", OFFSET(ignore_io_errors), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "lhls", "Enable Low-latency HLS(Experimental). Adds #EXT-X-PREFETCH tag with current segment's URI", OFFSET(lhls), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "ldash", "Enable Low-latency dash. Constrains the value of a few elements", OFFSET(ldash), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "master_m3u8_publish_rate", "Publish master playlist every after this many segment intervals", OFFSET(master_publish_rate), AV_OPT_TYPE_INT, {.i64 = 0}, 0, UINT_MAX, E},
    { "write_prft", "Write producer reference time element", OFFSET(write_prft), AV_OPT_TYPE_BOOL, {.i64 = -1}, -1, 1, E},
    { "mpd_profile", "Set profiles. Elements and values used in the manifest may be constrained by them", OFFSET(profile), AV_OPT_TYPE_FLAGS, {.i64 = MPD_PROFILE_DASH }, 0, UINT_MAX, E, "mpd_profile"},
    { "dash", "MPEG-DASH ISO Base media file format live profile", 0, AV_OPT_TYPE_CONST, {.i64 = MPD_PROFILE_DASH }, 0, UINT_MAX, E, "mpd_profile"},
    { "dvb_dash", "DVB-DASH profile", 0, AV_OPT_TYPE_CONST, {.i64 = MPD_PROFILE_DVB }, 0, UINT_MAX, E, "mpd_profile"},
    { "http_opts", "HTTP protocol options", OFFSET(http_opts), AV_OPT_TYPE_DICT, { .str = NULL }, 0, 0, E },
    { "target_latency", "Set desired target latency for Low-latency dash", OFFSET(target_latency), AV_OPT_TYPE_DURATION, { .i64 = 0 }, 0, INT_MAX, E },
    { "min_playback_rate", "Set desired minimum playback rate", OFFSET(min_playback_rate), AV_OPT_TYPE_RATIONAL, { .dbl = 1.0 }, 0.5, 1.5, E },
    { "max_playback_rate", "Set desired maximum playback rate", OFFSET(max_playback_rate), AV_OPT_TYPE_RATIONAL, { .dbl = 1.0 }, 0.5, 1.5, E },
    { "update_period", "Set the mpd update interval", OFFSET(update_period), AV_OPT_TYPE_INT64, {.i64 = 0}, 0, INT64_MAX, E},
    { NULL },
};

static const AVClass dash_class = {
    .class_name = "dash muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVOutputFormat ff_dash_muxer = {
    .name           = "dash",
    .long_name      = NULL_IF_CONFIG_SMALL("DASH Muxer"),
    .extensions     = "mpd",
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
