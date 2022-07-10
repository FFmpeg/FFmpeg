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
#include "apetag.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "avio_internal.h"

/// Two-byte MPC tag
#define MKMPCTAG(a, b) ((a) | ((b) << 8))

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

typedef struct MPCContext {
    int ver;
    int64_t header_pos;
    int64_t samples;

    int64_t apetag_start;
} MPCContext;

static inline int64_t bs_get_v(const uint8_t **bs)
{
    uint64_t v = 0;
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

static int mpc8_probe(const AVProbeData *p)
{
    const uint8_t *bs = p->buf + 4;
    const uint8_t *bs_end = bs + p->buf_size;
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
        if (size >= bs_end - bs + 2)
            return AVPROBE_SCORE_EXTENSION - 1; // seems to be valid MPC but no header yet
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
    uint64_t v = 0;
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
    pos -= avio_tell(pb);
    if (av_sat_add64(*size, pos) != (uint64_t)*size + pos) {
        *size = -1;
    } else
        *size += pos;
}

static int mpc8_parse_seektable(AVFormatContext *s, int64_t off)
{
    MPCContext *c = s->priv_data;
    int tag;
    int64_t size, pos, ppos[2];
    uint8_t *buf;
    int i, t, seekd, ret;
    int64_t ret64;
    GetBitContext gb;

    if (s->nb_streams == 0) {
        av_log(s, AV_LOG_ERROR, "No stream added before parsing seek table\n");
        return AVERROR_INVALIDDATA;
    }

    ret64 = avio_seek(s->pb, off, SEEK_SET);
    if (ret64 < 0)
        return AVERROR_INVALIDDATA;
    mpc8_get_chunk_header(s->pb, &tag, &size);
    if(tag != TAG_SEEKTABLE || avio_feof(s->pb)){
        av_log(s, AV_LOG_ERROR, "No seek table at given position\n");
        return AVERROR_INVALIDDATA;
    }
    if (size > INT_MAX/10 || size<=0) {
        av_log(s, AV_LOG_ERROR, "Bad seek table size\n");
        return AVERROR_INVALIDDATA;
    }
    if(!(buf = av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE)))
        return AVERROR(ENOMEM);
    ret = avio_read(s->pb, buf, size);
    if (ret != size) {
        av_log(s, AV_LOG_ERROR, "seek table truncated\n");
        av_free(buf);
        return AVERROR_INVALIDDATA;
    }
    memset(buf+size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    init_get_bits(&gb, buf, size * 8);
    size = gb_get_v(&gb);
    if(size > UINT_MAX/4 || size > c->samples/1152){
        av_log(s, AV_LOG_ERROR, "Seek table is too big\n");
        av_free(buf);
        return AVERROR_INVALIDDATA;
    }
    seekd = get_bits(&gb, 4);
    for(i = 0; i < 2; i++){
        pos = gb_get_v(&gb);
        if (av_sat_add64(pos, c->header_pos) != pos + (uint64_t)c->header_pos) {
            av_free(buf);
            return AVERROR_INVALIDDATA;
        }

        pos += c->header_pos;
        ppos[1 - i] = pos;
        av_add_index_entry(s->streams[0], pos, i, 0, 0, AVINDEX_KEYFRAME);
    }
    for(; i < size; i++){
        if (get_bits_left(&gb) < 13) {
            av_free(buf);
            return AVERROR_INVALIDDATA;
        }
        t = get_unary(&gb, 1, 33) << 12;
        t += get_bits(&gb, 12);
        if(t & 1)
            t = -(t & ~1);
        pos = (t >> 1) + (uint64_t)ppos[0]*2 - ppos[1];
        av_add_index_entry(s->streams[0], pos, (int64_t)i << seekd, 0, 0, AVINDEX_KEYFRAME);
        ppos[1] = ppos[0];
        ppos[0] = pos;
    }
    av_free(buf);
    return 0;
}

static int mpc8_handle_chunk(AVFormatContext *s, int tag, int64_t chunk_pos, int64_t size)
{
    AVIOContext *pb = s->pb;
    int64_t pos, off;
    int ret;

    switch(tag){
    case TAG_SEEKTBLOFF:
        pos = avio_tell(pb);
        off = ffio_read_varlen(pb);
        if (pos > INT64_MAX - size || off < 0 || off > INT64_MAX - chunk_pos)
            return AVERROR_INVALIDDATA;
        pos += size;
        ret = mpc8_parse_seektable(s, chunk_pos + off);
        if (ret < 0)
            return AVERROR_INVALIDDATA;
        avio_seek(pb, pos, SEEK_SET);
        break;
    default:
        avio_skip(pb, size);
    }
    return 0;
}

static int mpc8_read_header(AVFormatContext *s)
{
    MPCContext *c = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    int tag = 0, ret;
    int channels;
    int64_t size, pos;

    c->header_pos = avio_tell(pb);
    if(avio_rl32(pb) != TAG_MPCK){
        av_log(s, AV_LOG_ERROR, "Not a Musepack8 file\n");
        return AVERROR_INVALIDDATA;
    }

    while(!avio_feof(pb)){
        pos = avio_tell(pb);
        mpc8_get_chunk_header(pb, &tag, &size);
        if (size < 0) {
            av_log(s, AV_LOG_ERROR, "Invalid chunk length\n");
            return AVERROR_INVALIDDATA;
        }
        if(tag == TAG_STREAMHDR)
            break;
        ret = mpc8_handle_chunk(s, tag, pos, size);
        if (ret < 0)
            return ret;
    }
    if(tag != TAG_STREAMHDR){
        av_log(s, AV_LOG_ERROR, "Stream header not found\n");
        return AVERROR_INVALIDDATA;
    }
    pos = avio_tell(pb);
    avio_skip(pb, 4); //CRC
    c->ver = avio_r8(pb);
    if(c->ver != 8){
        avpriv_report_missing_feature(s, "Stream version %d", c->ver);
        return AVERROR_PATCHWELCOME;
    }
    c->samples = ffio_read_varlen(pb);
    ffio_read_varlen(pb); //silence samples at the beginning

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_MUSEPACK8;
    st->codecpar->bits_per_coded_sample = 16;

    if ((ret = ff_get_extradata(s, st->codecpar, pb, 2)) < 0)
        return ret;

    channels = (st->codecpar->extradata[1] >> 4) + 1;
    st->codecpar->ch_layout.nb_channels = channels;
    st->codecpar->sample_rate = mpc8_rate[st->codecpar->extradata[0] >> 5];
    avpriv_set_pts_info(st, 64, 1152  << (st->codecpar->extradata[1]&3)*2, st->codecpar->sample_rate);
    st->start_time = 0;
    st->duration = c->samples / (1152 << (st->codecpar->extradata[1]&3)*2);
    size -= avio_tell(pb) - pos;
    if (size > 0)
        avio_skip(pb, size);

    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
        int64_t pos = avio_tell(s->pb);
        c->apetag_start = ff_ape_parse_tag(s);
        avio_seek(s->pb, pos, SEEK_SET);
    }

    return 0;
}

