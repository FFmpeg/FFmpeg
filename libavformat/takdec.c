/*
 * Raw TAK demuxer
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

#include "libavutil/crc.h"

#define BITSTREAM_READER_LE
#include "libavcodec/tak.h"

#include "apetag.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "rawdec.h"

typedef struct TAKDemuxContext {
    int     mlast_frame;
    int64_t data_end;
} TAKDemuxContext;

static int tak_probe(AVProbeData *p)
{
    if (!memcmp(p->buf, "tBaK", 4))
        return AVPROBE_SCORE_EXTENSION;
    return 0;
}

static unsigned long tak_check_crc(unsigned long checksum, const uint8_t *buf,
                                   unsigned int len)
{
    return av_crc(av_crc_get_table(AV_CRC_24_IEEE), checksum, buf, len);
}

static int tak_read_header(AVFormatContext *s)
{
    TAKDemuxContext *tc = s->priv_data;
    AVIOContext *pb     = s->pb;
    GetBitContext gb;
    AVStream *st;
    uint8_t *buffer = NULL;
    int ret;

    st = avformat_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = AV_CODEC_ID_TAK;
    st->need_parsing         = AVSTREAM_PARSE_FULL_RAW;

    tc->mlast_frame = 0;
    if (avio_rl32(pb) != MKTAG('t', 'B', 'a', 'K')) {
        avio_seek(pb, -4, SEEK_CUR);
        return 0;
    }

    while (!avio_feof(pb)) {
        enum TAKMetaDataType type;
        int size;

        type = avio_r8(pb) & 0x7f;
        size = avio_rl24(pb);

        switch (type) {
        case TAK_METADATA_STREAMINFO:
        case TAK_METADATA_LAST_FRAME:
        case TAK_METADATA_ENCODER:
            if (size <= 3)
                return AVERROR_INVALIDDATA;

            buffer = av_malloc(size - 3 + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!buffer)
                return AVERROR(ENOMEM);
            memset(buffer + size - 3, 0, AV_INPUT_BUFFER_PADDING_SIZE);

            ffio_init_checksum(pb, tak_check_crc, 0xCE04B7U);
            if (avio_read(pb, buffer, size - 3) != size - 3) {
                av_freep(&buffer);
                return AVERROR(EIO);
            }
            if (ffio_get_checksum(s->pb) != avio_rb24(pb)) {
                av_log(s, AV_LOG_ERROR, "%d metadata block CRC error.\n", type);
                if (s->error_recognition & AV_EF_EXPLODE) {
                    av_freep(&buffer);
                    return AVERROR_INVALIDDATA;
                }
            }

            init_get_bits8(&gb, buffer, size - 3);
            break;
        case TAK_METADATA_MD5: {
            uint8_t md5[16];
            int i;

            if (size != 19)
                return AVERROR_INVALIDDATA;
            ffio_init_checksum(pb, tak_check_crc, 0xCE04B7U);
            avio_read(pb, md5, 16);
            if (ffio_get_checksum(s->pb) != avio_rb24(pb)) {
                av_log(s, AV_LOG_ERROR, "MD5 metadata block CRC error.\n");
                if (s->error_recognition & AV_EF_EXPLODE)
                    return AVERROR_INVALIDDATA;
            }

            av_log(s, AV_LOG_VERBOSE, "MD5=");
            for (i = 0; i < 16; i++)
                av_log(s, AV_LOG_VERBOSE, "%02x", md5[i]);
            av_log(s, AV_LOG_VERBOSE, "\n");
            break;
        }
        case TAK_METADATA_END: {
            int64_t curpos = avio_tell(pb);

            if (pb->seekable) {
                ff_ape_parse_tag(s);
                avio_seek(pb, curpos, SEEK_SET);
            }

            tc->data_end += curpos;
            return 0;
        }
        default:
            ret = avio_skip(pb, size);
            if (ret < 0)
                return ret;
        }

        if (type == TAK_METADATA_STREAMINFO) {
            TAKStreamInfo ti;

            avpriv_tak_parse_streaminfo(&gb, &ti);
            if (ti.samples > 0)
                st->duration = ti.samples;
            st->codecpar->bits_per_coded_sample = ti.bps;
            if (ti.ch_layout)
                st->codecpar->channel_layout = ti.ch_layout;
            st->codecpar->sample_rate           = ti.sample_rate;
            st->codecpar->channels              = ti.channels;
            st->start_time                   = 0;
            avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
            st->codecpar->extradata             = buffer;
            st->codecpar->extradata_size        = size - 3;
            buffer                           = NULL;
        } else if (type == TAK_METADATA_LAST_FRAME) {
            if (size != 11)
                return AVERROR_INVALIDDATA;
            tc->mlast_frame = 1;
            tc->data_end    = get_bits64(&gb, TAK_LAST_FRAME_POS_BITS) +
                              get_bits(&gb, TAK_LAST_FRAME_SIZE_BITS);
            av_freep(&buffer);
        } else if (type == TAK_METADATA_ENCODER) {
            av_log(s, AV_LOG_VERBOSE, "encoder version: %0X\n",
                   get_bits_long(&gb, TAK_ENCODER_VERSION_BITS));
            av_freep(&buffer);
        }
    }

    return AVERROR_EOF;
}

static int raw_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    TAKDemuxContext *tc = s->priv_data;
    int ret;

    if (tc->mlast_frame) {
        AVIOContext *pb = s->pb;
        int64_t size, left;

        left = tc->data_end - avio_tell(pb);
        size = FFMIN(left, 1024);
        if (size <= 0)
            return AVERROR_EOF;

        ret = av_get_packet(pb, pkt, size);
        if (ret < 0)
            return ret;

        pkt->stream_index = 0;
    } else {
        ret = ff_raw_read_partial_packet(s, pkt);
    }

    return ret;
}

AVInputFormat ff_tak_demuxer = {
    .name           = "tak",
    .long_name      = NULL_IF_CONFIG_SMALL("raw TAK"),
    .priv_data_size = sizeof(TAKDemuxContext),
    .read_probe     = tak_probe,
    .read_header    = tak_read_header,
    .read_packet    = raw_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "tak",
    .raw_codec_id   = AV_CODEC_ID_TAK,
};
