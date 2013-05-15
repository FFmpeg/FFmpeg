/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * @file
 * libavfilter virtual input device
 */

/* #define DEBUG */

#include <float.h>              /* DBL_MIN, DBL_MAX */

#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/file.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/avfiltergraph.h"
#include "libavfilter/buffersink.h"
#include "libavformat/internal.h"
#include "avdevice.h"

typedef struct {
    AVClass *class;          ///< class for private options
    char          *graph_str;
    char          *graph_filename;
    char          *dump_graph;
    AVFilterGraph *graph;
    AVFilterContext **sinks;
    int *sink_stream_map;
    int *sink_eof;
    int *stream_sink_map;
    AVFrame *decoded_frame;
} LavfiContext;

static int *create_all_formats(int n)
{
    int i, j, *fmts, count = 0;

    for (i = 0; i < n; i++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(i);
        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            count++;
    }

    if (!(fmts = av_malloc((count+1) * sizeof(int))))
        return NULL;
    for (j = 0, i = 0; i < n; i++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(i);
        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            fmts[j++] = i;
    }
    fmts[j] = -1;
    return fmts;
}

av_cold static int lavfi_read_close(AVFormatContext *avctx)
{
    LavfiContext *lavfi = avctx->priv_data;

    av_freep(&lavfi->sink_stream_map);
    av_freep(&lavfi->sink_eof);
    av_freep(&lavfi->stream_sink_map);
    av_freep(&lavfi->sinks);
    avfilter_graph_free(&lavfi->graph);
    av_frame_free(&lavfi->decoded_frame);

    return 0;
}

