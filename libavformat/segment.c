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
    AVFormatContext *avf;
    char *format;          ///< format to use for output segment files
    char *list;            ///< filename for the segment list file
    int   list_size;       ///< number of entries for the segment list file
    AVIOContext *list_pb;  ///< list file put-byte context
    float time;            ///< segment duration
    int  wrap;             ///< number after which the index wraps
    int64_t recording_time;
    int has_video;
} SegmentContext;

static int segment_start(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    int err = 0;

    if (seg->wrap)
        seg->number %= seg->wrap;

    if (av_get_frame_filename(oc->filename, sizeof(oc->filename),
                              s->filename, seg->number++) < 0) {
        av_log(oc, AV_LOG_ERROR, "Invalid segment filename template '%s'\n", s->filename);
        return AVERROR(EINVAL);
    }

    if ((err = avio_open2(&oc->pb, oc->filename, AVIO_FLAG_WRITE,
                          &s->interrupt_callback, NULL)) < 0)
        return err;

    if (!oc->priv_data && oc->oformat->priv_data_size > 0) {
        oc->priv_data = av_mallocz(oc->oformat->priv_data_size);
        if (!oc->priv_data) {
            avio_close(oc->pb);
            return AVERROR(ENOMEM);
        }
        if (oc->oformat->priv_class) {
            *(const AVClass**)oc->priv_data = oc->oformat->priv_class;
            av_opt_set_defaults(oc->priv_data);
        }
    }

    if ((err = oc->oformat->write_header(oc)) < 0) {
        goto fail;
    }

    return 0;

fail:
    av_log(oc, AV_LOG_ERROR, "Failure occurred when starting segment '%s'\n",
           oc->filename);
    avio_close(oc->pb);
    av_freep(&oc->priv_data);

    return err;
}

static int segment_end(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    int ret = 0;

    if (oc->oformat->write_trailer)
        ret = oc->oformat->write_trailer(oc);

    if (ret < 0)
        av_log(s, AV_LOG_ERROR, "Failure occurred when ending segment '%s'\n",
               oc->filename);

    if (seg->list) {
        if (seg->list_size && !(seg->number % seg->list_size)) {
            avio_close(seg->list_pb);
            if ((ret = avio_open2(&seg->list_pb, seg->list, AVIO_FLAG_WRITE,
                                  &s->interrupt_callback, NULL)) < 0)
                goto end;
        }
        avio_printf(seg->list_pb, "%s\n", oc->filename);
        avio_flush(seg->list_pb);
    }

end:
    avio_close(oc->pb);
    if (oc->oformat->priv_class)
        av_opt_free(oc->priv_data);
    av_freep(&oc->priv_data);

    return ret;
}

static int seg_write_header(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc;
    int ret, i;

    seg->number = 0;
    seg->recording_time = seg->time * 1000000;

    oc = avformat_alloc_context();

    if (!oc)
        return AVERROR(ENOMEM);

    if (seg->list)
        if ((ret = avio_open2(&seg->list_pb, seg->list, AVIO_FLAG_WRITE,
                              &s->interrupt_callback, NULL)) < 0)
            goto fail;

    for (i = 0; i< s->nb_streams; i++)
        seg->has_video +=
            (s->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO);

    if (seg->has_video > 1)
        av_log(s, AV_LOG_WARNING,
               "More than a single video stream present, "
               "expect issues decoding it.\n");

    oc->oformat = av_guess_format(seg->format, s->filename, NULL);

    if (!oc->oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }
    if (oc->oformat->flags & AVFMT_NOFILE) {
        av_log(s, AV_LOG_ERROR, "format %s not supported.\n",
               oc->oformat->name);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    seg->avf = oc;

    oc->streams = s->streams;
    oc->nb_streams = s->nb_streams;

    if (av_get_frame_filename(oc->filename, sizeof(oc->filename),
                              s->filename, seg->number++) < 0) {
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if ((ret = avio_open2(&oc->pb, oc->filename, AVIO_FLAG_WRITE,
                          &s->interrupt_callback, NULL)) < 0)
        goto fail;

    if ((ret = avformat_write_header(oc, NULL)) < 0) {
        avio_close(oc->pb);
        goto fail;
    }

fail:
    if (ret) {
        if (oc) {
            oc->streams = NULL;
            oc->nb_streams = 0;
            avformat_free_context(oc);
        }
        if (seg->list)
            avio_close(seg->list_pb);
    }
    return ret;
}

static int seg_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    AVStream *st = oc->streams[pkt->stream_index];
    int64_t end_pts = seg->recording_time * seg->number;
    int ret;

    /* if the segment has video, start a new segment *only* with a key video frame */
    if ((st->codec->codec_type == AVMEDIA_TYPE_VIDEO || !seg->has_video) &&
        av_compare_ts(pkt->pts, st->time_base,
                      end_pts, AV_TIME_BASE_Q) >= 0 &&
        pkt->flags & AV_PKT_FLAG_KEY) {

        av_log(s, AV_LOG_DEBUG, "Next segment starts with packet stream:%d pts:%"PRId64" pts_time:%f\n",
               pkt->stream_index, pkt->pts, pkt->pts * av_q2d(st->time_base));

        if ((ret = segment_end(s)) < 0 || (ret = segment_start(s)) < 0)
            goto fail;
    }

    ret = oc->oformat->write_packet(oc, pkt);

fail:
    if (ret < 0) {
        oc->streams = NULL;
        oc->nb_streams = 0;
        if (seg->list)
            avio_close(seg->list_pb);
        avformat_free_context(oc);
    }

    return ret;
}

static int seg_write_trailer(struct AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    int ret = segment_end(s);
    if (seg->list)
        avio_close(seg->list_pb);
    oc->streams = NULL;
    oc->nb_streams = 0;
    avformat_free_context(oc);
    return ret;
}

#define OFFSET(x) offsetof(SegmentContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "segment_format",    "set container format used for the segments", OFFSET(format),  AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E },
    { "segment_time",      "set segment length in seconds",              OFFSET(time),    AV_OPT_TYPE_FLOAT,  {.dbl = 2},     0, FLT_MAX, E },
    { "segment_list",      "set the segment list filename",              OFFSET(list),    AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E },
    { "segment_list_size", "set the maximum number of playlist entries", OFFSET(list_size), AV_OPT_TYPE_INT,  {.dbl = 5},     0, INT_MAX, E },
    { "segment_wrap",      "set number after which the index wraps",     OFFSET(wrap),    AV_OPT_TYPE_INT,    {.dbl = 0},     0, INT_MAX, E },
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
    .long_name      = NULL_IF_CONFIG_SMALL("segment muxer"),
    .priv_data_size = sizeof(SegmentContext),
    .flags          = AVFMT_GLOBALHEADER | AVFMT_NOFILE,
    .write_header   = seg_write_header,
    .write_packet   = seg_write_packet,
    .write_trailer  = seg_write_trailer,
    .priv_class     = &seg_class,
};

static const AVClass sseg_class = {
    .class_name = "stream_segment muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_stream_segment_muxer = {
    .name           = "stream_segment,ssegment",
    .long_name      = NULL_IF_CONFIG_SMALL("streaming segment muxer"),
    .priv_data_size = sizeof(SegmentContext),
    .flags          = AVFMT_NOFILE,
    .write_header   = seg_write_header,
    .write_packet   = seg_write_packet,
    .write_trailer  = seg_write_trailer,
    .priv_class     = &sseg_class,
};
