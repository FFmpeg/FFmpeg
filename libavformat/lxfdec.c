/*
 * LXF demuxer
 * Copyright (c) 2010 Tomas Härdin
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "riff.h"

#define LXF_PACKET_HEADER_SIZE  60
#define LXF_HEADER_DATA_SIZE    120
#define LXF_IDENT               "LEITCH\0"
#define LXF_IDENT_LENGTH        8
#define LXF_SAMPLERATE          48000
#define LXF_MAX_AUDIO_PACKET    (8008*15*4) ///< 15-channel 32-bit NTSC audio frame

static const AVCodecTag lxf_tags[] = {
    { CODEC_ID_MJPEG,       0 },
    { CODEC_ID_MPEG1VIDEO,  1 },
    { CODEC_ID_MPEG2VIDEO,  2 },    //MpMl, 4:2:0
    { CODEC_ID_MPEG2VIDEO,  3 },    //MpPl, 4:2:2
    { CODEC_ID_DVVIDEO,     4 },    //DV25
    { CODEC_ID_DVVIDEO,     5 },    //DVCPRO
    { CODEC_ID_DVVIDEO,     6 },    //DVCPRO50
    { CODEC_ID_RAWVIDEO,    7 },    //PIX_FMT_ARGB, where alpha is used for chroma keying
    { CODEC_ID_RAWVIDEO,    8 },    //16-bit chroma key
    { CODEC_ID_MPEG2VIDEO,  9 },    //4:2:2 CBP ("Constrained Bytes per Gop")
    { CODEC_ID_NONE,        0 },
};

typedef struct {
    int channels;                       ///< number of audio channels. zero means no audio
    uint8_t temp[LXF_MAX_AUDIO_PACKET]; ///< temp buffer for de-planarizing the audio data
    int frame_number;                   ///< current video frame
} LXFDemuxContext;

static int lxf_probe(AVProbeData *p)
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
static int check_checksum(const uint8_t *header)
{
    int x;
    uint32_t sum = 0;

    for (x = 0; x < LXF_PACKET_HEADER_SIZE; x += 4)
        sum += AV_RL32(&header[x]);

    return sum;
}

/**
 * Read input until we find the next ident. If found, copy it to the header buffer
 *
 * @param[out] header where to copy the ident to
 * @return 0 if an ident was found, < 0 on I/O error
 */
