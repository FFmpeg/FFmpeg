/*
 * Delphine Software International CIN File Demuxer
 * Copyright (c) 2006 Gregory Montoir (cyx@users.sourceforge.net)
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
 * Delphine Software International CIN file demuxer
 */

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"


typedef struct CinFileHeader {
    int video_frame_size;
    int video_frame_width;
    int video_frame_height;
    int audio_frequency;
    int audio_bits;
    int audio_stereo;
    int audio_frame_size;
} CinFileHeader;

typedef struct CinFrameHeader {
    int audio_frame_type;
    int video_frame_type;
    int pal_colors_count;
    int audio_frame_size;
    int video_frame_size;
} CinFrameHeader;

typedef struct CinDemuxContext {
    int audio_stream_index;
    int video_stream_index;
    CinFileHeader file_header;
    int64_t audio_stream_pts;
    int64_t video_stream_pts;
    CinFrameHeader frame_header;
    int audio_buffer_size;
} CinDemuxContext;


static int cin_probe(const AVProbeData *p)
{
    /* header starts with this special marker */
    if (AV_RL32(&p->buf[0]) != 0x55AA0000)
        return 0;

    /* for accuracy, check some header field values */
    if (AV_RL32(&p->buf[12]) != 22050 || p->buf[16] != 16 || p->buf[17] != 0)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int cin_read_file_header(CinDemuxContext *cin, AVIOContext *pb) {
    CinFileHeader *hdr = &cin->file_header;

    if (avio_rl32(pb) != 0x55AA0000)
        return AVERROR_INVALIDDATA;

    hdr->video_frame_size   = avio_rl32(pb);
    hdr->video_frame_width  = avio_rl16(pb);
    hdr->video_frame_height = avio_rl16(pb);
    hdr->audio_frequency    = avio_rl32(pb);
    hdr->audio_bits         = avio_r8(pb);
    hdr->audio_stereo       = avio_r8(pb);
    hdr->audio_frame_size   = avio_rl16(pb);

    if (hdr->audio_frequency != 22050 || hdr->audio_bits != 16 || hdr->audio_stereo != 0)
        return AVERROR_INVALIDDATA;

    return 0;
}

static int cin_read_header(AVFormatContext *s)
{
    int rc;
    CinDemuxContext *cin = s->priv_data;
    CinFileHeader *hdr = &cin->file_header;
    AVIOContext *pb = s->pb;
    AVStream *st;

    rc = cin_read_file_header(cin, pb);
    if (rc)
        return rc;

    cin->video_stream_pts = 0;
    cin->audio_stream_pts = 0;
    cin->audio_buffer_size = 0;

    /* initialize the video decoder stream */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avpriv_set_pts_info(st, 32, 1, 12);
    cin->video_stream_index = st->index;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_DSICINVIDEO;
    st->codecpar->codec_tag = 0;  /* no fourcc */
    st->codecpar->width = hdr->video_frame_width;
    st->codecpar->height = hdr->video_frame_height;

    /* initialize the audio decoder stream */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avpriv_set_pts_info(st, 32, 1, 22050);
    cin->audio_stream_index = st->index;
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_DSICINAUDIO;
    st->codecpar->codec_tag = 0;  /* no tag */
    st->codecpar->channels = 1;
    st->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
    st->codecpar->sample_rate = 22050;
    st->codecpar->bits_per_coded_sample = 8;
    st->codecpar->bit_rate = st->codecpar->sample_rate * st->codecpar->bits_per_coded_sample * st->codecpar->channels;

    return 0;
}

static int cin_read_frame_header(CinDemuxContext *cin, AVIOContext *pb) {
    CinFrameHeader *hdr = &cin->frame_header;

    hdr->video_frame_type = avio_r8(pb);
    hdr->audio_frame_type = avio_r8(pb);
    hdr->pal_colors_count = avio_rl16(pb);
    hdr->video_frame_size = avio_rl32(pb);
    hdr->audio_frame_size = avio_rl32(pb);

    if (avio_feof(pb) || pb->error)
        return AVERROR(EIO);

    if (avio_rl32(pb) != 0xAA55AA55)
        return AVERROR_INVALIDDATA;
    if (hdr->video_frame_size < 0 || hdr->audio_frame_size < 0)
        return AVERROR_INVALIDDATA;

    return 0;
}

static int cin_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    CinDemuxContext *cin = s->priv_data;
    AVIOContext *pb = s->pb;
    CinFrameHeader *hdr = &cin->frame_header;
    int rc, palette_type, pkt_size;
    int ret;

    if (cin->audio_buffer_size == 0) {
        rc = cin_read_frame_header(cin, pb);
        if (rc)
            return rc;

        if ((int16_t)hdr->pal_colors_count < 0) {
            hdr->pal_colors_count = -(int16_t)hdr->pal_colors_count;
            palette_type = 1;
        } else {
            palette_type = 0;
        }

        /* palette and video packet */
        pkt_size = (palette_type + 3) * hdr->pal_colors_count + hdr->video_frame_size;

        pkt_size = ffio_limit(pb, pkt_size);

        ret = av_new_packet(pkt, 4 + pkt_size);
        if (ret < 0)
            return ret;

        pkt->stream_index = cin->video_stream_index;
        pkt->pts = cin->video_stream_pts++;

        pkt->data[0] = palette_type;
        pkt->data[1] = hdr->pal_colors_count & 0xFF;
        pkt->data[2] = hdr->pal_colors_count >> 8;
        pkt->data[3] = hdr->video_frame_type;

        ret = avio_read(pb, &pkt->data[4], pkt_size);
        if (ret < 0) {
            av_packet_unref(pkt);
            return ret;
        }
        if (ret < pkt_size)
            av_shrink_packet(pkt, 4 + ret);

        /* sound buffer will be processed on next read_packet() call */
        cin->audio_buffer_size = hdr->audio_frame_size;
        return 0;
    }

    /* audio packet */
    ret = av_get_packet(pb, pkt, cin->audio_buffer_size);
    if (ret < 0)
        return ret;

    pkt->stream_index = cin->audio_stream_index;
    pkt->pts = cin->audio_stream_pts;
    pkt->duration = cin->audio_buffer_size - (pkt->pts == 0);
    cin->audio_stream_pts += pkt->duration;
    cin->audio_buffer_size = 0;
    return 0;
}

AVInputFormat ff_dsicin_demuxer = {
    .name           = "dsicin",
    .long_name      = NULL_IF_CONFIG_SMALL("Delphine Software International CIN"),
    .priv_data_size = sizeof(CinDemuxContext),
    .read_probe     = cin_probe,
    .read_header    = cin_read_header,
    .read_packet    = cin_read_packet,
};
