/*
 * Copyright (c) 2010 Nicolas George
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2012 Clément Bœsch
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * API example for audio decoding and filtering
 * @example doc/examples/filtering_audio.c
 */

#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

const char *filter_descr = "aresample=8000,aconvert=s16:mono";
const char *player       = "ffplay -f s16le -ar 8000 -ac 1 -";

static AVFormatContext *fmt_ctx;
static AVCodecContext *dec_ctx;
AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;
static int audio_stream_index = -1;

static int open_input_file(const char *filename)
{
    int ret;
    AVCodec *dec;

    if ((ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    /* select the audio stream */
    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find a audio stream in the input file\n");
        return ret;
    }
    audio_stream_index = ret;
    dec_ctx = fmt_ctx->streams[audio_stream_index]->codec;

    /* init the audio decoder */
    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open audio decoder\n");
        return ret;
    }

    return 0;
}

static int init_filters(const char *filters_descr)
{
    char args[512];
    int ret;
    AVFilter *abuffersrc  = avfilter_get_by_name("abuffer");
    AVFilter *abuffersink = avfilter_get_by_name("ffabuffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, -1 };
    AVABufferSinkParams *abuffersink_params;
    const AVFilterLink *outlink;
    AVRational time_base = fmt_ctx->streams[audio_stream_index]->time_base;

    filter_graph = avfilter_graph_alloc();

    /* buffer audio source: the decoded frames from the decoder will be inserted here. */
    if (!dec_ctx->channel_layout)
        dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);
    snprintf(args, sizeof(args),
            "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
             time_base.num, time_base.den, dec_ctx->sample_rate,
             av_get_sample_fmt_name(dec_ctx->sample_fmt), dec_ctx->channel_layout);
    ret = avfilter_graph_create_filter(&buffersrc_ctx, abuffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
        return ret;
    }

    /* buffer audio sink: to terminate the filter chain. */
    abuffersink_params = av_abuffersink_params_alloc();
    abuffersink_params->sample_fmts     = sample_fmts;
    ret = avfilter_graph_create_filter(&buffersink_ctx, abuffersink, "out",
                                       NULL, abuffersink_params, filter_graph);
    av_free(abuffersink_params);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
        return ret;
    }

    /* Endpoints for the filter graph. */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse(filter_graph, filters_descr,
                                    &inputs, &outputs, NULL)) < 0)
        return ret;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        return ret;

    /* Print summary of the sink buffer
     * Note: args buffer is reused to store channel layout string */
    outlink = buffersink_ctx->inputs[0];
    av_get_channel_layout_string(args, sizeof(args), -1, outlink->channel_layout);
    av_log(NULL, AV_LOG_INFO, "Output: srate:%dHz fmt:%s chlayout:%s\n",
           (int)outlink->sample_rate,
           (char *)av_x_if_null(av_get_sample_fmt_name(outlink->format), "?"),
           args);

    return 0;
}

static void print_samplesref(AVFilterBufferRef *samplesref)
{
    const AVFilterBufferRefAudioProps *props = samplesref->audio;
    const int n = props->nb_samples * av_get_channel_layout_nb_channels(props->channel_layout);
    const uint16_t *p     = (uint16_t*)samplesref->data[0];
    const uint16_t *p_end = p + n;

    while (p < p_end) {
        fputc(*p    & 0xff, stdout);
        fputc(*p>>8 & 0xff, stdout);
        p++;
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int ret;
    AVPacket packet;
    AVFrame frame;
    int got_frame;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s file | %s\n", argv[0], player);
        exit(1);
    }

    avcodec_register_all();
    av_register_all();
    avfilter_register_all();

    if ((ret = open_input_file(argv[1])) < 0)
        goto end;
    if ((ret = init_filters(filter_descr)) < 0)
        goto end;

    /* read all packets */
    while (1) {
        AVFilterBufferRef *samplesref;
        if ((ret = av_read_frame(fmt_ctx, &packet)) < 0)
            break;

        if (packet.stream_index == audio_stream_index) {
            avcodec_get_frame_defaults(&frame);
            got_frame = 0;
            ret = avcodec_decode_audio4(dec_ctx, &frame, &got_frame, &packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error decoding audio\n");
                continue;
            }

            if (got_frame) {
                /* push the audio data from decoded frame into the filtergraph */
                if (av_buffersrc_add_frame(buffersrc_ctx, &frame, 0) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph\n");
                    break;
                }

                /* pull filtered audio from the filtergraph */
                while (1) {
                    ret = av_buffersink_get_buffer_ref(buffersink_ctx, &samplesref, 0);
                    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if(ret < 0)
                        goto end;
                    if (samplesref) {
                        print_samplesref(samplesref);
                        avfilter_unref_bufferp(&samplesref);
                    }
                }
            }
        }
        av_free_packet(&packet);
    }
end:
    avfilter_graph_free(&filter_graph);
    if (dec_ctx)
        avcodec_close(dec_ctx);
    avformat_close_input(&fmt_ctx);

    if (ret < 0 && ret != AVERROR_EOF) {
        char buf[1024];
        av_strerror(ret, buf, sizeof(buf));
        fprintf(stderr, "Error occurred: %s\n", buf);
        exit(1);
    }

    exit(0);
}
