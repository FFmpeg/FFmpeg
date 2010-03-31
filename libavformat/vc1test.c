/*
 * VC1 Test Bitstreams Format Demuxer
 * Copyright (c) 2006, 2008 Konstantin Shishkov
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
 * @file libavformat/vc1test.c
 * VC1 test bitstream file demuxer
 * by Konstantin Shishkov
 * Format specified in SMPTE standard 421 Annex L
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define VC1_EXTRADATA_SIZE 4

static int vc1t_probe(AVProbeData *p)
{
    if (p->buf_size < 24)
        return 0;
    if (p->buf[3] != 0xC5 || AV_RL32(&p->buf[4]) != 4 || AV_RL32(&p->buf[20]) != 0xC)
        return 0;

    return AVPROBE_SCORE_MAX/2;
}

static int vc1t_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    ByteIOContext *pb = s->pb;
    AVStream *st;
    int frames;
    uint32_t fps;

    frames = get_le24(pb);
    if(get_byte(pb) != 0xC5 || get_le32(pb) != 4)
        return -1;

    /* init video codec */
    st = av_new_stream(s, 0);
    if (!st)
        return -1;

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_WMV3;

    st->codec->extradata = av_malloc(VC1_EXTRADATA_SIZE);
    st->codec->extradata_size = VC1_EXTRADATA_SIZE;
    get_buffer(pb, st->codec->extradata, VC1_EXTRADATA_SIZE);
    st->codec->height = get_le32(pb);
    st->codec->width = get_le32(pb);
    if(get_le32(pb) != 0xC)
        return -1;
    url_fskip(pb, 8);
    fps = get_le32(pb);
    if(fps == 0xFFFFFFFF)
        av_set_pts_info(st, 32, 1, 1000);
    else{
        if (!fps) {
            av_log(s, AV_LOG_ERROR, "Zero FPS specified, defaulting to 1 FPS\n");
            fps = 1;
        }
        av_set_pts_info(st, 24, 1, fps);
        st->duration = frames;
    }

    return 0;
}

static int vc1t_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    int frame_size;
    int keyframe = 0;
    uint32_t pts;

    if(url_feof(pb))
        return AVERROR(EIO);

    frame_size = get_le24(pb);
    if(get_byte(pb) & 0x80)
        keyframe = 1;
    pts = get_le32(pb);
    if(av_get_packet(pb, pkt, frame_size) < 0)
        return AVERROR(EIO);
    if(s->streams[0]->time_base.den == 1000)
        pkt->pts = pts;
    pkt->flags |= keyframe ? AV_PKT_FLAG_KEY : 0;
    pkt->pos -= 8;

    return pkt->size;
}

AVInputFormat vc1t_demuxer = {
    "vc1test",
    NULL_IF_CONFIG_SMALL("VC-1 test bitstream format"),
    0,
    vc1t_probe,
    vc1t_read_header,
    vc1t_read_packet,
    .flags = AVFMT_GENERIC_INDEX,
};
