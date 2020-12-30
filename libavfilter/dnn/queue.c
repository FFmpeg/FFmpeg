/*
 * Copyright (c) 2020
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

#include <stdio.h>
#include "queue.h"
#include "libavutil/mem.h"
#include "libavutil/avassert.h"

typedef struct FFQueueEntry FFQueueEntry;

struct FFQueueEntry {
    void *value;
    FFQueueEntry *prev;
    FFQueueEntry *next;
};

struct FFQueue {
    FFQueueEntry *head;
    FFQueueEntry *tail;
    size_t length;
};

static inline FFQueueEntry *create_entry(void *val)
{
    FFQueueEntry *entry = av_malloc(sizeof(*entry));
    if (entry)
        entry->value = val;
    return entry;
}

FFQueue* ff_queue_create(void)
{
    FFQueue *q = av_malloc(sizeof(*q));
    if (!q)
        return NULL;

    q->head = create_entry(q);
    q->tail = create_entry(q);

    if (!q->head || !q->tail) {
        av_freep(&q->head);
        av_freep(&q->tail);
        av_freep(&q);
        return NULL;
    }

    q->head->next = q->tail;
    q->tail->prev = q->head;
    q->head->prev = NULL;
    q->tail->next = NULL;
    q->length = 0;

    return q;
}

void ff_queue_destroy(FFQueue *q)
{
    FFQueueEntry *entry;
    if (!q)
        return;

    entry = q->head;
    while (entry != NULL) {
        FFQueueEntry *temp = entry;
        entry = entry->next;
        av_freep(&temp);
    }

    av_freep(&q);
}

size_t ff_queue_size(FFQueue *q)
{
     return q ? q->length : 0;
}

void *ff_queue_peek_front(FFQueue *q)
{
    if (!q || q->length == 0)
        return NULL;

    return q->head->next->value;
}

void *ff_queue_peek_back(FFQueue *q)
{
    if (!q || q->length == 0)
        return NULL;

    return q->tail->prev->value;
}

int ff_queue_push_front(FFQueue *q, void *v)
{
    FFQueueEntry *new_entry;
    FFQueueEntry *original_next;
    if (!q)
        return 0;

    new_entry = create_entry(v);
    if (!new_entry)
        return -1;
    original_next = q->head->next;

    q->head->next = new_entry;
    original_next->prev = new_entry;
    new_entry->prev = q->head;
    new_entry->next = original_next;
    q->length++;

    return q->length;
}

int ff_queue_push_back(FFQueue *q, void *v)
{
    FFQueueEntry *new_entry;
    FFQueueEntry *original_prev;
    if (!q)
        return 0;

    new_entry = create_entry(v);
    if (!new_entry)
        return -1;
    original_prev = q->tail->prev;

    q->tail->prev = new_entry;
    original_prev->next = new_entry;
    new_entry->next = q->tail;
    new_entry->prev = original_prev;
    q->length++;

    return q->length;
}

void *ff_queue_pop_front(FFQueue *q)
{
    FFQueueEntry *front;
    FFQueueEntry *new_head_next;
    void *ret;

    if (!q || q->length == 0)
        return NULL;

    front = q->head->next;
    new_head_next = front->next;
    ret = front->value;

    q->head->next = new_head_next;
    new_head_next->prev = q->head;

    av_freep(&front);
    q->length--;
    return ret;
}

void *ff_queue_pop_back(FFQueue *q)
{
    FFQueueEntry *back;
    FFQueueEntry *new_tail_prev;
    void *ret;

    if (!q || q->length == 0)
        return NULL;

    back = q->tail->prev;
    new_tail_prev = back->prev;
    ret = back->value;

    q->tail->prev = new_tail_prev;
    new_tail_prev->next = q->tail;

    av_freep(&back);
    q->length--;
    return ret;
}
