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
    char *header_filename;
    int chunk_duration;
    int chunk_index;
    char *http_method;
    uint64_t duration_written;
    int64_t prev_pts;
    AVFormatContext *avf;
} WebMChunkContext;

static int webm_chunk_init(AVFormatContext *s)
{
    WebMChunkContext *wc = s->priv_data;
    ff_const59 AVOutputFormat *oformat;
    AVFormatContext *oc;
    AVStream *st, *ost = s->streams[0];
    AVDictionary *dict = NULL;
    int ret;

    // DASH Streams can only have one track per file.
    if (s->nb_streams != 1)
        return AVERROR(EINVAL);

    if (!wc->header_filename) {
        av_log(s, AV_LOG_ERROR, "No header filename provided\n");
        return AVERROR(EINVAL);
    }

    wc->prev_pts = AV_NOPTS_VALUE;

    oformat = av_guess_format("webm", s->url, "video/webm");
    if (!oformat)
        return AVERROR_MUXER_NOT_FOUND;

    ret = avformat_alloc_output_context2(&wc->avf, oformat, NULL, NULL);
    if (ret < 0)
        return ret;
    oc = wc->avf;

    ff_format_set_url(oc, wc->header_filename);
    wc->header_filename = NULL;

    oc->interrupt_callback    = s->interrupt_callback;
    oc->max_delay             = s->max_delay;
    oc->flags                 = s->flags & ~AVFMT_FLAG_FLUSH_PACKETS;
    oc->strict_std_compliance = s->strict_std_compliance;
    oc->avoid_negative_ts     = s->avoid_negative_ts;

    oc->flush_packets         = 0;

    if ((ret = av_dict_copy(&oc->metadata, s->metadata, 0)) < 0)
        return ret;

    if (!(st = avformat_new_stream(oc, NULL)))
        return AVERROR(ENOMEM);

    if ((ret = avcodec_parameters_copy(st->codecpar, ost->codecpar)) < 0 ||
        (ret = av_dict_copy(&st->metadata, ost->metadata, 0))        < 0)
        return ret;

    st->sample_aspect_ratio = ost->sample_aspect_ratio;
    st->disposition         = ost->disposition;
    avpriv_set_pts_info(st, ost->pts_wrap_bits, ost->time_base.num,
                                                ost->time_base.den);

    if ((ret = av_dict_set_int(&dict, "dash", 1, 0))   < 0 ||
        (ret = av_dict_set_int(&dict, "cluster_time_limit",
                               wc->chunk_duration, 0)) < 0 ||
        (ret = av_dict_set_int(&dict, "live", 1, 0))   < 0)
        goto fail;

    ret = avformat_init_output(oc, &dict);
fail:
    av_dict_free(&dict);
    if (ret < 0)
        return ret;

    // Copy the timing info back to the original stream
    // so that the timestamps of the packets are directly usable
    avpriv_set_pts_info(ost, st->pts_wrap_bits, st->time_base.num,
                                                st->time_base.den);

    // This ensures that the timestamps will already be properly shifted
    // when the packets arrive here, so we don't need to shift again.
    s->avoid_negative_ts  = oc->avoid_negative_ts;
    s->internal->avoid_negative_ts_use_pts =
        oc->internal->avoid_negative_ts_use_pts;
    oc->avoid_negative_ts = 0;

    return 0;
}

