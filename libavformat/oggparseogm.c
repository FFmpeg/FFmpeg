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

#include "libavcodec/bytestream.h"

#include "avformat.h"
#include "internal.h"
#include "oggdec.h"
#include "riff.h"

static int
ogm_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    GetByteContext p;
    uint64_t time_unit;
    uint64_t spu;
    uint32_t size;
    int ret;

    bytestream2_init(&p, os->buf + os->pstart, os->psize);
    if (!(bytestream2_peek_byte(&p) & 1))
        return 0;

    if (bytestream2_peek_byte(&p) == 1) {
        bytestream2_skip(&p, 1);

        if (bytestream2_peek_byte(&p) == 'v'){
            int tag;
            st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            bytestream2_skip(&p, 8);
            tag = bytestream2_get_le32(&p);
            st->codecpar->codec_id = ff_codec_get_id(ff_codec_bmp_tags, tag);
            st->codecpar->codec_tag = tag;
            if (st->codecpar->codec_id == AV_CODEC_ID_MPEG4)
                st->need_parsing = AVSTREAM_PARSE_HEADERS;
        } else if (bytestream2_peek_byte(&p) == 't') {
            st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
            st->codecpar->codec_id = AV_CODEC_ID_TEXT;
            bytestream2_skip(&p, 12);
        } else {
            uint8_t acid[5] = { 0 };
            int cid;
            st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            bytestream2_skip(&p, 8);
            bytestream2_get_buffer(&p, acid, 4);
            acid[4] = 0;
            cid = strtol(acid, NULL, 16);
            st->codecpar->codec_id = ff_codec_get_id(ff_codec_wav_tags, cid);
            // our parser completely breaks AAC in Ogg
            if (st->codecpar->codec_id != AV_CODEC_ID_AAC)
                st->need_parsing = AVSTREAM_PARSE_FULL;
        }

        size        = bytestream2_get_le32(&p);
        size        = FFMIN(size, os->psize);
        time_unit   = bytestream2_get_le64(&p);
        spu         = bytestream2_get_le64(&p);
        if (!time_unit || !spu) {
            av_log(s, AV_LOG_ERROR, "Invalid timing values.\n");
            return AVERROR_INVALIDDATA;
        }

        bytestream2_skip(&p, 4);    /* default_len */
        bytestream2_skip(&p, 8);    /* buffersize + bits_per_sample */

        if(st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            st->codecpar->width = bytestream2_get_le32(&p);
            st->codecpar->height = bytestream2_get_le32(&p);
            avpriv_set_pts_info(st, 64, time_unit, spu * 10000000);
        } else {
            st->codecpar->channels = bytestream2_get_le16(&p);
            bytestream2_skip(&p, 2); /* block_align */
            st->codecpar->bit_rate = bytestream2_get_le32(&p) * 8;
            st->codecpar->sample_rate = spu * 10000000 / time_unit;
            avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
            if (size >= 56 && st->codecpar->codec_id == AV_CODEC_ID_AAC) {
                bytestream2_skip(&p, 4);
                size -= 4;
            }
            if (size > 52) {
                size -= 52;
                if (bytestream2_get_bytes_left(&p) < size)
                    return AVERROR_INVALIDDATA;
                if ((ret = ff_alloc_extradata(st->codecpar, size)) < 0)
                    return ret;
                bytestream2_get_buffer(&p, st->codecpar->extradata, st->codecpar->extradata_size);
            }
        }

        // Update internal avctx with changes to codecpar above.
        st->internal->need_context_update = 1;
    } else if (bytestream2_peek_byte(&p) == 3) {
        bytestream2_skip(&p, 7);
        if (bytestream2_get_bytes_left(&p) > 1)
            ff_vorbis_stream_comment(s, st, p.buffer, bytestream2_get_bytes_left(&p) - 1);
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

    if (os->psize < 100)
        return AVERROR_INVALIDDATA;
    t = AV_RL32(p + 96);

    if(t == 0x05589f80){
        if (os->psize < 184)
            return AVERROR_INVALIDDATA;

        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id = ff_codec_get_id(ff_codec_bmp_tags, AV_RL32(p + 68));
        avpriv_set_pts_info(st, 64, AV_RL64(p + 164), 10000000);
        st->codecpar->width = AV_RL32(p + 176);
        st->codecpar->height = AV_RL32(p + 180);
    } else if(t == 0x05589f81){
        if (os->psize < 136)
            return AVERROR_INVALIDDATA;

        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = ff_codec_get_id(ff_codec_wav_tags, AV_RL16(p + 124));
        st->codecpar->channels = AV_RL16(p + 126);
        st->codecpar->sample_rate = AV_RL32(p + 128);
        st->codecpar->bit_rate = AV_RL32(p + 132) * 8;
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
    if (os->psize < lb + 1)
        return AVERROR_INVALIDDATA;

    os->pstart += lb + 1;
    os->psize -= lb + 1;

    while (lb--)
        os->pduration += (uint64_t)p[lb+1] << (lb*8);

    return 0;
}

const struct ogg_codec ff_ogm_video_codec = {
    .magic = "\001video",
    .magicsize = 6,
    .header = ogm_header,
    .packet = ogm_packet,
    .granule_is_start = 1,
    .nb_header = 2,
};

const struct ogg_codec ff_ogm_audio_codec = {
    .magic = "\001audio",
    .magicsize = 6,
    .header = ogm_header,
    .packet = ogm_packet,
    .granule_is_start = 1,
    .nb_header = 2,
};

const struct ogg_codec ff_ogm_text_codec = {
    .magic = "\001text",
    .magicsize = 5,
    .header = ogm_header,
    .packet = ogm_packet,
    .granule_is_start = 1,
    .nb_header = 2,
};

const struct ogg_codec ff_ogm_old_codec = {
    .magic = "\001Direct Show Samples embedded in Ogg",
    .magicsize = 35,
    .header = ogm_dshow_header,
    .packet = ogm_packet,
    .granule_is_start = 1,
    .nb_header = 1,
};
