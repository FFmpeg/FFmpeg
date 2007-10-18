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
#define GSTR_TAG MKTAG('G', 'S', 'T', 'R')
#define SCDl_TAG MKTAG('S', 'C', 'D', 'l')
#define SCEl_TAG MKTAG('S', 'C', 'E', 'l')
#define MVhd_TAG MKTAG('M', 'V', 'h', 'd')
#define MV0K_TAG MKTAG('M', 'V', '0', 'K')
#define MV0F_TAG MKTAG('M', 'V', '0', 'F')

#define EA_BITS_PER_SAMPLE 16

typedef struct EaDemuxContext {
    int big_endian;

    int video_codec;
    AVRational time_base;
    int video_stream_index;

    int audio_codec;
    int audio_stream_index;
    int audio_frame_counter;

    int64_t audio_pts;

    int sample_rate;
    int num_channels;
    int num_samples;
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
 * Process PT/GSTR sound header
 * return 1 if success, 0 if invalid format, otherwise AVERROR_xxx
 */
static int process_audio_header_elements(AVFormatContext *s)
{
    int inHeader = 1;
    EaDemuxContext *ea = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int compression_type = -1, revision = -1;

    ea->sample_rate = -1;
    ea->num_channels = 1;

    while (inHeader) {
        int inSubheader;
        uint8_t byte;
        byte = get_byte(pb);

        switch (byte) {
        case 0xFD:
            av_log (s, AV_LOG_INFO, "entered audio subheader\n");
            inSubheader = 1;
            while (inSubheader) {
                uint8_t subbyte;
                subbyte = get_byte(pb);

                switch (subbyte) {
                case 0x80:
                    revision = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "revision (element 0x80) set to 0x%08x\n", revision);
                    break;
                case 0x82:
                    ea->num_channels = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "num_channels (element 0x82) set to 0x%08x\n", ea->num_channels);
                    break;
                case 0x83:
                    compression_type = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "compression_type (element 0x83) set to 0x%08x\n", compression_type);
                    break;
                case 0x84:
                    ea->sample_rate = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "sample_rate (element 0x84) set to %i\n", ea->sample_rate);
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
                case 0xFF:
                    av_log (s, AV_LOG_INFO, "end of header block reached (within audio subheader)\n");
                    inSubheader = 0;
                    inHeader = 0;
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

    switch (compression_type) {
    case  0: ea->audio_codec = CODEC_ID_PCM_S16LE; break;
    case  7: ea->audio_codec = CODEC_ID_ADPCM_EA; break;
    default:
        av_log(s, AV_LOG_ERROR, "unsupported stream type; compression_type=%i\n", compression_type);
        return 0;
    }

    if (ea->sample_rate == -1)
        ea->sample_rate = revision==3 ? 48000 : 22050;

    return 1;
}

static int process_video_header_vp6(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    ByteIOContext *pb = &s->pb;

    url_fskip(pb, 16);
    ea->time_base.den = get_le32(pb);
    ea->time_base.num = get_le32(pb);
    ea->video_codec = CODEC_ID_VP6;

    return 1;
}

/*
 * Process EA file header
 * Returns 1 if the EA file is valid and successfully opened, 0 otherwise
 */
static int process_ea_header(AVFormatContext *s) {
    uint32_t blockid, size = 0;
    EaDemuxContext *ea = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int i;

    for (i=0; i<5 && (!ea->audio_codec || !ea->video_codec); i++) {
        unsigned int startpos = url_ftell(pb);
        int err = 0;

        blockid = get_le32(pb);
        size = get_le32(pb);
        if (i == 0)
            ea->big_endian = size > 0x000FFFFF;
        if (ea->big_endian)
            size = bswap_32(size);

        switch (blockid) {
            case SCHl_TAG :
                blockid = get_le32(pb);
                if (blockid == GSTR_TAG) {
                    url_fskip(pb, 4);
                } else if (blockid != PT00_TAG) {
                    av_log (s, AV_LOG_ERROR, "unknown SCHl headerid\n");
                    return 0;
                }
                err = process_audio_header_elements(s);
                break;

            case MVhd_TAG :
                err = process_video_header_vp6(s);
                break;
        }

        if (err < 0) {
            av_log(s, AV_LOG_ERROR, "error parsing header: %i\n", err);
            return err;
        }

        url_fseek(pb, startpos + size, SEEK_SET);
    }

    url_fseek(pb, 0, SEEK_SET);

    return 1;
}


static int ea_probe(AVProbeData *p)
{
    uint32_t tag;

    tag = AV_RL32(&p->buf[0]);
    if (tag == SCHl_TAG || tag == MVhd_TAG)
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int ea_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    EaDemuxContext *ea = s->priv_data;
    AVStream *st;

    if (!process_ea_header(s))
        return AVERROR(EIO);

    if (ea->time_base.num && ea->time_base.den) {
        /* initialize the video decoder stream */
        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR(ENOMEM);
        ea->video_stream_index = st->index;
        st->codec->codec_type = CODEC_TYPE_VIDEO;
        st->codec->codec_id = ea->video_codec;
        st->codec->codec_tag = 0;  /* no fourcc */
        st->codec->time_base = ea->time_base;
    }

    /* initialize the audio decoder stream */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    av_set_pts_info(st, 33, 1, ea->sample_rate);
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = ea->audio_codec;
    st->codec->codec_tag = 0;  /* no tag */
    st->codec->channels = ea->num_channels;
    st->codec->sample_rate = ea->sample_rate;
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
    unsigned int chunk_type, chunk_size;
    int key = 0;

    while (!packet_read) {
        chunk_type = get_le32(pb);
        chunk_size = get_le32(pb) - 8;

        switch (chunk_type) {
        /* audio data */
        case SCDl_TAG:
            ret = av_get_packet(pb, pkt, chunk_size);
            if (ret != chunk_size)
                ret = AVERROR(EIO);
            else {
                    pkt->stream_index = ea->audio_stream_index;
                    pkt->pts = 90000;
                    pkt->pts *= ea->audio_frame_counter;
                    pkt->pts /= ea->sample_rate;

                    /* 2 samples/byte, 1 or 2 samples per frame depending
                     * on stereo; chunk also has 12-byte header */
                    ea->audio_frame_counter += ((chunk_size - 12) * 2) /
                        ea->num_channels;
            }

            packet_read = 1;
            break;

        /* ending tag */
        case SCEl_TAG:
            ret = AVERROR(EIO);
            packet_read = 1;
            break;

        case MV0K_TAG:
            key = PKT_FLAG_KEY;
        case MV0F_TAG:
            ret = av_get_packet(pb, pkt, chunk_size);
            if (ret != chunk_size)
                ret = AVERROR_IO;
            else {
                pkt->stream_index = ea->video_stream_index;
                pkt->flags |= key;
            }
            packet_read = 1;
            break;

        default:
            url_fseek(pb, chunk_size, SEEK_CUR);
            break;
        }
    }

    return ret;
}

AVInputFormat ea_demuxer = {
    "ea",
    "Electronic Arts Multimedia Format",
    sizeof(EaDemuxContext),
    ea_probe,
    ea_read_header,
    ea_read_packet,
};
