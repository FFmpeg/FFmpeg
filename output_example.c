/*
 * Libavformat API example: Output a media file in any supported
 * libavformat format. The default codecs are used.
 * 
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
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "avformat.h"

/* 5 seconds stream duration */
#define STREAM_DURATION 5.0


/**************************************************************/
/* audio output */

float t, tincr;
int16_t *samples;
uint8_t *audio_outbuf;
int audio_outbuf_size;
int audio_input_frame_size;

/* 
 * add an audio output stream
 */
AVStream *add_audio_stream(AVFormatContext *oc, int codec_id)
{
    AVCodec *codec;
    AVCodecContext *c;
    AVStream *st;

    st = av_new_stream(oc, 1);
    if (!st) {
        fprintf(stderr, "Could not alloc stream\n");
        exit(1);
    }

    /* find the MP2 encoder */
    codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }
    c = &st->codec;
    c->codec_type = CODEC_TYPE_AUDIO;

    /* put sample parameters */
    c->bit_rate = 64000;
    c->sample_rate = 44100;
    c->channels = 2;

    /* open it */
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }

    /* init signal generator */
    t = 0;
    tincr = 2 * M_PI * 440.0 / c->sample_rate;

    audio_outbuf_size = 10000;
    audio_outbuf = malloc(audio_outbuf_size);

    /* ugly hack for PCM codecs (will be removed ASAP with new PCM
       support to compute the input frame size in samples */
    if (c->frame_size <= 1) {
        audio_input_frame_size = audio_outbuf_size / c->channels;
        switch(st->codec.codec_id) {
        case CODEC_ID_PCM_S16LE:
        case CODEC_ID_PCM_S16BE:
        case CODEC_ID_PCM_U16LE:
        case CODEC_ID_PCM_U16BE:
            audio_input_frame_size >>= 1;
            break;
        default:
            break;
        }
    } else {
        audio_input_frame_size = c->frame_size;
    }
    samples = malloc(audio_input_frame_size * 2 * c->channels);

    return st;
}

void write_audio_frame(AVFormatContext *oc, AVStream *st)
{
    int j, out_size;
    AVCodecContext *c;


    c = &st->codec;

    for(j=0;j<audio_input_frame_size;j++) {
        samples[2*j] = (int)(sin(t) * 10000);
        samples[2*j+1] = samples[2*j];
        t += tincr;
    }
    
    out_size = avcodec_encode_audio(c, audio_outbuf, audio_outbuf_size, samples);

    /* write the compressed frame in the media file */
    if (av_write_frame(oc, st->index, audio_outbuf, out_size) != 0) {
        fprintf(stderr, "Error while writing audio frame\n");
        exit(1);
    }
}

/**************************************************************/
/* video output */

AVFrame *picture;
uint8_t *video_outbuf;
int frame_count, video_outbuf_size;

/* add a video output stream */
AVStream *add_video_stream(AVFormatContext *oc, int codec_id)
{
    AVCodec *codec;
    AVCodecContext *c;
    AVStream *st;
    uint8_t *picture_buf;
    int size;

    st = av_new_stream(oc, 0);
    if (!st) {
        fprintf(stderr, "Could not alloc stream\n");
        exit(1);
    }
    
    /* find the mpeg1 video encoder */
    codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    c = &st->codec;
    c->codec_type = CODEC_TYPE_VIDEO;

    /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two */
    c->width = 352;  
    c->height = 288;
    /* frames per second */
    c->frame_rate = 25;  
    c->frame_rate_base= 1;
    c->gop_size = 12; /* emit one intra frame every twelve frames */

    /* open it */
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }

    /* alloc various buffers */
    picture= avcodec_alloc_frame();
    video_outbuf_size = 100000;
    video_outbuf = malloc(video_outbuf_size);

    size = c->width * c->height;
    picture_buf = malloc((size * 3) / 2); /* size for YUV 420 */
    
    picture->data[0] = picture_buf;
    picture->data[1] = picture->data[0] + size;
    picture->data[2] = picture->data[1] + size / 4;
    picture->linesize[0] = c->width;
    picture->linesize[1] = c->width / 2;
    picture->linesize[2] = c->width / 2;

    return st;
}    

