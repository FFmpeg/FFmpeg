/*
 * VQF demuxer
 * Copyright (c) 2009 Vitor Sessak
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
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "riff.h"

typedef struct VqfContext {
    int frame_bit_len;
    uint8_t last_frame_bits;
    int remaining_bits;
} VqfContext;

static int vqf_probe(AVProbeData *probe_packet)
{
    if (AV_RL32(probe_packet->buf) != MKTAG('T','W','I','N'))
        return 0;

    if (!memcmp(probe_packet->buf + 4, "97012000", 8))
        return AVPROBE_SCORE_MAX;

    if (!memcmp(probe_packet->buf + 4, "00052200", 8))
        return AVPROBE_SCORE_MAX;

    if (AV_RL32(probe_packet->buf + 12) > (1<<27))
        return AVPROBE_SCORE_EXTENSION/2;

    return AVPROBE_SCORE_EXTENSION;
}

static void add_metadata(AVFormatContext *s, uint32_t tag,
                         unsigned int tag_len, unsigned int remaining)
{
    int len = FFMIN(tag_len, remaining);
    char *buf, key[5] = {0};

    if (len == UINT_MAX)
        return;

    buf = av_malloc(len+1);
    if (!buf)
        return;
    avio_read(s->pb, buf, len);
    buf[len] = 0;
    AV_WL32(key, tag);
    av_dict_set(&s->metadata, key, buf, AV_DICT_DONT_STRDUP_VAL);
}

static const AVMetadataConv vqf_metadata_conv[] = {
    { "(c) ", "copyright" },
    { "ARNG", "arranger"  },
    { "AUTH", "author"    },
    { "BAND", "band"      },
    { "CDCT", "conductor" },
    { "COMT", "comment"   },
    { "FILE", "filename"  },
    { "GENR", "genre"     },
    { "LABL", "publisher" },
    { "MUSC", "composer"  },
    { "NAME", "title"     },
    { "NOTE", "note"      },
    { "PROD", "producer"  },
    { "PRSN", "personnel" },
    { "REMX", "remixer"   },
    { "SING", "singer"    },
    { "TRCK", "track"     },
    { "WORD", "words"     },
    { 0 },
};

static int vqf_read_header(AVFormatContext *s)
{
    VqfContext *c = s->priv_data;
    AVStream *st  = avformat_new_stream(s, NULL);
    int chunk_tag;
    int rate_flag = -1;
    int header_size;
    int read_bitrate = 0;
    int size;
    uint8_t comm_chunk[12];

    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(s->pb, 12);

    header_size = avio_rb32(s->pb);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = AV_CODEC_ID_TWINVQ;
    st->start_time = 0;

    do {
        int len;
        chunk_tag = avio_rl32(s->pb);

        if (chunk_tag == MKTAG('D','A','T','A'))
            break;

        len = avio_rb32(s->pb);

        if ((unsigned) len > INT_MAX/2) {
            av_log(s, AV_LOG_ERROR, "Malformed header\n");
            return -1;
        }

        header_size -= 8;

        switch(chunk_tag){
        case MKTAG('C','O','M','M'):
            avio_read(s->pb, comm_chunk, 12);
            st->codecpar->channels = AV_RB32(comm_chunk    ) + 1;
            read_bitrate        = AV_RB32(comm_chunk + 4);
            rate_flag           = AV_RB32(comm_chunk + 8);
            avio_skip(s->pb, len-12);

            if (st->codecpar->channels <= 0) {
                av_log(s, AV_LOG_ERROR, "Invalid number of channels\n");
                return AVERROR_INVALIDDATA;
            }

            st->codecpar->bit_rate = (int64_t)read_bitrate * 1000;
            break;
        case MKTAG('D','S','I','Z'): // size of compressed data
        {
            av_dict_set_int(&s->metadata, "size", avio_rb32(s->pb), 0);
        }
            break;
        case MKTAG('Y','E','A','R'): // recording date
        case MKTAG('E','N','C','D'): // compression date
        case MKTAG('E','X','T','R'): // reserved
        case MKTAG('_','Y','M','H'): // reserved
        case MKTAG('_','N','T','T'): // reserved
        case MKTAG('_','I','D','3'): // reserved for ID3 tags
            avio_skip(s->pb, FFMIN(len, header_size));
            break;
        default:
            add_metadata(s, chunk_tag, len, header_size);
            break;
        }

        header_size -= len;

    } while (header_size >= 0 && !avio_feof(s->pb));

    switch (rate_flag) {
    case -1:
        av_log(s, AV_LOG_ERROR, "COMM tag not found!\n");
        return -1;
    case 44:
        st->codecpar->sample_rate = 44100;
        break;
    case 22:
        st->codecpar->sample_rate = 22050;
        break;
    case 11:
        st->codecpar->sample_rate = 11025;
        break;
    default:
        if (rate_flag < 8 || rate_flag > 44) {
            av_log(s, AV_LOG_ERROR, "Invalid rate flag %d\n", rate_flag);
            return AVERROR_INVALIDDATA;
        }
        st->codecpar->sample_rate = rate_flag*1000;
        break;
    }

    if (read_bitrate / st->codecpar->channels <  8 ||
        read_bitrate / st->codecpar->channels > 48) {
        av_log(s, AV_LOG_ERROR, "Invalid bitrate per channel %d\n",
               read_bitrate / st->codecpar->channels);
        return AVERROR_INVALIDDATA;
    }

    switch (((st->codecpar->sample_rate/1000) << 8) +
            read_bitrate/st->codecpar->channels) {
    case (11<<8) + 8 :
    case (8 <<8) + 8 :
    case (11<<8) + 10:
    case (22<<8) + 32:
        size = 512;
        break;
    case (16<<8) + 16:
    case (22<<8) + 20:
    case (22<<8) + 24:
        size = 1024;
        break;
    case (44<<8) + 40:
    case (44<<8) + 48:
        size = 2048;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "Mode not supported: %d Hz, %"PRId64" kb/s.\n",
               st->codecpar->sample_rate, (int64_t)st->codecpar->bit_rate);
        return -1;
    }
    c->frame_bit_len = st->codecpar->bit_rate*size/st->codecpar->sample_rate;
    avpriv_set_pts_info(st, 64, size, st->codecpar->sample_rate);

    /* put first 12 bytes of COMM chunk in extradata */
    if (ff_alloc_extradata(st->codecpar, 12))
        return AVERROR(ENOMEM);
    memcpy(st->codecpar->extradata, comm_chunk, 12);

    ff_metadata_conv_ctx(s, NULL, vqf_metadata_conv);

    return 0;
}

