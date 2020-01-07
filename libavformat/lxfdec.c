/*
 * LXF demuxer
 * Copyright (c) 2010 Tomas Härdin
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

#include <inttypes.h>

#include "libavutil/intreadwrite.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "internal.h"

#define LXF_MAX_PACKET_HEADER_SIZE 256
#define LXF_HEADER_DATA_SIZE    120
#define LXF_IDENT               "LEITCH\0"
#define LXF_IDENT_LENGTH        8
#define LXF_SAMPLERATE          48000

static const AVCodecTag lxf_tags[] = {
    { AV_CODEC_ID_MJPEG,       0 },
    { AV_CODEC_ID_MPEG1VIDEO,  1 },
    { AV_CODEC_ID_MPEG2VIDEO,  2 },    //MpMl, 4:2:0
    { AV_CODEC_ID_MPEG2VIDEO,  3 },    //MpPl, 4:2:2
    { AV_CODEC_ID_DVVIDEO,     4 },    //DV25
    { AV_CODEC_ID_DVVIDEO,     5 },    //DVCPRO
    { AV_CODEC_ID_DVVIDEO,     6 },    //DVCPRO50
    { AV_CODEC_ID_RAWVIDEO,    7 },    //AV_PIX_FMT_ARGB, where alpha is used for chroma keying
    { AV_CODEC_ID_RAWVIDEO,    8 },    //16-bit chroma key
    { AV_CODEC_ID_MPEG2VIDEO,  9 },    //4:2:2 CBP ("Constrained Bytes per Gop")
    { AV_CODEC_ID_NONE,        0 },
};

typedef struct LXFDemuxContext {
    int channels;                       ///< number of audio channels. zero means no audio
    int frame_number;                   ///< current video frame
    uint32_t video_format, packet_type, extended_size;
} LXFDemuxContext;

static int lxf_probe(const AVProbeData *p)
{
    if (!memcmp(p->buf, LXF_IDENT, LXF_IDENT_LENGTH))
        return AVPROBE_SCORE_MAX;

    return 0;
}

/**
 * Verify the checksum of an LXF packet header
 *
 * @param[in] header the packet header to check
 * @return zero if the checksum is OK, non-zero otherwise
 */
static int check_checksum(const uint8_t *header, int size)
{
    int x;
    uint32_t sum = 0;

    for (x = 0; x < size; x += 4)
        sum += AV_RL32(&header[x]);

    return sum;
}

/**
 * Read input until we find the next ident. If found, copy it to the header buffer
 *
 * @param[out] header where to copy the ident to
 * @return 0 if an ident was found, < 0 on I/O error
 */
static int lxf_sync(AVFormatContext *s, uint8_t *header)
{
    uint8_t buf[LXF_IDENT_LENGTH];
    int ret;

    if ((ret = avio_read(s->pb, buf, LXF_IDENT_LENGTH)) != LXF_IDENT_LENGTH)
        return ret < 0 ? ret : AVERROR_EOF;

    while (memcmp(buf, LXF_IDENT, LXF_IDENT_LENGTH)) {
        if (avio_feof(s->pb))
            return AVERROR_EOF;

        memmove(buf, &buf[1], LXF_IDENT_LENGTH-1);
        buf[LXF_IDENT_LENGTH-1] = avio_r8(s->pb);
    }

    memcpy(header, LXF_IDENT, LXF_IDENT_LENGTH);

    return 0;
}

/**
 * Read and checksum the next packet header
 *
 * @return the size of the payload following the header or < 0 on failure
 */
