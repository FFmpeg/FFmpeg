/*
 * Generic Segmenter
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

#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "avformat.h"
#include "internal.h"
#include <strings.h>
#include <float.h>

typedef struct {
    const AVClass *class;  /**< Class for private options. */
    int number;
    AVFormatContext *avf;
    char *format;          /**< Set by a private option. */
    char *pattern;         /**< Set by a private option. */
    char *path;            /**< Set by a private option. */
    float time;            /**< Set by a private option. */
    int64_t offset_time;
    int64_t recording_time;
} SegmentContext;

#if CONFIG_SEGMENT_MUXER

static int segment_header(SegmentContext *s)
{
    AVFormatContext *oc = s->avf;
    int err = 0;

    av_strlcpy(oc->filename, s->path, sizeof(oc->filename));

    av_strlcatf(oc->filename, sizeof(oc->filename),
                s->pattern, s->number++);

    if ((err = avio_open(&oc->pb, oc->filename, AVIO_FLAG_WRITE)) < 0) {
        return err;
    }

    if (!oc->priv_data && oc->oformat->priv_data_size > 0) {
        oc->priv_data = av_mallocz(oc->oformat->priv_data_size);
        if (!oc->priv_data) {
            avio_close(oc->pb);
            return AVERROR(ENOMEM);
        }
    }

    if ((err = oc->oformat->write_header(oc)) < 0) {
        avio_close(oc->pb);
        av_freep(&oc->priv_data);
    }

    return err;
}

static int segment_trailer(AVFormatContext *oc)
{
    int ret = 0;

    if(oc->oformat->write_trailer)
        ret = oc->oformat->write_trailer(oc);

    avio_close(oc->pb);
    av_freep(&oc->priv_data);

    return ret;
}

static int seg_write_header(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc;
    int ret;

    seg->number = 0;
    seg->recording_time = seg->time*1000000;
    seg->offset_time = 0;

    if (!seg->path) {
        char *t;
        seg->path = av_strdup(s->filename);
        t = strrchr(seg->path, '.');
        if (t) *t = '\0';
    }

    oc = avformat_alloc_context();

    if (!oc) {
        return AVERROR(ENOMEM);
    }

    oc->oformat = av_guess_format(seg->format, NULL, NULL);

    if (!oc->oformat) {
        avformat_free_context(oc);
        return AVERROR_MUXER_NOT_FOUND;
    }

    seg->avf = oc;

    oc->streams = s->streams;
    oc->nb_streams = s->nb_streams;

    av_strlcpy(oc->filename, seg->path, sizeof(oc->filename));

    av_strlcatf(oc->filename, sizeof(oc->filename),
                seg->pattern, seg->number++);

    if ((ret = avio_open(&oc->pb, oc->filename, AVIO_FLAG_WRITE)) < 0) {
        avformat_free_context(oc);
        return ret;
    }

    if ((ret = avformat_write_header(oc, NULL)) < 0) {
        avio_close(oc->pb);
    }

    if (ret)
        avformat_free_context(oc);

    avio_printf(s->pb, "%s\n", oc->filename);
    avio_flush(s->pb);

    return ret;
}

static int seg_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    AVStream *st = oc->streams[pkt->stream_index];
    int ret;

    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
        av_compare_ts(pkt->pts, st->time_base,
                  seg->recording_time*seg->number,
                              (AVRational){1, 1000000}) >= 0 &&
        pkt->flags & AV_PKT_FLAG_KEY) {
        av_log(s, AV_LOG_INFO, "I'd reset at %d %"PRId64"\n",
               pkt->stream_index, pkt->pts);

        ret = segment_trailer(oc);
        if (!ret)
            ret = segment_header(seg);

        if (ret) {
            avformat_free_context(oc);
            return ret;
        }
        avio_printf(s->pb, "%s\n", oc->filename);
        avio_flush(s->pb);
    }

    ret = oc->oformat->write_packet(oc, pkt);

    return ret;
}

static int seg_write_trailer(struct AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;

    return segment_trailer(oc);
}

#endif /* CONFIG_SEGMENT_MUXER */

#define OFFSET(x) offsetof(SegmentContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "container_format", "container format used for the segments", OFFSET(format), AV_OPT_TYPE_STRING, {.str = "nut"},  0, 0, E },
    { "segment_time",     "segment lenght in seconds",              OFFSET(time),   AV_OPT_TYPE_FLOAT,  {.dbl = 2},      0, FLT_MAX, E },
    { "segment_pattern",  "pattern to use in segment files",        OFFSET(pattern),AV_OPT_TYPE_STRING, {.str = "%03d"}, 0, 0, E },
    { "segment_basename", "basename to use in segment files",       OFFSET(path   ),AV_OPT_TYPE_STRING, {.str = NULL},   0, 0, E },
    { NULL },
};

static const AVClass seg_class = {
    .class_name = "segment muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

/* input
#if CONFIG_IMAGE2_DEMUXER
AVInputFormat ff_image2_demuxer = {
    .name           = "image2",
    .long_name      = NULL_IF_CONFIG_SMALL("image2 sequence"),
    .priv_data_size = sizeof(VideoData),
    .read_probe     = read_probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &img2_class,
};
#endif
*/

/* output */
#if CONFIG_SEGMENT_MUXER
AVOutputFormat ff_segment_muxer = {
    .name           = "segment",
    .long_name      = NULL_IF_CONFIG_SMALL("segment muxer"),
    .extensions     = "m3u8",
    .priv_data_size = sizeof(SegmentContext),
    .flags          = AVFMT_GLOBALHEADER,
    .write_header   = seg_write_header,
    .write_packet   = seg_write_packet,
    .write_trailer  = seg_write_trailer,
    .priv_class     = &seg_class,
};
#endif
