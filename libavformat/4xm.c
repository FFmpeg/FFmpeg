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

#define LE_16(x)  ((((uint8_t*)(x))[1] << 8) | ((uint8_t*)(x))[0])
#define LE_32(x)  ((((uint8_t*)(x))[3] << 24) | \
                   (((uint8_t*)(x))[2] << 16) | \
                   (((uint8_t*)(x))[1] << 8) | \
                    ((uint8_t*)(x))[0])

#define FOURCC_TAG( ch0, ch1, ch2, ch3 ) \
        ( (long)(unsigned char)(ch0) | \
        ( (long)(unsigned char)(ch1) << 8 ) | \
        ( (long)(unsigned char)(ch2) << 16 ) | \
        ( (long)(unsigned char)(ch3) << 24 ) )

#define  RIFF_TAG FOURCC_TAG('R', 'I', 'F', 'F')
#define _4XMV_TAG FOURCC_TAG('4', 'X', 'M', 'V')
#define  LIST_TAG FOURCC_TAG('L', 'I', 'S', 'T')
#define  HEAD_TAG FOURCC_TAG('H', 'E', 'A', 'D')
#define  TRK__TAG FOURCC_TAG('T', 'R', 'K', '_')
#define  MOVI_TAG FOURCC_TAG('M', 'O', 'V', 'I')
#define  VTRK_TAG FOURCC_TAG('V', 'T', 'R', 'K')
#define  STRK_TAG FOURCC_TAG('S', 'T', 'R', 'K')
#define  name_TAG FOURCC_TAG('n', 'a', 'm', 'e')
#define  vtrk_TAG FOURCC_TAG('v', 't', 'r', 'k')
#define  strk_TAG FOURCC_TAG('s', 't', 'r', 'k')
#define  ifrm_TAG FOURCC_TAG('i', 'f', 'r', 'm')
#define  pfrm_TAG FOURCC_TAG('p', 'f', 'r', 'm')
#define  cfrm_TAG FOURCC_TAG('c', 'f', 'r', 'm')
#define  snd__TAG FOURCC_TAG('s', 'n', 'd', '_')
#define  _TAG FOURCC_TAG('', '', '', '')

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

    int64_t pts;
    int last_chunk_was_audio;
    int last_audio_frame_count;
} FourxmDemuxContext;

static int fourxm_probe(AVProbeData *p)
{
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

        if (fourcc_tag == vtrk_TAG) {
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

            fourxm->video_stream_index = st->index;

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
            printf("bps:%d\n", fourxm->tracks[current_track].bits);
            i += 8 + size;

            /* allocate a new AVStream */
            st = av_new_stream(s, current_track);
            if (!st)
                return AVERROR_NOMEM;

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
    fourxm->pts = 0;
    fourxm->last_chunk_was_audio = 0;
    fourxm->last_audio_frame_count = 0;

    /* set the pts reference (1 pts = 1/90000) */
    s->pts_num = 1;
    s->pts_den = 90000;

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

    while (!packet_read) {

        if ((ret = get_buffer(&s->pb, header, 8)) < 0)
            return ret;
        fourcc_tag = LE_32(&header[0]);
        size = LE_32(&header[4]);
//printf(" %8X %c%c%c%c %d\n", fourcc_tag, fourcc_tag, fourcc_tag>>8, fourcc_tag>>16, fourcc_tag>>24, size);
        if (url_feof(pb))
            return -EIO;
        switch (fourcc_tag) {

        case LIST_TAG:
            /* skip the LIST-* tag and move on to the next fourcc */
            get_le32(pb);
            break;

        case ifrm_TAG:
        case pfrm_TAG:
        case cfrm_TAG:{

int id, whole;
static int stats[1000];

            /* bump the pts if this last data sent out was audio */
            if (fourxm->last_chunk_was_audio) {
                fourxm->last_chunk_was_audio = 0;
                pts_inc = fourxm->last_audio_frame_count;
                pts_inc *= 90000;
                pts_inc /= fourxm->tracks[fourxm->selected_track].sample_rate;
                fourxm->pts += pts_inc;
            }

            /* allocate 8 more bytes than 'size' to account for fourcc
             * and size */
            if (av_new_packet(pkt, size + 8))
                return -EIO;
            pkt->stream_index = fourxm->video_stream_index;
            pkt->pts = fourxm->pts;
            memcpy(pkt->data, header, 8);
            ret = get_buffer(&s->pb, &pkt->data[8], size);

if (fourcc_tag == cfrm_TAG) {
id = LE_32(&pkt->data[12]);
whole = LE_32(&pkt->data[16]);
stats[id] += size - 12;
//printf(" cfrm chunk id:%d size:%d whole:%d until now:%d\n", id, size, whole, stats[id]);
}

            if (ret < 0)
                av_free_packet(pkt);
            else
                packet_read = 1;
            break;
        }

        case snd__TAG:
printf (" snd_ chunk, ");
            track_number = get_le32(pb);
            out_size= get_le32(pb);
            size-=8;

            if (track_number == fourxm->selected_track) {
printf ("correct track, dispatching...\n");
                if (av_new_packet(pkt, size))
                    return -EIO;
                pkt->stream_index = 
                    fourxm->tracks[fourxm->selected_track].stream_index;
                pkt->pts = fourxm->pts;
                ret = get_buffer(&s->pb, pkt->data, size);
                if (ret < 0)
                    av_free_packet(pkt);
                else
                    packet_read = 1;

                /* maintain pts info for the benefit of the video track */
                fourxm->last_chunk_was_audio = 1;
                fourxm->last_audio_frame_count = size /
                    ((fourxm->tracks[fourxm->selected_track].bits / 8) *
                      fourxm->tracks[fourxm->selected_track].channels);

            } else {
printf ("wrong track, skipping...\n");
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
