/*
 * Copyright (c) 2003 Fabrice Bellard
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
 * libavformat API example.
 *
 * @example output.c
 * Output a media file in any supported libavformat format. The default
 * codecs are used.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "libavutil/channel_layout.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavformat/avformat.h"
#include "libavresample/avresample.h"
#include "libswscale/swscale.h"

/* 5 seconds stream duration */
#define STREAM_DURATION   5.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_NB_FRAMES  ((int)(STREAM_DURATION * STREAM_FRAME_RATE))
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */

#define SCALE_FLAGS SWS_BICUBIC

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream *st;
    AVCodecContext *enc;

    /* pts of the next frame that will be generated */
    int64_t next_pts;

    AVFrame *frame;
    AVFrame *tmp_frame;

    float t, tincr, tincr2;

    struct SwsContext *sws_ctx;
    AVAudioResampleContext *avr;
} OutputStream;

/**************************************************************/
/* audio output */

/*
 * add an audio output stream
 */
static void add_audio_stream(OutputStream *ost, AVFormatContext *oc,
                             enum AVCodecID codec_id)
{
    AVCodecContext *c;
    AVCodec *codec;
    int ret;

    /* find the audio encoder */
    codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        fprintf(stderr, "Could not alloc stream\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }
    ost->enc = c;

    /* put sample parameters */
    c->sample_fmt     = codec->sample_fmts           ? codec->sample_fmts[0]           : AV_SAMPLE_FMT_S16;
    c->sample_rate    = codec->supported_samplerates ? codec->supported_samplerates[0] : 44100;
    c->channel_layout = codec->channel_layouts       ? codec->channel_layouts[0]       : AV_CH_LAYOUT_STEREO;
    c->channels       = av_get_channel_layout_nb_channels(c->channel_layout);
    c->bit_rate       = 64000;

    ost->st->time_base = (AVRational){ 1, c->sample_rate };

    // some formats want stream headers to be separate
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    /* initialize sample format conversion;
     * to simplify the code, we always pass the data through lavr, even
     * if the encoder supports the generated format directly -- the price is
     * some extra data copying;
     */
    ost->avr = avresample_alloc_context();
    if (!ost->avr) {
        fprintf(stderr, "Error allocating the resampling context\n");
        exit(1);
    }

    av_opt_set_int(ost->avr, "in_sample_fmt",      AV_SAMPLE_FMT_S16,   0);
    av_opt_set_int(ost->avr, "in_sample_rate",     44100,               0);
    av_opt_set_int(ost->avr, "in_channel_layout",  AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(ost->avr, "out_sample_fmt",     c->sample_fmt,       0);
    av_opt_set_int(ost->avr, "out_sample_rate",    c->sample_rate,      0);
    av_opt_set_int(ost->avr, "out_channel_layout", c->channel_layout,   0);

    ret = avresample_open(ost->avr);
    if (ret < 0) {
        fprintf(stderr, "Error opening the resampling context\n");
        exit(1);
    }
}

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
                                  uint64_t channel_layout,
                                  int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame) {
        fprintf(stderr, "Error allocating an audio frame\n");
        exit(1);
    }

    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples) {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            fprintf(stderr, "Error allocating an audio buffer\n");
            exit(1);
        }
    }

    return frame;
}

static void open_audio(AVFormatContext *oc, OutputStream *ost)
{
    AVCodecContext *c;
    int nb_samples, ret;

    c = ost->enc;

    /* open it */
    if (avcodec_open2(c, NULL, NULL) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }

    /* init signal generator */
    ost->t     = 0;
    ost->tincr = 2 * M_PI * 110.0 / c->sample_rate;
    /* increment frequency by 110 Hz per second */
    ost->tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;

    if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 10000;
    else
        nb_samples = c->frame_size;

    ost->frame     = alloc_audio_frame(c->sample_fmt, c->channel_layout,
                                       c->sample_rate, nb_samples);
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, AV_CH_LAYOUT_STEREO,
                                       44100, nb_samples);

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and
 * 'nb_channels' channels. */