static int get_packet_header(AVFormatContext *s)
{
    LXFDemuxContext *lxf = s->priv_data;
    AVIOContext   *pb  = s->pb;
    int track_size, samples, ret;
    uint32_t version, audio_format, header_size, channels, tmp;
    AVStream *st;
    uint8_t header[LXF_MAX_PACKET_HEADER_SIZE];
    const uint8_t *p = header + LXF_IDENT_LENGTH;

    //find and read the ident
    if ((ret = lxf_sync(s, header)) < 0)
        return ret;

    ret = avio_read(pb, header + LXF_IDENT_LENGTH, 8);
    if (ret != 8)
        return ret < 0 ? ret : AVERROR_EOF;

    version     = bytestream_get_le32(&p);
    header_size = bytestream_get_le32(&p);
    if (version > 1)
        avpriv_request_sample(s, "Format version %"PRIu32, version);

    if (header_size < (version ? 72 : 60) ||
        header_size > LXF_MAX_PACKET_HEADER_SIZE ||
        (header_size & 3)) {
        av_log(s, AV_LOG_ERROR, "Invalid header size 0x%"PRIx32"\n", header_size);
        return AVERROR_INVALIDDATA;
    }

    //read the rest of the packet header
    if ((ret = avio_read(pb, header + (p - header),
                          header_size - (p - header))) !=
                          header_size - (p - header))
        return ret < 0 ? ret : AVERROR_EOF;

    if (check_checksum(header, header_size))
        av_log(s, AV_LOG_ERROR, "checksum error\n");

    lxf->packet_type = bytestream_get_le32(&p);
    p += version ? 20 : 12;

    lxf->extended_size = 0;
    switch (lxf->packet_type) {
    case 0:
        //video
        lxf->video_format = bytestream_get_le32(&p);
        ret               = bytestream_get_le32(&p);
        //skip VBI data and metadata
        avio_skip(pb, (int64_t)(uint32_t)AV_RL32(p + 4) +
                      (int64_t)(uint32_t)AV_RL32(p + 12));
        break;
    case 1:
        //audio
        if (s->nb_streams < 2) {
            av_log(s, AV_LOG_INFO, "got audio packet, but no audio stream present\n");
            break;
        }

        if (version == 0)
            p += 8;
        audio_format = bytestream_get_le32(&p);
        channels     = bytestream_get_le32(&p);
        track_size   = bytestream_get_le32(&p);

        st = s->streams[1];

        //set codec based on specified audio bitdepth
        //we only support tightly packed 16-, 20-, 24- and 32-bit PCM at the moment
        st->codecpar->bits_per_coded_sample = (audio_format >> 6) & 0x3F;

        if (st->codecpar->bits_per_coded_sample != (audio_format & 0x3F)) {
            avpriv_report_missing_feature(s, "Not tightly packed PCM");
            return AVERROR_PATCHWELCOME;
        }

        switch (st->codecpar->bits_per_coded_sample) {
        case 16: st->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE_PLANAR; break;
        case 20: st->codecpar->codec_id = AV_CODEC_ID_PCM_LXF;   break;
        case 24: st->codecpar->codec_id = AV_CODEC_ID_PCM_S24LE_PLANAR; break;
        case 32: st->codecpar->codec_id = AV_CODEC_ID_PCM_S32LE_PLANAR; break;
        default:
            avpriv_report_missing_feature(s, "PCM not 16-, 20-, 24- or 32-bits");
            return AVERROR_PATCHWELCOME;
        }

        samples = track_size * 8 / st->codecpar->bits_per_coded_sample;

        //use audio packet size to determine video standard
        //for NTSC we have one 8008-sample audio frame per five video frames
        if (samples == LXF_SAMPLERATE * 5005 / 30000) {
            avpriv_set_pts_info(s->streams[0], 64, 1001, 30000);
        } else {
            //assume PAL, but warn if we don't have 1920 samples
            if (samples != LXF_SAMPLERATE / 25)
                av_log(s, AV_LOG_WARNING,
                       "video doesn't seem to be PAL or NTSC. guessing PAL\n");

            avpriv_set_pts_info(s->streams[0], 64, 1, 25);
        }

        //TODO: warning if track mask != (1 << channels) - 1?
        ret = av_popcount(channels) * track_size;

        break;
    default:
        tmp = bytestream_get_le32(&p);
        ret = bytestream_get_le32(&p);
        if (tmp == 1)
            lxf->extended_size = bytestream_get_le32(&p);
        break;
    }

    return ret;
}

