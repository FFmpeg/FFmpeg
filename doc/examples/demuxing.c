/*
 * Copyright (c) 2012 Stefano Sabatini
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
 * libavformat demuxing API use example.
 *
 * Show how to use the libavformat and libavcodec API to demux and
 * decode video data.
 */

#include <libavutil/imgutils.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *dec_ctx = NULL;
static AVCodec *dec = NULL;
static AVStream *stream = NULL;
static const char *src_filename = NULL;
static const char *dst_filename = NULL;
static FILE *dst_file = NULL;
static uint8_t *dst_data[4] = {NULL};
static int dst_linesize[4];
static int dst_bufsize;
static int stream_idx;
static AVFrame *frame = NULL;
static AVPacket pkt;
static int frame_count = 0;

static int decode_packet(int *got_frame, int cached)
{
    int ret;

    if (pkt.stream_index != stream_idx)
        return 0;

    /* decode video frame */
    ret = avcodec_decode_video2(dec_ctx, frame, got_frame, &pkt);
    if (ret < 0) {
        fprintf(stderr, "Error decoding video frame\n");
        return ret;
    }

    if (*got_frame) {
        printf("frame%s n:%d coded_n:%d pts:%s\n",
               cached ? "(cached)" : "",
               frame_count++, frame->coded_picture_number,
               av_ts2timestr(frame->pts, &dec_ctx->time_base));

        /* copy decoded frame to destination buffer:
         * this is required since rawvideo expect non aligned data */
        av_image_copy(dst_data, dst_linesize,
                      (const uint8_t **)(frame->data), frame->linesize,
                      dec_ctx->pix_fmt, dec_ctx->width, dec_ctx->height);

        /* write to rawvideo file */
        fwrite(dst_data[0], 1, dst_bufsize, dst_file);
    }

    return ret;
}

int main (int argc, char **argv)
{
    int ret, got_frame;

    if (argc != 3) {
        fprintf(stderr, "usage: %s input_file output_file\n"
                "API example program to show how to read frames from an input file.\n"
                "This program reads frames from a file, decode them, and write them "
                "to a rawvideo file named like output_file."
                "\n", argv[0]);
        exit(1);
    }
    src_filename = argv[1];
    dst_filename = argv[2];

    /* register all formats and codecs */
    av_register_all();

    /* open input file, and allocated format context */
    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find video stream in file\n");
        goto end;
    }
    stream_idx = ret;
    stream = fmt_ctx->streams[stream_idx];

    /* find decoder for the stream */
    dec_ctx = stream->codec;
    dec = avcodec_find_decoder(dec_ctx->codec_id);
    if (!dec) {
        fprintf(stderr, "Failed to find any codec\n");
        ret = 1;
        goto end;
    }

    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec\n");
        goto end;
    }

    /* dump input information to stderr */
    av_dump_format(fmt_ctx, 0, src_filename, 0);

    dst_file = fopen(dst_filename, "wb");
    if (!dst_file) {
        fprintf(stderr, "Could not open destination file %s\n", dst_filename);
        ret = 1;
        goto end;
    }

    frame = avcodec_alloc_frame();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        ret = 1;
        goto end;
    }

    /* allocate image where the decoded image will be put */
    ret = av_image_alloc(dst_data, dst_linesize,
                         dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt, 1);
    if (ret < 0) {
        fprintf(stderr, "Could not alloc raw video buffer\n");
        goto end;
    }
    dst_bufsize = ret;

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.size = 0;
    pkt.data = NULL;

    printf("Demuxing file '%s' to '%s'\n", src_filename, dst_filename);

    /* read frames from the file */
    while (av_read_frame(fmt_ctx, &pkt) >= 0)
        decode_packet(&got_frame, 0);

    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;
    do {
        decode_packet(&got_frame, 1);
    } while (got_frame);

    printf("Demuxing succeeded. Play the output file with the command:\n"
           "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
           av_get_pix_fmt_name(dec_ctx->pix_fmt), dec_ctx->width, dec_ctx->height,
           dst_filename);

end:
    avcodec_close(dec_ctx);
    avformat_close_input(&fmt_ctx);
    if (dst_file)
        fclose(dst_file);
    av_free(frame);
    av_free(dst_data[0]);

    return ret < 0;
}
