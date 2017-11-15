/*
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#ifndef FFTOOLS_FFSERVER_CONFIG_H
#define FFTOOLS_FFSERVER_CONFIG_H

#define FFM_PACKET_SIZE 4096

#include "libavutil/dict.h"
#include "libavformat/avformat.h"
#include "libavformat/network.h"

#define FFSERVER_MAX_STREAMS 20

/* each generated stream is described here */
enum FFServerStreamType {
    STREAM_TYPE_LIVE,
    STREAM_TYPE_STATUS,
    STREAM_TYPE_REDIRECT,
};

enum FFServerIPAddressAction {
    IP_ALLOW = 1,
    IP_DENY,
};

typedef struct FFServerIPAddressACL {
    struct FFServerIPAddressACL *next;
    enum FFServerIPAddressAction action;
    /* These are in host order */
    struct in_addr first;
    struct in_addr last;
} FFServerIPAddressACL;

/**
 * This holds the stream parameters for an AVStream, it cannot be a AVStream
 * because AVStreams cannot be instanciated without a AVFormatContext, especially
 * not outside libavformat.
 *
 * The fields of this struct have the same semantics as the fields of an AVStream.
 */
typedef struct LayeredAVStream {
    int index;
    int id;
    AVCodecParameters *codecpar;
    AVCodecContext *codec;
    AVRational time_base;
    int pts_wrap_bits;
    AVRational sample_aspect_ratio;
    char *recommended_encoder_configuration;
} LayeredAVStream;

/* description of each stream of the ffserver.conf file */
typedef struct FFServerStream {
    enum FFServerStreamType stream_type;
    char filename[1024];          /* stream filename */
    struct FFServerStream *feed;  /* feed we are using (can be null if coming from file) */
    AVDictionary *in_opts;        /* input parameters */
    AVDictionary *metadata;       /* metadata to set on the stream */
    AVInputFormat *ifmt;          /* if non NULL, force input format */
    AVOutputFormat *fmt;
    FFServerIPAddressACL *acl;
    char dynamic_acl[1024];
    int nb_streams;
    int prebuffer;                /* Number of milliseconds early to start */
    int64_t max_time;             /* Number of milliseconds to run */
    int send_on_key;
    LayeredAVStream *streams[FFSERVER_MAX_STREAMS];
    int feed_streams[FFSERVER_MAX_STREAMS]; /* index of streams in the feed */
    char feed_filename[1024];     /* file name of the feed storage, or
                                     input file name for a stream */
    pid_t pid;                    /* Of ffmpeg process */
    time_t pid_start;             /* Of ffmpeg process */
    char **child_argv;
    struct FFServerStream *next;
    unsigned bandwidth;           /* bandwidth, in kbits/s */
    /* RTSP options */
    char *rtsp_option;
    /* multicast specific */
    int is_multicast;
    struct in_addr multicast_ip;
    int multicast_port;           /* first port used for multicast */
    int multicast_ttl;
    int loop;                     /* if true, send the stream in loops (only meaningful if file) */
    char single_frame;            /* only single frame */

    /* feed specific */
    int feed_opened;              /* true if someone is writing to the feed */
    int is_feed;                  /* true if it is a feed */
    int readonly;                 /* True if writing is prohibited to the file */
    int truncate;                 /* True if feeder connection truncate the feed file */
    int conns_served;
    int64_t bytes_served;
    int64_t feed_max_size;        /* maximum storage size, zero means unlimited */
    int64_t feed_write_index;     /* current write position in feed (it wraps around) */
    int64_t feed_size;            /* current size of feed */
    struct FFServerStream *next_feed;
} FFServerStream;

typedef struct FFServerConfig {
    char *filename;
    FFServerStream *first_feed;   /* contains only feeds */
    FFServerStream *first_stream; /* contains all streams, including feeds */
    unsigned int nb_max_http_connections;
    unsigned int nb_max_connections;
    uint64_t max_bandwidth;
    int debug;
    int bitexact;
    char logfilename[1024];
    struct sockaddr_in http_addr;
    struct sockaddr_in rtsp_addr;
    int errors;
    int warnings;
    int use_defaults;
    // Following variables MUST NOT be used outside configuration parsing code.
    enum AVCodecID guessed_audio_codec_id;
    enum AVCodecID guessed_video_codec_id;
    AVDictionary *video_opts;     /* AVOptions for video encoder */
    AVDictionary *audio_opts;     /* AVOptions for audio encoder */
    AVCodecContext *dummy_actx;   /* Used internally to test audio AVOptions. */
    AVCodecContext *dummy_vctx;   /* Used internally to test video AVOptions. */
    int no_audio;
    int no_video;
    int line_num;
    int stream_use_defaults;
} FFServerConfig;

void ffserver_get_arg(char *buf, int buf_size, const char **pp);

void ffserver_parse_acl_row(FFServerStream *stream, FFServerStream* feed,
                            FFServerIPAddressACL *ext_acl,
                            const char *p, const char *filename, int line_num);

int ffserver_parse_ffconfig(const char *filename, FFServerConfig *config);

void ffserver_free_child_args(void *argsp);

#endif /* FFTOOLS_FFSERVER_CONFIG_H */
