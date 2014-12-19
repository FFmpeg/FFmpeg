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
    { AV_CODEC_ID_ADPCM_THP,       MKTAG('G','A','D','P') },
    { AV_CODEC_ID_ADPCM_IMA_RAD,   MKTAG('R','A','D','P') },
    { AV_CODEC_ID_PCM_S16BE,       MKTAG('P','C','M','B') },
    { AV_CODEC_ID_PCM_S16LE,       MKTAG('P','C','M',' ') },
    { AV_CODEC_ID_NONE, 0 },
};

static const uint32_t rsd_unsupported_tags[] = {
    MKTAG('O','G','G',' '),
    MKTAG('V','A','G',' '),
    MKTAG('W','A','D','P'),
    MKTAG('X','A','D','P'),
    MKTAG('X','M','A',' '),
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
    int i, version, start = 0x800;
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
    case AV_CODEC_ID_ADPCM_IMA_RAD:
        codec->block_align = 20 * codec->channels;
        if (pb->seekable)
            st->duration = av_get_audio_frame_duration(codec, avio_size(pb) - start);
        break;
    case AV_CODEC_ID_ADPCM_THP:
        /* RSD3GADP is mono, so only alloc enough memory
           to store the coeff table for a single channel. */

        start = avio_rl32(pb);

        if (ff_get_extradata(codec, s->pb, 32) < 0)
            return AVERROR(ENOMEM);

        for (i = 0; i < 16; i++)
            AV_WB16(codec->extradata + i * 2, AV_RL16(codec->extradata + i * 2));

        if (pb->seekable)
            st->duration = (avio_size(pb) - start) / 8 * 14;
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

    avpriv_set_pts_info(st, 64, 1, codec->sample_rate);

    return 0;
}

static int rsd_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecContext *codec = s->streams[0]->codec;
    int ret, size = 1024;

    if (avio_feof(s->pb))
        return AVERROR_EOF;

    if (codec->codec_id == AV_CODEC_ID_ADPCM_IMA_RAD)
        ret = av_get_packet(s->pb, pkt, codec->block_align);
    else
        ret = av_get_packet(s->pb, pkt, size);

    if (ret != size) {
        if (ret < 0) {
            av_free_packet(pkt);
            return ret;
        }
        av_shrink_packet(pkt, ret);
    }
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
};
