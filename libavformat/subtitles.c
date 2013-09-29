/*
 * Copyright (c) 2012-2013 Clément Bœsch <u pkh me>
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
#include "libavutil/avassert.h"
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
        sub->flags |= AV_PKT_FLAG_KEY;
        sub->pts = sub->dts = 0;
        memcpy(sub->data, event, len);
    }
    return sub;
}

static int cmp_pkt_sub_ts_pos(const void *a, const void *b)
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

static int cmp_pkt_sub_pos_ts(const void *a, const void *b)
{
    const AVPacket *s1 = a;
    const AVPacket *s2 = b;
    if (s1->pos == s2->pos) {
        if (s1->pts == s2->pts)
            return 0;
        return s1->pts > s2->pts ? 1 : -1;
    }
    return s1->pos > s2->pos ? 1 : -1;
}

void ff_subtitles_queue_finalize(FFDemuxSubtitlesQueue *q)
{
    int i;

    qsort(q->subs, q->nb_subs, sizeof(*q->subs),
          q->sort == SUB_SORT_TS_POS ? cmp_pkt_sub_ts_pos
                                     : cmp_pkt_sub_pos_ts);
    for (i = 0; i < q->nb_subs; i++)
        if (q->subs[i].duration == -1 && i < q->nb_subs - 1)
            q->subs[i].duration = q->subs[i + 1].pts - q->subs[i].pts;
}

int ff_subtitles_queue_read_packet(FFDemuxSubtitlesQueue *q, AVPacket *pkt)
{
    AVPacket *sub = q->subs + q->current_sub_idx;

    if (q->current_sub_idx == q->nb_subs)
        return AVERROR_EOF;
    if (av_copy_packet(pkt, sub) < 0) {
        return AVERROR(ENOMEM);
    }

    pkt->dts = pkt->pts;
    q->current_sub_idx++;
    return 0;
}

static int search_sub_ts(const FFDemuxSubtitlesQueue *q, int64_t ts)
{
    int s1 = 0, s2 = q->nb_subs - 1;

    if (s2 < s1)
        return AVERROR(ERANGE);

    for (;;) {
        int mid;

        if (s1 == s2)
            return s1;
        if (s1 == s2 - 1)
            return q->subs[s1].pts <= q->subs[s2].pts ? s1 : s2;
        mid = (s1 + s2) / 2;
        if (q->subs[mid].pts <= ts)
            s1 = mid;
        else
            s2 = mid;
    }
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
        int i, idx = search_sub_ts(q, ts);
        int64_t ts_selected;

        if (idx < 0)
            return idx;
        for (i = idx; i < q->nb_subs && q->subs[i].pts < min_ts; i++)
            if (stream_index == -1 || q->subs[i].stream_index == stream_index)
                idx = i;
        for (i = idx; i > 0 && q->subs[i].pts > max_ts; i--)
            if (stream_index == -1 || q->subs[i].stream_index == stream_index)
                idx = i;

        ts_selected = q->subs[idx].pts;
        if (ts_selected < min_ts || ts_selected > max_ts)
            return AVERROR(ERANGE);

        /* look back in the latest subtitles for overlapping subtitles */
        for (i = idx - 1; i >= 0; i--) {
            int64_t pts = q->subs[i].pts;
            if (q->subs[i].duration <= 0 ||
                (stream_index != -1 && q->subs[i].stream_index != stream_index))
                continue;
            if (pts >= min_ts && pts > ts_selected - q->subs[i].duration)
                idx = i;
            else
                break;
        }

        /* If the queue is used to store multiple subtitles streams (like with
         * VobSub) and the stream index is not specified, we need to make sure
         * to focus on the smallest file position offset for a same timestamp;
         * queue is ordered by pts and then filepos, so we can take the first
         * entry for a given timestamp. */
        if (stream_index == -1)
            while (idx > 0 && q->subs[idx - 1].pts == q->subs[idx].pts)
                idx--;

        q->current_sub_idx = idx;
    }
    return 0;
}

void ff_subtitles_queue_clean(FFDemuxSubtitlesQueue *q)
{
    int i;

    for (i = 0; i < q->nb_subs; i++)
        av_free_packet(&q->subs[i]);
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
            if (!in_quotes && av_isspace(*s))
                break;
            in_quotes ^= *s == '"'; // XXX: support escaping?
            s++;
        }
        while (av_isspace(*s))
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
    char eol_buf[5], last_was_cr = 0;
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
            nb_eol += c == '\n' || last_was_cr;
            if (nb_eol == 2)
                break;
            eol_buf[i++] = c;
            if (i == sizeof(eol_buf) - 1)
                break;
            last_was_cr = c == '\r';
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
