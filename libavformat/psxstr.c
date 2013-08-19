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
 * @file
 * PSX STR file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * This module handles streams that have been ripped from Sony Playstation
 * CD games. This demuxer can handle either raw STR files (which are just
 * concatenations of raw compact disc sectors) or STR files with 0x2C-byte
 * RIFF headers, followed by CD sectors.
 */

#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

#define RIFF_TAG MKTAG('R', 'I', 'F', 'F')
#define CDXA_TAG MKTAG('C', 'D', 'X', 'A')

#define RAW_CD_SECTOR_SIZE      2352
#define RAW_CD_SECTOR_DATA_SIZE 2304
#define VIDEO_DATA_CHUNK_SIZE   0x7E0
#define VIDEO_DATA_HEADER_SIZE  0x38
#define RIFF_HEADER_SIZE        0x2C

#define CDXA_TYPE_MASK     0x0E
#define CDXA_TYPE_DATA     0x08
#define CDXA_TYPE_AUDIO    0x04
#define CDXA_TYPE_VIDEO    0x02

#define STR_MAGIC (0x80010160)

typedef struct StrChannel {
    /* video parameters */
    int video_stream_index;
    AVPacket tmp_pkt;

    /* audio parameters */
    int audio_stream_index;
} StrChannel;

typedef struct StrDemuxContext {

    /* a STR file can contain up to 32 channels of data */
    StrChannel channels[32];
} StrDemuxContext;

static const uint8_t sync_header[12] = {0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00};

static int str_probe(AVProbeData *p)
{
    const uint8_t *sector= p->buf;
    const uint8_t *end= sector + p->buf_size;
    int aud=0, vid=0;

    if (p->buf_size < RAW_CD_SECTOR_SIZE)
        return 0;

    if ((AV_RL32(&p->buf[0]) == RIFF_TAG) &&
        (AV_RL32(&p->buf[8]) == CDXA_TAG)) {

        /* RIFF header seen; skip 0x2C bytes */
        sector += RIFF_HEADER_SIZE;
    }

    while (end - sector >= RAW_CD_SECTOR_SIZE) {
        /* look for CD sync header (00, 0xFF x 10, 00) */
        if (memcmp(sector,sync_header,sizeof(sync_header)))
            return 0;

        if (sector[0x11] >= 32)
            return 0;

        switch (sector[0x12] & CDXA_TYPE_MASK) {
        case CDXA_TYPE_DATA:
        case CDXA_TYPE_VIDEO: {
                int current_sector = AV_RL16(&sector[0x1C]);
                int sector_count   = AV_RL16(&sector[0x1E]);
                int frame_size = AV_RL32(&sector[0x24]);

                if(!(   frame_size>=0
                     && current_sector < sector_count
                     && sector_count*VIDEO_DATA_CHUNK_SIZE >=frame_size)){
                    return 0;
                }

                /*st->codec->width      = AV_RL16(&sector[0x28]);
                st->codec->height     = AV_RL16(&sector[0x2A]);*/

//                 if (current_sector == sector_count-1) {
                    vid++;
//                 }

            }
            break;
        case CDXA_TYPE_AUDIO:
            if(sector[0x13]&0x2A)
                return 0;
            aud++;
            break;
        default:
            if(sector[0x12] & CDXA_TYPE_MASK)
                return 0;
        }
        sector += RAW_CD_SECTOR_SIZE;
    }
    /* MPEG files (like those ripped from VCDs) can also look like this;
     * only return half certainty */
    if(vid+aud > 3)  return AVPROBE_SCORE_EXTENSION;
    else if(vid+aud) return 1;
    else             return 0;
}

static int str_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    StrDemuxContext *str = s->priv_data;
    unsigned char sector[RAW_CD_SECTOR_SIZE];
    int start;
    int i;

    /* skip over any RIFF header */
    if (avio_read(pb, sector, RIFF_HEADER_SIZE) != RIFF_HEADER_SIZE)
        return AVERROR(EIO);
    if (AV_RL32(&sector[0]) == RIFF_TAG)
        start = RIFF_HEADER_SIZE;
    else
        start = 0;

    avio_seek(pb, start, SEEK_SET);

    for(i=0; i<32; i++){
        str->channels[i].video_stream_index=
        str->channels[i].audio_stream_index= -1;
    }

    s->ctx_flags |= AVFMTCTX_NOHEADER;

    return 0;
}

