/*
 * RSD demuxer
 * Copyright (c) 2013 James Almer
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

#include "libavcodec/bytestream.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio.h"
#include "internal.h"

static const AVCodecTag rsd_tags[] = {
    { AV_CODEC_ID_ADPCM_PSX,       MKTAG('V','A','G',' ') },
    { AV_CODEC_ID_ADPCM_THP_LE,    MKTAG('G','A','D','P') },
    { AV_CODEC_ID_ADPCM_THP,       MKTAG('W','A','D','P') },
    { AV_CODEC_ID_ADPCM_IMA_RAD,   MKTAG('R','A','D','P') },
    { AV_CODEC_ID_ADPCM_IMA_WAV,   MKTAG('X','A','D','P') },
    { AV_CODEC_ID_PCM_S16BE,       MKTAG('P','C','M','B') },
    { AV_CODEC_ID_PCM_S16LE,       MKTAG('P','C','M',' ') },
    { AV_CODEC_ID_XMA2,            MKTAG('X','M','A',' ') },
    { AV_CODEC_ID_NONE, 0 },
};

static const uint32_t rsd_unsupported_tags[] = {
    MKTAG('O','G','G',' '),
};

static int rsd_probe(AVProbeData *p)
{
    if (memcmp(p->buf, "RSD", 3) || p->buf[3] - '0' < 2 || p->buf[3] - '0' > 6)
        return 0;
    if (AV_RL32(p->buf +  8) > 256 || !AV_RL32(p->buf +  8))
        return AVPROBE_SCORE_MAX / 8;
    if (AV_RL32(p->buf + 16) > 8*48000 || !AV_RL32(p->buf + 16))
        return AVPROBE_SCORE_MAX / 8;
    return AVPROBE_SCORE_MAX;
}

static int rsd_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int i, ret, version, start = 0x800;
    AVCodecContext *codec;
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(pb, 3); // "RSD"
    version = avio_r8(pb) - '0';

    codec = st->codec;
    codec->codec_type = AVMEDIA_TYPE_AUDIO;
    codec->codec_tag  = avio_rl32(pb);
    codec->codec_id   = ff_codec_get_id(rsd_tags, codec->codec_tag);
    if (!codec->codec_id) {
        char tag_buf[32];

        av_get_codec_tag_string(tag_buf, sizeof(tag_buf), codec->codec_tag);
        for (i=0; i < FF_ARRAY_ELEMS(rsd_unsupported_tags); i++) {
            if (codec->codec_tag == rsd_unsupported_tags[i]) {
                avpriv_request_sample(s, "Codec tag: %s", tag_buf);
                return AVERROR_PATCHWELCOME;
            }
        }
        av_log(s, AV_LOG_ERROR, "Unknown codec tag: %s\n", tag_buf);
        return AVERROR_INVALIDDATA;
    }

    codec->channels = avio_rl32(pb);
    if (!codec->channels)
        return AVERROR_INVALIDDATA;

    avio_skip(pb, 4); // Bit depth
    codec->sample_rate = avio_rl32(pb);
    if (!codec->sample_rate)
        return AVERROR_INVALIDDATA;

    avio_skip(pb, 4); // Unknown

    switch (codec->codec_id) {
    case AV_CODEC_ID_XMA2:
        codec->block_align = 2048;
        ff_alloc_extradata(codec, 34);
        if (!codec->extradata)
            return AVERROR(ENOMEM);
        memset(codec->extradata, 0, 34);
        break;
    case AV_CODEC_ID_ADPCM_PSX:
        codec->block_align = 16 * codec->channels;
        if (pb->seekable)
            st->duration = av_get_audio_frame_duration(codec, avio_size(pb) - start);
        break;
    case AV_CODEC_ID_ADPCM_IMA_RAD:
        codec->block_align = 20 * codec->channels;
        if (pb->seekable)
            st->duration = av_get_audio_frame_duration(codec, avio_size(pb) - start);
        break;
    case AV_CODEC_ID_ADPCM_IMA_WAV:
        if (version == 2)
            start = avio_rl32(pb);

        codec->bits_per_coded_sample = 4;
        codec->block_align = 36 * codec->channels;
        if (pb->seekable)
            st->duration = av_get_audio_frame_duration(codec, avio_size(pb) - start);
        break;
    case AV_CODEC_ID_ADPCM_THP_LE:
        /* RSD3GADP is mono, so only alloc enough memory
           to store the coeff table for a single channel. */

        start = avio_rl32(pb);

        if ((ret = ff_get_extradata(codec, s->pb, 32)) < 0)
            return ret;
        if (pb->seekable)
            st->duration = av_get_audio_frame_duration(codec, avio_size(pb) - start);
        break;
    case AV_CODEC_ID_ADPCM_THP:
        codec->block_align = 8 * codec->channels;
        avio_skip(s->pb, 0x1A4 - avio_tell(s->pb));

        if ((ret = ff_alloc_extradata(st->codec, 32 * st->codec->channels)) < 0)
            return ret;

        for (i = 0; i < st->codec->channels; i++) {
            avio_read(s->pb, st->codec->extradata + 32 * i, 32);
            avio_skip(s->pb, 8);
        }
        if (pb->seekable)
            st->duration = (avio_size(pb) - start) / (8 * st->codec->channels) * 14;
        break;
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16BE:
        if (version != 4)
            start = avio_rl32(pb);

        if (pb->seekable)
            st->duration = (avio_size(pb) - start) / 2 / codec->channels;
        break;
    }

    avio_skip(pb, start - avio_tell(pb));
    if (codec->codec_id == AV_CODEC_ID_XMA2) {
        avio_skip(pb, avio_rb32(pb) + avio_rb32(pb));
        st->duration = avio_rb32(pb);
    }

    avpriv_set_pts_info(st, 64, 1, codec->sample_rate);

    return 0;
}

