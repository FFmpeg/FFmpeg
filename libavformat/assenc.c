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
#include "internal.h"

typedef struct ASSContext{
    unsigned int extra_index;
    int write_ts; // 0: ssa (timing in payload), 1: ass (matroska like)
}ASSContext;

static int write_header(AVFormatContext *s)
{
    ASSContext *ass = s->priv_data;
    AVCodecContext *avctx= s->streams[0]->codec;
    uint8_t *last= NULL;

    if (s->nb_streams != 1 || (avctx->codec_id != AV_CODEC_ID_SSA &&
                               avctx->codec_id != AV_CODEC_ID_ASS)) {
        av_log(s, AV_LOG_ERROR, "Exactly one ASS/SSA stream is needed.\n");
        return -1;
    }
    ass->write_ts = avctx->codec_id == AV_CODEC_ID_ASS;
    avpriv_set_pts_info(s->streams[0], 64, 1, 100);

    while(ass->extra_index < avctx->extradata_size){
        uint8_t *p  = avctx->extradata + ass->extra_index;
        uint8_t *end= strchr(p, '\n');
        if(!end) end= avctx->extradata + avctx->extradata_size;
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
    ASSContext *ass = s->priv_data;

    if (ass->write_ts) {
        long int layer;
        char *p;
        int64_t start = pkt->pts;
        int64_t end   = start + pkt->duration;
        int hh1, mm1, ss1, ms1;
        int hh2, mm2, ss2, ms2;

        p = pkt->data + strcspn(pkt->data, ",") + 1; // skip ReadOrder
        layer = strtol(p, &p, 10);
        if (*p == ',')
            p++;
        hh1 = (int)(start / 360000);    mm1 = (int)(start / 6000) % 60;
        hh2 = (int)(end   / 360000);    mm2 = (int)(end   / 6000) % 60;
        ss1 = (int)(start / 100) % 60;  ms1 = (int)(start % 100);
        ss2 = (int)(end   / 100) % 60;  ms2 = (int)(end   % 100);
        if (hh1 > 9) hh1 = 9, mm1 = 59, ss1 = 59, ms1 = 99;
        if (hh2 > 9) hh2 = 9, mm2 = 59, ss2 = 59, ms2 = 99;
        avio_printf(s->pb, "Dialogue: %ld,%d:%02d:%02d.%02d,%d:%02d:%02d.%02d,%s\r\n",
                    layer, hh1, mm1, ss1, ms1, hh2, mm2, ss2, ms2, p);
    } else {
        avio_write(s->pb, pkt->data, pkt->size);
    }

    return 0;
}

static int write_trailer(AVFormatContext *s)
{
    ASSContext *ass = s->priv_data;
    AVCodecContext *avctx= s->streams[0]->codec;

    avio_write(s->pb, avctx->extradata      + ass->extra_index,
                      avctx->extradata_size - ass->extra_index);

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
    .flags          = AVFMT_GLOBALHEADER | AVFMT_NOTIMESTAMPS | AVFMT_TS_NONSTRICT,
};