static int str_read_packet(AVFormatContext *s,
                           AVPacket *ret_pkt)
{
    AVIOContext *pb = s->pb;
    StrDemuxContext *str = s->priv_data;
    unsigned char sector[RAW_CD_SECTOR_SIZE];
    int channel;
    AVPacket *pkt;
    AVStream *st;

    while (1) {

        if (avio_read(pb, sector, RAW_CD_SECTOR_SIZE) != RAW_CD_SECTOR_SIZE)
            return AVERROR(EIO);

        channel = sector[0x11];
        if (channel >= 32)
            return AVERROR_INVALIDDATA;

        switch (sector[0x12] & CDXA_TYPE_MASK) {

        case CDXA_TYPE_DATA:
        case CDXA_TYPE_VIDEO:
            {

                int current_sector = AV_RL16(&sector[0x1C]);
                int sector_count   = AV_RL16(&sector[0x1E]);
                int frame_size = AV_RL32(&sector[0x24]);

                if(!(   frame_size>=0
                     && current_sector < sector_count
                     && sector_count*VIDEO_DATA_CHUNK_SIZE >=frame_size)){
                    av_log(s, AV_LOG_ERROR, "Invalid parameters %d %d %d\n", current_sector, sector_count, frame_size);
                    break;
                }

                if(str->channels[channel].video_stream_index < 0){
                    /* allocate a new AVStream */
                    st = avformat_new_stream(s, NULL);
                    if (!st)
                        return AVERROR(ENOMEM);
                    avpriv_set_pts_info(st, 64, 1, 15);

                    str->channels[channel].video_stream_index = st->index;

                    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
                    st->codec->codec_id   = AV_CODEC_ID_MDEC;
                    st->codec->codec_tag  = 0;  /* no fourcc */
                    st->codec->width      = AV_RL16(&sector[0x28]);
                    st->codec->height     = AV_RL16(&sector[0x2A]);
                }

                /* if this is the first sector of the frame, allocate a pkt */
                pkt = &str->channels[channel].tmp_pkt;

                if(pkt->size != sector_count*VIDEO_DATA_CHUNK_SIZE){
                    if(pkt->data)
                        av_log(s, AV_LOG_ERROR, "missmatching sector_count\n");
                    av_free_packet(pkt);
                    if (av_new_packet(pkt, sector_count*VIDEO_DATA_CHUNK_SIZE))
                        return AVERROR(EIO);

                    pkt->pos= avio_tell(pb) - RAW_CD_SECTOR_SIZE;
                    pkt->stream_index =
                        str->channels[channel].video_stream_index;
                }

                memcpy(pkt->data + current_sector*VIDEO_DATA_CHUNK_SIZE,
                       sector + VIDEO_DATA_HEADER_SIZE,
                       VIDEO_DATA_CHUNK_SIZE);

                if (current_sector == sector_count-1) {
                    pkt->size= frame_size;
                    *ret_pkt = *pkt;
                    pkt->data= NULL;
                    pkt->size= -1;
                    pkt->buf = NULL;
#if FF_API_DESTRUCT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
                    pkt->destruct = NULL;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
                    return 0;
                }

            }
            break;

        case CDXA_TYPE_AUDIO:
            if(str->channels[channel].audio_stream_index < 0){
                int fmt = sector[0x13];
                /* allocate a new AVStream */
                st = avformat_new_stream(s, NULL);
                if (!st)
                    return AVERROR(ENOMEM);

                str->channels[channel].audio_stream_index = st->index;

                st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
                st->codec->codec_id    = AV_CODEC_ID_ADPCM_XA;
                st->codec->codec_tag   = 0;  /* no fourcc */
                if (fmt & 1) {
                    st->codec->channels       = 2;
                    st->codec->channel_layout = AV_CH_LAYOUT_STEREO;
                } else {
                    st->codec->channels       = 1;
                    st->codec->channel_layout = AV_CH_LAYOUT_MONO;
                }
                st->codec->sample_rate = (fmt&4)?18900:37800;
            //    st->codec->bit_rate = 0; //FIXME;
                st->codec->block_align = 128;

                avpriv_set_pts_info(st, 64, 18 * 224 / st->codec->channels,
                                    st->codec->sample_rate);
                st->start_time = 0;
            }
            pkt = ret_pkt;
            if (av_new_packet(pkt, 2304))
                return AVERROR(EIO);
            memcpy(pkt->data,sector+24,2304);

            pkt->stream_index =
                str->channels[channel].audio_stream_index;
            pkt->duration = 1;
            return 0;
        default:
            av_log(s, AV_LOG_WARNING, "Unknown sector type %02X\n", sector[0x12]);
            /* drop the sector and move on */
            break;
        }

        if (url_feof(pb))
            return AVERROR(EIO);
    }
}

static int str_read_close(AVFormatContext *s)
{
    StrDemuxContext *str = s->priv_data;
    int i;
    for(i=0; i<32; i++){
        if(str->channels[i].tmp_pkt.data)
            av_free_packet(&str->channels[i].tmp_pkt);
    }

    return 0;
}

AVInputFormat ff_str_demuxer = {
    .name           = "psxstr",
    .long_name      = NULL_IF_CONFIG_SMALL("Sony Playstation STR"),
    .priv_data_size = sizeof(StrDemuxContext),
    .read_probe     = str_probe,
    .read_header    = str_read_header,
    .read_packet    = str_read_packet,
    .read_close     = str_read_close,
    .flags          = AVFMT_NO_BYTE_SEEK,
};
