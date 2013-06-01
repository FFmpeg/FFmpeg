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
    if (sscanf(s, "%u:%u:%u.%u", &hh, &mm, &ss, &ms) == 4) return (hh*3600LL + mm*60LL + ss) * 1000LL + ms;
    if (sscanf(s,    "%u:%u.%u",      &mm, &ss, &ms) == 3) return (            mm*60LL + ss) * 1000LL + ms;
    return AV_NOPTS_VALUE;
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
        int i;
        int64_t pos;
        AVPacket *sub;
        const char *p, *identifier, *settings;
        int identifier_len, settings_len;
        int64_t ts_start, ts_end;

        ff_subtitles_read_chunk(s->pb, &cue);

        if (!cue.len)
            break;

        p = identifier = cue.str;
        pos = avio_tell(s->pb);

        /* ignore header chunk */
        if (!strncmp(p, "\xEF\xBB\xBFWEBVTT", 9) ||
            !strncmp(p, "WEBVTT", 6))
            continue;

        /* optional cue identifier (can be a number like in SRT or some kind of
         * chaptering id) */
        for (i = 0; p[i] && p[i] != '\n' && p[i] != '\r'; i++) {
            if (!strncmp(p + i, "-->", 3)) {
                identifier = NULL;
                break;
            }
        }
        if (!identifier)
            identifier_len = 0;
        else {
            identifier_len = strcspn(p, "\r\n");
            p += identifier_len;
            if (*p == '\r')
                p++;
            if (*p == '\n')
                p++;
        }

        /* cue timestamps */
        if ((ts_start = read_ts(p)) == AV_NOPTS_VALUE)
            break;
        if (!(p = strstr(p, "-->")))
            break;
        p += 3;
        do p++; while (*p == ' ' || *p == '\t');
        if ((ts_end = read_ts(p)) == AV_NOPTS_VALUE)
            break;

        /* optional cue settings */
        p += strcspn(p, "\n\t ");
        while (*p == '\t' || *p == ' ')
            p++;
        settings = p;
        settings_len = strcspn(p, "\r\n");
        p += settings_len;
        if (*p == '\r')
            p++;
        if (*p == '\n')
            p++;

        /* create packet */
        sub = ff_subtitles_queue_insert(&webvtt->q, p, strlen(p), 0);
        if (!sub) {
            res = AVERROR(ENOMEM);
            goto end;
        }
        sub->pos = pos;
        sub->pts = ts_start;
        sub->duration = ts_end - ts_start;

#define SET_SIDE_DATA(name, type) do {                                  \
    if (name##_len) {                                                   \
        uint8_t *buf = av_packet_new_side_data(sub, type, name##_len);  \
        if (!buf) {                                                     \
            res = AVERROR(ENOMEM);                                      \
            goto end;                                                   \
        }                                                               \
        memcpy(buf, name, name##_len);                                  \
    }                                                                   \
} while (0)

        SET_SIDE_DATA(identifier, AV_PKT_DATA_WEBVTT_IDENTIFIER);
        SET_SIDE_DATA(settings,   AV_PKT_DATA_WEBVTT_SETTINGS);
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

static int webvtt_read_seek(AVFormatContext *s, int stream_index,
                            int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    WebVTTContext *webvtt = s->priv_data;
    return ff_subtitles_queue_seek(&webvtt->q, s, stream_index,
                                   min_ts, ts, max_ts, flags);
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
    .read_seek2     = webvtt_read_seek,
    .read_close     = webvtt_read_close,
    .extensions     = "vtt",
};
