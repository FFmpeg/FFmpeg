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
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavformat/avio_internal.h"
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
    int *sink_stream_subcc_map;
    AVFrame *decoded_frame;
    int nb_sinks;
    AVPacket subcc_packet;
} LavfiContext;

static int *create_all_formats(int n)
{
    int i, j, *fmts, count = 0;

    for (i = 0; i < n; i++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(i);
        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            count++;
    }

    if (!(fmts = av_malloc_array(count + 1, sizeof(*fmts))))
        return NULL;
    for (j = 0, i = 0; i < n; i++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(i);
        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            fmts[j++] = i;
    }
    fmts[j] = AV_PIX_FMT_NONE;
    return fmts;
}

av_cold static int lavfi_read_close(AVFormatContext *avctx)
{
    LavfiContext *lavfi = avctx->priv_data;

    av_freep(&lavfi->sink_stream_map);
    av_freep(&lavfi->sink_eof);
    av_freep(&lavfi->stream_sink_map);
    av_freep(&lavfi->sink_stream_subcc_map);
    av_freep(&lavfi->sinks);
    avfilter_graph_free(&lavfi->graph);
    av_frame_free(&lavfi->decoded_frame);

    return 0;
}

static int create_subcc_streams(AVFormatContext *avctx)
{
    LavfiContext *lavfi = avctx->priv_data;
    AVStream *st;
    int stream_idx, sink_idx;
    AVRational *time_base;

    for (stream_idx = 0; stream_idx < lavfi->nb_sinks; stream_idx++) {
        sink_idx = lavfi->stream_sink_map[stream_idx];
        if (lavfi->sink_stream_subcc_map[sink_idx]) {
            lavfi->sink_stream_subcc_map[sink_idx] = avctx->nb_streams;
            if (!(st = avformat_new_stream(avctx, NULL)))
                return AVERROR(ENOMEM);
            st->codecpar->codec_id = AV_CODEC_ID_EIA_608;
            st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
            time_base = &avctx->streams[stream_idx]->time_base;
            st->time_base.num = time_base->num;
            st->time_base.den = time_base->den;
        } else {
            lavfi->sink_stream_subcc_map[sink_idx] = -1;
        }
    }
    return 0;
}

