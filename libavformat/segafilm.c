/*
 * Sega FILM Format (CPK) Demuxer
 * Copyright (c) 2003 The FFmpeg project
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
  unsigned int sample_size;
  int64_t sample_offset;
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

static int film_probe(const AVProbeData *p)
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
    int i;
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

    if (film->video_type == AV_CODEC_ID_NONE && film->audio_type == AV_CODEC_ID_NONE)
        return AVERROR_INVALIDDATA;

    /* initialize the decoder streams */
    if (film->video_type != AV_CODEC_ID_NONE) {
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        film->video_stream_index = st->index;
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id = film->video_type;
        st->codecpar->codec_tag = 0;  /* no fourcc */
        st->codecpar->width = AV_RB32(&scratch[16]);
        st->codecpar->height = AV_RB32(&scratch[12]);

        if (film->video_type == AV_CODEC_ID_RAWVIDEO) {
            if (scratch[20] == 24) {
                st->codecpar->format = AV_PIX_FMT_RGB24;
            } else {
                av_log(s, AV_LOG_ERROR, "raw video is using unhandled %dbpp\n", scratch[20]);
                return -1;
            }
        }
    }

    if (film->audio_type != AV_CODEC_ID_NONE) {
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        film->audio_stream_index = st->index;
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = film->audio_type;
        st->codecpar->codec_tag = 1;
        st->codecpar->ch_layout.nb_channels = film->audio_channels;
        st->codecpar->sample_rate = film->audio_samplerate;

        if (film->audio_type == AV_CODEC_ID_ADPCM_ADX) {
            st->codecpar->bits_per_coded_sample = 18 * 8 / 32;
            st->codecpar->block_align = film->audio_channels * 18;
            ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL;
        } else {
            st->codecpar->bits_per_coded_sample = film->audio_bits;
            st->codecpar->block_align = film->audio_channels *
                st->codecpar->bits_per_coded_sample / 8;
        }

        st->codecpar->bit_rate = film->audio_channels * st->codecpar->sample_rate *
            st->codecpar->bits_per_coded_sample;
    }

    /* load the sample table */
    if (avio_read(pb, scratch, 16) != 16)
        return AVERROR(EIO);
    if (AV_RB32(&scratch[0]) != STAB_TAG)
        return AVERROR_INVALIDDATA;
    film->base_clock = AV_RB32(&scratch[8]);
    film->sample_count = AV_RB32(&scratch[12]);
    film->sample_table = av_malloc_array(film->sample_count, sizeof(film_sample));
    if (!film->sample_table)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            avpriv_set_pts_info(st, 33, 1, film->base_clock);
        else
            avpriv_set_pts_info(st, 64, 1, film->audio_samplerate);
    }

    audio_frame_counter = video_frame_counter = 0;
    for (i = 0; i < film->sample_count; i++) {
        /* load the next sample record and transfer it to an internal struct */
        if (avio_read(pb, scratch, 16) != 16)
            return AVERROR(EIO);
        film->sample_table[i].sample_offset =
            data_offset + AV_RB32(&scratch[0]);
        film->sample_table[i].sample_size = AV_RB32(&scratch[4]);
        if (film->sample_table[i].sample_size > INT_MAX / 4)
            return AVERROR_INVALIDDATA;
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
            film->sample_table[i].keyframe = (scratch[8] & 0x80) ? 0 : AVINDEX_KEYFRAME;
            video_frame_counter++;
            if (film->video_type != AV_CODEC_ID_NONE)
                av_add_index_entry(s->streams[film->video_stream_index],
                                   film->sample_table[i].sample_offset,
                                   film->sample_table[i].pts,
                                   film->sample_table[i].sample_size, 0,
                                   film->sample_table[i].keyframe);
        }
    }

    if (film->audio_type != AV_CODEC_ID_NONE)
        s->streams[film->audio_stream_index]->duration = audio_frame_counter;

    if (film->video_type != AV_CODEC_ID_NONE)
        s->streams[film->video_stream_index]->duration = video_frame_counter;

    film->current_sample = 0;

    return 0;
}

static int film_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    FilmDemuxContext *film = s->priv_data;
    AVIOContext *pb = s->pb;
    film_sample *sample;
    film_sample *next_sample = NULL;
    int next_sample_id;
    int ret = 0;

    if (film->current_sample >= film->sample_count)
        return AVERROR_EOF;

    sample = &film->sample_table[film->current_sample];

    /* Find the next sample from the same stream, assuming there is one;
     * this is used to calculate the duration below */
    next_sample_id = film->current_sample + 1;
    while (next_sample == NULL) {
        if (next_sample_id >= film->sample_count)
            break;

        next_sample = &film->sample_table[next_sample_id];
        if (next_sample->stream != sample->stream) {
            next_sample = NULL;
            next_sample_id++;
        }
    }

    /* position the stream (will probably be there anyway) */
    avio_seek(pb, sample->sample_offset, SEEK_SET);

    ret = av_get_packet(pb, pkt, sample->sample_size);
    if (ret != sample->sample_size)
        ret = AVERROR(EIO);

    pkt->stream_index = sample->stream;
    pkt->dts = sample->pts;
    pkt->pts = sample->pts;
    pkt->flags |= sample->keyframe ? AV_PKT_FLAG_KEY : 0;
    if (next_sample != NULL)
        pkt->duration = next_sample->pts - sample->pts;

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

    pos = avio_seek(s->pb, ffstream(st)->index_entries[ret].pos, SEEK_SET);
    if (pos < 0)
        return pos;

    film->current_sample = ret;

    return 0;
}

const AVInputFormat ff_segafilm_demuxer = {
    .name           = "film_cpk",
    .long_name      = NULL_IF_CONFIG_SMALL("Sega FILM / CPK"),
    .priv_data_size = sizeof(FilmDemuxContext),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = film_probe,
    .read_header    = film_read_header,
    .read_packet    = film_read_packet,
    .read_close     = film_read_close,
    .read_seek      = film_read_seek,
};
