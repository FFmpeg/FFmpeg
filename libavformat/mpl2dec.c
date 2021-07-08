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
 * MPL2 subtitles format demuxer
 */

#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} MPL2Context;

static int mpl2_probe(const AVProbeData *p)
{
    int i;
    char c;
    int64_t start, end;
    const unsigned char *ptr = p->buf;
    const unsigned char *ptr_end = ptr + p->buf_size;

    if (AV_RB24(ptr) == 0xefbbbf)
        ptr += 3;

    for (i = 0; i < 2; i++) {
        if (sscanf(ptr, "[%"SCNd64"][%"SCNd64"]%c", &start, &end, &c) != 3 &&
            sscanf(ptr, "[%"SCNd64"][]%c",          &start,       &c) != 2)
            return 0;
        ptr += ff_subtitles_next_line(ptr);
        if (ptr >= ptr_end)
            return 0;
    }
    return AVPROBE_SCORE_MAX;
}

static int read_ts(char **line, int64_t *pts_start, int64_t *duration)
{
    char c;
    int len;
    int64_t end;

    if (sscanf(*line, "[%"SCNd64"][]%c%n",
               pts_start, &c, &len) >= 2) {
        *duration = -1;
        *line += len - 1;
        return 0;
    }
    if (sscanf(*line, "[%"SCNd64"][%"SCNd64"]%c%n",
               pts_start, &end, &c, &len) >= 3) {
        if (end < *pts_start || end - (uint64_t)*pts_start > INT64_MAX) {
            *duration = -1;
        } else
            *duration = end - *pts_start;
        *line += len - 1;
        return 0;
    }
    return -1;
}

static int mpl2_read_header(AVFormatContext *s)
{
    MPL2Context *mpl2 = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 10);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_MPL2;

    if (avio_rb24(s->pb) != 0xefbbbf)
        avio_seek(s->pb, -3, SEEK_CUR);

    while (!avio_feof(s->pb)) {
        char line[4096];
        char *p = line;
        const int64_t pos = avio_tell(s->pb);
        int len = ff_get_line(s->pb, line, sizeof(line));
        int64_t pts_start;
        int64_t duration;

        if (!len)
            break;

        line[strcspn(line, "\r\n")] = 0;

        if (!read_ts(&p, &pts_start, &duration)) {
            AVPacket *sub;

            sub = ff_subtitles_queue_insert(&mpl2->q, p, strlen(p), 0);
            if (!sub)
                return AVERROR(ENOMEM);
            sub->pos = pos;
            sub->pts = pts_start;
            sub->duration = duration;
        }
    }

    ff_subtitles_queue_finalize(s, &mpl2->q);
    return 0;
}

const AVInputFormat ff_mpl2_demuxer = {
    .name           = "mpl2",
    .long_name      = NULL_IF_CONFIG_SMALL("MPL2 subtitles"),
    .priv_data_size = sizeof(MPL2Context),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = mpl2_probe,
    .read_header    = mpl2_read_header,
    .extensions     = "txt,mpl2",
    .read_packet    = ff_subtitles_read_packet,
    .read_seek2     = ff_subtitles_read_seek,
    .read_close     = ff_subtitles_read_close,
};
