/*
 * Sony Playstation (PSX) STR File Demuxer
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
 * @file psxstr.c
 * PSX STR file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * This module handles streams that have been ripped from Sony Playstation
 * CD games. This demuxer can handle either raw STR files (which are just
 * concatenations of raw compact disc sectors) or STR files with 0x2C-byte
 * RIFF headers, followed by CD sectors.
 */

#include "avformat.h"

//#define PRINTSTUFF

#define RIFF_TAG MKTAG('R', 'I', 'F', 'F')
#define CDXA_TAG MKTAG('C', 'D', 'X', 'A')

#define RAW_CD_SECTOR_SIZE 2352
#define RAW_CD_SECTOR_DATA_SIZE 2304
#define VIDEO_DATA_CHUNK_SIZE 0x7E0
#define VIDEO_DATA_HEADER_SIZE 0x38
#define RIFF_HEADER_SIZE 0x2C

#define CDXA_TYPE_MASK     0x0E
#define CDXA_TYPE_DATA     0x08
#define CDXA_TYPE_AUDIO    0x04
#define CDXA_TYPE_VIDEO    0x02

#define STR_MAGIC (0x80010160)

typedef struct StrChannel {

    int type;
#define STR_AUDIO 0
#define STR_VIDEO 1

    /* video parameters */
    int width;
    int height;
    int video_stream_index;

    /* audio parameters */
    int sample_rate;
    int channels;
    int bits;
    int audio_stream_index;
} StrChannel;

typedef struct StrDemuxContext {

    /* a STR file can contain up to 32 channels of data */
    StrChannel channels[32];

    /* only decode the first audio and video channels encountered */
    int video_channel;
    int audio_channel;

    int64_t pts;

    unsigned char *video_chunk;
    AVPacket tmp_pkt;
} StrDemuxContext;

static const char sync_header[12] = {0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00};

static int str_probe(AVProbeData *p)
{
    int start;

    /* need at least 0x38 bytes to validate */
    if (p->buf_size < 0x38)
        return 0;

    if ((AV_RL32(&p->buf[0]) == RIFF_TAG) &&
        (AV_RL32(&p->buf[8]) == CDXA_TAG)) {

        /* RIFF header seen; skip 0x2C bytes */
        start = RIFF_HEADER_SIZE;
    } else
        start = 0;

    /* look for CD sync header (00, 0xFF x 10, 00) */
    if (memcmp(p->buf+start,sync_header,sizeof(sync_header)))
        return 0;

    /* MPEG files (like those ripped from VCDs) can also look like this;
     * only return half certainty */
    return 50;
}

#if 0
static void dump(unsigned char *buf,size_t len)
{
    int i;
    for(i=0;i<len;i++) {
        if ((i&15)==0) av_log(NULL, AV_LOG_DEBUG, "%04x  ",i);
        av_log(NULL, AV_LOG_DEBUG, "%02x ",buf[i]);
        if ((i&15)==15) av_log(NULL, AV_LOG_DEBUG, "\n");
    }
    av_log(NULL, AV_LOG_DEBUG, "\n");
}
#endif

static int str_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    ByteIOContext *pb = &s->pb;
    StrDemuxContext *str = (StrDemuxContext *)s->priv_data;
    AVStream *st;
    unsigned char sector[RAW_CD_SECTOR_SIZE];
    int start;
    int i;
    int channel;

    /* initialize context members */
    str->pts = 0;
    str->audio_channel = -1;  /* assume to audio or video */
    str->video_channel = -1;
    str->video_chunk = NULL;


    /* skip over any RIFF header */
    if (get_buffer(pb, sector, RIFF_HEADER_SIZE) != RIFF_HEADER_SIZE)
        return AVERROR_IO;
    if (AV_RL32(&sector[0]) == RIFF_TAG)
        start = RIFF_HEADER_SIZE;
    else
        start = 0;

    url_fseek(pb, start, SEEK_SET);

    /* check through the first 32 sectors for individual channels */
    for (i = 0; i < 32; i++) {
        if (get_buffer(pb, sector, RAW_CD_SECTOR_SIZE) != RAW_CD_SECTOR_SIZE)
            return AVERROR_IO;

//printf("%02x %02x %02x %02x\n",sector[0x10],sector[0x11],sector[0x12],sector[0x13]);

        channel = sector[0x11];
        if (channel >= 32)
            return AVERROR_INVALIDDATA;

        switch (sector[0x12] & CDXA_TYPE_MASK) {

        case CDXA_TYPE_DATA:
        case CDXA_TYPE_VIDEO:
            /* check if this channel gets to be the dominant video channel */
            if (str->video_channel == -1) {
                /* qualify the magic number */
                if (AV_RL32(&sector[0x18]) != STR_MAGIC)
                    break;
                str->video_channel = channel;
                str->channels[channel].type = STR_VIDEO;
                str->channels[channel].width = AV_RL16(&sector[0x28]);
                str->channels[channel].height = AV_RL16(&sector[0x2A]);

                /* allocate a new AVStream */
                st = av_new_stream(s, 0);
                if (!st)
                    return AVERROR_NOMEM;
                av_set_pts_info(st, 64, 1, 15);

                str->channels[channel].video_stream_index = st->index;

                st->codec->codec_type = CODEC_TYPE_VIDEO;
                st->codec->codec_id = CODEC_ID_MDEC;
                st->codec->codec_tag = 0;  /* no fourcc */
                st->codec->width = str->channels[channel].width;
                st->codec->height = str->channels[channel].height;
            }
            break;

        case CDXA_TYPE_AUDIO:
            /* check if this channel gets to be the dominant audio channel */
            if (str->audio_channel == -1) {
                int fmt;
                str->audio_channel = channel;
                str->channels[channel].type = STR_AUDIO;
                str->channels[channel].channels =
                    (sector[0x13] & 0x01) ? 2 : 1;
                str->channels[channel].sample_rate =
                    (sector[0x13] & 0x04) ? 18900 : 37800;
                str->channels[channel].bits =
                    (sector[0x13] & 0x10) ? 8 : 4;

                /* allocate a new AVStream */
                st = av_new_stream(s, 0);
                if (!st)
                    return AVERROR_NOMEM;
                av_set_pts_info(st, 64, 128, str->channels[channel].sample_rate);

                str->channels[channel].audio_stream_index = st->index;

                fmt = sector[0x13];
                st->codec->codec_type = CODEC_TYPE_AUDIO;
                st->codec->codec_id = CODEC_ID_ADPCM_XA;
                st->codec->codec_tag = 0;  /* no fourcc */
                st->codec->channels = (fmt&1)?2:1;
                st->codec->sample_rate = (fmt&4)?18900:37800;
            //    st->codec->bit_rate = 0; //FIXME;
                st->codec->block_align = 128;
            }
            break;

        default:
            /* ignore */
            break;
        }
    }

