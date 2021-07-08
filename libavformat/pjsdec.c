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
 * PJS (Phoenix Japanimation Society) subtitles format demuxer
 *
 * @see http://subs.com.ru/page.php?al=pjs
 */

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} PJSContext;

static int pjs_probe(const AVProbeData *p)
{
    char c;
    int64_t start, end;
    const unsigned char *ptr = p->buf;

    if (sscanf(ptr, "%"SCNd64",%"SCNd64",%c", &start, &end, &c) == 3) {
        size_t q1pos = strcspn(ptr, "\"");
        size_t q2pos = q1pos + strcspn(ptr + q1pos + 1, "\"") + 1;
        if (strcspn(ptr, "\r\n") > q2pos)
            return AVPROBE_SCORE_MAX;
    }
    return 0;
}

static int64_t read_ts(char **line, int *duration)
{
    int64_t start, end;

    if (sscanf(*line, "%"SCNd64",%"SCNd64, &start, &end) == 2) {
        *line += strcspn(*line, "\"");
        *line += !!**line;
        if (end < start || end - (uint64_t)start > INT_MAX)
            return AV_NOPTS_VALUE;
        *duration = end - start;
        return start;
    }
    return AV_NOPTS_VALUE;
}

static int pjs_read_header(AVFormatContext *s)
{
    PJSContext *pjs = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 10);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_PJS;

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

        pts_start = read_ts(&p, &duration);
        if (pts_start != AV_NOPTS_VALUE) {
            AVPacket *sub;

            p[strcspn(p, "\"")] = 0;
            sub = ff_subtitles_queue_insert(&pjs->q, p, strlen(p), 0);
            if (!sub)
                return AVERROR(ENOMEM);
            sub->pos = pos;
            sub->pts = pts_start;
            sub->duration = duration;
        }
    }

    ff_subtitles_queue_finalize(s, &pjs->q);
    return 0;
}

const AVInputFormat ff_pjs_demuxer = {
    .name           = "pjs",
    .long_name      = NULL_IF_CONFIG_SMALL("PJS (Phoenix Japanimation Society) subtitles"),
    .priv_data_size = sizeof(PJSContext),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = pjs_probe,
    .read_header    = pjs_read_header,
    .extensions     = "pjs",
    .read_packet    = ff_subtitles_read_packet,
    .read_seek2     = ff_subtitles_read_seek,
    .read_close     = ff_subtitles_read_close,
};