static AVFrame *get_audio_frame(OutputStream *ost)
{
    AVFrame *frame = ost->tmp_frame;
    int j, i, v;
    int16_t *q = (int16_t*)frame->data[0];

    /* check if we want to generate more frames */
    if (av_compare_ts(ost->next_pts, ost->enc->time_base,
                      STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
        return NULL;


    for (j = 0; j < frame->nb_samples; j++) {
        v = (int)(sin(ost->t) * 10000);
        for (i = 0; i < ost->enc->channels; i++)
            *q++ = v;
        ost->t     += ost->tincr;
        ost->tincr += ost->tincr2;
    }

    return frame;
}

/* if a frame is provided, send it to the encoder, otherwise flush the encoder;
 * return 1 when encoding is finished, 0 otherwise
 */
static int encode_audio_frame(AVFormatContext *oc, OutputStream *ost,
                              AVFrame *frame)
{
    AVPacket pkt = { 0 }; // data and size must be 0;
    int got_packet;

    av_init_packet(&pkt);
    avcodec_encode_audio2(ost->enc, &pkt, frame, &got_packet);

    if (got_packet) {
        pkt.stream_index = ost->st->index;

        av_packet_rescale_ts(&pkt, ost->enc->time_base, ost->st->time_base);

        /* Write the compressed frame to the media file. */
        if (av_interleaved_write_frame(oc, &pkt) != 0) {
            fprintf(stderr, "Error while writing audio frame\n");
            exit(1);
        }
    }

    return (frame || got_packet) ? 0 : 1;
}

/*
 * encode one audio frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int process_audio_stream(AVFormatContext *oc, OutputStream *ost)
{
    AVFrame *frame;
    int got_output = 0;
    int ret;

    frame = get_audio_frame(ost);
    got_output |= !!frame;

    /* feed the data to lavr */
    if (frame) {
        ret = avresample_convert(ost->avr, NULL, 0, 0,
                                 frame->extended_data, frame->linesize[0],
                                 frame->nb_samples);
        if (ret < 0) {
            fprintf(stderr, "Error feeding audio data to the resampler\n");
            exit(1);
        }
    }

    while ((frame && avresample_available(ost->avr) >= ost->frame->nb_samples) ||
           (!frame && avresample_get_out_samples(ost->avr, 0))) {
        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally;
         * make sure we do not overwrite it here
         */
        ret = av_frame_make_writable(ost->frame);
        if (ret < 0)
            exit(1);

        /* the difference between the two avresample calls here is that the
         * first one just reads the already converted data that is buffered in
         * the lavr output buffer, while the second one also flushes the
         * resampler */
        if (frame) {
            ret = avresample_read(ost->avr, ost->frame->extended_data,
                                  ost->frame->nb_samples);
        } else {
            ret = avresample_convert(ost->avr, ost->frame->extended_data,
                                     ost->frame->linesize[0], ost->frame->nb_samples,
                                     NULL, 0, 0);
        }

        if (ret < 0) {
            fprintf(stderr, "Error while resampling\n");
            exit(1);
        } else if (frame && ret != ost->frame->nb_samples) {
            fprintf(stderr, "Too few samples returned from lavr\n");
            exit(1);
        }

        ost->frame->nb_samples = ret;

        ost->frame->pts        = ost->next_pts;
        ost->next_pts         += ost->frame->nb_samples;

        got_output |= encode_audio_frame(oc, ost, ret ? ost->frame : NULL);
    }

    return !got_output;
}

/**************************************************************/
/* video output */

/* Add a video output stream. */
static void add_video_stream(OutputStream *ost, AVFormatContext *oc,
                             enum AVCodecID codec_id)
{
    AVCodecContext *c;
    AVCodec *codec;

    /* find the video encoder */
    codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        fprintf(stderr, "Could not alloc stream\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }
    ost->enc = c;

    /* Put sample parameters. */
    c->bit_rate = 400000;
    /* Resolution must be a multiple of two. */
    c->width    = 352;
    c->height   = 288;
    /* timebase: This is the fundamental unit of time (in seconds) in terms
     * of which frame timestamps are represented. For fixed-fps content,
     * timebase should be 1/framerate and timestamp increments should be
     * identical to 1. */
    ost->st->time_base = (AVRational){ 1, STREAM_FRAME_RATE };
    c->time_base       = ost->st->time_base;

    c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
    c->pix_fmt       = STREAM_PIX_FMT;
    if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        /* just for testing, we also add B-frames */
        c->max_b_frames = 2;
    }
    if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
        /* Needed to avoid using macroblocks in which some coeffs overflow.
         * This does not happen with normal video, it just happens here as
         * the motion of the chroma plane does not match the luma plane. */
        c->mb_decision = 2;
    }
    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }

    return picture;
}

static void open_video(AVFormatContext *oc, OutputStream *ost)
{
    AVCodecContext *c;
    int ret;

    c = ost->enc;

    /* open the codec */
    if (avcodec_open2(c, NULL, NULL) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }

    /* Allocate the encoded raw picture. */
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        fprintf(stderr, "Could not allocate picture\n");
        exit(1);
    }

    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    ost->tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
        if (!ost->tmp_frame) {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}

/* Prepare a dummy image. */
static void fill_yuv_image(AVFrame *pict, int frame_index,
                           int width, int height)
{
    int x, y, i, ret;

    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally;
     * make sure we do not overwrite it here
     */
    ret = av_frame_make_writable(pict);
    if (ret < 0)
        exit(1);

    i = frame_index;

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
}

