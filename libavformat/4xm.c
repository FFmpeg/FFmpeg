/*
 * 4X Technologies .4xm File Demuxer (no muxer)
 * Copyright (c) 2003  The ffmpeg Project
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
 * @file 4xm.c
 * 4X Technologies file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the .4xm file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include "avformat.h"

#define  RIFF_TAG MKTAG('R', 'I', 'F', 'F')
#define _4XMV_TAG MKTAG('4', 'X', 'M', 'V')
#define  LIST_TAG MKTAG('L', 'I', 'S', 'T')
#define  HEAD_TAG MKTAG('H', 'E', 'A', 'D')
#define  TRK__TAG MKTAG('T', 'R', 'K', '_')
#define  MOVI_TAG MKTAG('M', 'O', 'V', 'I')
#define  VTRK_TAG MKTAG('V', 'T', 'R', 'K')
#define  STRK_TAG MKTAG('S', 'T', 'R', 'K')
#define  std__TAG MKTAG('s', 't', 'd', '_')
#define  name_TAG MKTAG('n', 'a', 'm', 'e')
#define  vtrk_TAG MKTAG('v', 't', 'r', 'k')
#define  strk_TAG MKTAG('s', 't', 'r', 'k')
#define  ifrm_TAG MKTAG('i', 'f', 'r', 'm')
#define  pfrm_TAG MKTAG('p', 'f', 'r', 'm')
#define  cfrm_TAG MKTAG('c', 'f', 'r', 'm')
#define  snd__TAG MKTAG('s', 'n', 'd', '_')

#define vtrk_SIZE 0x44
#define strk_SIZE 0x28

#define GET_LIST_HEADER() \
    fourcc_tag = get_le32(pb); \
    size = get_le32(pb); \
    if (fourcc_tag != LIST_TAG) \
        return AVERROR_INVALIDDATA; \
    fourcc_tag = get_le32(pb);

typedef struct AudioTrack {
    int sample_rate;
    int bits;
    int channels;
    int stream_index;
    int adpcm;
} AudioTrack;

typedef struct FourxmDemuxContext {
    int width;
    int height;
    int video_stream_index;
    int track_count;
    AudioTrack *tracks;
    int selected_track;

    int64_t audio_pts;
    int64_t video_pts;
    int video_pts_inc;
    float fps;
} FourxmDemuxContext;

static float get_le_float(unsigned char *buffer)
{
    float f;
    unsigned char *float_buffer = (unsigned char *)&f;

#ifdef WORDS_BIGENDIAN
    float_buffer[0] = buffer[3];
    float_buffer[1] = buffer[2];
    float_buffer[2] = buffer[1];
    float_buffer[3] = buffer[0];
#else
    float_buffer[0] = buffer[0];
    float_buffer[1] = buffer[1];
    float_buffer[2] = buffer[2];
    float_buffer[3] = buffer[3];
#endif

    return f;
}

static int fourxm_probe(AVProbeData *p)
{
    if (p->buf_size < 12)
        return 0;

    if ((LE_32(&p->buf[0]) != RIFF_TAG) ||
        (LE_32(&p->buf[8]) != _4XMV_TAG))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int fourxm_read_header(AVFormatContext *s,
                              AVFormatParameters *ap)
{
    ByteIOContext *pb = &s->pb;
    unsigned int fourcc_tag;
    unsigned int size;
    int header_size;
    FourxmDemuxContext *fourxm = (FourxmDemuxContext *)s->priv_data;
    unsigned char *header;
    int i;
    int current_track = -1;
    AVStream *st;

    fourxm->track_count = 0;
    fourxm->tracks = NULL;
    fourxm->selected_track = 0;
    fourxm->fps = 1.0;

    /* skip the first 3 32-bit numbers */
    url_fseek(pb, 12, SEEK_CUR);

    /* check for LIST-HEAD */
    GET_LIST_HEADER();
    header_size = size - 4;
    if (fourcc_tag != HEAD_TAG)
        return AVERROR_INVALIDDATA;

    /* allocate space for the header and load the whole thing */
    header = av_malloc(header_size);
    if (!header)
        return AVERROR_NOMEM;
    if (get_buffer(pb, header, header_size) != header_size)
        return AVERROR_IO;

    /* take the lazy approach and search for any and all vtrk and strk chunks */
    for (i = 0; i < header_size - 8; i++) {
        fourcc_tag = LE_32(&header[i]);
        size = LE_32(&header[i + 4]);

        if (fourcc_tag == std__TAG) {
            fourxm->fps = get_le_float(&header[i + 12]);
            fourxm->video_pts_inc = (int)(90000.0 / fourxm->fps);
        } else if (fourcc_tag == vtrk_TAG) {
            /* check that there is enough data */
            if (size != vtrk_SIZE) {
                av_free(header);
                return AVERROR_INVALIDDATA;
            }
            fourxm->width = LE_32(&header[i + 36]);
            fourxm->height = LE_32(&header[i + 40]);
            i += 8 + size;

            /* allocate a new AVStream */
            st = av_new_stream(s, 0);
            if (!st)
                return AVERROR_NOMEM;
            av_set_pts_info(st, 33, 1, 90000);

            fourxm->video_stream_index = st->index;

            st->codec.frame_rate = fourxm->fps;
            st->codec.frame_rate_base = 1.0;
            st->codec.codec_type = CODEC_TYPE_VIDEO;
            st->codec.codec_id = CODEC_ID_4XM;
            st->codec.codec_tag = 0;  /* no fourcc */
            st->codec.width = fourxm->width;
            st->codec.height = fourxm->height;

        } else if (fourcc_tag == strk_TAG) {
            /* check that there is enough data */
            if (size != strk_SIZE) {
                av_free(header);
                return AVERROR_INVALIDDATA;
            }
            current_track = LE_32(&header[i + 8]);
            if (current_track + 1 > fourxm->track_count) {
                fourxm->track_count = current_track + 1;
                if((unsigned)fourxm->track_count >= UINT_MAX / sizeof(AudioTrack))
                    return -1;
                fourxm->tracks = av_realloc(fourxm->tracks, 
                    fourxm->track_count * sizeof(AudioTrack));
                if (!fourxm->tracks) {
                    av_free(header);
                    return AVERROR_NOMEM;
                }
            }
            fourxm->tracks[current_track].adpcm = LE_32(&header[i + 12]);
            fourxm->tracks[current_track].channels = LE_32(&header[i + 36]);
            fourxm->tracks[current_track].sample_rate = LE_32(&header[i + 40]);
            fourxm->tracks[current_track].bits = LE_32(&header[i + 44]);
            i += 8 + size;

            /* allocate a new AVStream */
            st = av_new_stream(s, current_track);
            if (!st)
                return AVERROR_NOMEM;

            /* set the pts reference (1 pts = 1/90000) */
            av_set_pts_info(st, 33, 1, 90000);

            fourxm->tracks[current_track].stream_index = st->index;

            st->codec.codec_type = CODEC_TYPE_AUDIO;
            st->codec.codec_tag = 1;
            st->codec.channels = fourxm->tracks[current_track].channels;
            st->codec.sample_rate = fourxm->tracks[current_track].sample_rate;
            st->codec.bits_per_sample = fourxm->tracks[current_track].bits;
            st->codec.bit_rate = st->codec.channels * st->codec.sample_rate *
                st->codec.bits_per_sample;
            st->codec.block_align = st->codec.channels * st->codec.bits_per_sample;
            if (fourxm->tracks[current_track].adpcm)
                st->codec.codec_id = CODEC_ID_ADPCM_4XM;
            else if (st->codec.bits_per_sample == 8)
                st->codec.codec_id = CODEC_ID_PCM_U8;
            else
                st->codec.codec_id = CODEC_ID_PCM_S16LE;
        }
    }

    av_free(header);

    /* skip over the LIST-MOVI chunk (which is where the stream should be */
    GET_LIST_HEADER();
    if (fourcc_tag != MOVI_TAG)
        return AVERROR_INVALIDDATA;

    /* initialize context members */
    fourxm->video_pts = -fourxm->video_pts_inc;  /* first frame will push to 0 */
    fourxm->audio_pts = 0;

    return 0;
}

