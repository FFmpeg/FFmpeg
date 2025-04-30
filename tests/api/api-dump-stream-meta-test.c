/*
 * Copyright (c) 2025 Romain Beauxis
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
 * Dump stream metadata
 */

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/timestamp.h"

static int dump_stream_meta(const char *input_filename) {
    const AVCodec *codec = NULL;
    AVPacket *pkt = NULL;
    AVFrame *fr = NULL;
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *ctx = NULL;
    AVCodecParameters *origin_par = NULL;
    AVStream *st;
    int stream_idx = 0;
    int result;
    char *metadata;

    result = avformat_open_input(&fmt_ctx, input_filename, NULL, NULL);
    if (result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't open file\n");
        return result;
    }

    result = avformat_find_stream_info(fmt_ctx, NULL);
    if (result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't get stream info\n");
        goto end;
    }

    if (fmt_ctx->nb_streams > 1) {
        av_log(NULL, AV_LOG_ERROR, "More than one stream found in input!\n");
        goto end;
    }

    origin_par = fmt_ctx->streams[stream_idx]->codecpar;
    st = fmt_ctx->streams[stream_idx];

    result = av_dict_get_string(st->metadata, &metadata, '=', ':');
    if (result < 0)
        goto end;

    printf("Stream ID: %d, codec name: %s, metadata: %s\n", stream_idx,
           avcodec_get_name(origin_par->codec_id),
           strlen(metadata) ? metadata : "N/A");
    av_free(metadata);

    codec = avcodec_find_decoder(origin_par->codec_id);
    if (!codec) {
        av_log(NULL, AV_LOG_ERROR, "Can't find decoder\n");
        result = AVERROR_DECODER_NOT_FOUND;
        goto end;
    }

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate decoder context\n");
        result = AVERROR(ENOMEM);
        goto end;
    }

    result = avcodec_parameters_to_context(ctx, origin_par);
    if (result) {
        av_log(NULL, AV_LOG_ERROR, "Can't copy decoder context\n");
        goto end;
    }

    result = avcodec_open2(ctx, codec, NULL);
    if (result < 0) {
        av_log(ctx, AV_LOG_ERROR, "Can't open decoder\n");
        goto end;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        av_log(NULL, AV_LOG_ERROR, "Cannot allocate packet\n");
        result = AVERROR(ENOMEM);
        goto end;
    }

    fr = av_frame_alloc();
    if (!fr) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate frame\n");
        result = AVERROR(ENOMEM);
        goto end;
    }

    for (;;) {
        result = av_read_frame(fmt_ctx, pkt);
        if (result)
            goto end;

        if (pkt->stream_index != stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

        printf("Stream ID: %d, packet PTS: %s, packet DTS: %s\n",
               pkt->stream_index, av_ts2str(pkt->pts), av_ts2str(pkt->dts));

        if (st->event_flags & AVSTREAM_EVENT_FLAG_METADATA_UPDATED) {
            result = av_dict_get_string(st->metadata, &metadata, '=', ':');
            if (result < 0)
                goto end;

            printf("Stream ID: %d, new metadata: %s\n", pkt->stream_index,
                   strlen(metadata) ? metadata : "N/A");
            av_free(metadata);

            st->event_flags &= ~AVSTREAM_EVENT_FLAG_METADATA_UPDATED;
        }

        result = avcodec_send_packet(ctx, pkt);
        av_packet_unref(pkt);

        if (result < 0)
            goto end;

        do {
            result = avcodec_receive_frame(ctx, fr);
            if (result == AVERROR_EOF) {
                result = 0;
                goto end;
            }

            if (result == AVERROR(EAGAIN))
                break;

            if (result < 0)
                goto end;

            result = av_dict_get_string(fr->metadata, &metadata, '=', ':');
            if (result < 0)
                goto end;

            printf("Stream ID: %d, frame PTS: %s, metadata: %s\n",
                   pkt->stream_index, av_ts2str(fr->pts),
                   strlen(metadata) ? metadata : "N/A");
            av_free(metadata);
        } while (1);
    }

end:
    av_packet_free(&pkt);
    av_frame_free(&fr);
    avformat_close_input(&fmt_ctx);
    avcodec_free_context(&ctx);
    return result;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        av_log(NULL, AV_LOG_ERROR, "Incorrect input\n");
        return 1;
    }

    if (dump_stream_meta(argv[1]) != AVERROR_EOF)
        return 1;

    return 0;
}
