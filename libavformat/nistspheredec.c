/*
 * NIST Sphere demuxer
 * Copyright (c) 2012 Paul B Mahol
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

#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "pcm.h"

static int nist_probe(const AVProbeData *p)
{
    if (AV_RL64(p->buf) == AV_RL64("NIST_1A\x0a"))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int nist_read_header(AVFormatContext *s)
{
    char buffer[256], coding[32] = "pcm", format[32] = "01";
    int bps = 0, be = 0;
    int32_t header_size = -1;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;

    ff_get_line(s->pb, buffer, sizeof(buffer));
    ff_get_line(s->pb, buffer, sizeof(buffer));
    sscanf(buffer, "%"SCNd32, &header_size);
    if (header_size <= 0)
        return AVERROR_INVALIDDATA;

    while (!avio_feof(s->pb)) {
        ff_get_line(s->pb, buffer, sizeof(buffer));

        if (avio_tell(s->pb) >= header_size)
            return AVERROR_INVALIDDATA;

        if (!memcmp(buffer, "end_head", 8)) {
            if (!st->codecpar->bits_per_coded_sample)
                st->codecpar->bits_per_coded_sample = bps << 3;

            if (!av_strcasecmp(coding, "pcm")) {
                if (st->codecpar->codec_id == AV_CODEC_ID_NONE)
                    st->codecpar->codec_id = ff_get_pcm_codec_id(st->codecpar->bits_per_coded_sample,
                                                              0, be, 0xFFFF);
            } else if (!av_strcasecmp(coding, "alaw")) {
                st->codecpar->codec_id = AV_CODEC_ID_PCM_ALAW;
            } else if (!av_strcasecmp(coding, "ulaw") ||
                       !av_strcasecmp(coding, "mu-law")) {
                st->codecpar->codec_id = AV_CODEC_ID_PCM_MULAW;
            } else if (!av_strncasecmp(coding, "pcm,embedded-shorten", 20)) {
                st->codecpar->codec_id = AV_CODEC_ID_SHORTEN;
                if (ff_alloc_extradata(st->codecpar, 1))
                    st->codecpar->extradata[0] = 1;
            } else {
                avpriv_request_sample(s, "coding %s", coding);
            }

            avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

            st->codecpar->block_align = st->codecpar->bits_per_coded_sample *
                                        st->codecpar->ch_layout.nb_channels / 8;

            if (avio_tell(s->pb) > header_size)
                return AVERROR_INVALIDDATA;

            avio_skip(s->pb, header_size - avio_tell(s->pb));

            return 0;
        } else if (!memcmp(buffer, "channel_count", 13)) {
            sscanf(buffer, "%*s %*s %u", &st->codecpar->ch_layout.nb_channels);
            if (st->codecpar->ch_layout.nb_channels <= 0 || st->codecpar->ch_layout.nb_channels > INT16_MAX)
                return AVERROR_INVALIDDATA;
        } else if (!memcmp(buffer, "sample_byte_format", 18)) {
            sscanf(buffer, "%*s %*s %31s", format);

            if (!av_strcasecmp(format, "01")) {
                be = 0;
            } else if (!av_strcasecmp(format, "10")) {
                be = 1;
            } else if (!av_strcasecmp(format, "mu-law")) {
                st->codecpar->codec_id = AV_CODEC_ID_PCM_MULAW;
            } else if (av_strcasecmp(format, "1")) {
                avpriv_request_sample(s, "sample byte format %s", format);
                return AVERROR_PATCHWELCOME;
            }
        } else if (!memcmp(buffer, "sample_coding", 13)) {
            sscanf(buffer, "%*s %*s %31s", coding);
        } else if (!memcmp(buffer, "sample_count", 12)) {
            sscanf(buffer, "%*s %*s %"SCNd64, &st->duration);
        } else if (!memcmp(buffer, "sample_n_bytes", 14)) {
            sscanf(buffer, "%*s %*s %d", &bps);
            if (bps > INT16_MAX/8U)
                return AVERROR_INVALIDDATA;
        } else if (!memcmp(buffer, "sample_rate", 11)) {
            sscanf(buffer, "%*s %*s %d", &st->codecpar->sample_rate);
        } else if (!memcmp(buffer, "sample_sig_bits", 15)) {
            sscanf(buffer, "%*s %*s %d", &st->codecpar->bits_per_coded_sample);
            if (st->codecpar->bits_per_coded_sample <= 0 || st->codecpar->bits_per_coded_sample > INT16_MAX)
                return AVERROR_INVALIDDATA;
        } else {
            char key[32], value[32];
            if (sscanf(buffer, "%31s %*s %31s", key, value) == 2) {
                av_dict_set(&s->metadata, key, value, AV_DICT_APPEND);
            } else {
                av_log(s, AV_LOG_ERROR, "Failed to parse '%s' as metadata\n", buffer);
            }
        }
    }

    return AVERROR_EOF;
}

const FFInputFormat ff_nistsphere_demuxer = {
    .p.name         = "nistsphere",
    .p.long_name    = NULL_IF_CONFIG_SMALL("NIST SPeech HEader REsources"),
    .p.extensions   = "nist,sph",
    .p.flags        = AVFMT_GENERIC_INDEX,
    .read_probe     = nist_probe,
    .read_header    = nist_read_header,
    .read_packet    = ff_pcm_read_packet,
    .read_seek      = ff_pcm_read_seek,
};