av_cold static int lavfi_read_header(AVFormatContext *avctx)
{
    LavfiContext *lavfi = avctx->priv_data;
    AVFilterInOut *input_links = NULL, *output_links = NULL, *inout;
    AVFilter *buffersink, *abuffersink;
    int *pix_fmts = create_all_formats(AV_PIX_FMT_NB);
    enum AVMediaType type;
    int ret = 0, i, n;

#define FAIL(ERR) { ret = ERR; goto end; }

    if (!pix_fmts)
        FAIL(AVERROR(ENOMEM));

    avfilter_register_all();

    buffersink = avfilter_get_by_name("buffersink");
    abuffersink = avfilter_get_by_name("abuffersink");

    if (lavfi->graph_filename && lavfi->graph_str) {
        av_log(avctx, AV_LOG_ERROR,
               "Only one of the graph or graph_file options must be specified\n");
        FAIL(AVERROR(EINVAL));
    }

    if (lavfi->graph_filename) {
        uint8_t *file_buf, *graph_buf;
        size_t file_bufsize;
        ret = av_file_map(lavfi->graph_filename,
                          &file_buf, &file_bufsize, 0, avctx);
        if (ret < 0)
            goto end;

        /* create a 0-terminated string based on the read file */
        graph_buf = av_malloc(file_bufsize + 1);
        if (!graph_buf) {
            av_file_unmap(file_buf, file_bufsize);
            FAIL(AVERROR(ENOMEM));
        }
        memcpy(graph_buf, file_buf, file_bufsize);
        graph_buf[file_bufsize] = 0;
        av_file_unmap(file_buf, file_bufsize);
        lavfi->graph_str = graph_buf;
    }

    if (!lavfi->graph_str)
        lavfi->graph_str = av_strdup(avctx->filename);

    /* parse the graph, create a stream for each open output */
    if (!(lavfi->graph = avfilter_graph_alloc()))
        FAIL(AVERROR(ENOMEM));

    if ((ret = avfilter_graph_parse(lavfi->graph, lavfi->graph_str,
                                    &input_links, &output_links, avctx)) < 0)
        FAIL(ret);

    if (input_links) {
        av_log(avctx, AV_LOG_ERROR,
               "Open inputs in the filtergraph are not acceptable\n");
        FAIL(AVERROR(EINVAL));
    }

    /* count the outputs */
    for (n = 0, inout = output_links; inout; n++, inout = inout->next);

    if (!(lavfi->sink_stream_map = av_malloc(sizeof(int) * n)))
        FAIL(AVERROR(ENOMEM));
    if (!(lavfi->sink_eof = av_mallocz(sizeof(int) * n)))
        FAIL(AVERROR(ENOMEM));
    if (!(lavfi->stream_sink_map = av_malloc(sizeof(int) * n)))
        FAIL(AVERROR(ENOMEM));

    for (i = 0; i < n; i++)
        lavfi->stream_sink_map[i] = -1;

    /* parse the output link names - they need to be of the form out0, out1, ...
     * create a mapping between them and the streams */
    for (i = 0, inout = output_links; inout; i++, inout = inout->next) {
        int stream_idx;
        if (!strcmp(inout->name, "out"))
            stream_idx = 0;
        else if (sscanf(inout->name, "out%d\n", &stream_idx) != 1) {
            av_log(avctx,  AV_LOG_ERROR,
                   "Invalid outpad name '%s'\n", inout->name);
            FAIL(AVERROR(EINVAL));
        }

        if ((unsigned)stream_idx >= n) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid index was specified in output '%s', "
                   "must be a non-negative value < %d\n",
                   inout->name, n);
            FAIL(AVERROR(EINVAL));
        }

        /* is an audio or video output? */
        type = inout->filter_ctx->output_pads[inout->pad_idx].type;
        if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO) {
            av_log(avctx,  AV_LOG_ERROR,
                   "Output '%s' is not a video or audio output, not yet supported\n", inout->name);
            FAIL(AVERROR(EINVAL));
        }

        if (lavfi->stream_sink_map[stream_idx] != -1) {
            av_log(avctx,  AV_LOG_ERROR,
                   "An output with stream index %d was already specified\n",
                   stream_idx);
            FAIL(AVERROR(EINVAL));
        }
        lavfi->sink_stream_map[i] = stream_idx;
        lavfi->stream_sink_map[stream_idx] = i;
    }

    /* for each open output create a corresponding stream */
    for (i = 0, inout = output_links; inout; i++, inout = inout->next) {
        AVStream *st;
        if (!(st = avformat_new_stream(avctx, NULL)))
            FAIL(AVERROR(ENOMEM));
        st->id = i;
    }

    /* create a sink for each output and connect them to the graph */
    lavfi->sinks = av_malloc(sizeof(AVFilterContext *) * avctx->nb_streams);
    if (!lavfi->sinks)
        FAIL(AVERROR(ENOMEM));

    for (i = 0, inout = output_links; inout; i++, inout = inout->next) {
        AVFilterContext *sink;

        type = inout->filter_ctx->output_pads[inout->pad_idx].type;

        if (type == AVMEDIA_TYPE_VIDEO && ! buffersink ||
            type == AVMEDIA_TYPE_AUDIO && ! abuffersink) {
                av_log(avctx, AV_LOG_ERROR, "Missing required buffersink filter, aborting.\n");
                FAIL(AVERROR_FILTER_NOT_FOUND);
        }

        if (type == AVMEDIA_TYPE_VIDEO) {
            ret = avfilter_graph_create_filter(&sink, buffersink,
                                               inout->name, NULL,
                                               NULL, lavfi->graph);
            if (ret >= 0)
                ret = av_opt_set_int_list(sink, "pix_fmts", pix_fmts,  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
            if (ret < 0)
                goto end;
        } else if (type == AVMEDIA_TYPE_AUDIO) {
            enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_U8,
                                                  AV_SAMPLE_FMT_S16,
                                                  AV_SAMPLE_FMT_S32,
                                                  AV_SAMPLE_FMT_FLT,
                                                  AV_SAMPLE_FMT_DBL, -1 };

            ret = avfilter_graph_create_filter(&sink, abuffersink,
                                               inout->name, NULL,
                                               NULL, lavfi->graph);
            if (ret >= 0)
                ret = av_opt_set_int_list(sink, "sample_fmts", sample_fmts,  AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
            if (ret < 0)
                goto end;
        }

        lavfi->sinks[i] = sink;
        if ((ret = avfilter_link(inout->filter_ctx, inout->pad_idx, sink, 0)) < 0)
            FAIL(ret);
    }

    /* configure the graph */
    if ((ret = avfilter_graph_config(lavfi->graph, avctx)) < 0)
        FAIL(ret);

    if (lavfi->dump_graph) {
        char *dump = avfilter_graph_dump(lavfi->graph, lavfi->dump_graph);
        fputs(dump, stderr);
        fflush(stderr);
        av_free(dump);
    }

    /* fill each stream with the information in the corresponding sink */
    for (i = 0; i < avctx->nb_streams; i++) {
        AVFilterLink *link = lavfi->sinks[lavfi->stream_sink_map[i]]->inputs[0];
        AVStream *st = avctx->streams[i];
        st->codec->codec_type = link->type;
        avpriv_set_pts_info(st, 64, link->time_base.num, link->time_base.den);
        if (link->type == AVMEDIA_TYPE_VIDEO) {
            st->codec->codec_id   = AV_CODEC_ID_RAWVIDEO;
            st->codec->pix_fmt    = link->format;
            st->codec->time_base  = link->time_base;
            st->codec->width      = link->w;
            st->codec->height     = link->h;
            st       ->sample_aspect_ratio =
            st->codec->sample_aspect_ratio = link->sample_aspect_ratio;
            avctx->probesize = FFMAX(avctx->probesize,
                                     link->w * link->h *
                                     av_get_padded_bits_per_pixel(av_pix_fmt_desc_get(link->format)) *
                                     30);
        } else if (link->type == AVMEDIA_TYPE_AUDIO) {
            st->codec->codec_id    = av_get_pcm_codec(link->format, -1);
            st->codec->channels    = av_get_channel_layout_nb_channels(link->channel_layout);
            st->codec->sample_fmt  = link->format;
            st->codec->sample_rate = link->sample_rate;
            st->codec->time_base   = link->time_base;
            st->codec->channel_layout = link->channel_layout;
            if (st->codec->codec_id == AV_CODEC_ID_NONE)
                av_log(avctx, AV_LOG_ERROR,
                       "Could not find PCM codec for sample format %s.\n",
                       av_get_sample_fmt_name(link->format));
        }
    }

    if (!(lavfi->decoded_frame = av_frame_alloc()))
        FAIL(AVERROR(ENOMEM));

end:
    av_free(pix_fmts);
    avfilter_inout_free(&input_links);
    avfilter_inout_free(&output_links);
    if (ret < 0)
        lavfi_read_close(avctx);
    return ret;
}