static int mpc8_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MPCContext *c = s->priv_data;
    int tag, ret;
    int64_t pos, size;

    while(!avio_feof(s->pb)){
        pos = avio_tell(s->pb);

        /* don't return bogus packets with the ape tag data */
        if (c->apetag_start && pos >= c->apetag_start)
            return AVERROR_EOF;

        mpc8_get_chunk_header(s->pb, &tag, &size);
        if (size < 0 || size > INT_MAX)
            return -1;
        if(tag == TAG_AUDIOPACKET){
            if ((ret = av_get_packet(s->pb, pkt, size)) < 0)
                return ret;
            pkt->stream_index = 0;
            pkt->duration     = 1;
            return 0;
        }
        if(tag == TAG_STREAMEND)
            return AVERROR_EOF;
        mpc8_handle_chunk(s, tag, pos, size);
    }
    return AVERROR_EOF;
}

static int mpc8_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    AVStream *st = s->streams[stream_index];
    FFStream *const sti = ffstream(st);
    int index = av_index_search_timestamp(st, timestamp, flags);

    if(index < 0) return -1;
    if (avio_seek(s->pb, sti->index_entries[index].pos, SEEK_SET) < 0)
        return -1;
    avpriv_update_cur_dts(s, st, sti->index_entries[index].timestamp);
    return 0;
}


const AVInputFormat ff_mpc8_demuxer = {
    .name           = "mpc8",
    .long_name      = NULL_IF_CONFIG_SMALL("Musepack SV8"),
    .priv_data_size = sizeof(MPCContext),
    .read_probe     = mpc8_probe,
    .read_header    = mpc8_read_header,
    .read_packet    = mpc8_read_packet,
    .read_seek      = mpc8_read_seek,
};
