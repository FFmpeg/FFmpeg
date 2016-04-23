/*
 * TTA demuxer
 * Copyright (c) 2006 Alex Beregszaszi
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "internal.h"
#include "id3v1.h"

typedef struct TTAContext {
    int totalframes, currentframe;
    int frame_size;
    int last_frame_size;
} TTAContext;

static int tta_probe(AVProbeData *p)
{
    const uint8_t *d = p->buf;

    if (d[0] == 'T' && d[1] == 'T' && d[2] == 'A' && d[3] == '1')
        return AVPROBE_SCORE_EXTENSION + 30;
    return 0;
}

static int tta_read_header(AVFormatContext *s)
{
    TTAContext *c = s->priv_data;
    AVStream *st;
    int i, channels, bps, samplerate, datalen;
    int64_t framepos, start_offset;

    if (!av_dict_get(s->metadata, "", NULL, AV_DICT_IGNORE_SUFFIX))
        ff_id3v1_read(s);

    start_offset = avio_tell(s->pb);
    if (start_offset < 0)
        return start_offset;
    if (avio_rl32(s->pb) != AV_RL32("TTA1"))
        return -1; // not tta file

    avio_skip(s->pb, 2); // FIXME: flags
    channels = avio_rl16(s->pb);
    bps = avio_rl16(s->pb);
    samplerate = avio_rl32(s->pb);
    if(samplerate <= 0 || samplerate > 1000000){
        av_log(s, AV_LOG_ERROR, "nonsense samplerate\n");
        return -1;
    }

    datalen = avio_rl32(s->pb);
    if(datalen < 0){
        av_log(s, AV_LOG_ERROR, "nonsense datalen\n");
        return -1;
    }

    avio_skip(s->pb, 4); // header crc

    c->frame_size      = samplerate * 256 / 245;
    c->last_frame_size = datalen % c->frame_size;
    if (!c->last_frame_size)
        c->last_frame_size = c->frame_size;
    c->totalframes = datalen / c->frame_size + (c->last_frame_size < c->frame_size);
    c->currentframe = 0;

    if(c->totalframes >= UINT_MAX/sizeof(uint32_t)){
        av_log(s, AV_LOG_ERROR, "totalframes too large\n");
        return -1;
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avpriv_set_pts_info(st, 64, 1, samplerate);
    st->start_time = 0;
    st->duration = datalen;

    framepos = avio_tell(s->pb);
    if (framepos < 0)
        return framepos;
    framepos += 4 * c->totalframes + 4;

    for (i = 0; i < c->totalframes; i++) {
        uint32_t size = avio_rl32(s->pb);
        av_add_index_entry(st, framepos, i * c->frame_size, size, 0,
                           AVINDEX_KEYFRAME);
        framepos += size;
    }
    avio_skip(s->pb, 4); // seektable crc

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_TTA;
    st->codecpar->channels = channels;
    st->codecpar->sample_rate = samplerate;
    st->codecpar->bits_per_coded_sample = bps;

    st->codecpar->extradata_size = avio_tell(s->pb) - start_offset;
    if (st->codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE <= (unsigned)st->codecpar->extradata_size) {
        //this check is redundant as avio_read should fail
        av_log(s, AV_LOG_ERROR, "extradata_size too large\n");
        return -1;
    }
    st->codecpar->extradata = av_mallocz(st->codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codecpar->extradata) {
        st->codecpar->extradata_size = 0;
        return AVERROR(ENOMEM);
    }
    avio_seek(s->pb, start_offset, SEEK_SET);
    avio_read(s->pb, st->codecpar->extradata, st->codecpar->extradata_size);

    return 0;
}

static int tta_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    TTAContext *c = s->priv_data;
    AVStream *st = s->streams[0];
    int size, ret;

    // FIXME!
    if (c->currentframe >= c->totalframes)
        return AVERROR_EOF;

    size = st->index_entries[c->currentframe].size;

    ret = av_get_packet(s->pb, pkt, size);
    pkt->dts = st->index_entries[c->currentframe++].timestamp;
    pkt->duration = c->currentframe == c->totalframes ? c->last_frame_size :
                                                        c->frame_size;
    return ret;
}

static int tta_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    TTAContext *c = s->priv_data;
    AVStream *st = s->streams[stream_index];
    int index = av_index_search_timestamp(st, timestamp, flags);
    if (index < 0)
        return -1;
    if (avio_seek(s->pb, st->index_entries[index].pos, SEEK_SET) < 0)
        return -1;

    c->currentframe = index;

    return 0;
}

AVInputFormat ff_tta_demuxer = {
    .name           = "tta",
    .long_name      = NULL_IF_CONFIG_SMALL("TTA (True Audio)"),
    .priv_data_size = sizeof(TTAContext),
    .read_probe     = tta_probe,
    .read_header    = tta_read_header,
    .read_packet    = tta_read_packet,
    .read_seek      = tta_read_seek,
    .extensions     = "tta",
};