static int vqf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    VqfContext *c = s->priv_data;
    int ret;
    int size = (c->frame_bit_len - c->remaining_bits + 7)>>3;

    if (av_new_packet(pkt, size+2) < 0)
        return AVERROR(EIO);

    pkt->pos          = avio_tell(s->pb);
    pkt->stream_index = 0;
    pkt->duration     = 1;

    pkt->data[0] = 8 - c->remaining_bits; // Number of bits to skip
    pkt->data[1] = c->last_frame_bits;
    ret = avio_read(s->pb, pkt->data+2, size);

    if (ret != size) {
        av_packet_unref(pkt);
        return AVERROR(EIO);
    }

    c->last_frame_bits = pkt->data[size+1];
    c->remaining_bits  = (size << 3) - c->frame_bit_len + c->remaining_bits;

    return size+2;
}

static int vqf_read_seek(AVFormatContext *s,
                         int stream_index, int64_t timestamp, int flags)
{
    VqfContext *c = s->priv_data;
    AVStream *st;
    int64_t ret;
    int64_t pos;

    st = s->streams[stream_index];
    pos = av_rescale_rnd(timestamp * st->codecpar->bit_rate,
                         st->time_base.num,
                         st->time_base.den * (int64_t)c->frame_bit_len,
                         (flags & AVSEEK_FLAG_BACKWARD) ?
                                                   AV_ROUND_DOWN : AV_ROUND_UP);
    pos *= c->frame_bit_len;

    st->cur_dts = av_rescale(pos, st->time_base.den,
                             st->codecpar->bit_rate * (int64_t)st->time_base.num);

    if ((ret = avio_seek(s->pb, ((pos-7) >> 3) + s->internal->data_offset, SEEK_SET)) < 0)
        return ret;

    c->remaining_bits = -7 - ((pos-7)&7);
    return 0;
}

AVInputFormat ff_vqf_demuxer = {
    .name           = "vqf",
    .long_name      = NULL_IF_CONFIG_SMALL("Nippon Telegraph and Telephone Corporation (NTT) TwinVQ"),
    .priv_data_size = sizeof(VqfContext),
    .read_probe     = vqf_probe,
    .read_header    = vqf_read_header,
    .read_packet    = vqf_read_packet,
    .read_seek      = vqf_read_seek,
    .extensions     = "vqf,vql,vqe",
};
