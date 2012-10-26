/*
 * G.729 bit format muxer and demuxer
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
#include "avformat.h"
#include "internal.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/put_bits.h"

#define MAX_FRAME_SIZE 10

#define SYNC_WORD  0x6b21
#define BIT_0      0x7f
#define BIT_1      0x81

static int probe(AVProbeData *p)
{
    int i, j;

    if(p->buf_size < 0x40)
        return 0;

    for(i=0; i+3<p->buf_size && i< 10*0x50; ){
        if(AV_RL16(&p->buf[0]) != SYNC_WORD)
            return 0;
        j=AV_RL16(&p->buf[2]);
        if(j!=0x40 && j!=0x50)
            return 0;
        i+=j;
    }
    return AVPROBE_SCORE_MAX/2;
}

static int read_header(AVFormatContext *s)
{
    AVStream* st;

    st=avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id=AV_CODEC_ID_G729;
    st->codec->sample_rate=8000;
    st->codec->block_align = 16;
    st->codec->channels=1;

    avpriv_set_pts_info(st, 64, 1, 100);
    return 0;
}

static int read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    PutBitContext pbo;
    uint16_t buf[8 * MAX_FRAME_SIZE + 2];
    int packet_size;
    uint16_t* src=buf;
    int i, j, ret;
    int64_t pos= avio_tell(pb);

    if(url_feof(pb))
        return AVERROR_EOF;

    avio_rl16(pb); // sync word
    packet_size = avio_rl16(pb) / 8;
    if(packet_size > MAX_FRAME_SIZE)
        return AVERROR_INVALIDDATA;

    ret = avio_read(pb, (uint8_t*)buf, (8 * packet_size) * sizeof(uint16_t));
    if(ret<0)
        return ret;
    if(ret != 8 * packet_size * sizeof(uint16_t))
        return AVERROR(EIO);

    if (av_new_packet(pkt, packet_size) < 0)
        return AVERROR(ENOMEM);

    init_put_bits(&pbo, pkt->data, packet_size);
    for(j=0; j < packet_size; j++)
        for(i=0; i<8;i++)
            put_bits(&pbo,1, AV_RL16(src++) == BIT_1 ? 1 : 0);

    flush_put_bits(&pbo);

    pkt->duration=1;
    pkt->pos = pos;
    return 0;
}

AVInputFormat ff_bit_demuxer = {
    .name        = "bit",
    .long_name   = NULL_IF_CONFIG_SMALL("G.729 BIT file format"),
    .read_probe  = probe,
    .read_header = read_header,
    .read_packet = read_packet,
    .extensions  = "bit",
};

#if CONFIG_MUXERS
static int write_header(AVFormatContext *s)
{
    AVCodecContext *enc = s->streams[0]->codec;

    enc->codec_id = AV_CODEC_ID_G729;
    enc->channels = 1;
    enc->bits_per_coded_sample = 16;
    enc->block_align = (enc->bits_per_coded_sample * enc->channels) >> 3;

    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    GetBitContext gb;
    int i;

    avio_wl16(pb, SYNC_WORD);
    avio_wl16(pb, 8 * 10);

    init_get_bits(&gb, pkt->data, 8*10);
    for(i=0; i< 8 * 10; i++)
        avio_wl16(pb, get_bits1(&gb) ? BIT_1 : BIT_0);
    avio_flush(pb);

    return 0;
}

AVOutputFormat ff_bit_muxer = {
    .name         = "bit",
    .long_name    = NULL_IF_CONFIG_SMALL("G.729 BIT file format"),
    .mime_type    = "audio/bit",
    .extensions   = "bit",
    .audio_codec  = AV_CODEC_ID_G729,
    .video_codec  = AV_CODEC_ID_NONE,
    .write_header = write_header,
    .write_packet = write_packet,
};
#endif
