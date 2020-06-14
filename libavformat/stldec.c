/*
 * Copyright (c) 2014 Eejya Singh
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
 * STL subtitles format demuxer
 * @see https://documentation.apple.com/en/dvdstudiopro/usermanual/index.html#chapter=19%26section=13%26tasks=true
 */

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/avstring.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} STLContext;

static int stl_probe(AVProbeData *p)
{
    char c;
    const unsigned char *ptr = p->buf;

    if (AV_RB24(ptr) == 0xEFBBBF)
        ptr += 3;  /* skip UTF-8 BOM */

    while (*ptr == '\r' || *ptr == '\n' || *ptr == '$' || !strncmp(ptr, "//" , 2))
        ptr += ff_subtitles_next_line(ptr);

    if (sscanf(ptr, "%*d:%*d:%*d:%*d , %*d:%*d:%*d:%*d , %c", &c) == 1)
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int64_t get_pts(char **buf, int *duration)
{
    int hh1, mm1, ss1, ms1;
    int hh2, mm2, ss2, ms2;
    int len = 0;

    if (sscanf(*buf, "%2d:%2d:%2d:%2d , %2d:%2d:%2d:%2d , %n",
                &hh1, &mm1, &ss1, &ms1,
                &hh2, &mm2, &ss2, &ms2, &len) >= 8 && len > 0) {
        int64_t start = (hh1*3600LL + mm1*60LL + ss1) * 100LL + ms1;
        int64_t end = (hh2*3600LL + mm2*60LL + ss2) * 100LL + ms2;
        *duration = end - start;
        *buf += len;
        return start;
    }
    return AV_NOPTS_VALUE;
}

static int stl_read_header(AVFormatContext *s)
{
    STLContext *stl = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 100);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_STL;

    while (!avio_feof(s->pb)) {
        char line[4096];
        char *p = line;
        const int64_t pos = avio_tell(s->pb);
        int len = ff_get_line(s->pb, line, sizeof(line));
        int64_t pts_start;
        int duration;

        if (!len)
            break;

        line[strcspn(line, "\r\n")] = 0;
        pts_start = get_pts(&p , &duration);

        if (pts_start != AV_NOPTS_VALUE) {
            AVPacket *sub;
            sub = ff_subtitles_queue_insert(&stl->q, p, strlen(p), 0);
            if (!sub) {
                ff_subtitles_queue_clean(&stl->q);
                return AVERROR(ENOMEM);
            }
            sub->pos = pos;
            sub->pts = pts_start;
            sub->duration = duration;
        }
    }
    ff_subtitles_queue_finalize(s, &stl->q);
    return 0;
}
static int stl_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    STLContext *stl = s->priv_data;
    return ff_subtitles_queue_read_packet(&stl->q, pkt);
}

static int stl_read_seek(AVFormatContext *s, int stream_index,
                             int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    STLContext *stl = s->priv_data;
    return ff_subtitles_queue_seek(&stl->q, s, stream_index,
                                   min_ts, ts, max_ts, flags);
}

static int stl_read_close(AVFormatContext *s)
{
    STLContext *stl = s->priv_data;
    ff_subtitles_queue_clean(&stl->q);
    return 0;
}

AVInputFormat ff_stl_demuxer = {
    .name           = "stl",
    .long_name      = NULL_IF_CONFIG_SMALL("Spruce subtitle format"),
    .priv_data_size = sizeof(STLContext),
    .read_probe     = stl_probe,
    .read_header    = stl_read_header,
    .read_packet    = stl_read_packet,
    .read_seek2     = stl_read_seek,
    .read_close     = stl_read_close,
    .extensions     = "stl",
};