av_cold static int lavfi_read_header(AVFormatContext *avctx)
{
    LavfiContext *lavfi = avctx->priv_data;
    AVFilterInOut *input_links = NULL, *output_links = NULL, *inout;
    const AVFilter *buffersink, *abuffersink;
    int *pix_fmts = create_all_formats(AV_PIX_FMT_NB);
    enum AVMediaType type;
    int ret = 0, i, n;

#define FAIL(ERR) { ret = ERR; goto end; }

    if (!pix_fmts)
        FAIL(AVERROR(ENOMEM));

    buffersink = avfilter_get_by_name("buffersink");
    abuffersink = avfilter_get_by_name("abuffersink");

    if (lavfi->graph_filename && lavfi->graph_str) {
        av_log(avctx, AV_LOG_ERROR,
               "Only one of the graph or graph_file options must be specified\n");
        FAIL(AVERROR(EINVAL));
    }

    if (lavfi->graph_filename) {
        AVBPrint graph_file_pb;
        AVIOContext *avio = NULL;
        AVDictionary *options = NULL;
        if (avctx->protocol_whitelist && (ret = av_dict_set(&options, "protocol_whitelist", avctx->protocol_whitelist, 0)) < 0)
            goto end;
        ret = avio_open2(&avio, lavfi->graph_filename, AVIO_FLAG_READ, &avctx->interrupt_callback, &options);
        av_dict_free(&options);
        if (ret < 0)
            goto end;
        av_bprint_init(&graph_file_pb, 0, AV_BPRINT_SIZE_UNLIMITED);
        ret = avio_read_to_bprint(avio, &graph_file_pb, INT_MAX);
        avio_closep(&avio);
        if (ret) {
            av_bprint_finalize(&graph_file_pb, NULL);
            goto end;
        }
        if ((ret = av_bprint_finalize(&graph_file_pb, &lavfi->graph_str)))
            goto end;
    }

    if (!lavfi->graph_str)
        lavfi->graph_str = av_strdup(avctx->url);

    /* parse the graph, create a stream for each open output */
    if (!(lavfi->graph = avfilter_graph_alloc()))
        FAIL(AVERROR(ENOMEM));

    if ((ret = avfilter_graph_parse_ptr(lavfi->graph, lavfi->graph_str,
                                    &input_links, &output_links, avctx)) < 0)
        goto end;

    if (input_links) {
        av_log(avctx, AV_LOG_ERROR,
               "Open inputs in the filtergraph are not acceptable\n");
        FAIL(AVERROR(EINVAL));
    }

    /* count the outputs */
    for (n = 0, inout = output_links; inout; n++, inout = inout->next);
    lavfi->nb_sinks = n;

    if (!(lavfi->sink_stream_map = av_malloc(sizeof(int) * n)))
        FAIL(AVERROR(ENOMEM));
    if (!(lavfi->sink_eof = av_mallocz(sizeof(int) * n)))
        FAIL(AVERROR(ENOMEM));
    if (!(lavfi->stream_sink_map = av_malloc(sizeof(int) * n)))
        FAIL(AVERROR(ENOMEM));
    if (!(lavfi->sink_stream_subcc_map = av_malloc(sizeof(int) * n)))
        FAIL(AVERROR(ENOMEM));

    for (i = 0; i < n; i++)
        lavfi->stream_sink_map[i] = -1;

    /* parse the output link names - they need to be of the form out0, out1, ...
     * create a mapping between them and the streams */
    for (i = 0, inout = output_links; inout; i++, inout = inout->next) {
        int stream_idx = 0, suffix = 0, use_subcc = 0;
        sscanf(inout->name, "out%n%d%n", &suffix, &stream_idx, &suffix);
        if (!suffix) {
            av_log(avctx,  AV_LOG_ERROR,
                   "Invalid outpad name '%s'\n", inout->name);
            FAIL(AVERROR(EINVAL));
        }
        if (inout->name[suffix]) {
            if (!strcmp(inout->name + suffix, "+subcc")) {
                use_subcc = 1;
            } else {
                av_log(avctx,  AV_LOG_ERROR,
                       "Invalid outpad suffix '%s'\n", inout->name);
                FAIL(AVERROR(EINVAL));
            }
        }

        if ((unsigned)stream_idx >= n) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid index was specified in output '%s', "
                   "must be a non-negative value < %d\n",
                   inout->name, n);
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
        lavfi->sink_stream_subcc_map[i] = !!use_subcc;
    }

    /* for each open output create a corresponding stream */
    for (i = 0, inout = output_links; inout; i++, inout = inout->next) {
        AVStream *st;
        if (!(st = avformat_new_stream(avctx, NULL)))
            FAIL(AVERROR(ENOMEM));
        st->id = i;
    }

    /* create a sink for each output and connect them to the graph */
    lavfi->sinks = av_malloc_array(lavfi->nb_sinks, sizeof(AVFilterContext *));
    if (!lavfi->sinks)
        FAIL(AVERROR(ENOMEM));

    for (i = 0, inout = output_links; inout; i++, inout = inout->next) {
        AVFilterContext *sink;

        type = avfilter_pad_get_type(inout->filter_ctx->output_pads, inout->pad_idx);

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
            static const enum AVSampleFormat sample_fmts[] = {
                AV_SAMPLE_FMT_U8,  AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S32,
                AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_DBL,
            };

            ret = avfilter_graph_create_filter(&sink, abuffersink,
                                               inout->name, NULL,
                                               NULL, lavfi->graph);
            if (ret >= 0)
                ret = av_opt_set_bin(sink, "sample_fmts", (const uint8_t*)sample_fmts,
                                     sizeof(sample_fmts), AV_OPT_SEARCH_CHILDREN);
            if (ret < 0)
                goto end;
            ret = av_opt_set_int(sink, "all_channel_counts", 1,
                                 AV_OPT_SEARCH_CHILDREN);
            if (ret < 0)
                goto end;
        } else {
            av_log(avctx,  AV_LOG_ERROR,
                   "Output '%s' is not a video or audio output, not yet supported\n", inout->name);
            FAIL(AVERROR(EINVAL));
        }

        lavfi->sinks[i] = sink;
        if ((ret = avfilter_link(inout->filter_ctx, inout->pad_idx, sink, 0)) < 0)
            goto end;
    }

    /* configure the graph */
    if ((ret = avfilter_graph_config(lavfi->graph, avctx)) < 0)
        goto end;

    if (lavfi->dump_graph) {
        char *dump = avfilter_graph_dump(lavfi->graph, lavfi->dump_graph);
        if (dump != NULL) {
            fputs(dump, stderr);
            fflush(stderr);
            av_free(dump);
        } else {
            FAIL(AVERROR(ENOMEM));
        }
    }

    /* fill each stream with the information in the corresponding sink */
    for (i = 0; i < lavfi->nb_sinks; i++) {
        AVFilterContext *sink = lavfi->sinks[lavfi->stream_sink_map[i]];
        AVRational time_base = av_buffersink_get_time_base(sink);
        AVStream *st = avctx->streams[i];
        AVCodecParameters *const par = st->codecpar;
        avpriv_set_pts_info(st, 64, time_base.num, time_base.den);
        par->codec_type = av_buffersink_get_type(sink);
        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            int64_t probesize;
            par->codec_id   = AV_CODEC_ID_RAWVIDEO;
            par->format     = av_buffersink_get_format(sink);
            par->width      = av_buffersink_get_w(sink);
            par->height     = av_buffersink_get_h(sink);
            probesize       = par->width * par->height * 30 *
                              av_get_padded_bits_per_pixel(av_pix_fmt_desc_get(par->format));
            avctx->probesize = FFMAX(avctx->probesize, probesize);
            st       ->sample_aspect_ratio =
            par->sample_aspect_ratio = av_buffersink_get_sample_aspect_ratio(sink);
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            par->channels    = av_buffersink_get_channels(sink);
            par->sample_rate = av_buffersink_get_sample_rate(sink);
            par->channel_layout = av_buffersink_get_channel_layout(sink);
            par->format      = av_buffersink_get_format(sink);
            par->codec_id    = av_get_pcm_codec(par->format, -1);
            if (par->codec_id == AV_CODEC_ID_NONE)
                av_log(avctx, AV_LOG_ERROR,
                       "Could not find PCM codec for sample format %s.\n",
                       av_get_sample_fmt_name(par->format));
        }
    }

    if ((ret = create_subcc_streams(avctx)) < 0)
        goto end;

    if (!(lavfi->decoded_frame = av_frame_alloc()))
        FAIL(AVERROR(ENOMEM));

