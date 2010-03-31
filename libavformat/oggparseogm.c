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
#include "libavutil/intreadwrite.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "oggdec.h"
#include "riff.h"

static int
ogm_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    const uint8_t *p = os->buf + os->pstart;
    uint64_t time_unit;
    uint64_t spu;
    uint32_t default_len;

    if(!(*p & 1))
        return 0;

    if(*p == 1) {
        p++;

        if(*p == 'v'){
            int tag;
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            p += 8;
            tag = bytestream_get_le32(&p);
            st->codec->codec_id = ff_codec_get_id(ff_codec_bmp_tags, tag);
            st->codec->codec_tag = tag;
        } else if (*p == 't') {
            st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
            st->codec->codec_id = CODEC_ID_TEXT;
            p += 12;
        } else {
            uint8_t acid[5];
            int cid;
            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            p += 8;
            bytestream_get_buffer(&p, acid, 4);
            acid[4] = 0;
            cid = strtol(acid, NULL, 16);
            st->codec->codec_id = ff_codec_get_id(ff_codec_wav_tags, cid);
            st->need_parsing = AVSTREAM_PARSE_FULL;
        }

        p += 4;                     /* useless size field */

        time_unit   = bytestream_get_le64(&p);
        spu         = bytestream_get_le64(&p);
        default_len = bytestream_get_le32(&p);

        p += 8;                     /* buffersize + bits_per_sample */

        if(st->codec->codec_type == AVMEDIA_TYPE_VIDEO){
            st->codec->width = bytestream_get_le32(&p);
            st->codec->height = bytestream_get_le32(&p);
            st->codec->time_base.den = spu * 10000000;
            st->codec->time_base.num = time_unit;
            st->time_base = st->codec->time_base;
        } else {
            st->codec->channels = bytestream_get_le16(&p);
            p += 2;                 /* block_align */
            st->codec->bit_rate = bytestream_get_le32(&p) * 8;
            st->codec->sample_rate = spu * 10000000 / time_unit;
            st->time_base.num = 1;
            st->time_base.den = st->codec->sample_rate;
        }
    } else if (*p == 3) {
        if (os->psize > 8)
            ff_vorbis_comment(s, &st->metadata, p+7, os->psize-8);
    }

    return 1;
}

static int
ogm_dshow_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    uint8_t *p = os->buf + os->pstart;
    uint32_t t;

    if(!(*p & 1))
        return 0;
    if(*p != 1)
        return 1;

    t = AV_RL32(p + 96);

    if(t == 0x05589f80){
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codec->codec_id = ff_codec_get_id(ff_codec_bmp_tags, AV_RL32(p + 68));
        st->codec->time_base.den = 10000000;
        st->codec->time_base.num = AV_RL64(p + 164);
        st->codec->width = AV_RL32(p + 176);
        st->codec->height = AV_RL32(p + 180);
    } else if(t == 0x05589f81){
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id = ff_codec_get_id(ff_codec_wav_tags, AV_RL16(p + 124));
        st->codec->channels = AV_RL16(p + 126);
        st->codec->sample_rate = AV_RL32(p + 128);
        st->codec->bit_rate = AV_RL32(p + 132) * 8;
    }

    return 1;
}

static int
ogm_packet(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    uint8_t *p = os->buf + os->pstart;
    int lb;

    if(*p & 8)
        os->pflags |= AV_PKT_FLAG_KEY;

    lb = ((*p & 2) << 1) | ((*p >> 6) & 3);
    os->pstart += lb + 1;
    os->psize -= lb + 1;

    while (lb--)
        os->pduration += p[lb+1] << (lb*8);

    return 0;
}

const struct ogg_codec ff_ogm_video_codec = {
    .magic = "\001video",
    .magicsize = 6,
    .header = ogm_header,
    .packet = ogm_packet,
    .granule_is_start = 1,
};

const struct ogg_codec ff_ogm_audio_codec = {
    .magic = "\001audio",
    .magicsize = 6,
    .header = ogm_header,
    .packet = ogm_packet,
    .granule_is_start = 1,
};

const struct ogg_codec ff_ogm_text_codec = {
    .magic = "\001text",
    .magicsize = 5,
    .header = ogm_header,
    .packet = ogm_packet,
    .granule_is_start = 1,
};

const struct ogg_codec ff_ogm_old_codec = {
    .magic = "\001Direct Show Samples embedded in Ogg",
    .magicsize = 35,
    .header = ogm_dshow_header,
    .packet = ogm_packet,
    .granule_is_start = 1,
};
