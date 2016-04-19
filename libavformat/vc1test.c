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
 * @file
 * VC1 test bitstream file demuxer
 * by Konstantin Shishkov
 * Format specified in SMPTE standard 421 Annex L
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

#define VC1_EXTRADATA_SIZE 4

static int vc1t_probe(AVProbeData *p)
{
    if (p->buf_size < 24)
        return 0;
    if (p->buf[3] != 0xC5 || AV_RL32(&p->buf[4]) != 4 || AV_RL32(&p->buf[20]) != 0xC)
        return 0;

    return AVPROBE_SCORE_EXTENSION;
}

static int vc1t_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *st;
    int frames;
    uint32_t fps;

    frames = avio_rl24(pb);
    if(avio_r8(pb) != 0xC5 || avio_rl32(pb) != 4)
        return AVERROR_INVALIDDATA;

    /* init video codec */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_WMV3;

    if (ff_get_extradata(s, st->codecpar, pb, VC1_EXTRADATA_SIZE) < 0)
        return AVERROR(ENOMEM);
    st->codecpar->height = avio_rl32(pb);
    st->codecpar->width = avio_rl32(pb);
    if(avio_rl32(pb) != 0xC)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 8);
    fps = avio_rl32(pb);
    if(fps == 0xFFFFFFFF)
        avpriv_set_pts_info(st, 32, 1, 1000);
    else{
        if (!fps) {
            av_log(s, AV_LOG_ERROR, "Zero FPS specified, defaulting to 1 FPS\n");
            fps = 1;
        }
        avpriv_set_pts_info(st, 24, 1, fps);
        st->duration = frames;
    }

    return 0;
}

static int vc1t_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    int frame_size;
    int keyframe = 0;
    uint32_t pts;

    if(avio_feof(pb))
        return AVERROR(EIO);

    frame_size = avio_rl24(pb);
    if(avio_r8(pb) & 0x80)
        keyframe = 1;
    pts = avio_rl32(pb);
    if(av_get_packet(pb, pkt, frame_size) < 0)
        return AVERROR(EIO);
    if(s->streams[0]->time_base.den == 1000)
        pkt->pts = pts;
    pkt->flags |= keyframe ? AV_PKT_FLAG_KEY : 0;
    pkt->pos -= 8;

    return pkt->size;
}

AVInputFormat ff_vc1t_demuxer = {
    .name           = "vc1test",
    .long_name      = NULL_IF_CONFIG_SMALL("VC-1 test bitstream"),
    .read_probe     = vc1t_probe,
    .read_header    = vc1t_read_header,
    .read_packet    = vc1t_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
};
