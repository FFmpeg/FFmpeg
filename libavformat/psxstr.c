/*
 * Sony Playstation (PSX) STR File Demuxer
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

#define RIFF_TAG FOURCC_TAG('R', 'I', 'F', 'F')
#define CDXA_TAG FOURCC_TAG('C', 'D', 'X', 'A')

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
} StrDemuxContext;

static int str_probe(AVProbeData *p)
{
    int start;

    /* need at least 0x38 bytes to validate */
    if (p->buf_size < 0x38)
        return 0;

    if ((LE_32(&p->buf[0]) == RIFF_TAG) &&
        (LE_32(&p->buf[8]) == CDXA_TAG)) {

        /* RIFF header seen; skip 0x2C bytes */
        start = RIFF_HEADER_SIZE;
    } else
        start = 0;

    /* look for CD sync header (00, 0xFF x 10, 00) */
    if ((p->buf[start + 0] != 0x00) || (p->buf[start + 1] != 0xFF) ||
        (p->buf[start + 2] != 0xFF) || (p->buf[start + 3] != 0xFF) ||
        (p->buf[start + 4] != 0xFF) || (p->buf[start + 5] != 0xFF) ||
        (p->buf[start + 6] != 0xFF) || (p->buf[start + 7] != 0xFF) ||
        (p->buf[start + 8] != 0xFF) || (p->buf[start + 9] != 0xFF) ||
        (p->buf[start + 10] != 0xFF) || (p->buf[start + 11] != 0x00))
        return 0;

    /* MPEG files (like those ripped from VCDs) can also look like this;
     * only return half certainty */
    return 50;
}

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

    /* set the pts reference (1 pts = 1/90000) */
    s->pts_num = 1;
    s->pts_den = 90000;

    /* skip over any RIFF header */
    if (get_buffer(pb, sector, RIFF_HEADER_SIZE) != RIFF_HEADER_SIZE)
        return AVERROR_IO;
    if (LE_32(&sector[0]) == RIFF_TAG)
        start = RIFF_HEADER_SIZE;
    else
        start = 0;

    url_fseek(pb, start, SEEK_SET);

    /* check through the first 32 sectors for individual channels */
    for (i = 0; i < 32; i++) {
        if (get_buffer(pb, sector, RAW_CD_SECTOR_SIZE) != RAW_CD_SECTOR_SIZE)
            return AVERROR_IO;

        channel = sector[0x11];
        if (channel >= 32)
            return AVERROR_INVALIDDATA;

        switch (sector[0x12] & CDXA_TYPE_MASK) {

        case CDXA_TYPE_DATA:
        case CDXA_TYPE_VIDEO:
            /* check if this channel gets to be the dominant video channel */
            if (str->video_channel == -1) {
                /* qualify the magic number */
                if (LE_32(&sector[0x18]) != STR_MAGIC)
                    break;
                str->video_channel = channel;
                str->channels[channel].type = STR_VIDEO;
                str->channels[channel].width = LE_16(&sector[0x28]);
                str->channels[channel].height = LE_16(&sector[0x2A]);

                /* allocate a new AVStream */
                st = av_new_stream(s, 0);
                if (!st)
                    return AVERROR_NOMEM;

                str->channels[channel].video_stream_index = st->index;

                st->codec.codec_type = CODEC_TYPE_VIDEO;
                st->codec.codec_id = CODEC_ID_MDEC; 
                st->codec.codec_tag = 0;  /* no fourcc */
                st->codec.width = str->channels[channel].width;
                st->codec.height = str->channels[channel].height;
            }
            break;

        case CDXA_TYPE_AUDIO:
            /* check if this channel gets to be the dominant audio channel */
            if (str->audio_channel == -1) {
                str->audio_channel = channel;
                str->channels[channel].type = STR_AUDIO;
                str->channels[channel].channels = 
                    (sector[0x13] & 0x01) ? 2 : 1;
                str->channels[channel].sample_rate = 
                    (sector[0x13] & 0x04) ? 18900 : 37800;
                str->channels[channel].bits = 
                    (sector[0x13] & 0x10) ? 8 : 4;
            }
            break;

        default:
            /* ignore */
            break;
        }
    }

if (str->video_channel != -1)
  printf (" video channel = %d, %d x %d\n", str->video_channel,
    str->channels[str->video_channel].width,
    str->channels[str->video_channel].height);
