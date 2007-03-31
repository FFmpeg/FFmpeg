/*
 * TTA demuxer
 * Copyright (c) 2006 Alex Beregszaszi
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "avformat.h"
#include "bitstream.h"

typedef struct {
    int totalframes, currentframe;
    uint32_t *seektable;
} TTAContext;

static int tta_probe(AVProbeData *p)
{
    const uint8_t *d = p->buf;
    if (p->buf_size < 4)
        return 0;
    if (d[0] == 'T' && d[1] == 'T' && d[2] == 'A' && d[3] == '1')
        return 80;
    return 0;
}

static int tta_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    TTAContext *c = s->priv_data;
    AVStream *st;
    int i, channels, bps, samplerate, datalen, framelen;

    if (get_le32(&s->pb) != ff_get_fourcc("TTA1"))
        return -1; // not tta file

    url_fskip(&s->pb, 2); // FIXME: flags
    channels = get_le16(&s->pb);
    bps = get_le16(&s->pb);
    samplerate = get_le32(&s->pb);
    if(samplerate <= 0 || samplerate > 1000000){
        av_log(s, AV_LOG_ERROR, "nonsense samplerate\n");
        return -1;
    }

    datalen = get_le32(&s->pb);
    if(datalen < 0){
        av_log(s, AV_LOG_ERROR, "nonsense datalen\n");
        return -1;
    }

    url_fskip(&s->pb, 4); // header crc

    framelen = samplerate*256/245;
    c->totalframes = datalen / framelen + ((datalen % framelen) ? 1 : 0);
    c->currentframe = 0;

    if(c->totalframes >= UINT_MAX/sizeof(uint32_t)){
        av_log(s, AV_LOG_ERROR, "totalframes too large\n");
        return -1;
    }
    c->seektable = av_mallocz(sizeof(uint32_t)*c->totalframes);
    if (!c->seektable)
        return AVERROR_NOMEM;

    for (i = 0; i < c->totalframes; i++)
        c->seektable[i] = get_le32(&s->pb);
    url_fskip(&s->pb, 4); // seektable crc

    st = av_new_stream(s, 0);
//    av_set_pts_info(st, 32, 1, 1000);
    if (!st)
        return AVERROR_NOMEM;
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_TTA;
    st->codec->channels = channels;
    st->codec->sample_rate = samplerate;
    st->codec->bits_per_sample = bps;

    st->codec->extradata_size = url_ftell(&s->pb);
    if(st->codec->extradata_size+FF_INPUT_BUFFER_PADDING_SIZE <= (unsigned)st->codec->extradata_size){
        //this check is redundant as get_buffer should fail
        av_log(s, AV_LOG_ERROR, "extradata_size too large\n");
        return -1;
    }
    st->codec->extradata = av_mallocz(st->codec->extradata_size+FF_INPUT_BUFFER_PADDING_SIZE);
    url_fseek(&s->pb, 0, SEEK_SET);
    get_buffer(&s->pb, st->codec->extradata, st->codec->extradata_size);

    return 0;
}

static int tta_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    TTAContext *c = s->priv_data;
    int ret, size;

    // FIXME!
    if (c->currentframe > c->totalframes)
        size = 0;
    else
        size = c->seektable[c->currentframe];

    c->currentframe++;

    if (av_new_packet(pkt, size) < 0)
        return AVERROR_IO;

    pkt->pos = url_ftell(&s->pb);
    pkt->stream_index = 0;
    ret = get_buffer(&s->pb, pkt->data, size);
    if (ret <= 0) {
        av_free_packet(pkt);
        return AVERROR_IO;
    }
    pkt->size = ret;
//    av_log(s, AV_LOG_INFO, "TTA packet #%d desired size: %d read size: %d at pos %d\n",
//        c->currentframe, size, ret, pkt->pos);
    return 0; //ret;
}

static int tta_read_close(AVFormatContext *s)
{
    TTAContext *c = s->priv_data;
    if (c->seektable)
        av_free(c->seektable);
    return 0;
}

AVInputFormat tta_demuxer = {
    "tta",
    "true-audio",
    sizeof(TTAContext),
    tta_probe,
    tta_read_header,
    tta_read_packet,
    tta_read_close,
    .extensions = "tta",
};
