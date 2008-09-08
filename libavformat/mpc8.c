/*
 * Musepack SV8 demuxer
 * Copyright (c) 2007 Konstantin Shishkov
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

#include "libavcodec/bitstream.h"
#include "libavcodec/unary.h"
#include "avformat.h"

/// Two-byte MPC tag
#define MKMPCTAG(a, b) (a | (b << 8))

#define TAG_MPCK MKTAG('M','P','C','K')

/// Reserved MPC tags
enum MPCPacketTags{
    TAG_STREAMHDR   = MKMPCTAG('S','H'),
    TAG_STREAMEND   = MKMPCTAG('S','E'),

    TAG_AUDIOPACKET = MKMPCTAG('A','P'),

    TAG_SEEKTBLOFF  = MKMPCTAG('S','O'),
    TAG_SEEKTABLE   = MKMPCTAG('S','T'),

    TAG_REPLAYGAIN  = MKMPCTAG('R','G'),
    TAG_ENCINFO     = MKMPCTAG('E','I'),
};

static const int mpc8_rate[8] = { 44100, 48000, 37800, 32000, -1, -1, -1, -1 };

typedef struct {
    int ver;
    int frame;
    int64_t header_pos;
    int64_t samples;
} MPCContext;

static int mpc8_probe(AVProbeData *p)
{
    if (AV_RL32(p->buf) == TAG_MPCK)
        return AVPROBE_SCORE_MAX;
    return 0;
}

static inline int64_t gb_get_v(GetBitContext *gb)
{
    int64_t v = 0;
    int bits = 0;
    while(get_bits1(gb) && bits < 64-7){
        v <<= 7;
        v |= get_bits(gb, 7);
        bits += 7;
    }
    v <<= 7;
    v |= get_bits(gb, 7);

    return v;
}

static void mpc8_get_chunk_header(ByteIOContext *pb, int *tag, int64_t *size)
{
    int64_t pos;
    pos = url_ftell(pb);
    *tag = get_le16(pb);
    *size = ff_get_v(pb);
    *size -= url_ftell(pb) - pos;
}

static void mpc8_parse_seektable(AVFormatContext *s, int64_t off)
{
    MPCContext *c = s->priv_data;
    int tag;
    int64_t size, pos, ppos[2];
    uint8_t *buf;
    int i, t, seekd;
    GetBitContext gb;

    url_fseek(s->pb, off, SEEK_SET);
    mpc8_get_chunk_header(s->pb, &tag, &size);
    if(tag != TAG_SEEKTABLE){
        av_log(s, AV_LOG_ERROR, "No seek table at given position\n");
        return;
    }
    if(!(buf = av_malloc(size)))
        return;
    get_buffer(s->pb, buf, size);
    init_get_bits(&gb, buf, size * 8);
    size = gb_get_v(&gb);
    if(size > UINT_MAX/4 || size > c->samples/1152){
        av_log(s, AV_LOG_ERROR, "Seek table is too big\n");
        return;
    }
    seekd = get_bits(&gb, 4);
    for(i = 0; i < 2; i++){
        pos = gb_get_v(&gb) + c->header_pos;
        ppos[1 - i] = pos;
        av_add_index_entry(s->streams[0], pos, i, 0, 0, AVINDEX_KEYFRAME);
    }
    for(; i < size; i++){
        t = get_unary(&gb, 1, 33) << 12;
        t += get_bits(&gb, 12);
        if(t & 1)
            t = -(t & ~1);
        pos = (t >> 1) + ppos[0]*2 - ppos[1];
        av_add_index_entry(s->streams[0], pos, i << seekd, 0, 0, AVINDEX_KEYFRAME);
        ppos[1] = ppos[0];
        ppos[0] = pos;
    }
    av_free(buf);
}

static void mpc8_handle_chunk(AVFormatContext *s, int tag, int64_t chunk_pos, int64_t size)
{
    ByteIOContext *pb = s->pb;
    int64_t pos, off;

    switch(tag){
    case TAG_SEEKTBLOFF:
        pos = url_ftell(pb) + size;
        off = ff_get_v(pb);
        mpc8_parse_seektable(s, chunk_pos + off);
        url_fseek(pb, pos, SEEK_SET);
        break;
    default:
        url_fskip(pb, size);
    }
}

static int mpc8_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    MPCContext *c = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st;
    int tag = 0;
    int64_t size, pos;

    c->header_pos = url_ftell(pb);
    if(get_le32(pb) != TAG_MPCK){
        av_log(s, AV_LOG_ERROR, "Not a Musepack8 file\n");
        return -1;
    }

    while(!url_feof(pb)){
        pos = url_ftell(pb);
        mpc8_get_chunk_header(pb, &tag, &size);
        if(tag == TAG_STREAMHDR)
            break;
        mpc8_handle_chunk(s, tag, pos, size);
    }
    if(tag != TAG_STREAMHDR){
        av_log(s, AV_LOG_ERROR, "Stream header not found\n");
        return -1;
    }
    pos = url_ftell(pb);
    url_fskip(pb, 4); //CRC
    c->ver = get_byte(pb);
    if(c->ver != 8){
        av_log(s, AV_LOG_ERROR, "Unknown stream version %d\n", c->ver);
        return -1;
    }
    c->samples = ff_get_v(pb);
    ff_get_v(pb); //silence samples at the beginning

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_MUSEPACK8;
    st->codec->bits_per_coded_sample = 16;

    st->codec->extradata_size = 2;
    st->codec->extradata = av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
    get_buffer(pb, st->codec->extradata, st->codec->extradata_size);

    st->codec->channels = (st->codec->extradata[1] >> 4) + 1;
    st->codec->sample_rate = mpc8_rate[st->codec->extradata[0] >> 5];
    av_set_pts_info(st, 32, 1152  << (st->codec->extradata[1]&3)*2, st->codec->sample_rate);
    st->duration = c->samples / (1152 << (st->codec->extradata[1]&3)*2);
    size -= url_ftell(pb) - pos;

    return 0;
}

static int mpc8_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MPCContext *c = s->priv_data;
    int tag;
    int64_t pos, size;

    while(!url_feof(s->pb)){
        pos = url_ftell(s->pb);
        mpc8_get_chunk_header(s->pb, &tag, &size);
        if(tag == TAG_AUDIOPACKET){
            if(av_get_packet(s->pb, pkt, size) < 0)
                return AVERROR(ENOMEM);
            pkt->stream_index = 0;
            pkt->pts = c->frame;
            return 0;
        }
        if(tag == TAG_STREAMEND)
            return AVERROR(EIO);
        mpc8_handle_chunk(s, tag, pos, size);
    }
    return 0;
}

static int mpc8_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    AVStream *st = s->streams[stream_index];
    MPCContext *c = s->priv_data;
    int index = av_index_search_timestamp(st, timestamp, flags);

    if(index < 0) return -1;
    url_fseek(s->pb, st->index_entries[index].pos, SEEK_SET);
    c->frame = st->index_entries[index].timestamp;
    return 0;
}


AVInputFormat mpc8_demuxer = {
    "mpc8",
    NULL_IF_CONFIG_SMALL("Musepack SV8"),
    sizeof(MPCContext),
    mpc8_probe,
    mpc8_read_header,
    mpc8_read_packet,
    NULL,
    mpc8_read_seek,
};