void write_video_frame(AVFormatContext *oc, AVStream *st)
{
    int x, y, i, out_size;
    AVCodecContext *c;

    c = &st->codec;
    
    /* prepare a dummy image */
    /* Y */
    i = frame_count++;
    for(y=0;y<c->height;y++) {
        for(x=0;x<c->width;x++) {
            picture->data[0][y * picture->linesize[0] + x] = x + y + i * 3;
        }
    }
    
    /* Cb and Cr */
    for(y=0;y<c->height/2;y++) {
        for(x=0;x<c->width/2;x++) {
            picture->data[1][y * picture->linesize[1] + x] = 128 + y + i * 2;
            picture->data[2][y * picture->linesize[2] + x] = 64 + x + i * 5;
        }
    }

    /* encode the image */
    out_size = avcodec_encode_video(c, video_outbuf, video_outbuf_size, picture);

    /* write the compressed frame in the media file */
    if (av_write_frame(oc, st->index, video_outbuf, out_size) != 0) {
        fprintf(stderr, "Error while writing video frame\n");
        exit(1);
    }
}

/**************************************************************/
/* media file output */

int main(int argc, char **argv)
{
    const char *filename;
    AVOutputFormat *fmt;
    AVFormatContext *oc;
    AVStream *st, *audio_st, *video_st;
    int i;
    double audio_pts, video_pts;
    
    /* initialize libavcodec, and register all codecs and formats */
    av_register_all();
    
    if (argc != 2) {
        printf("usage: %s output_file\n"
               "API example program for to output media file with libavformat\n"
               "\n", argv[0]);
        exit(1);
    }
    
    filename = argv[1];

    /* auto detect the output format from the name. default is
       mpeg. */
    fmt = guess_format(NULL, filename, NULL);
    if (!fmt) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        fmt = guess_format("mpeg", NULL, NULL);
    }
    if (!fmt) {
        fprintf(stderr, "Could not find suitable output format\n");
        exit(1);
    }
    
    /* allocate the output media context */
    oc = av_mallocz(sizeof(AVFormatContext));
    if (!oc) {
        fprintf(stderr, "Memory error\n");
        exit(1);
    }
    oc->oformat = fmt;

    /* add the audio and video streams using the default format codecs
       and initialize the codecs */
    video_st = NULL;
    audio_st = NULL;
    if (fmt->video_codec != CODEC_ID_NONE) {
        video_st = add_video_stream(oc, fmt->video_codec);
    }
    if (fmt->audio_codec != CODEC_ID_NONE) {
        audio_st = add_audio_stream(oc, fmt->audio_codec);
    }

    dump_format(oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        if (url_fopen(&oc->pb, filename, URL_WRONLY) < 0) {
            fprintf(stderr, "Could not open '%s'\n", filename);
            exit(1);
        }
    }
    
    /* set the output parameters (must be done even if no parameters) */
    av_set_parameters(oc, NULL);

    /* write the stream header, if any */
    av_write_header(oc);
    
    for(;;) {
        /* compute current audio and video time */
        if (audio_st)
            audio_pts = (double)audio_st->pts.val * oc->pts_num / oc->pts_den;
        else
            audio_pts = 0.0;
        
        if (video_st)
            video_pts = (double)video_st->pts.val * oc->pts_num / oc->pts_den;
        else
            video_pts = 0.0;

        if ((!audio_st || audio_pts >= STREAM_DURATION) && 
            (!video_st || video_pts >= STREAM_DURATION))
            break;
        
        /* write interleaved audio and video frames */
        if (!video_st || (video_st && audio_pts < video_pts)) {
            write_audio_frame(oc, audio_st);
        } else {
            write_video_frame(oc, video_st);
        }
    }

    /* close each codec */
    for(i = 0;i < oc->nb_streams; i++) {
        st = oc->streams[i];
        avcodec_close(&st->codec);
    }

    /* write the trailer, if any */
    av_write_trailer(oc);
    
    if (!(fmt->flags & AVFMT_NOFILE)) {
        /* close the output file */
        url_fclose(&oc->pb);
    }

    /* free the stream */
    av_free(oc);

    return 0;
}
