/*
 * Psygnosis YOP demuxer
 *
 * Copyright (C) 2010 Mohamed Naufal Basheer <naufal11@gmail.com>
 * derived from the code by
 * Copyright (C) 2009 Thomas P. Higdon <thomas.p.higdon@gmail.com>
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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

typedef struct yop_dec_context {
    AVPacket video_packet;

    int odd_frame;
    int frame_size;
    int audio_block_length;
    int palette_size;
} YopDecContext;

static int yop_probe(const AVProbeData *probe_packet)
{
    if (AV_RB16(probe_packet->buf) == AV_RB16("YO")  &&
        probe_packet->buf[2]<10                      &&
        probe_packet->buf[3]<10                      &&
        probe_packet->buf[6]                         &&
        probe_packet->buf[7]                         &&
        !(probe_packet->buf[8] & 1)                  &&
        !(probe_packet->buf[10] & 1)                 &&
        AV_RL16(probe_packet->buf + 12 + 6) >= 920    &&
        AV_RL16(probe_packet->buf + 12 + 6) < probe_packet->buf[12] * 3 + 4 + probe_packet->buf[7] * 2048
    )
        return AVPROBE_SCORE_MAX * 3 / 4;

    return 0;
}

static int yop_read_header(AVFormatContext *s)
{
    YopDecContext *yop = s->priv_data;
    AVIOContext *pb  = s->pb;

    AVCodecParameters *audio_par, *video_par;
    AVStream *audio_stream, *video_stream;

    int frame_rate, ret;

    audio_stream = avformat_new_stream(s, NULL);
    video_stream = avformat_new_stream(s, NULL);
    if (!audio_stream || !video_stream)
        return AVERROR(ENOMEM);

    // Audio
    audio_par                 = audio_stream->codecpar;
    audio_par->codec_type     = AVMEDIA_TYPE_AUDIO;
    audio_par->codec_id       = AV_CODEC_ID_ADPCM_IMA_APC;
    audio_par->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    audio_par->sample_rate    = 22050;

    // Video
    video_par               = video_stream->codecpar;
    video_par->codec_type   = AVMEDIA_TYPE_VIDEO;
    video_par->codec_id     = AV_CODEC_ID_YOP;

    avio_skip(pb, 6);

    frame_rate              = avio_r8(pb);
    yop->frame_size         = avio_r8(pb) * 2048;
    video_par->width        = avio_rl16(pb);
    video_par->height       = avio_rl16(pb);

    video_stream->sample_aspect_ratio = (AVRational){1, 2};

    ret = ff_get_extradata(s, video_par, pb, 8);
    if (ret < 0)
        return ret;

    yop->palette_size       = video_par->extradata[0] * 3 + 4;
    yop->audio_block_length = AV_RL16(video_par->extradata + 6);

    video_par->bit_rate     = 8 * (yop->frame_size - yop->audio_block_length) * frame_rate;

    // 1840 samples per frame, 1 nibble per sample; hence 1840/2 = 920
    if (yop->audio_block_length < 920 ||
        yop->audio_block_length + yop->palette_size >= yop->frame_size) {
        av_log(s, AV_LOG_ERROR, "YOP has invalid header\n");
        return AVERROR_INVALIDDATA;
    }

    avio_seek(pb, 2048, SEEK_SET);

    avpriv_set_pts_info(video_stream, 32, 1, frame_rate);

    return 0;
}

static int yop_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    YopDecContext *yop = s->priv_data;
    AVIOContext *pb  = s->pb;

    int ret;
    int actual_video_data_size = yop->frame_size -
                                 yop->audio_block_length - yop->palette_size;

    yop->video_packet.stream_index = 1;

    if (yop->video_packet.data) {
        av_packet_move_ref(pkt, &yop->video_packet);
        pkt->data[0]           =  yop->odd_frame;
        pkt->flags             |= AV_PKT_FLAG_KEY;
        yop->odd_frame         ^= 1;
        return 0;
    }
    ret = av_new_packet(&yop->video_packet,
                        yop->frame_size - yop->audio_block_length);
    if (ret < 0)
        return ret;

    yop->video_packet.pos = avio_tell(pb);

    ret = avio_read(pb, yop->video_packet.data, yop->palette_size);
    if (ret < 0) {
        goto err_out;
    }else if (ret < yop->palette_size) {
        ret = AVERROR_EOF;
        goto err_out;
    }

    ret = av_get_packet(pb, pkt, 920);
    if (ret < 0)
        goto err_out;

    // Set position to the start of the frame
    pkt->pos = yop->video_packet.pos;

    avio_skip(pb, yop->audio_block_length - ret);

    ret = avio_read(pb, yop->video_packet.data + yop->palette_size,
                     actual_video_data_size);
    if (ret < 0)
        goto err_out;
    else if (ret < actual_video_data_size)
        av_shrink_packet(&yop->video_packet, yop->palette_size + ret);

    // Arbitrarily return the audio data first
    return 0;

err_out:
    av_packet_unref(&yop->video_packet);
    return ret;
}

static int yop_read_close(AVFormatContext *s)
{
    YopDecContext *yop = s->priv_data;
    av_packet_unref(&yop->video_packet);
    return 0;
}

static int yop_read_seek(AVFormatContext *s, int stream_index,
                         int64_t timestamp, int flags)
{
    YopDecContext *yop = s->priv_data;
    int64_t frame_pos, pos_min, pos_max;
    int frame_count;

    if (!stream_index)
        return -1;

    pos_min        = ffformatcontext(s)->data_offset;
    pos_max        = avio_size(s->pb) - yop->frame_size;
    frame_count    = (pos_max - pos_min) / yop->frame_size;

    timestamp      = FFMAX(0, FFMIN(frame_count, timestamp));

    frame_pos      = timestamp * yop->frame_size + pos_min;

    if (avio_seek(s->pb, frame_pos, SEEK_SET) < 0)
        return -1;

    av_packet_unref(&yop->video_packet);
    yop->odd_frame = timestamp & 1;

    return 0;
}

const FFInputFormat ff_yop_demuxer = {
    .p.name         = "yop",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Psygnosis YOP"),
    .p.extensions   = "yop",
    .p.flags        = AVFMT_GENERIC_INDEX,
    .priv_data_size = sizeof(YopDecContext),
    .read_probe     = yop_probe,
    .read_header    = yop_read_header,
    .read_packet    = yop_read_packet,
    .read_close     = yop_read_close,
    .read_seek      = yop_read_seek,
};
