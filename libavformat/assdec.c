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

#include "libavutil/mathematics.h"
#include "avformat.h"
#include "internal.h"

#define MAX_LINESIZE 2000

typedef struct ASSContext{
    uint8_t *event_buffer;
    uint8_t **event;
    unsigned int event_count;
    unsigned int event_index;
}ASSContext;

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
    int i, len, header_remaining;
    ASSContext *ass = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    int allocated[2]={0};
    uint8_t *p, **dst[2]={0};
    int pos[2]={0};

    st = avformat_new_stream(s, NULL);
    if (!st)
        return -1;
    avpriv_set_pts_info(st, 64, 1, 100);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id= CODEC_ID_SSA;

    header_remaining= INT_MAX;
    dst[0] = &st->codec->extradata;
    dst[1] = &ass->event_buffer;
    while(!url_feof(pb)){
        uint8_t line[MAX_LINESIZE];

        len = ff_get_line(pb, line, sizeof(line));

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
        memcpy(p + pos[i], line, len+1);
        pos[i] += len;
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

    qsort(ass->event, ass->event_count, sizeof(*ass->event), (void*)event_cmp);

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
    pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->pos= p - ass->event_buffer + s->streams[0]->codec->extradata_size;
    pkt->pts= pkt->dts= get_pts(p);
    memcpy(pkt->data, p, pkt->size);

    ass->event_index++;

    return 0;
}

static int read_seek2(AVFormatContext *s, int stream_index,
                      int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    ASSContext *ass = s->priv_data;

    if (flags & AVSEEK_FLAG_BYTE) {
        return AVERROR(ENOSYS);
    } else if (flags & AVSEEK_FLAG_FRAME) {
        if (ts < 0 || ts >= ass->event_count)
            return AVERROR(ERANGE);
        ass->event_index = ts;
    } else {
        int i, idx = -1;
        int64_t min_ts_diff = INT64_MAX;
        if (stream_index == -1) {
            AVRational time_base = s->streams[0]->time_base;
            ts = av_rescale_q(ts, AV_TIME_BASE_Q, time_base);
            min_ts = av_rescale_rnd(min_ts, time_base.den,
                                    time_base.num * (int64_t)AV_TIME_BASE,
                                    AV_ROUND_UP);
            max_ts = av_rescale_rnd(max_ts, time_base.den,
                                    time_base.num * (int64_t)AV_TIME_BASE,
                                    AV_ROUND_DOWN);
        }
        /* TODO: ass->event[] is sorted by pts so we could do a binary search */
        for (i=0; i<ass->event_count; i++) {
            int64_t pts = get_pts(ass->event[i]);
            int64_t ts_diff = FFABS(pts - ts);
            if (pts >= min_ts && pts <= max_ts && ts_diff < min_ts_diff) {
                min_ts_diff = ts_diff;
                idx = i;
            }
        }
        if (idx < 0)
            return AVERROR(ERANGE);
        ass->event_index = idx;
    }
    return 0;
}

AVInputFormat ff_ass_demuxer = {
    .name           = "ass",
    .long_name      = NULL_IF_CONFIG_SMALL("Advanced SubStation Alpha subtitle format"),
    .priv_data_size = sizeof(ASSContext),
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_close     = read_close,
    .read_seek2     = read_seek2,
};
