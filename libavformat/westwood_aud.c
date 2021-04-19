/*
 * Westwood Studios AUD Format Demuxer
 * Copyright (c) 2003 The FFmpeg project
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
 * Westwood Studios AUD file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the Westwood file formats, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *   http://www.geocities.com/SiliconValley/8682/aud3.txt
 *
 * Implementation note: There is no definite file signature for AUD files.
 * The demuxer uses a probabilistic strategy for content detection. This
 * entails performing sanity checks on certain header values in order to
 * qualify a file. Refer to wsaud_probe() for the precise parameters.
 */

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

#define AUD_HEADER_SIZE 12
#define AUD_CHUNK_PREAMBLE_SIZE 8
#define AUD_CHUNK_SIGNATURE 0x0000DEAF

static int wsaud_probe(const AVProbeData *p)
{
    int field;

    /* Probabilistic content detection strategy: There is no file signature
     * so perform sanity checks on various header parameters:
     *   8000 <= sample rate (16 bits) <= 48000  ==> 40001 acceptable numbers
     *   flags <= 0x03 (2 LSBs are used)         ==> 4 acceptable numbers
     *   compression type (8 bits) = 1 or 99     ==> 2 acceptable numbers
     *   first audio chunk signature (32 bits)   ==> 1 acceptable number
     * The number space contains 2^64 numbers. There are 40001 * 4 * 2 * 1 =
     * 320008 acceptable number combinations.
     */

    if (p->buf_size < AUD_HEADER_SIZE + AUD_CHUNK_PREAMBLE_SIZE)
        return 0;

    /* check sample rate */
    field = AV_RL16(&p->buf[0]);
    if ((field < 8000) || (field > 48000))
        return 0;

    /* enforce the rule that the top 6 bits of this flags field are reserved (0);
     * this might not be true, but enforce it until deemed unnecessary */
    if (p->buf[10] & 0xFC)
        return 0;

    if (p->buf[11] != 99 && p->buf[11] != 1)
        return 0;

    /* read ahead to the first audio chunk and validate the first header signature */
    if (AV_RL32(&p->buf[16]) != AUD_CHUNK_SIGNATURE)
        return 0;

    /* return 1/2 certainty since this file check is a little sketchy */
    return AVPROBE_SCORE_EXTENSION;
}

static int wsaud_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *st;
    unsigned char header[AUD_HEADER_SIZE];
    int sample_rate, channels, codec;

    if (avio_read(pb, header, AUD_HEADER_SIZE) != AUD_HEADER_SIZE)
        return AVERROR(EIO);

    sample_rate = AV_RL16(&header[0]);
    channels    = (header[10] & 0x1) + 1;
    codec       = header[11];

    /* initialize the audio decoder stream */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    switch (codec) {
    case  1:
        if (channels != 1) {
            avpriv_request_sample(s, "Stereo WS-SND1");
            return AVERROR_PATCHWELCOME;
        }
        st->codecpar->codec_id = AV_CODEC_ID_WESTWOOD_SND1;
        break;
    case 99:
        st->codecpar->codec_id = AV_CODEC_ID_ADPCM_IMA_WS;
        st->codecpar->bits_per_coded_sample = 4;
        st->codecpar->bit_rate = channels * sample_rate * 4;
        break;
    default:
        avpriv_request_sample(s, "Unknown codec: %d", codec);
        return AVERROR_PATCHWELCOME;
    }
    avpriv_set_pts_info(st, 64, 1, sample_rate);
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->channels    = channels;
    st->codecpar->channel_layout = channels == 1 ? AV_CH_LAYOUT_MONO :
                                                   AV_CH_LAYOUT_STEREO;
    st->codecpar->sample_rate = sample_rate;

    return 0;
}

static int wsaud_read_packet(AVFormatContext *s,
                             AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    unsigned char preamble[AUD_CHUNK_PREAMBLE_SIZE];
    unsigned int chunk_size;
    int ret = 0;
    AVStream *st = s->streams[0];

    if (avio_read(pb, preamble, AUD_CHUNK_PREAMBLE_SIZE) !=
        AUD_CHUNK_PREAMBLE_SIZE)
        return AVERROR(EIO);

    /* validate the chunk */
    if (AV_RL32(&preamble[4]) != AUD_CHUNK_SIGNATURE)
        return AVERROR_INVALIDDATA;

    chunk_size = AV_RL16(&preamble[0]);

    if (st->codecpar->codec_id == AV_CODEC_ID_WESTWOOD_SND1) {
        /* For Westwood SND1 audio we need to add the output size and input
           size to the start of the packet to match what is in VQA.
           Specifically, this is needed to signal when a packet should be
           decoding as raw 8-bit pcm or variable-size ADPCM. */
        int out_size = AV_RL16(&preamble[2]);
        if ((ret = av_new_packet(pkt, chunk_size + 4)) < 0)
            return ret;
        if ((ret = avio_read(pb, &pkt->data[4], chunk_size)) != chunk_size)
            return ret < 0 ? ret : AVERROR(EIO);
        AV_WL16(&pkt->data[0], out_size);
        AV_WL16(&pkt->data[2], chunk_size);

        pkt->duration = out_size;
    } else {
        ret = av_get_packet(pb, pkt, chunk_size);
        if (ret != chunk_size)
            return AVERROR(EIO);

        if (st->codecpar->channels <= 0) {
            av_log(s, AV_LOG_ERROR, "invalid number of channels %d\n",
                   st->codecpar->channels);
            return AVERROR_INVALIDDATA;
        }

        /* 2 samples/byte, 1 or 2 samples per frame depending on stereo */
        pkt->duration = (chunk_size * 2) / st->codecpar->channels;
    }
    pkt->stream_index = st->index;

    return ret;
}

const AVInputFormat ff_wsaud_demuxer = {
    .name           = "wsaud",
    .long_name      = NULL_IF_CONFIG_SMALL("Westwood Studios audio"),
    .read_probe     = wsaud_probe,
    .read_header    = wsaud_read_header,
    .read_packet    = wsaud_read_packet,
};
