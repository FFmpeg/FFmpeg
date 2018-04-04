/*
 * Vivo stream demuxer
 * Copyright (c) 2009 Daniel Verkamp <daniel at drv.nu>
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
 * @file
 * @brief Vivo stream demuxer
 * @author Daniel Verkamp <daniel at drv.nu>
 * @sa http://wiki.multimedia.cx/index.php?title=Vivo
 */

#include "libavutil/parseutils.h"
#include "avformat.h"
#include "internal.h"

typedef struct VivoContext {
    int version;

    int type;
    int sequence;
    int length;
    int duration;

    uint8_t  text[1024 + 1];
} VivoContext;

static int vivo_probe(const AVProbeData *p)
{
    const unsigned char *buf = p->buf;
    unsigned c, length = 0;

    // stream must start with packet of type 0 and sequence number 0
    if (*buf++ != 0)
        return 0;

    // read at most 2 bytes of coded length
    c = *buf++;
    length = c & 0x7F;
    if (c & 0x80) {
        c = *buf++;
        length = (length << 7) | (c & 0x7F);
    }
    if (c & 0x80 || length > 1024 || length < 21)
        return 0;

    buf += 2;
    if (memcmp(buf, "Version:Vivo/", 13))
        return 0;
    buf += 13;

    if (*buf < '0' || *buf > '2')
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int vivo_get_packet_header(AVFormatContext *s)
{
    VivoContext *vivo = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned c, get_length = 0;

    if (avio_feof(pb))
        return AVERROR_EOF;

    c = avio_r8(pb);
    if (c == 0x82) {
        get_length = 1;
        c = avio_r8(pb);
    }

    vivo->type     = c >> 4;
    vivo->sequence = c & 0xF;

    switch (vivo->type) {
    case 0:   get_length =   1; break;
    case 1: vivo->length = 128; break;
    case 2:   get_length =   1; break;
    case 3: vivo->length =  40; break;
    case 4: vivo->length =  24; break;
    default:
        av_log(s, AV_LOG_ERROR, "unknown packet type %d\n", vivo->type);
        return AVERROR_INVALIDDATA;
    }

    if (get_length) {
        c = avio_r8(pb);
        vivo->length = c & 0x7F;
        if (c & 0x80) {
            c = avio_r8(pb);
            vivo->length = (vivo->length << 7) | (c & 0x7F);

            if (c & 0x80) {
                av_log(s, AV_LOG_ERROR, "coded length is more than two bytes\n");
                return AVERROR_INVALIDDATA;
            }
        }
    }

    return 0;
}

static int vivo_read_header(AVFormatContext *s)
{
    VivoContext *vivo = s->priv_data;
    AVRational fps = { 1, 25};
    AVStream *ast, *vst;
    unsigned char *line, *line_end, *key, *value;
    long value_int;
    int ret, value_used;
    int64_t duration = 0;
    char *end_value;

    vst = avformat_new_stream(s, NULL);
    ast = avformat_new_stream(s, NULL);
    if (!ast || !vst)
        return AVERROR(ENOMEM);

    ast->codecpar->sample_rate = 8000;

    while (1) {
        if ((ret = vivo_get_packet_header(s)) < 0)
            return ret;

        // done reading all text header packets?
        if (vivo->sequence || vivo->type)
            break;

        if (vivo->length <= 1024) {
            avio_read(s->pb, vivo->text, vivo->length);
            vivo->text[vivo->length] = 0;
        } else {
            av_log(s, AV_LOG_WARNING, "too big header, skipping\n");
            avio_skip(s->pb, vivo->length);
            continue;
        }

        line = vivo->text;
        while (*line) {
            line_end = strstr(line, "\r\n");
            if (!line_end)
                break;

            *line_end = 0;
            key = line;
            line = line_end + 2; // skip \r\n

            if (line_end == key) // skip blank lines
                continue;

            value = strchr(key, ':');
            if (!value) {
                av_log(s, AV_LOG_WARNING, "missing colon in key:value pair '%s'\n",
                       key);
                continue;
            }

            *value++ = 0;

            av_log(s, AV_LOG_DEBUG, "header: '%s' = '%s'\n", key, value);

            value_int = strtol(value, &end_value, 10);
            value_used = 0;
            if (*end_value == 0) { // valid integer
                av_log(s, AV_LOG_DEBUG, "got a valid integer (%ld)\n", value_int);
                value_used = 1;
                if (!strcmp(key, "Duration")) {
                    duration = value_int;
                } else if (!strcmp(key, "Width")) {
                    vst->codecpar->width = value_int;
                } else if (!strcmp(key, "Height")) {
                    vst->codecpar->height = value_int;
                } else if (!strcmp(key, "TimeUnitNumerator")) {
                    fps.num = value_int / 1000;
                } else if (!strcmp(key, "TimeUnitDenominator")) {
                    fps.den = value_int;
                } else if (!strcmp(key, "SamplingFrequency")) {
                    ast->codecpar->sample_rate = value_int;
                } else if (!strcmp(key, "NominalBitrate")) {
                } else if (!strcmp(key, "Length")) {
                    // size of file
                } else {
                    value_used = 0;
                }
            }

            if (!strcmp(key, "Version")) {
                if (sscanf(value, "Vivo/%d.", &vivo->version) != 1)
                    return AVERROR_INVALIDDATA;
                value_used = 1;
            } else if (!strcmp(key, "FPS")) {
                AVRational tmp;

                value_used = 1;
                if (!av_parse_ratio(&tmp, value, 10000, AV_LOG_WARNING, s))
                    fps = av_inv_q(tmp);
            }

            if (!value_used)
                av_dict_set(&s->metadata, key, value, 0);
        }
    }

    avpriv_set_pts_info(ast, 64, 1, ast->codecpar->sample_rate);
    avpriv_set_pts_info(vst, 64, fps.num, fps.den);
    if (duration)
        s->duration = av_rescale(duration, 1000, 1);

    vst->start_time        = 0;
    vst->codecpar->codec_tag  = 0;
    vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;

    if (vivo->version == 1) {
        vst->codecpar->codec_id = AV_CODEC_ID_H263;
        ast->codecpar->codec_id = AV_CODEC_ID_G723_1;
        ast->codecpar->bits_per_coded_sample = 8;
        ast->codecpar->block_align = 24;
        ast->codecpar->bit_rate = 6400;
    } else {
        ast->codecpar->codec_id = AV_CODEC_ID_SIREN;
        ast->codecpar->bits_per_coded_sample = 16;
        ast->codecpar->block_align = 40;
        ast->codecpar->bit_rate = 6400;
        vivo->duration = 320;
    }

    ast->start_time        = 0;
    ast->codecpar->codec_tag  = 0;
    ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    ast->codecpar->channels = 1;

    return 0;
}

static int vivo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    VivoContext *vivo = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned old_sequence = vivo->sequence, old_type = vivo->type;
    int stream_index, duration, ret = 0;

restart:

    if (avio_feof(pb))
        return AVERROR_EOF;

    switch (vivo->type) {
    case 0:
        avio_skip(pb, vivo->length);
        if ((ret = vivo_get_packet_header(s)) < 0)
            return ret;
        goto restart;
    case 1:
    case 2: // video
        stream_index = 0;
        duration = 1;
        break;
    case 3:
    case 4: // audio
        stream_index = 1;
        duration = vivo->duration;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "unknown packet type %d\n", vivo->type);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = av_get_packet(pb, pkt, vivo->length)) < 0)
        return ret;

    // get next packet header
    if ((ret = vivo_get_packet_header(s)) < 0)
        return ret;

    while (vivo->sequence == old_sequence &&
           (((vivo->type - 1) >> 1) == ((old_type - 1) >> 1))) {
        if (avio_feof(pb)) {
            return AVERROR_EOF;
        }

        if ((ret = av_append_packet(pb, pkt, vivo->length)) < 0)
            return ret;

        // get next packet header
        if ((ret = vivo_get_packet_header(s)) < 0)
            return ret;
    }

    pkt->stream_index = stream_index;
    pkt->duration = duration;

    return ret;
}

AVInputFormat ff_vivo_demuxer = {
    .name           = "vivo",
    .long_name      = NULL_IF_CONFIG_SMALL("Vivo"),
    .priv_data_size = sizeof(VivoContext),
    .read_probe     = vivo_probe,
    .read_header    = vivo_read_header,
    .read_packet    = vivo_read_packet,
    .extensions     = "viv",
};
