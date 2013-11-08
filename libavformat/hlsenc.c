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

#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"

#include "avformat.h"
#include "internal.h"

typedef struct ListEntry {
    char  name[1024];
    int   duration;
    struct ListEntry *next;
} ListEntry;

typedef struct HLSContext {
    const AVClass *class;  // Class for private options.
    unsigned number;
    int64_t sequence;
    AVOutputFormat *oformat;
    AVFormatContext *avf;
    float time;            // Set by a private option.
    int  size;             // Set by a private option.
    int  wrap;             // Set by a private option.
    int64_t recording_time;
    int has_video;
    int64_t start_pts;
    int64_t end_pts;
    int64_t duration;      // last segment duration computed so far, in seconds
    int nb_entries;
    ListEntry *list;
    ListEntry *end_list;
    char *basename;
    AVIOContext *pb;
} HLSContext;

static int hls_mux_init(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc;
    int i;

    hls->avf = oc = avformat_alloc_context();
    if (!oc)
        return AVERROR(ENOMEM);

    oc->oformat            = hls->oformat;
    oc->interrupt_callback = s->interrupt_callback;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st;
        if (!(st = avformat_new_stream(oc, NULL)))
            return AVERROR(ENOMEM);
        avcodec_copy_context(st->codec, s->streams[i]->codec);
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
    }

    return 0;
}

static int append_entry(HLSContext *hls, uint64_t duration)
{
    ListEntry *en = av_malloc(sizeof(*en));

    if (!en)
        return AVERROR(ENOMEM);

    av_strlcpy(en->name, av_basename(hls->avf->filename), sizeof(en->name));

    en->duration = duration;
    en->next     = NULL;

    if (!hls->list)
        hls->list = en;
    else
        hls->end_list->next = en;

    hls->end_list = en;

    if (hls->nb_entries >= hls->size) {
        en = hls->list;
        hls->list = en->next;
        av_free(en);
    } else
        hls->nb_entries++;

    hls->sequence++;

    return 0;
}

static void free_entries(HLSContext *hls)
{
    ListEntry *p = hls->list, *en;

    while(p) {
        en = p;
        p = p->next;
        av_free(en);
    }
}

static int hls_window(AVFormatContext *s, int last)
{
    HLSContext *hls = s->priv_data;
    ListEntry *en;
    int target_duration = 0;
    int ret = 0;

    if ((ret = avio_open2(&hls->pb, s->filename, AVIO_FLAG_WRITE,
                          &s->interrupt_callback, NULL)) < 0)
        goto fail;

    for (en = hls->list; en; en = en->next) {
        if (target_duration < en->duration)
            target_duration = en->duration;
    }

    avio_printf(hls->pb, "#EXTM3U\n");
    avio_printf(hls->pb, "#EXT-X-VERSION:3\n");
    avio_printf(hls->pb, "#EXT-X-TARGETDURATION:%d\n", target_duration);
    avio_printf(hls->pb, "#EXT-X-MEDIA-SEQUENCE:%"PRId64"\n",
                FFMAX(0, hls->sequence - hls->size));

    for (en = hls->list; en; en = en->next) {
        avio_printf(hls->pb, "#EXTINF:%d,\n", en->duration);
        avio_printf(hls->pb, "%s\n", en->name);
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

    if (av_get_frame_filename(oc->filename, sizeof(oc->filename),
                              c->basename, c->wrap ? c->number % c->wrap : c->number) < 0) {
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
    int basename_size = strlen(s->filename) + strlen(pattern) + 1;

    hls->number      = 0;

    hls->recording_time = hls->time * AV_TIME_BASE;
    hls->start_pts      = AV_NOPTS_VALUE;

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

    if ((ret = avformat_write_header(hls->avf, NULL)) < 0)
        return ret;


fail:
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
        hls->duration = av_rescale(pkt->pts - hls->end_pts,
                                   st->time_base.num, st->time_base.den);

    if (can_split && av_compare_ts(pkt->pts - hls->start_pts, st->time_base,
                                   end_pts, AV_TIME_BASE_Q) >= 0) {
        ret = append_entry(hls, hls->duration);
        if (ret)
            return ret;

        hls->end_pts = pkt->pts;
        hls->duration = 0;

        av_write_frame(oc, NULL); /* Flush any buffered data */
        avio_close(oc->pb);

        ret = hls_start(s);

        if (ret)
            return ret;

        oc = hls->avf;

        if ((ret = hls_window(s, 0)) < 0)
            return ret;
    }

    ret = ff_write_chained(oc, pkt->stream_index, pkt, s);

    return ret;
}

static int hls_write_trailer(struct AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc = hls->avf;

    av_write_trailer(oc);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    av_free(hls->basename);
    append_entry(hls, hls->duration);
    hls_window(s, 1);

    free_entries(hls);
    avio_close(hls->pb);
    return 0;
}

#define OFFSET(x) offsetof(HLSContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"start_number",  "set first number in the sequence",        OFFSET(sequence),AV_OPT_TYPE_INT64,  {.i64 = 0},     0, INT64_MAX, E},
    {"hls_time",      "set segment length in seconds",           OFFSET(time),    AV_OPT_TYPE_FLOAT,  {.dbl = 2},     0, FLT_MAX, E},
    {"hls_list_size", "set maximum number of playlist entries",  OFFSET(size),    AV_OPT_TYPE_INT,    {.i64 = 5},     0, INT_MAX, E},
    {"hls_wrap",      "set number after which the index wraps",  OFFSET(wrap),    AV_OPT_TYPE_INT,    {.i64 = 0},     0, INT_MAX, E},
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
    .audio_codec    = AV_CODEC_ID_MP2,
    .video_codec    = AV_CODEC_ID_MPEG2VIDEO,
    .flags          = AVFMT_NOFILE | AVFMT_ALLOW_FLUSH,
    .write_header   = hls_write_header,
    .write_packet   = hls_write_packet,
    .write_trailer  = hls_write_trailer,
    .priv_class     = &hls_class,
};
