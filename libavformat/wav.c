/*
 * WAV muxer and demuxer
 * Copyright (c) 2001, 2002 Fabrice Bellard
 *
 * Sony Wave64 demuxer
 * RF64 demuxer
 * Copyright (c) 2009 Daniel Verkamp
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
#include "pcm.h"
#include "riff.h"

typedef struct {
    int64_t data;
    int64_t data_end;
    int64_t minpts;
    int64_t maxpts;
    int last_duration;
    int w64;
} WAVContext;

#if CONFIG_WAV_MUXER
static int wav_write_header(AVFormatContext *s)
{
    WAVContext *wav = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t fmt, fact;

    put_tag(pb, "RIFF");
    put_le32(pb, 0); /* file length */
    put_tag(pb, "WAVE");

    /* format header */
    fmt = ff_start_tag(pb, "fmt ");
    if (ff_put_wav_header(pb, s->streams[0]->codec) < 0) {
        av_log(s, AV_LOG_ERROR, "%s codec not supported in WAVE format\n",
               s->streams[0]->codec->codec ? s->streams[0]->codec->codec->name : "NONE");
        av_free(wav);
        return -1;
    }
    ff_end_tag(pb, fmt);

    if (s->streams[0]->codec->codec_tag != 0x01 /* hence for all other than PCM */
        && !url_is_streamed(s->pb)) {
        fact = ff_start_tag(pb, "fact");
        put_le32(pb, 0);
        ff_end_tag(pb, fact);
    }

    av_set_pts_info(s->streams[0], 64, 1, s->streams[0]->codec->sample_rate);
    wav->maxpts = wav->last_duration = 0;
    wav->minpts = INT64_MAX;

    /* data header */
    wav->data = ff_start_tag(pb, "data");

    put_flush_packet(pb);

    return 0;
}

static int wav_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb  = s->pb;
    WAVContext    *wav = s->priv_data;
    put_buffer(pb, pkt->data, pkt->size);
    if(pkt->pts != AV_NOPTS_VALUE) {
        wav->minpts        = FFMIN(wav->minpts, pkt->pts);
        wav->maxpts        = FFMAX(wav->maxpts, pkt->pts);
        wav->last_duration = pkt->duration;
    } else
        av_log(s, AV_LOG_ERROR, "wav_write_packet: NOPTS\n");
    return 0;
}

static int wav_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb  = s->pb;
    WAVContext    *wav = s->priv_data;
    int64_t file_size;

    put_flush_packet(pb);

    if (!url_is_streamed(s->pb)) {
        ff_end_tag(pb, wav->data);

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

AVOutputFormat wav_muxer = {
    "wav",
    NULL_IF_CONFIG_SMALL("WAV format"),
    "audio/x-wav",
    "wav",
    sizeof(WAVContext),
    CODEC_ID_PCM_S16LE,
    CODEC_ID_NONE,
    wav_write_header,
    wav_write_packet,
    wav_write_trailer,
    .codec_tag= (const AVCodecTag* const []){ff_codec_wav_tags, 0},
};
#endif /* CONFIG_WAV_MUXER */


#if CONFIG_WAV_DEMUXER

static int64_t next_tag(ByteIOContext *pb, unsigned int *tag)
{
    *tag = get_le32(pb);
    return get_le32(pb);
}

/* return the size of the found tag */
static int64_t find_tag(ByteIOContext *pb, uint32_t tag1)
{
    unsigned int tag;
    int64_t size;

    for (;;) {
        if (url_feof(pb))
            return -1;
        size = next_tag(pb, &tag);
        if (tag == tag1)
            break;
        url_fseek(pb, size, SEEK_CUR);
    }
    return size;
}

static int wav_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 32)
        return 0;
    if (!memcmp(p->buf + 8, "WAVE", 4)) {
        if (!memcmp(p->buf, "RIFF", 4))
            /*
              Since ACT demuxer has standard WAV header at top of it's own,
              returning score is decreased to avoid probe conflict
              between ACT and WAV.
            */
            return AVPROBE_SCORE_MAX - 1;
        else if (!memcmp(p->buf,      "RF64", 4) &&
                 !memcmp(p->buf + 12, "ds64", 4))
            return AVPROBE_SCORE_MAX;
    }
    return 0;
}

/* wav input */
static int wav_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    int64_t size, av_uninit(data_size);
    int64_t sample_count=0;
    int rf64;
    unsigned int tag;
    ByteIOContext *pb = s->pb;
    AVStream *st;
    WAVContext *wav = s->priv_data;

    /* check RIFF header */
    tag = get_le32(pb);

    rf64 = tag == MKTAG('R', 'F', '6', '4');
    if (!rf64 && tag != MKTAG('R', 'I', 'F', 'F'))
        return -1;
    get_le32(pb); /* file size */
    tag = get_le32(pb);
    if (tag != MKTAG('W', 'A', 'V', 'E'))
        return -1;

    if (rf64) {
        if (get_le32(pb) != MKTAG('d', 's', '6', '4'))
            return -1;
        size = get_le32(pb);
        if (size < 16)
            return -1;
        get_le64(pb); /* RIFF size */
        data_size = get_le64(pb);
        sample_count = get_le64(pb);
        url_fskip(pb, size - 16); /* skip rest of ds64 chunk */
    }

    /* parse fmt header */
    size = find_tag(pb, MKTAG('f', 'm', 't', ' '));
    if (size < 0)
        return -1;
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    ff_get_wav_header(pb, st->codec, size);
    st->need_parsing = AVSTREAM_PARSE_FULL;

    av_set_pts_info(st, 64, 1, st->codec->sample_rate);

    for (;;) {
        if (url_feof(pb))
            return -1;
        size = next_tag(pb, &tag);
        if (tag == MKTAG('d', 'a', 't', 'a')){
            break;
        }else if (tag == MKTAG('f','a','c','t') && !sample_count){
            sample_count = get_le32(pb);
            size -= 4;
        }
        url_fseek(pb, size, SEEK_CUR);
    }
    if (rf64)
        size = data_size;
    if (size < 0)
        return -1;
    if (!size) {
        wav->data_end = INT64_MAX;
    } else
        wav->data_end= url_ftell(pb) + size;

    if (!sample_count && st->codec->channels && av_get_bits_per_sample(st->codec->codec_id))
        sample_count = (size<<3) / (st->codec->channels * (uint64_t)av_get_bits_per_sample(st->codec->codec_id));
    if (sample_count)
        st->duration = sample_count;
    return 0;
}

