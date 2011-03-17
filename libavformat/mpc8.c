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

#include "libavcodec/get_bits.h"
#include "libavcodec/unary.h"
#include "avformat.h"
#include "avio_internal.h"

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

static inline int64_t bs_get_v(uint8_t **bs)
{
    int64_t v = 0;
    int br = 0;
    int c;

    do {
        c = **bs; (*bs)++;
        v <<= 7;
        v |= c & 0x7F;
        br++;
        if (br > 10)
            return -1;
    } while (c & 0x80);

    return v - br;
}

static int mpc8_probe(AVProbeData *p)
{
    uint8_t *bs = p->buf + 4;
    uint8_t *bs_end = bs + p->buf_size;
    int64_t size;

    if (p->buf_size < 16)
        return 0;
    if (AV_RL32(p->buf) != TAG_MPCK)
        return 0;
    while (bs < bs_end + 3) {
        int header_found = (bs[0] == 'S' && bs[1] == 'H');
        if (bs[0] < 'A' || bs[0] > 'Z' || bs[1] < 'A' || bs[1] > 'Z')
            return 0;
        bs += 2;
        size = bs_get_v(&bs);
        if (size < 2)
            return 0;
        if (bs + size - 2 >= bs_end)
            return AVPROBE_SCORE_MAX / 4 - 1; //seems to be valid MPC but no header yet
        if (header_found) {
            if (size < 11 || size > 28)
                return 0;
            if (!AV_RL32(bs)) //zero CRC is invalid
                return 0;
            return AVPROBE_SCORE_MAX;
        } else {
            bs += size - 2;
        }
    }
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

static void mpc8_get_chunk_header(AVIOContext *pb, int *tag, int64_t *size)
{
    int64_t pos;
    pos = avio_tell(pb);
    *tag = avio_rl16(pb);
    *size = ffio_read_varlen(pb);
    *size -= avio_tell(pb) - pos;
}

static void mpc8_parse_seektable(AVFormatContext *s, int64_t off)
{
    MPCContext *c = s->priv_data;
    int tag;
    int64_t size, pos, ppos[2];
    uint8_t *buf;
    int i, t, seekd;
    GetBitContext gb;

    avio_seek(s->pb, off, SEEK_SET);
    mpc8_get_chunk_header(s->pb, &tag, &size);
    if(tag != TAG_SEEKTABLE){
        av_log(s, AV_LOG_ERROR, "No seek table at given position\n");
        return;
    }
    if(!(buf = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE)))
        return;
    avio_read(s->pb, buf, size);
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
    AVIOContext *pb = s->pb;
    int64_t pos, off;

    switch(tag){
    case TAG_SEEKTBLOFF:
        pos = avio_tell(pb) + size;
        off = ffio_read_varlen(pb);
        mpc8_parse_seektable(s, chunk_pos + off);
        avio_seek(pb, pos, SEEK_SET);
        break;
    default:
        avio_skip(pb, size);
    }
}

static int mpc8_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    MPCContext *c = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    int tag = 0;
    int64_t size, pos;

    c->header_pos = avio_tell(pb);
    if(avio_rl32(pb) != TAG_MPCK){
        av_log(s, AV_LOG_ERROR, "Not a Musepack8 file\n");
        return -1;
    }

    while(!url_feof(pb)){
        pos = avio_tell(pb);
        mpc8_get_chunk_header(pb, &tag, &size);
        if(tag == TAG_STREAMHDR)
            break;
        mpc8_handle_chunk(s, tag, pos, size);
    }
    if(tag != TAG_STREAMHDR){
        av_log(s, AV_LOG_ERROR, "Stream header not found\n");
        return -1;
    }
    pos = avio_tell(pb);
    avio_skip(pb, 4); //CRC
    c->ver = avio_r8(pb);
    if(c->ver != 8){
        av_log(s, AV_LOG_ERROR, "Unknown stream version %d\n", c->ver);
        return -1;
    }
    c->samples = ffio_read_varlen(pb);
    ffio_read_varlen(pb); //silence samples at the beginning

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_MUSEPACK8;
    st->codec->bits_per_coded_sample = 16;

    st->codec->extradata_size = 2;
    st->codec->extradata = av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
    avio_read(pb, st->codec->extradata, st->codec->extradata_size);

    st->codec->channels = (st->codec->extradata[1] >> 4) + 1;
    st->codec->sample_rate = mpc8_rate[st->codec->extradata[0] >> 5];
    av_set_pts_info(st, 32, 1152  << (st->codec->extradata[1]&3)*2, st->codec->sample_rate);
    st->duration = c->samples / (1152 << (st->codec->extradata[1]&3)*2);
    size -= avio_tell(pb) - pos;

    return 0;
}

static int mpc8_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MPCContext *c = s->priv_data;
    int tag;
    int64_t pos, size;

    while(!url_feof(s->pb)){
        pos = avio_tell(s->pb);
        mpc8_get_chunk_header(s->pb, &tag, &size);
        if (size < 0)
            return -1;
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
    avio_seek(s->pb, st->index_entries[index].pos, SEEK_SET);
    c->frame = st->index_entries[index].timestamp;
    return 0;
}


AVInputFormat ff_mpc8_demuxer = {
    "mpc8",
    NULL_IF_CONFIG_SMALL("Musepack SV8"),
    sizeof(MPCContext),
    mpc8_probe,
    mpc8_read_header,
    mpc8_read_packet,
    NULL,
    mpc8_read_seek,
};
