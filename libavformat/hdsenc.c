/*
 * Live HDS fragmenter
 * Copyright (c) 2013 Martin Storsjo
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
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "os_support.h"

#include "libavutil/avstring.h"
#include "libavutil/base64.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"

typedef struct Fragment {
    char file[1024];
    int64_t start_time, duration;
    int n;
} Fragment;

typedef struct OutputStream {
    int bitrate;
    int first_stream;
    AVFormatContext *ctx;
    int ctx_inited;
    uint8_t iobuf[32768];
    char temp_filename[1024];
    int64_t frag_start_ts, last_ts;
    AVIOContext *out;
    int packets_written;
    int nb_fragments, fragments_size, fragment_index;
    Fragment **fragments;

    int has_audio, has_video;

    uint8_t *metadata;
    int metadata_size;

    uint8_t *extra_packets[2];
    int extra_packet_sizes[2];
    int nb_extra_packets;
} OutputStream;

typedef struct HDSContext {
    const AVClass *class;  /* Class for private options. */
    int window_size;
    int extra_window_size;
    int min_frag_duration;
    int remove_at_exit;

    OutputStream *streams;
    int nb_streams;
} HDSContext;

static int parse_header(OutputStream *os, const uint8_t *buf, int buf_size)
{
    if (buf_size < 13)
        return AVERROR_INVALIDDATA;
    if (memcmp(buf, "FLV", 3))
        return AVERROR_INVALIDDATA;
    buf      += 13;
    buf_size -= 13;
    while (buf_size >= 11 + 4) {
        int type = buf[0];
        int size = AV_RB24(&buf[1]) + 11 + 4;
        if (size > buf_size)
            return AVERROR_INVALIDDATA;
        if (type == 8 || type == 9) {
            if (os->nb_extra_packets >= FF_ARRAY_ELEMS(os->extra_packets))
                return AVERROR_INVALIDDATA;
            os->extra_packet_sizes[os->nb_extra_packets] = size;
            os->extra_packets[os->nb_extra_packets] = av_malloc(size);
            if (!os->extra_packets[os->nb_extra_packets])
                return AVERROR(ENOMEM);
            memcpy(os->extra_packets[os->nb_extra_packets], buf, size);
            os->nb_extra_packets++;
        } else if (type == 0x12) {
            if (os->metadata)
                return AVERROR_INVALIDDATA;
            os->metadata_size = size - 11 - 4;
            os->metadata      = av_malloc(os->metadata_size);
            if (!os->metadata)
                return AVERROR(ENOMEM);
            memcpy(os->metadata, buf + 11, os->metadata_size);
        }
        buf      += size;
        buf_size -= size;
    }
    if (!os->metadata)
        return AVERROR_INVALIDDATA;
    return 0;
}

static int hds_write(void *opaque, uint8_t *buf, int buf_size)
{
    OutputStream *os = opaque;
    if (os->out) {
        avio_write(os->out, buf, buf_size);
    } else {
        if (!os->metadata_size) {
            int ret;
            // Assuming the IO buffer is large enough to fit the
            // FLV header and all metadata and extradata packets
            if ((ret = parse_header(os, buf, buf_size)) < 0)
                return ret;
        }
    }
    return buf_size;
}

static void hds_free(AVFormatContext *s)
{
    HDSContext *c = s->priv_data;
    int i, j;
    if (!c->streams)
        return;
    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        if (os->out)
            ff_format_io_close(s, &os->out);
        if (os->ctx && os->ctx_inited)
            av_write_trailer(os->ctx);
        if (os->ctx)
            avio_context_free(&os->ctx->pb);
        if (os->ctx)
            avformat_free_context(os->ctx);
        av_freep(&os->metadata);
        for (j = 0; j < os->nb_extra_packets; j++)
            av_freep(&os->extra_packets[j]);
        for (j = 0; j < os->nb_fragments; j++)
            av_freep(&os->fragments[j]);
        av_freep(&os->fragments);
    }
    av_freep(&c->streams);
}

