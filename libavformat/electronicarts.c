/* Electronic Arts Multimedia File Demuxer
 * Copyright (c) 2004  The ffmpeg Project
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
 * @file electronicarts.c
 * Electronic Arts Multimedia file demuxer (WVE/UV2/etc.)
 * by Robin Kay (komadori at gekkou.co.uk)
 */

#include "avformat.h"

#define SCHl_TAG MKTAG('S', 'C', 'H', 'l')
#define PT00_TAG MKTAG('P', 'T', 0x0, 0x0)
#define SCDl_TAG MKTAG('S', 'C', 'D', 'l')
#define pIQT_TAG MKTAG('p', 'I', 'Q', 'T')
#define SCEl_TAG MKTAG('S', 'C', 'E', 'l')
#define _TAG MKTAG('', '', '', '')

#define EA_SAMPLE_RATE 22050
#define EA_BITS_PER_SAMPLE 16
#define EA_PREAMBLE_SIZE 8

typedef struct EaDemuxContext {
    int width;
    int height;
    int video_stream_index;
    int track_count;

    int audio_stream_index;
    int audio_frame_counter;

    int64_t audio_pts;
    int64_t video_pts;
    int video_pts_inc;
    float fps;

    int num_channels;
    int num_samples;
    int compression_type;
} EaDemuxContext;

static uint32_t read_arbitary(ByteIOContext *pb) {
    uint8_t size, byte;
    int i;
    uint32_t word;

    size = get_byte(pb);

    word = 0;
    for (i = 0; i < size; i++) {
        byte = get_byte(pb);
        word <<= 8;
        word |= byte;
    }

    return word;
}

/*
 * Process WVE file header
 * Returns 1 if the WVE file is valid and successfully opened, 0 otherwise
 */
static int process_ea_header(AVFormatContext *s) {
    int inHeader;
    uint32_t blockid, size;
    EaDemuxContext *ea = (EaDemuxContext *)s->priv_data;
    ByteIOContext *pb = &s->pb;

    if (get_buffer(pb, (void*)&blockid, 4) != 4) {
        return 0;
    }
    if (le2me_32(blockid) != SCHl_TAG) {
        return 0;
    }

    if (get_buffer(pb, (void*)&size, 4) != 4) {
        return 0;
    }
    size = le2me_32(size);

    if (get_buffer(pb, (void*)&blockid, 4) != 4) {
        return 0;
    }
    if (le2me_32(blockid) != PT00_TAG) {
        av_log (s, AV_LOG_ERROR, "PT header missing\n");
        return 0;
    }

    inHeader = 1;
    while (inHeader) {
        int inSubheader;
        uint8_t byte;
        byte = get_byte(pb) & 0xFF;

        switch (byte) {
        case 0xFD:
            av_log (s, AV_LOG_INFO, "entered audio subheader\n");
            inSubheader = 1;
            while (inSubheader) {
                uint8_t subbyte;
                subbyte = get_byte(pb) & 0xFF;

                switch (subbyte) {
                case 0x82:
                    ea->num_channels = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "num_channels (element 0x82) set to 0x%08x\n", ea->num_channels);
                    break;
                case 0x83:
                    ea->compression_type = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "compression_type (element 0x83) set to 0x%08x\n", ea->compression_type);
                    break;
                case 0x85:
                    ea->num_samples = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "num_samples (element 0x85) set to 0x%08x\n", ea->num_samples);
                    break;
                case 0x8A:
                    av_log (s, AV_LOG_INFO, "element 0x%02x set to 0x%08x\n", subbyte, read_arbitary(pb));
                    av_log (s, AV_LOG_INFO, "exited audio subheader\n");
                    inSubheader = 0;
                    break;
                default:
                    av_log (s, AV_LOG_INFO, "element 0x%02x set to 0x%08x\n", subbyte, read_arbitary(pb));
                    break;
                }
            }
            break;
        case 0xFF:
            av_log (s, AV_LOG_INFO, "end of header block reached\n");
            inHeader = 0;
            break;
        default:
            av_log (s, AV_LOG_INFO, "header element 0x%02x set to 0x%08x\n", byte, read_arbitary(pb));
            break;
        }
    }

    if ((ea->num_channels != 2) || (ea->compression_type != 7)) {
        av_log (s, AV_LOG_ERROR, "unsupported stream type\n");
        return 0;
    }

    /* skip to the start of the data */
    url_fseek(pb, size, SEEK_SET);

    return 1;
}


