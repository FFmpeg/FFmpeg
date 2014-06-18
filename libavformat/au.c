/*
 * AU muxer and demuxer
 * Copyright (c) 2001 Fabrice Bellard
 *
 * first version by Francois Revol <revol@free.fr>
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

/*
 * Reference documents:
 * http://www.opengroup.org/public/pubs/external/auformat.html
 * http://www.goice.co.jp/member/mo/formats/au.html
 */

#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "pcm.h"

static const AVCodecTag codec_au_tags[] = {
    { AV_CODEC_ID_PCM_MULAW,  1 },
    { AV_CODEC_ID_PCM_S8,     2 },
    { AV_CODEC_ID_PCM_S16BE,  3 },
    { AV_CODEC_ID_PCM_S24BE,  4 },
    { AV_CODEC_ID_PCM_S32BE,  5 },
    { AV_CODEC_ID_PCM_F32BE,  6 },
    { AV_CODEC_ID_PCM_F64BE,  7 },
    { AV_CODEC_ID_PCM_ALAW,  27 },
    { AV_CODEC_ID_NONE,       0 },
};

#if CONFIG_AU_DEMUXER

static int au_probe(AVProbeData *p)
{
    if (p->buf[0] == '.' && p->buf[1] == 's' &&
        p->buf[2] == 'n' && p->buf[3] == 'd')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

#define BLOCK_SIZE 1024

static int au_read_header(AVFormatContext *s)
{
    int size;
    unsigned int tag;
    AVIOContext *pb = s->pb;
    unsigned int id, channels, rate;
    int bps;
    enum AVCodecID codec;
    AVStream *st;

    tag = avio_rl32(pb);
    if (tag != MKTAG('.', 's', 'n', 'd'))
        return AVERROR_INVALIDDATA;
    size = avio_rb32(pb); /* header size */
    avio_rb32(pb);        /* data size */

    id       = avio_rb32(pb);
    rate     = avio_rb32(pb);
    channels = avio_rb32(pb);

    if (size > 24) {
        /* skip unused data */
        avio_skip(pb, size - 24);
    }

    codec = ff_codec_get_id(codec_au_tags, id);

    if (codec == AV_CODEC_ID_NONE) {
        avpriv_request_sample(s, "unknown or unsupported codec tag: %u", id);
        return AVERROR_PATCHWELCOME;
    }

    bps = av_get_bits_per_sample(codec);
    if (!bps) {
        avpriv_request_sample(s, "Unknown bits per sample");
        return AVERROR_PATCHWELCOME;
    }

    if (channels == 0 || channels >= INT_MAX / (BLOCK_SIZE * bps >> 3)) {
        av_log(s, AV_LOG_ERROR, "Invalid number of channels %u\n", channels);
        return AVERROR_INVALIDDATA;
    }

    if (rate == 0 || rate > INT_MAX) {
        av_log(s, AV_LOG_ERROR, "Invalid sample rate: %u\n", rate);
        return AVERROR_INVALIDDATA;
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_tag   = id;
    st->codecpar->codec_id    = codec;
    st->codecpar->channels    = channels;
    st->codecpar->sample_rate = rate;
    st->codecpar->bit_rate    = channels * rate * bps;
    st->codecpar->block_align = channels * bps >> 3;

    st->start_time = 0;
    avpriv_set_pts_info(st, 64, 1, rate);

    return 0;
}

static int au_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    ret = av_get_packet(s->pb, pkt, BLOCK_SIZE *
                        s->streams[0]->codecpar->block_align);
    if (ret < 0)
        return ret;

    pkt->stream_index = 0;
    pkt->duration     = ret / s->streams[0]->codecpar->block_align;

    return 0;
}

AVInputFormat ff_au_demuxer = {
    .name        = "au",
    .long_name   = NULL_IF_CONFIG_SMALL("Sun AU"),
    .read_probe  = au_probe,
    .read_header = au_read_header,
    .read_packet = au_read_packet,
    .read_seek   = ff_pcm_read_seek,
    .codec_tag   = (const AVCodecTag* const []) { codec_au_tags, 0 },
};

#endif /* CONFIG_AU_DEMUXER */

#if CONFIG_AU_MUXER

#include "rawenc.h"

/* if we don't know the size in advance */
#define AU_UNKNOWN_SIZE ((uint32_t)(~0))

/* AUDIO_FILE header */
static int put_au_header(AVIOContext *pb, AVCodecParameters *par)
{
    if (!par->codec_tag)
        return AVERROR(EINVAL);

    ffio_wfourcc(pb, ".snd");                   /* magic number */
    avio_wb32(pb, 24);                          /* header size */
    avio_wb32(pb, AU_UNKNOWN_SIZE);             /* data size */
    avio_wb32(pb, par->codec_tag);              /* codec ID */
    avio_wb32(pb, par->sample_rate);
    avio_wb32(pb, par->channels);

    return 0;
}

static int au_write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int ret;

    s->priv_data = NULL;

    if ((ret = put_au_header(pb, s->streams[0]->codecpar)) < 0)
        return ret;

    avio_flush(pb);

    return 0;
}

static int au_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int64_t file_size;

    if (s->pb->seekable) {
        /* update file size */
        file_size = avio_tell(pb);
        avio_seek(pb, 8, SEEK_SET);
        avio_wb32(pb, (uint32_t)(file_size - 24));
        avio_seek(pb, file_size, SEEK_SET);
        avio_flush(pb);
    }

    return 0;
}

AVOutputFormat ff_au_muxer = {
    .name          = "au",
    .long_name     = NULL_IF_CONFIG_SMALL("Sun AU"),
    .mime_type     = "audio/basic",
    .extensions    = "au",
    .audio_codec   = AV_CODEC_ID_PCM_S16BE,
    .video_codec   = AV_CODEC_ID_NONE,
    .write_header  = au_write_header,
    .write_packet  = ff_raw_write_packet,
    .write_trailer = au_write_trailer,
    .codec_tag     = (const AVCodecTag* const []) { codec_au_tags, 0 },
    .flags         = AVFMT_NOTIMESTAMPS,
};

#endif /* CONFIG_AU_MUXER */
