/*
 * Copyright (c) 2015, Vignesh Venkatasubramanian
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

/**
 * @file WebM Chunk Muxer
 * The chunk muxer enables writing WebM Live chunks where there is a header
 * chunk, followed by data chunks where each Cluster is written out as a Chunk.
 */

#include <float.h>
#include <time.h>

#include "avformat.h"
#include "avio.h"
#include "avio_internal.h"
#include "internal.h"

#include "libavutil/avassert.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/parseutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"
#include "libavutil/time_internal.h"
#include "libavutil/timestamp.h"

#define MAX_FILENAME_SIZE 1024

typedef struct WebMChunkContext {
    const AVClass *class;
    int chunk_start_index;
    char *header_filename;
    int chunk_duration;
    int chunk_index;
    char *http_method;
    uint64_t duration_written;
    int prev_pts;
    AVOutputFormat *oformat;
    AVFormatContext *avf;
} WebMChunkContext;

static int chunk_mux_init(AVFormatContext *s)
{
    WebMChunkContext *wc = s->priv_data;
    AVFormatContext *oc;
    int ret;

    ret = avformat_alloc_output_context2(&wc->avf, wc->oformat, NULL, NULL);
    if (ret < 0)
        return ret;
    oc = wc->avf;

    oc->interrupt_callback = s->interrupt_callback;
    oc->max_delay          = s->max_delay;
    av_dict_copy(&oc->metadata, s->metadata, 0);

    *(const AVClass**)oc->priv_data = oc->oformat->priv_class;
    av_opt_set_defaults(oc->priv_data);
    av_opt_set_int(oc->priv_data, "dash", 1, 0);
    av_opt_set_int(oc->priv_data, "cluster_time_limit", wc->chunk_duration, 0);
    av_opt_set_int(oc->priv_data, "live", 1, 0);

    oc->streams = s->streams;
    oc->nb_streams = s->nb_streams;

    return 0;
}

static int get_chunk_filename(AVFormatContext *s, int is_header, char *filename)
{
    WebMChunkContext *wc = s->priv_data;
    AVFormatContext *oc = wc->avf;
    if (!filename) {
        return AVERROR(EINVAL);
    }
    if (is_header) {
        if (!wc->header_filename) {
            av_log(oc, AV_LOG_ERROR, "No header filename provided\n");
            return AVERROR(EINVAL);
        }
        av_strlcpy(filename, wc->header_filename, strlen(wc->header_filename) + 1);
    } else {
        if (av_get_frame_filename(filename, MAX_FILENAME_SIZE,
                                  s->filename, wc->chunk_index - 1) < 0) {
            av_log(oc, AV_LOG_ERROR, "Invalid chunk filename template '%s'\n", s->filename);
            return AVERROR(EINVAL);
        }
    }
    return 0;
}

static int webm_chunk_write_header(AVFormatContext *s)
{
    WebMChunkContext *wc = s->priv_data;
    AVFormatContext *oc = NULL;
    int ret;
    int i;
    AVDictionary *options = NULL;

    // DASH Streams can only have either one track per file.
    if (s->nb_streams != 1) { return AVERROR_INVALIDDATA; }

    wc->chunk_index = wc->chunk_start_index;
    wc->oformat = av_guess_format("webm", s->filename, "video/webm");
    if (!wc->oformat)
        return AVERROR_MUXER_NOT_FOUND;

    ret = chunk_mux_init(s);
    if (ret < 0)
        return ret;
    oc = wc->avf;
    ret = get_chunk_filename(s, 1, oc->filename);
    if (ret < 0)
        return ret;
    if (wc->http_method)
        av_dict_set(&options, "method", wc->http_method, 0);
    ret = s->io_open(s, &oc->pb, oc->filename, AVIO_FLAG_WRITE, &options);
    av_dict_free(&options);
    if (ret < 0)
        return ret;

    oc->pb->seekable = 0;
    ret = oc->oformat->write_header(oc);
    if (ret < 0)
        return ret;
    ff_format_io_close(s, &oc->pb);
    for (i = 0; i < s->nb_streams; i++) {
        // ms precision is the de-facto standard timescale for mkv files.
        avpriv_set_pts_info(s->streams[i], 64, 1, 1000);
    }
    return 0;
}

