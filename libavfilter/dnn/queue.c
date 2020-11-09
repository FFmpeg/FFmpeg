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

typedef struct _queue_entry queue_entry;

typedef struct _queue {
    queue_entry *head;
    queue_entry *tail;
    size_t length;
}queue;

typedef struct _queue_entry {
    void *value;
    queue_entry *prev;
    queue_entry *next;
} queue_entry;

static inline queue_entry *create_entry(void *val)
{
    queue_entry *entry = av_malloc(sizeof(*entry));
    av_assert0(entry != NULL);
    entry->value = val;
    return entry;
}

queue* queue_create(void)
{
    queue *q = av_malloc(sizeof(*q));
    if (!q)
        return NULL;

    q->head = create_entry(q);
    q->tail = create_entry(q);
    q->head->next = q->tail;
    q->tail->prev = q->head;
    q->head->prev = NULL;
    q->tail->next = NULL;
    q->length = 0;

    return q;
}

void queue_destroy(queue *q)
{
    queue_entry *entry;
    if (!q)
        return;

    entry = q->head;
    while (entry != NULL) {
        queue_entry *temp = entry;
        entry = entry->next;
        av_freep(&temp);
    }

    av_freep(&q);
}

size_t queue_size(queue *q)
{
     return q ? q->length : 0;
}

void *queue_peek_front(queue *q)
{
    if (!q || q->length == 0)
        return NULL;

    return q->head->next->value;
}

void *queue_peek_back(queue *q)
{
    if (!q || q->length == 0)
        return NULL;

    return q->tail->prev->value;
}

void queue_push_front(queue *q, void *v)
{
    queue_entry *new_entry;
    queue_entry *original_next;
    if (!q)
        return;

    new_entry = create_entry(v);
    original_next = q->head->next;

    q->head->next = new_entry;
    original_next->prev = new_entry;
    new_entry->prev = q->head;
    new_entry->next = original_next;
    q->length++;
}

void queue_push_back(queue *q, void *v)
{
    queue_entry *new_entry;
    queue_entry *original_prev;
    if (!q)
        return;

    new_entry = create_entry(v);
    original_prev = q->tail->prev;

    q->tail->prev = new_entry;
    original_prev->next = new_entry;
    new_entry->next = q->tail;
    new_entry->prev = original_prev;
    q->length++;
}

void *queue_pop_front(queue *q)
{
    queue_entry *front;
    queue_entry *new_head_next;
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

void *queue_pop_back(queue *q)
{
    queue_entry *back;
    queue_entry *new_tail_prev;
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
