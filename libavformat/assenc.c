/*
 * SSA/ASS muxer
 * Copyright (c) 2008 Michael Niedermayer
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avformat.h"

typedef struct ASSContext{
    unsigned int extra_index;
}ASSContext;

static int write_header(AVFormatContext *s)
{
    ASSContext *ass = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;
    uint8_t *last= NULL;

    if(s->nb_streams != 1 || par->codec_id != AV_CODEC_ID_SSA){
        av_log(s, AV_LOG_ERROR, "Exactly one ASS/SSA stream is needed.\n");
        return -1;
    }

    while(ass->extra_index < par->extradata_size){
        uint8_t *p  = par->extradata + ass->extra_index;
        uint8_t *end= strchr(p, '\n');
        if(!end) end= par->extradata + par->extradata_size;
        else     end++;

        avio_write(s->pb, p, end-p);
        ass->extra_index += end-p;

        if(last && !memcmp(last, "[Events]", 8))
            break;
        last=p;
    }

    avio_flush(s->pb);

    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

static int write_trailer(AVFormatContext *s)
{
    ASSContext *ass = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;

    avio_write(s->pb, par->extradata      + ass->extra_index,
                      par->extradata_size - ass->extra_index);

    return 0;
}

AVOutputFormat ff_ass_muxer = {
    .name           = "ass",
    .long_name      = NULL_IF_CONFIG_SMALL("SSA (SubStation Alpha) subtitle"),
    .mime_type      = "text/x-ssa",
    .extensions     = "ass,ssa",
    .priv_data_size = sizeof(ASSContext),
    .subtitle_codec = AV_CODEC_ID_SSA,
    .write_header   = write_header,
    .write_packet   = write_packet,
    .write_trailer  = write_trailer,
    .flags          = AVFMT_GLOBALHEADER | AVFMT_NOTIMESTAMPS,
};
