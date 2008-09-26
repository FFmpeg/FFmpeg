/*
 * SSA/ASS muxer
 * Copyright (c) 2008 Michael Niedermayer
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

typedef struct ASSContext{
    unsigned int extra_index;
}ASSContext;

static int write_header(AVFormatContext *s)
{
    ASSContext *ass = s->priv_data;
    AVCodecContext *avctx= s->streams[0]->codec;
    uint8_t *last= NULL;

    if(s->nb_streams != 1 || avctx->codec_id != CODEC_ID_SSA){
        av_log(s, AV_LOG_ERROR, "Exactly one ASS/SSA stream is needed.\n");
        return -1;
    }

    while(ass->extra_index < avctx->extradata_size){
        uint8_t *p  = avctx->extradata + ass->extra_index;
        uint8_t *end= strchr(p, '\n');
        if(!end) end= avctx->extradata + avctx->extradata_size;
        else     end++;

        put_buffer(s->pb, p, end-p);
        ass->extra_index += end-p;

        if(last && !memcmp(last, "[Events]", 8))
            break;
        last=p;
    }

    put_flush_packet(s->pb);

    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    put_buffer(s->pb, pkt->data, pkt->size);

    put_flush_packet(s->pb);

    return 0;
}

static int write_trailer(AVFormatContext *s)
{
    ASSContext *ass = s->priv_data;
    AVCodecContext *avctx= s->streams[0]->codec;

    put_buffer(s->pb, avctx->extradata      + ass->extra_index,
                      avctx->extradata_size - ass->extra_index);

    put_flush_packet(s->pb);

    return 0;
}

AVOutputFormat ass_muxer = {
    "ass",
    NULL_IF_CONFIG_SMALL("SSA/ASS format"),
    NULL,
    "ass,ssa",
    sizeof(ASSContext),
    CODEC_ID_NONE,
    CODEC_ID_NONE,
    write_header,
    write_packet,
    write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_NOTIMESTAMPS
};
