/*
 * Copyright (c) 2015 Ludmila Glinskih
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
 * Seek test.
 */

#include "libavutil/adler32.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"

int64_t *pts_array;
uint32_t *crc_array;
int size_of_array;
int number_of_elements;

static int add_crc_to_array(uint32_t crc, int64_t pts)
{
    if (size_of_array <= number_of_elements) {
        if (size_of_array == 0)
            size_of_array = 10;
        size_of_array *= 2;
        crc_array = av_realloc_f(crc_array, size_of_array, sizeof(uint32_t));
        pts_array = av_realloc_f(pts_array, size_of_array, sizeof(int64_t));
        if ((crc_array == NULL) || (pts_array == NULL)) {
            av_log(NULL, AV_LOG_ERROR, "Can't allocate array to store crcs\n");
            return AVERROR(ENOMEM);
        }
    }
    crc_array[number_of_elements] = crc;
    pts_array[number_of_elements] = pts;
    number_of_elements++;
    return 0;
}

static int compare_crc_in_array(uint32_t crc, int64_t pts)
{
    int i;
    for (i = 0; i < number_of_elements; i++) {
        if (pts_array[i] == pts) {
            if (crc_array[i] == crc) {
                printf("Comparing 0x%08"PRIx32" %"PRId64" %d is OK\n", crc, pts, i);
                return 0;
            }
            else {
                av_log(NULL, AV_LOG_ERROR, "Incorrect crc of a frame after seeking\n");
                return -1;
            }
        }
    }
    av_log(NULL, AV_LOG_ERROR, "Incorrect pts of a frame after seeking\n");
    return -1;
}

static int compute_crc_of_packets(AVFormatContext *fmt_ctx, int video_stream,
                                AVCodecContext *ctx, AVFrame *fr, uint64_t ts_start, uint64_t ts_end, int no_seeking)
{
    int number_of_written_bytes;
    int got_frame = 0;
    int result;
    int end_of_stream = 0;
    int byte_buffer_size;
    uint8_t *byte_buffer;
    uint32_t crc;
    AVPacket pkt;

    byte_buffer_size = av_image_get_buffer_size(ctx->pix_fmt, ctx->width, ctx->height, 16);
    byte_buffer = av_malloc(byte_buffer_size);
    if (!byte_buffer) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate buffer\n");
        return AVERROR(ENOMEM);
    }

    if (!no_seeking) {
        result = av_seek_frame(fmt_ctx, video_stream, ts_start, AVSEEK_FLAG_ANY);
        printf("Seeking to %"PRId64", computing crc for frames with pts < %"PRId64"\n", ts_start, ts_end);
        if (result < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error in seeking\n");
            return result;
        }
        avcodec_flush_buffers(ctx);
    }

    av_init_packet(&pkt);
    do {
        if (!end_of_stream)
            if (av_read_frame(fmt_ctx, &pkt) < 0)
                end_of_stream = 1;
        if (end_of_stream) {
            pkt.data = NULL;
            pkt.size = 0;
        }
        if (pkt.stream_index == video_stream || end_of_stream) {
            got_frame = 0;
            if ((pkt.pts == AV_NOPTS_VALUE) && (!end_of_stream)) {
                av_log(NULL, AV_LOG_ERROR, "Error: frames doesn't have pts values\n");
                return -1;
            }
            result = avcodec_decode_video2(ctx, fr, &got_frame, &pkt);
            if (result < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error decoding frame\n");
                return result;
            }
            if (got_frame) {
                number_of_written_bytes = av_image_copy_to_buffer(byte_buffer, byte_buffer_size,
                                        (const uint8_t* const *)fr->data, (const int*) fr->linesize,
                                        ctx->pix_fmt, ctx->width, ctx->height, 1);
                if (number_of_written_bytes < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Can't copy image to buffer\n");
                    return number_of_written_bytes;
                }
                if ((!no_seeking) && (fr->pts > ts_end))
                    break;
                crc = av_adler32_update(0, (const uint8_t*)byte_buffer, number_of_written_bytes);
                printf("%10"PRId64", 0x%08"PRIx32"\n", fr->pts, crc);
                if (no_seeking) {
                    if (add_crc_to_array(crc, fr->pts) < 0)
                        return -1;
                }
                else {
                    if (compare_crc_in_array(crc, fr->pts) < 0)
                        return -1;
                }
            }
        }
        av_packet_unref(&pkt);
        av_init_packet(&pkt);
    } while ((!end_of_stream || got_frame) && (no_seeking || (fr->pts + fr->pkt_duration <= ts_end)));

    av_packet_unref(&pkt);
    av_freep(&byte_buffer);

    return 0;
}

static long int read_seek_range(const char *string_with_number)
{
    long int number;
    char *end_of_string = NULL;
    number = strtol(string_with_number, &end_of_string, 10);
    if ((strlen(string_with_number) != end_of_string - string_with_number)  || (number < 0)) {
        av_log(NULL, AV_LOG_ERROR, "Incorrect input ranges of seeking\n");
        return -1;
    }
    else if ((number == LONG_MAX) || (number == LONG_MIN)) {
        if (errno == ERANGE) {
            av_log(NULL, AV_LOG_ERROR, "Incorrect input ranges of seeking\n");
            return -1;
        }
    }
    return number;
}

static int seek_test(const char *input_filename, const char *start, const char *end)
{
    AVCodec *codec = NULL;
    AVCodecContext *ctx= NULL;
    AVCodecParameters *origin_par = NULL;
    AVFrame *fr = NULL;
    AVFormatContext *fmt_ctx = NULL;
    int video_stream;
    int result;
    int i, j;
    long int start_ts, end_ts;

    size_of_array = 0;
    number_of_elements = 0;
    crc_array = NULL;
    pts_array = NULL;

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

    start_ts = read_seek_range(start);
    end_ts = read_seek_range(end);
    if ((start_ts < 0) || (end_ts < 0)) {
        result = -1;
        goto end;
    }

    //TODO: add ability to work with audio format
    video_stream = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream < 0) {
      av_log(NULL, AV_LOG_ERROR, "Can't find video stream in input file\n");
      result = video_stream;
      goto end;
    }

    origin_par = fmt_ctx->streams[video_stream]->codecpar;

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

    fr = av_frame_alloc();
    if (!fr) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate frame\n");
        result = AVERROR(ENOMEM);
        goto end;
    }

    result = compute_crc_of_packets(fmt_ctx, video_stream, ctx, fr, 0, 0, 1);
    if (result != 0)
        goto end;

    for (i = start_ts; i < end_ts; i += 100) {
        for (j = i + 100; j < end_ts; j += 100) {
            result = compute_crc_of_packets(fmt_ctx, video_stream, ctx, fr, i, j, 0);
            if (result != 0)
                break;
        }
    }

end:
    av_freep(&crc_array);
    av_freep(&pts_array);
    av_frame_free(&fr);
    avcodec_close(ctx);
    avformat_close_input(&fmt_ctx);
    avcodec_free_context(&ctx);
    return result;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        av_log(NULL, AV_LOG_ERROR, "Incorrect input\n");
        return 1;
    }

    if (seek_test(argv[1], argv[2], argv[3]) != 0)
        return 1;

    return 0;
}
