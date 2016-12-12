/*
 * Wideband Single-bit Data (WSD) demuxer
 * Copyright (c) 2014 Peter Ross
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

#include "libavutil/intreadwrite.h"
#include "libavutil/timecode.h"
#include "avformat.h"
#include "internal.h"
#include "rawdec.h"

static int wsd_probe(AVProbeData *p)
{
    if (p->buf_size < 45 || memcmp(p->buf, "1bit", 4) ||
        !AV_RB32(p->buf + 36) || !p->buf[44] ||
        (p->buf[0] >= 0x10 && (AV_RB32(p->buf + 20) < 0x80 || AV_RB32(p->buf + 24) < 0x80)))
       return 0;
    return AVPROBE_SCORE_MAX;
}

static int empty_string(const char *buf, unsigned size)
{
    while (size--) {
       if (*buf++ != ' ')
           return 0;
    }
    return 1;
}

static int wsd_to_av_channel_layoyt(AVFormatContext *s, int bit)
{
    switch (bit) {
    case 2: return AV_CH_BACK_RIGHT;
    case 3:
        avpriv_request_sample(s, "Rr-middle");
        break;
    case 4: return AV_CH_BACK_CENTER;
    case 5:
        avpriv_request_sample(s, "Lr-middle");
        break;
    case 6: return AV_CH_BACK_LEFT;
    case 24: return AV_CH_LOW_FREQUENCY;
    case 26: return AV_CH_FRONT_RIGHT;
    case 27: return AV_CH_FRONT_RIGHT_OF_CENTER;
    case 28: return AV_CH_FRONT_CENTER;
    case 29: return AV_CH_FRONT_LEFT_OF_CENTER;
    case 30: return AV_CH_FRONT_LEFT;
    default:
        av_log(s, AV_LOG_WARNING, "reserved channel assignment\n");
        break;
    }
    return 0;
}

static int get_metadata(AVFormatContext *s, const char *const tag, const unsigned size)
{
    uint8_t *buf;
    if (!(size + 1))
        return AVERROR(ENOMEM);

    buf = av_malloc(size + 1);
    if (!buf)
        return AVERROR(ENOMEM);

    if (avio_read(s->pb, buf, size) != size) {
        av_free(buf);
        return AVERROR(EIO);
    }

    if (empty_string(buf, size)) {
        av_free(buf);
        return 0;
    }

    buf[size] = 0;
    av_dict_set(&s->metadata, tag, buf, AV_DICT_DONT_STRDUP_VAL);
    return 0;
}

static int wsd_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *st;
    int version;
    uint32_t text_offset, data_offset, channel_assign;
    char playback_time[AV_TIMECODE_STR_SIZE];

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(pb, 8);
    version = avio_r8(pb);
    av_log(s, AV_LOG_DEBUG, "version: %i.%i\n", version >> 4, version & 0xF);
    avio_skip(pb, 11);

    if (version < 0x10) {
        text_offset = 0x80;
        data_offset = 0x800;
        avio_skip(pb, 8);
    } else {
        text_offset = avio_rb32(pb);
        data_offset = avio_rb32(pb);
    }

    avio_skip(pb, 4);
    av_timecode_make_smpte_tc_string(playback_time, avio_rb32(pb), 0);
    av_dict_set(&s->metadata, "playback_time", playback_time, 0);

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = s->iformat->raw_codec_id;
    st->codecpar->sample_rate = avio_rb32(pb) / 8;
    avio_skip(pb, 4);
    st->codecpar->channels    = avio_r8(pb) & 0xF;
    st->codecpar->bit_rate    = (int64_t)st->codecpar->channels * st->codecpar->sample_rate * 8LL;
    if (!st->codecpar->channels)
        return AVERROR_INVALIDDATA;

    avio_skip(pb, 3);
    channel_assign         = avio_rb32(pb);
    if (!(channel_assign & 1)) {
        int i;
        for (i = 1; i < 32; i++)
            if (channel_assign & (1 << i))
                st->codecpar->channel_layout |= wsd_to_av_channel_layoyt(s, i);
    }

    avio_skip(pb, 16);
    if (avio_rb32(pb))
       avpriv_request_sample(s, "emphasis");

    if (avio_seek(pb, text_offset, SEEK_SET) >= 0) {
        get_metadata(s, "title",       128);
        get_metadata(s, "composer",    128);
        get_metadata(s, "song_writer", 128);
        get_metadata(s, "artist",      128);
        get_metadata(s, "album",       128);
        get_metadata(s, "genre",        32);
        get_metadata(s, "date",         32);
        get_metadata(s, "location",     32);
        get_metadata(s, "comment",     512);
        get_metadata(s, "user",        512);
    }

    return avio_seek(pb, data_offset, SEEK_SET);
}

AVInputFormat ff_wsd_demuxer = {
    .name         = "wsd",
    .long_name    = NULL_IF_CONFIG_SMALL("Wideband Single-bit Data (WSD)"),
    .read_probe   = wsd_probe,
    .read_header  = wsd_read_header,
    .read_packet  = ff_raw_read_partial_packet,
    .extensions   = "wsd",
    .flags        = AVFMT_GENERIC_INDEX | AVFMT_NO_BYTE_SEEK,
    .raw_codec_id = AV_CODEC_ID_DSD_MSBF,
};
