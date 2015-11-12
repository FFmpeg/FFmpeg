/*
 * Sega FILM Format (CPK) Demuxer
 * Copyright (c) 2003 The FFmpeg Project
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

/**
 * @file
 * Sega FILM (.cpk) file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the Sega FILM file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"

#define FILM_TAG MKBETAG('F', 'I', 'L', 'M')
#define FDSC_TAG MKBETAG('F', 'D', 'S', 'C')
#define STAB_TAG MKBETAG('S', 'T', 'A', 'B')
#define CVID_TAG MKBETAG('c', 'v', 'i', 'd')
#define RAW_TAG  MKBETAG('r', 'a', 'w', ' ')

typedef struct film_sample {
  int stream;
  int64_t sample_offset;
  unsigned int sample_size;
  int64_t pts;
  int keyframe;
} film_sample;

typedef struct FilmDemuxContext {
    int video_stream_index;
    int audio_stream_index;

    enum AVCodecID audio_type;
    unsigned int audio_samplerate;
    unsigned int audio_bits;
    unsigned int audio_channels;

    enum AVCodecID video_type;
    unsigned int sample_count;
    film_sample *sample_table;
    unsigned int current_sample;

    unsigned int base_clock;
    unsigned int version;
} FilmDemuxContext;

static int film_probe(AVProbeData *p)
{
    if (AV_RB32(&p->buf[0]) != FILM_TAG)
        return 0;

    if (AV_RB32(&p->buf[16]) != FDSC_TAG)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int film_read_close(AVFormatContext *s)
{
    FilmDemuxContext *film = s->priv_data;

    av_freep(&film->sample_table);

    return 0;
}

static int film_read_header(AVFormatContext *s)
{
    FilmDemuxContext *film = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    unsigned char scratch[256];
    int i, ret;
    unsigned int data_offset;
    unsigned int audio_frame_counter;
    unsigned int video_frame_counter;

    film->sample_table = NULL;

    /* load the main FILM header */
    if (avio_read(pb, scratch, 16) != 16)
        return AVERROR(EIO);
    data_offset = AV_RB32(&scratch[4]);
    film->version = AV_RB32(&scratch[8]);

    /* load the FDSC chunk */
    if (film->version == 0) {
        /* special case for Lemmings .film files; 20-byte header */
        if (avio_read(pb, scratch, 20) != 20)
            return AVERROR(EIO);
        /* make some assumptions about the audio parameters */
        film->audio_type = AV_CODEC_ID_PCM_S8;
        film->audio_samplerate = 22050;
        film->audio_channels = 1;
        film->audio_bits = 8;
    } else {
        /* normal Saturn .cpk files; 32-byte header */
        if (avio_read(pb, scratch, 32) != 32)
            return AVERROR(EIO);
        film->audio_samplerate = AV_RB16(&scratch[24]);
        film->audio_channels = scratch[21];
        film->audio_bits = scratch[22];
        if (scratch[23] == 2 && film->audio_channels > 0)
            film->audio_type = AV_CODEC_ID_ADPCM_ADX;
        else if (film->audio_channels > 0) {
            if (film->audio_bits == 8)
                film->audio_type = AV_CODEC_ID_PCM_S8_PLANAR;
            else if (film->audio_bits == 16)
                film->audio_type = AV_CODEC_ID_PCM_S16BE_PLANAR;
            else
                film->audio_type = AV_CODEC_ID_NONE;
        } else
            film->audio_type = AV_CODEC_ID_NONE;
    }

    if (AV_RB32(&scratch[0]) != FDSC_TAG)
        return AVERROR_INVALIDDATA;

    if (AV_RB32(&scratch[8]) == CVID_TAG) {
        film->video_type = AV_CODEC_ID_CINEPAK;
    } else if (AV_RB32(&scratch[8]) == RAW_TAG) {
        film->video_type = AV_CODEC_ID_RAWVIDEO;
    } else {
        film->video_type = AV_CODEC_ID_NONE;
    }

    /* initialize the decoder streams */
    if (film->video_type) {
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        film->video_stream_index = st->index;
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codec->codec_id = film->video_type;
        st->codec->codec_tag = 0;  /* no fourcc */
        st->codec->width = AV_RB32(&scratch[16]);
        st->codec->height = AV_RB32(&scratch[12]);

        if (film->video_type == AV_CODEC_ID_RAWVIDEO) {
            if (scratch[20] == 24) {
                st->codec->pix_fmt = AV_PIX_FMT_RGB24;
            } else {
                av_log(s, AV_LOG_ERROR, "raw video is using unhandled %dbpp\n", scratch[20]);
                return -1;
            }
        }
    }

    if (film->audio_type) {
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        film->audio_stream_index = st->index;
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id = film->audio_type;
        st->codec->codec_tag = 1;
        st->codec->channels = film->audio_channels;
        st->codec->sample_rate = film->audio_samplerate;

        if (film->audio_type == AV_CODEC_ID_ADPCM_ADX) {
            st->codec->bits_per_coded_sample = 18 * 8 / 32;
            st->codec->block_align = st->codec->channels * 18;
            st->need_parsing = AVSTREAM_PARSE_FULL;
        } else {
            st->codec->bits_per_coded_sample = film->audio_bits;
            st->codec->block_align = st->codec->channels *
                st->codec->bits_per_coded_sample / 8;
        }

        st->codec->bit_rate = st->codec->channels * st->codec->sample_rate *
            st->codec->bits_per_coded_sample;
    }

    /* load the sample table */
    if (avio_read(pb, scratch, 16) != 16)
        return AVERROR(EIO);
    if (AV_RB32(&scratch[0]) != STAB_TAG)
        return AVERROR_INVALIDDATA;
    film->base_clock = AV_RB32(&scratch[8]);
    film->sample_count = AV_RB32(&scratch[12]);
    if(film->sample_count >= UINT_MAX / sizeof(film_sample))
        return -1;
    film->sample_table = av_malloc_array(film->sample_count, sizeof(film_sample));
    if (!film->sample_table)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            avpriv_set_pts_info(st, 33, 1, film->base_clock);
        else
            avpriv_set_pts_info(st, 64, 1, film->audio_samplerate);
    }

    audio_frame_counter = video_frame_counter = 0;
    for (i = 0; i < film->sample_count; i++) {
        /* load the next sample record and transfer it to an internal struct */
        if (avio_read(pb, scratch, 16) != 16) {
            ret = AVERROR(EIO);
            goto fail;
        }
        film->sample_table[i].sample_offset =
            data_offset + AV_RB32(&scratch[0]);
        film->sample_table[i].sample_size = AV_RB32(&scratch[4]);
        if (film->sample_table[i].sample_size > INT_MAX / 4) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        if (AV_RB32(&scratch[8]) == 0xFFFFFFFF) {
            film->sample_table[i].stream = film->audio_stream_index;
            film->sample_table[i].pts = audio_frame_counter;

            if (film->audio_type == AV_CODEC_ID_ADPCM_ADX)
                audio_frame_counter += (film->sample_table[i].sample_size * 32 /
                    (18 * film->audio_channels));
            else if (film->audio_type != AV_CODEC_ID_NONE)
                audio_frame_counter += (film->sample_table[i].sample_size /
                    (film->audio_channels * film->audio_bits / 8));
        } else {
            film->sample_table[i].stream = film->video_stream_index;
            film->sample_table[i].pts = AV_RB32(&scratch[8]) & 0x7FFFFFFF;
            film->sample_table[i].keyframe = (scratch[8] & 0x80) ? 0 : 1;
            video_frame_counter++;
            if (film->video_type)
                av_add_index_entry(s->streams[film->video_stream_index],
                                   film->sample_table[i].sample_offset,
                                   film->sample_table[i].pts,
                                   film->sample_table[i].sample_size, 0,
                                   film->sample_table[i].keyframe);
        }
    }

    if (film->audio_type)
        s->streams[film->audio_stream_index]->duration = audio_frame_counter;

    if (film->video_type)
        s->streams[film->video_stream_index]->duration = video_frame_counter;

    film->current_sample = 0;

    return 0;
