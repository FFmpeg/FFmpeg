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
#include "internal.h"

typedef struct yop_dec_context {
    AVPacket video_packet;

    int odd_frame;
    int frame_size;
    int audio_block_length;
    int palette_size;
} YopDecContext;

static int yop_probe(AVProbeData *probe_packet)
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

    AVCodecContext *audio_dec, *video_dec;
    AVStream *audio_stream, *video_stream;

    int frame_rate, ret;

    audio_stream = avformat_new_stream(s, NULL);
    video_stream = avformat_new_stream(s, NULL);
    if (!audio_stream || !video_stream)
        return AVERROR(ENOMEM);

    // Extra data that will be passed to the decoder
    video_stream->codec->extradata_size = 8;

    video_stream->codec->extradata = av_mallocz(video_stream->codec->extradata_size +
                                                FF_INPUT_BUFFER_PADDING_SIZE);

    if (!video_stream->codec->extradata)
        return AVERROR(ENOMEM);

    // Audio
    audio_dec               = audio_stream->codec;
    audio_dec->codec_type   = AVMEDIA_TYPE_AUDIO;
    audio_dec->codec_id     = AV_CODEC_ID_ADPCM_IMA_APC;
    audio_dec->channels     = 1;
    audio_dec->channel_layout = AV_CH_LAYOUT_MONO;
    audio_dec->sample_rate  = 22050;

    // Video
    video_dec               = video_stream->codec;
    video_dec->codec_type   = AVMEDIA_TYPE_VIDEO;
    video_dec->codec_id     = AV_CODEC_ID_YOP;

    avio_skip(pb, 6);

    frame_rate              = avio_r8(pb);
    yop->frame_size         = avio_r8(pb) * 2048;
    video_dec->width        = avio_rl16(pb);
    video_dec->height       = avio_rl16(pb);

    video_stream->sample_aspect_ratio = (AVRational){1, 2};

    ret = avio_read(pb, video_dec->extradata, 8);
    if (ret < 8)
        return ret < 0 ? ret : AVERROR_EOF;

    yop->palette_size       = video_dec->extradata[0] * 3 + 4;
    yop->audio_block_length = AV_RL16(video_dec->extradata + 6);

    video_dec->bit_rate     = 8 * (yop->frame_size - yop->audio_block_length) * frame_rate;

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
        *pkt                   =  yop->video_packet;
        memset(&yop->video_packet, 0, sizeof(yop->video_packet));
        pkt->data[0]           =  yop->odd_frame;
        pkt->flags             |= AV_PKT_FLAG_KEY;
        yop->odd_frame         ^= 1;
        return pkt->size;
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
    return yop->audio_block_length;

err_out:
    av_free_packet(&yop->video_packet);
    return ret;
}

static int yop_read_close(AVFormatContext *s)
{
    YopDecContext *yop = s->priv_data;
    av_free_packet(&yop->video_packet);
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

    pos_min        = s->data_offset;
    pos_max        = avio_size(s->pb) - yop->frame_size;
    frame_count    = (pos_max - pos_min) / yop->frame_size;

    timestamp      = FFMAX(0, FFMIN(frame_count, timestamp));

    frame_pos      = timestamp * yop->frame_size + pos_min;

    if (avio_seek(s->pb, frame_pos, SEEK_SET) < 0)
        return -1;

    av_free_packet(&yop->video_packet);
    yop->odd_frame = timestamp & 1;

    return 0;
}

AVInputFormat ff_yop_demuxer = {
    .name           = "yop",
    .long_name      = NULL_IF_CONFIG_SMALL("Psygnosis YOP"),
    .priv_data_size = sizeof(YopDecContext),
    .read_probe     = yop_probe,
    .read_header    = yop_read_header,
    .read_packet    = yop_read_packet,
    .read_close     = yop_read_close,
    .read_seek      = yop_read_seek,
    .extensions     = "yop",
    .flags          = AVFMT_GENERIC_INDEX,
};
