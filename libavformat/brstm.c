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
    uint8_t     *table;
    uint8_t     *adpc;
} BRSTMDemuxContext;

static int probe(AVProbeData *p)
{
    if (AV_RL32(p->buf) == MKTAG('R','S','T','M') &&
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

static int read_header(AVFormatContext *s)
{
    BRSTMDemuxContext *b = s->priv_data;
    int bom, major, minor, codec, chunk;
    int64_t pos, h1offset, toffset;
    uint32_t size, start, asize;
    AVStream *st;
    int ret = AVERROR_EOF;

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
    if (bom == 0xFFFE) {
        avpriv_request_sample(s, "little endian byte order");
        return AVERROR_PATCHWELCOME;
    }

    major = avio_r8(s->pb);
    minor = avio_r8(s->pb);
    avio_skip(s->pb, 4); // size of file
    size = avio_rb16(s->pb);
    if (size < 14)
        return AVERROR_INVALIDDATA;

    avio_skip(s->pb, size - 14);
    pos = avio_tell(s->pb);
    if (avio_rl32(s->pb) != MKTAG('H','E','A','D'))
        return AVERROR_INVALIDDATA;
    size = avio_rb32(s->pb);
    if (size < 256)
        return AVERROR_INVALIDDATA;
    avio_skip(s->pb, 4); // unknown
    h1offset = avio_rb32(s->pb);
    if (h1offset > size)
        return AVERROR_INVALIDDATA;
    avio_skip(s->pb, 12);
    toffset = avio_rb32(s->pb) + 16LL;
    if (toffset > size)
        return AVERROR_INVALIDDATA;

    avio_skip(s->pb, pos + h1offset + 8 - avio_tell(s->pb));
    codec = avio_r8(s->pb);

    switch (codec) {
    case 0: codec = AV_CODEC_ID_PCM_S8_PLANAR;    break;
    case 1: codec = AV_CODEC_ID_PCM_S16BE_PLANAR; break;
    case 2: codec = AV_CODEC_ID_ADPCM_THP;        break;
    default:
        avpriv_request_sample(s, "codec %d", codec);
        return AVERROR_PATCHWELCOME;
    }

    avio_skip(s->pb, 1); // loop flag
    st->codec->codec_id = codec;
    st->codec->channels = avio_r8(s->pb);
    if (!st->codec->channels)
        return AVERROR_INVALIDDATA;

    avio_skip(s->pb, 1); // padding
    st->codec->sample_rate = avio_rb16(s->pb);
    if (!st->codec->sample_rate)
        return AVERROR_INVALIDDATA;

    avio_skip(s->pb, 2); // padding
    avio_skip(s->pb, 4); // loop start sample
    st->start_time = 0;
    st->duration = avio_rb32(s->pb);
    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

    start = avio_rb32(s->pb);
    b->current_block = 0;
    b->block_count = avio_rb32(s->pb);
    if (b->block_count > UINT16_MAX) {
        av_log(s, AV_LOG_WARNING, "too many blocks: %u\n", b->block_count);
        return AVERROR_INVALIDDATA;
    }

    b->block_size = avio_rb32(s->pb);
    if (b->block_size > UINT16_MAX / st->codec->channels)
        return AVERROR_INVALIDDATA;
    b->block_size *= st->codec->channels;

    b->samples_per_block = avio_rb32(s->pb);
    b->last_block_used_bytes = avio_rb32(s->pb);
    if (b->last_block_used_bytes > UINT16_MAX / st->codec->channels)
        return AVERROR_INVALIDDATA;
    b->last_block_used_bytes *= st->codec->channels;

    avio_skip(s->pb, 4); // last block samples
    avio_skip(s->pb, 4); // last block size

    if (codec == AV_CODEC_ID_ADPCM_THP) {
        int ch;

        avio_skip(s->pb, pos + toffset - avio_tell(s->pb));
        toffset = avio_rb32(s->pb) + 16LL;
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
            avio_skip(s->pb, 24);
        }
    }

    if (size < (avio_tell(s->pb) - pos)) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }
    avio_skip(s->pb, size - (avio_tell(s->pb) - pos));

    while (!url_feof(s->pb)) {
        chunk = avio_rl32(s->pb);
        size  = avio_rb32(s->pb);
        if (size < 8) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        size -= 8;
        switch (chunk) {
        case MKTAG('A','D','P','C'):
            if (codec != AV_CODEC_ID_ADPCM_THP)
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
                avio_read(s->pb, b->adpc, asize);
                avio_skip(s->pb, size - asize);
            }
            break;
        case MKTAG('D','A','T','A'):
            if ((start < avio_tell(s->pb)) ||
                (!b->adpc && codec == AV_CODEC_ID_ADPCM_THP)) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            avio_skip(s->pb, start - avio_tell(s->pb));

            if (major != 1 || minor)
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
    uint32_t samples, size;
    int ret;

    if (url_feof(s->pb))
        return AVERROR_EOF;
    b->current_block++;
    if (b->current_block == b->block_count) {
        size    = b->last_block_used_bytes;
        samples = size / (8 * codec->channels) * 14;
    } else if (b->current_block < b->block_count) {
        size    = b->block_size;
        samples = b->samples_per_block;
    } else {
        return AVERROR_EOF;
    }

    if (codec->codec_id == AV_CODEC_ID_ADPCM_THP) {
        uint8_t *dst;

        if (av_new_packet(pkt, 8 + (32 + 4) * codec->channels + size) < 0)
            return AVERROR(ENOMEM);
        dst = pkt->data;
        bytestream_put_be32(&dst, size);
        bytestream_put_be32(&dst, samples);
        bytestream_put_buffer(&dst, b->table, 32 * codec->channels);
        bytestream_put_buffer(&dst, b->adpc + 4 * codec->channels *
                                    (b->current_block - 1), 4 * codec->channels);

        ret = avio_read(s->pb, dst, size);
        if (ret != size)
            av_free_packet(pkt);
        pkt->duration = samples;
    } else {
        ret = av_get_packet(s->pb, pkt, size);
    }

    pkt->stream_index = 0;

    if (ret != size)
        ret = AVERROR(EIO);

    return ret;
}

AVInputFormat ff_brstm_demuxer = {
    .name           = "brstm",
    .long_name      = NULL_IF_CONFIG_SMALL("BRSTM (Binary Revolution Stream)"),
    .priv_data_size = sizeof(BRSTMDemuxContext),
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_close     = read_close,
    .extensions     = "brstm",
};
