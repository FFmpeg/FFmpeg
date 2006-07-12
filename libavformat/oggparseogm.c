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

#include <stdlib.h>
#include "avformat.h"
#include "bitstream.h"
#include "bswap.h"
#include "ogg2.h"
#include "riff.h"

static int
ogm_header(AVFormatContext *s, int idx)
{
    ogg_t *ogg = s->priv_data;
    ogg_stream_t *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    uint8_t *p = os->buf + os->pstart;
    uint64_t time_unit;
    uint64_t spu;
    uint32_t default_len;

    if(!(*p & 1))
        return 0;
    if(*p != 1)
        return 1;

    p++;

    if(*p == 'v'){
        int tag;
        st->codec->codec_type = CODEC_TYPE_VIDEO;
        p += 8;
        tag = le2me_32(unaligned32(p));
        st->codec->codec_id = codec_get_bmp_id(tag);
        st->codec->codec_tag = tag;
    } else {
        int cid;
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        p += 8;
        p[4] = 0;
        cid = strtol(p, NULL, 16);
        st->codec->codec_id = codec_get_wav_id(cid);
    }

    p += 4;
    p += 4;                     /* useless size field */

    time_unit = le2me_64(unaligned64(p));
    p += 8;
    spu = le2me_64(unaligned64(p));
    p += 8;
    default_len = le2me_32(unaligned32(p));
    p += 4;

    p += 8;                     /* buffersize + bits_per_sample */

    if(st->codec->codec_type == CODEC_TYPE_VIDEO){
        st->codec->width = le2me_32(unaligned32(p));
        p += 4;
        st->codec->height = le2me_32(unaligned32(p));
        st->codec->time_base.den = spu * 10000000;
        st->codec->time_base.num = time_unit;
        st->time_base = st->codec->time_base;
    } else {
        st->codec->channels = le2me_16(unaligned16(p));
        p += 2;
        p += 2;                 /* block_align */
        st->codec->bit_rate = le2me_32(unaligned32(p)) * 8;
        st->codec->sample_rate = spu * 10000000 / time_unit;
        st->time_base.num = 1;
        st->time_base.den = st->codec->sample_rate;
    }

    return 1;
}

static int
ogm_dshow_header(AVFormatContext *s, int idx)
{
    ogg_t *ogg = s->priv_data;
    ogg_stream_t *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    uint8_t *p = os->buf + os->pstart;
    uint32_t t;

    if(!(*p & 1))
        return 0;
    if(*p != 1)
        return 1;

    t = le2me_32(unaligned32(p + 96));

    if(t == 0x05589f80){
        st->codec->codec_type = CODEC_TYPE_VIDEO;
        st->codec->codec_id = codec_get_bmp_id(le2me_32(unaligned32(p + 68)));
        st->codec->time_base.den = 10000000;
        st->codec->time_base.num = le2me_64(unaligned64(p + 164));
        st->codec->width = le2me_32(unaligned32(p + 176));
        st->codec->height = le2me_32(unaligned32(p + 180));
    } else if(t == 0x05589f81){
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        st->codec->codec_id = codec_get_wav_id(le2me_16(unaligned16(p+124)));
        st->codec->channels = le2me_16(unaligned16(p + 126));
        st->codec->sample_rate = le2me_32(unaligned32(p + 128));
        st->codec->bit_rate = le2me_32(unaligned32(p + 132)) * 8;
    }

    return 1;
}

static int
ogm_packet(AVFormatContext *s, int idx)
{
    ogg_t *ogg = s->priv_data;
    ogg_stream_t *os = ogg->streams + idx;
    uint8_t *p = os->buf + os->pstart;
    int lb;

    lb = ((*p & 2) << 1) | ((*p >> 6) & 3);
    os->pstart += lb + 1;
    os->psize -= lb + 1;

    return 0;
}

ogg_codec_t ogm_video_codec = {
    .magic = "\001video",
    .magicsize = 6,
    .header = ogm_header,
    .packet = ogm_packet
};

ogg_codec_t ogm_audio_codec = {
    .magic = "\001audio",
    .magicsize = 6,
    .header = ogm_header,
    .packet = ogm_packet
};

ogg_codec_t ogm_old_codec = {
    .magic = "\001Direct Show Samples embedded in Ogg",
    .magicsize = 35,
    .header = ogm_dshow_header,
    .packet = ogm_packet
};