fail:
    film_read_close(s);
    return ret;
}

static int film_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    FilmDemuxContext *film = s->priv_data;
    AVIOContext *pb = s->pb;
    film_sample *sample;
    int ret = 0;

    if (film->current_sample >= film->sample_count)
        return AVERROR_EOF;

    sample = &film->sample_table[film->current_sample];

    /* position the stream (will probably be there anyway) */
    avio_seek(pb, sample->sample_offset, SEEK_SET);

    ret = av_get_packet(pb, pkt, sample->sample_size);
    if (ret != sample->sample_size)
        ret = AVERROR(EIO);

    pkt->stream_index = sample->stream;
    pkt->pts = sample->pts;

    film->current_sample++;

    return ret;
}

static int film_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    FilmDemuxContext *film = s->priv_data;
    AVStream *st = s->streams[stream_index];
    int64_t pos;
    int ret = av_index_search_timestamp(st, timestamp, flags);
    if (ret < 0)
        return ret;

    pos = avio_seek(s->pb, st->index_entries[ret].pos, SEEK_SET);
    if (pos < 0)
        return pos;

    film->current_sample = ret;

    return 0;
}

AVInputFormat ff_segafilm_demuxer = {
    .name           = "film_cpk",
    .long_name      = NULL_IF_CONFIG_SMALL("Sega FILM / CPK"),
    .priv_data_size = sizeof(FilmDemuxContext),
    .read_probe     = film_probe,
    .read_header    = film_read_header,
    .read_packet    = film_read_packet,
    .read_close     = film_read_close,
    .read_seek      = film_read_seek,
};
