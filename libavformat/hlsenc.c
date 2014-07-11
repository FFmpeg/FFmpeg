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
#include <stdint.h>

#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"

#include "avformat.h"
#include "internal.h"

typedef struct HLSSegment {
    char filename[1024];
    double duration; /* in seconds */
    size_t size; /* in bytes */
    int64_t pos; /* in bytes */

    struct HLSSegment *next;
} HLSSegment;

typedef enum HLSFlags {
    // Generate a single media file and use byte ranges in the playlist.
    HLS_SINGLE_FILE = (1 << 0),
    // Do not include the media filename in segment name (only the base url)
    HLS_NO_FILENAME = (1 << 1),
} HLSFlags;

typedef struct HLSContext {
    const AVClass *class;  // Class for private options.

    int64_t sequence;
    int64_t start_sequence;
    int nb_entries;
    int max_nb_segments;

    AVOutputFormat *oformat;
    AVFormatContext *ctx;

    float target_duration;
    int wrap;
    uint32_t flags; // enum HLSFlags

    int64_t recording_time;
    int has_video;
    int64_t first_pts; ///< first PTS found in the input file
    int64_t last_pts;  ///< PTS of the last packet we splitted at

    int ref_stream; ///< the index of the stream used as time reference

    double segment_duration; // last segment duration computed so far, in seconds
    size_t segment_size; // in bytes
    int64_t segment_pos; // offset in bytes

    HLSSegment *segments;
    HLSSegment *last_segment;

    char *media_filename;
    char *baseurl;

    AVIOContext *pb;
} HLSContext;

static int hls_mux_init(AVFormatContext *ctx)
{
    HLSContext *hls;
    int i, video_ref, audio_ref;

    hls = ctx->priv_data;

    if (hls->flags & HLS_NO_FILENAME) {
        if (!(hls->flags & HLS_SINGLE_FILE)) {
            av_log(ctx, AV_LOG_ERROR,
                   "HLS flag 'no_filename' cannot be used without flag "
                   "'single_file'\n");
            return AVERROR(EINVAL);
        }

        if (!hls->baseurl) {
            av_log(ctx, AV_LOG_ERROR,
                   "HLS flag 'no_filename' cannot be used without "
                   "-hls_base_url\n");
            return AVERROR(EINVAL);
        }
    }

    hls->segment_duration = 0.0;
    hls->segment_size = 0;
    hls->segment_pos = 0;

    hls->first_pts = AV_NOPTS_VALUE;
    hls->last_pts = AV_NOPTS_VALUE;

    hls->ctx = avformat_alloc_context();
    if (!hls->ctx)
        return AVERROR(ENOMEM);

    hls->ctx->oformat = hls->oformat;
    hls->ctx->interrupt_callback = ctx->interrupt_callback;
    av_dict_copy(&hls->ctx->metadata, ctx->metadata, 0);

    hls->ref_stream = -1;
    video_ref = -1;
    audio_ref = -1;

    for (i = 0; i < ctx->nb_streams; i++) {
        AVStream *stream, *input_stream;

        input_stream = ctx->streams[i];

        /* Create output stream */
        stream = avformat_new_stream(hls->ctx, NULL);
        if (!stream)
            return AVERROR(ENOMEM);

        avcodec_copy_context(stream->codec, input_stream->codec);
        stream->sample_aspect_ratio = input_stream->sample_aspect_ratio;

        /* See if we can use this stream as a time reference. We use the first
         * video stream found, or the first audio stream if there is no video
         * stream. */
        switch (input_stream->codec->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            hls->has_video++;
            if (video_ref == -1)
                video_ref = i;
            break;

        case AVMEDIA_TYPE_AUDIO:
            if (audio_ref == -1)
                audio_ref = i;
            break;

        default:
            break;
        }
    }

    if (hls->has_video > 1) {
        av_log(ctx, AV_LOG_WARNING,
               "More than a single video stream present, "
               "expect issues decoding it.\n");
    }

    if (video_ref >= 0) {
        hls->ref_stream = video_ref;
    } else if (audio_ref >= 0) {
        hls->ref_stream = audio_ref;
    } else {
        return AVERROR_STREAM_NOT_FOUND;
    }

    return 0;
}

static int hls_append_segment(HLSContext *hls)
{
    const char *basename;
    HLSSegment *segment;

    /* Create a new segment and append it to the segment list */

    segment = av_malloc(sizeof(*segment));
    if (!segment)
        return AVERROR(ENOMEM);

    basename = av_basename(hls->ctx->filename);
    av_strlcpy(segment->filename, basename, sizeof(segment->filename));

    segment->duration = hls->segment_duration;
    segment->size = hls->segment_size;
    segment->pos = hls->segment_pos;

    segment->next = NULL;

    if (!hls->segments) {
        hls->segments = segment;
    } else {
        hls->last_segment->next = segment;
    }

    hls->last_segment = segment;

    if (hls->max_nb_segments > 0 && hls->nb_entries >= hls->max_nb_segments) {
        segment = hls->segments;
        hls->segments = segment->next;
        av_free(segment);
    } else {
        hls->nb_entries++;
    }

    hls->sequence++;

    return 0;
}

