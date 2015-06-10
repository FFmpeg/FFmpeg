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
#include "apetag.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "id3v1.h"
#include "libavutil/crc.h"
#include "libavutil/dict.h"

typedef struct TTAContext {
    int totalframes, currentframe;
    int frame_size;
    int last_frame_size;
} TTAContext;

static unsigned long tta_check_crc(unsigned long checksum, const uint8_t *buf,
                                   unsigned int len)
{
    return av_crc(av_crc_get_table(AV_CRC_32_IEEE_LE), checksum, buf, len);
}

static int tta_probe(AVProbeData *p)
{
    if (AV_RL32(&p->buf[0]) == MKTAG('T', 'T', 'A', '1') &&
        (AV_RL16(&p->buf[4]) == 1 || AV_RL16(&p->buf[4]) == 2) &&
        AV_RL16(&p->buf[6]) > 0 &&
        AV_RL16(&p->buf[8]) > 0 &&
        AV_RL32(&p->buf[10]) > 0)
        return AVPROBE_SCORE_EXTENSION + 30;
    return 0;
}

static int tta_read_header(AVFormatContext *s)
{
    TTAContext *c = s->priv_data;
    AVStream *st;
    int i, channels, bps, samplerate;
    int64_t framepos, start_offset;
    uint32_t nb_samples, crc;

    ff_id3v1_read(s);

    start_offset = avio_tell(s->pb);
    if (start_offset < 0)
        return start_offset;
    ffio_init_checksum(s->pb, tta_check_crc, UINT32_MAX);
    if (avio_rl32(s->pb) != AV_RL32("TTA1"))
        return AVERROR_INVALIDDATA;

    avio_skip(s->pb, 2); // FIXME: flags
    channels = avio_rl16(s->pb);
    bps = avio_rl16(s->pb);
    samplerate = avio_rl32(s->pb);
    if(samplerate <= 0 || samplerate > 1000000){
        av_log(s, AV_LOG_ERROR, "nonsense samplerate\n");
        return AVERROR_INVALIDDATA;
    }

    nb_samples = avio_rl32(s->pb);
    if (!nb_samples) {
        av_log(s, AV_LOG_ERROR, "invalid number of samples\n");
        return AVERROR_INVALIDDATA;
    }

    crc = ffio_get_checksum(s->pb) ^ UINT32_MAX;
    if (crc != avio_rl32(s->pb) && s->error_recognition & AV_EF_CRCCHECK) {
        av_log(s, AV_LOG_ERROR, "Header CRC error\n");
        return AVERROR_INVALIDDATA;
    }

    c->frame_size      = samplerate * 256 / 245;
    c->last_frame_size = nb_samples % c->frame_size;
    if (!c->last_frame_size)
        c->last_frame_size = c->frame_size;
    c->totalframes = nb_samples / c->frame_size + (c->last_frame_size < c->frame_size);
    c->currentframe = 0;

    if(c->totalframes >= UINT_MAX/sizeof(uint32_t) || c->totalframes <= 0){
        av_log(s, AV_LOG_ERROR, "totalframes %d invalid\n", c->totalframes);
        return AVERROR_INVALIDDATA;
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avpriv_set_pts_info(st, 64, 1, samplerate);
    st->start_time = 0;
    st->duration = nb_samples;

    framepos = avio_tell(s->pb);
    if (framepos < 0)
        return framepos;
    framepos += 4 * c->totalframes + 4;

    if (ff_alloc_extradata(st->codec, avio_tell(s->pb) - start_offset))
        return AVERROR(ENOMEM);

    avio_seek(s->pb, start_offset, SEEK_SET);
    avio_read(s->pb, st->codec->extradata, st->codec->extradata_size);

    ffio_init_checksum(s->pb, tta_check_crc, UINT32_MAX);
    for (i = 0; i < c->totalframes; i++) {
        uint32_t size = avio_rl32(s->pb);
        int r;
        if ((r = av_add_index_entry(st, framepos, i * c->frame_size, size, 0,
                                    AVINDEX_KEYFRAME)) < 0)
            return r;
        framepos += size;
    }
    crc = ffio_get_checksum(s->pb) ^ UINT32_MAX;
    if (crc != avio_rl32(s->pb) && s->error_recognition & AV_EF_CRCCHECK) {
        av_log(s, AV_LOG_ERROR, "Seek table CRC error\n");
        return AVERROR_INVALIDDATA;
    }

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = AV_CODEC_ID_TTA;
    st->codec->channels = channels;
    st->codec->sample_rate = samplerate;
    st->codec->bits_per_coded_sample = bps;

    if (s->pb->seekable) {
        int64_t pos = avio_tell(s->pb);
        ff_ape_parse_tag(s);
        avio_seek(s->pb, pos, SEEK_SET);
    }

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

    if (st->nb_index_entries < c->totalframes) {
        av_log(s, AV_LOG_ERROR, "Index entry disappeared\n");
        return AVERROR_INVALIDDATA;
    }

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
