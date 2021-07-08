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
#include "avio_internal.h"
#include "libavcodec/internal.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} SubViewerContext;

static int subviewer_probe(const AVProbeData *p)
{
    char c;
    const unsigned char *ptr = p->buf;

    if (AV_RB24(ptr) == 0xEFBBBF)
        ptr += 3;  /* skip UTF-8 BOM */
    if (sscanf(ptr, "%*u:%*u:%*u.%*u,%*u:%*u:%*u.%*u%c", &c) == 1)
        return AVPROBE_SCORE_EXTENSION;
    if (!strncmp(ptr, "[INFORMATION]", 13))
        return AVPROBE_SCORE_MAX/3;
    return 0;
}

static int read_ts(const char *s, int64_t *start, int *duration)
{
    int64_t end;
    int hh1, mm1, ss1, ms1;
    int hh2, mm2, ss2, ms2;
    int multiplier = 1;

    if (sscanf(s, "%u:%u:%u.%2u,%u:%u:%u.%2u",
               &hh1, &mm1, &ss1, &ms1, &hh2, &mm2, &ss2, &ms2) == 8) {
        multiplier = 10;
    } else if (sscanf(s, "%u:%u:%u.%1u,%u:%u:%u.%1u",
                      &hh1, &mm1, &ss1, &ms1, &hh2, &mm2, &ss2, &ms2) == 8) {
        multiplier = 100;
    }
    if (sscanf(s, "%u:%u:%u.%u,%u:%u:%u.%u",
               &hh1, &mm1, &ss1, &ms1, &hh2, &mm2, &ss2, &ms2) == 8) {
        ms1 = FFMIN(ms1, 999);
        ms2 = FFMIN(ms2, 999);
        end    = (hh2*3600LL + mm2*60LL + ss2) * 1000LL + ms2 * multiplier;
        *start = (hh1*3600LL + mm1*60LL + ss1) * 1000LL + ms1 * multiplier;
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
    int res = 0, new_event = 1;
    int64_t pts_start = AV_NOPTS_VALUE;
    int duration = -1;
    AVPacket *sub = NULL;

    if (!st)
        return AVERROR(ENOMEM);
    res = ffio_ensure_seekback(s->pb, 3);
    if (res < 0)
        return res;
    if (avio_rb24(s->pb) != 0xefbbbf)
        avio_seek(s->pb, -3, SEEK_CUR);
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_SUBVIEWER;

    av_bprint_init(&header, 0, AV_BPRINT_SIZE_UNLIMITED);

    while (!avio_feof(s->pb)) {
        char line[2048];
        int64_t pos = 0;
        int len = ff_get_line(s->pb, line, sizeof(line));

        if (!len)
            break;

        line[strcspn(line, "\r\n")] = 0;

        if (line[0] == '[' && strncmp(line, "[br]", 4)) {

            /* ignore event style, XXX: add to side_data? */
            if (strstr(line, "[COLF]") || strstr(line, "[SIZE]") ||
                strstr(line, "[FONT]") || strstr(line, "[STYLE]"))
                continue;

            if (!st->codecpar->extradata) { // header not finalized yet
                av_bprintf(&header, "%s\n", line);
                if (!strncmp(line, "[END INFORMATION]", 17) || !strncmp(line, "[SUBTITLE]", 10)) {
                    /* end of header */
                    res = ff_bprint_to_codecpar_extradata(st->codecpar, &header);
                    if (res < 0)
                        goto end;
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
                    while (j < sizeof(value) - 1 && line[i] && line[i] != ']')
                        value[j++] = line[i++];
                    value[j] = 0;

                    av_dict_set(&s->metadata, key, value, 0);
                }
            }
        } else if (read_ts(line, &pts_start, &duration) >= 0) {
            new_event = 1;
            pos = avio_tell(s->pb);
        } else if (*line) {
            if (pts_start == AV_NOPTS_VALUE) {
                res = AVERROR_INVALIDDATA;
                goto end;
            }
            if (!new_event) {
                sub = ff_subtitles_queue_insert(&subviewer->q, "\n", 1, 1);
                if (!sub) {
                    res = AVERROR(ENOMEM);
                    goto end;
                }
            }
            sub = ff_subtitles_queue_insert(&subviewer->q, line, strlen(line), !new_event);
            if (!sub) {
                res = AVERROR(ENOMEM);
                goto end;
            }
            if (new_event) {
                sub->pos = pos;
                sub->pts = pts_start;
                sub->duration = duration;
            }
            new_event = 0;
        }
    }

    ff_subtitles_queue_finalize(s, &subviewer->q);

end:
    av_bprint_finalize(&header, NULL);
    return res;
}

const AVInputFormat ff_subviewer_demuxer = {
    .name           = "subviewer",
    .long_name      = NULL_IF_CONFIG_SMALL("SubViewer subtitle format"),
    .priv_data_size = sizeof(SubViewerContext),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = subviewer_probe,
    .read_header    = subviewer_read_header,
    .extensions     = "sub",
    .read_packet    = ff_subtitles_read_packet,
    .read_seek2     = ff_subtitles_read_seek,
    .read_close     = ff_subtitles_read_close,
};
