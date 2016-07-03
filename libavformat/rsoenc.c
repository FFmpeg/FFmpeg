/*
 * RSO muxer
 * Copyright (c) 2001 Fabrice Bellard (original AU code)
 * Copyright (c) 2010 Rafael Carre
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

#include "avformat.h"
#include "internal.h"
#include "riff.h"
#include "rso.h"

static int rso_write_header(AVFormatContext *s)
{
    AVIOContext  *pb  = s->pb;
    AVCodecParameters *par = s->streams[0]->codecpar;

    if (!par->codec_tag)
        return AVERROR_INVALIDDATA;

    if (par->channels != 1) {
        av_log(s, AV_LOG_ERROR, "RSO only supports mono\n");
        return AVERROR_INVALIDDATA;
    }

    if (!s->pb->seekable) {
        av_log(s, AV_LOG_ERROR, "muxer does not support non seekable output\n");
        return AVERROR_INVALIDDATA;
    }

    /* XXX: find legal sample rates (if any) */
    if (par->sample_rate >= 1u<<16) {
        av_log(s, AV_LOG_ERROR, "Sample rate must be < 65536\n");
        return AVERROR_INVALIDDATA;
    }

    if (par->codec_id == AV_CODEC_ID_ADPCM_IMA_WAV) {
        av_log(s, AV_LOG_ERROR, "ADPCM in RSO not implemented\n");
        return AVERROR_PATCHWELCOME;
    }

    /* format header */
    avio_wb16(pb, par->codec_tag);   /* codec ID */
    avio_wb16(pb, 0);                /* data size, will be written at EOF */
    avio_wb16(pb, par->sample_rate);
    avio_wb16(pb, 0x0000);           /* play mode ? (0x0000 = don't loop) */

    avio_flush(pb);

    return 0;
}

static int rso_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

static int rso_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int64_t file_size;
    uint16_t coded_file_size;

    file_size = avio_tell(pb);

    if (file_size < 0)
        return file_size;

    if (file_size > 0xffff + RSO_HEADER_SIZE) {
        av_log(s, AV_LOG_WARNING,
               "Output file is too big (%"PRId64" bytes >= 64kB)\n", file_size);
        coded_file_size = 0xffff;
    } else {
        coded_file_size = file_size - RSO_HEADER_SIZE;
    }

    /* update file size */
    avio_seek(pb, 2, SEEK_SET);
    avio_wb16(pb, coded_file_size);
    avio_seek(pb, file_size, SEEK_SET);

    return 0;
}

AVOutputFormat ff_rso_muxer = {
    .name           =   "rso",
    .long_name      =   NULL_IF_CONFIG_SMALL("Lego Mindstorms RSO"),
    .extensions     =   "rso",
    .audio_codec    =   AV_CODEC_ID_PCM_U8,
    .video_codec    =   AV_CODEC_ID_NONE,
    .write_header   =   rso_write_header,
    .write_packet   =   rso_write_packet,
    .write_trailer  =   rso_write_trailer,
    .codec_tag      =   (const AVCodecTag* const []){ff_codec_rso_tags, 0},
    .flags          =   AVFMT_NOTIMESTAMPS,
};