if (str->video_channel != -1)
  av_log (s, AV_LOG_DEBUG, " video channel = %d, %d x %d %d\n", str->video_channel,
    str->channels[str->video_channel].width,
    str->channels[str->video_channel].height,str->channels[str->video_channel].video_stream_index);
if (str->audio_channel != -1)
   av_log (s, AV_LOG_DEBUG, " audio channel = %d, %d Hz, %d channels, %d bits/sample %d\n",
    str->audio_channel,
    str->channels[str->audio_channel].sample_rate,
    str->channels[str->audio_channel].channels,
    str->channels[str->audio_channel].bits,str->channels[str->audio_channel].audio_stream_index);

    /* back to the start */
    url_fseek(pb, start, SEEK_SET);

    return 0;
}

static int str_read_packet(AVFormatContext *s,
                           AVPacket *ret_pkt)
{
    ByteIOContext *pb = &s->pb;
    StrDemuxContext *str = (StrDemuxContext *)s->priv_data;
    unsigned char sector[RAW_CD_SECTOR_SIZE];
    int channel;
    int packet_read = 0;
    int ret = 0;
    AVPacket *pkt;

    while (!packet_read) {

        if (get_buffer(pb, sector, RAW_CD_SECTOR_SIZE) != RAW_CD_SECTOR_SIZE)
            return AVERROR_IO;

        channel = sector[0x11];
        if (channel >= 32)
            return AVERROR_INVALIDDATA;

        switch (sector[0x12] & CDXA_TYPE_MASK) {

        case CDXA_TYPE_DATA:
        case CDXA_TYPE_VIDEO:
            /* check if this the video channel we care about */
            if (channel == str->video_channel) {

                int current_sector = AV_RL16(&sector[0x1C]);
                int sector_count   = AV_RL16(&sector[0x1E]);
                int frame_size = AV_RL32(&sector[0x24]);
                int bytes_to_copy;
//        printf("%d %d %d\n",current_sector,sector_count,frame_size);
                /* if this is the first sector of the frame, allocate a pkt */
                pkt = &str->tmp_pkt;
                if (current_sector == 0) {
                    if (av_new_packet(pkt, frame_size))
                        return AVERROR_IO;

                    pkt->pos= url_ftell(pb) - RAW_CD_SECTOR_SIZE;
                    pkt->stream_index =
                        str->channels[channel].video_stream_index;
               //     pkt->pts = str->pts;

                    /* if there is no audio, adjust the pts after every video
                     * frame; assume 15 fps */
                   if (str->audio_channel != -1)
                       str->pts += (90000 / 15);
                }

                /* load all the constituent chunks in the video packet */
                bytes_to_copy = frame_size - current_sector*VIDEO_DATA_CHUNK_SIZE;
                if (bytes_to_copy>0) {
                    if (bytes_to_copy>VIDEO_DATA_CHUNK_SIZE) bytes_to_copy=VIDEO_DATA_CHUNK_SIZE;
                    memcpy(pkt->data + current_sector*VIDEO_DATA_CHUNK_SIZE,
                        sector + VIDEO_DATA_HEADER_SIZE, bytes_to_copy);
                }
                if (current_sector == sector_count-1) {
                    *ret_pkt = *pkt;
                    return 0;
                }

            }
            break;

        case CDXA_TYPE_AUDIO:
#ifdef PRINTSTUFF
printf (" dropping audio sector\n");
#endif
#if 1
            /* check if this the video channel we care about */
            if (channel == str->audio_channel) {
                pkt = ret_pkt;
                if (av_new_packet(pkt, 2304))
                    return AVERROR_IO;
                memcpy(pkt->data,sector+24,2304);

                pkt->stream_index =
                    str->channels[channel].audio_stream_index;
                //pkt->pts = str->pts;
                return 0;
            }
#endif
            break;
        default:
            /* drop the sector and move on */
#ifdef PRINTSTUFF
printf (" dropping other sector\n");
#endif
            break;
        }

        if (url_feof(pb))
            return AVERROR_IO;
    }

    return ret;
}

static int str_read_close(AVFormatContext *s)
{
    StrDemuxContext *str = (StrDemuxContext *)s->priv_data;

    av_free(str->video_chunk);

    return 0;
}

AVInputFormat str_demuxer = {
    "psxstr",
    "Sony Playstation STR format",
    sizeof(StrDemuxContext),
    str_probe,
    str_read_header,
    str_read_packet,
    str_read_close,
};
