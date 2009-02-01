/*
 * Sony OpenMG (OMA) demuxer
 *
 * Copyright (c) 2008 Maxim Poliakovski
 *               2008 Benjamin Larsson
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

/**
 * @file libavformat/oma.c
 * This is a demuxer for Sony OpenMG Music files
 *
 * Known file extensions: ".oma", "aa3"
 * The format of such files consists of three parts:
 * - "ea3" header carrying overall info and metadata.
 * - "EA3" header is a Sony-specific header containing information about
 *   the OpenMG file: codec type (usually ATRAC, can also be MP3 or WMA),
 *   codec specific info (packet size, sample rate, channels and so on)
 *   and DRM related info (file encryption, content id).
 * - Sound data organized in packets follow the EA3 header
 *   (can be encrypted using the Sony DRM!).
 *
 * LIMITATIONS: This version supports only plain (unencrypted) OMA files.
 * If any DRM-protected (encrypted) file is encountered you will get the
 * corresponding error message. Try to remove the encryption using any
 * Sony software (for example SonicStage).
 * CODEC SUPPORT: Only ATRAC3 codec is currently supported!
 */

#include "avformat.h"
#include "libavutil/intreadwrite.h"
#include "raw.h"
#include "riff.h"

#define EA3_HEADER_SIZE 96

enum {
    OMA_CODECID_ATRAC3  = 0,
    OMA_CODECID_ATRAC3P = 1,
    OMA_CODECID_MP3     = 3,
    OMA_CODECID_LPCM    = 4,
    OMA_CODECID_WMA     = 5,
};

static const AVCodecTag codec_oma_tags[] = {
    { CODEC_ID_ATRAC3,  OMA_CODECID_ATRAC3 },
    { CODEC_ID_ATRAC3P, OMA_CODECID_ATRAC3P },
    { CODEC_ID_MP3,     OMA_CODECID_MP3 },
};

static int oma_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    static const uint16_t srate_tab[6] = {320,441,480,882,960,0};
    int     ret, ea3_taglen, EA3_pos, framesize, jsflag, samplerate;
    uint32_t codec_params;
    int16_t eid;
    uint8_t buf[EA3_HEADER_SIZE];
    uint8_t *edata;
    AVStream *st;

    ret = get_buffer(s->pb, buf, 10);
    if (ret != 10)
        return -1;

    ea3_taglen = ((buf[6] & 0x7f) << 21) | ((buf[7] & 0x7f) << 14) | ((buf[8] & 0x7f) << 7) | (buf[9] & 0x7f);

    EA3_pos = ea3_taglen + 10;
    if (buf[5] & 0x10)
        EA3_pos += 10;

    url_fseek(s->pb, EA3_pos, SEEK_SET);
    ret = get_buffer(s->pb, buf, EA3_HEADER_SIZE);
    if (ret != EA3_HEADER_SIZE)
        return -1;

    if (memcmp(buf, (const uint8_t[]){'E', 'A', '3'},3) || buf[4] != 0 || buf[5] != EA3_HEADER_SIZE) {
        av_log(s, AV_LOG_ERROR, "Couldn't find the EA3 header !\n");
        return -1;
    }

    eid = AV_RB16(&buf[6]);
    if (eid != -1 && eid != -128) {
        av_log(s, AV_LOG_ERROR, "Encrypted file! Eid: %d\n", eid);
        return -1;
    }

    codec_params = AV_RB24(&buf[33]);

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->start_time = 0;
    st->codec->codec_type  = CODEC_TYPE_AUDIO;
    st->codec->codec_tag   = buf[32];
    st->codec->codec_id    = codec_get_id(codec_oma_tags, st->codec->codec_tag);

    switch (buf[32]) {
        case OMA_CODECID_ATRAC3:
            samplerate = srate_tab[(codec_params >> 13) & 7]*100;
            if (samplerate != 44100)
                av_log(s, AV_LOG_ERROR, "Unsupported sample rate, send sample file to developers: %d\n", samplerate);

            framesize = (codec_params & 0x3FF) * 8;
            jsflag = (codec_params >> 17) & 1; /* get stereo coding mode, 1 for joint-stereo */
            st->codec->channels    = 2;
            st->codec->sample_rate = samplerate;
            st->codec->bit_rate    = st->codec->sample_rate * framesize * 8 / 1024;

            /* fake the atrac3 extradata (wav format, makes stream copy to wav work) */
            st->codec->extradata_size = 14;
            edata = av_mallocz(14 + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!edata)
                return AVERROR(ENOMEM);

            st->codec->extradata = edata;
            AV_WL16(&edata[0],  1);             // always 1
            AV_WL32(&edata[2],  samplerate);    // samples rate
            AV_WL16(&edata[6],  jsflag);        // coding mode
            AV_WL16(&edata[8],  jsflag);        // coding mode
            AV_WL16(&edata[10], 1);             // always 1
            // AV_WL16(&edata[12], 0);          // always 0

            av_set_pts_info(st, 64, 1, st->codec->sample_rate);
            break;
        case OMA_CODECID_ATRAC3P:
            st->codec->channels = (codec_params >> 10) & 7;
            framesize = ((codec_params & 0x3FF) * 8) + 8;
            st->codec->sample_rate = srate_tab[(codec_params >> 13) & 7]*100;
            st->codec->bit_rate    = st->codec->sample_rate * framesize * 8 / 1024;
            av_set_pts_info(st, 64, 1, st->codec->sample_rate);
            av_log(s, AV_LOG_ERROR, "Unsupported codec ATRAC3+!\n");
            break;
        case OMA_CODECID_MP3:
            st->need_parsing = AVSTREAM_PARSE_FULL;
            framesize = 1024;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "Unsupported codec %d!\n",buf[32]);
            return -1;
            break;
    }

    st->codec->block_align = framesize;
    url_fseek(s->pb, EA3_pos + EA3_HEADER_SIZE, SEEK_SET);

    return 0;
}


static int oma_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret = av_get_packet(s->pb, pkt, s->streams[0]->codec->block_align);

    pkt->stream_index = 0;
    if (ret <= 0)
        return AVERROR(EIO);

    return ret;
}

static int oma_read_probe(AVProbeData *p)
{
    if (!memcmp(p->buf, (const uint8_t[]){'e', 'a', '3', 3, 0},5))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}


AVInputFormat oma_demuxer = {
    "oma",
    NULL_IF_CONFIG_SMALL("Sony OpenMG audio"),
    0,
    oma_read_probe,
    oma_read_header,
    oma_read_packet,
    0,
    pcm_read_seek,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "oma,aa3",
    .codec_tag= (const AVCodecTag* const []){codec_oma_tags, 0},
};

