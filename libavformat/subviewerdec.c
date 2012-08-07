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
 * SubViewer subtitle demuxer
 * @see https://en.wikipedia.org/wiki/SubViewer
 */

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} SubViewerContext;

static int subviewer_probe(AVProbeData *p)
{
    char c;
    const unsigned char *ptr = p->buf;

    if (AV_RB24(ptr) == 0xEFBBBF)
        ptr += 3;  /* skip UTF-8 BOM */
    if (sscanf(ptr, "%*u:%*u:%*u.%*u,%*u:%*u:%*u.%*u%c", &c) == 1)
        return AVPROBE_SCORE_MAX/2;
    if (!strncmp(ptr, "[INFORMATION]", 13))
        return AVPROBE_SCORE_MAX/3;
    return 0;
}

static int read_ts(const char *s, int64_t *start, int *duration)
{
    int64_t end;
    int hh1, mm1, ss1, ms1;
    int hh2, mm2, ss2, ms2;

    if (sscanf(s, "%u:%u:%u.%u,%u:%u:%u.%u",
               &hh1, &mm1, &ss1, &ms1, &hh2, &mm2, &ss2, &ms2) == 8) {
        end    = (hh2*3600 + mm2*60 + ss2) * 100 + ms2;
        *start = (hh1*3600 + mm1*60 + ss1) * 100 + ms1;
        *duration = end - *start;
        return 0;
    }
    return -1;
}

static int subviewer_read_header(AVFormatContext *s)
{
    SubViewerContext *subviewer = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    AVBPrint header;
    int res = 0;

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 100);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id   = AV_CODEC_ID_SUBVIEWER;

    av_bprint_init(&header, 0, AV_BPRINT_SIZE_UNLIMITED);

    while (!url_feof(s->pb)) {
        char line[2048];
        const int64_t pos = avio_tell(s->pb);
        int len = ff_get_line(s->pb, line, sizeof(line));

        if (!len)
            break;

        if (line[0] == '[' && strncmp(line, "[br]", 4)) {

            /* ignore event style, XXX: add to side_data? */
            if (strstr(line, "[COLF]") || strstr(line, "[SIZE]") ||
                strstr(line, "[FONT]") || strstr(line, "[STYLE]"))
                continue;

            if (!st->codec->extradata) { // header not finalized yet
                av_bprintf(&header, "%s", line);
                if (!strncmp(line, "[END INFORMATION]", 17) || !strncmp(line, "[SUBTITLE]", 10)) {
                    /* end of header */
                    av_bprint_finalize(&header, (char **)&st->codec->extradata);
                    if (!st->codec->extradata) {
                        res = AVERROR(ENOMEM);
                        goto end;
                    }
                    st->codec->extradata_size = header.len + 1;
                } else if (strncmp(line, "[INFORMATION]", 13)) {
                    /* assume file metadata at this point */
                    int i, j = 0;
                    char key[32], value[128];

                    for (i = 1; i < sizeof(key) - 1 && line[i] && line[i] != ']'; i++)
                        key[i - 1] = av_tolower(line[i]);
                    key[i - 1] = 0;

                    if (line[i] == ']')
                        i++;
                    while (line[i] == ' ')
                        i++;
                    while (j < sizeof(value) - 1 && line[i] && !strchr("]\r\n", line[i]))
                        value[j++] = line[i++];
                    value[j] = 0;

                    av_dict_set(&s->metadata, key, value, 0);
                }
            }
        } else {
            int64_t pts_start = AV_NOPTS_VALUE;
            int duration = -1;
            int timed_line = !read_ts(line, &pts_start, &duration);
            AVPacket *sub;

            sub = ff_subtitles_queue_insert(&subviewer->q, line, len, !timed_line);
            if (!sub) {
                res = AVERROR(ENOMEM);
                goto end;
            }
            if (timed_line) {
                sub->pos = pos;
                sub->pts = pts_start;
                sub->duration = duration;
            }
        }
    }

    ff_subtitles_queue_finalize(&subviewer->q);

end:
    av_bprint_finalize(&header, NULL);
    return res;
}

static int subviewer_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SubViewerContext *subviewer = s->priv_data;
    return ff_subtitles_queue_read_packet(&subviewer->q, pkt);
}

static int subviewer_read_close(AVFormatContext *s)
{
    SubViewerContext *subviewer = s->priv_data;
    ff_subtitles_queue_clean(&subviewer->q);
    return 0;
}

AVInputFormat ff_subviewer_demuxer = {
    .name           = "subviewer",
    .long_name      = NULL_IF_CONFIG_SMALL("SubViewer subtitle format"),
    .priv_data_size = sizeof(SubViewerContext),
    .read_probe     = subviewer_probe,
    .read_header    = subviewer_read_header,
    .read_packet    = subviewer_read_packet,
    .read_close     = subviewer_read_close,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "sub",
};