static int chunk_start(AVFormatContext *s)
{
    WebMChunkContext *wc = s->priv_data;
    AVFormatContext *oc = wc->avf;
    int ret;

    ret = avio_open_dyn_buf(&oc->pb);
    if (ret < 0)
        return ret;
    wc->chunk_index++;
    return 0;
}

static int chunk_end(AVFormatContext *s)
{
    WebMChunkContext *wc = s->priv_data;
    AVFormatContext *oc = wc->avf;
    int ret;
    int buffer_size;
    uint8_t *buffer;
    AVIOContext *pb;
    char filename[MAX_FILENAME_SIZE];
    AVDictionary *options = NULL;

    if (wc->chunk_start_index == wc->chunk_index)
        return 0;
    // Flush the cluster in WebM muxer.
    oc->oformat->write_packet(oc, NULL);
    buffer_size = avio_close_dyn_buf(oc->pb, &buffer);
    ret = get_chunk_filename(s, 0, filename);
    if (ret < 0)
        goto fail;
    if (wc->http_method)
        av_dict_set(&options, "method", wc->http_method, 0);
    ret = s->io_open(s, &pb, filename, AVIO_FLAG_WRITE, &options);
    if (ret < 0)
        goto fail;
    avio_write(pb, buffer, buffer_size);
    ff_format_io_close(s, &pb);
    oc->pb = NULL;
fail:
    av_dict_free(&options);
    av_free(buffer);
    return (ret < 0) ? ret : 0;
}

static int webm_chunk_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    WebMChunkContext *wc = s->priv_data;
    AVFormatContext *oc = wc->avf;
    AVStream *st = s->streams[pkt->stream_index];
    int ret;

    if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        wc->duration_written += av_rescale_q(pkt->pts - wc->prev_pts,
                                             st->time_base,
                                             (AVRational) {1, 1000});
        wc->prev_pts = pkt->pts;
    }

    // For video, a new chunk is started only on key frames. For audio, a new
    // chunk is started based on chunk_duration.
    if ((st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
         (pkt->flags & AV_PKT_FLAG_KEY)) ||
        (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
         (pkt->pts == 0 || wc->duration_written >= wc->chunk_duration))) {
        wc->duration_written = 0;
        if ((ret = chunk_end(s)) < 0 || (ret = chunk_start(s)) < 0) {
            goto fail;
        }
    }

    ret = oc->oformat->write_packet(oc, pkt);
    if (ret < 0)
        goto fail;

fail:
    if (ret < 0) {
        oc->streams = NULL;
        oc->nb_streams = 0;
        avformat_free_context(oc);
    }

    return ret;
}

static int webm_chunk_write_trailer(AVFormatContext *s)
{
    WebMChunkContext *wc = s->priv_data;
    AVFormatContext *oc = wc->avf;
    oc->oformat->write_trailer(oc);
    chunk_end(s);
    oc->streams = NULL;
    oc->nb_streams = 0;
    avformat_free_context(oc);
    return 0;
}

#define OFFSET(x) offsetof(WebMChunkContext, x)
static const AVOption options[] = {
    { "chunk_start_index",  "start index of the chunk", OFFSET(chunk_start_index), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "header", "filename of the header where the initialization data will be written", OFFSET(header_filename), AV_OPT_TYPE_STRING, { 0 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "audio_chunk_duration", "duration of each chunk in milliseconds", OFFSET(chunk_duration), AV_OPT_TYPE_INT, {.i64 = 5000}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "method", "set the HTTP method", OFFSET(http_method), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

#if CONFIG_WEBM_CHUNK_MUXER
static const AVClass webm_chunk_class = {
    .class_name = "WebM Chunk Muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_webm_chunk_muxer = {
    .name           = "webm_chunk",
    .long_name      = NULL_IF_CONFIG_SMALL("WebM Chunk Muxer"),
    .mime_type      = "video/webm",
    .extensions     = "chk",
    .flags          = AVFMT_NOFILE | AVFMT_GLOBALHEADER | AVFMT_NEEDNUMBER |
                      AVFMT_TS_NONSTRICT,
    .priv_data_size = sizeof(WebMChunkContext),
    .write_header   = webm_chunk_write_header,
    .write_packet   = webm_chunk_write_packet,
    .write_trailer  = webm_chunk_write_trailer,
    .priv_class     = &webm_chunk_class,
};
#endif