static int lxf_read_header(AVFormatContext *s)
{
    LXFDemuxContext *lxf = s->priv_data;
    AVIOContext   *pb  = s->pb;
    uint8_t header_data[LXF_HEADER_DATA_SIZE];
    int ret;
    AVStream *st;
    uint32_t video_params, disk_params;
    uint16_t record_date, expiration_date;

    if ((ret = get_packet_header(s)) < 0)
        return ret;

    if (ret != LXF_HEADER_DATA_SIZE) {
        av_log(s, AV_LOG_ERROR, "expected %d B size header, got %d\n",
               LXF_HEADER_DATA_SIZE, ret);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = avio_read(pb, header_data, LXF_HEADER_DATA_SIZE)) != LXF_HEADER_DATA_SIZE)
        return ret < 0 ? ret : AVERROR_EOF;

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    st->duration          = AV_RL32(&header_data[32]);
    video_params          = AV_RL32(&header_data[40]);
    record_date           = AV_RL16(&header_data[56]);
    expiration_date       = AV_RL16(&header_data[58]);
    disk_params           = AV_RL32(&header_data[116]);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->bit_rate   = 1000000 * ((video_params >> 14) & 0xFF);
    st->codecpar->codec_tag  = video_params & 0xF;
    st->codecpar->codec_id   = ff_codec_get_id(lxf_tags, st->codecpar->codec_tag);
    st->need_parsing         = AVSTREAM_PARSE_HEADERS;

    av_log(s, AV_LOG_DEBUG, "record: %x = %i-%02i-%02i\n",
           record_date, 1900 + (record_date & 0x7F), (record_date >> 7) & 0xF,
           (record_date >> 11) & 0x1F);

    av_log(s, AV_LOG_DEBUG, "expire: %x = %i-%02i-%02i\n",
           expiration_date, 1900 + (expiration_date & 0x7F), (expiration_date >> 7) & 0xF,
           (expiration_date >> 11) & 0x1F);

    if ((video_params >> 22) & 1)
        av_log(s, AV_LOG_WARNING, "VBI data not yet supported\n");

    if ((lxf->channels = 1 << (disk_params >> 4 & 3) + 1)) {
        if (!(st = avformat_new_stream(s, NULL)))
            return AVERROR(ENOMEM);

        st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        st->codecpar->sample_rate = LXF_SAMPLERATE;
        st->codecpar->channels    = lxf->channels;

        avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    }

    avio_skip(s->pb, lxf->extended_size);

    return 0;
}

static int lxf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    LXFDemuxContext *lxf = s->priv_data;
    AVIOContext   *pb  = s->pb;
    uint32_t stream;
    int ret, ret2;

    if ((ret = get_packet_header(s)) < 0)
        return ret;

    stream = lxf->packet_type;

    if (stream > 1) {
        av_log(s, AV_LOG_WARNING,
               "got packet with illegal stream index %"PRIu32"\n", stream);
        return FFERROR_REDO;
    }

    if (stream == 1 && s->nb_streams < 2) {
        av_log(s, AV_LOG_ERROR, "got audio packet without having an audio stream\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret2 = av_new_packet(pkt, ret)) < 0)
        return ret2;

    if ((ret2 = avio_read(pb, pkt->data, ret)) != ret) {
        return ret2 < 0 ? ret2 : AVERROR_EOF;
    }

    pkt->stream_index = stream;

    if (!stream) {
        //picture type (0 = closed I, 1 = open I, 2 = P, 3 = B)
        if (((lxf->video_format >> 22) & 0x3) < 2)
            pkt->flags |= AV_PKT_FLAG_KEY;

        pkt->dts = lxf->frame_number++;
    }

    return ret;
}

AVInputFormat ff_lxf_demuxer = {
    .name           = "lxf",
    .long_name      = NULL_IF_CONFIG_SMALL("VR native stream (LXF)"),
    .priv_data_size = sizeof(LXFDemuxContext),
    .read_probe     = lxf_probe,
    .read_header    = lxf_read_header,
    .read_packet    = lxf_read_packet,
    .codec_tag      = (const AVCodecTag* const []){lxf_tags, 0},
};
