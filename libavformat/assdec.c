/*
 * SSA/ASS demuxer
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

#define MAX_LINESIZE 2000

typedef struct ASSContext{
    uint8_t *event_buffer;
    uint8_t **event;
    unsigned int event_count;
    unsigned int event_index;
}ASSContext;

static void get_line(ByteIOContext *s, char *buf, int maxlen)
{
    int i = 0;
    char c;

    do{
        c = get_byte(s);
        if (i < maxlen-1)
            buf[i++] = c;
    }while(c != '\n' && c);

    buf[i] = 0;
}

static int probe(AVProbeData *p)
{
    const char *header= "[Script Info]";

    if(   !memcmp(p->buf  , header, strlen(header))
       || !memcmp(p->buf+3, header, strlen(header)))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int read_close(AVFormatContext *s)
{
    ASSContext *ass = s->priv_data;

    av_freep(&ass->event_buffer);
    av_freep(&ass->event);

    return 0;
}

static int64_t get_pts(const uint8_t *p)
{
    int hour, min, sec, hsec;

    if(sscanf(p, "%*[^,],%d:%d:%d%*c%d", &hour, &min, &sec, &hsec) != 4)
        return AV_NOPTS_VALUE;

//    av_log(NULL, AV_LOG_ERROR, "%d %d %d %d %d [%s]\n", i, hour, min, sec, hsec, p);

    min+= 60*hour;
    sec+= 60*min;

    return sec*100+hsec;
}

static int event_cmp(uint8_t **a, uint8_t **b)
{
    return get_pts(*a) - get_pts(*b);
}

static int read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    int i, header_remaining;
    ASSContext *ass = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st;
    int allocated[2]={0};
    uint8_t *p, **dst[2]={0};
    int pos[2]={0};

    st = av_new_stream(s, 0);
    if (!st)
        return -1;
    av_set_pts_info(st, 64, 1, 100);
    st->codec->codec_type = CODEC_TYPE_SUBTITLE;
    st->codec->codec_id= CODEC_ID_SSA;

    header_remaining= INT_MAX;
    dst[0] = &st->codec->extradata;
    dst[1] = &ass->event_buffer;
    while(!url_feof(pb)){
        uint8_t line[MAX_LINESIZE];

        get_line(pb, line, sizeof(line));

        if(!memcmp(line, "[Events]", 8))
            header_remaining= 2;
        else if(line[0]=='[')
            header_remaining= INT_MAX;

        i= header_remaining==0;

        if(i && get_pts(line) == AV_NOPTS_VALUE)
            continue;

        p = av_fast_realloc(*(dst[i]), &allocated[i], pos[i]+MAX_LINESIZE);
        if(!p)
            goto fail;
        *(dst[i])= p;
        memcpy(p + pos[i], line, strlen(line)+1);
        pos[i] += strlen(line);
        if(i) ass->event_count++;
        else  header_remaining--;
    }
    st->codec->extradata_size= pos[0];

    if(ass->event_count >= UINT_MAX / sizeof(*ass->event))
        goto fail;

    ass->event= av_malloc(ass->event_count * sizeof(*ass->event));
    p= ass->event_buffer;
    for(i=0; i<ass->event_count; i++){
        ass->event[i]= p;
        while(*p && *p != '\n')
            p++;
        p++;
    }

    qsort(ass->event, ass->event_count, sizeof(*ass->event), event_cmp);

    return 0;

fail:
    read_close(s);

    return -1;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASSContext *ass = s->priv_data;
    uint8_t *p, *end;

    if(ass->event_index >= ass->event_count)
        return AVERROR(EIO);

    p= ass->event[ ass->event_index ];

    end= strchr(p, '\n');
    av_new_packet(pkt, end ? end-p+1 : strlen(p));
    pkt->flags |= PKT_FLAG_KEY;
    pkt->pos= p - ass->event_buffer + s->streams[0]->codec->extradata_size;
    pkt->pts= pkt->dts= get_pts(p);
    memcpy(pkt->data, p, pkt->size);

    ass->event_index++;

    return 0;
}

AVInputFormat ass_demuxer = {
    "ass",
    NULL_IF_CONFIG_SMALL("SSA/ASS format"),
    sizeof(ASSContext),
    probe,
    read_header,
    read_packet,
    read_close,
//    read_seek,
};