static int write_manifest(AVFormatContext *s, int final)
{
    HDSContext *c = s->priv_data;
    AVIOContext *out;
    char filename[1024], temp_filename[1024];
    int ret, i;
    double duration = 0;

    if (c->nb_streams > 0)
        duration = c->streams[0].last_ts * av_q2d(s->streams[0]->time_base);

    snprintf(filename, sizeof(filename), "%s/index.f4m", s->filename);
    snprintf(temp_filename, sizeof(temp_filename), "%s/index.f4m.tmp", s->filename);
    ret = s->io_open(s, &out, temp_filename, AVIO_FLAG_WRITE, NULL);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Unable to open %s for writing\n", temp_filename);
        return ret;
    }
    avio_printf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    avio_printf(out, "<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n");
    avio_printf(out, "\t<id>%s</id>\n", av_basename(s->filename));
    avio_printf(out, "\t<streamType>%s</streamType>\n",
                     final ? "recorded" : "live");
    avio_printf(out, "\t<deliveryType>streaming</deliveryType>\n");
    if (final)
        avio_printf(out, "\t<duration>%f</duration>\n", duration);
    for (i = 0; i < c->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        int b64_size = AV_BASE64_SIZE(os->metadata_size);
        char *base64 = av_malloc(b64_size);
        if (!base64) {
            ff_format_io_close(s, &out);
            return AVERROR(ENOMEM);
        }
        av_base64_encode(base64, b64_size, os->metadata, os->metadata_size);

        avio_printf(out, "\t<bootstrapInfo profile=\"named\" url=\"stream%d.abst\" id=\"bootstrap%d\" />\n", i, i);
        avio_printf(out, "\t<media bitrate=\"%d\" url=\"stream%d\" bootstrapInfoId=\"bootstrap%d\">\n", os->bitrate/1000, i, i);
        avio_printf(out, "\t\t<metadata>%s</metadata>\n", base64);
        avio_printf(out, "\t</media>\n");
        av_free(base64);
    }
    avio_printf(out, "</manifest>\n");
    avio_flush(out);
    ff_format_io_close(s, &out);
    return ff_rename(temp_filename, filename, s);
}

static void update_size(AVIOContext *out, int64_t pos)
{
    int64_t end = avio_tell(out);
    avio_seek(out, pos, SEEK_SET);
    avio_wb32(out, end - pos);
    avio_seek(out, end, SEEK_SET);
}

/* Note, the .abst files need to be served with the "binary/octet"
 * mime type, otherwise at least the OSMF player can easily fail
 * with "stream not found" when polling for the next fragment. */
