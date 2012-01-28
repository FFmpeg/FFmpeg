/*
 * id RoQ (.roq) File Demuxer
 * Copyright (c) 2003 The ffmpeg Project
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
 * id RoQ format file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the .roq file format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"

#define RoQ_MAGIC_NUMBER 0x1084
#define RoQ_CHUNK_PREAMBLE_SIZE 8
#define RoQ_AUDIO_SAMPLE_RATE 22050
#define RoQ_CHUNKS_TO_SCAN 30

#define RoQ_INFO           0x1001
#define RoQ_QUAD_CODEBOOK  0x1002
#define RoQ_QUAD_VQ        0x1011
#define RoQ_SOUND_MONO     0x1020
#define RoQ_SOUND_STEREO   0x1021

typedef struct RoqDemuxContext {

    int frame_rate;
    int width;
    int height;
    int audio_channels;

    int video_stream_index;
    int audio_stream_index;

    int64_t video_pts;
    unsigned int audio_frame_count;

} RoqDemuxContext;

static int roq_probe(AVProbeData *p)
{
    if ((AV_RL16(&p->buf[0]) != RoQ_MAGIC_NUMBER) ||
        (AV_RL32(&p->buf[2]) != 0xFFFFFFFF))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int roq_read_header(AVFormatContext *s)
{
    RoqDemuxContext *roq = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned char preamble[RoQ_CHUNK_PREAMBLE_SIZE];

    /* get the main header */
    if (avio_read(pb, preamble, RoQ_CHUNK_PREAMBLE_SIZE) !=
        RoQ_CHUNK_PREAMBLE_SIZE)
        return AVERROR(EIO);
    roq->frame_rate = AV_RL16(&preamble[6]);

    /* init private context parameters */
    roq->width = roq->height = roq->audio_channels = roq->video_pts =
    roq->audio_frame_count = 0;
    roq->audio_stream_index = -1;
    roq->video_stream_index = -1;

    s->ctx_flags |= AVFMTCTX_NOHEADER;

    return 0;
}

static int roq_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    RoqDemuxContext *roq = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret = 0;
    unsigned int chunk_size;
    unsigned int chunk_type;
    unsigned int codebook_size;
    unsigned char preamble[RoQ_CHUNK_PREAMBLE_SIZE];
    int packet_read = 0;
    int64_t codebook_offset;

    while (!packet_read) {

        if (url_feof(s->pb))
            return AVERROR(EIO);

        /* get the next chunk preamble */
        if ((ret = avio_read(pb, preamble, RoQ_CHUNK_PREAMBLE_SIZE)) !=
            RoQ_CHUNK_PREAMBLE_SIZE)
            return AVERROR(EIO);

        chunk_type = AV_RL16(&preamble[0]);
        chunk_size = AV_RL32(&preamble[2]);
        if(chunk_size > INT_MAX)
            return AVERROR_INVALIDDATA;

        chunk_size = ffio_limit(pb, chunk_size);

        switch (chunk_type) {

        case RoQ_INFO:
            if (roq->video_stream_index == -1) {
                AVStream *st = avformat_new_stream(s, NULL);
                if (!st)
                    return AVERROR(ENOMEM);
                avpriv_set_pts_info(st, 63, 1, roq->frame_rate);
                roq->video_stream_index = st->index;
                st->codec->codec_type   = AVMEDIA_TYPE_VIDEO;
                st->codec->codec_id     = CODEC_ID_ROQ;
                st->codec->codec_tag    = 0;  /* no fourcc */

                if (avio_read(pb, preamble, RoQ_CHUNK_PREAMBLE_SIZE) != RoQ_CHUNK_PREAMBLE_SIZE)
                    return AVERROR(EIO);
                st->codec->width  = roq->width  = AV_RL16(preamble);
                st->codec->height = roq->height = AV_RL16(preamble + 2);
                break;
            }
            /* don't care about this chunk anymore */
            avio_skip(pb, RoQ_CHUNK_PREAMBLE_SIZE);
            break;

        case RoQ_QUAD_CODEBOOK:
            /* packet needs to contain both this codebook and next VQ chunk */
            codebook_offset = avio_tell(pb) - RoQ_CHUNK_PREAMBLE_SIZE;
            codebook_size = chunk_size;
            avio_skip(pb, codebook_size);
            if (avio_read(pb, preamble, RoQ_CHUNK_PREAMBLE_SIZE) !=
                RoQ_CHUNK_PREAMBLE_SIZE)
                return AVERROR(EIO);
            chunk_size = AV_RL32(&preamble[2]) + RoQ_CHUNK_PREAMBLE_SIZE * 2 +
                codebook_size;

            /* rewind */
            avio_seek(pb, codebook_offset, SEEK_SET);

            /* load up the packet */
            ret= av_get_packet(pb, pkt, chunk_size);
            if (ret != chunk_size)
                return AVERROR(EIO);
            pkt->stream_index = roq->video_stream_index;
            pkt->pts = roq->video_pts++;

            packet_read = 1;
            break;

        case RoQ_SOUND_MONO:
        case RoQ_SOUND_STEREO:
            if (roq->audio_stream_index == -1) {
                AVStream *st = avformat_new_stream(s, NULL);
                if (!st)
                    return AVERROR(ENOMEM);
                avpriv_set_pts_info(st, 32, 1, RoQ_AUDIO_SAMPLE_RATE);
                roq->audio_stream_index = st->index;
                st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
                st->codec->codec_id = CODEC_ID_ROQ_DPCM;
                st->codec->codec_tag = 0;  /* no tag */
                st->codec->channels = roq->audio_channels = chunk_type == RoQ_SOUND_STEREO ? 2 : 1;
                st->codec->sample_rate = RoQ_AUDIO_SAMPLE_RATE;
                st->codec->bits_per_coded_sample = 16;
                st->codec->bit_rate = st->codec->channels * st->codec->sample_rate *
                    st->codec->bits_per_coded_sample;
                st->codec->block_align = st->codec->channels * st->codec->bits_per_coded_sample;
            }
        case RoQ_QUAD_VQ:
            /* load up the packet */
            if (av_new_packet(pkt, chunk_size + RoQ_CHUNK_PREAMBLE_SIZE))
                return AVERROR(EIO);
            /* copy over preamble */
            memcpy(pkt->data, preamble, RoQ_CHUNK_PREAMBLE_SIZE);

            if (chunk_type == RoQ_QUAD_VQ) {
                pkt->stream_index = roq->video_stream_index;
                pkt->pts = roq->video_pts++;
            } else {
                pkt->stream_index = roq->audio_stream_index;
                pkt->pts = roq->audio_frame_count;
                roq->audio_frame_count += (chunk_size / roq->audio_channels);
            }

            pkt->pos= avio_tell(pb);
            ret = avio_read(pb, pkt->data + RoQ_CHUNK_PREAMBLE_SIZE,
                chunk_size);
            if (ret != chunk_size)
                ret = AVERROR(EIO);

            packet_read = 1;
            break;

        default:
            av_log(s, AV_LOG_ERROR, "  unknown RoQ chunk (%04X)\n", chunk_type);
            return AVERROR_INVALIDDATA;
        }
    }

    return ret;
}

AVInputFormat ff_roq_demuxer = {
    .name           = "RoQ",
    .long_name      = NULL_IF_CONFIG_SMALL("id RoQ format"),
    .priv_data_size = sizeof(RoqDemuxContext),
    .read_probe     = roq_probe,
    .read_header    = roq_read_header,
    .read_packet    = roq_read_packet,
};
