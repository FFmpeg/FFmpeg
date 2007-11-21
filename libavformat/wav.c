/*
 * WAV muxer and demuxer
 * Copyright (c) 2001, 2002 Fabrice Bellard.
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
#include "raw.h"
#include "riff.h"

typedef struct {
    offset_t data;
    offset_t data_end;
    int64_t minpts;
    int64_t maxpts;
    int last_duration;
} WAVContext;

#ifdef CONFIG_MUXERS
static int wav_write_header(AVFormatContext *s)
{
    WAVContext *wav = s->priv_data;
    ByteIOContext *pb = s->pb;
    offset_t fmt, fact;

    put_tag(pb, "RIFF");
    put_le32(pb, 0); /* file length */
    put_tag(pb, "WAVE");

    /* format header */
    fmt = start_tag(pb, "fmt ");
    if (put_wav_header(pb, s->streams[0]->codec) < 0) {
        av_free(wav);
        return -1;
    }
    end_tag(pb, fmt);

    if(s->streams[0]->codec->codec_tag != 0x01 /* hence for all other than PCM */
       && !url_is_streamed(s->pb)) {
        fact = start_tag(pb, "fact");
        put_le32(pb, 0);
        end_tag(pb, fact);
    }

    av_set_pts_info(s->streams[0], 64, 1, s->streams[0]->codec->sample_rate);
    wav->maxpts = wav->last_duration = 0;
    wav->minpts = INT64_MAX;

    /* data header */
    wav->data = start_tag(pb, "data");

    put_flush_packet(pb);

    return 0;
}

static int wav_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    WAVContext *wav = s->priv_data;
    put_buffer(pb, pkt->data, pkt->size);
    if(pkt->pts != AV_NOPTS_VALUE) {
        wav->minpts = FFMIN(wav->minpts, pkt->pts);
        wav->maxpts = FFMAX(wav->maxpts, pkt->pts);
        wav->last_duration = pkt->duration;
    } else
        av_log(s, AV_LOG_ERROR, "wav_write_packet: NOPTS\n");
    return 0;
}

static int wav_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    WAVContext *wav = s->priv_data;
    offset_t file_size;

    if (!url_is_streamed(s->pb)) {
        end_tag(pb, wav->data);

        /* update file size */
        file_size = url_ftell(pb);
        url_fseek(pb, 4, SEEK_SET);
        put_le32(pb, (uint32_t)(file_size - 8));
        url_fseek(pb, file_size, SEEK_SET);

        put_flush_packet(pb);

        if(s->streams[0]->codec->codec_tag != 0x01) {
            /* Update num_samps in fact chunk */
            int number_of_samples;
            number_of_samples = av_rescale(wav->maxpts - wav->minpts + wav->last_duration,
                                           s->streams[0]->codec->sample_rate * (int64_t)s->streams[0]->time_base.num,
                                           s->streams[0]->time_base.den);
            url_fseek(pb, wav->data-12, SEEK_SET);
            put_le32(pb, number_of_samples);
            url_fseek(pb, file_size, SEEK_SET);
            put_flush_packet(pb);
        }
    }
    return 0;
}
#endif //CONFIG_MUXERS

/* return the size of the found tag */
/* XXX: > 2GB ? */
static int find_tag(ByteIOContext *pb, uint32_t tag1)
{
    unsigned int tag;
    int size;

    for(;;) {
        if (url_feof(pb))
            return -1;
        tag = get_le32(pb);
        size = get_le32(pb);
        if (tag == tag1)
            break;
        url_fseek(pb, size, SEEK_CUR);
    }
    if (size < 0)
        size = 0x7fffffff;
    return size;
}

static int wav_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 32)
        return 0;
    if (p->buf[0] == 'R' && p->buf[1] == 'I' &&
        p->buf[2] == 'F' && p->buf[3] == 'F' &&
        p->buf[8] == 'W' && p->buf[9] == 'A' &&
        p->buf[10] == 'V' && p->buf[11] == 'E')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

/* wav input */
static int wav_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    int size;
    unsigned int tag;
    ByteIOContext *pb = s->pb;
    AVStream *st;
    WAVContext *wav = s->priv_data;

    /* check RIFF header */
    tag = get_le32(pb);

    if (tag != MKTAG('R', 'I', 'F', 'F'))
        return -1;
    get_le32(pb); /* file size */
    tag = get_le32(pb);
    if (tag != MKTAG('W', 'A', 'V', 'E'))
        return -1;

    /* parse fmt header */
    size = find_tag(pb, MKTAG('f', 'm', 't', ' '));
    if (size < 0)
        return -1;
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    get_wav_header(pb, st->codec, size);
    st->need_parsing = AVSTREAM_PARSE_FULL;

    av_set_pts_info(st, 64, 1, st->codec->sample_rate);

    size = find_tag(pb, MKTAG('d', 'a', 't', 'a'));
    if (size < 0)
        return -1;
    wav->data_end= url_ftell(pb) + size;
    return 0;
}

#define MAX_SIZE 4096

static int wav_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    int ret, size, left;
    AVStream *st;
    WAVContext *wav = s->priv_data;

    if (url_feof(s->pb))
        return AVERROR(EIO);
    st = s->streams[0];

    left= wav->data_end - url_ftell(s->pb);
    if(left <= 0){
        left = find_tag(s->pb, MKTAG('d', 'a', 't', 'a'));
        if (left < 0) {
            return AVERROR(EIO);
        }
        wav->data_end= url_ftell(s->pb) + left;
    }

    size = MAX_SIZE;
    if (st->codec->block_align > 1) {
        if (size < st->codec->block_align)
            size = st->codec->block_align;
        size = (size / st->codec->block_align) * st->codec->block_align;
    }
    size= FFMIN(size, left);
    ret= av_get_packet(s->pb, pkt, size);
    if (ret <= 0)
        return AVERROR(EIO);
    pkt->stream_index = 0;

    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

static int wav_read_close(AVFormatContext *s)
{
    return 0;
}

static int wav_read_seek(AVFormatContext *s,
                         int stream_index, int64_t timestamp, int flags)
{
    AVStream *st;

    st = s->streams[0];
    switch(st->codec->codec_id) {
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
    case CODEC_ID_AC3:
    case CODEC_ID_DTS:
        /* use generic seeking with dynamically generated indexes */
        return -1;
    default:
        break;
    }
    return pcm_read_seek(s, stream_index, timestamp, flags);
}

#ifdef CONFIG_WAV_DEMUXER
AVInputFormat wav_demuxer = {
    "wav",
    "wav format",
    sizeof(WAVContext),
    wav_probe,
    wav_read_header,
    wav_read_packet,
    wav_read_close,
    wav_read_seek,
    .flags= AVFMT_GENERIC_INDEX,
    .codec_tag= (const AVCodecTag*[]){codec_wav_tags, 0},
};
#endif
#ifdef CONFIG_WAV_MUXER
AVOutputFormat wav_muxer = {
    "wav",
    "wav format",
    "audio/x-wav",
    "wav",
    sizeof(WAVContext),
    CODEC_ID_PCM_S16LE,
    CODEC_ID_NONE,
    wav_write_header,
    wav_write_packet,
    wav_write_trailer,
    .codec_tag= (const AVCodecTag*[]){codec_wav_tags, 0},
};
#endif
