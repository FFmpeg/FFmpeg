/*
 * Sega FILM Format (CPK) Demuxer
 * Copyright (c) 2003 The ffmpeg Project
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

typedef struct {
  int stream;
  int64_t sample_offset;
  unsigned int sample_size;
  int64_t pts;
  int keyframe;
} film_sample;

typedef struct FilmDemuxContext {
    int video_stream_index;
    int audio_stream_index;

    enum CodecID audio_type;
    unsigned int audio_samplerate;
    unsigned int audio_bits;
    unsigned int audio_channels;

    enum CodecID video_type;
    unsigned int sample_count;
    film_sample *sample_table;
    unsigned int current_sample;

    unsigned int base_clock;
    unsigned int version;

    /* buffer used for interleaving stereo PCM data */
    unsigned char *stereo_buffer;
    int stereo_buffer_size;
} FilmDemuxContext;

static int film_probe(AVProbeData *p)
{
    if (AV_RB32(&p->buf[0]) != FILM_TAG)
        return 0;

    return AVPROBE_SCORE_MAX;
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

    film->sample_table = NULL;
    film->stereo_buffer = NULL;
    film->stereo_buffer_size = 0;

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
        film->audio_type = CODEC_ID_PCM_S8;
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
        if (scratch[23] == 2)
            film->audio_type = CODEC_ID_ADPCM_ADX;
        else if (film->audio_channels > 0) {
            if (film->audio_bits == 8)
                film->audio_type = CODEC_ID_PCM_S8;
            else if (film->audio_bits == 16)
                film->audio_type = CODEC_ID_PCM_S16BE;
            else
                film->audio_type = CODEC_ID_NONE;
        } else
            film->audio_type = CODEC_ID_NONE;
    }

    if (AV_RB32(&scratch[0]) != FDSC_TAG)
        return AVERROR_INVALIDDATA;

    if (AV_RB32(&scratch[8]) == CVID_TAG) {
        film->video_type = CODEC_ID_CINEPAK;
    } else if (AV_RB32(&scratch[8]) == RAW_TAG) {
        film->video_type = CODEC_ID_RAWVIDEO;
    } else {
        film->video_type = CODEC_ID_NONE;
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

        if (film->video_type == CODEC_ID_RAWVIDEO) {
            if (scratch[20] == 24) {
                st->codec->pix_fmt = PIX_FMT_RGB24;
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

        if (film->audio_type == CODEC_ID_ADPCM_ADX) {
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
    film->sample_table = av_malloc(film->sample_count * sizeof(film_sample));
    if (!film->sample_table)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            avpriv_set_pts_info(st, 33, 1, film->base_clock);
        else
            avpriv_set_pts_info(st, 64, 1, film->audio_samplerate);
    }

    audio_frame_counter = 0;
    for (i = 0; i < film->sample_count; i++) {
        /* load the next sample record and transfer it to an internal struct */
        if (avio_read(pb, scratch, 16) != 16) {
            av_free(film->sample_table);
            return AVERROR(EIO);
        }
        film->sample_table[i].sample_offset =
            data_offset + AV_RB32(&scratch[0]);
        film->sample_table[i].sample_size = AV_RB32(&scratch[4]);
        if (AV_RB32(&scratch[8]) == 0xFFFFFFFF) {
            film->sample_table[i].stream = film->audio_stream_index;
            film->sample_table[i].pts = audio_frame_counter;

            if (film->audio_type == CODEC_ID_ADPCM_ADX)
                audio_frame_counter += (film->sample_table[i].sample_size * 32 /
                    (18 * film->audio_channels));
            else if (film->audio_type != CODEC_ID_NONE)
                audio_frame_counter += (film->sample_table[i].sample_size /
                    (film->audio_channels * film->audio_bits / 8));
        } else {
            film->sample_table[i].stream = film->video_stream_index;
            film->sample_table[i].pts = AV_RB32(&scratch[8]) & 0x7FFFFFFF;
            film->sample_table[i].keyframe = (scratch[8] & 0x80) ? 0 : 1;
        }
    }

    film->current_sample = 0;

    return 0;
}

static int film_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    FilmDemuxContext *film = s->priv_data;
    AVIOContext *pb = s->pb;
    film_sample *sample;
    int ret = 0;
    int i;
    int left, right;

    if (film->current_sample >= film->sample_count)
        return AVERROR(EIO);

    sample = &film->sample_table[film->current_sample];

    /* position the stream (will probably be there anyway) */
    avio_seek(pb, sample->sample_offset, SEEK_SET);

    /* do a special song and dance when loading FILM Cinepak chunks */
    if ((sample->stream == film->video_stream_index) &&
        (film->video_type == CODEC_ID_CINEPAK)) {
        pkt->pos= avio_tell(pb);
        if (av_new_packet(pkt, sample->sample_size))
            return AVERROR(ENOMEM);
        avio_read(pb, pkt->data, sample->sample_size);
    } else if ((sample->stream == film->audio_stream_index) &&
        (film->audio_channels == 2) &&
        (film->audio_type != CODEC_ID_ADPCM_ADX)) {
        /* stereo PCM needs to be interleaved */

        if (ffio_limit(pb, sample->sample_size) != sample->sample_size)
            return AVERROR(EIO);
        if (av_new_packet(pkt, sample->sample_size))
            return AVERROR(ENOMEM);

        /* make sure the interleave buffer is large enough */
        if (sample->sample_size > film->stereo_buffer_size) {
            av_free(film->stereo_buffer);
            film->stereo_buffer_size = sample->sample_size;
            film->stereo_buffer = av_malloc(film->stereo_buffer_size);
            if (!film->stereo_buffer) {
                film->stereo_buffer_size = 0;
                return AVERROR(ENOMEM);
            }
        }

        pkt->pos= avio_tell(pb);
        ret = avio_read(pb, film->stereo_buffer, sample->sample_size);
        if (ret != sample->sample_size)
            ret = AVERROR(EIO);

        left = 0;
        right = sample->sample_size / 2;
        for (i = 0; i + 1 + 2*(film->audio_bits != 8) < sample->sample_size; ) {
            if (film->audio_bits == 8) {
                pkt->data[i++] = film->stereo_buffer[left++];
                pkt->data[i++] = film->stereo_buffer[right++];
            } else {
                pkt->data[i++] = film->stereo_buffer[left++];
                pkt->data[i++] = film->stereo_buffer[left++];
                pkt->data[i++] = film->stereo_buffer[right++];
                pkt->data[i++] = film->stereo_buffer[right++];
            }
        }
    } else {
        ret= av_get_packet(pb, pkt, sample->sample_size);
        if (ret != sample->sample_size)
            ret = AVERROR(EIO);
    }

    pkt->stream_index = sample->stream;
    pkt->pts = sample->pts;

    film->current_sample++;

    return ret;
}

static int film_read_close(AVFormatContext *s)
{
    FilmDemuxContext *film = s->priv_data;

    av_free(film->sample_table);
    av_free(film->stereo_buffer);

    return 0;
}

AVInputFormat ff_segafilm_demuxer = {
    .name           = "film_cpk",
    .long_name      = NULL_IF_CONFIG_SMALL("Sega FILM/CPK format"),
    .priv_data_size = sizeof(FilmDemuxContext),
    .read_probe     = film_probe,
    .read_header    = film_read_header,
    .read_packet    = film_read_packet,
    .read_close     = film_read_close,
};
