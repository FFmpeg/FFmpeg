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
 * WebVTT subtitle decoder
 * @see https://www.w3.org/TR/webvtt1/
 * @todo need to support extended markups and cue settings
 */

#include "avcodec.h"
#include "ass.h"
#include "codec_internal.h"
#include "libavutil/bprint.h"
#include "libavutil/mathematics.h"

static const struct {
    const char *from;
    const char *to;
} webvtt_tag_replace[] = {
    {"{", "\\{{}"}, {"\\", "\\\xe2\x81\xa0"}, // escape to avoid ASS markup conflicts
    {"&gt;", ">"}, {"&lt;", "<"},
    {"&lrm;", "\xe2\x80\x8e"}, {"&rlm;", "\xe2\x80\x8f"},
    {"&amp;", "&"}, {"&nbsp;", "\\h"},
};
static const struct {
    const char from[6];
    const char to[6];
} webvtt_valid_tags[] = {
    {"i", "{\\i1}"}, {"/i", "{\\i0}"},
    {"b", "{\\b1}"}, {"/b", "{\\b0}"},
    {"u", "{\\u1}"}, {"/u", "{\\u0}"},
};

/* parse a WebVTT timestamp string (HH:MM:SS.mmm or MM:SS.mmm).
 * Returns milliseconds or -1 on failure. */
static int64_t parse_webvtt_timestamp(const char *buf)
{
    int h = 0, m = 0, s = 0, ms = 0;

    if (sscanf(buf, "%d:%2d:%2d.%3d", &h, &m, &s, &ms) == 4) {
        if (m > 59 || s > 59)
            return -1;
        return (int64_t)h * 3600000 + m * 60000 + s * 1000 + ms;
    }
    if (sscanf(buf, "%2d:%2d.%3d", &m, &s, &ms) == 3) {
        if (m > 59 || s > 59)
            return -1;
        return m * 60000 + s * 1000 + ms;
    }

    return -1;
}

/* validate a cue timestamp tag body: must be digits/colons/periods,
 * parseable, strictly within (cue_start, cue_end), and after prev_ts.
 * Returns 1 and writes to *ts_out on success, 0 on failure. */
static int read_cue_timestamp(const char *body, int len,
                              int64_t cue_start, int64_t cue_end,
                              int64_t prev_ts, int64_t *ts_out)
{
    int64_t ts;

    if (len < 1 || !av_isdigit(body[0]))
        return 0;
    if ((int)strspn(body, "0123456789:.") != len)
        return 0;

    ts = parse_webvtt_timestamp(body);
    if (ts <= cue_start || ts >= cue_end)
        return 0;
    if (prev_ts >= 0 && ts <= prev_ts)
        return 0;

    *ts_out = ts;
    return 1;
}

/* Append the pending segment text, prefixed by a {\kf} karaoke override when
 * dur_cs > 0.  A {\kf} must precede the text it times, so segments are buffered
 * in \seg and flushed once their duration (the span up to the next timestamp,
 * or the cue end) is known. */
static void flush_segment(AVBPrint *buf, AVBPrint *seg, int64_t dur_cs)
{
    if (dur_cs > 0)
        av_bprintf(buf, "{\\kf%"PRId64"}", dur_cs);
    av_bprintf(buf, "%s", seg->str);
    av_bprint_clear(seg);
}