static int write_abst(AVFormatContext *s, OutputStream *os, int final)
{
    HDSContext *c = s->priv_data;
    AVIOContext *out;
    char filename[1024], temp_filename[1024];
    int i, ret;
    int64_t asrt_pos, afrt_pos;
    int start = 0, fragments;
    int index = s->streams[os->first_stream]->id;
    int64_t cur_media_time = 0;
    if (c->window_size)
        start = FFMAX(os->nb_fragments - c->window_size, 0);
    fragments = os->nb_fragments - start;
    if (final)
        cur_media_time = os->last_ts;
    else if (os->nb_fragments)
        cur_media_time = os->fragments[os->nb_fragments - 1]->start_time;

    snprintf(filename, sizeof(filename),
             "%s/stream%d.abst", s->filename, index);
    snprintf(temp_filename, sizeof(temp_filename),
             "%s/stream%d.abst.tmp", s->filename, index);
    ret = s->io_open(s, &out, temp_filename, AVIO_FLAG_WRITE, NULL);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Unable to open %s for writing\n", temp_filename);
        return ret;
    }
    avio_wb32(out, 0); // abst size
    avio_wl32(out, MKTAG('a','b','s','t'));
    avio_wb32(out, 0); // version + flags
    avio_wb32(out, os->fragment_index - 1); // BootstrapinfoVersion
    avio_w8(out, final ? 0 : 0x20); // profile, live, update
    avio_wb32(out, 1000); // timescale
    avio_wb64(out, cur_media_time);
    avio_wb64(out, 0); // SmpteTimeCodeOffset
    avio_w8(out, 0); // MovieIdentifer (null string)
    avio_w8(out, 0); // ServerEntryCount
    avio_w8(out, 0); // QualityEntryCount
    avio_w8(out, 0); // DrmData (null string)
    avio_w8(out, 0); // MetaData (null string)
    avio_w8(out, 1); // SegmentRunTableCount
    asrt_pos = avio_tell(out);
    avio_wb32(out, 0); // asrt size
    avio_wl32(out, MKTAG('a','s','r','t'));
    avio_wb32(out, 0); // version + flags
    avio_w8(out, 0); // QualityEntryCount
    avio_wb32(out, 1); // SegmentRunEntryCount
    avio_wb32(out, 1); // FirstSegment
    avio_wb32(out, final ? (os->fragment_index - 1) : 0xffffffff); // FragmentsPerSegment
    update_size(out, asrt_pos);
    avio_w8(out, 1); // FragmentRunTableCount
    afrt_pos = avio_tell(out);
    avio_wb32(out, 0); // afrt size
    avio_wl32(out, MKTAG('a','f','r','t'));
    avio_wb32(out, 0); // version + flags
    avio_wb32(out, 1000); // timescale
    avio_w8(out, 0); // QualityEntryCount
    avio_wb32(out, fragments); // FragmentRunEntryCount
    for (i = start; i < os->nb_fragments; i++) {
        avio_wb32(out, os->fragments[i]->n);
        avio_wb64(out, os->fragments[i]->start_time);
        avio_wb32(out, os->fragments[i]->duration);
    }
    update_size(out, afrt_pos);
    update_size(out, 0);
    ff_format_io_close(s, &out);
    return ff_rename(temp_filename, filename, s);
}

static int init_file(AVFormatContext *s, OutputStream *os, int64_t start_ts)
{
    int ret, i;
    ret = s->io_open(s, &os->out, os->temp_filename, AVIO_FLAG_WRITE, NULL);
    if (ret < 0)
        return ret;
    avio_wb32(os->out, 0);
    avio_wl32(os->out, MKTAG('m','d','a','t'));
    for (i = 0; i < os->nb_extra_packets; i++) {
        AV_WB24(os->extra_packets[i] + 4, start_ts);
        os->extra_packets[i][7] = (start_ts >> 24) & 0x7f;
        avio_write(os->out, os->extra_packets[i], os->extra_packet_sizes[i]);
    }
    return 0;
}

static void close_file(AVFormatContext *s, OutputStream *os)
{
    int64_t pos = avio_tell(os->out);
    avio_seek(os->out, 0, SEEK_SET);
    avio_wb32(os->out, pos);
    avio_flush(os->out);
    ff_format_io_close(s, &os->out);
}