static int lavfi_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    LavfiContext *lavfi = avctx->priv_data;
    double min_pts = DBL_MAX;
    int stream_idx, min_pts_sink_idx = 0;
    AVFrame *frame = lavfi->decoded_frame;
    AVPicture pict;
    AVDictionary *frame_metadata;
    int ret, i;
    int size = 0;

    /* iterate through all the graph sinks. Select the sink with the
     * minimum PTS */
    for (i = 0; i < avctx->nb_streams; i++) {
        AVRational tb = lavfi->sinks[i]->inputs[0]->time_base;
        double d;
        int ret;

        if (lavfi->sink_eof[i])
            continue;

        ret = av_buffersink_get_frame_flags(lavfi->sinks[i], frame,
                                            AV_BUFFERSINK_FLAG_PEEK);
        if (ret == AVERROR_EOF) {
            av_dlog(avctx, "EOF sink_idx:%d\n", i);
            lavfi->sink_eof[i] = 1;
            continue;
        } else if (ret < 0)
            return ret;
        d = av_rescale_q(frame->pts, tb, AV_TIME_BASE_Q);
        av_dlog(avctx, "sink_idx:%d time:%f\n", i, d);
        av_frame_unref(frame);

        if (d < min_pts) {
            min_pts = d;
            min_pts_sink_idx = i;
        }
    }
    if (min_pts == DBL_MAX)
        return AVERROR_EOF;

    av_dlog(avctx, "min_pts_sink_idx:%i\n", min_pts_sink_idx);

    av_buffersink_get_frame_flags(lavfi->sinks[min_pts_sink_idx], frame, 0);
    stream_idx = lavfi->sink_stream_map[min_pts_sink_idx];

    if (frame->width /* FIXME best way of testing a video */) {
        size = avpicture_get_size(frame->format, frame->width, frame->height);
        if ((ret = av_new_packet(pkt, size)) < 0)
            return ret;

        memcpy(pict.data,     frame->data,     4*sizeof(frame->data[0]));
        memcpy(pict.linesize, frame->linesize, 4*sizeof(frame->linesize[0]));

        avpicture_layout(&pict, frame->format, frame->width, frame->height,
                         pkt->data, size);
    } else if (av_frame_get_channels(frame) /* FIXME test audio */) {
        size = frame->nb_samples * av_get_bytes_per_sample(frame->format) *
                                   av_frame_get_channels(frame);
        if ((ret = av_new_packet(pkt, size)) < 0)
            return ret;
        memcpy(pkt->data, frame->data[0], size);
    }

    frame_metadata = av_frame_get_metadata(frame);
    if (frame_metadata) {
        uint8_t *metadata;
        AVDictionaryEntry *e = NULL;
        AVBPrint meta_buf;

        av_bprint_init(&meta_buf, 0, AV_BPRINT_SIZE_UNLIMITED);
        while ((e = av_dict_get(frame_metadata, "", e, AV_DICT_IGNORE_SUFFIX))) {
            av_bprintf(&meta_buf, "%s", e->key);
            av_bprint_chars(&meta_buf, '\0', 1);
            av_bprintf(&meta_buf, "%s", e->value);
            av_bprint_chars(&meta_buf, '\0', 1);
        }
        if (!av_bprint_is_complete(&meta_buf) ||
            !(metadata = av_packet_new_side_data(pkt, AV_PKT_DATA_STRINGS_METADATA,
                                                 meta_buf.len))) {
            av_bprint_finalize(&meta_buf, NULL);
            return AVERROR(ENOMEM);
        }
        memcpy(metadata, meta_buf.str, meta_buf.len);
        av_bprint_finalize(&meta_buf, NULL);
    }

    pkt->stream_index = stream_idx;
    pkt->pts = frame->pts;
    pkt->pos = av_frame_get_pkt_pos(frame);
    pkt->size = size;
    av_frame_unref(frame);
    return size;
}

#define OFFSET(x) offsetof(LavfiContext, x)

#define DEC AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "graph",     "set libavfilter graph", OFFSET(graph_str),  AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { "graph_file","set libavfilter graph filename", OFFSET(graph_filename), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC},
    { "dumpgraph", "dump graph to stderr",  OFFSET(dump_graph), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { NULL },
};

static const AVClass lavfi_class = {
    .class_name = "lavfi indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_lavfi_demuxer = {
    .name           = "lavfi",
    .long_name      = NULL_IF_CONFIG_SMALL("Libavfilter virtual input device"),
    .priv_data_size = sizeof(LavfiContext),
    .read_header    = lavfi_read_header,
    .read_packet    = lavfi_read_packet,
    .read_close     = lavfi_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &lavfi_class,
};