if (str->audio_channel != -1)
  printf (" audio channel = %d, %d Hz, %d channels, %d bits/sample\n", 
    str->video_channel,
    str->channels[str->video_channel].sample_rate,
    str->channels[str->video_channel].channels,
    str->channels[str->video_channel].bits);

    /* back to the start */
    url_fseek(pb, start, SEEK_SET);

    return 0;
}

static int str_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    ByteIOContext *pb = &s->pb;
    StrDemuxContext *str = (StrDemuxContext *)s->priv_data;
    unsigned char sector[RAW_CD_SECTOR_SIZE];
    int channel;
    int packet_read = 0;
    int video_sector_count = 0;
    int current_video_sector = 0;
    int video_frame_size = 0;
    int video_frame_index = 0;
    int bytes_to_copy;
    int ret = 0;

    while (!packet_read) {

        if (get_buffer(pb, sector, RAW_CD_SECTOR_SIZE) != RAW_CD_SECTOR_SIZE)
            return -EIO;

        channel = sector[0x11];
        if (channel >= 32)
            return AVERROR_INVALIDDATA;

        switch (sector[0x12] & CDXA_TYPE_MASK) {

        case CDXA_TYPE_DATA:
        case CDXA_TYPE_VIDEO:
            /* check if this the video channel we care about */
            if (channel == str->video_channel) {

                /* if this is the first sector of the frame, allocate a pkt */
                if (current_video_sector == 0) {
                    video_frame_size = LE_32(&sector[0x24]);
                    video_sector_count = LE_16(&sector[0x1E]);
                    if (av_new_packet(pkt, video_frame_size))
                        return -EIO;

                    pkt->stream_index = 
                        str->channels[channel].video_stream_index;
                    pkt->pts = str->pts;

                    /* if there is no audio, adjust the pts after every video
                     * frame; assume 15 fps */
                   if (str->audio_channel != -1)
                       str->pts += (90000 / 15);
                }

                /* load all the constituent chunks in the video packet */
                if (video_frame_size - video_frame_index < VIDEO_DATA_CHUNK_SIZE)
                    bytes_to_copy = video_frame_size - video_frame_index;
                else
                    bytes_to_copy = VIDEO_DATA_CHUNK_SIZE;
                if (video_frame_index < video_frame_size)
                    memcpy(&pkt->data[video_frame_index],
                        &sector[VIDEO_DATA_HEADER_SIZE], bytes_to_copy);

#ifdef PRINTSTUFF
printf ("  chunk %d/%d (bytes %d/%d), first 6 bytes = %02X %02X %02X %02X %02X %02X\n",
  current_video_sector, video_sector_count,
  video_frame_index, video_frame_size,
  pkt->data[current_video_sector * VIDEO_DATA_CHUNK_SIZE + 0],
  pkt->data[current_video_sector * VIDEO_DATA_CHUNK_SIZE + 1],
  pkt->data[current_video_sector * VIDEO_DATA_CHUNK_SIZE + 2],
  pkt->data[current_video_sector * VIDEO_DATA_CHUNK_SIZE + 3],
  pkt->data[current_video_sector * VIDEO_DATA_CHUNK_SIZE + 4],
  pkt->data[current_video_sector * VIDEO_DATA_CHUNK_SIZE + 5]);
#endif

                video_frame_index += bytes_to_copy;
                /* must keep reading sectors until all current video sectors
                 * are consumed */
                current_video_sector++;
                if (current_video_sector >= video_sector_count)
                    packet_read = 1;

            }
            break;

        case CDXA_TYPE_AUDIO:
#ifdef PRINTSTUFF
printf (" dropping audio sector\n");
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
            return -EIO;
    }

    return ret;
}

static int str_read_close(AVFormatContext *s)
{
    StrDemuxContext *str = (StrDemuxContext *)s->priv_data;

    av_free(str->video_chunk);

    return 0;
}

static AVInputFormat str_iformat = {
    "psxstr",
    "Sony Playstation STR format",
    sizeof(StrDemuxContext),
    str_probe,
    str_read_header,
    str_read_packet,
    str_read_close,
};

int str_init(void)
{
    av_register_input_format(&str_iformat);
    return 0;
}
