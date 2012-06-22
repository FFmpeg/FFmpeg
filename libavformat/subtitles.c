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

void ff_subtitles_queue_clean(FFDemuxSubtitlesQueue *q)
{
    int i;

    for (i = 0; i < q->nb_subs; i++)
        av_destruct_packet(&q->subs[i]);
    av_freep(&q->subs);
    q->nb_subs = q->allocated_size = q->current_sub_idx = 0;
}