/** Find chunk with w64 GUID by skipping over other chunks
 * @return the size of the found chunk
 */
static int64_t find_guid(ByteIOContext *pb, const uint8_t guid1[16])
{
    uint8_t guid[16];
    int64_t size;

    while (!url_feof(pb)) {
        get_buffer(pb, guid, 16);
        size = get_le64(pb);
        if (size <= 24)
            return -1;
        if (!memcmp(guid, guid1, 16))
            return size;
        url_fskip(pb, FFALIGN(size, INT64_C(8)) - 24);
    }
    return -1;
}

static const uint8_t guid_data[16] = { 'd', 'a', 't', 'a',
    0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };

#define MAX_SIZE 4096

static int wav_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    int ret, size;
    int64_t left;
    AVStream *st;
    WAVContext *wav = s->priv_data;

    st = s->streams[0];

    left = wav->data_end - url_ftell(s->pb);
    if (left <= 0){
        if (CONFIG_W64_DEMUXER && wav->w64)
            left = find_guid(s->pb, guid_data) - 24;
        else
            left = find_tag(s->pb, MKTAG('d', 'a', 't', 'a'));
        if (left < 0)
            return AVERROR_EOF;
        wav->data_end= url_ftell(s->pb) + left;
    }

    size = MAX_SIZE;
    if (st->codec->block_align > 1) {
        if (size < st->codec->block_align)
            size = st->codec->block_align;
        size = (size / st->codec->block_align) * st->codec->block_align;
    }
    size = FFMIN(size, left);
    ret  = av_get_packet(s->pb, pkt, size);
    if (ret < 0)
        return ret;
    pkt->stream_index = 0;

    return ret;
}

static int wav_read_seek(AVFormatContext *s,
                         int stream_index, int64_t timestamp, int flags)
{
    AVStream *st;

    st = s->streams[0];
    switch (st->codec->codec_id) {
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

AVInputFormat wav_demuxer = {
    "wav",
    NULL_IF_CONFIG_SMALL("WAV format"),
    sizeof(WAVContext),
    wav_probe,
    wav_read_header,
    wav_read_packet,
    NULL,
    wav_read_seek,
    .flags= AVFMT_GENERIC_INDEX,
    .codec_tag= (const AVCodecTag* const []){ff_codec_wav_tags, 0},
};
#endif /* CONFIG_WAV_DEMUXER */


#if CONFIG_W64_DEMUXER
static const uint8_t guid_riff[16] = { 'r', 'i', 'f', 'f',
    0x2E, 0x91, 0xCF, 0x11, 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00 };

static const uint8_t guid_wave[16] = { 'w', 'a', 'v', 'e',
    0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };

static const uint8_t guid_fmt [16] = { 'f', 'm', 't', ' ',
    0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };

static int w64_probe(AVProbeData *p)
{
    if (p->buf_size <= 40)
        return 0;
    if (!memcmp(p->buf,      guid_riff, 16) &&
        !memcmp(p->buf + 24, guid_wave, 16))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int w64_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    int64_t size;
    ByteIOContext *pb  = s->pb;
    WAVContext    *wav = s->priv_data;
    AVStream *st;
    uint8_t guid[16];

    get_buffer(pb, guid, 16);
    if (memcmp(guid, guid_riff, 16))
        return -1;

    if (get_le64(pb) < 16 + 8 + 16 + 8 + 16 + 8) /* riff + wave + fmt + sizes */
        return -1;

    get_buffer(pb, guid, 16);
    if (memcmp(guid, guid_wave, 16)) {
        av_log(s, AV_LOG_ERROR, "could not find wave guid\n");
        return -1;
    }

    size = find_guid(pb, guid_fmt);
    if (size < 0) {
        av_log(s, AV_LOG_ERROR, "could not find fmt guid\n");
        return -1;
    }

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    /* subtract chunk header size - normal wav file doesn't count it */
    ff_get_wav_header(pb, st->codec, size - 24);
    url_fskip(pb, FFALIGN(size, INT64_C(8)) - size);

    st->need_parsing = AVSTREAM_PARSE_FULL;

    av_set_pts_info(st, 64, 1, st->codec->sample_rate);

    size = find_guid(pb, guid_data);
    if (size < 0) {
        av_log(s, AV_LOG_ERROR, "could not find data guid\n");
        return -1;
    }
    wav->data_end = url_ftell(pb) + size - 24;
    wav->w64      = 1;

    return 0;
}

AVInputFormat w64_demuxer = {
    "w64",
    NULL_IF_CONFIG_SMALL("Sony Wave64 format"),
    sizeof(WAVContext),
    w64_probe,
    w64_read_header,
    wav_read_packet,
    NULL,
    wav_read_seek,
    .flags = AVFMT_GENERIC_INDEX,
    .codec_tag = (const AVCodecTag* const []){ff_codec_wav_tags, 0},
};
#endif /* CONFIG_W64_DEMUXER */
