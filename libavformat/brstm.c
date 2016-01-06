/*
 * BRSTM demuxer
 * Copyright (c) 2012 Paul B Mahol
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
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "internal.h"

typedef struct BRSTMDemuxContext {
    uint32_t    block_size;
    uint32_t    block_count;
    uint32_t    current_block;
    uint32_t    samples_per_block;
    uint32_t    last_block_used_bytes;
    uint32_t    last_block_size;
    uint32_t    last_block_samples;
    uint32_t    data_start;
    uint8_t     *table;
    uint8_t     *adpc;
    int         little_endian;
} BRSTMDemuxContext;

static int probe(AVProbeData *p)
{
    if (AV_RL32(p->buf) == MKTAG('R','S','T','M') &&
        (AV_RL16(p->buf + 4) == 0xFFFE ||
         AV_RL16(p->buf + 4) == 0xFEFF))
        return AVPROBE_SCORE_MAX / 3 * 2;
    return 0;
}

static int probe_bfstm(AVProbeData *p)
{
    if ((AV_RL32(p->buf) == MKTAG('F','S','T','M') ||
         AV_RL32(p->buf) == MKTAG('C','S','T','M')) &&
        (AV_RL16(p->buf + 4) == 0xFFFE ||
         AV_RL16(p->buf + 4) == 0xFEFF))
        return AVPROBE_SCORE_MAX / 3 * 2;
    return 0;
}

static int read_close(AVFormatContext *s)
{
    BRSTMDemuxContext *b = s->priv_data;

    av_freep(&b->table);
    av_freep(&b->adpc);

    return 0;
}

static av_always_inline unsigned int read16(AVFormatContext *s)
{
    BRSTMDemuxContext *b = s->priv_data;
    if (b->little_endian)
        return avio_rl16(s->pb);
    else
        return avio_rb16(s->pb);
}

static av_always_inline unsigned int read32(AVFormatContext *s)
{
    BRSTMDemuxContext *b = s->priv_data;
    if (b->little_endian)
        return avio_rl32(s->pb);
    else
        return avio_rb32(s->pb);
}

static int read_header(AVFormatContext *s)
{
    BRSTMDemuxContext *b = s->priv_data;
    int bom, major, minor, codec, chunk;
    int64_t h1offset, pos, toffset;
    uint32_t size, asize, start = 0;
    AVStream *st;
    int ret = AVERROR_EOF;
    int loop = 0;
    int bfstm = !strcmp("bfstm", s->iformat->name);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;

    avio_skip(s->pb, 4);

    bom = avio_rb16(s->pb);
    if (bom != 0xFEFF && bom != 0xFFFE) {
        av_log(s, AV_LOG_ERROR, "invalid byte order: %X\n", bom);
        return AVERROR_INVALIDDATA;
    }

    if (bom == 0xFFFE)
        b->little_endian = 1;

    if (!bfstm) {
        major = avio_r8(s->pb);
        minor = avio_r8(s->pb);
        avio_skip(s->pb, 4); // size of file
        size = read16(s);
        if (size < 14)
            return AVERROR_INVALIDDATA;

        avio_skip(s->pb, size - 14);
        pos = avio_tell(s->pb);
        if (avio_rl32(s->pb) != MKTAG('H','E','A','D'))
            return AVERROR_INVALIDDATA;
    } else {
        uint32_t info_offset = 0;
        uint16_t section_count, header_size, i;

        header_size = read16(s); // 6

        avio_skip(s->pb, 4); // Unknown constant 0x00030000
        avio_skip(s->pb, 4); // size of file
        section_count = read16(s);
        avio_skip(s->pb, 2); // padding
        for (i = 0; avio_tell(s->pb) < header_size
                    && !(start && info_offset)
                    && i < section_count; i++) {
            uint16_t flag = read16(s);
            avio_skip(s->pb, 2);
            switch (flag) {
            case 0x4000:
                info_offset = read32(s);
                /*info_size =*/ read32(s);
                break;
            case 0x4001:
                avio_skip(s->pb, 4); // seek offset
                avio_skip(s->pb, 4); // seek size
                break;
            case 0x4002:
                start = read32(s) + 8;
                avio_skip(s->pb, 4); //data_size = read32(s);
                break;
            case 0x4003:
                avio_skip(s->pb, 4); // REGN offset
                avio_skip(s->pb, 4); // REGN size
                break;
            }
        }

        if (!info_offset || !start)
            return AVERROR_INVALIDDATA;

        avio_skip(s->pb, info_offset - avio_tell(s->pb));
        pos = avio_tell(s->pb);
        if (avio_rl32(s->pb) != MKTAG('I','N','F','O'))
            return AVERROR_INVALIDDATA;
    }

    size = read32(s);
    if (size < 192)
        return AVERROR_INVALIDDATA;
    avio_skip(s->pb, 4); // unknown
    h1offset = read32(s);
    if (h1offset > size)
        return AVERROR_INVALIDDATA;
    avio_skip(s->pb, 12);
    toffset = read32(s) + 16LL;
    if (toffset > size)
        return AVERROR_INVALIDDATA;

    avio_skip(s->pb, pos + h1offset + 8 - avio_tell(s->pb));
    codec = avio_r8(s->pb);

    switch (codec) {
    case 0: codec = AV_CODEC_ID_PCM_S8_PLANAR;    break;
    case 1: codec = b->little_endian ?
                    AV_CODEC_ID_PCM_S16LE_PLANAR :
                    AV_CODEC_ID_PCM_S16BE_PLANAR; break;
    case 2: codec = b->little_endian ?
                    AV_CODEC_ID_ADPCM_THP_LE :
                    AV_CODEC_ID_ADPCM_THP;        break;
    default:
        avpriv_request_sample(s, "codec %d", codec);
        return AVERROR_PATCHWELCOME;
    }

    loop = avio_r8(s->pb); // loop flag
    st->codec->codec_id = codec;
    st->codec->channels = avio_r8(s->pb);
    if (!st->codec->channels)
        return AVERROR_INVALIDDATA;

    avio_skip(s->pb, 1); // padding

    st->codec->sample_rate = bfstm ? read32(s) : read16(s);
    if (st->codec->sample_rate <= 0)
        return AVERROR_INVALIDDATA;

    if (!bfstm)
        avio_skip(s->pb, 2); // padding

    if (loop) {
        if (av_dict_set_int(&s->metadata, "loop_start",
                            av_rescale(read32(s), AV_TIME_BASE,
                                       st->codec->sample_rate),
                            0) < 0)
            return AVERROR(ENOMEM);
    } else {
        avio_skip(s->pb, 4);
    }

    st->start_time = 0;
    st->duration = read32(s);
    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

    if (!bfstm)
        start = read32(s);
    b->current_block = 0;
    b->block_count = read32(s);
    if (b->block_count > UINT16_MAX) {
        av_log(s, AV_LOG_WARNING, "too many blocks: %u\n", b->block_count);
        return AVERROR_INVALIDDATA;
    }

    b->block_size = read32(s);
    if (b->block_size > UINT32_MAX / st->codec->channels)
        return AVERROR_INVALIDDATA;

    b->samples_per_block = read32(s);
    b->last_block_used_bytes = read32(s);
    b->last_block_samples = read32(s);
    b->last_block_size = read32(s);
    if (b->last_block_size > UINT32_MAX / st->codec->channels)
        return AVERROR_INVALIDDATA;
    if (b->last_block_used_bytes > b->last_block_size)
        return AVERROR_INVALIDDATA;


    if (codec == AV_CODEC_ID_ADPCM_THP || codec == AV_CODEC_ID_ADPCM_THP_LE) {
        int ch;

        avio_skip(s->pb, pos + toffset - avio_tell(s->pb));
        if (!bfstm)
            toffset = read32(s) + 16LL;
        else
            toffset = toffset + read32(s) + st->codec->channels * 8 - 8;
        if (toffset > size)
            return AVERROR_INVALIDDATA;

        avio_skip(s->pb, pos + toffset - avio_tell(s->pb));
        b->table = av_mallocz(32 * st->codec->channels);
        if (!b->table)
            return AVERROR(ENOMEM);

        for (ch = 0; ch < st->codec->channels; ch++) {
            if (avio_read(s->pb, b->table + ch * 32, 32) != 32) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            avio_skip(s->pb, bfstm ? 14 : 24);
        }
    }

    if (size < (avio_tell(s->pb) - pos)) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    avio_skip(s->pb, size - (avio_tell(s->pb) - pos));

    while (!avio_feof(s->pb)) {
        chunk = avio_rl32(s->pb);
        size  = read32(s);
        if (size < 8) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        size -= 8;
        switch (chunk) {
        case MKTAG('S','E','E','K'):
        case MKTAG('A','D','P','C'):
            if (codec != AV_CODEC_ID_ADPCM_THP &&
                codec != AV_CODEC_ID_ADPCM_THP_LE)
                goto skip;

            asize = b->block_count * st->codec->channels * 4;
            if (size < asize) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            if (b->adpc) {
                av_log(s, AV_LOG_WARNING, "skipping additional ADPC chunk\n");
                goto skip;
            } else {
                b->adpc = av_mallocz(asize);
                if (!b->adpc) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                if (bfstm && codec != AV_CODEC_ID_ADPCM_THP_LE) {
                    // Big-endian BFSTMs have little-endian SEEK tables
                    // for some strange reason.
                    int i;
                    for (i = 0; i < asize; i += 2) {
                        b->adpc[i+1] = avio_r8(s->pb);
                        b->adpc[i]   = avio_r8(s->pb);
                    }
                } else {
                    avio_read(s->pb, b->adpc, asize);
                }
                avio_skip(s->pb, size - asize);
            }
            break;
        case MKTAG('D','A','T','A'):
            if ((start < avio_tell(s->pb)) ||
                (!b->adpc && (codec == AV_CODEC_ID_ADPCM_THP ||
                              codec == AV_CODEC_ID_ADPCM_THP_LE))) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            avio_skip(s->pb, start - avio_tell(s->pb));

            if (bfstm && (codec == AV_CODEC_ID_ADPCM_THP ||
                          codec == AV_CODEC_ID_ADPCM_THP_LE))
                avio_skip(s->pb, 24);

            b->data_start = avio_tell(s->pb);

            if (!bfstm && (major != 1 || minor))
                avpriv_request_sample(s, "Version %d.%d", major, minor);

            return 0;
        default:
            av_log(s, AV_LOG_WARNING, "skipping unknown chunk: %X\n", chunk);
skip:
            avio_skip(s->pb, size);
        }
    }