end:
    av_free(pix_fmts);
    avfilter_inout_free(&input_links);
    avfilter_inout_free(&output_links);
    return ret;
}

static int create_subcc_packet(AVFormatContext *avctx, AVFrame *frame,
                               int sink_idx)
{
    LavfiContext *lavfi = avctx->priv_data;
    AVFrameSideData *sd;
    int stream_idx, ret;

    if ((stream_idx = lavfi->sink_stream_subcc_map[sink_idx]) < 0)
        return 0;
    if (!(sd = av_frame_get_side_data(frame, AV_FRAME_DATA_A53_CC)))
        return 0;
    if ((ret = av_new_packet(&lavfi->subcc_packet, sd->size)) < 0)
        return ret;
    memcpy(lavfi->subcc_packet.data, sd->data, sd->size);
    lavfi->subcc_packet.stream_index = stream_idx;
    lavfi->subcc_packet.pts = frame->pts;
    lavfi->subcc_packet.pos = frame->pkt_pos;
    return 0;
}

static int lavfi_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    LavfiContext *lavfi = avctx->priv_data;
    double min_pts = DBL_MAX;
    int stream_idx, min_pts_sink_idx = 0;
    AVFrame *frame = lavfi->decoded_frame;
    AVDictionary *frame_metadata;
    int ret, i;
    int size = 0;
    AVStream *st;

    if (lavfi->subcc_packet.size) {
        av_packet_move_ref(pkt, &lavfi->subcc_packet);
        return pkt->size;
    }

    /* iterate through all the graph sinks. Select the sink with the
     * minimum PTS */
    for (i = 0; i < lavfi->nb_sinks; i++) {
        AVRational tb = av_buffersink_get_time_base(lavfi->sinks[i]);
        double d;
        int ret;

        if (lavfi->sink_eof[i])
            continue;

        ret = av_buffersink_get_frame_flags(lavfi->sinks[i], frame,
                                            AV_BUFFERSINK_FLAG_PEEK);
        if (ret == AVERROR_EOF) {
            ff_dlog(avctx, "EOF sink_idx:%d\n", i);
            lavfi->sink_eof[i] = 1;
            continue;
        } else if (ret < 0)
            return ret;
        d = av_rescale_q_rnd(frame->pts, tb, AV_TIME_BASE_Q, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        ff_dlog(avctx, "sink_idx:%d time:%f\n", i, d);
        av_frame_unref(frame);

        if (d < min_pts) {
            min_pts = d;
            min_pts_sink_idx = i;
        }
    }
    if (min_pts == DBL_MAX)
        return AVERROR_EOF;

    ff_dlog(avctx, "min_pts_sink_idx:%i\n", min_pts_sink_idx);

    av_buffersink_get_frame_flags(lavfi->sinks[min_pts_sink_idx], frame, 0);
    stream_idx = lavfi->sink_stream_map[min_pts_sink_idx];
    st = avctx->streams[stream_idx];

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        size = av_image_get_buffer_size(frame->format, frame->width, frame->height, 1);
        if ((ret = av_new_packet(pkt, size)) < 0)
            goto fail;

        av_image_copy_to_buffer(pkt->data, size, (const uint8_t **)frame->data, frame->linesize,
                                frame->format, frame->width, frame->height, 1);
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        size = frame->nb_samples * av_get_bytes_per_sample(frame->format) *
                                   frame->channels;
        if ((ret = av_new_packet(pkt, size)) < 0)
            goto fail;
        memcpy(pkt->data, frame->data[0], size);
    }

    frame_metadata = frame->metadata;
    if (frame_metadata) {
        size_t size;
        uint8_t *metadata = av_packet_pack_dictionary(frame_metadata, &size);

        if (!metadata) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if ((ret = av_packet_add_side_data(pkt, AV_PKT_DATA_STRINGS_METADATA,
                                           metadata, size)) < 0) {
            av_freep(&metadata);
            goto fail;
        }
    }

    if ((ret = create_subcc_packet(avctx, frame, min_pts_sink_idx)) < 0) {
        goto fail;
    }

    pkt->stream_index = stream_idx;
    pkt->pts = frame->pts;
    pkt->pos = frame->pkt_pos;
    av_frame_unref(frame);
    return size;
fail:
    av_frame_unref(frame);
    return ret;

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
    .category   = AV_CLASS_CATEGORY_DEVICE_INPUT,
};

const AVInputFormat ff_lavfi_demuxer = {
    .name           = "lavfi",
    .long_name      = NULL_IF_CONFIG_SMALL("Libavfilter virtual input device"),
    .priv_data_size = sizeof(LavfiContext),
    .read_header    = lavfi_read_header,
    .read_packet    = lavfi_read_packet,
    .read_close     = lavfi_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &lavfi_class,
    .flags_internal = FF_FMT_INIT_CLEANUP,
};