static AVFrame *get_video_frame(OutputStream *ost)
{
    AVCodecContext *c = ost->enc;

    /* check if we want to generate more frames */
    if (av_compare_ts(ost->next_pts, c->time_base,
                      STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
        return NULL;

    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        /* as we only generate a YUV420P picture, we must convert it
         * to the codec pixel format if needed */
        if (!ost->sws_ctx) {
            ost->sws_ctx = sws_getContext(c->width, c->height,
                                          AV_PIX_FMT_YUV420P,
                                          c->width, c->height,
                                          c->pix_fmt,
                                          SCALE_FLAGS, NULL, NULL, NULL);
            if (!ost->sws_ctx) {
                fprintf(stderr,
                        "Cannot initialize the conversion context\n");
                exit(1);
            }
        }
        fill_yuv_image(ost->tmp_frame, ost->next_pts, c->width, c->height);
        sws_scale(ost->sws_ctx, ost->tmp_frame->data, ost->tmp_frame->linesize,
                  0, c->height, ost->frame->data, ost->frame->linesize);
    } else {
        fill_yuv_image(ost->frame, ost->next_pts, c->width, c->height);
    }

    ost->frame->pts = ost->next_pts++;

    return ost->frame;
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(AVFormatContext *oc, OutputStream *ost)
{
    int ret;
    AVCodecContext *c;
    AVFrame *frame;
    AVPacket pkt   = { 0 };
    int got_packet = 0;

    c = ost->enc;

    frame = get_video_frame(ost);

    av_init_packet(&pkt);

    /* encode the image */
    ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding a video frame\n");
        exit(1);
    }

    if (got_packet) {
        av_packet_rescale_ts(&pkt, c->time_base, ost->st->time_base);
        pkt.stream_index = ost->st->index;

        /* Write the compressed frame to the media file. */
        ret = av_interleaved_write_frame(oc, &pkt);
    }

    if (ret != 0) {
        fprintf(stderr, "Error while writing video frame\n");
        exit(1);
    }

    return (frame || got_packet) ? 0 : 1;
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    avresample_free(&ost->avr);
}

/**************************************************************/
/* media file output */

int main(int argc, char **argv)
{
    OutputStream video_st = { 0 }, audio_st = { 0 };
    const char *filename;
    AVOutputFormat *fmt;
    AVFormatContext *oc;
    int have_video = 0, have_audio = 0;
    int encode_video = 0, encode_audio = 0;

    /* Initialize libavcodec, and register all codecs and formats. */
    av_register_all();

    if (argc != 2) {
        printf("usage: %s output_file\n"
               "API example program to output a media file with libavformat.\n"
               "The output format is automatically guessed according to the file extension.\n"
               "Raw images can also be output by using '%%d' in the filename\n"
               "\n", argv[0]);
        return 1;
    }

    filename = argv[1];

    /* Autodetect the output format from the name. default is MPEG. */
    fmt = av_guess_format(NULL, filename, NULL);
    if (!fmt) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        fmt = av_guess_format("mpeg", NULL, NULL);
    }
    if (!fmt) {
        fprintf(stderr, "Could not find suitable output format\n");
        return 1;
    }

    /* Allocate the output media context. */
    oc = avformat_alloc_context();
    if (!oc) {
        fprintf(stderr, "Memory error\n");
        return 1;
    }
    oc->oformat = fmt;
    snprintf(oc->filename, sizeof(oc->filename), "%s", filename);

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
        add_video_stream(&video_st, oc, fmt->video_codec);
        have_video = 1;
        encode_video = 1;
    }
    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
        add_audio_stream(&audio_st, oc, fmt->audio_codec);
        have_audio = 1;
        encode_audio = 1;
    }

    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (have_video)
        open_video(oc, &video_st);
    if (have_audio)
        open_audio(oc, &audio_st);

    av_dump_format(oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        if (avio_open(&oc->pb, filename, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "Could not open '%s'\n", filename);
            return 1;
        }
    }

    /* Write the stream header, if any. */
    avformat_write_header(oc, NULL);

    while (encode_video || encode_audio) {
        /* select the stream to encode */
        if (encode_video &&
            (!encode_audio || av_compare_ts(video_st.next_pts, video_st.enc->time_base,
                                            audio_st.next_pts, audio_st.enc->time_base) <= 0)) {
            encode_video = !write_video_frame(oc, &video_st);
        } else {
            encode_audio = !process_audio_stream(oc, &audio_st);
        }
    }

    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(oc);

    /* Close each codec. */
    if (have_video)
        close_stream(oc, &video_st);
    if (have_audio)
        close_stream(oc, &audio_st);

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_close(oc->pb);

    /* free the stream */
    avformat_free_context(oc);

    return 0;
}
