/*
 * ACT file format demuxer
 * Copyright (c) 2007-2008 Vladimir Voroshilov
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio_internal.h"
#include "riff.h"
#include "internal.h"

#define CHUNK_SIZE 512
#define RIFF_TAG MKTAG('R','I','F','F')
#define WAVE_TAG MKTAG('W','A','V','E')

typedef struct{
    int bytes_left_in_chunk;
    uint8_t audio_buffer[22];///< temporary buffer for ACT frame
    char second_packet;      ///< 1 - if temporary buffer contains valid (second) G.729 packet
} ACTContext;

static int probe(const AVProbeData *p)
{
    int i;

    if ((AV_RL32(&p->buf[0]) != RIFF_TAG) ||
        (AV_RL32(&p->buf[8]) != WAVE_TAG) ||
        (AV_RL32(&p->buf[16]) != 16))
    return 0;

    //We can't be sure that this is ACT and not regular WAV
    if (p->buf_size<512)
        return 0;

    for(i=44; i<256; i++)
        if(p->buf[i])
            return 0;

    if(p->buf[256]!=0x84)
        return 0;

    for(i=264; i<512; i++)
        if(p->buf[i])
            return 0;

    return AVPROBE_SCORE_MAX;
}

static int read_header(AVFormatContext *s)
{
    ACTContext* ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    int size;
    AVStream* st;
    int ret;

    int min,sec,msec;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(pb, 16);
    size=avio_rl32(pb);
    ret = ff_get_wav_header(s, pb, st->codecpar, size, 0);
    if (ret < 0)
        return ret;

    /*
      8000Hz (Fine-rec) file format has 10 bytes long
      packets with 10ms of sound data in them
    */
    if (st->codecpar->sample_rate != 8000) {
        av_log(s, AV_LOG_ERROR, "Sample rate %d is not supported.\n", st->codecpar->sample_rate);
        return AVERROR_INVALIDDATA;
    }

    st->codecpar->frame_size=80;
    st->codecpar->ch_layout.nb_channels = 1;
    avpriv_set_pts_info(st, 64, 1, 100);

    st->codecpar->codec_id=AV_CODEC_ID_G729;

    avio_seek(pb, 257, SEEK_SET);
    msec=avio_rl16(pb);
    sec=avio_r8(pb);
    min=avio_rl32(pb);

    st->duration = av_rescale(1000*(min*60+sec)+msec, st->codecpar->sample_rate, 1000 * st->codecpar->frame_size);

    ctx->bytes_left_in_chunk=CHUNK_SIZE;

    avio_seek(pb, 512, SEEK_SET);

    return 0;
}


static int read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    ACTContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;
    int frame_size=s->streams[0]->codecpar->sample_rate==8000?10:22;


    if(s->streams[0]->codecpar->sample_rate==8000)
        ret=av_new_packet(pkt, 10);
    else
        ret=av_new_packet(pkt, 11);

    if(ret)
        return ret;

    if(s->streams[0]->codecpar->sample_rate==4400 && !ctx->second_packet)
    {
        ret = ffio_read_size(pb, ctx->audio_buffer, frame_size);

        if(ret<0)
            return ret;

        pkt->data[0]=ctx->audio_buffer[11];
        pkt->data[1]=ctx->audio_buffer[0];
        pkt->data[2]=ctx->audio_buffer[12];
        pkt->data[3]=ctx->audio_buffer[1];
        pkt->data[4]=ctx->audio_buffer[13];
        pkt->data[5]=ctx->audio_buffer[2];
        pkt->data[6]=ctx->audio_buffer[14];
        pkt->data[7]=ctx->audio_buffer[3];
        pkt->data[8]=ctx->audio_buffer[15];
        pkt->data[9]=ctx->audio_buffer[4];
        pkt->data[10]=ctx->audio_buffer[16];

        ctx->second_packet=1;
    }
    else if(s->streams[0]->codecpar->sample_rate==4400 && ctx->second_packet)
    {
        pkt->data[0]=ctx->audio_buffer[5];
        pkt->data[1]=ctx->audio_buffer[17];
        pkt->data[2]=ctx->audio_buffer[6];
        pkt->data[3]=ctx->audio_buffer[18];
        pkt->data[4]=ctx->audio_buffer[7];
        pkt->data[5]=ctx->audio_buffer[19];
        pkt->data[6]=ctx->audio_buffer[8];
        pkt->data[7]=ctx->audio_buffer[20];
        pkt->data[8]=ctx->audio_buffer[9];
        pkt->data[9]=ctx->audio_buffer[21];
        pkt->data[10]=ctx->audio_buffer[10];

        ctx->second_packet=0;
    }
    else // 8000 Hz
    {
        ret = ffio_read_size(pb, ctx->audio_buffer, frame_size);

        if(ret<0)
            return ret;

        pkt->data[0]=ctx->audio_buffer[5];
        pkt->data[1]=ctx->audio_buffer[0];
        pkt->data[2]=ctx->audio_buffer[6];
        pkt->data[3]=ctx->audio_buffer[1];
        pkt->data[4]=ctx->audio_buffer[7];
        pkt->data[5]=ctx->audio_buffer[2];
        pkt->data[6]=ctx->audio_buffer[8];
        pkt->data[7]=ctx->audio_buffer[3];
        pkt->data[8]=ctx->audio_buffer[9];
        pkt->data[9]=ctx->audio_buffer[4];
    }

    ctx->bytes_left_in_chunk -= frame_size;

    if(ctx->bytes_left_in_chunk < frame_size)
    {
        avio_skip(pb, ctx->bytes_left_in_chunk);
        ctx->bytes_left_in_chunk=CHUNK_SIZE;
    }

    pkt->duration=1;

    return ret;
}

const AVInputFormat ff_act_demuxer = {
    .name           = "act",
    .long_name      = "ACT Voice file format",
    .priv_data_size = sizeof(ACTContext),
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
};
