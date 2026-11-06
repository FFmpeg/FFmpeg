/*
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

#include <stdio.h>

#include "libavutil/channel_layout.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/samplefmt.h"
#include "libavutil/timestamp.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

#define SRC "=sample_fmt=flt:sample_rate=48000:channel_layout=mono:time_base=1/48000"

static int send_frame(AVFilterContext *src, int64_t pts)
{
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame)
        return AVERROR(ENOMEM);
    frame->format      = AV_SAMPLE_FMT_FLT;
    frame->sample_rate = 48000;
    frame->ch_layout   = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    frame->nb_samples  = 1024;
    frame->pts         = pts;
    ret = av_frame_get_buffer(frame, 0);
    if (ret >= 0) {
        av_samples_set_silence(frame->extended_data, 0, frame->nb_samples,
                               1, frame->format);
        ret = av_buffersrc_add_frame(src, frame);
    }
    av_frame_free(&frame);
    return ret;
}

static int drain(AVFilterContext *sink, AVFrame *frame)
{
    int ret;

    while ((ret = av_buffersink_get_frame(sink, frame)) >= 0) {
        printf("pts:%s samples:%d\n", av_ts2str(frame->pts), frame->nb_samples);
        av_frame_unref(frame);
    }
    return ret == AVERROR(EAGAIN) || ret == AVERROR_EOF ? 0 : ret;
}

int main(void)
{
    AVFilterGraph *graph = avfilter_graph_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFilterContext *in0, *in1, *sink;
    int ret = AVERROR(ENOMEM);

    if (!graph || !frame)
        goto end;
    graph->nb_threads = 1;
    ret = avfilter_graph_parse_ptr(graph, "abuffer@in0" SRC "[a];abuffer@in1" SRC "[b];"
                                   "[a][b]amix=duration=longest,abuffersink@out",
                                   NULL, NULL, NULL);
    if (ret < 0 || (ret = avfilter_graph_config(graph, NULL)) < 0)
        goto end;
    in0  = avfilter_graph_get_filter(graph, "abuffer@in0");
    in1  = avfilter_graph_get_filter(graph, "abuffer@in1");
    sink = avfilter_graph_get_filter(graph, "abuffersink@out");

    /* Input 1's second frame is consumed while input 0's frame list is
     * empty, before input 0's EOF has been signalled. */
    if ((ret = send_frame(in0, 48000)) < 0 ||
        (ret = send_frame(in1, 0)) < 0 ||
        (ret = drain(sink, frame)) < 0 ||
        (ret = send_frame(in1, 1024)) < 0 ||
        (ret = drain(sink, frame)) < 0 ||
        (ret = av_buffersrc_add_frame(in0, NULL)) < 0 ||
        (ret = drain(sink, frame)) < 0 ||
        (ret = send_frame(in1, 2048)) < 0 ||
        (ret = av_buffersrc_add_frame(in1, NULL)) < 0 ||
        (ret = drain(sink, frame)) < 0)
        goto end;

end:
    av_frame_free(&frame);
    avfilter_graph_free(&graph);
    if (ret < 0) {
        fprintf(stderr, "%s\n", av_err2str(ret));
        return 1;
    }
    return 0;
}
