/*
 * Copyright (c) 2015 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

#include <stdio.h>
#include "libavformat/avformat.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/internal.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"

static int try_decode_video_frame(AVCodecContext *codec_ctx, AVPacket *pkt, int decode)
{
    int ret = 0;
    int got_frame = 0;
    AVFrame *frame = NULL;
    int skip_frame = codec_ctx->skip_frame;

    if (!avcodec_is_open(codec_ctx)) {
        const AVCodec *codec = avcodec_find_decoder(codec_ctx->codec_id);

        ret = avcodec_open2(codec_ctx, codec, NULL);
        if (ret < 0) {
            av_log(codec_ctx, AV_LOG_ERROR, "Failed to open codec\n");
            goto end;
        }
    }

    frame = av_frame_alloc();
    if (!frame) {
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate frame\n");
        goto end;
    }

    if (!decode && avpriv_codec_get_cap_skip_frame_fill_param(codec_ctx->codec)) {
        codec_ctx->skip_frame = AVDISCARD_ALL;
    }

    do {
        ret = avcodec_decode_video2(codec_ctx, frame, &got_frame, pkt);
        av_assert0(decode || (!decode && !got_frame));
        if (ret < 0)
            break;
        pkt->data += ret;
        pkt->size -= ret;

        if (got_frame) {
            break;
        }
    } while (pkt->size > 0);

end:
    codec_ctx->skip_frame = skip_frame;

    av_frame_free(&frame);
    return ret;
}

static int find_video_stream_info(AVFormatContext *fmt_ctx, int decode)
{
    int ret = 0;
    int i, done = 0;
    AVPacket pkt;

    av_init_packet(&pkt);

    while (!done) {
        AVCodecContext *codec_ctx = NULL;
        AVStream *st;

        if ((ret = av_read_frame(fmt_ctx, &pkt)) < 0) {
            av_log(fmt_ctx, AV_LOG_ERROR, "Failed to read frame\n");
            goto end;
        }

        st = fmt_ctx->streams[pkt.stream_index];
        codec_ctx = st->codec;

        /* Writing to AVStream.codec_info_nb_frames must not be done by
         * user applications. It is done here for testing purposing as
         * find_video_stream_info tries to mimic avformat_find_stream_info
         * which writes to this field.
         * */
        if (codec_ctx->codec_type != AVMEDIA_TYPE_VIDEO ||
            st->codec_info_nb_frames++ > 0) {
            av_packet_unref(&pkt);
            continue;
        }

        ret = try_decode_video_frame(codec_ctx, &pkt, decode);
        if (ret < 0) {
            av_log(fmt_ctx, AV_LOG_ERROR, "Failed to decode video frame\n");
            goto end;
        }

        av_packet_unref(&pkt);

        /* check if all video streams have demuxed a packet */
        done = 1;
        for (i = 0; i < fmt_ctx->nb_streams; i++) {
            st = fmt_ctx->streams[i];
            codec_ctx = st->codec;

            if (codec_ctx->codec_type != AVMEDIA_TYPE_VIDEO)
                continue;

            done &= st->codec_info_nb_frames > 0;
        }
    }

end:
    av_packet_unref(&pkt);

    /* close all codecs opened in try_decode_video_frame */
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *st = fmt_ctx->streams[i];
        avcodec_close(st->codec);
    }

    return ret < 0;
}

static void dump_video_streams(const AVFormatContext *fmt_ctx, int decode)
{
    int i;

    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        const AVOption *opt = NULL;
        const AVStream *st = fmt_ctx->streams[i];
        AVCodecContext *codec_ctx = st->codec;

        printf("stream=%d, decode=%d\n", i, decode);
        while (opt = av_opt_next(codec_ctx, opt)) {
            uint8_t *str;

            if (opt->type == AV_OPT_TYPE_CONST)
                continue;

            if (!strcmp(opt->name, "frame_number"))
                continue;

            if (av_opt_get(codec_ctx, opt->name, 0, &str) >= 0) {
                printf("    %s=%s\n", opt->name, str);
                av_free(str);
            }
        }
    }
}

static int open_and_probe_video_streams(AVFormatContext **fmt_ctx, const char *filename, int decode)
{
    int ret = 0;

    ret = avformat_open_input(fmt_ctx, filename, NULL, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open input '%s'", filename);
        goto end;
    }

    ret = find_video_stream_info(*fmt_ctx, decode);
    if (ret < 0) {
        goto end;
    }

    dump_video_streams(*fmt_ctx, decode);

end:
    return ret;
}

static int check_video_streams(const AVFormatContext *fmt_ctx1, const AVFormatContext *fmt_ctx2)
{
    int i;
    int ret = 0;

    av_assert0(fmt_ctx1->nb_streams == fmt_ctx2->nb_streams);
    for (i = 0; i < fmt_ctx1->nb_streams; i++) {
        const AVOption *opt = NULL;
        const AVStream *st1 = fmt_ctx1->streams[i];
        const AVStream *st2 = fmt_ctx2->streams[i];
        AVCodecContext *codec_ctx1 = st1->codec;
        AVCodecContext *codec_ctx2 = st2->codec;

        if (codec_ctx1->codec_type != AVMEDIA_TYPE_VIDEO)
            continue;

        while (opt = av_opt_next(codec_ctx1, opt)) {
            uint8_t *str1 = NULL, *str2 = NULL;

            if (opt->type == AV_OPT_TYPE_CONST)
                continue;

            if (!strcmp(opt->name, "frame_number"))
                continue;

            av_assert0(av_opt_get(codec_ctx1, opt->name, 0, &str1) >= 0);
            av_assert0(av_opt_get(codec_ctx2, opt->name, 0, &str2) >= 0);
            if (strcmp(str1, str2)) {
                av_log(NULL, AV_LOG_ERROR, "Field %s differs: %s %s", opt->name, str1, str2);
                ret = AVERROR(EINVAL);
            }
            av_free(str1);
            av_free(str2);
        }
    }

    return ret;
}

int main(int argc, char* argv[])
{
    int ret = 0;
    AVFormatContext *fmt_ctx = NULL;
    AVFormatContext *fmt_ctx_no_decode = NULL;

    av_register_all();

    if (argc < 2) {
        av_log(NULL, AV_LOG_ERROR, "Usage: %s <input>\n", argv[0]);
        return -1;
    }

    if ((ret = open_and_probe_video_streams(&fmt_ctx_no_decode, argv[1], 0)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to probe '%s' without frame decoding\n", argv[1]);
        goto end;
    }

    if ((ret = open_and_probe_video_streams(&fmt_ctx, argv[1], 1)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to probe '%s' with frame decoding\n", argv[1]);
        goto end;
    }

    ret = check_video_streams(fmt_ctx, fmt_ctx_no_decode);

end:
    avformat_close_input(&fmt_ctx);
    avformat_close_input(&fmt_ctx_no_decode);

    return ret;
}