static int get_chunk_filename(AVFormatContext *s, char filename[MAX_FILENAME_SIZE])
{
    WebMChunkContext *wc = s->priv_data;
    if (!filename) {
        return AVERROR(EINVAL);
    }
    if (av_get_frame_filename(filename, MAX_FILENAME_SIZE,
                              s->url, wc->chunk_index - 1) < 0) {
        av_log(s, AV_LOG_ERROR, "Invalid chunk filename template '%s'\n", s->url);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int webm_chunk_write_header(AVFormatContext *s)
{
    WebMChunkContext *wc = s->priv_data;
    AVFormatContext *oc = wc->avf;
    int ret;
    AVDictionary *options = NULL;

    if (wc->http_method)
        if ((ret = av_dict_set(&options, "method", wc->http_method, 0)) < 0)
            return ret;
    ret = s->io_open(s, &oc->pb, oc->url, AVIO_FLAG_WRITE, &options);
    av_dict_free(&options);
    if (ret < 0)
        return ret;

    oc->pb->seekable = 0;
    ret = avformat_write_header(oc, NULL);
    ff_format_io_close(s, &oc->pb);
    if (ret < 0)
        return ret;
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

static int chunk_end(AVFormatContext *s, int flush)
{
    WebMChunkContext *wc = s->priv_data;
    AVFormatContext *oc = wc->avf;
    int ret;
    int buffer_size;
    uint8_t *buffer;
    AVIOContext *pb;
    char filename[MAX_FILENAME_SIZE];
    AVDictionary *options = NULL;

    if (!oc->pb)
        return 0;

    if (flush)
        // Flush the cluster in WebM muxer.
        av_write_frame(oc, NULL);
    buffer_size = avio_close_dyn_buf(oc->pb, &buffer);
    oc->pb = NULL;
    ret = get_chunk_filename(s, filename);
    if (ret < 0)
        goto fail;
    if (wc->http_method)
        if ((ret = av_dict_set(&options, "method", wc->http_method, 0)) < 0)
            goto fail;
    ret = s->io_open(s, &pb, filename, AVIO_FLAG_WRITE, &options);
    av_dict_free(&options);
    if (ret < 0)
        goto fail;
    avio_write(pb, buffer, buffer_size);
    ff_format_io_close(s, &pb);
fail:
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
        if (wc->prev_pts != AV_NOPTS_VALUE)
            wc->duration_written += av_rescale_q(pkt->pts - wc->prev_pts,
                                                 st->time_base,
                                                 (AVRational) {1, 1000});
        wc->prev_pts = pkt->pts;
    }

    // For video, a new chunk is started only on key frames. For audio, a new
    // chunk is started based on chunk_duration. Also, a new chunk is started
    // unconditionally if there is no currently open chunk.
    if (!oc->pb || (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
         (pkt->flags & AV_PKT_FLAG_KEY)) ||
        (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
         wc->duration_written >= wc->chunk_duration)) {
        wc->duration_written = 0;
        if ((ret = chunk_end(s, 1)) < 0 || (ret = chunk_start(s)) < 0) {
            return ret;
        }
    }

    // We only have one stream, so use the non-interleaving av_write_frame.
    return av_write_frame(oc, pkt);
}

static int webm_chunk_write_trailer(AVFormatContext *s)
{
    WebMChunkContext *wc = s->priv_data;
    AVFormatContext *oc = wc->avf;
    int ret;

    if (!oc->pb) {
        ret = chunk_start(s);
        if (ret < 0)
            return ret;
    }
    ret = av_write_trailer(oc);
    if (ret < 0)
        return ret;
    return chunk_end(s, 0);
}

static void webm_chunk_deinit(AVFormatContext *s)
{
    WebMChunkContext *wc = s->priv_data;

    if (!wc->avf)
        return;

    ffio_free_dyn_buf(&wc->avf->pb);
    avformat_free_context(wc->avf);
    wc->avf = NULL;
}

#define OFFSET(x) offsetof(WebMChunkContext, x)
static const AVOption options[] = {
    { "chunk_start_index",  "start index of the chunk", OFFSET(chunk_index), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "header", "filename of the header where the initialization data will be written", OFFSET(header_filename), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
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
    .init           = webm_chunk_init,
    .write_header   = webm_chunk_write_header,
    .write_packet   = webm_chunk_write_packet,
    .write_trailer  = webm_chunk_write_trailer,
    .deinit         = webm_chunk_deinit,
    .priv_class     = &webm_chunk_class,
};
#endif
