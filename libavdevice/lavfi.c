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

#include "float.h"              /* DBL_MIN, DBL_MAX */

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/avfiltergraph.h"
#include "libavfilter/buffersink.h"
#include "avdevice.h"

typedef struct {
    AVClass *class;          ///< class for private options
    char          *graph_str;
    AVFilterGraph *graph;
    AVFilterContext **sinks;
    int *sink_stream_map;
    int *stream_sink_map;
} LavfiContext;

static int *create_all_formats(int n)
{
    int i, j, *fmts, count = 0;

    for (i = 0; i < n; i++)
        if (!(av_pix_fmt_descriptors[i].flags & PIX_FMT_HWACCEL))
            count++;

    if (!(fmts = av_malloc((count+1) * sizeof(int))))
        return NULL;
    for (j = 0, i = 0; i < n; i++) {
        if (!(av_pix_fmt_descriptors[i].flags & PIX_FMT_HWACCEL))
            fmts[j++] = i;
    }
    fmts[j] = -1;
    return fmts;
}

av_cold static int lavfi_read_close(AVFormatContext *avctx)
{
    LavfiContext *lavfi = avctx->priv_data;

    av_freep(&lavfi->sink_stream_map);
    av_freep(&lavfi->stream_sink_map);
    avfilter_graph_free(&lavfi->graph);

    return 0;
}

av_cold static int lavfi_read_header(AVFormatContext *avctx,
                                     AVFormatParameters *ap)
{
    LavfiContext *lavfi = avctx->priv_data;
    AVFilterInOut *input_links = NULL, *output_links = NULL, *inout;
    AVFilter *buffersink, *abuffersink;
    int *pix_fmts = create_all_formats(PIX_FMT_NB);
    enum AVMediaType type;
    int ret = 0, i, n;

#define FAIL(ERR) { ret = ERR; goto end; }

    avfilter_register_all();

    buffersink = avfilter_get_by_name("buffersink");
    abuffersink = avfilter_get_by_name("abuffersink");

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

        /* is a video output? */
        type = inout->filter_ctx->output_pads[inout->pad_idx].type;
        if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO) {
            av_log(avctx,  AV_LOG_ERROR,
                   "Output '%s' is not a video or audio output, not yet supported\n", inout->name);
            FAIL(AVERROR(EINVAL));
        }

        if (lavfi->stream_sink_map[stream_idx] != -1) {
            av_log(avctx,  AV_LOG_ERROR,
                   "An with stream index %d was already specified\n",
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
        AVBufferSinkParams *buffersink_params = av_buffersink_params_alloc();
        buffersink_params->pixel_fmts = pix_fmts;

#if FF_API_OLD_VSINK_API
        ret = avfilter_graph_create_filter(&sink, buffersink,
                                           inout->name, NULL,
                                           pix_fmts, lavfi->graph);
#else
        buffersink_params->pixel_fmts = pix_fmts;
        ret = avfilter_graph_create_filter(&sink, buffersink,
                                           inout->name, NULL,
                                           buffersink_params, lavfi->graph);
#endif
        av_freep(&buffersink_params);

        if (ret < 0)
            goto end;
        } else if (type == AVMEDIA_TYPE_AUDIO) {
            enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, -1 };
            const int packing_fmts[] = { AVFILTER_PACKED, -1 };
            const int64_t *chlayouts = avfilter_all_channel_layouts;
            AVABufferSinkParams *abuffersink_params = av_abuffersink_params_alloc();
            abuffersink_params->sample_fmts = sample_fmts;
            abuffersink_params->packing_fmts = packing_fmts;
            abuffersink_params->channel_layouts = chlayouts;

            ret = avfilter_graph_create_filter(&sink, abuffersink,
                                               inout->name, NULL,
                                               abuffersink_params, lavfi->graph);
            av_free(abuffersink_params);
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

    /* fill each stream with the information in the corresponding sink */
    for (i = 0; i < avctx->nb_streams; i++) {
        AVFilterLink *link = lavfi->sinks[lavfi->stream_sink_map[i]]->inputs[0];
        AVStream *st = avctx->streams[i];
        st->codec->codec_type = link->type;
        av_set_pts_info(st, 64, link->time_base.num, link->time_base.den);
        if (link->type == AVMEDIA_TYPE_VIDEO) {
            st->codec->codec_id   = CODEC_ID_RAWVIDEO;
            st->codec->pix_fmt    = link->format;
            st->codec->time_base  = link->time_base;
            st->codec->width      = link->w;
            st->codec->height     = link->h;
            st       ->sample_aspect_ratio =
            st->codec->sample_aspect_ratio = link->sample_aspect_ratio;
        } else if (link->type == AVMEDIA_TYPE_AUDIO) {
            st->codec->codec_id    = CODEC_ID_PCM_S16LE;
            st->codec->channels    = av_get_channel_layout_nb_channels(link->channel_layout);
            st->codec->sample_fmt  = link->format;
            st->codec->sample_rate = link->sample_rate;
            st->codec->time_base   = link->time_base;
            st->codec->channel_layout = link->channel_layout;
        }
    }

end:
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
    AVFilterBufferRef *ref;
    AVPicture pict;
    int ret, i, size;

    /* iterate through all the graph sinks. Select the sink with the
     * minimum PTS */
    for (i = 0; i < avctx->nb_streams; i++) {
        AVRational tb = lavfi->sinks[i]->inputs[0]->time_base;
        double d;
        int ret = av_buffersink_get_buffer_ref(lavfi->sinks[i],
                                               &ref, AV_BUFFERSINK_FLAG_PEEK);
        if (ret < 0)
            return ret;
        d = av_rescale_q(ref->pts, tb, AV_TIME_BASE_Q);
        av_dlog(avctx, "sink_idx:%d time:%f\n", i, d);

        if (d < min_pts) {
            min_pts = d;
            min_pts_sink_idx = i;
        }
    }
    av_dlog(avctx, "min_pts_sink_idx:%i\n", min_pts_sink_idx);

    av_buffersink_get_buffer_ref(lavfi->sinks[min_pts_sink_idx], &ref, 0);
    stream_idx = lavfi->sink_stream_map[min_pts_sink_idx];

    if (ref->video) {
        size = avpicture_get_size(ref->format, ref->video->w, ref->video->h);
        if ((ret = av_new_packet(pkt, size)) < 0)
            return ret;

        memcpy(pict.data,     ref->data,     4*sizeof(ref->data[0]));
        memcpy(pict.linesize, ref->linesize, 4*sizeof(ref->linesize[0]));

        avpicture_layout(&pict, ref->format, ref->video->w,
                         ref->video->h, pkt->data, size);
    } else if (ref->audio) {
        size = ref->audio->nb_samples *
            av_get_bytes_per_sample(ref->format) *
            av_get_channel_layout_nb_channels(ref->audio->channel_layout);
        if ((ret = av_new_packet(pkt, size)) < 0)
            return ret;
        memcpy(pkt->data, ref->data[0], size);
    }

    pkt->stream_index = stream_idx;
    pkt->pts = ref->pts;
    pkt->pos = ref->pos;
    pkt->size = size;
    avfilter_unref_buffer(ref);

    return size;
}

#define OFFSET(x) offsetof(LavfiContext, x)

#define DEC AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "graph", "Libavfilter graph", OFFSET(graph_str),  AV_OPT_TYPE_STRING, {.str = NULL }, 0,  0, DEC },
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