static int sync(AVFormatContext *s, uint8_t *header)
{
    uint8_t buf[LXF_IDENT_LENGTH];
    int ret;

    if ((ret = avio_read(s->pb, buf, LXF_IDENT_LENGTH)) != LXF_IDENT_LENGTH)
        return ret < 0 ? ret : AVERROR_EOF;

    while (memcmp(buf, LXF_IDENT, LXF_IDENT_LENGTH)) {
        if (s->pb->eof_reached)
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
 * @param[out] header the read packet header
 * @param[out] format context dependent format information
 * @return the size of the payload following the header or < 0 on failure
 */
static int get_packet_header(AVFormatContext *s, uint8_t *header, uint32_t *format)
{
    AVIOContext   *pb  = s->pb;
    int track_size, samples, ret;
    AVStream *st;

    //find and read the ident
    if ((ret = sync(s, header)) < 0)
        return ret;

    //read the rest of the packet header
    if ((ret = avio_read(pb, header + LXF_IDENT_LENGTH,
                          LXF_PACKET_HEADER_SIZE - LXF_IDENT_LENGTH)) !=
                          LXF_PACKET_HEADER_SIZE - LXF_IDENT_LENGTH) {
        return ret < 0 ? ret : AVERROR_EOF;
    }

    if (check_checksum(header))
        av_log(s, AV_LOG_ERROR, "checksum error\n");

    *format = AV_RL32(&header[32]);
    ret     = AV_RL32(&header[36]);

    //type
    switch (AV_RL32(&header[16])) {
    case 0:
        //video
        //skip VBI data and metadata
        avio_skip(pb, (int64_t)(uint32_t)AV_RL32(&header[44]) +
                      (int64_t)(uint32_t)AV_RL32(&header[52]));
        break;
    case 1:
        //audio
        if (!(st = s->streams[1])) {
            av_log(s, AV_LOG_INFO, "got audio packet, but no audio stream present\n");
            break;
        }

        //set codec based on specified audio bitdepth
        //we only support tightly packed 16-, 20-, 24- and 32-bit PCM at the moment
        *format                          = AV_RL32(&header[40]);
        st->codec->bits_per_coded_sample = (*format >> 6) & 0x3F;

        if (st->codec->bits_per_coded_sample != (*format & 0x3F)) {
            av_log(s, AV_LOG_WARNING, "only tightly packed PCM currently supported\n");
            return AVERROR_PATCHWELCOME;
        }

        switch (st->codec->bits_per_coded_sample) {
        case 16: st->codec->codec_id = CODEC_ID_PCM_S16LE; break;
        case 20: st->codec->codec_id = CODEC_ID_PCM_LXF;   break;
        case 24: st->codec->codec_id = CODEC_ID_PCM_S24LE; break;
        case 32: st->codec->codec_id = CODEC_ID_PCM_S32LE; break;
        default:
            av_log(s, AV_LOG_WARNING,
                   "only 16-, 20-, 24- and 32-bit PCM currently supported\n");
            return AVERROR_PATCHWELCOME;
        }

        track_size = AV_RL32(&header[48]);
        samples = track_size * 8 / st->codec->bits_per_coded_sample;

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
        ret = av_popcount(AV_RL32(&header[44])) * track_size;

        break;
    default:
        break;
    }

    return ret;
}

static int lxf_read_header(AVFormatContext *s)
{
    LXFDemuxContext *lxf = s->priv_data;
    AVIOContext   *pb  = s->pb;
    uint8_t header[LXF_PACKET_HEADER_SIZE], header_data[LXF_HEADER_DATA_SIZE];
    int ret;
    AVStream *st;
    uint32_t format, video_params, disk_params;
    uint16_t record_date, expiration_date;

    if ((ret = get_packet_header(s, header, &format)) < 0)
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

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->bit_rate   = 1000000 * ((video_params >> 14) & 0xFF);
    st->codec->codec_tag  = video_params & 0xF;
    st->codec->codec_id   = ff_codec_get_id(lxf_tags, st->codec->codec_tag);

    av_log(s, AV_LOG_DEBUG, "record: %x = %i-%02i-%02i\n",
           record_date, 1900 + (record_date & 0x7F), (record_date >> 7) & 0xF,
           (record_date >> 11) & 0x1F);

    av_log(s, AV_LOG_DEBUG, "expire: %x = %i-%02i-%02i\n",
           expiration_date, 1900 + (expiration_date & 0x7F), (expiration_date >> 7) & 0xF,
           (expiration_date >> 11) & 0x1F);

    if ((video_params >> 22) & 1)
        av_log(s, AV_LOG_WARNING, "VBI data not yet supported\n");

    if ((lxf->channels = (disk_params >> 2) & 0xF)) {
        if (!(st = avformat_new_stream(s, NULL)))
            return AVERROR(ENOMEM);

        st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
        st->codec->sample_rate = LXF_SAMPLERATE;
        st->codec->channels    = lxf->channels;

        avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
    }

    if (format == 1) {
        //skip extended field data
        avio_skip(s->pb, (uint32_t)AV_RL32(&header[40]));
    }

    return 0;
}

/**
 * De-planerize the PCM data in lxf->temp
 * FIXME: remove this once support for planar audio is added to libavcodec
 *
 * @param[out] out where to write the de-planerized data to
 * @param[in] bytes the total size of the PCM data
 */
static void deplanarize(LXFDemuxContext *lxf, AVStream *ast, uint8_t *out, int bytes)
{
    int x, y, z, i, bytes_per_sample = ast->codec->bits_per_coded_sample >> 3;

    for (z = i = 0; z < lxf->channels; z++)
        for (y = 0; y < bytes / bytes_per_sample / lxf->channels; y++)
            for (x = 0; x < bytes_per_sample; x++, i++)
                out[x + bytes_per_sample*(z + y*lxf->channels)] = lxf->temp[i];
}

static int lxf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    LXFDemuxContext *lxf = s->priv_data;
    AVIOContext   *pb  = s->pb;
    uint8_t header[LXF_PACKET_HEADER_SIZE], *buf;
    AVStream *ast = NULL;
    uint32_t stream, format;
    int ret, ret2;

    if ((ret = get_packet_header(s, header, &format)) < 0)
        return ret;

    stream = AV_RL32(&header[16]);

    if (stream > 1) {
        av_log(s, AV_LOG_WARNING, "got packet with illegal stream index %u\n", stream);
        return AVERROR(EAGAIN);
    }

    if (stream == 1 && !(ast = s->streams[1])) {
        av_log(s, AV_LOG_ERROR, "got audio packet without having an audio stream\n");
        return AVERROR_INVALIDDATA;
    }

    //make sure the data fits in the de-planerization buffer
    if (ast && ret > LXF_MAX_AUDIO_PACKET) {
        av_log(s, AV_LOG_ERROR, "audio packet too large (%i > %i)\n",
            ret, LXF_MAX_AUDIO_PACKET);
        return AVERROR_INVALIDDATA;
    }

    if ((ret2 = av_new_packet(pkt, ret)) < 0)
        return ret2;

    //read non-20-bit audio data into lxf->temp so we can deplanarize it
    buf = ast && ast->codec->codec_id != CODEC_ID_PCM_LXF ? lxf->temp : pkt->data;

    if ((ret2 = avio_read(pb, buf, ret)) != ret) {
        av_free_packet(pkt);
        return ret2 < 0 ? ret2 : AVERROR_EOF;
    }

    pkt->stream_index = stream;

    if (ast) {
        if(ast->codec->codec_id != CODEC_ID_PCM_LXF)
            deplanarize(lxf, ast, pkt->data, ret);
    } else {
        //picture type (0 = closed I, 1 = open I, 2 = P, 3 = B)
        if (((format >> 22) & 0x3) < 2)
            pkt->flags |= AV_PKT_FLAG_KEY;

        pkt->dts = lxf->frame_number++;
    }

    return ret;
}

AVInputFormat ff_lxf_demuxer = {
    .name           = "lxf",
    .long_name      = NULL_IF_CONFIG_SMALL("VR native stream format (LXF)"),
    .priv_data_size = sizeof(LXFDemuxContext),
    .read_probe     = lxf_probe,
    .read_header    = lxf_read_header,
    .read_packet    = lxf_read_packet,
    .codec_tag      = (const AVCodecTag* const []){lxf_tags, 0},
};