static void hls_free_segments(HLSContext *hls)
{
    HLSSegment *segment;

    segment = hls->segments;

    while (segment) {
        HLSSegment *next;

        next = segment->next;
        av_free(segment);
        segment = next;
    }
}

static int hls_generate_playlist(AVFormatContext *ctx)
{
    HLSContext *hls;
    HLSSegment *segment;
    int target_duration;
    int64_t sequence;
    unsigned int version;
    int ret;

    hls = ctx->priv_data;
    target_duration = 0;
    ret = 0;

    if (hls->flags & HLS_SINGLE_FILE) {
        /* Byte ranges are part of HLS version 4. */
        version = 4;
    } else {
        /* We use floating point values for segment durations, which are part
         * of HLS version 3. */
        version = 3;
    }

    ret = avio_open2(&hls->pb, ctx->filename, AVIO_FLAG_WRITE,
                     &ctx->interrupt_callback, NULL);
    if (ret < 0)
        return ret;

    for (segment = hls->segments; segment; segment = segment->next) {
        if (segment->duration > target_duration)
            target_duration = ceil(segment->duration);
    }

    avio_printf(hls->pb, "#EXTM3U\n");
    avio_printf(hls->pb, "#EXT-X-VERSION:%u\n", version);
    avio_printf(hls->pb, "#EXT-X-TARGETDURATION:%d\n", target_duration);

    sequence = FFMAX(hls->start_sequence, hls->sequence - hls->nb_entries);
    avio_printf(hls->pb, "#EXT-X-MEDIA-SEQUENCE:%"PRId64"\n", sequence);

    for (segment = hls->segments; segment; segment = segment->next) {
        avio_printf(hls->pb, "#EXTINF:%f,\n", segment->duration);

        if (hls->flags & HLS_SINGLE_FILE) {
            avio_printf(hls->pb, "#EXT-X-BYTERANGE:%"PRIu64"@%"PRIi64"\n",
                        (uint64_t)segment->size, segment->pos);
        }

        avio_printf(hls->pb, "%s%s\n",
                    (hls->baseurl ? hls->baseurl : ""),
                    ((hls->flags & HLS_NO_FILENAME) ? "" : segment->filename));
    }

    avio_printf(hls->pb, "#EXT-X-ENDLIST\n");

    avio_closep(&hls->pb);
    return 0;
}

static int hls_create_file(AVFormatContext *ctx)
{
    HLSContext *hls;
    int file_idx;
    int ret;

    hls = ctx->priv_data;

    ret = 0;

    if (hls->wrap) {
        file_idx = hls->sequence % hls->wrap;
    } else {
        file_idx = hls->sequence;
    }

    if (hls->flags & HLS_SINGLE_FILE) {
        av_strlcpy(hls->ctx->filename, hls->media_filename,
                   sizeof(hls->ctx->filename));
    } else {
        if (av_get_frame_filename(hls->ctx->filename, sizeof(hls->ctx->filename),
                                  hls->media_filename, file_idx) < 0) {
            av_log(hls->ctx, AV_LOG_ERROR,
                   "Invalid segment filename template '%s'\n",
                   hls->media_filename);
            return AVERROR(EINVAL);
        }
    }

    ret = avio_open2(&hls->ctx->pb, hls->ctx->filename, AVIO_FLAG_WRITE,
                     &ctx->interrupt_callback, NULL);
    if (ret < 0)
        return ret;

    if (hls->ctx->oformat->priv_class && hls->ctx->priv_data)
        av_opt_set(hls->ctx->priv_data, "mpegts_flags", "resend_headers", 0);

    return 0;
}

static int hls_write_header(AVFormatContext *ctx)
{
    HLSContext *hls;
    int ret;
    char *dot;
    size_t basename_size;
    char filename[1024];

    hls = ctx->priv_data;

    hls->sequence  = hls->start_sequence;
    hls->recording_time = hls->target_duration * AV_TIME_BASE;

    /* HLS demands that media files use the MPEG-TS container */
    hls->oformat = av_guess_format("mpegts", NULL, NULL);
    if (!hls->oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }

    /* Generate the name of the media file(s) */
    av_strlcpy(filename, ctx->filename, sizeof(filename));
    dot = strrchr(filename, '.');
    if (dot)
        *dot = '\0';

    basename_size = sizeof(filename);
    hls->media_filename = av_malloc(basename_size);
    if (!hls->media_filename) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    av_strlcpy(hls->media_filename, filename, basename_size);
    if (!(hls->flags & HLS_SINGLE_FILE))
        av_strlcat(hls->media_filename, "%d", basename_size);
    av_strlcat(hls->media_filename, ".ts", basename_size);

    /* Initialize the muxer and create the first file */
    ret = hls_mux_init(ctx);
    if (ret < 0)
        goto fail;

    ret = hls_create_file(ctx);
    if (ret < 0)
        goto fail;

    ret = avformat_write_header(hls->ctx, NULL);
    if (ret < 0)
        goto fail;

    return 0;

fail:
    if (ret) {
        av_free(hls->media_filename);
        if (hls->ctx)
            avformat_free_context(hls->ctx);
    }

    return ret;
}

