/*
 * Copyright (c) 2012 Clément Bœsch
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
 * VPlayer subtitles format demuxer
 */

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} VPlayerContext;

static int vplayer_probe(const AVProbeData *p)
{
    char c;
    const unsigned char *ptr = p->buf;

    if ((sscanf(ptr, "%*3d:%*2d:%*2d.%*2d%c", &c) == 1 ||
         sscanf(ptr, "%*3d:%*2d:%*2d%c",      &c) == 1) && strchr(": =", c))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int64_t read_ts(char **line)
{
    char c;
    int hh, mm, ss, ms, n, len;

    if (((n = sscanf(*line, "%d:%d:%d.%d%c%n", &hh, &mm, &ss, &ms, &c, &len)) >= 5 ||
         (n = sscanf(*line, "%d:%d:%d%c%n",    &hh, &mm, &ss,      &c, &len)) >= 4) && strchr(": =", c)) {
        *line += len;
        return (hh*3600LL + mm*60LL + ss) * 100LL + (n < 5 ? 0 : ms);
    }
    return AV_NOPTS_VALUE;
}

static int vplayer_read_header(AVFormatContext *s)
{
    VPlayerContext *vplayer = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 100);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_VPLAYER;

    while (!avio_feof(s->pb)) {
        char line[4096];
        char *p = line;
        const int64_t pos = avio_tell(s->pb);
        int len = ff_get_line(s->pb, line, sizeof(line));
        int64_t pts_start;

        if (!len)
            break;

        line[strcspn(line, "\r\n")] = 0;

        pts_start = read_ts(&p);
        if (pts_start != AV_NOPTS_VALUE) {
            AVPacket *sub;

            sub = ff_subtitles_queue_insert(&vplayer->q, p, strlen(p), 0);
            if (!sub)
                return AVERROR(ENOMEM);
            sub->pos = pos;
            sub->pts = pts_start;
            sub->duration = -1;
        }
    }

    ff_subtitles_queue_finalize(s, &vplayer->q);
    return 0;
}

static int vplayer_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    VPlayerContext *vplayer = s->priv_data;
    return ff_subtitles_queue_read_packet(&vplayer->q, pkt);
}

static int vplayer_read_seek(AVFormatContext *s, int stream_index,
                             int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    VPlayerContext *vplayer = s->priv_data;
    return ff_subtitles_queue_seek(&vplayer->q, s, stream_index,
                                   min_ts, ts, max_ts, flags);
}

static int vplayer_read_close(AVFormatContext *s)
{
    VPlayerContext *vplayer = s->priv_data;
    ff_subtitles_queue_clean(&vplayer->q);
    return 0;
}

AVInputFormat ff_vplayer_demuxer = {
    .name           = "vplayer",
    .long_name      = NULL_IF_CONFIG_SMALL("VPlayer subtitles"),
    .priv_data_size = sizeof(VPlayerContext),
    .read_probe     = vplayer_probe,
    .read_header    = vplayer_read_header,
    .read_packet    = vplayer_read_packet,
    .read_seek2     = vplayer_read_seek,
    .read_close     = vplayer_read_close,
    .extensions     = "txt",
};