static int hds_write_header(AVFormatContext *s)
{
    HDSContext *c = s->priv_data;
    int ret = 0, i;
    AVOutputFormat *oformat;

    if (mkdir(s->filename, 0777) == -1 && errno != EEXIST) {
        ret = AVERROR(errno);
        av_log(s, AV_LOG_ERROR , "Failed to create directory %s\n", s->filename);
        goto fail;
    }

    oformat = av_guess_format("flv", NULL, NULL);
    if (!oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }

    c->streams = av_mallocz_array(s->nb_streams, sizeof(*c->streams));
    if (!c->streams) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (i = 0; i < s->nb_streams; i++) {
        OutputStream *os = &c->streams[c->nb_streams];
        AVFormatContext *ctx;
        AVStream *st = s->streams[i];

        if (!st->codecpar->bit_rate) {
            av_log(s, AV_LOG_ERROR, "No bit rate set for stream %d\n", i);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (os->has_video) {
                c->nb_streams++;
                os++;
            }
            os->has_video = 1;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (os->has_audio) {
                c->nb_streams++;
                os++;
            }
            os->has_audio = 1;
        } else {
            av_log(s, AV_LOG_ERROR, "Unsupported stream type in stream %d\n", i);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        os->bitrate += s->streams[i]->codecpar->bit_rate;

        if (!os->ctx) {
            os->first_stream = i;
            ctx = avformat_alloc_context();
            if (!ctx) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            os->ctx = ctx;
            ctx->oformat = oformat;
            ctx->interrupt_callback = s->interrupt_callback;
            ctx->flags = s->flags;

            ctx->pb = avio_alloc_context(os->iobuf, sizeof(os->iobuf),
                                         AVIO_FLAG_WRITE, os,
                                         NULL, hds_write, NULL);
            if (!ctx->pb) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        } else {
            ctx = os->ctx;
        }
        s->streams[i]->id = c->nb_streams;

        if (!(st = avformat_new_stream(ctx, NULL))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        avcodec_parameters_copy(st->codecpar, s->streams[i]->codecpar);
        st->codecpar->codec_tag = 0;
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        st->time_base = s->streams[i]->time_base;
    }
    if (c->streams[c->nb_streams].ctx)
        c->nb_streams++;

    for (i = 0; i < c->nb_streams; i++) {
        OutputStream *os = &c->streams[i];
        int j;
        if ((ret = avformat_write_header(os->ctx, NULL)) < 0) {
             goto fail;
        }
        os->ctx_inited = 1;
        avio_flush(os->ctx->pb);
        for (j = 0; j < os->ctx->nb_streams; j++)
            s->streams[os->first_stream + j]->time_base = os->ctx->streams[j]->time_base;

        snprintf(os->temp_filename, sizeof(os->temp_filename),
                 "%s/stream%d_temp", s->filename, i);
        ret = init_file(s, os, 0);
        if (ret < 0)
            goto fail;

        if (!os->has_video && c->min_frag_duration <= 0) {
            av_log(s, AV_LOG_WARNING,
                   "No video stream in output stream %d and no min frag duration set\n", i);
        }
        os->fragment_index = 1;
        write_abst(s, os, 0);
    }
    ret = write_manifest(s, 0);

fail:
    if (ret)
        hds_free(s);
    return ret;
}

static int add_fragment(OutputStream *os, const char *file,
                        int64_t start_time, int64_t duration)
{
    Fragment *frag;
    if (duration == 0)
        duration = 1;
    if (os->nb_fragments >= os->fragments_size) {
        int ret;
        os->fragments_size = (os->fragments_size + 1) * 2;
        if ((ret = av_reallocp_array(&os->fragments, os->fragments_size,
                                     sizeof(*os->fragments))) < 0) {
            os->fragments_size = 0;
            os->nb_fragments   = 0;
            return ret;
        }
    }
    frag = av_mallocz(sizeof(*frag));
    if (!frag)
        return AVERROR(ENOMEM);
    av_strlcpy(frag->file, file, sizeof(frag->file));
    frag->start_time = start_time;
    frag->duration   = duration;
    frag->n          = os->fragment_index;
    os->fragments[os->nb_fragments++] = frag;
    os->fragment_index++;
    return 0;
}

static int hds_flush(AVFormatContext *s, OutputStream *os, int final,
                     int64_t end_ts)
{
    HDSContext *c = s->priv_data;
    int i, ret = 0;
    char target_filename[1024];
    int index = s->streams[os->first_stream]->id;

    if (!os->packets_written)
        return 0;

    avio_flush(os->ctx->pb);
    os->packets_written = 0;
    close_file(s, os);

    snprintf(target_filename, sizeof(target_filename),
             "%s/stream%dSeg1-Frag%d", s->filename, index, os->fragment_index);
    ret = ff_rename(os->temp_filename, target_filename, s);
    if (ret < 0)
        return ret;
    add_fragment(os, target_filename, os->frag_start_ts, end_ts - os->frag_start_ts);

    if (!final) {
        ret = init_file(s, os, end_ts);
        if (ret < 0)
            return ret;
    }

    if (c->window_size || (final && c->remove_at_exit)) {
        int remove = os->nb_fragments - c->window_size - c->extra_window_size;
        if (final && c->remove_at_exit)
            remove = os->nb_fragments;
        if (remove > 0) {
            for (i = 0; i < remove; i++) {
                unlink(os->fragments[i]->file);
                av_freep(&os->fragments[i]);
            }
            os->nb_fragments -= remove;
            memmove(os->fragments, os->fragments + remove,
                    os->nb_fragments * sizeof(*os->fragments));
        }
    }

    if (ret >= 0)
        ret = write_abst(s, os, final);
    return ret;
}

static int hds_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    HDSContext *c = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    OutputStream *os = &c->streams[s->streams[pkt->stream_index]->id];
    int64_t end_dts = os->fragment_index * (int64_t)c->min_frag_duration;
    int ret;

    if (st->first_dts == AV_NOPTS_VALUE)
        st->first_dts = pkt->dts;

    if ((!os->has_video || st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) &&
        av_compare_ts(pkt->dts - st->first_dts, st->time_base,
                      end_dts, AV_TIME_BASE_Q) >= 0 &&
        pkt->flags & AV_PKT_FLAG_KEY && os->packets_written) {

        if ((ret = hds_flush(s, os, 0, pkt->dts)) < 0)
            return ret;
    }

    // Note, these fragment start timestamps, that represent a whole
    // OutputStream, assume all streams in it have the same time base.
    if (!os->packets_written)
        os->frag_start_ts = pkt->dts;
    os->last_ts = pkt->dts;

    os->packets_written++;
    return ff_write_chained(os->ctx, pkt->stream_index - os->first_stream, pkt, s, 0);
}

static int hds_write_trailer(AVFormatContext *s)
{
    HDSContext *c = s->priv_data;
    int i;

    for (i = 0; i < c->nb_streams; i++)
        hds_flush(s, &c->streams[i], 1, c->streams[i].last_ts);
    write_manifest(s, 1);

    if (c->remove_at_exit) {
        char filename[1024];
        snprintf(filename, sizeof(filename), "%s/index.f4m", s->filename);
        unlink(filename);
        for (i = 0; i < c->nb_streams; i++) {
            snprintf(filename, sizeof(filename), "%s/stream%d.abst", s->filename, i);
            unlink(filename);
        }
        rmdir(s->filename);
    }

    hds_free(s);
    return 0;
}

#define OFFSET(x) offsetof(HDSContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "window_size", "number of fragments kept in the manifest", OFFSET(window_size), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, E },
    { "extra_window_size", "number of fragments kept outside of the manifest before removing from disk", OFFSET(extra_window_size), AV_OPT_TYPE_INT, { .i64 = 5 }, 0, INT_MAX, E },
    { "min_frag_duration", "minimum fragment duration (in microseconds)", OFFSET(min_frag_duration), AV_OPT_TYPE_INT64, { .i64 = 10000000 }, 0, INT_MAX, E },
    { "remove_at_exit", "remove all fragments when finished", OFFSET(remove_at_exit), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { NULL },
};

static const AVClass hds_class = {
    .class_name = "HDS muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_hds_muxer = {
    .name           = "hds",
    .long_name      = NULL_IF_CONFIG_SMALL("HDS Muxer"),
    .priv_data_size = sizeof(HDSContext),
    .audio_codec    = AV_CODEC_ID_AAC,
    .video_codec    = AV_CODEC_ID_H264,
    .flags          = AVFMT_GLOBALHEADER | AVFMT_NOFILE,
    .write_header   = hds_write_header,
    .write_packet   = hds_write_packet,
    .write_trailer  = hds_write_trailer,
    .priv_class     = &hds_class,
};
