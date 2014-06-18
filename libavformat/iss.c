/*
 * ISS (.iss) file demuxer
 * Copyright (c) 2008 Jaikrishnan Menon <realityman@gmx.net>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Funcom ISS file demuxer
 * @author Jaikrishnan Menon
 * @see http://wiki.multimedia.cx/index.php?title=FunCom_ISS
 */

#include "libavutil/channel_layout.h"
#include "avformat.h"
#include "internal.h"
#include "libavutil/avstring.h"

#define ISS_SIG "IMA_ADPCM_Sound"
#define ISS_SIG_LEN 15
#define MAX_TOKEN_SIZE 20

typedef struct IssDemuxContext {
    int packet_size;
    int sample_start_pos;
} IssDemuxContext;

static void get_token(AVIOContext *s, char *buf, int maxlen)
{
    int i = 0;
    char c;

    while ((c = avio_r8(s))) {
        if(c == ' ')
            break;
        if (i < maxlen-1)
            buf[i++] = c;
    }

    if(!c)
        avio_r8(s);

    buf[i] = 0; /* Ensure null terminated, but may be truncated */
}

static int iss_probe(AVProbeData *p)
{
    if (strncmp(p->buf, ISS_SIG, ISS_SIG_LEN))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static av_cold int iss_read_header(AVFormatContext *s)
{
    IssDemuxContext *iss = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    char token[MAX_TOKEN_SIZE];
    int stereo, rate_divisor;

    get_token(pb, token, sizeof(token)); //"IMA_ADPCM_Sound"
    get_token(pb, token, sizeof(token)); //packet size
    sscanf(token, "%d", &iss->packet_size);
    get_token(pb, token, sizeof(token)); //File ID
    get_token(pb, token, sizeof(token)); //out size
    get_token(pb, token, sizeof(token)); //stereo
    sscanf(token, "%d", &stereo);
    get_token(pb, token, sizeof(token)); //Unknown1
    get_token(pb, token, sizeof(token)); //RateDivisor
    sscanf(token, "%d", &rate_divisor);
    get_token(pb, token, sizeof(token)); //Unknown2
    get_token(pb, token, sizeof(token)); //Version ID
    get_token(pb, token, sizeof(token)); //Size

    iss->sample_start_pos = avio_tell(pb);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_ADPCM_IMA_ISS;
    if (stereo) {
        st->codecpar->channels       = 2;
        st->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;
    } else {
        st->codecpar->channels       = 1;
        st->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
    }
    st->codecpar->sample_rate = 44100;
    if(rate_divisor > 0)
         st->codecpar->sample_rate /= rate_divisor;
    st->codecpar->bits_per_coded_sample = 4;
    st->codecpar->bit_rate = st->codecpar->channels * st->codecpar->sample_rate
                                      * st->codecpar->bits_per_coded_sample;
    st->codecpar->block_align = iss->packet_size;
    avpriv_set_pts_info(st, 32, 1, st->codecpar->sample_rate);

    return 0;
}

static int iss_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    IssDemuxContext *iss = s->priv_data;
    int ret = av_get_packet(s->pb, pkt, iss->packet_size);

    if(ret != iss->packet_size)
        return AVERROR(EIO);

    pkt->stream_index = 0;
    pkt->pts = avio_tell(s->pb) - iss->sample_start_pos;
    if(s->streams[0]->codecpar->channels > 0)
        pkt->pts /= s->streams[0]->codecpar->channels*2;
    return 0;
}

AVInputFormat ff_iss_demuxer = {
    .name           = "iss",
    .long_name      = NULL_IF_CONFIG_SMALL("Funcom ISS"),
    .priv_data_size = sizeof(IssDemuxContext),
    .read_probe     = iss_probe,
    .read_header    = iss_read_header,
    .read_packet    = iss_read_packet,
};
