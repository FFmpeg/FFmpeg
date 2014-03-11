/*
 * Motion Pixels MVI Demuxer
 * Copyright (c) 2008 Gregory Montoir (cyx@users.sourceforge.net)
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

#include "libavutil/channel_layout.h"
#include "avformat.h"
#include "internal.h"

#define MVI_FRAC_BITS 10

#define MVI_AUDIO_STREAM_INDEX 0
#define MVI_VIDEO_STREAM_INDEX 1

typedef struct MviDemuxContext {
    unsigned int (*get_int)(AVIOContext *);
    uint32_t audio_data_size;
    uint64_t audio_size_counter;
    uint64_t audio_frame_size;
    int audio_size_left;
    int video_frame_size;
} MviDemuxContext;

static int read_header(AVFormatContext *s)
{
    MviDemuxContext *mvi = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *ast, *vst;
    unsigned int version, frames_count, msecs_per_frame, player_version;

    ast = avformat_new_stream(s, NULL);
    if (!ast)
        return AVERROR(ENOMEM);

    vst = avformat_new_stream(s, NULL);
    if (!vst)
        return AVERROR(ENOMEM);

    if (ff_alloc_extradata(vst->codec, 2))
        return AVERROR(ENOMEM);

    version                  = avio_r8(pb);
    vst->codec->extradata[0] = avio_r8(pb);
    vst->codec->extradata[1] = avio_r8(pb);
    frames_count             = avio_rl32(pb);
    msecs_per_frame          = avio_rl32(pb);
    vst->codec->width        = avio_rl16(pb);
    vst->codec->height       = avio_rl16(pb);
    avio_r8(pb);
    ast->codec->sample_rate  = avio_rl16(pb);
    mvi->audio_data_size     = avio_rl32(pb);
    avio_r8(pb);
    player_version           = avio_rl32(pb);
    avio_rl16(pb);
    avio_r8(pb);

    if (frames_count == 0 || mvi->audio_data_size == 0)
        return AVERROR_INVALIDDATA;

    if (version != 7 || player_version > 213) {
        av_log(s, AV_LOG_ERROR, "unhandled version (%d,%d)\n", version, player_version);
        return AVERROR_INVALIDDATA;
    }

    avpriv_set_pts_info(ast, 64, 1, ast->codec->sample_rate);
    ast->codec->codec_type      = AVMEDIA_TYPE_AUDIO;
    ast->codec->codec_id        = AV_CODEC_ID_PCM_U8;
    ast->codec->channels        = 1;
    ast->codec->channel_layout  = AV_CH_LAYOUT_MONO;
    ast->codec->bits_per_coded_sample = 8;
    ast->codec->bit_rate        = ast->codec->sample_rate * 8;

    avpriv_set_pts_info(vst, 64, msecs_per_frame, 1000000);
    vst->avg_frame_rate    = av_inv_q(vst->time_base);
    vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codec->codec_id   = AV_CODEC_ID_MOTIONPIXELS;

    mvi->get_int = (vst->codec->width * vst->codec->height < (1 << 16)) ? avio_rl16 : avio_rl24;

    mvi->audio_frame_size   = ((uint64_t)mvi->audio_data_size << MVI_FRAC_BITS) / frames_count;
    if (mvi->audio_frame_size <= 1 << MVI_FRAC_BITS - 1) {
        av_log(s, AV_LOG_ERROR,
               "Invalid audio_data_size (%"PRIu32") or frames_count (%u)\n",
               mvi->audio_data_size, frames_count);
        return AVERROR_INVALIDDATA;
    }

    mvi->audio_size_counter = (ast->codec->sample_rate * 830 / mvi->audio_frame_size - 1) * mvi->audio_frame_size;
    mvi->audio_size_left    = mvi->audio_data_size;

    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, count;
    MviDemuxContext *mvi = s->priv_data;
    AVIOContext *pb = s->pb;

    if (mvi->video_frame_size == 0) {
        mvi->video_frame_size = (mvi->get_int)(pb);
        if (mvi->audio_size_left == 0)
            return AVERROR(EIO);
        count = (mvi->audio_size_counter + mvi->audio_frame_size + 512) >> MVI_FRAC_BITS;
        if (count > mvi->audio_size_left)
            count = mvi->audio_size_left;
        if ((ret = av_get_packet(pb, pkt, count)) < 0)
            return ret;
        pkt->stream_index = MVI_AUDIO_STREAM_INDEX;
        mvi->audio_size_left -= count;
        mvi->audio_size_counter += mvi->audio_frame_size - (count << MVI_FRAC_BITS);
    } else {
        if ((ret = av_get_packet(pb, pkt, mvi->video_frame_size)) < 0)
            return ret;
        pkt->stream_index = MVI_VIDEO_STREAM_INDEX;
        mvi->video_frame_size = 0;
    }
    return 0;
}

AVInputFormat ff_mvi_demuxer = {
    .name           = "mvi",
    .long_name      = NULL_IF_CONFIG_SMALL("Motion Pixels MVI"),
    .priv_data_size = sizeof(MviDemuxContext),
    .read_header    = read_header,
    .read_packet    = read_packet,
    .extensions     = "mvi",
};
