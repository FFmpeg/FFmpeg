/*
 * SubRip subtitle demuxer
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
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
#include "libavutil/intreadwrite.h"

static int srt_probe(AVProbeData *p)
{
    unsigned char *ptr = p->buf;
    int i, v, num = 0;

    if (AV_RB24(ptr) == 0xEFBBBF)
        ptr += 3;  /* skip UTF-8 BOM */

    for (i=0; i<2; i++) {
        if (num == i && sscanf(ptr, "%*d:%*2d:%*2d%*1[,.]%*3d --> %*d:%*2d:%*2d%*1[,.]%3d", &v) == 1)
            return AVPROBE_SCORE_MAX;
        num = atoi(ptr);
        ptr += strcspn(ptr, "\n") + 1;
    }
    return 0;
}

static int srt_read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id   = AV_CODEC_ID_SRT;
    return 0;
}

static int64_t get_pts(const char *buf, int *duration)
{
    int i, hour, min, sec, hsec;
    int he, me, se, mse;

    for (i=0; i<2; i++) {
        int64_t start, end;
        if (sscanf(buf, "%d:%2d:%2d%*1[,.]%3d --> %d:%2d:%2d%*1[,.]%3d",
                   &hour, &min, &sec, &hsec, &he, &me, &se, &mse) == 8) {
            min += 60*hour;
            sec += 60*min;
            start = sec*1000+hsec;
            me += 60*he;
            se += 60*me;
            end = se*1000+mse;
            *duration = end - start;
            return start;
        }
        buf += strcspn(buf, "\n") + 1;
    }
    return AV_NOPTS_VALUE;
}

static inline int is_eol(char c)
{
    return c == '\r' || c == '\n';
}

static int srt_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    char buffer[2048], *ptr = buffer, *ptr2;
    int64_t pos = avio_tell(s->pb);
    int res = AVERROR_EOF;

    do {
        ptr2 = ptr;
        ptr += ff_get_line(s->pb, ptr, sizeof(buffer)+buffer-ptr);
    } while (!is_eol(*ptr2) && !url_feof(s->pb) && ptr-buffer<sizeof(buffer)-1);

    if (buffer[0] && !(res = av_new_packet(pkt, ptr-buffer))) {
        memcpy(pkt->data, buffer, pkt->size);
        pkt->flags |= AV_PKT_FLAG_KEY;
        pkt->pos = pos;
        pkt->pts = pkt->dts = get_pts(pkt->data, &(pkt->duration));
    }
    return res;
}

AVInputFormat ff_srt_demuxer = {
    .name        = "srt",
    .long_name   = NULL_IF_CONFIG_SMALL("SubRip subtitle"),
    .read_probe  = srt_probe,
    .read_header = srt_read_header,
    .read_packet = srt_read_packet,
    .flags       = AVFMT_GENERIC_INDEX,
};
