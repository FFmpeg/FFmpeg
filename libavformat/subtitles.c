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

#include "avformat.h"
#include "subtitles.h"
#include "libavutil/avstring.h"

AVPacket *ff_subtitles_queue_insert(FFDemuxSubtitlesQueue *q,
                                    const uint8_t *event, int len, int merge)
{
    AVPacket *subs, *sub;

    if (merge && q->nb_subs > 0) {
        /* merge with previous event */

        int old_len;
        sub = &q->subs[q->nb_subs - 1];
        old_len = sub->size;
        if (av_grow_packet(sub, len) < 0)
            return NULL;
        memcpy(sub->data + old_len, event, len);
    } else {
        /* new event */

        if (q->nb_subs >= INT_MAX/sizeof(*q->subs) - 1)
            return NULL;
        subs = av_fast_realloc(q->subs, &q->allocated_size,
                               (q->nb_subs + 1) * sizeof(*q->subs));
        if (!subs)
            return NULL;
        q->subs = subs;
        sub = &subs[q->nb_subs++];
        if (av_new_packet(sub, len) < 0)
            return NULL;
        sub->destruct = NULL;
        sub->flags |= AV_PKT_FLAG_KEY;
        sub->pts = sub->dts = 0;
        memcpy(sub->data, event, len);
    }
    return sub;
}

static int cmp_pkt_sub(const void *a, const void *b)
{
    const AVPacket *s1 = a;
    const AVPacket *s2 = b;
    if (s1->pts == s2->pts) {
        if (s1->pos == s2->pos)
            return 0;
        return s1->pos > s2->pos ? 1 : -1;
    }
    return s1->pts > s2->pts ? 1 : -1;
}

void ff_subtitles_queue_finalize(FFDemuxSubtitlesQueue *q)
{
    int i;

    qsort(q->subs, q->nb_subs, sizeof(*q->subs), cmp_pkt_sub);
    for (i = 0; i < q->nb_subs; i++)
        if (q->subs[i].duration == -1 && i < q->nb_subs - 1)
            q->subs[i].duration = q->subs[i + 1].pts - q->subs[i].pts;
}

int ff_subtitles_queue_read_packet(FFDemuxSubtitlesQueue *q, AVPacket *pkt)
{
    AVPacket *sub = q->subs + q->current_sub_idx;

    if (q->current_sub_idx == q->nb_subs)
        return AVERROR_EOF;
    *pkt = *sub;
    pkt->dts = pkt->pts;
    q->current_sub_idx++;
    return 0;
}

int ff_subtitles_queue_seek(FFDemuxSubtitlesQueue *q, AVFormatContext *s, int stream_index,
                            int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    if (flags & AVSEEK_FLAG_BYTE) {
        return AVERROR(ENOSYS);
    } else if (flags & AVSEEK_FLAG_FRAME) {
        if (ts < 0 || ts >= q->nb_subs)
            return AVERROR(ERANGE);
        q->current_sub_idx = ts;
    } else {
        int i, idx = -1;
        int64_t min_ts_diff = INT64_MAX;
        int64_t ts_selected;
        /* TODO: q->subs[] is sorted by pts so we could do a binary search */
        for (i = 0; i < q->nb_subs; i++) {
            int64_t pts = q->subs[i].pts;
            uint64_t ts_diff = FFABS(pts - ts);
            if (pts >= min_ts && pts <= max_ts && ts_diff < min_ts_diff) {
                min_ts_diff = ts_diff;
                idx = i;
            }
        }
        if (idx < 0)
            return AVERROR(ERANGE);
        /* look back in the latest subtitles for overlapping subtitles */
        ts_selected = q->subs[idx].pts;
        for (i = idx - 1; i >= 0; i--) {
            if (q->subs[i].duration <= 0)
                continue;
            if (q->subs[i].pts > ts_selected - q->subs[i].duration)
                idx = i;
            else
                break;
        }
        q->current_sub_idx = idx;
    }
    return 0;
}

void ff_subtitles_queue_clean(FFDemuxSubtitlesQueue *q)
{
    int i;

    for (i = 0; i < q->nb_subs; i++)
        av_destruct_packet(&q->subs[i]);
    av_freep(&q->subs);
    q->nb_subs = q->allocated_size = q->current_sub_idx = 0;
}

int ff_smil_extract_next_chunk(AVIOContext *pb, AVBPrint *buf, char *c)
{
    int i = 0;
    char end_chr;

    if (!*c) // cached char?
        *c = avio_r8(pb);
    if (!*c)
        return 0;

    end_chr = *c == '<' ? '>' : '<';
    do {
        av_bprint_chars(buf, *c, 1);
        *c = avio_r8(pb);
        i++;
    } while (*c != end_chr && *c);
    if (end_chr == '>') {
        av_bprint_chars(buf, '>', 1);
        *c = 0;
    }
    return i;
}

const char *ff_smil_get_attr_ptr(const char *s, const char *attr)
{
    int in_quotes = 0;
    const int len = strlen(attr);

    while (*s) {
        while (*s) {
            if (!in_quotes && isspace(*s))
                break;
            in_quotes ^= *s == '"'; // XXX: support escaping?
            s++;
        }
        while (isspace(*s))
            s++;
        if (!av_strncasecmp(s, attr, len) && s[len] == '=')
            return s + len + 1 + (s[len + 1] == '"');
    }
    return NULL;
}

static inline int is_eol(char c)
{
    return c == '\r' || c == '\n';
}

void ff_subtitles_read_chunk(AVIOContext *pb, AVBPrint *buf)
{
    char eol_buf[5];
    int n = 0, i = 0, nb_eol = 0;

    av_bprint_clear(buf);

    for (;;) {
        char c = avio_r8(pb);

        if (!c)
            break;

        /* ignore all initial line breaks */
        if (n == 0 && is_eol(c))
            continue;

        /* line break buffering: we don't want to add the trailing \r\n */
        if (is_eol(c)) {
            nb_eol += c == '\n';
            if (nb_eol == 2)
                break;
            eol_buf[i++] = c;
            if (i == sizeof(eol_buf) - 1)
                break;
            continue;
        }

        /* only one line break followed by data: we flush the line breaks
         * buffer */
        if (i) {
            eol_buf[i] = 0;
            av_bprintf(buf, "%s", eol_buf);
            i = nb_eol = 0;
        }

        av_bprint_chars(buf, c, 1);
        n++;
    }
}
