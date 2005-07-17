/*
 * Sega FILM Format (CPK) Demuxer
 * Copyright (c) 2003 The ffmpeg Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file segafilm.c
 * Sega FILM (.cpk) file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the Sega FILM file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include "avformat.h"

#define FILM_TAG MKBETAG('F', 'I', 'L', 'M')
#define FDSC_TAG MKBETAG('F', 'D', 'S', 'C')
#define STAB_TAG MKBETAG('S', 'T', 'A', 'B')
#define CVID_TAG MKBETAG('c', 'v', 'i', 'd')

typedef struct {
  int stream;
  offset_t sample_offset;
  unsigned int sample_size;
  int64_t pts;
  int keyframe;
} film_sample_t;

typedef struct FilmDemuxContext {
    int video_stream_index;
    int audio_stream_index;

    unsigned int audio_type;
    unsigned int audio_samplerate;
    unsigned int audio_bits;
    unsigned int audio_channels;

    unsigned int video_type;
    unsigned int sample_count;
    film_sample_t *sample_table;
    unsigned int current_sample;

    unsigned int base_clock;
    unsigned int version;
    int cvid_extra_bytes;  /* the number of bytes thrown into the Cinepak
                            * chunk header to throw off decoders */

    /* buffer used for interleaving stereo PCM data */
    unsigned char *stereo_buffer;
    int stereo_buffer_size;
} FilmDemuxContext;