static int rsd_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecContext *codec = s->streams[0]->codec;
    int ret, size = 1024;
    int64_t pos;

    if (avio_feof(s->pb))
        return AVERROR_EOF;

    pos = avio_tell(s->pb);
    if (codec->codec_id == AV_CODEC_ID_ADPCM_IMA_RAD ||
        codec->codec_id == AV_CODEC_ID_ADPCM_PSX     ||
        codec->codec_id == AV_CODEC_ID_ADPCM_IMA_WAV ||
        codec->codec_id == AV_CODEC_ID_XMA2) {
        ret = av_get_packet(s->pb, pkt, codec->block_align);
    } else if (codec->codec_tag == MKTAG('W','A','D','P') &&
               codec->channels > 1) {
        int i, ch;

        ret = av_new_packet(pkt, codec->block_align);
        if (ret < 0)
            return ret;
        for (i = 0; i < 4; i++) {
            for (ch = 0; ch < codec->channels; ch++) {
                pkt->data[ch * 8 + i * 2 + 0] = avio_r8(s->pb);
                pkt->data[ch * 8 + i * 2 + 1] = avio_r8(s->pb);
            }
        }
        ret = 0;
    } else {
        ret = av_get_packet(s->pb, pkt, size);
    }

    if (codec->codec_id == AV_CODEC_ID_XMA2 && pkt->size >= 1)
        pkt->duration = (pkt->data[0] >> 2) * 512;

    pkt->pos = pos;
    pkt->stream_index = 0;

    return ret;
}

AVInputFormat ff_rsd_demuxer = {
    .name           =   "rsd",
    .long_name      =   NULL_IF_CONFIG_SMALL("GameCube RSD"),
    .read_probe     =   rsd_probe,
    .read_header    =   rsd_read_header,
    .read_packet    =   rsd_read_packet,
    .extensions     =   "rsd",
    .codec_tag      =   (const AVCodecTag* const []){rsd_tags, 0},
    .flags          =   AVFMT_GENERIC_INDEX,
};