static int webvtt_event_to_ass(AVBPrint *buf, const char *p,
                               int64_t cue_start_ms, int64_t cue_end_ms)
{
    int i, again = 0;
    int64_t prev_ts = -1, ts;
    int64_t start_cs = 0;       /* cs from cue start where the pending segment begins */
    AVBPrint seg;

    av_bprint_init(&seg, 0, AV_BPRINT_SIZE_UNLIMITED);

    while (*p) {
        if (*p == '<') {
            const char *tag_end = strchr(p, '>');
            ptrdiff_t len;
            if (!tag_end)
                break;
            len = tag_end - p + 1;

            /* A cue timestamp ends the pending segment; flush it with its own
             * duration (rounded against cue start, then differenced, so the
             * durations telescope to the cue length without drift). */
            if (len > 2 &&
                read_cue_timestamp(p + 1, (int)(len - 2),
                                   cue_start_ms, cue_end_ms, prev_ts, &ts)) {
                int64_t end_cs = (ts - cue_start_ms + 5) / 10;
                flush_segment(buf, &seg, end_cs - start_cs);
                start_cs = end_cs;
                prev_ts  = ts;
                p += len;
                again = 1;
                continue;
            }

            for (i = 0; i < FF_ARRAY_ELEMS(webvtt_valid_tags); i++) {
                const char *from = webvtt_valid_tags[i].from;
                if(!strncmp(p + 1, from, strlen(from))) {
                    av_bprintf(&seg, "%s", webvtt_valid_tags[i].to);
                    break;
                }
            }
            p += len;
            again = 1;
        }

        for (i = 0; i < FF_ARRAY_ELEMS(webvtt_tag_replace); i++) {
            const char *from = webvtt_tag_replace[i].from;
            const size_t len = strlen(from);
            if (!strncmp(p, from, len)) {
                av_bprintf(&seg, "%s", webvtt_tag_replace[i].to);
                p += len;
                again = 1;
                break;
            }
        }

        if (again) {
            again = 0;
            continue;
        }
        if (p[0] == '\n' && p[1])
            av_bprintf(&seg, "\\N");
        else if (*p != '\r')
            av_bprint_chars(&seg, *p, 1);
        p++;
    }

    /* Flush the final segment.  With no cue timestamp this is the whole cue,
     * emitted untimed (duration 0 -> no {\kf}). */
    flush_segment(buf, &seg,
                  prev_ts < 0 ? 0 : (cue_end_ms - cue_start_ms + 5) / 10 - start_cs);

    av_bprint_finalize(&seg, NULL);
    return 0;
}

static int webvtt_decode_frame(AVCodecContext *avctx, AVSubtitle *sub,
                               int *got_sub_ptr, const AVPacket *avpkt)
{
    int ret = 0;
    const char *ptr = avpkt->data;
    FFASSDecoderContext *s = avctx->priv_data;
    AVBPrint buf;
    AVRational ms = { 1, 1000 };
    int64_t start_ms = 0, end_ms = 0;

    /* Inline cue timestamps are absolute milliseconds on the media timeline.
     * Convert the packet timing to the same unit via pkt_timebase instead of
     * assuming a 1/1000 time base.  If the timing is unknown, leave both at 0
     * so every inline timestamp is rejected and the cue is emitted verbatim. */
    if (avpkt->pts != AV_NOPTS_VALUE && avctx->pkt_timebase.num) {
        start_ms = av_rescale_q(avpkt->pts, avctx->pkt_timebase, ms);
        end_ms   = start_ms + av_rescale_q(avpkt->duration, avctx->pkt_timebase, ms);
    }

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    if (ptr && avpkt->size > 0 &&
        !webvtt_event_to_ass(&buf, ptr, start_ms, end_ms))
        ret = ff_ass_add_rect(sub, buf.str, s->readorder++, 0, NULL, NULL);
    av_bprint_finalize(&buf, NULL);
    if (ret < 0)
        return ret;
    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

const FFCodec ff_webvtt_decoder = {
    .p.name         = "webvtt",
    CODEC_LONG_NAME("WebVTT subtitle"),
    .p.type         = AVMEDIA_TYPE_SUBTITLE,
    .p.id           = AV_CODEC_ID_WEBVTT,
    FF_CODEC_DECODE_SUB_CB(webvtt_decode_frame),
    .init           = ff_ass_subtitle_header_default,
    .flush          = ff_ass_decoder_flush,
    .priv_data_size = sizeof(FFASSDecoderContext),
};