static int film_probe(AVProbeData *p)
{
    if (p->buf_size < 4)
        return 0;

    if (BE_32(&p->buf[0]) != FILM_TAG)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int film_read_header(AVFormatContext *s,
                            AVFormatParameters *ap)
{
    FilmDemuxContext *film = (FilmDemuxContext *)s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVStream *st;
    unsigned char scratch[256];
    int i;
    unsigned int data_offset;
    unsigned int audio_frame_counter;

    film->sample_table = NULL;
    film->stereo_buffer = NULL;
    film->stereo_buffer_size = 0;

    /* load the main FILM header */
    if (get_buffer(pb, scratch, 16) != 16)
        return AVERROR_IO;
    data_offset = BE_32(&scratch[4]);
    film->version = BE_32(&scratch[8]);

    /* load the FDSC chunk */
    if (film->version == 0) {
        /* special case for Lemmings .film files; 20-byte header */
        if (get_buffer(pb, scratch, 20) != 20)
            return AVERROR_IO;
        /* make some assumptions about the audio parameters */
        film->audio_type = CODEC_ID_PCM_S8;
        film->audio_samplerate = 22050;
        film->audio_channels = 1;
        film->audio_bits = 8;
    } else {
        /* normal Saturn .cpk files; 32-byte header */
        if (get_buffer(pb, scratch, 32) != 32)
            return AVERROR_IO;
        film->audio_samplerate = BE_16(&scratch[24]);;
        film->audio_channels = scratch[21];
        film->audio_bits = scratch[22];
        if (film->audio_bits == 8)
            film->audio_type = CODEC_ID_PCM_S8;
        else if (film->audio_bits == 16)
            film->audio_type = CODEC_ID_PCM_S16BE;
        else
            film->audio_type = 0;
    }

    if (BE_32(&scratch[0]) != FDSC_TAG)
        return AVERROR_INVALIDDATA;

    film->cvid_extra_bytes = 0;
    if (BE_32(&scratch[8]) == CVID_TAG) {
        film->video_type = CODEC_ID_CINEPAK;
        if (film->version)
            film->cvid_extra_bytes = 2;
        else
            film->cvid_extra_bytes = 6;  /* Lemmings 3DO case */
    } else
        film->video_type = 0;

    /* initialize the decoder streams */
    if (film->video_type) {
        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR_NOMEM;
        film->video_stream_index = st->index;
        st->codec->codec_type = CODEC_TYPE_VIDEO;
        st->codec->codec_id = film->video_type;
        st->codec->codec_tag = 0;  /* no fourcc */
        st->codec->width = BE_32(&scratch[16]);
        st->codec->height = BE_32(&scratch[12]);
    }

    if (film->audio_type) {
        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR_NOMEM;
        film->audio_stream_index = st->index;
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        st->codec->codec_id = film->audio_type;
        st->codec->codec_tag = 1;
        st->codec->channels = film->audio_channels;
        st->codec->bits_per_sample = film->audio_bits;
        st->codec->sample_rate = film->audio_samplerate;
        st->codec->bit_rate = st->codec->channels * st->codec->sample_rate *
            st->codec->bits_per_sample;
        st->codec->block_align = st->codec->channels * 
            st->codec->bits_per_sample / 8;
    }

    /* load the sample table */
    if (get_buffer(pb, scratch, 16) != 16)
        return AVERROR_IO;
    if (BE_32(&scratch[0]) != STAB_TAG)
        return AVERROR_INVALIDDATA;
    film->base_clock = BE_32(&scratch[8]);
    film->sample_count = BE_32(&scratch[12]);
    if(film->sample_count >= UINT_MAX / sizeof(film_sample_t))
        return -1;
    film->sample_table = av_malloc(film->sample_count * sizeof(film_sample_t));
    
    for(i=0; i<s->nb_streams; i++)
        av_set_pts_info(s->streams[i], 33, 1, film->base_clock);
    
    audio_frame_counter = 0;
    for (i = 0; i < film->sample_count; i++) {
        /* load the next sample record and transfer it to an internal struct */
        if (get_buffer(pb, scratch, 16) != 16) {
            av_free(film->sample_table);
            return AVERROR_IO;
        }
        film->sample_table[i].sample_offset = 
            data_offset + BE_32(&scratch[0]);
        film->sample_table[i].sample_size = BE_32(&scratch[4]);
        if (BE_32(&scratch[8]) == 0xFFFFFFFF) {
            film->sample_table[i].stream = film->audio_stream_index;
            film->sample_table[i].pts = audio_frame_counter;
            film->sample_table[i].pts *= film->base_clock;
            film->sample_table[i].pts /= film->audio_samplerate;

            audio_frame_counter += (film->sample_table[i].sample_size /
                (film->audio_channels * film->audio_bits / 8));
        } else {
            film->sample_table[i].stream = film->video_stream_index;
            film->sample_table[i].pts = BE_32(&scratch[8]) & 0x7FFFFFFF;
            film->sample_table[i].keyframe = (scratch[8] & 0x80) ? 0 : 1;
        }
    }

    film->current_sample = 0;

    return 0;
}

static int film_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    FilmDemuxContext *film = (FilmDemuxContext *)s->priv_data;
    ByteIOContext *pb = &s->pb;
    film_sample_t *sample;
    int ret = 0;
    int i;
    int left, right;

    if (film->current_sample >= film->sample_count)
        return AVERROR_IO;

    sample = &film->sample_table[film->current_sample];

    /* position the stream (will probably be there anyway) */
    url_fseek(pb, sample->sample_offset, SEEK_SET);

    /* do a special song and dance when loading FILM Cinepak chunks */
    if ((sample->stream == film->video_stream_index) && 
        (film->video_type == CODEC_ID_CINEPAK)) {
        if (av_new_packet(pkt, sample->sample_size - film->cvid_extra_bytes))
            return AVERROR_NOMEM;
        if(pkt->size < 10)
            return -1;
        pkt->pos= url_ftell(pb);
        ret = get_buffer(pb, pkt->data, 10);
        /* skip the non-spec CVID bytes */
        url_fseek(pb, film->cvid_extra_bytes, SEEK_CUR);
        ret += get_buffer(pb, pkt->data + 10, 
            sample->sample_size - 10 - film->cvid_extra_bytes);
        if (ret != sample->sample_size - film->cvid_extra_bytes)
            ret = AVERROR_IO;
    } else if ((sample->stream == film->audio_stream_index) &&
        (film->audio_channels == 2)) {
        /* stereo PCM needs to be interleaved */

        if (av_new_packet(pkt, sample->sample_size))
            return AVERROR_NOMEM;

        /* make sure the interleave buffer is large enough */
        if (sample->sample_size > film->stereo_buffer_size) {
            av_free(film->stereo_buffer);
            film->stereo_buffer_size = sample->sample_size;
            film->stereo_buffer = av_malloc(film->stereo_buffer_size);
        }

        pkt->pos= url_ftell(pb);
        ret = get_buffer(pb, film->stereo_buffer, sample->sample_size);
        if (ret != sample->sample_size)
            ret = AVERROR_IO;

        left = 0;
        right = sample->sample_size / 2;
        for (i = 0; i < sample->sample_size; ) {
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
            ret = AVERROR_IO;
    }

    pkt->stream_index = sample->stream;
    pkt->pts = sample->pts;

    film->current_sample++;

    return ret;
}

static int film_read_close(AVFormatContext *s)
{
    FilmDemuxContext *film = (FilmDemuxContext *)s->priv_data;

    av_free(film->sample_table);
    av_free(film->stereo_buffer);

    return 0;
}

static AVInputFormat film_iformat = {
    "film_cpk",
    "Sega FILM/CPK format",
    sizeof(FilmDemuxContext),
    film_probe,
    film_read_header,
    film_read_packet,
    film_read_close,
};

int film_init(void)
{
    av_register_input_format(&film_iformat);
    return 0;
}
