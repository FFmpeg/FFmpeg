/*
 * Generic segmenter
 * Copyright (c) 2011, Luca Barbato
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <float.h>

#include "avformat.h"
#include "internal.h"

#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/parseutils.h"
#include "libavutil/mathematics.h"

typedef struct {
    const AVClass *class;  /**< Class for private options. */
    int number;
    AVOutputFormat *oformat;
    AVFormatContext *avf;
    char *format;          /**< Set by a private option. */
    char *list;            /**< Set by a private option. */
    int  list_type;        /**< Set by a private option. */
    float time;            /**< Set by a private option. */
    int  size;             /**< Set by a private option. */
    int  wrap;             /**< Set by a private option. */
    int  individual_header_trailer; /**< Set by a private option. */
    int  write_header_trailer; /**< Set by a private option. */
    int64_t offset_time;
    int64_t recording_time;
    int has_video;
    AVIOContext *pb;
} SegmentContext;

enum {
    LIST_FLAT,
    LIST_HLS
};

static int segment_mux_init(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc;
    int i;

    seg->avf = oc = avformat_alloc_context();
    if (!oc)
        return AVERROR(ENOMEM);

    oc->oformat            = seg->oformat;
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

static int segment_hls_window(AVFormatContext *s, int last)
{
    SegmentContext *seg = s->priv_data;
    int i, ret = 0;
    char buf[1024];

    if ((ret = avio_open2(&seg->pb, seg->list, AVIO_FLAG_WRITE,
                              &s->interrupt_callback, NULL)) < 0)
        goto fail;

    avio_printf(seg->pb, "#EXTM3U\n");
    avio_printf(seg->pb, "#EXT-X-VERSION:3\n");
    avio_printf(seg->pb, "#EXT-X-TARGETDURATION:%d\n", (int)seg->time);
    avio_printf(seg->pb, "#EXT-X-MEDIA-SEQUENCE:%d\n",
                FFMAX(0, seg->number - seg->size));

    for (i = FFMAX(0, seg->number - seg->size);
         i < seg->number; i++) {
        avio_printf(seg->pb, "#EXTINF:%d,\n", (int)seg->time);
        av_get_frame_filename(buf, sizeof(buf), s->filename, i);
        avio_printf(seg->pb, "%s\n", buf);
    }

    if (last)
        avio_printf(seg->pb, "#EXT-X-ENDLIST\n");
fail:
    avio_closep(&seg->pb);
    return ret;
}

static int segment_start(AVFormatContext *s, int write_header)
{
    SegmentContext *c = s->priv_data;
    AVFormatContext *oc = c->avf;
    int err = 0;

    if (write_header) {
        avformat_free_context(oc);
        c->avf = NULL;
        if ((err = segment_mux_init(s)) < 0)
            return err;
        oc = c->avf;
    }

    if (c->wrap)
        c->number %= c->wrap;

    if (av_get_frame_filename(oc->filename, sizeof(oc->filename),
                              s->filename, c->number++) < 0)
        return AVERROR(EINVAL);

    if ((err = avio_open2(&oc->pb, oc->filename, AVIO_FLAG_WRITE,
                          &s->interrupt_callback, NULL)) < 0)
        return err;

    if (oc->oformat->priv_class && oc->priv_data)
        av_opt_set(oc->priv_data, "resend_headers", "1", 0); /* mpegts specific */

    if (write_header) {
        if ((err = avformat_write_header(oc, NULL)) < 0)
            return err;
    }

    return 0;
}

static int segment_end(AVFormatContext *oc, int write_trailer)
{
    int ret = 0;

    av_write_frame(oc, NULL); /* Flush any buffered data (fragmented mp4) */
    if (write_trailer)
        av_write_trailer(oc);
    avio_close(oc->pb);

    return ret;
}

static int open_null_ctx(AVIOContext **ctx)
{
    int buf_size = 32768;
    uint8_t *buf = av_malloc(buf_size);
    if (!buf)
        return AVERROR(ENOMEM);
    *ctx = avio_alloc_context(buf, buf_size, AVIO_FLAG_WRITE, NULL, NULL, NULL, NULL);
    if (!*ctx) {
        av_free(buf);
        return AVERROR(ENOMEM);
    }
    return 0;
}

static void close_null_ctx(AVIOContext *pb)
{
    av_free(pb->buffer);
    av_free(pb);
}

static int seg_write_header(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = NULL;
    int ret, i;

    seg->number = 0;
    seg->offset_time = 0;
    seg->recording_time = seg->time * 1000000;
    if (!seg->write_header_trailer)
        seg->individual_header_trailer = 0;

    if (seg->list && seg->list_type != LIST_HLS)
        if ((ret = avio_open2(&seg->pb, seg->list, AVIO_FLAG_WRITE,
                              &s->interrupt_callback, NULL)) < 0)
            goto fail;

    for (i = 0; i < s->nb_streams; i++)
        seg->has_video +=
            (s->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO);

    if (seg->has_video > 1)
        av_log(s, AV_LOG_WARNING,
               "More than a single video stream present, "
               "expect issues decoding it.\n");

    seg->oformat = av_guess_format(seg->format, s->filename, NULL);

    if (!seg->oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }
    if (seg->oformat->flags & AVFMT_NOFILE) {
        av_log(s, AV_LOG_ERROR, "format %s not supported.\n",
               seg->oformat->name);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if ((ret = segment_mux_init(s)) < 0)
        goto fail;
    oc = seg->avf;

    if (av_get_frame_filename(oc->filename, sizeof(oc->filename),
                              s->filename, seg->number++) < 0) {
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (seg->write_header_trailer) {
        if ((ret = avio_open2(&oc->pb, oc->filename, AVIO_FLAG_WRITE,
                              &s->interrupt_callback, NULL)) < 0)
            goto fail;
    } else {
        if ((ret = open_null_ctx(&oc->pb)) < 0)
            goto fail;
    }

    if ((ret = avformat_write_header(oc, NULL)) < 0) {
        avio_close(oc->pb);
        goto fail;
    }

    if (!seg->write_header_trailer) {
        close_null_ctx(oc->pb);
        if ((ret = avio_open2(&oc->pb, oc->filename, AVIO_FLAG_WRITE,
                              &s->interrupt_callback, NULL)) < 0)
            goto fail;
    }

    if (seg->list) {
        if (seg->list_type == LIST_HLS) {
            if ((ret = segment_hls_window(s, 0)) < 0)
                goto fail;
        } else {
            avio_printf(seg->pb, "%s\n", oc->filename);
            avio_flush(seg->pb);
        }
    }

fail:
    if (ret) {
        if (seg->list)
            avio_close(seg->pb);
        if (seg->avf)
            avformat_free_context(seg->avf);
    }
    return ret;
}

static int seg_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    AVStream *st = s->streams[pkt->stream_index];
    int64_t end_pts = seg->recording_time * seg->number;
    int ret, can_split = 1;

    if (seg->has_video) {
        can_split = st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                    pkt->flags & AV_PKT_FLAG_KEY;
    }

    if (can_split && av_compare_ts(pkt->pts, st->time_base, end_pts,
                                   AV_TIME_BASE_Q) >= 0) {
        av_log(s, AV_LOG_DEBUG, "Next segment starts at %d %"PRId64"\n",
               pkt->stream_index, pkt->pts);

        ret = segment_end(oc, seg->individual_header_trailer);

        if (!ret)
            ret = segment_start(s, seg->individual_header_trailer);

        if (ret)
            goto fail;

        oc = seg->avf;

        if (seg->list) {
            if (seg->list_type == LIST_HLS) {
                if ((ret = segment_hls_window(s, 0)) < 0)
                    goto fail;
            } else {
                avio_printf(seg->pb, "%s\n", oc->filename);
                avio_flush(seg->pb);
                if (seg->size && !(seg->number % seg->size)) {
                    avio_closep(&seg->pb);
                    if ((ret = avio_open2(&seg->pb, seg->list, AVIO_FLAG_WRITE,
                                          &s->interrupt_callback, NULL)) < 0)
                        goto fail;
                }
            }
        }
    }

    ret = ff_write_chained(oc, pkt->stream_index, pkt, s);

fail:
    if (ret < 0) {
        if (seg->list)
            avio_close(seg->pb);
        avformat_free_context(oc);
    }

    return ret;
}

static int seg_write_trailer(struct AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    int ret;
    if (!seg->write_header_trailer) {
        if ((ret = segment_end(oc, 0)) < 0)
            goto fail;
        open_null_ctx(&oc->pb);
        ret = av_write_trailer(oc);
        close_null_ctx(oc->pb);
    } else {
        ret = segment_end(oc, 1);
    }

    if (ret < 0)
        goto fail;

    if (seg->list && seg->list_type == LIST_HLS) {
        if ((ret = segment_hls_window(s, 1) < 0))
            goto fail;
    }

fail:
    avio_close(seg->pb);
    avformat_free_context(oc);
    return ret;
}

#define OFFSET(x) offsetof(SegmentContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "segment_format",    "container format used for the segments",  OFFSET(format),  AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E },
    { "segment_time",      "segment length in seconds",               OFFSET(time),    AV_OPT_TYPE_FLOAT,  {.dbl = 2},     0, FLT_MAX, E },
    { "segment_list",      "output the segment list",                 OFFSET(list),    AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E },
    { "segment_list_size", "maximum number of playlist entries",      OFFSET(size),    AV_OPT_TYPE_INT,    {.i64 = 5},     0, INT_MAX, E },
    { "segment_list_type", "segment list format",                     OFFSET(list_type),    AV_OPT_TYPE_INT,    {.i64 = LIST_FLAT},     0, 2, E, "list_type" },
    {   "flat",            "plain list (default)",                    0,               AV_OPT_TYPE_CONST,  {.i64 = LIST_FLAT}, 0, 0, E, "list_type" },
    {   "hls",             "Apple HTTP Live Streaming compatible",    0,               AV_OPT_TYPE_CONST,  {.i64 = LIST_HLS},  0, 0, E, "list_type" },
    { "segment_wrap",      "number after which the index wraps",      OFFSET(wrap),    AV_OPT_TYPE_INT,    {.i64 = 0},     0, INT_MAX, E },
    { "individual_header_trailer", "write header/trailer to each segment", OFFSET(individual_header_trailer), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, E },
    { "write_header_trailer", "write a header to the first segment and a trailer to the last one", OFFSET(write_header_trailer), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, E },
    { NULL },
};

static const AVClass seg_class = {
    .class_name = "segment muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};


AVOutputFormat ff_segment_muxer = {
    .name           = "segment",
    .long_name      = NULL_IF_CONFIG_SMALL("segment"),
    .priv_data_size = sizeof(SegmentContext),
    .flags          = AVFMT_NOFILE,
    .write_header   = seg_write_header,
    .write_packet   = seg_write_packet,
    .write_trailer  = seg_write_trailer,
    .priv_class     = &seg_class,
};