fail:
    read_close(s);

    return ret;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecContext *codec = s->streams[0]->codec;
    BRSTMDemuxContext *b = s->priv_data;
    uint32_t samples, size, skip = 0;
    int ret, i;

    if (avio_feof(s->pb))
        return AVERROR_EOF;
    b->current_block++;
    if (b->current_block == b->block_count) {
        size    = b->last_block_used_bytes;
        samples = b->last_block_samples;
        skip    = b->last_block_size - b->last_block_used_bytes;

        if (samples < size * 14 / 8) {
            uint32_t adjusted_size = samples / 14 * 8;
            if (samples % 14)
                adjusted_size += (samples % 14 + 1) / 2 + 1;

            skip += size - adjusted_size;
            size = adjusted_size;
        }
    } else if (b->current_block < b->block_count) {
        size    = b->block_size;
        samples = b->samples_per_block;
    } else {
        return AVERROR_EOF;
    }

    if (codec->codec_id == AV_CODEC_ID_ADPCM_THP ||
        codec->codec_id == AV_CODEC_ID_ADPCM_THP_LE) {
        uint8_t *dst;

        if (!b->adpc) {
            av_log(s, AV_LOG_ERROR, "adpcm_thp requires ADPC chunk, but none was found.\n");
            return AVERROR_INVALIDDATA;
        }
        if (!b->table) {
            b->table = av_mallocz(32 * codec->channels);
            if (!b->table)
                return AVERROR(ENOMEM);
        }

        if (size > (INT_MAX - 32 - 4) ||
            (32 + 4 + size) > (INT_MAX / codec->channels) ||
            (32 + 4 + size) * codec->channels > INT_MAX - 8)
            return AVERROR_INVALIDDATA;
        if (av_new_packet(pkt, 8 + (32 + 4 + size) * codec->channels) < 0)
            return AVERROR(ENOMEM);
        dst = pkt->data;
        if (codec->codec_id == AV_CODEC_ID_ADPCM_THP_LE) {
            bytestream_put_le32(&dst, size * codec->channels);
            bytestream_put_le32(&dst, samples);
        } else {
            bytestream_put_be32(&dst, size * codec->channels);
            bytestream_put_be32(&dst, samples);
        }
        bytestream_put_buffer(&dst, b->table, 32 * codec->channels);
        bytestream_put_buffer(&dst, b->adpc + 4 * codec->channels *
                                    (b->current_block - 1), 4 * codec->channels);

        for (i = 0; i < codec->channels; i++) {
            ret = avio_read(s->pb, dst, size);
            dst += size;
            avio_skip(s->pb, skip);
            if (ret != size) {
                av_packet_unref(pkt);
                break;
            }
        }
        pkt->duration = samples;
    } else {
        size *= codec->channels;
        ret = av_get_packet(s->pb, pkt, size);
    }

    pkt->stream_index = 0;

    if (ret != size)
        ret = AVERROR(EIO);

    return ret;
}

