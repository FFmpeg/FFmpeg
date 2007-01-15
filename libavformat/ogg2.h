/**
    Copyright (C) 2005  Michael Ahlberg, Måns Rullgård

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
**/

#ifndef OGG_H
#define OGG_H

#include "avformat.h"

typedef struct ogg_codec {
    int8_t *magic;
    uint8_t magicsize;
    int8_t *name;
    int (*header)(AVFormatContext *, int);
    int (*packet)(AVFormatContext *, int);
    uint64_t (*gptopts)(AVFormatContext *, int, uint64_t);
} ogg_codec_t;

typedef struct ogg_stream {
    uint8_t *buf;
    unsigned int bufsize;
    unsigned int bufpos;
    unsigned int pstart;
    unsigned int psize;
    uint32_t serial;
    uint32_t seq;
    uint64_t granule, lastgp;
    int flags;
    ogg_codec_t *codec;
    int header;
    int nsegs, segp;
    uint8_t segments[255];
    void *private;
} ogg_stream_t;

typedef struct ogg_state {
    uint64_t pos;
    int curidx;
    struct ogg_state *next;
    int nstreams;
    ogg_stream_t streams[1];
} ogg_state_t;

typedef struct ogg {
    ogg_stream_t *streams;
    int nstreams;
    int headers;
    int curidx;
    uint64_t size;
    ogg_state_t *state;
} ogg_t;

#define OGG_FLAG_CONT 1
#define OGG_FLAG_BOS  2
#define OGG_FLAG_EOS  4

extern ogg_codec_t vorbis_codec;
extern ogg_codec_t theora_codec;
extern ogg_codec_t flac_codec;
extern ogg_codec_t ogm_video_codec;
extern ogg_codec_t ogm_audio_codec;
extern ogg_codec_t ogm_old_codec;

extern int vorbis_comment(AVFormatContext *ms, uint8_t *buf, int size);

#endif
