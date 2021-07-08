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
 * SubViewer v1 subtitle demuxer
 */

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} SubViewer1Context;

static int subviewer1_probe(const AVProbeData *p)
{
    const unsigned char *ptr = p->buf;

    if (strstr(ptr, "******** START SCRIPT ********"))
        return AVPROBE_SCORE_EXTENSION;
    return 0;
}

static int subviewer1_read_header(AVFormatContext *s)
{
    int delay = 0;
    AVPacket *sub = NULL;
    SubViewer1Context *subviewer1 = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_SUBVIEWER1;

    while (!avio_feof(s->pb)) {
        char line[4096];
        int len = ff_get_line(s->pb, line, sizeof(line));
        int hh, mm, ss;

        if (!len)
            break;

        if (!strncmp(line, "[DELAY]", 7)) {
            ff_get_line(s->pb, line, sizeof(line));
            sscanf(line, "%d", &delay);
        }

        if (sscanf(line, "[%d:%d:%d]", &hh, &mm, &ss) == 3) {
            const int64_t pos = avio_tell(s->pb);
            int64_t pts_start = hh*3600LL + mm*60LL + ss + delay;

            len = ff_get_line(s->pb, line, sizeof(line));
            line[strcspn(line, "\r\n")] = 0;
            if (!*line) {
                if (sub)
                    sub->duration = pts_start - sub->pts;
            } else {
                sub = ff_subtitles_queue_insert(&subviewer1->q, line, len, 0);
                if (!sub)
                    return AVERROR(ENOMEM);
                sub->pos = pos;
                sub->pts = pts_start;
                sub->duration = -1;
            }
        }
    }

    ff_subtitles_queue_finalize(s, &subviewer1->q);
    return 0;
}

const AVInputFormat ff_subviewer1_demuxer = {
    .name           = "subviewer1",
    .long_name      = NULL_IF_CONFIG_SMALL("SubViewer v1 subtitle format"),
    .priv_data_size = sizeof(SubViewer1Context),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = subviewer1_probe,
    .read_header    = subviewer1_read_header,
    .extensions     = "sub",
    .read_packet    = ff_subtitles_read_packet,
    .read_seek2     = ff_subtitles_read_seek,
    .read_close     = ff_subtitles_read_close,
};