static int hls_write_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    HLSContext *hls;
    AVStream *stream;
    int ret, can_split;

    hls = ctx->priv_data;
    stream = ctx->streams[pkt->stream_index];

    if (hls->first_pts == AV_NOPTS_VALUE) {
        hls->first_pts = pkt->pts;
        hls->last_pts = pkt->pts;
    }

    if (pkt->stream_index == hls->ref_stream
     && pkt->pts != AV_NOPTS_VALUE) {
        AVRational time_base;
        double pts_diff;
        enum AVMediaType media;

        media = stream->codec->codec_type;

        time_base = stream->time_base;
        pts_diff = pkt->pts - hls->last_pts;

        hls->segment_duration = pts_diff * time_base.num / time_base.den;

        if (hls->segment_duration < hls->target_duration) {
            can_split = 0;
        } else if (hls->has_video) {
            /* If there is a video stream, cut before video keyframes */
            can_split = (media == AVMEDIA_TYPE_VIDEO)
                     && (pkt->flags & AV_PKT_FLAG_KEY);
        } else {
            /* In there is no video stream, cut at every audio packet */
            can_split = (media == AVMEDIA_TYPE_AUDIO);
        }
    } else {
        can_split = 0;
    }

    if (can_split) {
        /* Write a segment ending just before this packet */
        hls->segment_size = (size_t)(hls->ctx->pb->pos - hls->segment_pos);

        ret = hls_append_segment(hls);
        if (ret)
            return ret;

        hls->last_pts = pkt->pts;

        /* Get ready for the next segment starting with this packet */
        hls->segment_duration = 0.0;
        hls->segment_size = 0;

        if (hls->flags & HLS_SINGLE_FILE) {
            /* Mark the current position, i.e. the position where the next
             * segment is going to start. */
            hls->segment_pos = hls->ctx->pb->pos;

            // Each segment must start with a MPEG-TS header.
            if (hls->ctx->oformat->priv_class && hls->ctx->priv_data)
                av_opt_set(hls->ctx->priv_data, "mpegts_flags", "resend_headers", 0);
        } else {
            /* Flush any buffered data and close the current file */
            av_write_frame(hls->ctx, NULL);
            avio_close(hls->ctx->pb);

            /* Open the next file */
            ret = hls_create_file(ctx);
            if (ret)
                return ret;
        }
    }

    ret = ff_write_chained(hls->ctx, pkt->stream_index, pkt, ctx);
    if (ret < 0)
        return ret;

    return 0;
}

static int hls_write_trailer(struct AVFormatContext *ctx)
{
    HLSContext *hls;

    hls = ctx->priv_data;

    if (hls->segment_duration > 0.0) {
        hls->segment_size = (size_t)(hls->ctx->pb->pos - hls->segment_pos);
        hls_append_segment(hls);
    }

    hls_generate_playlist(ctx);

    av_write_trailer(hls->ctx);
    avio_closep(&hls->ctx->pb);

    avformat_free_context(hls->ctx);
    av_free(hls->media_filename);

    hls_free_segments(hls);
    avio_close(hls->pb);
    return 0;
}

#define OFFSET(x) offsetof(HLSContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"start_number",  "set first number in the sequence",        OFFSET(start_sequence),AV_OPT_TYPE_INT64,  {.i64 = 0},     0, INT64_MAX, E},
    {"hls_time",      "set segment length in seconds",           OFFSET(target_duration), AV_OPT_TYPE_FLOAT,  {.dbl = 2},     0, FLT_MAX, E},
    {"hls_list_size", "set maximum number of playlist entries",  OFFSET(max_nb_segments), AV_OPT_TYPE_INT,    {.i64 = 5},     0, INT_MAX, E},
    {"hls_wrap",      "set number after which the index wraps",  OFFSET(wrap),    AV_OPT_TYPE_INT,    {.i64 = 0},     0, INT_MAX, E},
    {"hls_base_url",  "url to prepend to each playlist entry",   OFFSET(baseurl), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E},

    {"hls_flags",   "set flags affecting HLS playlist and media file generation", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = 0 }, 0, UINT_MAX, E, "flags"},
    {"single_file", "generate a single media file indexed with byte ranges", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_SINGLE_FILE }, 0, UINT_MAX,   E, "flags"},
    {"no_filename", "do not use the name of the media file in segment names", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_NO_FILENAME }, 0, UINT_MAX,   E, "flags"},

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
    .audio_codec    = AV_CODEC_ID_AAC,
    .video_codec    = AV_CODEC_ID_H264,
    .flags          = AVFMT_NOFILE | AVFMT_ALLOW_FLUSH,
    .write_header   = hls_write_header,
    .write_packet   = hls_write_packet,
    .write_trailer  = hls_write_trailer,
    .priv_class     = &hls_class,
};
