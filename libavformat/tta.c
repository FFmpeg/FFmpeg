/*
 * TTA demuxer
 * Copyright (c) 2006 Alex Beregszaszi
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

#include "libavcodec/get_bits.h"
#include "avformat.h"
#include "id3v2.h"
#include "id3v1.h"

typedef struct {
    int totalframes, currentframe;
} TTAContext;

static int tta_probe(AVProbeData *p)
{
    const uint8_t *d = p->buf;

    if (ff_id3v2_match(d))
        d += ff_id3v2_tag_len(d);

    if (d - p->buf >= p->buf_size)
        return 0;

    if (d[0] == 'T' && d[1] == 'T' && d[2] == 'A' && d[3] == '1')
        return 80;
    return 0;
}

static int tta_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    TTAContext *c = s->priv_data;
    AVStream *st;
    int i, channels, bps, samplerate, datalen, framelen;
    uint64_t framepos, start_offset;

    ff_id3v2_read(s);
    if (!av_metadata_get(s->metadata, "", NULL, AV_METADATA_IGNORE_SUFFIX))
        ff_id3v1_read(s);

    start_offset = url_ftell(s->pb);
    if (get_le32(s->pb) != AV_RL32("TTA1"))
        return -1; // not tta file

    url_fskip(s->pb, 2); // FIXME: flags
    channels = get_le16(s->pb);
    bps = get_le16(s->pb);
    samplerate = get_le32(s->pb);
    if(samplerate <= 0 || samplerate > 1000000){
        av_log(s, AV_LOG_ERROR, "nonsense samplerate\n");
        return -1;
    }

    datalen = get_le32(s->pb);
    if(datalen < 0){
        av_log(s, AV_LOG_ERROR, "nonsense datalen\n");
        return -1;
    }

    url_fskip(s->pb, 4); // header crc

    framelen = samplerate*256/245;
    c->totalframes = datalen / framelen + ((datalen % framelen) ? 1 : 0);
    c->currentframe = 0;

    if(c->totalframes >= UINT_MAX/sizeof(uint32_t)){
        av_log(s, AV_LOG_ERROR, "totalframes too large\n");
        return -1;
    }

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    av_set_pts_info(st, 64, 1, samplerate);
    st->start_time = 0;
    st->duration = datalen;

    framepos = url_ftell(s->pb) + 4*c->totalframes + 4;

    for (i = 0; i < c->totalframes; i++) {
        uint32_t size = get_le32(s->pb);
        av_add_index_entry(st, framepos, i*framelen, size, 0, AVINDEX_KEYFRAME);
        framepos += size;
    }
    url_fskip(s->pb, 4); // seektable crc

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_TTA;
    st->codec->channels = channels;
    st->codec->sample_rate = samplerate;
    st->codec->bits_per_coded_sample = bps;

    st->codec->extradata_size = url_ftell(s->pb) - start_offset;
    if(st->codec->extradata_size+FF_INPUT_BUFFER_PADDING_SIZE <= (unsigned)st->codec->extradata_size){
        //this check is redundant as get_buffer should fail
        av_log(s, AV_LOG_ERROR, "extradata_size too large\n");
        return -1;
    }
    st->codec->extradata = av_mallocz(st->codec->extradata_size+FF_INPUT_BUFFER_PADDING_SIZE);
    url_fseek(s->pb, start_offset, SEEK_SET);
    get_buffer(s->pb, st->codec->extradata, st->codec->extradata_size);

    return 0;
}

static int tta_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    TTAContext *c = s->priv_data;
    AVStream *st = s->streams[0];
    int size, ret;

    // FIXME!
    if (c->currentframe > c->totalframes)
        return -1;

    size = st->index_entries[c->currentframe].size;

    ret = av_get_packet(s->pb, pkt, size);
    pkt->dts = st->index_entries[c->currentframe++].timestamp;
    return ret;
}

static int tta_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    TTAContext *c = s->priv_data;
    AVStream *st = s->streams[stream_index];
    int index = av_index_search_timestamp(st, timestamp, flags);
    if (index < 0)
        return -1;

    c->currentframe = index;
    url_fseek(s->pb, st->index_entries[index].pos, SEEK_SET);

    return 0;
}

AVInputFormat tta_demuxer = {
    "tta",
    NULL_IF_CONFIG_SMALL("True Audio"),
    sizeof(TTAContext),
    tta_probe,
    tta_read_header,
    tta_read_packet,
    NULL,
    tta_read_seek,
    .extensions = "tta",
};