static int read_seek(AVFormatContext *s, int stream_index,
                     int64_t timestamp, int flags)
{
    AVStream *st = s->streams[stream_index];
    BRSTMDemuxContext *b = s->priv_data;
    int64_t ret = 0;

    timestamp /= b->samples_per_block;
    ret = avio_seek(s->pb, b->data_start + timestamp * b->block_size *
                           st->codec->channels, SEEK_SET);
    if (ret < 0)
        return ret;

    b->current_block = timestamp;
    ff_update_cur_dts(s, st, timestamp * b->samples_per_block);
    return 0;
}

AVInputFormat ff_brstm_demuxer = {
    .name           = "brstm",
    .long_name      = NULL_IF_CONFIG_SMALL("BRSTM (Binary Revolution Stream)"),
    .priv_data_size = sizeof(BRSTMDemuxContext),
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_close     = read_close,
    .read_seek      = read_seek,
    .extensions     = "brstm",
};

AVInputFormat ff_bfstm_demuxer = {
    .name           = "bfstm",
    .long_name      = NULL_IF_CONFIG_SMALL("BFSTM (Binary Cafe Stream)"),
    .priv_data_size = sizeof(BRSTMDemuxContext),
    .read_probe     = probe_bfstm,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_close     = read_close,
    .read_seek      = read_seek,
    .extensions     = "bfstm,bcstm",
};
