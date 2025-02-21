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
#include "demux.h"
#include "internal.h"
#include "subtitles.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

typedef struct {
    const AVClass *class;
    FFDemuxSubtitlesQueue q;
    int kind;
} WebVTTContext;

static int webvtt_probe(const AVProbeData *p)
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
    AVBPrint cue;
    int res = 0;
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_WEBVTT;
    st->disposition |= webvtt->kind;

    av_bprint_init(&cue,    0, AV_BPRINT_SIZE_UNLIMITED);

    for (;;) {
        int i;
        int64_t pos;
        AVPacket *sub;
        const char *p, *identifier, *settings;
        size_t identifier_len, settings_len;
        int64_t ts_start, ts_end;

        res = ff_subtitles_read_chunk(s->pb, &cue);
        if (res < 0)
            goto end;

        if (!cue.len)
            break;

        p = identifier = cue.str;
        pos = avio_tell(s->pb);

        /* ignore header chunk */
        if (!strncmp(p, "\xEF\xBB\xBFWEBVTT", 9) ||
            !strncmp(p, "WEBVTT", 6) ||
            !strncmp(p, "STYLE", 5) ||
            !strncmp(p, "REGION", 6) ||
            !strncmp(p, "NOTE", 4))
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
        p += 2;
        do p++; while (*p == ' ' || *p == '\t');
        if ((ts_end = read_ts(p)) == AV_NOPTS_VALUE)
            break;

        /* optional cue settings */
        p += strcspn(p, "\n\r\t ");
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

    ff_subtitles_queue_finalize(s, &webvtt->q);

end:
    av_bprint_finalize(&cue,    NULL);
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

#define OFFSET(x) offsetof(WebVTTContext, x)
#define KIND_FLAGS AV_OPT_FLAG_SUBTITLE_PARAM|AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "kind", "Set kind of WebVTT track", OFFSET(kind), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, KIND_FLAGS, .unit = "webvtt_kind" },
        { "subtitles",    "WebVTT subtitles kind",    0, AV_OPT_TYPE_CONST, { .i64 = 0 },                           INT_MIN, INT_MAX, KIND_FLAGS, .unit = "webvtt_kind" },
        { "captions",     "WebVTT captions kind",     0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_CAPTIONS },     INT_MIN, INT_MAX, KIND_FLAGS, .unit = "webvtt_kind" },
        { "descriptions", "WebVTT descriptions kind", 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DESCRIPTIONS }, INT_MIN, INT_MAX, KIND_FLAGS, .unit = "webvtt_kind" },
        { "metadata",     "WebVTT metadata kind",     0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_METADATA },     INT_MIN, INT_MAX, KIND_FLAGS, .unit = "webvtt_kind" },
    { NULL }
};

static const AVClass webvtt_demuxer_class = {
    .class_name  = "WebVTT demuxer",
    .item_name   = av_default_item_name,
    .option      = options,
    .version     = LIBAVUTIL_VERSION_INT,
};

const FFInputFormat ff_webvtt_demuxer = {
    .p.name         = "webvtt",
    .p.long_name    = NULL_IF_CONFIG_SMALL("WebVTT subtitle"),
    .p.mime_type    = "text/vtt",
    .p.extensions   = "vtt,webvtt",
    .p.priv_class   = &webvtt_demuxer_class,
    .priv_data_size = sizeof(WebVTTContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = webvtt_probe,
    .read_header    = webvtt_read_header,
    .read_packet    = webvtt_read_packet,
    .read_seek2     = webvtt_read_seek,
    .read_close     = webvtt_read_close,
};
