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


#ifndef AVFILTER_DNN_QUEUE_H
#define AVFILTER_DNN_QUEUE_H

typedef struct _queue queue;

queue *queue_create(void);
void queue_destroy(queue *q);

size_t queue_size(queue *q);

void *queue_peek_front(queue *q);
void *queue_peek_back(queue *q);

void queue_push_front(queue *q, void *v);
void queue_push_back(queue *q, void *v);

void *queue_pop_front(queue *q);
void *queue_pop_back(queue *q);

#endif
