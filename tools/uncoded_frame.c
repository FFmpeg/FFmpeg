#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libavutil/avassert.h"
#include "libavdevice/avdevice.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavformat/avformat.h"

typedef struct {
    AVFormatContext *mux;
    AVStream *stream;
    AVFilterContext *sink;
    AVFilterLink *link;
} Stream;

static int create_sink(Stream *st, AVFilterGraph *graph,
                       AVFilterContext *f, int idx)
{
    enum AVMediaType type = avfilter_pad_get_type(f->output_pads, idx);
    const char *sink_name;
    int ret;

    switch (type) {
    case AVMEDIA_TYPE_VIDEO: sink_name =  "buffersink"; break;
    case AVMEDIA_TYPE_AUDIO: sink_name = "abuffersink"; break;
    default:
        av_log(NULL, AV_LOG_ERROR, "Stream type not supported\n");
        return AVERROR(EINVAL);
    }
    ret = avfilter_graph_create_filter(&st->sink,
                                       avfilter_get_by_name(sink_name),
                                       NULL, NULL, NULL, graph);
    if (ret < 0)
        return ret;
    ret = avfilter_link(f, idx, st->sink, 0);
    if (ret < 0)
        return ret;
    st->link = st->sink->inputs[0];
    return 0;
}

int main(int argc, char **argv)
{
    char *in_graph_desc, **out_dev_name;
    int nb_out_dev = 0, nb_streams = 0;
    AVFilterGraph *in_graph = NULL;
    Stream *streams = NULL, *st;
    AVFrame *frame = NULL;
    int i, j, run = 1, ret;

    //av_log_set_level(AV_LOG_DEBUG);

    if (argc < 3) {
        av_log(NULL, AV_LOG_ERROR,
               "Usage: %s filter_graph dev:out [dev2:out2...]\n\n"
               "Examples:\n"
               "%s movie=file.nut:s=v+a xv:- alsa:default\n"
               "%s movie=file.nut:s=v+a uncodedframecrc:pipe:0\n",
               argv[0], argv[0], argv[0]);
        exit(1);
    }
    in_graph_desc = argv[1];
    out_dev_name = argv + 2;
    nb_out_dev = argc - 2;

    av_register_all();
    avdevice_register_all();
    avfilter_register_all();

    /* Create input graph */
    if (!(in_graph = avfilter_graph_alloc())) {
        ret = AVERROR(ENOMEM);
        av_log(NULL, AV_LOG_ERROR, "Unable to alloc graph graph: %s\n",
               av_err2str(ret));
        goto fail;
    }
    ret = avfilter_graph_parse_ptr(in_graph, in_graph_desc, NULL, NULL, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Unable to parse graph: %s\n",
               av_err2str(ret));
        goto fail;
    }
    nb_streams = 0;
    for (i = 0; i < in_graph->nb_filters; i++) {
        AVFilterContext *f = in_graph->filters[i];
        for (j = 0; j < f->nb_inputs; j++) {
            if (!f->inputs[j]) {
                av_log(NULL, AV_LOG_ERROR, "Graph has unconnected inputs\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
        }
        for (j = 0; j < f->nb_outputs; j++)
            if (!f->outputs[j])
                nb_streams++;
    }
    if (!nb_streams) {
        av_log(NULL, AV_LOG_ERROR, "Graph has no output stream\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }
    if (nb_out_dev != 1 && nb_out_dev != nb_streams) {
        av_log(NULL, AV_LOG_ERROR,
               "Graph has %d output streams, %d devices given\n",
               nb_streams, nb_out_dev);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (!(streams = av_calloc(nb_streams, sizeof(*streams)))) {
        ret = AVERROR(ENOMEM);
        av_log(NULL, AV_LOG_ERROR, "Could not allocate streams\n");
    }
    st = streams;
    for (i = 0; i < in_graph->nb_filters; i++) {
        AVFilterContext *f = in_graph->filters[i];
        for (j = 0; j < f->nb_outputs; j++) {
            if (!f->outputs[j]) {
                if ((ret = create_sink(st++, in_graph, f, j)) < 0)
                    goto fail;
            }
        }
    }
    av_assert0(st - streams == nb_streams);
    if ((ret = avfilter_graph_config(in_graph, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to configure graph\n");
        goto fail;
    }

    /* Create output devices */
    for (i = 0; i < nb_out_dev; i++) {
        char *fmt = NULL, *dev = out_dev_name[i];
        st = &streams[i];
        if ((dev = strchr(dev, ':'))) {
            *(dev++) = 0;
            fmt = out_dev_name[i];
        }
        ret = avformat_alloc_output_context2(&st->mux, NULL, fmt, dev);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate output: %s\n",
                   av_err2str(ret));
            goto fail;
        }
        if (!(st->mux->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open2(&st->mux->pb, st->mux->filename, AVIO_FLAG_WRITE,
                             NULL, NULL);
            if (ret < 0) {
                av_log(st->mux, AV_LOG_ERROR, "Failed to init output: %s\n",
                       av_err2str(ret));
                goto fail;
            }
        }
    }
    for (; i < nb_streams; i++)
        streams[i].mux = streams[0].mux;

    /* Create output device streams */
    for (i = 0; i < nb_streams; i++) {
        st = &streams[i];
        if (!(st->stream = avformat_new_stream(st->mux, NULL))) {
            ret = AVERROR(ENOMEM);
            av_log(NULL, AV_LOG_ERROR, "Failed to create output stream\n");
            goto fail;
        }
        st->stream->codec->codec_type = st->link->type;
        st->stream->time_base = st->stream->codec->time_base =
            st->link->time_base;
        switch (st->link->type) {
        case AVMEDIA_TYPE_VIDEO:
            st->stream->codec->codec_id = AV_CODEC_ID_RAWVIDEO;
            st->stream->avg_frame_rate =
            st->stream->  r_frame_rate = av_buffersink_get_frame_rate(st->sink);
            st->stream->codec->width               = st->link->w;
            st->stream->codec->height              = st->link->h;
            st->stream->codec->sample_aspect_ratio = st->link->sample_aspect_ratio;
            st->stream->codec->pix_fmt             = st->link->format;
            break;
        case AVMEDIA_TYPE_AUDIO:
            st->stream->codec->channel_layout = st->link->channel_layout;
            st->stream->codec->channels = avfilter_link_get_channels(st->link);
            st->stream->codec->sample_rate = st->link->sample_rate;
            st->stream->codec->sample_fmt = st->link->format;
            st->stream->codec->codec_id =
                av_get_pcm_codec(st->stream->codec->sample_fmt, -1);
            break;
        default:
            av_assert0(!"reached");
        }
    }

    /* Init output devices */
    for (i = 0; i < nb_out_dev; i++) {
        st = &streams[i];
        if ((ret = avformat_write_header(st->mux, NULL)) < 0) {
            av_log(st->mux, AV_LOG_ERROR, "Failed to init output: %s\n",
                   av_err2str(ret));
            goto fail;
        }
    }

    /* Check output devices */
    for (i = 0; i < nb_streams; i++) {
        st = &streams[i];
        ret = av_write_uncoded_frame_query(st->mux, st->stream->index);
        if (ret < 0) {
            av_log(st->mux, AV_LOG_ERROR,
                   "Uncoded frames not supported on stream #%d: %s\n",
                   i, av_err2str(ret));
            goto fail;
        }
    }

    while (run) {
        ret = avfilter_graph_request_oldest(in_graph);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                run = 0;
            } else {
                av_log(NULL, AV_LOG_ERROR, "Error filtering: %s\n",
                       av_err2str(ret));
                break;
            }
        }
        for (i = 0; i < nb_streams; i++) {
            st = &streams[i];
            while (1) {
                if (!frame && !(frame = av_frame_alloc())) {
                    ret = AVERROR(ENOMEM);
                    av_log(NULL, AV_LOG_ERROR, "Could not allocate frame\n");
                    goto fail;
                }
                ret = av_buffersink_get_frame_flags(st->sink, frame,
                                                    AV_BUFFERSINK_FLAG_NO_REQUEST);
                if (ret < 0) {
                    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                        av_log(NULL, AV_LOG_WARNING, "Error in sink: %s\n",
                               av_err2str(ret));
                    break;
                }
                if (frame->pts != AV_NOPTS_VALUE)
                    frame->pts = av_rescale_q(frame->pts,
                                              st->link  ->time_base,
                                              st->stream->time_base);
                ret = av_interleaved_write_uncoded_frame(st->mux,
                                                         st->stream->index,
                                                         frame);
                frame = NULL;
                if (ret < 0) {
                    av_log(st->stream->codec, AV_LOG_ERROR,
                           "Error writing frame: %s\n", av_err2str(ret));
                    goto fail;
                }
            }
        }
    }
    ret = 0;

    for (i = 0; i < nb_out_dev; i++) {
        st = &streams[i];
        av_write_trailer(st->mux);
    }

fail:
    av_frame_free(&frame);
    avfilter_graph_free(&in_graph);
    if (streams) {
        for (i = 0; i < nb_out_dev; i++) {
            st = &streams[i];
            if (st->mux) {
                if (st->mux->pb)
                    avio_close(st->mux->pb);
                avformat_free_context(st->mux);
            }
        }
    }
    av_freep(&streams);
    return ret < 0;
}
