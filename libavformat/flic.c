/*
 * FLI/FLC Animation File Demuxer
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
 * @file flic.c
 * FLI/FLC file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the .fli/.flc file format and all of its many
 * variations, visit:
 *   http://www.compuphase.com/flic.htm
 *
 * This demuxer handles standard 0xAF11- and 0xAF12-type FLIs. It also
 * handles special FLIs from the PC game "Magic Carpet".
 */

#include "avformat.h"

#define FLIC_FILE_MAGIC_1 0xAF11
#define FLIC_FILE_MAGIC_2 0xAF12
#define FLIC_CHUNK_MAGIC_1 0xF1FA
#define FLIC_CHUNK_MAGIC_2 0xF5FA
#define FLIC_MC_PTS_INC 6000  /* pts increment for Magic Carpet game FLIs */
#define FLIC_DEFAULT_PTS_INC 6000  /* for FLIs that have 0 speed */

#define FLIC_HEADER_SIZE 128
#define FLIC_PREAMBLE_SIZE 6

typedef struct FlicDemuxContext {
    int frame_pts_inc;
    int64_t pts;
    int video_stream_index;
} FlicDemuxContext;

static int flic_probe(AVProbeData *p)
{
    int magic_number;

    if (p->buf_size < 6)
        return 0;

    magic_number = LE_16(&p->buf[4]);
    if ((magic_number != FLIC_FILE_MAGIC_1) &&
        (magic_number != FLIC_FILE_MAGIC_2))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int flic_read_header(AVFormatContext *s,
                            AVFormatParameters *ap)
{
    FlicDemuxContext *flic = (FlicDemuxContext *)s->priv_data;
    ByteIOContext *pb = &s->pb;
    unsigned char header[FLIC_HEADER_SIZE];
    AVStream *st;
    int speed;
    int magic_number;

    flic->pts = 0;

    /* load the whole header and pull out the width and height */
    if (get_buffer(pb, header, FLIC_HEADER_SIZE) != FLIC_HEADER_SIZE)
        return AVERROR_IO;

    magic_number = LE_16(&header[4]);
    speed = LE_32(&header[0x10]);

    /* initialize the decoder streams */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;
    flic->video_stream_index = st->index;
    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_FLIC;
    st->codec->codec_tag = 0;  /* no fourcc */
    st->codec->width = LE_16(&header[0x08]);
    st->codec->height = LE_16(&header[0x0A]);

    if (!st->codec->width || !st->codec->height)
        return AVERROR_INVALIDDATA;

    /* send over the whole 128-byte FLIC header */
    st->codec->extradata_size = FLIC_HEADER_SIZE;
    st->codec->extradata = av_malloc(FLIC_HEADER_SIZE);
    memcpy(st->codec->extradata, header, FLIC_HEADER_SIZE);

    av_set_pts_info(st, 33, 1, 90000);

    /* Time to figure out the framerate: If there is a FLIC chunk magic
     * number at offset 0x10, assume this is from the Bullfrog game,
     * Magic Carpet. */
    if (LE_16(&header[0x10]) == FLIC_CHUNK_MAGIC_1) {

        flic->frame_pts_inc = FLIC_MC_PTS_INC;

        /* rewind the stream since the first chunk is at offset 12 */
        url_fseek(pb, 12, SEEK_SET);

        /* send over abbreviated FLIC header chunk */
        av_free(st->codec->extradata);
        st->codec->extradata_size = 12;
        st->codec->extradata = av_malloc(12);
        memcpy(st->codec->extradata, header, 12);

    } else if (magic_number == FLIC_FILE_MAGIC_1) {
        /*
         * in this case, the speed (n) is number of 1/70s ticks between frames:
         *
         *    pts        n * frame #
         *  --------  =  -----------  => pts = n * (90000/70) * frame #
         *   90000           70
         *
         *  therefore, the frame pts increment = n * 1285.7
         */
        flic->frame_pts_inc = speed * 1285.7;
    } else if (magic_number == FLIC_FILE_MAGIC_2) {
        /*
         * in this case, the speed (n) is number of milliseconds between frames:
         *
         *    pts        n * frame #
         *  --------  =  -----------  => pts = n * 90 * frame #
         *   90000          1000
         *
         *  therefore, the frame pts increment = n * 90
         */
        flic->frame_pts_inc = speed * 90;
    } else
        return AVERROR_INVALIDDATA;

    if (flic->frame_pts_inc == 0)
        flic->frame_pts_inc = FLIC_DEFAULT_PTS_INC;

    return 0;
}

static int flic_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    FlicDemuxContext *flic = (FlicDemuxContext *)s->priv_data;
    ByteIOContext *pb = &s->pb;
    int packet_read = 0;
    unsigned int size;
    int magic;
    int ret = 0;
    unsigned char preamble[FLIC_PREAMBLE_SIZE];

    while (!packet_read) {

        if ((ret = get_buffer(pb, preamble, FLIC_PREAMBLE_SIZE)) !=
            FLIC_PREAMBLE_SIZE) {
            ret = AVERROR_IO;
            break;
        }

        size = LE_32(&preamble[0]);
        magic = LE_16(&preamble[4]);

        if (((magic == FLIC_CHUNK_MAGIC_1) || (magic == FLIC_CHUNK_MAGIC_2)) && size > FLIC_PREAMBLE_SIZE) {
            if (av_new_packet(pkt, size)) {
                ret = AVERROR_IO;
                break;
            }
            pkt->stream_index = flic->video_stream_index;
            pkt->pts = flic->pts;
            pkt->pos = url_ftell(pb); 
            memcpy(pkt->data, preamble, FLIC_PREAMBLE_SIZE);
            ret = get_buffer(pb, pkt->data + FLIC_PREAMBLE_SIZE, 
                size - FLIC_PREAMBLE_SIZE);
            if (ret != size - FLIC_PREAMBLE_SIZE) {
                av_free_packet(pkt);
                ret = AVERROR_IO;
            }
            flic->pts += flic->frame_pts_inc;
            packet_read = 1;
        } else {
            /* not interested in this chunk */
            url_fseek(pb, size - 6, SEEK_CUR);
        }
    }

    return ret;
}

static int flic_read_close(AVFormatContext *s)
{
//    FlicDemuxContext *flic = (FlicDemuxContext *)s->priv_data;

    return 0;
}

static AVInputFormat flic_iformat = {
    "flic",
    "FLI/FLC animation format",
    sizeof(FlicDemuxContext),
    flic_probe,
    flic_read_header,
    flic_read_packet,
    flic_read_close,
};

int flic_init(void)
{
    av_register_input_format(&flic_iformat);
    return 0;
}