static int ea_probe(AVProbeData *p)
{
    if (p->buf_size < 4)
        return 0;

    if (AV_RL32(&p->buf[0]) != SCHl_TAG)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int ea_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    EaDemuxContext *ea = (EaDemuxContext *)s->priv_data;
    AVStream *st;

    if (!process_ea_header(s))
        return AVERROR_IO;

#if 0
    /* initialize the video decoder stream */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;
    av_set_pts_info(st, 33, 1, 90000);
    ea->video_stream_index = st->index;
    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_EA_MJPEG;
    st->codec->codec_tag = 0;  /* no fourcc */
#endif

    /* initialize the audio decoder stream */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;
    av_set_pts_info(st, 33, 1, EA_SAMPLE_RATE);
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_ADPCM_EA;
    st->codec->codec_tag = 0;  /* no tag */
    st->codec->channels = ea->num_channels;
    st->codec->sample_rate = EA_SAMPLE_RATE;
    st->codec->bits_per_sample = EA_BITS_PER_SAMPLE;
    st->codec->bit_rate = st->codec->channels * st->codec->sample_rate *
        st->codec->bits_per_sample / 4;
    st->codec->block_align = st->codec->channels * st->codec->bits_per_sample;

    ea->audio_stream_index = st->index;
    ea->audio_frame_counter = 0;

    return 1;
}

static int ea_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    EaDemuxContext *ea = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int ret = 0;
    int packet_read = 0;
    unsigned char preamble[EA_PREAMBLE_SIZE];
    unsigned int chunk_type, chunk_size;

    while (!packet_read) {

        if (get_buffer(pb, preamble, EA_PREAMBLE_SIZE) != EA_PREAMBLE_SIZE)
            return AVERROR_IO;
        chunk_type = AV_RL32(&preamble[0]);
        chunk_size = AV_RL32(&preamble[4]) - EA_PREAMBLE_SIZE;

        switch (chunk_type) {
        /* audio data */
        case SCDl_TAG:
            ret = av_get_packet(pb, pkt, chunk_size);
            if (ret != chunk_size)
                ret = AVERROR_IO;
            else {
                    pkt->stream_index = ea->audio_stream_index;
                    pkt->pts = 90000;
                    pkt->pts *= ea->audio_frame_counter;
                    pkt->pts /= EA_SAMPLE_RATE;

                    /* 2 samples/byte, 1 or 2 samples per frame depending
                     * on stereo; chunk also has 12-byte header */
                    ea->audio_frame_counter += ((chunk_size - 12) * 2) /
                        ea->num_channels;
            }

            packet_read = 1;
            break;

        /* ending tag */
        case SCEl_TAG:
            ret = AVERROR_IO;
            packet_read = 1;
            break;

        default:
            url_fseek(pb, chunk_size, SEEK_CUR);
            break;
        }

        /* ending packet */
        if (chunk_type == SCEl_TAG) {
        }
    }

    return ret;
}

static int ea_read_close(AVFormatContext *s)
{
//    EaDemuxContext *ea = (EaDemuxContext *)s->priv_data;

    return 0;
}

AVInputFormat ea_demuxer = {
    "ea",
    "Electronic Arts Multimedia Format",
    sizeof(EaDemuxContext),
    ea_probe,
    ea_read_header,
    ea_read_packet,
    ea_read_close,
};
