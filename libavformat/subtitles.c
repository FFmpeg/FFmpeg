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
#include "avio_internal.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"

void ff_text_init_avio(void *s, FFTextReader *r, AVIOContext *pb)
{
    int i;
    r->pb = pb;
    r->buf_pos = r->buf_len = 0;
    r->type = FF_UTF_8;
    for (i = 0; i < 2; i++)
        r->buf[r->buf_len++] = avio_r8(r->pb);
    if (strncmp("\xFF\xFE", r->buf, 2) == 0) {
        r->type = FF_UTF16LE;
        r->buf_pos += 2;
    } else if (strncmp("\xFE\xFF", r->buf, 2) == 0) {
        r->type = FF_UTF16BE;
        r->buf_pos += 2;
    } else {
        r->buf[r->buf_len++] = avio_r8(r->pb);
        if (strncmp("\xEF\xBB\xBF", r->buf, 3) == 0) {
            // UTF8
            r->buf_pos += 3;
        }
    }
    if (s && (r->type == FF_UTF16LE || r->type == FF_UTF16BE))
        av_log(s, AV_LOG_INFO,
               "UTF16 is automatically converted to UTF8, do not specify a character encoding\n");
}

void ff_text_init_buf(FFTextReader *r, void *buf, size_t size)
{
    memset(&r->buf_pb, 0, sizeof(r->buf_pb));
    ffio_init_context(&r->buf_pb, buf, size, 0, NULL, NULL, NULL, NULL);
    ff_text_init_avio(NULL, r, &r->buf_pb);
}

int64_t ff_text_pos(FFTextReader *r)
{
    return avio_tell(r->pb) - r->buf_len + r->buf_pos;
}

int ff_text_r8(FFTextReader *r)
{
    uint32_t val;
    uint8_t tmp;
    if (r->buf_pos < r->buf_len)
        return r->buf[r->buf_pos++];
    if (r->type == FF_UTF16LE) {
        GET_UTF16(val, avio_rl16(r->pb), return 0;)
    } else if (r->type == FF_UTF16BE) {
        GET_UTF16(val, avio_rb16(r->pb), return 0;)
    } else {
        return avio_r8(r->pb);
    }
    if (!val)
        return 0;
    r->buf_pos = 0;
    r->buf_len = 0;
    PUT_UTF8(val, tmp, r->buf[r->buf_len++] = tmp;)
    return r->buf[r->buf_pos++]; // buf_len is at least 1
}

void ff_text_read(FFTextReader *r, char *buf, size_t size)
{
    for ( ; size > 0; size--)
        *buf++ = ff_text_r8(r);
}

int ff_text_eof(FFTextReader *r)
{
    return r->buf_pos >= r->buf_len && avio_feof(r->pb);
}

int ff_text_peek_r8(FFTextReader *r)
{
    int c;
    if (r->buf_pos < r->buf_len)
        return r->buf[r->buf_pos];
    c = ff_text_r8(r);
    if (!avio_feof(r->pb)) {
        r->buf_pos = 0;
        r->buf_len = 1;
        r->buf[0] = c;
    }
    return c;
}

AVPacket *ff_subtitles_queue_insert(FFDemuxSubtitlesQueue *q,
                                    const uint8_t *event, size_t len, int merge)
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
    if (s1->pts == s2->pts)
        return FFDIFFSIGN(s1->pos, s2->pos);
    return FFDIFFSIGN(s1->pts , s2->pts);
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

static void drop_dups(void *log_ctx, FFDemuxSubtitlesQueue *q)
{
    int i, drop = 0;

    for (i = 1; i < q->nb_subs; i++) {
        const int last_id = i - 1 - drop;
        const AVPacket *last = &q->subs[last_id];

        if (q->subs[i].pts        == last->pts &&
            q->subs[i].duration   == last->duration &&
            q->subs[i].stream_index == last->stream_index &&
            !strcmp(q->subs[i].data, last->data)) {

            av_packet_unref(&q->subs[i]);
            drop++;
        } else if (drop) {
            q->subs[last_id + 1] = q->subs[i];
            memset(&q->subs[i], 0, sizeof(q->subs[i])); // for safety
        }
    }

    if (drop) {
        q->nb_subs -= drop;
        av_log(log_ctx, AV_LOG_WARNING, "Dropping %d duplicated subtitle events\n", drop);
    }
}

void ff_subtitles_queue_finalize(void *log_ctx, FFDemuxSubtitlesQueue *q)
{
    int i;

    qsort(q->subs, q->nb_subs, sizeof(*q->subs),
          q->sort == SUB_SORT_TS_POS ? cmp_pkt_sub_ts_pos
                                     : cmp_pkt_sub_pos_ts);
    for (i = 0; i < q->nb_subs; i++)
        if (q->subs[i].duration < 0 && i < q->nb_subs - 1)
            q->subs[i].duration = q->subs[i + 1].pts - q->subs[i].pts;

    if (!q->keep_duplicates)
        drop_dups(log_ctx, q);
}

int ff_subtitles_queue_read_packet(FFDemuxSubtitlesQueue *q, AVPacket *pkt)
{
    AVPacket *sub = q->subs + q->current_sub_idx;

    if (q->current_sub_idx == q->nb_subs)
        return AVERROR_EOF;
    if (av_packet_ref(pkt, sub) < 0) {
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
        av_packet_unref(&q->subs[i]);
    av_freep(&q->subs);
    q->nb_subs = q->allocated_size = q->current_sub_idx = 0;
}

int ff_smil_extract_next_text_chunk(FFTextReader *tr, AVBPrint *buf, char *c)
{
    int i = 0;
    char end_chr;

    if (!*c) // cached char?
        *c = ff_text_r8(tr);
    if (!*c)
        return 0;

    end_chr = *c == '<' ? '>' : '<';
    do {
        av_bprint_chars(buf, *c, 1);
        *c = ff_text_r8(tr);
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
    const size_t len = strlen(attr);

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

void ff_subtitles_read_text_chunk(FFTextReader *tr, AVBPrint *buf)
{
    char eol_buf[5], last_was_cr = 0;
    int n = 0, i = 0, nb_eol = 0;

    av_bprint_clear(buf);

    for (;;) {
        char c = ff_text_r8(tr);

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

void ff_subtitles_read_chunk(AVIOContext *pb, AVBPrint *buf)
{
    FFTextReader tr;
    tr.buf_pos = tr.buf_len = 0;
    tr.type = 0;
    tr.pb = pb;
    ff_subtitles_read_text_chunk(&tr, buf);
}

ptrdiff_t ff_subtitles_read_line(FFTextReader *tr, char *buf, size_t size)
{
    size_t cur = 0;
    if (!size)
        return 0;
    while (cur + 1 < size) {
        unsigned char c = ff_text_r8(tr);
        if (!c)
            return ff_text_eof(tr) ? cur : AVERROR_INVALIDDATA;
        if (c == '\r' || c == '\n')
            break;
        buf[cur++] = c;
        buf[cur] = '\0';
    }
    if (ff_text_peek_r8(tr) == '\r')
        ff_text_r8(tr);
    if (ff_text_peek_r8(tr) == '\n')
        ff_text_r8(tr);
    return cur;
}