static int fourxm_read_packet(AVFormatContext *s,
                              AVPacket *pkt)
{
    FourxmDemuxContext *fourxm = s->priv_data;
    ByteIOContext *pb = &s->pb;
    unsigned int fourcc_tag;
    unsigned int size, out_size;
    int ret = 0;
    int track_number;
    int packet_read = 0;
    unsigned char header[8];
    int64_t pts_inc;
    int audio_frame_count;

    while (!packet_read) {

        if ((ret = get_buffer(&s->pb, header, 8)) < 0)
            return ret;
        fourcc_tag = LE_32(&header[0]);
        size = LE_32(&header[4]);
        if (url_feof(pb))
            return AVERROR_IO;
        switch (fourcc_tag) {

        case LIST_TAG:
            /* this is a good time to bump the video pts */
            fourxm->video_pts += fourxm->video_pts_inc;

            /* skip the LIST-* tag and move on to the next fourcc */
            get_le32(pb);
            break;

        case ifrm_TAG:
        case pfrm_TAG:
        case cfrm_TAG:{

            /* allocate 8 more bytes than 'size' to account for fourcc
             * and size */
            if (av_new_packet(pkt, size + 8))
                return AVERROR_IO;
            pkt->stream_index = fourxm->video_stream_index;
            pkt->pts = fourxm->video_pts;
            memcpy(pkt->data, header, 8);
            ret = get_buffer(&s->pb, &pkt->data[8], size);

            if (ret < 0)
                av_free_packet(pkt);
            else
                packet_read = 1;
            break;
        }

        case snd__TAG:
            track_number = get_le32(pb);
            out_size= get_le32(pb);
            size-=8;

            if (track_number == fourxm->selected_track) {
                if (av_new_packet(pkt, size))
                    return AVERROR_IO;
                pkt->stream_index = 
                    fourxm->tracks[fourxm->selected_track].stream_index;
                pkt->pts = fourxm->audio_pts;
                ret = get_buffer(&s->pb, pkt->data, size);
                if (ret < 0)
                    av_free_packet(pkt);
                else
                    packet_read = 1;

                /* pts accounting */
                audio_frame_count = size;
                if (fourxm->tracks[fourxm->selected_track].adpcm)
                    audio_frame_count -= 
                        2 * (fourxm->tracks[fourxm->selected_track].channels);
                audio_frame_count /=
                      fourxm->tracks[fourxm->selected_track].channels;
                if (fourxm->tracks[fourxm->selected_track].adpcm)
                    audio_frame_count *= 2;
                else 
                    audio_frame_count /=
                    (fourxm->tracks[fourxm->selected_track].bits / 8);
                pts_inc = audio_frame_count;
                pts_inc *= 90000;
                pts_inc /= fourxm->tracks[fourxm->selected_track].sample_rate;
                fourxm->audio_pts += pts_inc;

            } else {
                url_fseek(pb, size, SEEK_CUR);
            }
            break;

        default:
            url_fseek(pb, size, SEEK_CUR);
            break;
        }
    }
    return ret;
}

static int fourxm_read_close(AVFormatContext *s)
{
    FourxmDemuxContext *fourxm = (FourxmDemuxContext *)s->priv_data;

    av_free(fourxm->tracks);

    return 0;
}

static AVInputFormat fourxm_iformat = {
    "4xm",
    "4X Technologies format",
    sizeof(FourxmDemuxContext),
    fourxm_probe,
    fourxm_read_header,
    fourxm_read_packet,
    fourxm_read_close,
};

int fourxm_init(void)
{
    av_register_input_format(&fourxm_iformat);
    return 0;
}
