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
 * WebVTT subtitle demuxer
 * @see http://dev.w3.org/html5/webvtt/
 */

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} WebVTTContext;

static int webvtt_probe(AVProbeData *p)
{
    const uint8_t *ptr = p->buf;

    if (AV_RB24(ptr) == 0xEFBBBF)
        ptr += 3;  /* skip UTF-8 BOM */
    if (!strncmp(ptr, "WEBVTT", 6) &&
        (!ptr[6] || strchr("\n\r\t ", ptr[6])))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int64_t read_ts(const char *s)
{
    int hh, mm, ss, ms;
    if (sscanf(s, "%u:%u:%u.%u", &hh, &mm, &ss, &ms) == 4) return (hh*3600 + mm*60 + ss) * 1000 + ms;
    if (sscanf(s,    "%u:%u.%u",      &mm, &ss, &ms) == 3) return (          mm*60 + ss) * 1000 + ms;
    return AV_NOPTS_VALUE;
}

static int64_t extract_cue(AVBPrint *buf, AVIOContext *pb)
{
    int prev_chr_is_eol = 0;
    int64_t pos = avio_tell(pb);

    av_bprint_clear(buf);
    for (;;) {
        char c = avio_r8(pb);
        if (!c)
            break;
        if (c == '\r' || c == '\n') {
            if (prev_chr_is_eol)
                break;
            prev_chr_is_eol = (c == '\n');
        } else
            prev_chr_is_eol = 0;
        if (c != '\r')
            av_bprint_chars(buf, c, 1);
    }
    av_bprint_chars(buf, '\0', 1);
    return pos;
}

static int webvtt_read_header(AVFormatContext *s)
{
    WebVTTContext *webvtt = s->priv_data;
    AVBPrint header, cue;
    int res = 0;
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codec->codec_id   = AV_CODEC_ID_WEBVTT;

    av_bprint_init(&header, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&cue,    0, AV_BPRINT_SIZE_UNLIMITED);

    for (;;) {
        int i, len;
        int64_t pos = extract_cue(&cue, s->pb);
        AVPacket *sub;
        const char *p = cue.str;
        const char *identifier = p;
        //const char *settings = NULL;
        int64_t ts_start, ts_end;

        if (!*p) // EOF
            break;

        /* ignore header chunk */
        if (!strncmp(p, "\xEF\xBB\xBFWEBVTT", 9) ||
            !strncmp(p, "WEBVTT", 6))
            continue;

        /* optional cue identifier (can be a number like in SRT or some kind of
         * chaptering id), silently skip it */
        for (i = 0; p[i] && p[i] != '\n'; i++) {
            if (!strncmp(p + i, "-->", 3)) {
                identifier = NULL;
                break;
            }
        }
        if (identifier)
            p += strcspn(p, "\n");

        /* cue timestamps */
        if ((ts_start = read_ts(p)) == AV_NOPTS_VALUE)
            break;
        if (!(p = strstr(p, "-->")))
            break;
        p += 3;
        do p++; while (*p == ' ' || *p == '\t');
        if ((ts_end = read_ts(p)) == AV_NOPTS_VALUE)
            break;

        /* optional cue settings, TODO: store in side_data */
        p += strcspn(p, "\n\t ");
        while (*p == '\t' || *p == ' ')
            p++;
        if (*p != '\n') {
            //settings = p;
            p += strcspn(p, "\n");
        }
        if (*p == '\n')
            p++;

        /* create packet */
        len = cue.str + cue.len - p - 1;
        sub = ff_subtitles_queue_insert(&webvtt->q, p, len, 0);
        if (!sub) {
            res = AVERROR(ENOMEM);
            goto end;
        }
        sub->pos = pos;
        sub->pts = ts_start;
        sub->duration = ts_end - ts_start;
    }

    ff_subtitles_queue_finalize(&webvtt->q);

end:
    av_bprint_finalize(&cue,    NULL);
    av_bprint_finalize(&header, NULL);
    return res;
}

static int webvtt_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    WebVTTContext *webvtt = s->priv_data;
    return ff_subtitles_queue_read_packet(&webvtt->q, pkt);
}

static int webvtt_read_close(AVFormatContext *s)
{
    WebVTTContext *webvtt = s->priv_data;
    ff_subtitles_queue_clean(&webvtt->q);
    return 0;
}

AVInputFormat ff_webvtt_demuxer = {
    .name           = "webvtt",
    .long_name      = NULL_IF_CONFIG_SMALL("WebVTT subtitle"),
    .priv_data_size = sizeof(WebVTTContext),
    .read_probe     = webvtt_probe,
    .read_header    = webvtt_read_header,
    .read_packet    = webvtt_read_packet,
    .read_close     = webvtt_read_close,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "vtt",
};
